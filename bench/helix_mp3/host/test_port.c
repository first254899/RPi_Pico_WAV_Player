/* M0 port of MULSHIFT32/MADD64 (16x16 partial products) against the plain 64-bit C version */
#include <stdint.h>
#include <stdio.h>

#define HELIX_PORT_MODE HELIX_PORT_M0
#include "helix_port.h"

static int64_t ref_madd(int64_t s, int x, int y) { return s + (int64_t) x * y; }
static int ref_mulshift(int x, int y) { return (int) (((int64_t) x * y) >> 32); }

static uint64_t st = 88172645463325252ull;
static uint32_t rnd(void) { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return (uint32_t) st; }

int main(void)
{
    const int edge[] = {0, 1, -1, 2, -2, 0x7fff, 0x8000, -0x8000, 0xffff, 0x10000, -0x10000,
                        0x7fffffff, (int) 0x80000000, (int) 0x80000001, 0x40000000, -0x40000000};
    long fails = 0, n = 0;
    for (unsigned i = 0; i < sizeof(edge) / sizeof(*edge); i++) {
        for (unsigned j = 0; j < sizeof(edge) / sizeof(*edge); j++) {
            int x = edge[i], y = edge[j];
            fails += MULSHIFT32(x, y) != ref_mulshift(x, y);
            fails += MADD64(12345, x, y) != ref_madd(12345, x, y);
            n += 2;
        }
    }
    for (long k = 0; k < 20000000; k++) {
        int x = (int) rnd(), y = (int) rnd();
        if (k & 1) { x >>= rnd() & 31; }
        int64_t s = (int64_t) (((uint64_t) rnd() << 32) | rnd()) >> 2;
        fails += MULSHIFT32(x, y) != ref_mulshift(x, y);
        fails += MADD64(s, x, y) != ref_madd(s, x, y);
        n += 2;
    }
    printf("test_port: %ld checks, %ld failed\n", n, fails);
    return fails != 0;
}
