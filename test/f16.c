#include <float.h>

#include "test.h"

int main() {
    // Literals and single rounding to binary16
    ASSERT(1, 1.5f16 == 1.5f16);
    ASSERT(1, 1.0f16 == 1);
    ASSERT(2, sizeof(_Float16));
    ASSERT(2, _Alignof(_Float16));

    // 0.1 rounds to 0x3555 = 0.3330078125... wait, 0.1 = 0x2E66
    // (1 + 341/1024) * 2^-4 = 0.0999755859375
    ASSERT(1, 0.1f16 == 0x1.998p-4f16);

    // Arithmetic (folded in fp128, rounded once to f16)
    ASSERT(1, 1.0f16 + 0.5f16 == 1.5f16);
    ASSERT(1, 1.5f16 * 2.0f16 == 3.0f16);
    ASSERT(1, 3.0f16 / 2.0f16 == 1.5f16);
    ASSERT(1, 1.0f16 < 2.0f16);
    ASSERT(1, 2.0f16 >= 2.0f16);
    ASSERT(1, -1.5f16 < 0);

    // Casts
    ASSERT(3, (int)3.5f16);
    ASSERT(1, (_Float16)1 == 1.0f16);
    ASSERT(1, (_Float16)3.0f == 3.0f16);
    ASSERT(1, (double)0.5f16 == 0.5);
    ASSERT(1, (_Float16)(float)0.5f == 0.5f16);

    // Min/max constants from float.h
    ASSERT(1, 0x1p-24f16 == FLT16_TRUE_MIN);
    ASSERT(1, 0x1.ffcp+15f16 == FLT16_MAX);

    // Conversions between formats
    ASSERT(1, (_Float32)0.5f16 == 0.5f32);
    ASSERT(1, (_Float16)0.5f32 == 0.5f16);

    // sizeof in arrays / pointers
    _Float16 arr[4];
    ASSERT(8, sizeof(arr));
    ASSERT(1, &arr[1] - &arr[0] == 1);

    // === f16 boundaries: subnormals, overflow, cast matrix ===
    ASSERT(1, 65504.0f16 + 65504.0f16 > 65504.0f16);    // overflows to inf
    ASSERT(1, -65504.0f16 - 65504.0f16 < -65504.0f16);  // -inf
    ASSERT(0, 1.0f16 / 0.0f16 == 0.0f16);               // inf != 0
    ASSERT(1, 5.960464477539063e-8f16 > 0);             // min subnormal
    ASSERT(1, 5.960464477539063e-8f16 / 2 == 0.0f16);   // underflows
    ASSERT(1, (float)65504.0f16 == 65504.0f);
    ASSERT(1, (double)0.5f16 == 0.5);
    ASSERT(1, (int)1.5f16 == 1);
    ASSERT(1, (unsigned)0.5f16 == 0);
    _Float16 hx = 3.0f16;
    hx = hx * 0.5f16;
    ASSERT(1, hx == 1.5f16);
    // f16 wrapping in expressions: promoted to float? f16+f16 stays f16?
    ASSERT(1, sizeof(0.5f16 + 0.5f16) == 2);
    ASSERT(1, 0.5f16 + 0.5f16 == 1.0f16);

    printf("OK\n");
    return 0;
}
