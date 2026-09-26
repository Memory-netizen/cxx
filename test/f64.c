#include <float.h>

#include "test.h"

int main() {
    ASSERT(8, sizeof(_Float64));
    ASSERT(8, _Alignof(_Float64));

    // Literals: single rounding to binary64 == the double constant
    ASSERT(1, 0.1f64 == 0.1);
    ASSERT(1, 0.1f64 == (double)0.1);
    ASSERT(1, 1.0f64 == 1.0);
    ASSERT(1, 0x1.921fb54442d18p+1f64 == 3.141592653589793);

    // Arithmetic
    ASSERT(1, 0.1f64 + 0.2f64 == 0.30000000000000004);  // the classic binary64 result
    ASSERT(1, 1.5f64 * 2.0f64 == 3.0f64);
    ASSERT(1, 1.0f64 / 3.0f64 == (1.0 / 3.0));
    ASSERT(1, 1.0f64 < 2.0f64 && 2.0f64 >= 2.0f64);

    // Casts
    ASSERT(3, (int)3.9f64);
    ASSERT(1, (_Float64)7 == 7.0f64);
    ASSERT(1, (float)0.5f64 == 0.5f);
    ASSERT(1, (_Float32)0.5f64 == 0.5f32);
    ASSERT(1, (_Float128)0.5f64 == 0.5f128);

    // Constants
    ASSERT(1, 0x0.0000000000001p-1022f64 == FLT64_TRUE_MIN);
    ASSERT(1, 0x1.fffffffffffffp+1023f64 == FLT64_MAX);

    // _Generic and standard-type tie
    ASSERT(1, _Generic(0.5f64, _Float64: 1, default: 0));
    ASSERT(1, _Generic(0.5f64 + 0.25, _Float64: 1, default: 0));

    printf("OK\n");
    return 0;
}
