#include <float.h>

#include "test.h"

int main() {
    ASSERT(16, sizeof(long double));
    ASSERT(16, _Alignof(long double));

    // Literals and basic arithmetic
    ASSERT(1, 1.5L + 2.25L == 3.75L);
    ASSERT(1, 2.0L * 3.0L == 6.0L);
    ASSERT(1, 1.0L / 2.0L == 0.5L);
    ASSERT(1, 1.0L < 2.0L && 2.0L > 1.0L);
    ASSERT(1, -0.0L == 0.0L);

    // Casts
    ASSERT(3, (int)3.9L);
    ASSERT(-3, (int)-3.9L);
    ASSERT(1, (long double)3 == 3.0L);
    ASSERT(1, (long double)7 == 7);
    ASSERT(1, (double)0.5L == 0.5);

    // long double carries more precision than double
    ASSERT(1, 1.0L + 0x1p-60L != 1.0L);  // vanishes in double
    ASSERT(1, (double)(1.0L + 0x1p-60L) == 1.0);

    // 1/3 keeps the long double precision
    ASSERT(1, 1.0L / 3.0L > 0.333333333333333333L);
    ASSERT(1, 1.0L / 3.0L < 0.333333333333333334L);

    // Wide range
    ASSERT(1, 1e4000L > 0);
    ASSERT(1, 1e4000L < LDBL_MAX);
    ASSERT(1, 1e-4000L > 0);

    // float.h values come from the per-target predefined macros
    ASSERT(1, LDBL_MANT_DIG == __LDBL_MANT_DIG__);
    ASSERT(1, LDBL_TRUE_MIN == __LDBL_DENORM_MIN__);
    ASSERT(1, LDBL_MAX_EXP == __LDBL_MAX_EXP__);

#if __LDBL_MANT_DIG__ == 64
    // x87 80-bit (amd64)
    ASSERT(1, 1.0L + 0x1p-63L > 1.0L);   // one ULP of 1.0
    ASSERT(1, 1.0L + 0x1p-64L == 1.0L);  // half-ULP tie rounds to even
    ASSERT(1, LDBL_EPSILON == 0x1p-63L);
    ASSERT(1, LDBL_TRUE_MIN == 0x1p-16445L);
    ASSERT(1, LDBL_MAX_EXP == 16384);
#elif __LDBL_MANT_DIG__ == 113
    // binary128 (arm64, riscv64)
    ASSERT(1, 1.0L + 0x1p-112L > 1.0L);
    ASSERT(1, 1.0L + 0x1p-113L == 1.0L);
    ASSERT(1, LDBL_EPSILON == 0x1p-112L);
    ASSERT(1, LDBL_TRUE_MIN == 0x1p-16494L);
    ASSERT(1, LDBL_MAX_EXP == 16384);
#endif

    // Casts between long double and the interchange types
    ASSERT(1, (_Float128)0.5L == 0.5f128);
    ASSERT(1, (long double)0.5f128 == 0.5L);
    ASSERT(1, (_Float64)0.5L == 0.5f64);
    ASSERT(1, (_Float32)0.5L == 0.5f32);

    // Usual arithmetic conversions (C23 H.4.3)
    ASSERT(1, _Generic(0.5L + 0.25, long double: 1, default: 0));
    // binary128 is a superset of fp80, and wins the equivalent-set tie
    // against binary128 long double: both yield _Float128.
    ASSERT(1, _Generic(0.5L + 0.25f128, _Float128: 1, default: 0));

    // Arrays and pointer arithmetic
    long double arr[3] = {1.0L, 2.0L, 3.0L};
    ASSERT(1, arr[0] + arr[1] == arr[2]);
    ASSERT(1, &arr[1] - &arr[0] == 1);

    printf("OK\n");
    return 0;
}
