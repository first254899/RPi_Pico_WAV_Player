/*-----------------------------------------------------------/
/ m33cost: QEMU TCG plugin, Cortex-M33 cycle ESTIMATE + XIP cache model
/------------------------------------------------------------/
/ SPDX-License-Identifier: GPL-2.0-or-later
/ (a QEMU plugin built against qemu-plugin.h, like QEMU's own contrib/plugins; not part of any firmware)
/-----------------------------------------------------------*/
// QEMU executes instructions but has no timing. This plugin classifies every executed Thumb/Thumb-2
// instruction and adds a cost from two tables:
//   lo: optimistic (pipelined loads 1 cycle, taken branch 2, not-taken forward branches 1)
//   hi: pessimistic (loads 2, taken branch 4, every conditional branch assumed taken, long MAC 2)
// The real Cortex-M33 (no caches of its own, single issue) should be between the two when code and
// data are in zero-wait-state SRAM. It also simulates an RP2350-like XIP cache (16 KB, 2-way, 8-byte
// lines, LRU) for instruction fetches and data loads inside [flash_lo, flash_hi) (text + rodata of the
// image), so misses * penalty estimates running from flash. Windows end at each execution of the
// address `hook` (bench_cycles()); the per-window totals are written to `out` at exit.
//   qemu-system-arm -M mps2-an505 -semihosting -nographic -kernel x.elf
//     -plugin ./libm33cost.so,hook=0x...,flash_lo=0x...,flash_hi=0x...,out=windows.csv
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qemu-plugin.h"

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static uint64_t hook, flash_lo, flash_hi;
static const char* out_path = "m33cost.csv";

static uint64_t insns, cyc_lo, cyc_hi;  // single vCPU: plain counters updated by callbacks
static uint64_t accesses, misses;

typedef struct { uint64_t insns, lo, hi, acc, miss; } win_t;
static win_t* wins;
static size_t nwins, capwins;
static win_t last;

// ---- XIP cache model ----
#define WAYS 2
#define LINE 8
#define SETS (16 * 1024 / LINE / WAYS)
static int64_t tags[SETS][WAYS];
static uint8_t victim[SETS];

static inline void cache_access(uint64_t addr)
{
    if (addr < flash_lo || addr >= flash_hi) { return; }
    accesses++;
    uint64_t line = (addr - flash_lo) / LINE;
    uint32_t set = (uint32_t) (line % SETS);
    int64_t tag = (int64_t) (line / SETS);
    if (tags[set][0] == tag) { victim[set] = 1; return; }
    if (tags[set][1] == tag) { victim[set] = 0; return; }
    misses++;
    tags[set][victim[set]] = tag;
    victim[set] ^= 1;
}

// ---- instruction classes ----
typedef struct { uint32_t lo, hi; uint8_t mem; } cost_t;

static int popcount16(uint32_t x) { return __builtin_popcount(x & 0xffff); }

