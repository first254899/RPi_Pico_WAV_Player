/*-----------------------------------------------------------/
/ bench_decode: decode an MP3 clip in memory and time every frame
/------------------------------------------------------------/
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/-----------------------------------------------------------*/
#include "bench_decode.h"

#include <stdlib.h>
#include <string.h>

#include "mp3dec.h"

void (*bench_pcm_sink)(const short* pcm, int samples) = NULL;

static short pcm[MAX_NCHAN * MAX_NGRAN * MAX_NSAMP];
static size_t heap_bytes;

size_t bench_decoder_heap_bytes(void)
{
    return heap_bytes;
}

static uint32_t fnv1a(uint32_t h, const short* s, int n)
{
    const uint8_t* p = (const uint8_t*) s;
    for (int i = 0; i < n * 2; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

// real/buffers.c is built with HELIX_BENCH_MALLOC: its malloc() calls come here
static size_t malloc_total;

void* bench_malloc(size_t size)
{
    malloc_total += size;
    return malloc(size);
}

static size_t bench_malloc_total(void)
{
    return malloc_total;
}

int bench_decode(const uint8_t* mp3, size_t len, uint64_t (*cyc)(void), bench_result_t* r)
{
    memset(r, 0, sizeof(*r));
    r->min = UINT64_MAX;
    r->pcm_hash = 2166136261u;

    size_t heap0 = bench_malloc_total();
    HMP3Decoder dec = MP3InitDecoder();
    if (!dec) { return -1; }
    heap_bytes = bench_malloc_total() - heap0;

    unsigned char* ptr = (unsigned char*) mp3;
    int left = (int) len;
    while (left > 0) {
        int off = MP3FindSyncWord(ptr, left);
        if (off < 0) { break; }
        ptr += off;
        left -= off;
        uint64_t t0 = cyc();
        int err = MP3Decode(dec, &ptr, &left, pcm, 0);
        uint64_t t1 = cyc();
        if (err == ERR_MP3_INDATA_UNDERFLOW) { break; }  // truncated last frame
        if (err != ERR_MP3_NONE) {
            r->errors++;
            if (err == ERR_MP3_MAINDATA_UNDERFLOW) { continue; }  // bit reservoir not filled yet (no output)
            ptr++;  // skip a broken header
            left--;
            continue;
        }
        MP3FrameInfo info;
        MP3GetLastFrameInfo(dec, &info);
        uint64_t d = t1 - t0;
        r->frames++;
        r->sum += d;
        if (d < r->min) { r->min = d; }
        if (d > r->max) { r->max = d; }
        r->samprate = (uint32_t) info.samprate;
        r->bitrate = (uint32_t) info.bitrate;
        r->nchans = (uint32_t) info.nChans;
        r->samps_per_frame = (uint32_t) info.outputSamps;
        r->pcm_hash = fnv1a(r->pcm_hash, pcm, info.outputSamps);
        if (bench_pcm_sink) { bench_pcm_sink(pcm, info.outputSamps); }
    }
    MP3FreeDecoder(dec);
    if (r->frames == 0) { r->min = 0; }
    return 0;
}
