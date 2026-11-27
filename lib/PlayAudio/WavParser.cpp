/*------------------------------------------------------/
/ Copyright (c) 2026, first254899
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

#include "WavParser.h"

#include <cstring>

namespace {

uint16_t getU16LE(const uint8_t* ptr)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(ptr[1]) << 8) | ptr[0]);
}

uint32_t getU32LE(const uint8_t* ptr)
{
    return (static_cast<uint32_t>(ptr[3]) << 24) | (static_cast<uint32_t>(ptr[2]) << 16) |
           (static_cast<uint32_t>(ptr[1]) << 8) | static_cast<uint32_t>(ptr[0]);
}

// KSDATAFORMAT_SUBTYPE_xxx = {0000xxxx-0000-0010-8000-00AA00389B71}, bytes after the 16-bit format code
const uint8_t SubFormatGuidTail[14] = {
    0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

// parse and validate the body of 'fmt ' chunk (len: bytes available in body)
WavParser::result_t parseFmt(const uint8_t* body, uint32_t len, WavParser::info_t& info)
{
    if (len < 16) { return WavParser::RESULT_BAD_FMT; }
    uint16_t format    = getU16LE(body + 0);
    info.channels      = getU16LE(body + 2);
    info.sampFreq      = getU32LE(body + 4);
    info.bytesPerSec   = getU32LE(body + 8);
    info.blockAlign    = getU16LE(body + 12);
    info.bitsPerSample = getU16LE(body + 14);
    info.validBits     = info.bitsPerSample;
    info.extensible    = (format == WavParser::FMT_EXTENSIBLE);
    if (info.extensible) {
        // WAVEFORMATEXTENSIBLE: cbSize(2) validBits(2) channelMask(4) SubFormat(16)
        if (len < 40 || getU16LE(body + 16) < 22) { return WavParser::RESULT_BAD_FMT; }
        uint16_t validBits = getU16LE(body + 18);
        if (validBits > info.bitsPerSample) { return WavParser::RESULT_BAD_FMT; }
        if (validBits != 0) { info.validBits = validBits; }  // 0: not specified
        if (memcmp(body + 26, SubFormatGuidTail, sizeof(SubFormatGuidTail)) != 0) {
            return WavParser::RESULT_UNSUPPORTED;  // not a KSDATAFORMAT_SUBTYPE_xxx
        }
        format = getU16LE(body + 24);
    }
    info.format = format;

    // sanity of the header itself
    if (info.channels == 0 || info.sampFreq == 0 || info.bitsPerSample == 0 ||
        info.bitsPerSample % 8 != 0 || info.blockAlign != info.channels * (info.bitsPerSample / 8)) {
        return WavParser::RESULT_BAD_FMT;
    }
    // what the player can handle
    if (info.channels > WavParser::MAX_CHANNELS) { return WavParser::RESULT_UNSUPPORTED; }
    if (info.sampFreq < WavParser::MIN_SAMP_FREQ || info.sampFreq > WavParser::MAX_SAMP_FREQ) {
        return WavParser::RESULT_UNSUPPORTED;
    }
    if (format == WavParser::FMT_PCM) {
        if (info.bitsPerSample != 16 && info.bitsPerSample != 24 && info.bitsPerSample != 32) {
            return WavParser::RESULT_UNSUPPORTED;
        }
    } else if (format == WavParser::FMT_FLOAT) {
        if (info.bitsPerSample != 32) { return WavParser::RESULT_UNSUPPORTED; }
    } else {
        return WavParser::RESULT_UNSUPPORTED;
    }
    return WavParser::RESULT_OK;
}

}  // namespace

WavParser::result_t WavParser::parse(WavSource& src, info_t& info)
{
    // positions are kept in 64 bit so that 'ofs + 8 + size' never wraps.
    // RIFF cannot address beyond 4 GiB, and ReadBuffer works with 32-bit positions
    const uint64_t fileSize = src.size();
    const uint64_t limit = (fileSize < 0xffffffffULL) ? fileSize : 0xffffffffULL;
    uint8_t hdr[12];

    if (limit < sizeof(hdr)) { return RESULT_NOT_WAVE; }
    if (!src.readAt(0, hdr, sizeof(hdr))) { return RESULT_IO_ERROR; }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) { return RESULT_NOT_WAVE; }
    // RIFF size at hdr[4] is not trusted (often 0 or stale in recorded/streamed files), the actual file size is used instead

    info_t tmp = {};
    bool fmtFound = false;
    bool dataFound = false;
    uint64_t dataPos = 0;
    uint64_t dataSize = 0;
    uint64_t ofs = 12;
    for (int n = 0; n < MAX_CHUNKS && ofs + 8 <= limit; n++) {
        if (!src.readAt(ofs, hdr, 8)) { return RESULT_IO_ERROR; }
        const uint32_t size = getU32LE(hdr + 4);
        const uint64_t body = ofs + 8;
        const uint64_t avail = limit - body;  // bytes in the file after chunk header
        if (memcmp(hdr, "fmt ", 4) == 0 && !fmtFound) {
            uint8_t fmt[40] = {};
            uint32_t len = (size < sizeof(fmt)) ? size : sizeof(fmt);
            if (len > avail) { return RESULT_BAD_FMT; }  // truncated in 'fmt '
            if (!src.readAt(body, fmt, len)) { return RESULT_IO_ERROR; }
            result_t result = parseFmt(fmt, len, tmp);
            if (result != RESULT_OK) { return result; }
            fmtFound = true;
        } else if (memcmp(hdr, "data", 4) == 0 && !dataFound) {
            dataFound = true;
            dataPos = body;
            if (size == 0 || size == 0xffffffffUL) {
                // unfinalized recording / streaming writer: data runs to the end of file
                dataSize = avail;
                break;  // the chunk end is unknown, cannot walk any further
            }
            dataSize = (size < avail) ? size : avail;  // clamp truncated file
        }
        if (fmtFound && dataFound) { break; }
        ofs = body + size + (size & 1);  // chunks are padded to even size
    }

    if (!fmtFound) { return RESULT_NO_FMT; }
    if (!dataFound) { return RESULT_NO_DATA; }
    dataSize -= dataSize % tmp.blockAlign;  // drop a partial frame at the end
    if (dataSize == 0) { return RESULT_NO_DATA; }
    tmp.dataPos = static_cast<uint32_t>(dataPos);
    tmp.dataSize = static_cast<uint32_t>(dataSize);
    info = tmp;
    return RESULT_OK;
}

const char* WavParser::resultStr(result_t result)
{
    switch (result) {
        case RESULT_OK:          return "OK";
        case RESULT_IO_ERROR:    return "I/O error";
        case RESULT_NOT_WAVE:    return "not RIFF/WAVE";
        case RESULT_NO_FMT:      return "no fmt chunk";
        case RESULT_BAD_FMT:     return "broken fmt chunk";
        case RESULT_UNSUPPORTED: return "unsupported format";
        case RESULT_NO_DATA:     return "no audio data";
        default:                 return "unknown";
    }
}
