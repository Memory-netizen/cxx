#include <float.h>

#include "test.h"

int main() {
    ASSERT(16, sizeof(_Float128));
    ASSERT(16, _Alignof(_Float128));

    // Exact hex literal
    ASSERT(1, 0x1.921fb54442d18469898cc51701b8p+1f128 == 3.141592653589793238462643383279502884f128);

    // Decimal literals round correctly (single rounding to binary128)
    ASSERT(1, 0.1f128 == 0x1.999999999999999999999999999ap-4f128);
    ASSERT(1, 1.5f128 == 0x1.8p+0f128);

    // Arithmetic, folded in binary128
    ASSERT(1, 1.0f128 / 3.0f128 == 0x1.5555555555555555555555555555p-2f128);
    ASSERT(1, 1.0f128 + 2.0f128 == 3.0f128);
    ASSERT(1, 2.0f128 * 3.0f128 == 6.0f128);
    ASSERT(1, 1.0f128 < 2.0f128 && 2.0f128 > 1.0f128);

    // Constants from float.h
    ASSERT(1, 0x1p-16494f128 == FLT128_TRUE_MIN);
    ASSERT(1, 0x1p-16382f128 == FLT128_MIN);
    ASSERT(1, 0x1.ffffffffffffffffffffffffffffp+16383f128 == FLT128_MAX);

    // Casts
    ASSERT(3, (int)3.9f128);
    ASSERT(1, (_Float128)7 == 7.0f128);
    ASSERT(1, (double)0.5f128 == 0.5);
    ASSERT(1, (_Float32)0.5f128 == 0.5f32);
    ASSERT(1, (_Float64)0.5f128 == 0.5f64);
    ASSERT(1, (_Float16)0.5f128 == 0.5f16);

    // Narrowing and widening round-trips
    ASSERT(1, (_Float128)(_Float64)0.1f128 == 0.1f64);
    ASSERT(1, (_Float128)(_Float32)0.1f128 == 0.1f32);

    // _Generic and same-format ties with long double
    ASSERT(1, _Generic(0.5f128, _Float128: 1, default: 0));
    ASSERT(1, _Generic(0.5f128 + 0.25L, _Float128: 1, default: 0));

    // Array indexing and pointer arithmetic
    _Float128 arr[3] = {1.0f128, 2.0f128, 3.0f128};
    ASSERT(1, arr[0] + arr[1] == arr[2]);
    ASSERT(1, &arr[1] - &arr[0] == 1);

    printf("OK\n");
    return 0;
}
