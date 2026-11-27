/*------------------------------------------------------/
/ Copyright (c) 2021, Elehobica
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

#pragma once

#include "PlayAudio.h"
#include "WavParser.h"

//=================================
// Definition of PlayWav Class
//=================================
class PlayWav : public PlayAudio
{
public:
    static void decode_func();
    PlayWav();
    ~PlayWav();
    bool play(const char* filename, size_t fpos = 0, uint32_t samplesPlayed = 0);
    uint32_t totalMillis();
protected:
    static constexpr uint16_t FMT_PCM   = WavParser::FMT_PCM;
    static constexpr uint16_t FMT_FLOAT = WavParser::FMT_FLOAT;
    static PlayWav* g_inst;
    uint32_t dataSize;
    uint16_t blockBytes;
    uint16_t format;  // 1: PCM, 3: IEEE float (EXTENSIBLE is resolved to either by WavParser)
    uint32_t accum[2] = {};
    uint32_t accumCount;
    bool parseHeader(size_t fpos, size_t* startPos, size_t* eodPos);
    void decode();
};
