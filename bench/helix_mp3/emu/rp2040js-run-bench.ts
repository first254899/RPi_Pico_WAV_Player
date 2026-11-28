// Runs a helix_bench_rp2040*_emu.uf2 in rp2040js (https://github.com/wokwi/rp2040js) and adds what
// rp2040js does not model: an RP2040 XIP cache (16 KB, 2-way, 8-byte lines, LRU) in front of the flash.
//   cp emu/rp2040js-run-bench.ts <rp2040js>/demo/ && cd <rp2040js> && npm ci --ignore-scripts
//   npx tsx demo/rp2040js-run-bench.ts helix_bench_rp2040_emu.uf2 0x<address of bench_cycles> [windows.csv]
// rp2040js counts ~ Cortex-M0+ cycles per instruction with zero-wait-state memory (so its numbers
// correspond to code and tables in SRAM). Each execution of bench_cycles() ends a window; windows
// alternate between "overhead" and "MP3Decode()" (see core/bench_decode.c), and for every decode window
// the XIP cache misses are counted, so cycles + misses * penalty estimates the flash (XIP) build.
import * as fs from 'fs';
import { Simulator } from '../src/simulator.js';
import { bootromB1 } from './bootrom.js';
import { loadUF2 } from './load-flash.js';

const image = process.argv[2];
const hook = parseInt(process.argv[3], 16) & ~1;
const sim = new Simulator();
const mcu = sim.rp2040;
mcu.loadBootrom(bootromB1);
loadUF2(image, mcu);

// XIP cache model (cached alias 0x10000000..0x10ffffff only)
const WAYS = 2, LINE = 8, SETS = (16 * 1024) / LINE / WAYS;
const tags = new Int32Array(SETS * WAYS).fill(-1);
const lru = new Uint8Array(SETS); // way to replace next
let misses = 0, accesses = 0;
function xip(addr: number) {
  addr >>>= 0;
  if (addr < 0x10000000 || addr >= 0x11000000) return;
  accesses++;
  const line = (addr - 0x10000000) >>> 3;
  const set = line % SETS, tag = (line / SETS) | 0;
  const b = set * WAYS;
  if (tags[b] === tag) { lru[set] = 1; return; }
  if (tags[b + 1] === tag) { lru[set] = 0; return; }
  misses++;
  const w = lru[set];
  tags[b + w] = tag;
  lru[set] = w ^ 1;
}
const r32 = mcu.readUint32.bind(mcu), r16 = mcu.readUint16.bind(mcu), r8 = mcu.readUint8.bind(mcu);
mcu.readUint32 = (a: number) => { xip(a); return r32(a); };
mcu.readUint16 = (a: number) => { xip(a); return r16(a); };
mcu.readUint8 = (a: number) => { xip(a); return r8(a); };

// windows between executions of bench_cycles()
const core = mcu.core;
const exec = core.executeInstruction.bind(core);
let lastCycles = 0, lastMisses = 0, lastAccesses = 0, hits = 0;
const windows: { cycles: number; misses: number; accesses: number }[] = [];
core.executeInstruction = () => {
  if ((core.PC & ~1) === hook) {
    windows.push({ cycles: core.cycles - lastCycles, misses: misses - lastMisses, accesses: accesses - lastAccesses });
    lastCycles = core.cycles; lastMisses = misses; lastAccesses = accesses;
    hits++;
  }
  return exec();
};

let line = '';
const t0 = Date.now();
mcu.uart[0].onByte = (v: number) => {
  const ch = String.fromCharCode(v);
  process.stdout.write(ch);
  line += ch;
  if (ch !== '\n') return;
  if (line.includes('BENCH DONE')) finish();
  line = '';
};

function stats(xs: number[]) {
  const s = xs.reduce((a, b) => a + b, 0);
  return { min: Math.min(...xs), avg: s / xs.length, max: Math.max(...xs) };
}

function finish() {
  // bench_cycles() is called before (t0) and after (t1) every MP3Decode(); each clip starts with cycles_init()
  // which doesn't call it, so pairs are (call 2k+1 -> 2k+2) within the bench loop: take every window that
  // ends on an even call index (1-based), i.e. windows[1], windows[3], ...
  const dec = windows.filter((_, i) => i % 2 === 1);
  if (process.argv[4]) {
    // same columns as emu/qemu/m33cost.c (insns unknown here: 0; lo = hi = rp2040js cycles)
    const rows = windows.map((w) => `0,${w.cycles},${w.cycles},${w.accesses},${w.misses}`);
    fs.writeFileSync(process.argv[4], 'insns,cyc_lo,cyc_hi,flash_accesses,flash_misses\n' + rows.join('\n') + '\n');
  }
  const perClip = 308;
  console.log(`[rp2040js] bench_cycles hits ${hits}, decode windows ${dec.length}, total core cycles ${core.cycles}, host ${(Date.now() - t0) / 1000} s`);
  for (let c = 0; c * perClip < dec.length; c++) {
    const w = dec.slice(c * perClip, (c + 1) * perClip);
    const cy = stats(w.map((x) => x.cycles)), mi = stats(w.map((x) => x.misses));
    const ac = stats(w.map((x) => x.accesses));
    const withPen = (p: number) => stats(w.map((x) => x.cycles + x.misses * p));
    const p50 = withPen(50), p70 = withPen(70);
    console.log(`[rp2040js] clip ${c}: frames ${w.length} cycles(0ws) min ${cy.min} avg ${cy.avg.toFixed(0)} max ${cy.max}` +
      ` | XIP accesses avg ${ac.avg.toFixed(0)} misses min ${mi.min} avg ${mi.avg.toFixed(0)} max ${mi.max} (miss rate ${(100 * mi.avg / ac.avg).toFixed(2)} %)` +
      ` | +50cyc/miss avg ${p50.avg.toFixed(0)} max ${p50.max} | +70cyc/miss avg ${p70.avg.toFixed(0)} max ${p70.max}`);
  }
  process.exit(0);
}

core.PC = 0x10000000;
sim.execute();
setTimeout(() => { console.log('[rp2040js] TIMEOUT'); process.exit(1); }, 60 * 60 * 1000);
