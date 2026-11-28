/*-----------------------------------------------------------/
/ Helix MP3 benchmark on QEMU mps2-an505 (Cortex-M33), for the m33cost plugin
/------------------------------------------------------------/
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/-----------------------------------------------------------*/
// Same decode loop as the Pico firmware. QEMU does not count cycles: the m33cost plugin
// (emu/qemu/m33cost.c) applies a Cortex-M33 cycle model and an XIP cache model, using the
// executions of bench_cycles() as window marks. The PCM hash proves the SMMUL/SMLAL port is bit-exact.
#include <stdio.h>

#include "bench_decode.h"

extern const uint8_t clip_128k[], clip_128k_end[], clip_320k[], clip_320k_end[];

static volatile uint32_t calls;
uint64_t __attribute__((noinline)) bench_cycles(void)
{
    return ++calls;  // only marks windows for the plugin
}

int main(void)
{
    const struct { const char* name; const uint8_t* b; const uint8_t* e; uint32_t hash; } clips[] = {
        {"128k", clip_128k, clip_128k_end, 0x4b669ce9u},
        {"320k", clip_320k, clip_320k_end, 0xab75bc48u},
    };
    for (int c = 0; c < 2; c++) {
        bench_result_t r;
        bench_decode(clips[c].b, (size_t) (clips[c].e - clips[c].b), bench_cycles, &r);
        printf("an505 clip %s frames %u errors %u hash 0x%08x %s heap %u B\n", clips[c].name, (unsigned) r.frames,
            (unsigned) r.errors, (unsigned) r.pcm_hash, (r.pcm_hash == clips[c].hash) ? "OK" : "MISMATCH",
            (unsigned) bench_decoder_heap_bytes());
    }
    return 0;
}
