/*------------------------------------------------------/
/ Copyright (c) 2026, first254899
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

// Host-side unit test for WavParser (lib/PlayAudio/WavParser.*)

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "WavParser.h"

static int g_failed = 0;
static int g_checks = 0;

#define CHECK_EQ(actual, expected) do { \
    g_checks++; \
    auto a_ = (actual); auto e_ = (expected); \
    if (!(a_ == e_)) { \
        g_failed++; \
        printf("  FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #actual, \
            static_cast<long long>(a_), static_cast<long long>(e_)); \
    } \
} while (0)

//=================================
// WAV image builder
//=================================
class Wav
{
public:
    std::vector<uint8_t> b;
    Wav() { raw("RIFF"); u32(0); raw("WAVE"); }  // RIFF size fixed up by finish()
    Wav& raw(const char* s) { b.insert(b.end(), s, s + strlen(s)); return *this; }
    Wav& u8(uint8_t v) { b.push_back(v); return *this; }
    Wav& u16(uint16_t v) { u8(v & 0xff); return u8(v >> 8); }
    Wav& u32(uint32_t v) { u16(v & 0xffff); return u16(v >> 16); }
    Wav& zeros(size_t n) { b.insert(b.end(), n, 0); return *this; }
    // chunk header + body of given length (filled with pattern), padded if odd
    Wav& chunk(const char* id, uint32_t size, bool pad = true) {
        raw(id); u32(size);
        for (uint32_t i = 0; i < size; i++) { u8(static_cast<uint8_t>(0xA5 ^ i)); }
        if (pad && (size & 1)) { u8(0); }
        return *this;
    }
    Wav& fmt(uint16_t format, uint16_t ch, uint32_t fs, uint16_t bits, int blockAlign = -1) {
        uint16_t ba = (blockAlign >= 0) ? static_cast<uint16_t>(blockAlign) : static_cast<uint16_t>(ch * bits / 8);
        raw("fmt "); u32(16);
        u16(format); u16(ch); u32(fs); u32(fs * ba); u16(ba); u16(bits);
        return *this;
    }
    Wav& fmtFloatEx(uint16_t ch, uint32_t fs) {  // WAVEFORMATEX with cbSize = 0 (typical for IEEE float)
        uint16_t ba = static_cast<uint16_t>(ch * 4);
        raw("fmt "); u32(18);
        u16(WavParser::FMT_FLOAT); u16(ch); u32(fs); u32(fs * ba); u16(ba); u16(32); u16(0);
        return *this;
    }
    Wav& fmtExtensible(uint16_t subFormat, uint16_t ch, uint32_t fs, uint16_t bits, uint16_t validBits,
                       bool goodGuid = true, uint16_t cbSize = 22) {
        uint16_t ba = static_cast<uint16_t>(ch * bits / 8);
        raw("fmt "); u32(40);
        u16(WavParser::FMT_EXTENSIBLE); u16(ch); u32(fs); u32(fs * ba); u16(ba); u16(bits);
        u16(cbSize); u16(validBits); u32(ch == 2 ? 0x3 : 0x4);
        u16(subFormat);
        const uint8_t tail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
        for (int i = 0; i < 14; i++) { u8((goodGuid || i != 13) ? tail[i] : 0x00); }
        return *this;
    }
    Wav& data(uint32_t sizeField, size_t actualBytes) {
        raw("data"); u32(sizeField);
        for (size_t i = 0; i < actualBytes; i++) { u8(static_cast<uint8_t>(i)); }
        return *this;
    }
    Wav& finish() {
        uint32_t riff = static_cast<uint32_t>(b.size() - 8);
        b[4] = riff & 0xff; b[5] = (riff >> 8) & 0xff; b[6] = (riff >> 16) & 0xff; b[7] = riff >> 24;
        return *this;
    }
    Wav& truncate(size_t n) { b.resize(n); return *this; }
};

class MemSource : public WavSource
{
public:
    MemSource(const std::vector<uint8_t>& v, uint64_t fakeSize = 0, uint64_t failAt = UINT64_MAX)
        : v(v), fakeSize(fakeSize), failAt(failAt) {}
    uint64_t size() override { return fakeSize ? fakeSize : v.size(); }
    bool readAt(uint64_t pos, void* buf, size_t len) override {
        reads++;
        if (pos + len > failAt) { return false; }
        if (pos > v.size() || len > v.size() - pos) { return false; }
        memcpy(buf, v.data() + pos, len);
        return true;
    }
    int reads = 0;
private:
    const std::vector<uint8_t>& v;
    uint64_t fakeSize;
    uint64_t failAt;
};

static WavParser::result_t parse(const Wav& w, WavParser::info_t& info)
{
    MemSource src(w.b);
    memset(&info, 0xCC, sizeof(info));
    return WavParser::parse(src, info);
}

#define TEST(name) static void name(); static struct name##_reg { name##_reg() { tests().push_back({#name, name}); } } name##_inst; static void name()
struct TestEntry { const char* name; void (*fn)(); };
static std::vector<TestEntry>& tests() { static std::vector<TestEntry> t; return t; }

//=================================
// Normal PCM
//=================================
TEST(pcm16_stereo_44k)
{
    Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 16).data(4000, 4000).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_OK);
    CHECK_EQ(i.format, WavParser::FMT_PCM);
    CHECK_EQ(i.extensible, false);
    CHECK_EQ(i.channels, 2);
    CHECK_EQ(i.sampFreq, 44100u);
    CHECK_EQ(i.blockAlign, 4);
    CHECK_EQ(i.bitsPerSample, 16);
    CHECK_EQ(i.validBits, 16);
    CHECK_EQ(i.bytesPerSec, 176400u);
    CHECK_EQ(i.dataPos, 44u);
    CHECK_EQ(i.dataSize, 4000u);
}

TEST(pcm24_stereo_96k)
{
    Wav w; w.fmt(WavParser::FMT_PCM, 2, 96000, 24).data(6000, 6000).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_OK);
    CHECK_EQ(i.bitsPerSample, 24);
    CHECK_EQ(i.blockAlign, 6);
    CHECK_EQ(i.dataPos, 44u);
    CHECK_EQ(i.dataSize, 6000u);
}

TEST(pcm32_mono_192k)
{
    Wav w; w.fmt(WavParser::FMT_PCM, 1, 192000, 32).data(800, 800).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_OK);
    CHECK_EQ(i.channels, 1);
    CHECK_EQ(i.bitsPerSample, 32);
    CHECK_EQ(i.blockAlign, 4);
    CHECK_EQ(i.dataSize, 800u);
}

//=================================
// EXTENSIBLE / float
//=================================
TEST(extensible_pcm24_96k)
{
    Wav w; w.fmtExtensible(WavParser::FMT_PCM, 2, 96000, 24, 24).data(600, 600).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_OK);
    CHECK_EQ(i.format, WavParser::FMT_PCM);
    CHECK_EQ(i.extensible, true);
    CHECK_EQ(i.bitsPerSample, 24);
    CHECK_EQ(i.validBits, 24);
    CHECK_EQ(i.dataPos, 12u + 8 + 40 + 8);
    CHECK_EQ(i.dataSize, 600u);
}

TEST(extensible_pcm24_in_32bit_container)
{
    Wav w; w.fmtExtensible(WavParser::FMT_PCM, 2, 48000, 32, 24).data(800, 800).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_OK);
    CHECK_EQ(i.format, WavParser::FMT_PCM);
    CHECK_EQ(i.bitsPerSample, 32);
    CHECK_EQ(i.validBits, 24);
    CHECK_EQ(i.blockAlign, 8);
}

TEST(extensible_float32)
{
    Wav w; w.fmtExtensible(WavParser::FMT_FLOAT, 2, 96000, 32, 32).chunk("fact", 4).data(800, 800).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_OK);
    CHECK_EQ(i.format, WavParser::FMT_FLOAT);
    CHECK_EQ(i.extensible, true);
    CHECK_EQ(i.bitsPerSample, 32);
    CHECK_EQ(i.dataPos, 12u + 8 + 40 + 12 + 8);
}

TEST(plain_float32)
{
    Wav w; w.fmtFloatEx(2, 44100).chunk("fact", 4).data(800, 800).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_OK);
    CHECK_EQ(i.format, WavParser::FMT_FLOAT);
    CHECK_EQ(i.extensible, false);
    CHECK_EQ(i.dataPos, 12u + 8 + 18 + 12 + 8);
}

TEST(extensible_unknown_subformat_guid)
{
    Wav w; w.fmtExtensible(WavParser::FMT_PCM, 2, 44100, 16, 16, false).data(400, 400).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_UNSUPPORTED);
}

TEST(extensible_short_cbsize)
{
    Wav w; w.fmtExtensible(WavParser::FMT_PCM, 2, 44100, 16, 16, true, 0).data(400, 400).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_BAD_FMT);
}

TEST(extensible_valid_bits_over_container)
{
    Wav w; w.fmtExtensible(WavParser::FMT_PCM, 2, 44100, 16, 24).data(400, 400).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_BAD_FMT);
}

TEST(unsupported_formats)
{
    WavParser::info_t i;
    { Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 8).data(400, 400).finish();   CHECK_EQ(parse(w, i), WavParser::RESULT_UNSUPPORTED); }  // 8 bit
    { Wav w; w.fmt(WavParser::FMT_FLOAT, 2, 44100, 64).data(400, 400).finish(); CHECK_EQ(parse(w, i), WavParser::RESULT_UNSUPPORTED); } // double
    { Wav w; w.fmt(0x0002, 2, 44100, 16).data(400, 400).finish();             CHECK_EQ(parse(w, i), WavParser::RESULT_UNSUPPORTED); }  // ADPCM
    { Wav w; w.fmt(WavParser::FMT_PCM, 6, 48000, 16).data(1200, 1200).finish(); CHECK_EQ(parse(w, i), WavParser::RESULT_UNSUPPORTED); } // 5.1
    { Wav w; w.fmt(WavParser::FMT_PCM, 2, 384000, 24).data(600, 600).finish(); CHECK_EQ(parse(w, i), WavParser::RESULT_UNSUPPORTED); } // > 192k
    { Wav w; w.fmt(WavParser::FMT_PCM, 2, 4000, 16).data(400, 400).finish();   CHECK_EQ(parse(w, i), WavParser::RESULT_UNSUPPORTED); }  // < 8k
}

//=================================
// Chunk walking
//=================================
TEST(odd_sized_list_before_data)
{
    Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 16).chunk("LIST", 13).data(400, 400).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_OK);
    CHECK_EQ(i.dataPos, 12u + 24 + 8 + 13 + 1 + 8);  // pad byte honoured
    CHECK_EQ(i.dataSize, 400u);
}

TEST(odd_sized_chunk_before_fmt)
{
    Wav w; w.chunk("JUNK", 27).fmt(WavParser::FMT_PCM, 2, 44100, 16).data(400, 400).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_OK);
    CHECK_EQ(i.dataPos, 12u + 8 + 28 + 24 + 8);
}

TEST(large_chunk_before_data_beyond_4608_bytes)
{
    Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 16).chunk("id3 ", 100001).chunk("bext", 602).data(4000, 4000).finish();
    WavParser::info_t i;
    MemSource src(w.b);
    CHECK_EQ(WavParser::parse(src, i), WavParser::RESULT_OK);
    CHECK_EQ(i.dataPos, 12u + 24 + 8 + 100002 + 8 + 602 + 8);
    CHECK_EQ(i.dataSize, 4000u);
    CHECK_EQ(src.reads < 10, true);  // skipped by seeking, not by reading through it
}

TEST(trailing_chunk_after_data_is_excluded)
{
    // short clip followed by a tag: the end of audio is the end of 'data', not the end of file
    Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 16).data(1000, 1000).chunk("id3 ", 30000).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_OK);
    CHECK_EQ(i.dataPos, 44u);
    CHECK_EQ(i.dataSize, 1000u);
}

TEST(data_before_fmt)
{
    Wav w; w.data(400, 400).fmt(WavParser::FMT_PCM, 2, 44100, 16).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_OK);
    CHECK_EQ(i.dataPos, 20u);
    CHECK_EQ(i.dataSize, 400u);
}

TEST(duplicate_fmt_uses_first)
{
    Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 16).fmt(WavParser::FMT_PCM, 2, 48000, 24).data(400, 400).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_OK);
    CHECK_EQ(i.sampFreq, 44100u);
}

//=================================
// data size special values / truncation
//=================================
TEST(data_size_zero_runs_to_eof)
{
    Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 16).data(0, 4002).finish();  // 4002: partial frame at the end
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_OK);
    CHECK_EQ(i.dataPos, 44u);
    CHECK_EQ(i.dataSize, 4000u);  // rounded down to blockAlign
}

TEST(data_size_ffffffff_runs_to_eof)
{
    Wav w; w.fmt(WavParser::FMT_PCM, 2, 48000, 24).data(0xffffffff, 6000).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_OK);
    CHECK_EQ(i.dataSize, 6000u);
}

TEST(data_size_zero_and_no_samples)
{
    Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 16).data(0, 0).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_NO_DATA);
}

TEST(data_size_beyond_eof_is_clamped)
{
    Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 16).data(1000000, 4000).finish();  // truncated copy
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_OK);
    CHECK_EQ(i.dataSize, 4000u);
}

TEST(huge_chunk_size_before_data)
{
    // ofs + 8 + size would wrap in 32 bit
    for (uint32_t size : {0xfffffff0u, 0xfffffff8u, 0xffffffffu, 0x7fffffffu}) {
        Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 16).raw("LIST").u32(size).zeros(64).data(400, 400).finish();
        WavParser::info_t i;
        CHECK_EQ(parse(w, i), WavParser::RESULT_NO_DATA);  // terminates, data unreachable
    }
}

TEST(huge_chunk_size_with_fake_4gib_file)
{
    // file size reported above 4 GiB: offsets must still not wrap
    Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 16).raw("JUNK").u32(0xfffffffe).finish();
    WavParser::info_t i;
    MemSource src(w.b, 0x1'0000'0100ULL);
    CHECK_EQ(WavParser::parse(src, i), WavParser::RESULT_NO_DATA);
}

TEST(many_tiny_chunks_terminate)
{
    Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 16);
    for (int n = 0; n < WavParser::MAX_CHUNKS + 10; n++) { w.chunk("JUNK", 0); }
    w.data(400, 400).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_NO_DATA);
}

TEST(truncated_files)
{
    Wav full; full.fmt(WavParser::FMT_PCM, 2, 44100, 16).chunk("LIST", 40).data(400, 400).finish();
    WavParser::info_t i;
    { Wav w; CHECK_EQ(parse(w.truncate(0), i), WavParser::RESULT_NOT_WAVE); }
    { Wav w = full; CHECK_EQ(parse(w.truncate(11), i), WavParser::RESULT_NOT_WAVE); }
    { Wav w = full; CHECK_EQ(parse(w.truncate(12), i), WavParser::RESULT_NO_FMT); }
    { Wav w = full; CHECK_EQ(parse(w.truncate(18), i), WavParser::RESULT_NO_FMT); }   // inside fmt chunk header
    { Wav w = full; CHECK_EQ(parse(w.truncate(30), i), WavParser::RESULT_BAD_FMT); }  // inside fmt body
    { Wav w = full; CHECK_EQ(parse(w.truncate(60), i), WavParser::RESULT_NO_DATA); }  // inside LIST
    { Wav w = full; CHECK_EQ(parse(w.truncate(12 + 24 + 48 + 8), i), WavParser::RESULT_NO_DATA); }  // data header only
    { Wav w = full; CHECK_EQ(parse(w.truncate(12 + 24 + 48 + 8 + 102), i), WavParser::RESULT_OK); CHECK_EQ(i.dataSize, 100u); }
}

//=================================
// broken fmt
//=================================
TEST(channels_zero)
{
    Wav w; w.fmt(WavParser::FMT_PCM, 0, 44100, 16, 4).data(400, 400).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_BAD_FMT);
}

TEST(samp_freq_zero)
{
    Wav w; w.fmt(WavParser::FMT_PCM, 2, 0, 16).data(400, 400).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_BAD_FMT);
}

TEST(bad_block_align_or_bits)
{
    WavParser::info_t i;
    { Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 16, 0).data(400, 400).finish(); CHECK_EQ(parse(w, i), WavParser::RESULT_BAD_FMT); }
    { Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 16, 6).data(400, 400).finish(); CHECK_EQ(parse(w, i), WavParser::RESULT_BAD_FMT); }
    { Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 0, 4).data(400, 400).finish();  CHECK_EQ(parse(w, i), WavParser::RESULT_BAD_FMT); }
    { Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 20, 6).data(400, 400).finish(); CHECK_EQ(parse(w, i), WavParser::RESULT_BAD_FMT); }
}

TEST(fmt_too_short)
{
    Wav w; w.raw("fmt ").u32(14).u16(1).u16(2).u32(44100).u32(176400).u16(4).data(400, 400).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_BAD_FMT);
}

TEST(not_riff_wave)
{
    WavParser::info_t i;
    { Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 16).data(400, 400).finish(); w.b[0] = 'X'; CHECK_EQ(parse(w, i), WavParser::RESULT_NOT_WAVE); }
    { Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 16).data(400, 400).finish(); w.b[8] = 'A'; CHECK_EQ(parse(w, i), WavParser::RESULT_NOT_WAVE); }
}

TEST(no_fmt)
{
    Wav w; w.chunk("LIST", 10).data(400, 400).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_NO_FMT);
}

TEST(no_fmt_before_open_ended_data)
{
    Wav w; w.data(0, 400).fmt(WavParser::FMT_PCM, 2, 44100, 16).finish();
    WavParser::info_t i;
    CHECK_EQ(parse(w, i), WavParser::RESULT_NO_FMT);
}

TEST(io_error)
{
    Wav w; w.fmt(WavParser::FMT_PCM, 2, 44100, 16).chunk("LIST", 40).data(400, 400).finish();
    WavParser::info_t i;
    for (uint64_t failAt : {0ULL, 20ULL, 40ULL, 90ULL}) {
        MemSource src(w.b, 0, failAt);
        CHECK_EQ(WavParser::parse(src, i), WavParser::RESULT_IO_ERROR);
    }
}

TEST(info_untouched_on_failure)
{
    Wav w; w.fmt(WavParser::FMT_PCM, 0, 44100, 16, 4).data(400, 400).finish();
    WavParser::info_t i = {};
    i.sampFreq = 12345;
    MemSource src(w.b);
    CHECK_EQ(WavParser::parse(src, i), WavParser::RESULT_BAD_FMT);
    CHECK_EQ(i.sampFreq, 12345u);
}

//=================================
// float -> s32 conversion used by PlayWav::decode
//=================================
static uint32_t bitsOf(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

TEST(float_to_s32_special_values)
{
    CHECK_EQ(wavFloatToS32(bitsOf(0.0f)), 0);
    CHECK_EQ(wavFloatToS32(bitsOf(-0.0f)), 0);
    CHECK_EQ(wavFloatToS32(bitsOf(0.5f)), 0x40000000);
    CHECK_EQ(wavFloatToS32(bitsOf(-0.5f)), -0x40000000);
    CHECK_EQ(wavFloatToS32(bitsOf(1.0f)), INT32_MAX);
    CHECK_EQ(wavFloatToS32(bitsOf(-1.0f)), INT32_MIN);
    CHECK_EQ(wavFloatToS32(bitsOf(1.5f)), INT32_MAX);
    CHECK_EQ(wavFloatToS32(bitsOf(-7.0f)), INT32_MIN);
    CHECK_EQ(wavFloatToS32(bitsOf(INFINITY)), INT32_MAX);
    CHECK_EQ(wavFloatToS32(bitsOf(-INFINITY)), INT32_MIN);
    CHECK_EQ(wavFloatToS32(bitsOf(NAN)), 0);
    CHECK_EQ(wavFloatToS32(bitsOf(std::ldexp(1.0f, -31))), 1);
    CHECK_EQ(wavFloatToS32(bitsOf(std::ldexp(1.0f, -32))), 0);
    CHECK_EQ(wavFloatToS32(bitsOf(1.0e-40f)), 0);  // denormal
    CHECK_EQ(wavFloatToS32(bitsOf(std::nextafter(1.0f, 0.0f))), 0x7fffff80);
    CHECK_EQ(wavFloatToS32(bitsOf(std::nextafter(-1.0f, 0.0f))), -0x7fffff80);
}

TEST(float_to_s32_matches_reference)
{
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    int mismatches = 0;
    for (int n = 0; n < 1000000; n++) {
        float f = (n & 1) ? dist(rng) : std::ldexp(dist(rng), -static_cast<int>(rng() % 32));
        int32_t ref = static_cast<int32_t>(static_cast<double>(f) * 2147483648.0);  // truncation, exact in double
        if (wavFloatToS32(bitsOf(f)) != ref) { mismatches++; }
    }
    CHECK_EQ(mismatches, 0);
}

int main()
{
    for (const auto& t : tests()) {
        int before = g_failed;
        t.fn();
        printf("%s %s\n", (g_failed == before) ? "[ OK ]" : "[FAIL]", t.name);
    }
    printf("%d tests, %d checks, %d failed\n", static_cast<int>(tests().size()), g_checks, g_failed);
    return g_failed ? 1 : 0;
}
