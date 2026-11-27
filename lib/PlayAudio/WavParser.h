/*------------------------------------------------------/
/ Copyright (c) 2026, first254899
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

#pragma once

#include <cstddef>
#include <cstdint>

//=================================
// Interface of WavSource Class
//=================================
// Minimal random-access file abstraction used by WavParser
// so that the parser has no dependency on FatFs (testable on host)
class WavSource
{
public:
    virtual ~WavSource() {}
    virtual uint64_t size() = 0;
    // read exactly len bytes from pos. return false on I/O error or short read
    virtual bool readAt(uint64_t pos, void* buf, size_t len) = 0;
};

//=================================
// Interface of WavParser Class
//=================================
class WavParser
{
public:
    typedef enum {
        RESULT_OK = 0,
        RESULT_IO_ERROR,     // read failure
        RESULT_NOT_WAVE,     // no RIFF/WAVE header
        RESULT_NO_FMT,       // 'fmt ' chunk not found
        RESULT_BAD_FMT,      // 'fmt ' chunk truncated or inconsistent
        RESULT_UNSUPPORTED,  // valid WAV but format not playable
        RESULT_NO_DATA       // 'data' chunk not found or empty
    } result_t;
    static constexpr uint16_t FMT_PCM        = 0x0001;
    static constexpr uint16_t FMT_FLOAT      = 0x0003;
    static constexpr uint16_t FMT_EXTENSIBLE = 0xFFFE;
    static constexpr uint16_t MAX_CHANNELS   = 2;
    static constexpr uint32_t MIN_SAMP_FREQ  = 8000;
    static constexpr uint32_t MAX_SAMP_FREQ  = 192000;
    static constexpr int      MAX_CHUNKS     = 1024;  // guard against files made of tiny chunks
    typedef struct {
        uint16_t format;         // FMT_PCM or FMT_FLOAT (resolved from SubFormat if EXTENSIBLE)
        bool     extensible;     // format tag was WAVE_FORMAT_EXTENSIBLE
        uint16_t channels;
        uint32_t sampFreq;
        uint32_t bytesPerSec;
        uint16_t blockAlign;     // bytes per frame
        uint16_t bitsPerSample;  // container bits per sample
        uint16_t validBits;      // valid bits per sample (== bitsPerSample unless EXTENSIBLE says otherwise)
        uint32_t dataPos;        // file offset of the first sample
        uint32_t dataSize;       // bytes of samples, multiple of blockAlign, clamped to the file size
    } info_t;
    static result_t parse(WavSource& src, info_t& info);
    static const char* resultStr(result_t result);
};

//=================================
// IEEE754 float (bit pattern) to S32 conversion
//=================================
// Same result as (int32_t) (f * 2^31) (truncation toward zero) with saturation for |f| >= 1.0
// and NaN -> 0, but integer-only so that it is cheap inside the I2S DMA ISR (no FPU on RP2040)
static inline int32_t wavFloatToS32(uint32_t bits)
{
    const uint32_t exp = (bits >> 23) & 0xff;
    const bool neg = (bits >> 31) != 0;
    if (exp == 0xff && (bits & 0x7fffff) != 0) { return 0; }  // NaN
    if (exp >= 127) { return neg ? INT32_MIN : INT32_MAX; }   // |f| >= 1.0 (incl. Inf)
    if (exp < 127 - 31) { return 0; }                         // |f| < 2^-31 (incl. denormal, zero)
    const uint32_t mant = (bits & 0x7fffff) | 0x800000;       // 1.m * 2^23
    const int shift = static_cast<int>(exp) - 119;            // f * 2^31 = mant * 2^(exp - 127 - 23 + 31)
    const uint32_t mag = (shift >= 0) ? (mant << shift) : (mant >> -shift);
    return neg ? -static_cast<int32_t>(mag) : static_cast<int32_t>(mag);
}
