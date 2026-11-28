/*-----------------------------------------------------------/
/ Helix MP3 decode benchmark for RP2040 / RP2350
/------------------------------------------------------------/
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/-----------------------------------------------------------*/
// Decodes the embedded clips at 96, 133 and 150 MHz (and 200 MHz on RP2040) and measures CPU cycles per MP3Decode()
// call with SysTick (processor clock, 24-bit, extended in software: one frame is far below 2^24 cycles).
// Results are kept and printed every 5 s on USB CDC and UART0 (GP0/GP1, 115200), so a terminal
// can be connected at any time. BENCH_EMU=1 (emulator build): UART only, default clock, one pass.

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/structs/systick.h"
#include "hardware/uart.h"
#include "hardware/vreg.h"

#include "bench_decode.h"

// emulator hook: rp2040js/QEMU plugins watch the execution of this function's address (see bench/helix_mp3/emu)
uint64_t __attribute__((noinline)) bench_cycles(void);

extern const uint8_t clip_128k[], clip_128k_end[], clip_320k[], clip_320k_end[];

// FNV-1a of the PCM output as decoded on the host (bench/helix_mp3/host): proves bit-exact decoding
#define HASH_128K 0x4b669ce9u
#define HASH_320K 0xab75bc48u

static const struct {
    const char* name;
    const uint8_t* begin;
    const uint8_t* end;
    uint32_t hash;
} clips[] = {
    {"128k", clip_128k, clip_128k_end, HASH_128K},
    {"320k", clip_320k, clip_320k_end, HASH_320K},
};
#define NUM_CLIPS (sizeof(clips) / sizeof(clips[0]))

#if BENCH_EMU
static const uint32_t freqs_khz[] = {0};  // keep the clock set by the runtime
#elif PICO_RP2040
// RP2040: 133 MHz is the rating at the default 1.10 V; 150 and 200 MHz (certified since SDK 2.1.1) need 1.15 V
static const uint32_t freqs_khz[] = {96000, 133000, 150000, 200000};
#else
static const uint32_t freqs_khz[] = {96000, 133000, 150000};  // RP2350: 150 MHz is the default and rating
#endif
#define NUM_FREQS (sizeof(freqs_khz) / sizeof(freqs_khz[0]))

typedef struct {
    bool done;
    bool clock_ok;
    uint32_t sys_hz;
    uint64_t us;  // wall time of the whole clip (cross-check of the cycle counts)
    bench_result_t r;
} run_t;
static run_t runs[NUM_FREQS][NUM_CLIPS];
static size_t heap_bytes;
static uint32_t stack_bytes;

static uint32_t systick_last;
static uint64_t systick_hi;

static void cycles_init(void)
{
    systick_hw->csr = 0;
    systick_hw->rvr = 0x00ffffff;
    systick_hw->cvr = 0;
    systick_hw->csr = 0x5;  // enable, processor clock, no interrupt
    systick_last = 0;
    systick_hi = 0;
}

// must be called at least once every 2^24 cycles
uint64_t bench_cycles(void)
{
    uint32_t v = 0x00ffffff - (systick_hw->cvr & 0x00ffffff);  // SysTick counts down
    if (v < systick_last) { systick_hi += 0x01000000; }
    systick_last = v;
    return systick_hi + v;
}

// stack high-water mark of the main core (stack is at the top of SCRATCH_Y, grows down)
extern uint32_t __scratch_y_end__[], __StackTop[];
#define STACK_PAINT 0x5a5a5a5au

static void __attribute__((noinline)) stack_paint(void)
{
    uint32_t sp;
    __asm__ volatile ("mov %0, sp" : "=r" (sp));
    for (uint32_t* p = __scratch_y_end__; p < (uint32_t*) (sp - 64); p++) { *p = STACK_PAINT; }
}

static uint32_t stack_used(void)
{
    uint32_t* p = __scratch_y_end__;
    while (p < __StackTop && *p == STACK_PAINT) { p++; }
    return (uint32_t) ((uintptr_t) __StackTop - (uintptr_t) p);
}

