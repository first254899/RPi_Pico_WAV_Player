/*------------------------------------------------------/
/ Copyright (c) 2021, Elehobica
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

#include "PlayWav.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"

#include "ReadBuffer.h"

//#define DEBUG_PLAYWAV

PlayWav* PlayWav::g_inst = nullptr;

void PlayWav::decode_func()
{
    if (g_inst == nullptr) { return; }
    g_inst->decode();
}

PlayWav::PlayWav() : PlayAudio()
{
    g_inst = this;
}

PlayWav::~PlayWav()
{
}

bool PlayWav::play(const char* filename, size_t fpos, uint32_t samplesPlayed)
{
    accum[0] = 0;
    accum[1] = 0;
    accumCount = 0;
    return PlayAudio::play(filename, fpos, samplesPlayed);
}

namespace {

// WavSource on FatFs (core0 only, while the file is not bound to ReadBuffer)
class FatFsWavSource : public WavSource
{
public:
    FatFsWavSource(FIL* fp) : fp(fp) {}
    uint64_t size() { return static_cast<uint64_t>(f_size(fp)); }
    bool readAt(uint64_t pos, void* buf, size_t len)
    {
        if (f_lseek(fp, static_cast<FSIZE_t>(pos)) != FR_OK) { return false; }
        UINT br;
        if (f_read(fp, buf, static_cast<UINT>(len), &br) != FR_OK) { return false; }
        return br == len;
    }
private:
    FIL* fp;
};

}  // namespace

bool PlayWav::parseHeader(size_t fpos, size_t* startPos, size_t* eodPos)
{
    FatFsWavSource src(&fil);
    WavParser::info_t info;
    WavParser::result_t result = WavParser::parse(src, info);
    if (result != WavParser::RESULT_OK) {
        printf("WAV: skip, %s\r\n", WavParser::resultStr(result));
        return false;
    }
    format        = info.format;
    channels      = info.channels;
    bitRateKbps   = static_cast<uint16_t>(info.bytesPerSec * 8 / 1000);
    blockBytes    = info.blockAlign;
    bitsPerSample = info.bitsPerSample;
    dataSize      = info.dataSize;
    if (sampFreq != info.sampFreq) { reinitI2s = true; }  // stays requested even if this play() fails later
    sampFreq = info.sampFreq;

    *startPos = info.dataPos;
    *eodPos = info.dataPos + info.dataSize;
    if (fpos != 0) {  // resume
        if (fpos >= *startPos && fpos < *eodPos) {
            *startPos = fpos - (fpos - info.dataPos) % blockBytes;  // align to frame
        } else {
            printf("WAV: resume position out of data, play from the beginning\r\n");
            setSamplesPlayed(0);
        }
    }
    return true;
}

void PlayWav::decode()
{
    if (ap == nullptr) { return; }

    if (isMuteCondition()) {
        PlayAudio::decode();
        return;
    }

    audio_buffer_t* buffer;
    if ((buffer = take_audio_buffer(ap, false)) == nullptr) { return; }

    #ifdef DEBUG_PLAYWAV
    static int decodeCount = 0;
    uint64_t start = to_us_since_boot(get_absolute_time());
    #endif // DEBUG_PLAYWAV

    int32_t* samples = reinterpret_cast<int32_t*>(buffer->buffer->bytes);
    const uint8_t* buf = rdbuf->buf();
    buffer->sample_count = std::min(static_cast<uint32_t>(rdbuf->getLeft()/blockBytes), buffer->max_sample_count);
    for (int i = 0; i < buffer->sample_count; i++, buf += blockBytes) {
        for (int j = 0; j < 2; j++) {
            int base = (channels == 2) ? j * bitsPerSample / 8 : 0;
            int32_t buf_s32;
            switch ((format << 8) | bitsPerSample) {
                case ((FMT_PCM   << 8) | 16): buf_s32 = static_cast<int32_t>((buf[base+1] << 24) | (buf[base+0] << 16)); break;
                case ((FMT_PCM   << 8) | 24): buf_s32 = static_cast<int32_t>((buf[base+2] << 24) | (buf[base+1] << 16) | (buf[base+0] << 8)); break;
                case ((FMT_PCM   << 8) | 32): buf_s32 = static_cast<int32_t>((buf[base+3] << 24) | (buf[base+2] << 16) | (buf[base+1] << 8) | (buf[base+0] << 0)); break;
                case ((FMT_FLOAT << 8) | 32): buf_s32 = wavFloatToS32((static_cast<uint32_t>(buf[base+3]) << 24) | (static_cast<uint32_t>(buf[base+2]) << 16) | (static_cast<uint32_t>(buf[base+1]) << 8) | static_cast<uint32_t>(buf[base+0])); break;
                default: buf_s32 = 0; break;  // not reached: refused by WavParser
            }
            samples[i*2+j] = static_cast<int32_t>((static_cast<int64_t>(buf_s32) * vol_table[volume] / 65536)) + DAC_ZERO;
            accum[j] += (buf_s32/65536) * (buf_s32/65536) / 32768 * 44100 / sampFreq;  // normalized to 44100 Hz's level
        }
        accumCount++;
    }
    give_audio_buffer(ap, buffer);
    incSamplesPlayed(buffer->sample_count);
    if (accumCount >= 576 * sampFreq / 44100) {  // normalized to 44100 Hz's timing
        setLevelInt(accum[0] / buffer->sample_count, accum[1] / buffer->sample_count);
        accum[0] = 0;
        accum[1] = 0;
        accumCount = 0;
    }
    rdbuf->shift(buffer->sample_count*blockBytes);
    if (rdbuf->isEof() && rdbuf->getLeft() < blockBytes) { finish(); }  // not stop(): this is in ISR

    #ifdef DEBUG_PLAYWAV
    uint32_t time = static_cast<uint32_t>(to_us_since_boot(get_absolute_time()) - start);
    if (decodeCount++ % 97 == 0) {  // use prime number to avoid sync
        printf("WAV::decode %d us\n", time);
    }
    #endif // DEBUG_PLAYWAV
}

uint32_t PlayWav::totalMillis()
{
    return  std::max(
        static_cast<uint32_t>(static_cast<uint64_t>(dataSize) * 1000 / (sampFreq * channels * bitsPerSample/8)),
        elapsedMillis()
    );
}