/*------------------------------------------------------/
/ Copyright (c) 2026, first254899
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

// Host-side test of TagRead (src/TagRead.*) error paths against the FatFs stub.
// Built with ASan/LSan: any leak on an error path fails the run at exit.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "TagRead.h"

static int g_failed = 0;
static int g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

typedef std::vector<uint8_t> bytes;

static void put(bytes& b, const char* s) { b.insert(b.end(), s, s + strlen(s)); }
static void be32(bytes& b, uint32_t v) { for (int i = 3; i >= 0; i--) { b.push_back(static_cast<uint8_t>(v >> (8 * i))); } }
static void ss32(bytes& b, uint32_t v) { for (int i = 3; i >= 0; i--) { b.push_back(static_cast<uint8_t>((v >> (7 * i)) & 0x7f)); } }
static void le32(bytes& b, uint32_t v) { for (int i = 0; i < 4; i++) { b.push_back(static_cast<uint8_t>(v >> (8 * i))); } }

// ID3v2 frame with ISO-8859-1 text
static bytes frame(int ver, const char* id, const char* text, uint32_t sizeOverride = 0)
{
    bytes f;
    uint32_t size = static_cast<uint32_t>(strlen(text) + 1);
    if (sizeOverride) { size = sizeOverride; }
    if (ver == 2) {
        put(f, id);
        f.push_back(static_cast<uint8_t>(size >> 16)); f.push_back(static_cast<uint8_t>(size >> 8)); f.push_back(static_cast<uint8_t>(size));
    } else {
        put(f, id);
        if (ver == 4) { ss32(f, size); } else { be32(f, size); }
        f.push_back(0); f.push_back(0);
    }
    f.push_back(0);  // encoding
    put(f, text);
    return f;
}

static bytes id3v2(int ver, const std::vector<bytes>& frames, uint8_t flags = 0, int sizeAdjust = 0, int padding = 16)
{
    bytes body;
    for (auto& f : frames) { body.insert(body.end(), f.begin(), f.end()); }
    body.insert(body.end(), padding, 0);
    bytes t;
    put(t, "ID3");
    t.push_back(static_cast<uint8_t>(ver)); t.push_back(0); t.push_back(flags);
    ss32(t, static_cast<uint32_t>(static_cast<int>(body.size()) + sizeAdjust));
    t.insert(t.end(), body.begin(), body.end());
    return t;
}

static bytes withAudio(bytes b, size_t n = 2000)
{
    for (size_t i = 0; i < n; i++) { b.push_back(static_cast<uint8_t>(i * 7)); }
    return b;
}

static bytes riffWav(const bytes& extraChunks, uint32_t dataBytes)
{
    bytes b;
    put(b, "RIFF"); le32(b, 0); put(b, "WAVE");
    put(b, "fmt "); le32(b, 16);
    b.push_back(1); b.push_back(0); b.push_back(2); b.push_back(0);
    le32(b, 44100); le32(b, 44100 * 4); b.push_back(4); b.push_back(0); b.push_back(16); b.push_back(0);
    put(b, "data"); le32(b, dataBytes); b.insert(b.end(), dataBytes, 0);
    b.insert(b.end(), extraChunks.begin(), extraChunks.end());
    uint32_t riffSize = static_cast<uint32_t>(b.size() - 8);
    for (int i = 0; i < 4; i++) { b[4 + i] = static_cast<uint8_t>(riffSize >> (8 * i)); }
    return b;
}

static std::string title(TagRead& t)
{
    char s[256] = {};
    return t.getUTF8Title(s, sizeof(s) - 1) ? std::string(s) : std::string("<none>");
}

int main()
{
    TagRead tag;  // reused across files like UIPlayMode does
    auto load = [&](const char* name, const bytes& b) {
        stub_files()[name] = b;
        return tag.loadFile(name);
    };

    // valid tags of each version (v2.4 frames leaked before: LSan would report them)
    for (int ver : {2, 3, 4}) {
        const char* tit = (ver == 2) ? "TT2" : "TIT2";
        const char* art = (ver == 2) ? "TP1" : "TPE1";
        load("v.mp3", withAudio(id3v2(ver, {frame(ver, tit, "Song"), frame(ver, art, "Artist")})));
        CHECK(title(tag) == "Song");
        char s[256] = {};
        CHECK(tag.getUTF8Artist(s, sizeof(s) - 1) == 1 && std::string(s) == "Artist");
    }
    // a big frame (cover art) that is skipped
    {
        bytes big(70000, 0x55);
        bytes f; put(f, "APIC"); be32(f, static_cast<uint32_t>(big.size())); f.push_back(0); f.push_back(0);
        f.insert(f.end(), big.begin(), big.end());
        load("art.mp3", withAudio(id3v2(3, {frame(3, "TIT2", "Big"), f})));
        CHECK(title(tag) == "Big");
    }

    // error paths in the frame loops: each used to leak the frame and/or the whole tag
    // (files must be >= sizeof(id31) bytes, otherwise TagRead gives up before ID3v2)
    auto fillA = [](bytes b, size_t n) { b.insert(b.end(), n, 'A'); return b; };
    for (int ver : {2, 3, 4}) {
        const char* tit = (ver == 2) ? "TT2" : "TIT2";
        // tag size claims more than the file has: 'AAAA' is a huge frame that is skipped to EOF,
        // then the next frame header read comes up short
        load("short_hdr.mp3", fillA(id3v2(ver, {frame(ver, tit, "A")}, 0, 5000, 0), 600));
        // frame data shorter than its size (< frame_size_limit): data read comes up short
        bytes f = frame(ver, tit, "B", 900);
        load("short_data.mp3", fillA(id3v2(ver, {frame(ver, tit, "A"), f}, 0, 900, 0), 500));
    }
    // 0xFF frame id (v2.3, v2.4)
    for (int ver : {3, 4}) {
        bytes bad = frame(ver, "TIT2", "x");
        bad[0] = static_cast<char>(0xff);
        load("ff.mp3", withAudio(id3v2(ver, {frame(ver, "TIT2", "A"), bad})));
    }
    // unsupported header flags (extended header etc.): the header was leaked
    load("flags.mp3", withAudio(id3v2(3, {frame(3, "TIT2", "A")}, 0x40)));
    CHECK(title(tag) != "A");
    // not an ID3 tag / empty / tiny files
    load("plain.mp3", withAudio(bytes()));
    load("empty.mp3", bytes());
    load("tiny.mp3", bytes{'I', 'D', '3'});
    CHECK(tag.loadFile("missing.mp3") == 1);

    // WAV: id3 chunk, LIST INFO, and a WAV shorter than an ID3v1 block (crashed with id3v1 == NULL)
    {
        bytes id3chunk; put(id3chunk, "id3 ");
        bytes t = id3v2(3, {frame(3, "TIT2", "Chunk")});
        le32(id3chunk, static_cast<uint32_t>(t.size())); id3chunk.insert(id3chunk.end(), t.begin(), t.end());
        if (t.size() & 1) { id3chunk.push_back(0); }
        CHECK(load("id3.wav", riffWav(id3chunk, 4000)) == 1);
        CHECK(title(tag) == "Chunk");

        bytes info; put(info, "INFO"); put(info, "INAM"); le32(info, 5); put(info, "List"); info.push_back(0); info.push_back(0);
        bytes list; put(list, "LIST"); le32(list, static_cast<uint32_t>(info.size())); list.insert(list.end(), info.begin(), info.end());
        CHECK(load("list.wav", riffWav(list, 4000)) == 1);
        CHECK(title(tag) == "List");
        bytes tiny = riffWav(list, 8);  // < sizeof(id31)
        CHECK(tiny.size() < sizeof(id31));
        CHECK(load("tiny.wav", tiny) == 1);
        CHECK(title(tag) == "List");
        load("tiny_noinfo.wav", riffWav(bytes(), 8));
        char s[256];
        CHECK(tag.getUTF8Track(s, sizeof(s) - 1) == 0);
        CHECK(tag.getUTF8Album(s, sizeof(s) - 1) == 0);
    }

    // many loads in a row with the same object: previous tags are freed (checked by LSan at exit)
    for (int i = 0; i < 200; i++) {
        int ver = 2 + i % 3;
        load("loop.mp3", withAudio(id3v2(ver, {frame(ver, ver == 2 ? "TT2" : "TIT2", "L")})));
    }
    CHECK(title(tag) == "L");

    printf("test_tag_read: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