static void run_all(void)
{
    for (size_t f = 0; f < NUM_FREQS; f++) {
        bool ok = true;
        if (freqs_khz[f]) {
            stdio_flush();
            sleep_ms(20);
#if PICO_RP2040
            if (freqs_khz[f] > 133000) {
                vreg_set_voltage(VREG_VOLTAGE_1_15);
                sleep_ms(10);
            }
#endif
            ok = set_sys_clock_khz(freqs_khz[f], false);
            uart_set_baudrate(uart0, PICO_DEFAULT_UART_BAUD_RATE);  // clk_peri follows clk_sys
        }
        for (size_t c = 0; c < NUM_CLIPS; c++) {
            run_t* run = &runs[f][c];
            run->clock_ok = ok;
            run->sys_hz = clock_get_hz(clk_sys);
            if (!ok) { continue; }
            cycles_init();
            stack_paint();
            uint64_t t0 = time_us_64();
            bench_decode(clips[c].begin, (size_t) (clips[c].end - clips[c].begin), bench_cycles, &run->r);
            run->us = time_us_64() - t0;
            uint32_t s = stack_used();
            if (s > stack_bytes) { stack_bytes = s; }
            heap_bytes = bench_decoder_heap_bytes();
            run->done = true;
        }
    }
}

#if BENCH_EMU
#define BENCH_KIND "EMULATED: SysTick of the emulator's cycle model, not real hardware"
#else
#define BENCH_KIND "MEASURED: SysTick on this chip"
#endif

static void report(void)
{
    printf("\n=== %s (%s, %s), Helix MP3 (libhelix-mp3), cycles per MP3Decode() call [%s] ===\n",
        BENCH_NAME, BENCH_PORT_MODE, BENCH_BINTYPE, BENCH_KIND);
    printf("decoder heap %u B, main stack high-water %u B (includes the benchmark itself)\n",
        (unsigned) heap_bytes, (unsigned) stack_bytes);
    printf("clip  sys_MHz frames err     min_cyc     avg_cyc     max_cyc  avg_us  CPU%%avg CPU%%max  wall_ms  pcm\n");
    for (size_t f = 0; f < NUM_FREQS; f++) {
        for (size_t c = 0; c < NUM_CLIPS; c++) {
            const run_t* run = &runs[f][c];
            if (!run->clock_ok) { printf("%-5s %7.1f clock not available\n", clips[c].name, freqs_khz[f] / 1000.0); continue; }
            if (!run->done) { continue; }
            const bench_result_t* r = &run->r;
            double avg = r->frames ? (double) r->sum / r->frames : 0;
            // real time per frame: samples per channel / sample rate
            double frame_s = (r->nchans && r->samprate) ? (double) r->samps_per_frame / r->nchans / r->samprate : 1;
            double budget = frame_s * run->sys_hz;
            printf("%-5s %7.1f %6u %3u %11llu %11.0f %11llu %7.0f %7.1f %7.1f %8llu  %s\n",
                clips[c].name, run->sys_hz / 1e6, (unsigned) r->frames, (unsigned) r->errors,
                (unsigned long long) r->min, avg, (unsigned long long) r->max, avg * 1e6 / run->sys_hz,
                100.0 * avg / budget, 100.0 * r->max / budget, (unsigned long long) (run->us / 1000),
                (r->pcm_hash == clips[c].hash) ? "OK" : "MISMATCH");
        }
    }
    printf("frame = 1152 samples @ 44.1 kHz = 26.12 ms; CPU%% = cycles / (26.12 ms * sys clock)\n");
}

int main(void)
{
    stdio_init_all();
#if !BENCH_EMU
    sleep_ms(3000);  // give USB CDC time to enumerate
    printf("%s: running...\n", BENCH_NAME);
#endif
    run_all();
#if BENCH_EMU
    report();
    printf("BENCH DONE\n");
    while (true) { tight_loop_contents(); }
#else
    while (true) {
        report();
        sleep_ms(5000);
    }
#endif
}
