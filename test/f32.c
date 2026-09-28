#include <float.h>

#include "test.h"

int main() {
    ASSERT(4, sizeof(_Float32));
    ASSERT(4, _Alignof(_Float32));

    // Literals: single rounding to binary32 must match the standard float
    // constants (0.1f32 == (float)0.1 bitwise via value equality)
    ASSERT(1, 0.1f32 == (float)0.1);
    ASSERT(1, 0.1f32 == 0.1f);
    ASSERT(1, 1.0f32 == 1.0f);
    ASSERT(1, 0x1.921fb6p+1f32 == (float)3.14159265);

    // Arithmetic, folded in fp128 then rounded once
    ASSERT(1, 0.1f32 + 0.2f32 == 0.3f32);
    ASSERT(1, 1.5f32 * 2.0f32 == 3.0f32);
    ASSERT(1, 1.0f32 / 3.0f32 == (float)(1.0 / 3.0));
    ASSERT(1, 2.0f32 - 1.0f32 == 1.0f32);
    ASSERT(1, 1.0f32 < 2.0f32 && 2.0f32 > 1.0f32);
    ASSERT(1, -0.0f32 == 0.0f32);

    // Casts
    ASSERT(3, (int)3.9f32);
    ASSERT(-3, (int)-3.9f32);
    ASSERT(1, (_Float32)7 == 7.0f32);
    ASSERT(1, (double)0.5f32 == 0.5);
    ASSERT(1, (_Float16)0.5f32 == 0.5f16);
    ASSERT(1, (_Float128)0.5f32 == 0.5f128);
    ASSERT(1, (_Float64)0.5f32 == 0.5f64);

    // Constants from float.h
    ASSERT(1, 0x1p-149f32 == FLT32_TRUE_MIN);
    ASSERT(1, 0x1.fffffep+127f32 == FLT32_MAX);

    // _Generic selection
    ASSERT(1, _Generic(0.5f32, _Float32: 1, default: 0));

    // Mixed _Float32/float uses the standard type (float)
    ASSERT(1, _Generic(0.5f32 + 0.25f, _Float32: 1, default: 0));

    // === default argument promotions in variadic calls ===
    // float and _Float32 both promote to double (clang-verified); the
    // reader lives in test/common and uses the host va_arg.
    extern double read_first_double(int n, ...);
    extern int read_first_int(int n, ...);
    ASSERT(1, read_first_double(0, 1.5f) == 1.5);
    ASSERT(1, read_first_double(0, 1.5f32) == 1.5);
    // integer promotions: char/short/_Bool -> int (GPR slot, int reader)
    ASSERT(3, read_first_int(0, (char)3));
    ASSERT(4, read_first_int(0, (short)4));
    ASSERT(1, read_first_int(0, (_Bool)1));
    // _BitInt(3) is NOT promoted: it occupies the slot as-is
    ASSERT(5, read_first_int(0, (_BitInt(3))5));

    // === Correctly rounded division (exact rational expectations) ===
    ASSERT(1, 1234567.0f32 / 891.0f32 == 0x1.5a6636p+10f32);
    ASSERT(1, 76543.0f32 / 210.0f32 == 0x1.6c7d9p+8f32);
    ASSERT(1, 999983.0f32 / 61.0f32 == 0x1.0024a8p+14f32);
    ASSERT(1, 1.0f32 / 3.0f32 == 0x1.555556p-2f32);
    ASSERT(1, 1.0f32 / 10.0f32 == 0x1.99999ap-4f32);

    printf("OK\n");
    return 0;
}
