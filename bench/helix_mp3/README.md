# Helix MP3 decode benchmark (experimental — Set B)

**Experimental, not for merge.** Standalone benchmark of the Helix fixed-point MP3 decoder
([libhelix-mp3](https://github.com/ultraembedded/libhelix-mp3), submodule `lib/libhelix-mp3`, unmodified)
on RP2040 (Pico) and RP2350 (Pico 2). It does not change the player build.

> **Nothing here has been measured on real hardware yet.** All cycle numbers below are **ESTIMATES**
> from emulators. The firmware (`pico/`) measures real cycles with SysTick when it is flashed on a board.

## Layout
| path | what |
|---|---|
| `port/helix_port.h` | replacement for Helix `real/assembly.h`, force-included (`-include`) so the submodule stays unmodified. `HELIX_PORT_C64` (plain C, `__aeabi_lmul` on M0+), `HELIX_PORT_M0` (C, 16x16 partial products), `HELIX_PORT_DSP` (M33: `SMMUL`, `SMLAL`, `CLZ`). All bit-identical. |
| `helix.cmake` | Helix source list + `add_helix_library()` |
| `core/bench_decode.*` | decode a clip in memory, time each `MP3Decode()` call, FNV-1a hash of the PCM |
| `clips/` | `clip_128k.mp3`, `clip_320k.mp3`: 8 s, 44.1 kHz stereo, CBR joint stereo, LAME 3.100 `-q 2 -t`, from a **synthetic** signal (`make_clips.sh`: chords with harmonics, arpeggio, pink noise, noise bursts). No third-party audio. |
| `host/` | host build: port self-test, PCM hash, comparison with ffmpeg |
| `pico/` | benchmark firmware (separate CMake project) |
| `emu/` | emulator estimates: rp2040js runner with XIP cache model, QEMU mps2-an505 build + `m33cost` plugin, `analyze.py`, `run_all.sh` |
| `results/` | raw window CSVs, logs, `estimates.md` |
| `uf2/` | pre-built firmware (convenience copies; rebuild with the commands below) |

## License note (libhelix-mp3)
- Helix code is © RealNetworks, dual-licensed **RPSL 1.0** (RealNetworks Public Source License, OSI-approved) or **RCSL** (Community Source License, needs an agreement with RealNetworks). We use it under the RPSL.
- RPSL §4.1/4.2: a distributed or "externally deployed" work that contains Helix must be licensed **as a whole** under the RPSL plus licenses on its *Compatible Source License* list (Exhibit B: BSD, MIT, Apache, zlib, … ), and its **source must be made available**. The player (BSD-2), the Pico SDK (BSD-3) and TinyUSB (MIT) are on that list. **GPL code cannot be combined with Helix** (Exhibit B note 1; for example the GPL-3 Arduino wrapper `arduino-libhelix` must not be used). FatFs uses its own BSD-style 1-clause license, which is not named on the list — check before shipping an MP3 player build.
- `port/helix_port.h` is a modification of Helix `assembly.h`, so it carries the RPSL header. Our other benchmark files are BSD-2. `emu/qemu/m33cost.c` is GPL-2.0-or-later (a QEMU plugin, never linked into firmware). `qemu-plugin.h` is not committed.
- This is not legal advice. Have it reviewed before an MP3-capable release.

## Build
```
# firmware (Pico SDK 2.x)
cmake -S bench/helix_mp3/pico -B build-bench-rp2040 -G Ninja && ninja -C build-bench-rp2040
cmake -S bench/helix_mp3/pico -B build-bench-rp2350 -G Ninja -DPICO_PLATFORM=rp2350 -DPICO_BOARD=pico2 && ninja -C build-bench-rp2350
# host check
cmake -S bench/helix_mp3/host -B build-bench-host -G Ninja && ninja -C build-bench-host && build-bench-host/test_port
build-bench-host/helix_host_c64 bench/helix_mp3/clips/clip_320k.mp3
# emulator estimates (see emu/run_all.sh for requirements)
bench/helix_mp3/emu/run_all.sh /tmp/helix-emu build-bench-rp2040
```

## Running on hardware
Flash a `.uf2` (BOOTSEL). After 3 s it decodes both clips at 96, 133 and 150 MHz (and at 200 MHz on RP2040 with VREG 1.15 V), then prints the table every 5 s on USB CDC and UART0 (GP0 TX, 115200 8N1). The `pcm` column must say `OK`, which means the output is bit-identical to the host decode.

| uf2 | chip | port | code location |
|---|---|---|---|
| `helix_bench_rp2040.uf2` | RP2040 | C64 | flash (XIP) |
| `helix_bench_rp2040_ram.uf2` | RP2040 | C64 | SRAM (`copy_to_ram`; clips stay in flash) |
| `helix_bench_rp2040_m0.uf2` | RP2040 | M0 (comparison) | flash (XIP) |
| `helix_bench_rp2350.uf2` | RP2350 | DSP | flash (XIP) |
| `helix_bench_rp2350_ram.uf2` | RP2350 | DSP | SRAM (`copy_to_ram`) |

`*_emu` builds (UART only, default clock, one pass) are for rp2040js.
The RP2040 hardware builds were smoke-tested in rp2040js. **The RP2350 uf2 files have not been executed anywhere** (there is no RP2350 emulator here); the same decoder code was run on QEMU Cortex-M33 instead.

Example line: `320k 150.0 308 0 <min> <avg> <max> <avg_us> <CPU%avg> <CPU%max> <wall_ms> OK`.

## Correctness (host, measured)
- `test_port`: the M0 MULSHIFT32/MADD64 matches 64-bit C in 40 M random and edge-case checks.
- The C64 and M0 ports give identical PCM (hash `0x4b669ce9` / `0xab75bc48`). The DSP port (`SMMUL`/`SMLAL`) gives the same hashes on QEMU Cortex-M33, and the rp2040js runs give them as well.
- Compared with ffmpeg's decoder on the same files: 84.3 dB (128k) and 84.7 dB (320k) SNR, maximum difference ±1 LSB, no offset.

## Method of the estimates
- **RP2040: rp2040js** (wokwi, commit a4068f7). It runs the real `helix_bench_rp2040*_emu.uf2` (Pico SDK, bootrom). Its Cortex-M0+ timing follows the TRM: 1 cycle per instruction, loads/stores 2, taken branch 2, BL 3, single-cycle MULS, zero-wait-state memory. SysTick in the firmware and the core cycle counter agree.
  - `emu/rp2040js-run-bench.ts` adds a **model of the XIP cache**: 16 KB, 2-way, 8-byte lines, LRU. It counts the misses in each `MP3Decode()` window and charges an **assumed 50–70 system cycles per miss** (QSPI at clk_sys/2, continuous-read quad I/O).
  - Not modelled: bus contention, other masters (DMA), flash timing details.
  - Expected accuracy: about ±10 % for the SRAM case, worse for XIP.
- **RP2350: QEMU** `mps2-an505` (Cortex-M33). The decoder object code is built with exactly the rp2350 Pico SDK flags. QEMU has no timing, so `emu/qemu/m33cost.c` classifies every executed instruction and sums two cost tables:
  - **lo** (optimistic): pipelined loads 1 cycle, taken branch 2, forward conditional branches not taken.
  - **hi** (pessimistic): loads 2, every branch 4, long MAC 2, division 12.
  - The plugin also runs the same XIP cache model over code and rodata.
  - The real M33 should fall between lo and hi for SRAM-resident code. For RP2350 the XIP numbers are given as hi + misses × 50–70.
- **Sanity anchor:** RealNetworks' own figure (`lib/libhelix-mp3/docs/cpuusage.xls`, ARMulator, zero-wait-state memory, hand-written asm) is 17 MHz on ARM9E for 44.1 kHz/128 kbps stereo, which is about 444 k cycles per frame. The M33 estimate of 360–542 k (avg) brackets it.
- The non-emu firmware also runs in rp2040js, as a smoke test: clock switching, VREG, copy_to_ram boot, PCM `OK`. Its cycle numbers there scale with the requested clock because rp2040js doesn't model clock changes, so they are not used.

## Results — ESTIMATES (cycles per frame, CPU % of one core)
CPU % = cycles / (26.12 ms × f). Avg (max) over 308 frames. The full table is in `results/estimates.md`.

| chip, port | clip | memory | cycles/frame avg (max) | @96 MHz | @133 MHz | @150 MHz | @200 MHz |
|---|---|---|---|---|---|---|---|
| RP2040 M0+, C64 | 128k | SRAM | 2.64 M (2.73 M) | 105 % (109) | 76 % (79) | 67 % (70) | 51 % (52) |
| RP2040 M0+, C64 | 128k | XIP +50..70/miss | 2.78–2.83 M (2.90–2.97 M) | 111–113 % | 80–82 % | 71–72 % (74–76) | 53–54 % |
| RP2040 M0+, C64 | 320k | SRAM | 2.81 M (2.89 M) | 112 % (115) | 81 % (83) | 72 % (74) | 54 % (55) |
| RP2040 M0+, C64 | 320k | XIP +50..70/miss | 2.96–3.02 M (3.05–3.11 M) | 118–120 % | 85–87 % | 75–77 % (78–79) | 57–58 % |
| RP2350 M33, DSP | 128k | SRAM lo..hi | 0.36–0.54 M (0.42–0.63 M) | 14–22 % | 10–16 % | 9–14 % (11–16) | n/a |
| RP2350 M33, DSP | 128k | XIP hi +50..70/miss | 0.61–0.63 M (0.72–0.75 M) | 24–25 % | 17–18 % | 15–16 % (18–19) | n/a |
| RP2350 M33, DSP | 320k | SRAM lo..hi | 0.42–0.62 M (0.45–0.68 M) | 17–25 % | 12–18 % | 11–16 % (11–17) | n/a |
| RP2350 M33, DSP | 320k | XIP hi +50..70/miss | 0.70–0.73 M (0.77–0.81 M) | 28–29 % | 20–21 % | 18–19 % (20–21) | n/a |

- RP2040 is rated 133 MHz at the default 1.10 V; 200 MHz is certified at VREG ≥ 1.15 V (SDK ≥ 2.1.1). 150 MHz is also run at 1.15 V here. RP2350 is rated 150 MHz.
- The M0 port (16x16 partial products, inlined) is **slower**: 3.9–4.0 M cycles in SRAM. Its larger inlined code also thrashes the XIP cache (about 25 k misses per frame against about 2.8 k), giving 5.1–5.8 M cycles from flash. So C64 is the default for RP2040.
- Decode cost hardly depends on the bitrate: 320k costs about 6 % (M0+) to 15 % (M33) more than 128k.

## Memory footprint
| | RP2040 (C64) | RP2350 (DSP) |
|---|---|---|
| decoder code (`.text`, library before `--gc-sections`) | 42.2 KB | 31.3 KB |
| constant tables (`.rodata`) | 12.7 KB | 12.7 KB |
| initialised data | 0.6 KB | 0.6 KB |
| heap (`MP3InitDecoder`, measured) | 23,816 B | 23,816 B |
| PCM output per frame (16-bit stereo) | 4,608 B | 4,608 B |
| input buffer needed | ≥ 1,940 B (`MAINBUF_SIZE`), 4 KB+ in practice | same |
| main stack high-water (decoder + benchmark, rp2040js) | 1,052 B (M0 port: 2,208 B) | not measured (QEMU) |
| whole benchmark, `copy_to_ram` image in SRAM | 108 KB | 93 KB |

The heap figure matches RealNetworks' `docs/memory.xls` exactly (23,816 B).

## Recommendation
- **Focus on Pico 2 (RP2350).** The estimated decode load is 15–21 % of one core at 150 MHz from flash, and still under a third of a core at 96 MHz. It leaves room for FatFs, the LCD, cover-art JPEG decoding and the I2S path without special tricks.
- **RP2040 is marginal.** The estimate is 67–79 % of one core at 150 MHz, 76–90 % at 133 MHz, and over 100 % at 96 MHz. It is only workable as a dedicated decoder core at 200 MHz (51–60 %, VREG 1.15 V) with the hot code in SRAM. Even then the margin depends on effects that aren't modelled (bus contention with the LCD/SD SPI DMA and the other core, real XIP behaviour), and the Huffman/IMDCT worst cases on real music can be heavier than the synthetic clips. Treat RP2040 MP3 as a stretch goal and decide after `helix_bench_rp2040*.uf2` has run on a real board. An optimised Thumb-1 `MULSHIFT32`/`MADD64` in asm or RAM would be the first thing to try there.

## What the decode-pipeline refactor needs (not done here)
1. **Decode in thread context, not in the I2S DMA IRQ.** One `MP3Decode()` takes 0.4–0.8 M cycles on M33 (2.6–3.1 M on M0+), far longer than an I2S buffer period. Use a decoder task (on core1, or the core that owns FatFs) that writes PCM into a ring of at least 2–3 frames (4.6 KB each). The IRQ then only copies, converts 16→32 bit and applies the gain ramp (`GainRamp`/`Fader` from #2 can be reused).
2. **Input buffering with compaction and refill.** Helix wants contiguous bytes and a `bytesLeft` count (at least one frame plus the 511-byte reservoir). That differs from the current ReadBuffer item API. Pull the data from ReadBuffer in thread context and keep the FatFs core-ownership rule; core1 owning FatFs and the decoder fits it.
3. **Stream parsing:**
   - Skip ID3v2 (TagRead already parses it).
   - Parse the Xing/Info/VBRI header for duration, VBR and the seek table.
   - Handle MPEG-2/2.5 (576 samples per frame) and 32/22.05/16 kHz and other rates in the I2S retune path.
   - Seek: jump to a byte offset, resync with `MP3FindSyncWord`, and drop the output until the bit reservoir is valid (`ERR_MP3_MAINDATA_UNDERFLOW`).
   - Resume: store the byte position and samples played.
4. **Error handling:**
   - Skip and resync on broken frames with a mute ramp.
   - Detect bitrate and sample-rate changes inside one stream.
   - On underrun, keep the existing instant mute.
5. **Memory:**
   - +24 KB heap, plus about 10 KB of PCM and input buffers, plus about 45–55 KB of flash.
   - On RP2040, place the hot functions (IMDCT, polyphase, dequant, huffman) in SRAM (`__not_in_flash_func` needs a source change or a linker-script rule). The decoder task stack should be at least 2–3 KB.
6. **Clocks:**
   - RP2350 can stay at 150 MHz or lower.
   - RP2040 needs 200 MHz with VREG 1.15 V. Check that the I2S PIO dividers and the SPI SD/LCD clocks still work at that speed.
7. **Licensing:** RPSL obligations for MP3-capable firmware releases (see the license note above).
