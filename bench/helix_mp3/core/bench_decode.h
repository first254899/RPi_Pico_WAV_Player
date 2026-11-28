/*-----------------------------------------------------------/
/ bench_decode: decode an MP3 clip in memory and time every frame
/------------------------------------------------------------/
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/-----------------------------------------------------------*/
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t frames;     // frames decoded
    uint32_t errors;     // frames that returned an error (skipped)
    uint64_t sum;        // sum of per-frame counts (cycles or whatever cyc() returns)
    uint64_t min;
    uint64_t max;
    uint32_t pcm_hash;   // FNV-1a over all output samples (proves bit-exact output across ports)
    uint32_t samprate;
    uint32_t bitrate;
    uint32_t nchans;
    uint32_t samps_per_frame;  // output samples per frame (all channels)
} bench_result_t;

// cyc(): monotonic 64-bit counter (cycles on target, instructions or ns on emulators/host)
// returns 0 on success, < 0 if the decoder could not be created
int bench_decode(const uint8_t* mp3, size_t len, uint64_t (*cyc)(void), bench_result_t* r);

// optional: called with every decoded frame (outside the timed region), e.g. to write the PCM to a file
extern void (*bench_pcm_sink)(const short* pcm, int samples);

// heap bytes allocated by MP3InitDecoder() (measured by the caller's malloc hooks, see bench_decode.c)
size_t bench_decoder_heap_bytes(void);

#ifdef __cplusplus
}
#endif
