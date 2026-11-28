/*------------------------------------------------------/
/ Copyright (c) 2026, first254899
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

// Host-side unit test for GainRamp / Fader (lib/PlayAudio/GainRamp.h):
// ramp math, and a model of PlayWav::decode() driving the Fader buffer by buffer

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "GainRamp.h"

static int g_failed = 0;
static int g_checks = 0;
static const char* g_stage = "";

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { g_failed++; printf("  FAIL line %d: %s (%s)\n", __LINE__, #cond, g_stage); } \
} while (0)

static int g_sectionFailed = 0;
static void section(const char* name)
{
    printf("%s %s\n", (g_failed == g_sectionFailed) ? "[ OK ]" : "[FAIL]", name);
    g_sectionFailed = g_failed;
}

//=================================
// model of PlayWav::decode(): one call per output buffer
//=================================
static constexpr uint32_t BUF = 576;   // SAMPLES_PER_BUFFER
static constexpr int32_t ZERO = 1;     // DAC_ZERO

struct Decoder {
    std::vector<int32_t> stream;   // mono for simplicity: one sample per frame
    size_t pos = 0;                // consumed frames
    size_t buffered = SIZE_MAX;    // frames available beyond pos (underrun model), SIZE_MAX: all
    bool eofKnown = true;          // whole remainder counts as buffered and at EOF
    Fader fader;
    uint32_t ramp = 132;
    std::vector<int32_t> out;      // all output samples (without ZERO offset)
    std::vector<int32_t> gains;    // gain of every consumed frame
    std::vector<size_t> srcIdx;    // stream index of every consumed frame
    size_t maxReadEnd = 0;

    // returns frames consumed
    uint32_t decode(bool active, int32_t vol)
    {
        size_t left = stream.size() - pos;
        size_t can = std::min(left, buffered);
        bool eof = eofKnown && can == left;
        uint32_t framesToEnd = static_cast<uint32_t>(can);
        uint32_t avail = std::min<uint32_t>(framesToEnd, BUF);
        std::vector<int32_t> samples;
        if (!fader.begin(active, vol, avail, eof, framesToEnd, ramp)) {
            samples.assign(BUF, ZERO);
            for (auto s : samples) { out.push_back(s - ZERO); }
            return 0;
        }
        uint32_t n;
        int32_t g;
        for (n = 0; n < avail && fader.next(n, g); n++) {
            size_t idx = pos + n;
            maxReadEnd = std::max(maxReadEnd, idx + 1);
            samples.push_back(GainRamp::apply(stream[idx], g) + ZERO);
            gains.push_back(g);
            srcIdx.push_back(idx);
        }
        if (fader.padToFull()) { while (samples.size() < BUF) { samples.push_back(ZERO); } }
        for (auto s : samples) { out.push_back(s - ZERO); }
        pos += n;
        if (buffered != SIZE_MAX) { buffered -= n; }
        return n;
    }
};

static std::vector<int32_t> randomStream(size_t n, uint32_t seed)
{
    std::mt19937 rng(seed);
    std::vector<int32_t> v(n);
    for (auto& s : v) { s = static_cast<int32_t>(rng()); }
    if (n > 2) { v[0] = INT32_MIN; v[1] = INT32_MAX; }
    return v;
}

static int32_t maxGainStep(const std::vector<int32_t>& g, size_t from = 0, size_t to = SIZE_MAX)
{
    int32_t m = 0;
    to = std::min(to, g.size());
    for (size_t i = from + 1; i < to; i++) { m = std::max(m, std::abs(g[i] - g[i-1])); }
    return m;
}

int main()
{
    const int32_t U = GainRamp::UNITY;

    // apply(): unity is bit-exact over the full int32 range, no overflow at the extremes
    {
        g_stage = "apply";
        std::mt19937 rng(1);
        bool exact = true;
        for (int i = 0; i < 1000000; i++) {
            int32_t s = static_cast<int32_t>(rng());
            exact = exact && GainRamp::apply(s, U) == s;
        }
        CHECK(exact);
        CHECK(GainRamp::apply(INT32_MIN, U) == INT32_MIN);
        CHECK(GainRamp::apply(INT32_MAX, U) == INT32_MAX);
        CHECK(GainRamp::apply(INT32_MIN, 0) == 0);
        CHECK(GainRamp::apply(INT32_MAX, U / 2) == INT32_MAX / 2);
        CHECK(GainRamp::apply(INT32_MIN, 1) == INT32_MIN / 65536);
        // same operation as the former volume code: (int64) s * vol / 65536
        bool same = true;
        for (int i = 0; i < 100000; i++) {
            int32_t s = static_cast<int32_t>(rng());
            int32_t v = static_cast<int32_t>(rng() % (U + 1));
            same = same && GainRamp::apply(s, v) == static_cast<int32_t>(static_cast<int64_t>(s) * v / 65536);
        }
        CHECK(same);
        section("apply(): unity bit-exact, same math as the volume control");
    }

    // ramp shape
    {
        g_stage = "ramp";
        for (uint32_t len : {1u, 2u, 3u, 64u, 132u, 256u, 576u, 65536u, 200000u}) {
            for (auto [from, to] : {std::pair<int32_t, int32_t>{0, U}, {U, 0}, {0, 1}, {U, 7}, {3, 65000}, {0, 0}}) {
                GainRamp r(from);
                r.setTarget(to, len);
                CHECK(r.target() == to);
                std::vector<int32_t> g;
                uint32_t frames = 0;
                while (r.active()) { g.push_back(r.next()); frames++; }
                if (from == to) { CHECK(frames == 0 && r.gain() == to); continue; }
                CHECK(frames == len);
                CHECK(g.back() == to);              // ends exactly on target
                bool mono = true, inRange = true;
                for (size_t i = 0; i < g.size(); i++) {
                    int32_t prev = (i == 0) ? from : g[i-1];
                    mono = mono && ((to > from) ? g[i] >= prev : g[i] <= prev);
                    inRange = inRange && g[i] >= std::min(from, to) && g[i] <= std::max(from, to);
                }
                CHECK(mono);
                CHECK(inRange);
                // linear: each step within one unit of the ideal step
                int64_t ideal = (static_cast<int64_t>(std::abs(to - from)) + len - 1) / len;
                int32_t m = std::abs(g[0] - from);
                for (size_t i = 1; i < g.size(); i++) { m = std::max(m, std::abs(g[i] - g[i-1])); }
                CHECK(m <= ideal + 1);
                CHECK(r.next() == to);              // stays
            }
        }
        // retarget during a ramp continues from the current gain
        GainRamp r(0);
        r.setTarget(U, 100);
        for (int i = 0; i < 40; i++) { r.next(); }
        int32_t mid = r.gain();
        r.setTarget(0, 100);
        int32_t first = r.next();
        CHECK(first < mid && mid - first <= mid / 100 + 1);
        r.setTarget(5, 0);
        CHECK(r.gain() == 5 && !r.active());
        section("GainRamp: exact length and end value, monotonic, linear, retarget");
    }

    // play start: fade in, then bit-perfect at unity
    {
        g_stage = "start";
        Decoder d;
        d.stream = randomStream(10000, 2);
        d.fader.restart();
        while (d.pos < d.stream.size()) { d.decode(true, U); }
        CHECK(d.srcIdx.size() == d.stream.size());
        CHECK(d.gains[0] > 0 && d.gains[0] <= U / 132 + 1);
        CHECK(d.gains[131] == U);
        bool exact = true;
        for (size_t i = 132; i + 132 < d.stream.size(); i++) {
            exact = exact && d.out[i] == d.stream[i] && d.gains[i] == U;
        }
        CHECK(exact);  // full volume, no ramp active: output == input bit for bit
        CHECK(maxGainStep(d.gains) <= U / 132 + 1);
        section("play start fades in, then output is bit-perfect at full volume");
    }

    // track end: last frame gets gain 0, never reads past the end, every frame played once
    {
        for (size_t len : {size_t(1), size_t(50), size_t(131), size_t(132), size_t(133), size_t(575), size_t(576),
                           size_t(577), size_t(700), size_t(5000), size_t(100003)}) {
            g_stage = "end";
            Decoder d;
            d.stream = randomStream(len, static_cast<uint32_t>(len));
            d.fader.restart();
            int guard = 0;
            while (d.pos < d.stream.size() && guard++ < 100000) { d.decode(true, U); }
            CHECK(d.pos == len);
            CHECK(d.maxReadEnd == len);
            CHECK(d.gains.back() == 0);
            CHECK(d.fader.silent());
            bool order = true;
            for (size_t i = 0; i < d.srcIdx.size(); i++) { order = order && d.srcIdx[i] == i; }
            CHECK(order && d.srcIdx.size() == len);
            if (len >= 2 * 132) {
                CHECK(maxGainStep(d.gains) <= U / 132 + 1);
                CHECK(d.gains[len - 133] == U);   // the end fade starts 132 frames before the end
            }
            // after the end: nothing consumed, silence
            CHECK(d.decode(false, U) == 0);
        }
        // end of track not known in advance (EOF item arrives late): still no read past the end
        {
            g_stage = "late eof";
            Decoder d;
            d.stream = randomStream(3000, 9);
            d.eofKnown = false;
            d.fader.restart();
            // the decoder can't reach the last frames before the EOF item is in rdbuf
            while (d.stream.size() - d.pos > 60) { d.buffered = d.stream.size() - d.pos - 60; d.decode(true, U); }
            d.buffered = SIZE_MAX;
            d.eofKnown = true;
            while (d.pos < d.stream.size()) { d.decode(true, U); }
            CHECK(d.maxReadEnd == 3000);
            CHECK(d.gains.back() == 0);   // shorter fade over what is left
        }
        // pause during the end fade, then unpause: the rest of the track is still played out
        {
            g_stage = "pause in end fade";
            Decoder d;
            d.stream = randomStream(600, 10);
            d.ramp = 300;
            d.fader.restart();
            d.decode(true, U);                    // 576 frames: fade-in, end fade starts at 300
            CHECK(d.pos == 576);
            CHECK(d.decode(false, U) > 0);        // pause: fades out with the 24 frames left
            CHECK(d.pos == 600 || d.fader.silent());
            while (d.pos < d.stream.size()) { CHECK(d.decode(true, U) > 0); }
            CHECK(d.pos == 600);
            CHECK(d.gains.back() == 0);
        }
        section("track end fades to 0 on the last frame without reading past the end");
    }

    // pause/unpause and stop: fade out with buffered data, nothing lost or repeated
    {
        g_stage = "pause";
        Decoder d;
        d.stream = randomStream(50000, 3);
        d.fader.restart();
        for (int i = 0; i < 5; i++) { d.decode(true, U); }
        size_t before = d.pos;
        uint32_t c = d.decode(false, U);          // pause
        CHECK(c == 132);                          // fade-out consumes one ramp
        CHECK(d.gains.back() == 0);
        CHECK(d.out.size() % BUF == 0);           // padded to a full buffer
        CHECK(d.fader.silent());
        for (int i = 0; i < 10; i++) { CHECK(d.decode(false, U) == 0); }  // paused: silence, nothing consumed
        CHECK(d.pos == before + 132);
        d.decode(true, U);                        // unpause
        CHECK(d.gains[before + 132] > 0 && d.gains[before + 132] <= U / 132 + 1);
        while (d.pos < d.stream.size()) { d.decode(true, U); }
        bool order = true;
        for (size_t i = 0; i < d.srcIdx.size(); i++) { order = order && d.srcIdx[i] == i; }
        CHECK(order && d.srcIdx.size() == d.stream.size());
        CHECK(maxGainStep(d.gains) <= U / 132 + 1);

        // pause during the fade-in: fade out from where it is
        g_stage = "pause during fade-in";
        Decoder e;
        e.stream = randomStream(5000, 4);
        e.fader.restart();
        e.ramp = 500;
        e.buffered = 100;
        e.decode(true, U);                        // 100 frames of a 500 frame fade-in
        e.buffered = SIZE_MAX;
        int32_t g0 = e.gains.back();
        CHECK(g0 > 0 && g0 < U);
        e.decode(false, U);
        CHECK(e.gains.back() == 0);
        CHECK(maxGainStep(e.gains) <= U / 500 + 1);
        section("pause/unpause/stop fade with buffered data, every frame played once");
    }

    // underrun: entry fades with what is buffered (shorter if needed), recovery fades in
    {
        g_stage = "underrun";
        Decoder d;
        d.stream = randomStream(20000, 5);
        d.fader.restart();
        for (int i = 0; i < 3; i++) { d.decode(true, U); }
        d.buffered = 40;                          // only 40 frames left in rdbuf
        uint32_t c = d.decode(false, U);          // near empty -> mute
        CHECK(c == 40);
        CHECK(d.gains.back() == 0);
        CHECK(d.maxReadEnd == d.pos);
        CHECK(d.decode(false, U) == 0);
        d.buffered = 0;
        CHECK(d.decode(false, U) == 0);           // nothing buffered and silent
        d.buffered = SIZE_MAX;                    // refilled
        d.decode(true, U);
        size_t k = 3 * BUF + 40;
        CHECK(d.gains[k] > 0 && d.gains[k] <= U / 132 + 1);
        // nothing buffered at all when the underrun starts: gain jumps to 0 (nothing to fade with)
        g_stage = "underrun, empty";
        Decoder e;
        e.stream = randomStream(5000, 6);
        e.fader.restart();
        e.decode(true, U);
        e.buffered = 0;
        CHECK(e.decode(false, U) == 0);
        CHECK(e.fader.silent());
        section("underrun entry/recovery fade, never beyond the buffered data");
    }

    // volume changes ramp; volume 0 still consumes (track advances)
    {
        g_stage = "volume";
        Decoder d;
        d.stream = randomStream(20000, 7);
        d.fader.restart();
        for (int i = 0; i < 3; i++) { d.decode(true, U); }
        d.decode(true, 30773);
        d.decode(true, 0);
        CHECK(d.gains.back() == 0);
        CHECK(d.fader.silent());
        CHECK(d.decode(true, 0) == BUF);         // volume 0: silent but playing
        d.decode(true, U);
        CHECK(maxGainStep(d.gains) <= U / 132 + 1);
        section("volume changes ramp, volume 0 keeps playing");
    }

    // random sequences: invariants
    {
        g_stage = "random";
        std::mt19937 rng(8);
        for (int iter = 0; iter < 300; iter++) {
            Decoder d;
            d.stream = randomStream(1 + rng() % 30000, iter);
            d.ramp = 64 + rng() % 513;
            d.fader.restart();
            bool active = true;
            int32_t vol = U;
            int guard = 0;
            while (d.pos < d.stream.size() && guard++ < 10000) {
                if (rng() % 7 == 0) { active = !active; }
                if (rng() % 9 == 0) { vol = static_cast<int32_t>(rng() % (U + 1)); }
                size_t left = d.stream.size() - d.pos;
                if (rng() % 3 == 0 && left > 1) {
                    // EOF item not in rdbuf yet: the tail is not reachable
                    d.eofKnown = false;
                    d.buffered = std::min<size_t>((rng() % 5 == 0) ? rng() % 200 : SIZE_MAX, left - 1 - rng() % std::min<size_t>(left - 1, 600));
                } else {
                    d.eofKnown = true;
                    d.buffered = SIZE_MAX;
                }
                size_t before = d.pos;
                d.decode(active || guard > 9000, vol);
                CHECK(d.pos - before <= BUF);
            }
            CHECK(d.pos == d.stream.size());
            CHECK(d.maxReadEnd <= d.stream.size());
            bool order = true;
            for (size_t i = 0; i < d.srcIdx.size(); i++) { order = order && d.srcIdx[i] == i; }
            CHECK(order);
            CHECK(d.out.size() >= d.srcIdx.size());
        }
        section("random play/pause/volume/underrun: no frame lost, repeated or read past the end");
    }

    printf("test_gain_ramp: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
