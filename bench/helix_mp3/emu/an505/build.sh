#!/bin/sh
# builds helix_bench_an505.elf with the same compiler flags as the rp2350 Pico build of the decoder
set -e
H=$(cd "$(dirname "$0")/../.." && pwd)        # bench/helix_mp3
L=$H/../../lib/libhelix-mp3
O=${1:-/tmp/an505}
mkdir -p $O
CF="-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=softfp -O3 -DNDEBUG -ffunction-sections -fdata-sections"
sed "s|@CLIP_DIR@|$H/clips|" $H/pico/clips.S.in | sed 's/\.flashdata\.clips/.rodata.clips/' > $O/clips.S
for f in mp3dec.c mp3tabs.c real/bitstream.c real/buffers.c real/dct32.c real/dequant.c real/dqchan.c real/huffman.c \
         real/hufftabs.c real/imdct.c real/polyphase.c real/scalfact.c real/stproc.c real/subband.c real/trigtabs.c; do
    arm-none-eabi-gcc $CF -w -include $H/port/helix_port.h -DHELIX_PORT_MODE=HELIX_PORT_DSP -DHELIX_BENCH_MALLOC \
        -I$L/pub -I$L/real -c $L/$f -o $O/$(basename $f .c).o
done
arm-none-eabi-gcc $CF -Wall -Wextra -I$H/core -I$L/pub -c $H/core/bench_decode.c -o $O/bench_decode.o
arm-none-eabi-gcc $CF -Wall -Wextra -I$H/core -c $H/emu/an505/main_an505.c -o $O/main_an505.o
arm-none-eabi-gcc $CF -Wall -Wextra -c $H/emu/an505/startup.c -o $O/startup.o
arm-none-eabi-gcc $CF -c $O/clips.S -o $O/clips.o
arm-none-eabi-gcc $CF --specs=rdimon.specs -nostartfiles -T $H/emu/an505/an505.ld -Wl,--gc-sections \
    -Wl,--no-warn-rwx-segments $O/*.o -o $O/helix_bench_an505.elf
arm-none-eabi-size $O/helix_bench_an505.elf
