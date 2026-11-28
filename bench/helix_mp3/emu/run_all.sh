#!/bin/sh
# Re-runs the emulator ESTIMATES. Needs: arm-none-eabi-gcc (PATH), qemu-system-arm >= 9 with plugin
# support, gcc, node >= 20, curl, git, and the RP2040 firmware already built in $BB (see ../README.md).
#   emu/run_all.sh [work dir] [rp2040 build dir]
set -e
E=$(cd "$(dirname "$0")" && pwd)
W=${1:-/tmp/helix-emu}
BB=${2:-/tmp/bb-rp2040}
mkdir -p $W
# QEMU plugin API header (GPL-2.0-or-later, not committed): must match the installed QEMU major version
QV=$(qemu-system-arm --version | sed -n 's/.*version \([0-9]*\.[0-9]*\.[0-9]*\).*/\1/p')
[ -f $W/qemu-plugin.h ] || curl -sfL -o $W/qemu-plugin.h https://gitlab.com/qemu-project/qemu/-/raw/v$QV/include/qemu/qemu-plugin.h
gcc -O2 -shared -fPIC -Wall -Wextra -I$E/qemu -I$W -o $W/libm33cost.so $E/qemu/m33cost.c
$E/an505/build.sh $W/an505
ELF=$W/an505/helix_bench_an505.elf
sym() { arm-none-eabi-nm $1 | awk -v s=$2 '$3==s{print "0x"$1}'; }
qemu-system-arm -M mps2-an505 -nographic -semihosting -kernel $ELF \
    -plugin $W/libm33cost.so,hook=$(sym $ELF bench_cycles),flash_lo=$(sym $ELF __code_start),flash_hi=$(sym $ELF __rodata_end),out=$W/m33_windows.csv
# rp2040js (pinned to the commit used for the committed results)
if [ ! -d $W/rp2040js ]; then
    git clone -q https://github.com/wokwi/rp2040js $W/rp2040js
    git -C $W/rp2040js checkout -q a4068f7
    (cd $W/rp2040js && npm ci --ignore-scripts --silent)
fi
cp $E/rp2040js-run-bench.ts $W/rp2040js/demo/
for v in emu m0_emu; do
    (cd $W/rp2040js && npx tsx demo/rp2040js-run-bench.ts $BB/helix_bench_rp2040_$v.uf2 \
        $(sym $BB/helix_bench_rp2040_$v.elf bench_cycles) $W/m0p_$v.csv) | grep -v "Unimplemented\|CortexM0Core\|invalid memory\|undefined address"
done
python3 $E/analyze.py "RP2040 M0+, C64 port (rp2040js)" $W/m0p_emu.csv "RP2040 M0+, M0 port (rp2040js)" $W/m0p_m0_emu.csv \
    "RP2350 M33, DSP port (QEMU+m33cost)" $W/m33_windows.csv