static cost_t classify(uint32_t hw1, uint32_t hw2, int wide)
{
    cost_t c = {1, 1, 0};
    if (!wide) {
        if ((hw1 >> 11) == 0x09) { c.lo = 1; c.hi = 2; c.mem = 1; return c; }               // LDR literal
        if ((hw1 >> 12) == 0x5) {                                                            // LDR/STR register
            int op = (hw1 >> 9) & 7;
            if (op >= 3) { c.lo = 1; c.hi = 2; c.mem = 1; } else { c.mem = 2; }
            return c;
        }
        if ((hw1 >> 13) == 0x3 || (hw1 >> 12) == 0x8 || (hw1 >> 12) == 0x9) {               // LDR/STR imm, SP
            if (hw1 & 0x0800) { c.lo = 1; c.hi = 2; c.mem = 1; } else { c.mem = 2; }
            return c;
        }
        if ((hw1 & 0xfe00) == 0xb400) { c.lo = c.hi = 1 + popcount16(hw1 & 0x1ff); c.mem = 2; return c; } // PUSH
        if ((hw1 & 0xfe00) == 0xbc00) {                                                      // POP
            c.lo = c.hi = 1 + popcount16(hw1 & 0x1ff);
            if (hw1 & 0x100) { c.lo += 2; c.hi += 3; }
            c.mem = 1;
            return c;
        }
        if ((hw1 >> 11) == 0x18) { c.lo = c.hi = 1 + popcount16(hw1 & 0xff); c.mem = 2; return c; } // STM
        if ((hw1 >> 11) == 0x19) { c.lo = c.hi = 1 + popcount16(hw1 & 0xff); c.mem = 1; return c; } // LDM
        if ((hw1 & 0xf500) == 0xb100) { c.lo = 1; c.hi = 3; return c; }                     // CBZ/CBNZ (forward)
        if ((hw1 >> 12) == 0xd && ((hw1 >> 8) & 0xf) < 0xe) {                                // B<cond>
            if (hw1 & 0x80) { c.lo = 2; c.hi = 4; } else { c.lo = 1; c.hi = 4; }             // backward / forward
            return c;
        }
        if ((hw1 >> 11) == 0x1c) { c.lo = 2; c.hi = 4; return c; }                           // B
        if ((hw1 & 0xff00) == 0x4700) { c.lo = 2; c.hi = 4; return c; }                      // BX/BLX
        if ((hw1 & 0xff87) == 0x4687) { c.lo = 2; c.hi = 4; return c; }                      // MOV pc, Rm
        return c;                                                                            // ALU, MULS, IT, ...
    }
    uint32_t op1 = (hw1 >> 11) & 3;
    if (op1 == 1) {
        if ((hw1 & 0xfe40) == 0xe800) {                                                      // LDM/STM
            c.lo = c.hi = 1 + popcount16(hw2);
            if ((hw1 & 0x0010) && (hw2 & 0x8000)) { c.lo += 2; c.hi += 3; }
            c.mem = (hw1 & 0x0010) ? 1 : 2;
            return c;
        }
        if ((hw1 & 0xfe40) == 0xe840) {                                                      // LDRD/STRD/LDREX/TBB
            if ((hw1 & 0xfff0) == 0xe8d0 && (hw2 & 0xffe0) == 0xf000) { c.lo = 3; c.hi = 5; c.mem = 1; return c; } // TBB/TBH
            if (hw1 & 0x0010) { c.lo = 2; c.hi = 3; c.mem = 1; } else { c.lo = 2; c.hi = 3; c.mem = 2; }
            return c;
        }
        return c;                                                                            // data processing (reg), coproc
    }
    if (op1 == 2) {
        if (hw2 & 0x8000) {                                                                  // branches, misc control
            if (hw2 & 0x4000) { c.lo = 2; c.hi = 4; return c; }                              // BL / BLX
            if (hw2 & 0x1000) { c.lo = 2; c.hi = 4; return c; }                              // B.W
            if (((hw1 >> 6) & 0xf) < 0xe) {                                                  // B<cond>.W
                if (hw1 & 0x0400) { c.lo = 2; c.hi = 4; } else { c.lo = 1; c.hi = 4; }
                return c;
            }
            return c;                                                                        // MSR/MRS/hints
        }
        return c;                                                                            // data processing (imm)
    }
    // op1 == 3
    if ((hw1 & 0xfe00) == 0xf800) {                                                          // load/store single
        if (hw1 & 0x0010) {
            c.lo = 1; c.hi = 2; c.mem = 1;
            if (((hw2 >> 12) & 0xf) == 0xf && (hw1 & 0x0060) == 0x0040) { c.lo += 2; c.hi += 3; } // LDR pc
        } else {
            c.mem = 2;
        }
        return c;
    }
    if ((hw1 & 0xff80) == 0xfb00) { c.lo = 1; c.hi = 1; return c; }                         // MUL/MLA/SMMUL/...
    if ((hw1 & 0xff80) == 0xfb80) {                                                          // long mul, MAC, DIV
        uint32_t op = (hw1 >> 4) & 7;
        if ((op == 1 || op == 3) && ((hw2 >> 4) & 0xf) == 0xf) { c.lo = 2; c.hi = 12; return c; } // SDIV/UDIV
        c.lo = 1; c.hi = (op >= 4) ? 2 : 1;                                                  // SMLAL etc: 1..2
        return c;
    }
    return c;                                                                                // data processing (reg), FP
}

// ---- callbacks ----
typedef struct { uint32_t lo, hi; uint64_t vaddr; } insn_info_t;
typedef struct { uint64_t start, end; } tb_info_t;

static void insn_exec(unsigned int vcpu, void* ud)
{
    (void) vcpu;
    const insn_info_t* ii = ud;
    insns++;
    cyc_lo += ii->lo;
    cyc_hi += ii->hi;
}

