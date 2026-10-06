#include <float.h>

#include "test.h"

// The alignment of a _BitInt wider than one word. AAPCS64 gives such a type
// sixteen-byte alignment; SysV AMD64 and RISC-V cap it at the long long
// alignment, as clang did everywhere before LLVM 105dd60 ("[Clang][AArch64]
// Fixed incorrect _BitInt alignment", #90602). The size is sixteen either
// way, so this one number decides the layout of any struct holding one.
#ifdef __aarch64__
#define WIDE_BITINT_ALIGN 16
#else
#define WIDE_BITINT_ALIGN 8
#endif

int main() {
    // Sizes and alignments
    ASSERT(1, sizeof(_BitInt(8)));
    ASSERT(4, sizeof(_BitInt(20)));
    ASSERT(16, sizeof(_BitInt(65)));
    ASSERT(16, sizeof(_BitInt(128)));
    ASSERT(1, sizeof(unsigned _BitInt(1)));
    ASSERT(WIDE_BITINT_ALIGN, _Alignof(_BitInt(70)));

    // wb / uwb literal suffixes
    ASSERT(1, 1wb == (_BitInt(2))1);
    ASSERT(1, 1uwb == (unsigned _BitInt(1))1);
    ASSERT(3, (int)3wb);
    ASSERT(1, 127wb == (_BitInt(8))127);
    ASSERT(1, 128wb == (_BitInt(9))128);
    ASSERT(1, _Generic(1wb, _BitInt(2): 1, default: 0));
    ASSERT(1, _Generic(1uwb, unsigned _BitInt(1): 1, default: 0));

    // Wide literals beyond 64 bits
    ASSERT(1, 0x123456789abcdef01wb == (_BitInt(66))0x123456789abcdef01wb);

    // Arithmetic with wrap semantics
    ASSERT(1, (unsigned _BitInt(8))255 + 1 == 256);  // promotes to int
    ASSERT(1, (_BitInt(8))127 + 1 == 128);           // promotes to int
    ASSERT(1, (_BitInt(8)) - 128 - 1 == -129);
    ASSERT(1, (_BitInt(70))7 * 9 == 63);
    ASSERT(1, (_BitInt(70)) - 7 / 2 == -3);
    ASSERT(1, (_BitInt(70))7 % 3 == 1);
    ASSERT(1, (_BitInt(70)) - 7 % 3 == -1);

    // Wide arithmetic above 64 bits
    ASSERT(1, ((_BitInt(70))1 << 65) + 1 > 0);
    ASSERT(1, (((_BitInt(70))1 << 65) >> 65) == 1);
    ASSERT(1, ((unsigned _BitInt(128)) - 1) >> 127 == 1);
    ASSERT(1, (_BitInt(128)) - 1 >> 127 == -1);

    // Comparisons and logic
    ASSERT(1, (_BitInt(70))1 < (_BitInt(70))2);
    ASSERT(1, (unsigned _BitInt(70)) - 1 > 0);
    ASSERT(1, ~(_BitInt(70))0 == -1);
    ASSERT(1, ((_BitInt(70))5 & 3) == 1);
    ASSERT(1, ((_BitInt(70))5 | 3) == 7);
    ASSERT(1, ((_BitInt(70))5 ^ 3) == 6);

    // Casts
    ASSERT(5, (int)(_BitInt(70))5);
    ASSERT(1, (_BitInt(70))5 == 5);
    ASSERT(1, (_BitInt(8))300 == 44);                // truncation
    ASSERT(1, (_BitInt(70))(_BitInt(8)) - 1 == -1);  // sign extension
    ASSERT(1, (_BitInt(8)) - 1 == -1);

    // Mixed with standard integers follows the wider type
    ASSERT(1, _Generic((_BitInt(40))1 + 2, _BitInt(40): 1, default: 0));

    // Promotion of small _BitInt to int (C23 6.3.1.1)
    ASSERT(1, _Generic((_BitInt(8))1 + 1, int: 1, default: 0));
    ASSERT(1, _Generic((unsigned _BitInt(8))1 + 1, int: 1, default: 0));

    // Array indexing
    _BitInt(70) a[2] = {5, 6};
    ASSERT(6, a[1]);
    ASSERT(1, &a[1] - &a[0] == 1);

    // === wrap boundaries (unsigned defined; signed LLVM wrap) ===
    _BitInt(77) smax = (((_BitInt(77))1) << 76) - 1;  // 2^76-1
    ASSERT(1, smax > 0);
    ASSERT(1, smax + 1 < 0);  // wraps to the sign bit
    unsigned _BitInt(77) umax = (unsigned _BitInt(77)) - 1;
    ASSERT(0, umax + 1);
    ASSERT(1, umax - 1 < umax);
    unsigned _BitInt(128) u128 = (unsigned _BitInt(128)) - 1;
    ASSERT(0, u128 + 1);
    ASSERT(1, u128 % 2);
    unsigned _BitInt(128) h128 = ((unsigned _BitInt(128))1) << 127;
    ASSERT(0, h128 * 2);  // 2^128 wraps to 0
    ASSERT(1, u128 / 2 == h128 - 1);
    // bitwise ops across the 64-bit boundary
    unsigned _BitInt(77) bm = ((unsigned _BitInt(77))0xF) << 64;
    ASSERT(1, (bm | 1) == (bm + 1));
    ASSERT(1, (bm & 0xF) == 0);
    ASSERT(1, (bm >> 64) == 0xF);
    ASSERT(1, ~bm == umax - bm);
    ASSERT(1, (bm ^ bm) == 0);
    // signed arithmetic shift preserves sign
    _BitInt(77) neg = -8;
    ASSERT(-2, neg >> 2);
    // division truncates toward zero
    _BitInt(77) sd = -7;
    ASSERT(-3, sd / 2);
    ASSERT(-1, sd % 2);
    // small width wrap boundaries
    _BitInt(2) s2 = 1;
    ASSERT(-2, s2 + (_BitInt(2))1);  // 2 -> -2 in i2 (same-type stays _BitInt)
    ASSERT(2, s2 + 1);               // mixed with int: promoted, no wrap
    unsigned _BitInt(1) u1 = 1;
    ASSERT(0, u1 + u1);

    printf("OK\n");
    return 0;
}
