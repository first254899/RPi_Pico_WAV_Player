/*------------------------------------------------------/
/ Copyright (c) 2026, first254899
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

// Host-side test of the ReadBuffer core0/core1 protocol (lib/PlayAudio/ReadBuffer.*)
// core1 runs as a std::thread, pico queue and FatFs are stubbed (test/stubs).
// The main thread plays the role of core0 and of the decoder in the I2S ISR,
// including its instant mute (isNearEmpty() / isFilled()).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <random>
#include <vector>

#include "fatfs_owner.h"
#include "ReadBuffer.h"

static int g_failed = 0;
static int g_checks = 0;
static std::atomic<const char*> g_stage{"init"};

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { g_failed++; printf("  FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #cond, g_stage.load()); } \
} while (0)

using Clock = std::chrono::steady_clock;

// any hang (e.g. reqBind() never returning) aborts the whole test
static void watchdog()
{
    const char* last = nullptr;
    auto since = Clock::now();
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const char* cur = g_stage.load();
        if (cur != last) { last = cur; since = Clock::now(); continue; }
        if (Clock::now() - since > std::chrono::seconds(20)) {
            printf("  FAIL: HANG in stage '%s'\n", cur);
            fflush(stdout);
            std::_Exit(2);
        }
    }
}

static std::vector<uint8_t> makeFile(size_t size, uint32_t seed)
{
    std::vector<uint8_t> v(size);
    std::mt19937 rng(seed);
    for (auto& b : v) { b = static_cast<uint8_t>(rng()); }
    return v;
}

struct Played {
    std::vector<uint8_t> data;
    bool eof = false;
    bool error = false;
    size_t tellAtEnd = 0;
};

// emulate PlayWav::decode() + PlayAudio::isMuteCondition() until end of stream
static Played consume(ReadBuffer* rb, size_t chunk, size_t maxBytes = SIZE_MAX)
{
    Played p;
    bool mute = false;
    while (p.data.size() < maxBytes) {
        if (!mute && rb->isNearEmpty()) {
            mute = true;
        } else if (mute && rb->isFilled()) {
            mute = false;
        }
        if (mute) { std::this_thread::sleep_for(std::chrono::microseconds(20)); continue; }
        size_t n = std::min({rb->getLeft(), chunk, maxBytes - p.data.size()});
        p.data.insert(p.data.end(), rb->buf(), rb->buf() + n);
        rb->shift(n);
        if (rb->isEof() && rb->getLeft() == 0) { p.eof = true; break; }
    }
    p.error = rb->isError();
    p.tellAtEnd = rb->tell();
    return p;
}

static int g_sectionFailed = 0;
static void section(const char* name)
{
    printf("%s %s\n", (g_failed == g_sectionFailed) ? "[ OK ]" : "[FAIL]", name);
    g_sectionFailed = g_failed;
}

static bool sameAs(const std::vector<uint8_t>& out, const std::vector<uint8_t>& file, size_t from, size_t to)
{
    return out.size() == to - from && std::equal(out.begin(), out.end(), file.begin() + from);
}

static void playRange(ReadBuffer* rb, const std::vector<uint8_t>& file, size_t start, size_t eod,
                      size_t chunk, unsigned delayUs, const char* stage)
{
    g_stage = stage;
    FIL fil;
    fil.file = &file;
    fil.readDelayUs = delayUs;
    rb->reqBind(&fil, start, eod);
    // FatFs ownership: core1 keeps it while more than the initial fill (the lookahead) is left to read
    if (start < eod && eod - start > 3456 * 8) { CHECK(fatfs_owner_core == 1); }
    Played p = consume(rb, chunk);
    // core1 handed FatFs back before core0 saw the end of data: core0 may e.g. look for the next track now
    CHECK(fatfs_owner_core == 0);
    FATFS_ASSERT_OWNER();
    size_t expectEnd = std::max(start, eod);
    CHECK(p.eof);
    CHECK(!p.error);
    CHECK(sameAs(p.data, file, std::min(start, expectEnd), expectEnd));
    CHECK(fil.maxReadEnd.load() <= eod);  // C1: never read at or beyond eodPos
    CHECK(p.tellAtEnd == expectEnd);
    rb->reqUnbind();
    CHECK(fatfs_owner_core == 0);
}

int main()
{
    std::thread(watchdog).detach();
    ReadBuffer* rb = ReadBuffer::getInstance();
    const size_t ONE = 3456;       // SECONDARY_BUFFER_SIZE
    const size_t LOOKAHEAD = ONE * 8;

    // whole files of every size class around the item / lookahead boundaries (H1: short files)
    {
        const size_t sizes[] = {1, 2, 100, ONE - 1, ONE, ONE + 1, LOOKAHEAD - 1, LOOKAHEAD, LOOKAHEAD + 1,
                                LOOKAHEAD + ONE + 7, 100000, 1000003};
        for (size_t s : sizes) {
            auto f = makeFile(s, static_cast<uint32_t>(s));
            playRange(rb, f, 0, s, 2304, 0, "whole file");
            playRange(rb, f, 0, s, 4608, 50, "whole file, slow card");
        }
        section("whole files (short files do not hang)");
    }

    // C1: data ends long before the end of file (short clip + trailing tag chunk), header skipped
    {
        auto f = makeFile(300000, 1);
        playRange(rb, f, 44, 1000, 2304, 0, "short data, trailing chunk");
        playRange(rb, f, 44, 44 + LOOKAHEAD - 4, 2304, 0, "data just below lookahead");
        playRange(rb, f, 44, 44 + LOOKAHEAD + 4, 2304, 30, "data just above lookahead");
        // resume near the end of data
        playRange(rb, f, 200000 - 8, 200000, 2304, 0, "resume 8 B before eod");
        playRange(rb, f, 200000 - ONE * 3 - 4, 200000, 2304, 0, "resume ~10 KB before eod");
        section("never reads beyond eodPos");
    }

    // nothing to read: start at/after eodPos gives an immediate EOF
    {
        auto f = makeFile(50000, 2);
        for (size_t start : {size_t(1000), size_t(1001), size_t(50000)}) {
            g_stage = "start >= eod";
            FIL fil; fil.file = &f;
            rb->reqBind(&fil, start, 1000);
            Played p = consume(rb, 2304);
            CHECK(p.eof);
            CHECK(!p.error);
            CHECK(p.data.empty());
            CHECK(fil.reads.load() == 0);
            rb->reqUnbind();
        }
        section("start >= eodPos -> zero-length EOF");
    }

    // H2: read error ends the stream, core1 keeps serving requests
    {
        auto f = makeFile(400000, 3);
        for (size_t failAt : {size_t(44), size_t(5000), size_t(LOOKAHEAD + 10), size_t(250001)}) {
            g_stage = "read error";
            FIL fil; fil.file = &f; fil.failReadAt = failAt;
            rb->reqBind(&fil, 44, 300000);
            Played p = consume(rb, 2304);
            CHECK(p.eof);
            CHECK(p.error);
            CHECK(sameAs(p.data, f, 44, std::max<size_t>(failAt, 44)));
            rb->reqUnbind();
            // core1 is still alive and the next bind works
            playRange(rb, f, 44, 300000, 2304, 0, "after read error");
        }
        {
            g_stage = "seek error";
            FIL fil; fil.file = &f; fil.failSeek = true;
            rb->reqBind(&fil, 44, 300000);
            CHECK(rb->isError());
            CHECK(rb->isEof());
            CHECK(rb->getLeft() == 0);
            CHECK(fatfs_owner_core == 0);
            rb->reqUnbind();
            playRange(rb, f, 44, 300000, 2304, 0, "after seek error");
        }
        {
            g_stage = "file shorter than eodPos";
            FIL fil; fil.file = &f;
            rb->reqBind(&fil, 44, 500000);  // header claims more than the file has
            Played p = consume(rb, 2304);
            CHECK(p.eof);
            CHECK(p.error);
            CHECK(sameAs(p.data, f, 44, f.size()));
            rb->reqUnbind();
        }
        // unbind while core1 hits errors / is idle: repeated unbinds are fine
        g_stage = "double unbind";
        rb->reqUnbind();
        rb->reqUnbind();
        section("read/seek errors end the stream, core1 survives");
    }

    // stop/skip mid-stream, then a new file: no stale data from the previous binding
    {
        std::mt19937 rng(7);
        auto a = makeFile(600000, 4);
        auto b = makeFile(600000, 5);
        for (int n = 0; n < 200; n++) {
            g_stage = "stop mid-stream";
            FIL fil; fil.file = (n & 1) ? &a : &b;
            fil.readDelayUs = rng() % 3 == 0 ? 100 : 0;
            size_t start = 44 + rng() % 400000;
            size_t eod = start + 1 + rng() % 150000;
            size_t stopAfter = rng() % 60000;
            rb->reqBind(&fil, start, eod);
            Played p = consume(rb, 1 + rng() % 4608, stopAfter);
            size_t expectEnd = std::min(eod, start + std::max<size_t>(stopAfter, 0));
            if (p.eof) { expectEnd = eod; }
            CHECK(sameAs(p.data, *fil.file, start, start + p.data.size()));
            CHECK(p.data.size() == expectEnd - start || (p.eof && p.data.size() == eod - start));
            CHECK(fil.maxReadEnd.load() <= eod);
            rb->reqUnbind();
            CHECK(fatfs_owner_core == 0);  // unbind hands FatFs back even in the middle of the stream
            FATFS_ASSERT_OWNER();
        }
        section("200 random stop/restart cycles without stale data");
    }

    // seek(): rebind from a new position with the stored eodPos
    {
        auto f = makeFile(200000, 6);
        g_stage = "seek";
        FIL fil; fil.file = &f;
        rb->reqBind(&fil, 44, 150000);
        Played head = consume(rb, 2304, 10000);
        CHECK(sameAs(head.data, f, 44, 10044));
        CHECK(rb->seek(120000));
        Played tail = consume(rb, 2304);
        CHECK(tail.eof);
        CHECK(sameAs(tail.data, f, 120000, 150000));
        CHECK(!rb->seek(150000));  // at eodPos: refused
        rb->reqUnbind();
        section("seek");
    }

    // slow card, fast consumer: the instant mute must be released near the end (isFilled), no zero insertion
    {
        auto f = makeFile(300000, 8);
        playRange(rb, f, 44, 300000, 4608, 400, "slow card");
        section("slow card: mute released near the end, content intact");
    }

    // core1 sleeps (__wfe) instead of spinning while there is nothing to do, and wakes up on core0's queue operations
    {
        auto f = makeFile(1000000, 9);
        auto idleCpuMs = [](const char* stage) {
            g_stage = stage;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));  // let core1 settle
            std::clock_t c0 = std::clock();
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            return (std::clock() - c0) * 1000.0 / CLOCKS_PER_SEC;
        };
        FIL fil; fil.file = &f;
        rb->reqBind(&fil, 44, 1000000);  // queue full, nothing consumed
        double busyFull = idleCpuMs("idle: queue full");
        CHECK(busyFull < 100.0);  // a spinning core1 burns ~300 ms of CPU here
        Played p = consume(rb, 4608, 50000);  // consumer wakes core1 to refill
        CHECK(sameAs(p.data, f, 44, 50044));
        CHECK(rb->isFull() || rb->getLeft() > 0);
        rb->reqUnbind();
        FIL fil2; fil2.file = &f;
        rb->reqBind(&fil2, 44, 5000);  // all data read (done), nothing consumed
        double busyDone = idleCpuMs("idle: end of data");
        CHECK(busyDone < 100.0);
        rb->reqUnbind();  // request wakes core1
        double busyUnbound = idleCpuMs("idle: unbound");
        CHECK(busyUnbound < 100.0);
        printf("       idle CPU over 300 ms: full %.1f ms, end of data %.1f ms, unbound %.1f ms\n", busyFull, busyDone, busyUnbound);
        playRange(rb, f, 44, 1000000, 2304, 0, "after idle");
        section("core1 sleeps while idle and wakes on queue operations");
    }

    // zero insertion is counted (fill() runs in the ISR and must not print): consume faster than a slow card
    {
        auto f = makeFile(400000, 10);
        g_stage = "zero fill count";
        FIL fil; fil.file = &f; fil.readDelayUs = 2000;
        uint32_t before = ReadBuffer::getZeroFillCount();
        rb->reqBind(&fil, 44, 400000);
        for (int i = 0; i < 40 && !rb->isEof(); i++) { rb->shift(rb->getLeft()); }  // no instant mute
        CHECK(ReadBuffer::getZeroFillCount() > before);
        rb->reqUnbind();
        CHECK(fatfs_owner_core == 0);
        section("underrun with zero insertion is counted for the main loop log");
    }

    printf("%d checks, %d failed\n", g_checks, g_failed);
    fflush(stdout);
    std::_Exit(g_failed ? 1 : 0);  // core1 thread runs forever
}
