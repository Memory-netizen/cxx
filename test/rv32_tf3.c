/* Soft-float helpers for the bare-metal rv32 test runs.
 *
 * LLVM lowers fp128 operations on rv32 (ilp32d) to calls with fp128
 * passed BY REFERENCE (sret pointer for results); these shims implement
 * that ABI on top of the vendored fp128 library. Also provides the
 * 64-bit division helpers gcc emits on rv32, since no libgcc rv32
 * multilib exists in the riscv64-linux-gnu toolchain.
 *
 * Compiled freestanding for rv32; must not itself pull in any libcalls.
 */
#include <stdint.h>

#include "../src/support/fp128.h"

void __addtf3(Fp128 *ret, Fp128 *a, Fp128 *b) { *ret = fp128_add(*a, *b); }

int __eqtf2(Fp128 *a, Fp128 *b) { return fp128_cmp(*a, *b) == 0 ? 0 : 1; }

int __netf2(Fp128 *a, Fp128 *b) { return fp128_cmp(*a, *b) != 0; }

double __trunctfdf2(Fp128 *a) {
  union {
    uint64_t u;
    double d;
  } u;
  u.u = fp128_to_fp64_bits(*a);
  return u.d;
}

void __floatsitf(Fp128 *ret, int v) {
  uint32_t s = v < 0 ? 0xFFFFFFFFu : 0;
  Int128 i = {{(uint32_t)v, s, s, s}};
  *ret = fp128_from_int128(i, SIGNED);
}

/* Restoring 64-bit division: shift-and-subtract, no libcalls. */
uint64_t __udivdi3(uint64_t n, uint64_t d) {
  if (d == 0)
    return 0;
  uint64_t q = 0;
  for (int i = 63; i >= 0; i--)
    if ((n >> i) >= d) {
      q |= (uint64_t)1 << i;
      n -= d << i;
    }
  return q;
}

uint64_t __umoddi3(uint64_t n, uint64_t d) {
  if (d == 0)
    return 0;
  for (int i = 63; i >= 0; i--)
    if ((n >> i) >= d)
      n -= d << i;
  return n;
}

int64_t __divdi3(int64_t a, int64_t b) {
  int neg = (a < 0) ^ (b < 0);
  uint64_t q = __udivdi3(a < 0 ? -(uint64_t)a : (uint64_t)a,
                        b < 0 ? -(uint64_t)b : (uint64_t)b);
  return neg ? -(int64_t)q : (int64_t)q;
}

int64_t __moddi3(int64_t a, int64_t b) {
  uint64_t r = __umoddi3(a < 0 ? -(uint64_t)a : (uint64_t)a,
                         b < 0 ? -(uint64_t)b : (uint64_t)b);
  return a < 0 ? -(int64_t)r : (int64_t)r;
}