static void hook_exec(unsigned int vcpu, void* ud)
{
    (void) vcpu; (void) ud;
    if (nwins == capwins) {
        capwins = capwins ? capwins * 2 : 4096;
        wins = realloc(wins, capwins * sizeof(*wins));
    }
    win_t now = {insns, cyc_lo, cyc_hi, accesses, misses};
    wins[nwins++] = (win_t) {now.insns - last.insns, now.lo - last.lo, now.hi - last.hi,
                             now.acc - last.acc, now.miss - last.miss};
    last = now;
}

static void tb_exec(unsigned int vcpu, void* ud)
{
    (void) vcpu;
    const tb_info_t* t = ud;
    for (uint64_t a = t->start & ~(uint64_t) (LINE - 1); a < t->end; a += LINE) { cache_access(a); }
}

static void mem_cb(unsigned int vcpu, qemu_plugin_meminfo_t info, uint64_t vaddr, void* ud)
{
    (void) vcpu; (void) ud;
    if (!qemu_plugin_mem_is_store(info)) { cache_access(vaddr); }
}

static void tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb* tb)
{
    (void) id;
    size_t n = qemu_plugin_tb_n_insns(tb);
    if (n == 0) { return; }
    uint64_t start = qemu_plugin_insn_vaddr(qemu_plugin_tb_get_insn(tb, 0));
    struct qemu_plugin_insn* li = qemu_plugin_tb_get_insn(tb, n - 1);
    tb_info_t* t = malloc(sizeof(*t));
    t->start = start;
    t->end = qemu_plugin_insn_vaddr(li) + qemu_plugin_insn_size(li);
    qemu_plugin_register_vcpu_tb_exec_cb(tb, tb_exec, QEMU_PLUGIN_CB_NO_REGS, t);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn* insn = qemu_plugin_tb_get_insn(tb, i);
        uint8_t b[4] = {0};
        size_t sz = qemu_plugin_insn_data(insn, b, sizeof(b));
        uint32_t hw1 = b[0] | (b[1] << 8), hw2 = b[2] | (b[3] << 8);
        cost_t c = classify(hw1, hw2, sz == 4);
        insn_info_t* ii = malloc(sizeof(*ii));
        ii->lo = c.lo; ii->hi = c.hi; ii->vaddr = qemu_plugin_insn_vaddr(insn);
        if ((ii->vaddr & ~1ull) == hook) {
            qemu_plugin_register_vcpu_insn_exec_cb(insn, hook_exec, QEMU_PLUGIN_CB_NO_REGS, NULL);
        }
        qemu_plugin_register_vcpu_insn_exec_cb(insn, insn_exec, QEMU_PLUGIN_CB_NO_REGS, ii);
        if (c.mem == 1) { qemu_plugin_register_vcpu_mem_cb(insn, mem_cb, QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_MEM_R, NULL); }
    }
}

static void at_exit(qemu_plugin_id_t id, void* ud)
{
    (void) id; (void) ud;
    FILE* f = fopen(out_path, "w");
    if (!f) { return; }
    fprintf(f, "insns,cyc_lo,cyc_hi,flash_accesses,flash_misses\n");
    for (size_t i = 0; i < nwins; i++) {
        fprintf(f, "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
            wins[i].insns, wins[i].lo, wins[i].hi, wins[i].acc, wins[i].miss);
    }
    fprintf(f, "# total insns %" PRIu64 " lo %" PRIu64 " hi %" PRIu64 "\n", insns, cyc_lo, cyc_hi);
    fclose(f);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t* info, int argc, char** argv)
{
    (void) info;
    memset(tags, 0xff, sizeof(tags));
    for (int i = 0; i < argc; i++) {
        char* v = strchr(argv[i], '=');
        if (!v) { continue; }
        *v++ = 0;
        if (!strcmp(argv[i], "hook")) { hook = strtoull(v, NULL, 0) & ~1ull; }
        else if (!strcmp(argv[i], "flash_lo")) { flash_lo = strtoull(v, NULL, 0); }
        else if (!strcmp(argv[i], "flash_hi")) { flash_hi = strtoull(v, NULL, 0); }
        else if (!strcmp(argv[i], "out")) { out_path = strdup(v); }
    }
    qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans);
    qemu_plugin_register_atexit_cb(id, at_exit, NULL);
    return 0;
}
