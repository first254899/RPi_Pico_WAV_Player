#!/usr/bin/env python3
"""Turn emulator window CSVs (emu/qemu/m33cost.c, emu/rp2040js-run-bench.ts) into ESTIMATED cycles per
frame and CPU % at 96/133/150 MHz. Every row is one window between two executions of bench_cycles();
decode windows are the odd rows (see core/bench_decode.c). 308 frames per clip, clips 128k then 320k.
  analyze.py <label> <windows.csv> [<label> <windows.csv> ...]"""
import csv
import sys

FRAMES = 308
FRAME_S = 1152 / 44100
FREQS = (96e6, 133e6, 150e6, 200e6)  # 200 MHz: RP2040 only (certified at 1.15 V)
PENALTIES = (50, 70)  # sys cycles per XIP cache miss (8-byte line, QSPI at sys/2), assumed


def load(path):
    rows = [r for r in csv.reader(open(path)) if r and not r[0].startswith(('#', 'insns'))]
    rows = [[int(x) for x in r] for r in rows]
    return rows[1::2]  # decode windows


def st(xs):
    return min(xs), sum(xs) / len(xs), max(xs)


def main(args):
    print('| model | clip | memory | cycles/frame min / avg / max | CPU % avg (max) @96 / 133 / 150 / 200 MHz |')
    print('|---|---|---|---|---|')
    for label, path in zip(args[0::2], args[1::2]):
        dec = load(path)
        for c, clip in enumerate(('128k', '320k')):
            w = dec[c * FRAMES:(c + 1) * FRAMES]
            cases = []
            if w[0][1] == w[0][2]:
                cases.append(('SRAM (0 wait)', [x[1] for x in w]))
            else:
                cases.append(('SRAM, lo model', [x[1] for x in w]))
                cases.append(('SRAM, hi model', [x[2] for x in w]))
            for p in PENALTIES:
                cases.append((f'XIP, +{p}/miss' + ('' if w[0][1] == w[0][2] else ', hi'), [x[2] + x[4] * p for x in w]))
            ins = [x[0] for x in w]
            for name, xs in cases:
                mn, av, mx = st(xs)
                cpu = ' / '.join('n/a' if (f > 150e6 and 'RP2350' in label) else
                                 f'{100 * av / (FRAME_S * f):.0f} ({100 * mx / (FRAME_S * f):.0f})' for f in FREQS)
                print(f'| {label} | {clip} | {name} | {mn:,.0f} / {av:,.0f} / {mx:,.0f} | {cpu} |')
            if ins[0]:
                mi = st([x[4] for x in w])
                print(f'| {label} | {clip} | (instructions; XIP misses) | {st(ins)[1]:,.0f} insns avg; misses avg {mi[1]:,.0f} max {mi[2]:,} | |')
            else:
                mi = st([x[4] for x in w])
                print(f'| {label} | {clip} | (XIP misses) | misses avg {mi[1]:,.0f} max {mi[2]:,} | |')


if __name__ == '__main__':
    main(sys.argv[1:])
