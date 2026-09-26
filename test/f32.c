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

    printf("OK\n");
    return 0;
}
