/*-----------------------------------------------------------/
/ host_bench: run bench_decode() on a file, print the PCM hash
/------------------------------------------------------------/
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/-----------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "bench_decode.h"

static uint64_t ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t) t.tv_sec * 1000000000u + (uint64_t) t.tv_nsec;
}

static FILE* out;
static void sink(const short* pcm, int samples)
{
    fwrite(pcm, sizeof(short), (size_t) samples, out);
}

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s clip.mp3 [out.s16le]\n", argv[0]); return 2; }
    if (argc > 2) {
        out = fopen(argv[2], "wb");
        if (!out) { perror(argv[2]); return 2; }
        bench_pcm_sink = sink;
    }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc((size_t) len);
    if (!buf || fread(buf, 1, (size_t) len, f) != (size_t) len) { return 2; }
    fclose(f);
    bench_result_t r;
    if (bench_decode(buf, (size_t) len, ns, &r) != 0) { return 1; }
    printf("frames %u errors %u %u Hz %u ch %u kbps hash 0x%08x heap %zu B ns/frame min %llu avg %llu max %llu\n",
        r.frames, r.errors, r.samprate, r.nchans, r.bitrate / 1000, r.pcm_hash, bench_decoder_heap_bytes(),
        (unsigned long long) r.min, (unsigned long long) (r.sum / (r.frames ? r.frames : 1)), (unsigned long long) r.max);
    free(buf);
    if (out) { fclose(out); }
    return (r.errors == 0 && r.frames > 0) ? 0 : 1;
}
