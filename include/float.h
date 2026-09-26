#ifndef __STDFLOAT_H
#define __STDFLOAT_H

#define DECIMAL_DIG 21
#define FLT_EVAL_METHOD 0  // C11 5.2.4.2.2p9
#define FLT_RADIX 2
#define FLT_ROUNDS 1  // C11 5.2.4.2.2p8: to nearest

#define FLT_DIG 6
#define FLT_EPSILON 0x1p-23
#define FLT_MANT_DIG 24
#define FLT_MAX 0x1.fffffep+127
#define FLT_MAX_10_EXP 38
#define FLT_MAX_EXP 128
#define FLT_MIN 0x1p-126
#define FLT_MIN_10_EXP -37
#define FLT_MIN_EXP -125
#define FLT_TRUE_MIN 0x1p-149

#define DBL_DIG 15
#define DBL_EPSILON 0x1p-52
#define DBL_MANT_DIG 53
#define DBL_MAX 0x1.fffffffffffffp+1023
#define DBL_MAX_10_EXP 308
#define DBL_MAX_EXP 1024
#define DBL_MIN 0x1p-1022
#define DBL_MIN_10_EXP -307
#define DBL_MIN_EXP -1021
#define DBL_TRUE_MIN 0x0.0000000000001p-1022

#define LDBL_DIG 15
#define LDBL_EPSILON 0x1p-52
#define LDBL_MANT_DIG 53
#define LDBL_MAX 0x1.fffffffffffffp+1023
#define LDBL_MAX_10_EXP 308
#define LDBL_MAX_EXP 1024
#define LDBL_MIN 0x1p-1022
#define LDBL_MIN_10_EXP -307
#define LDBL_MIN_EXP -1021
#define LDBL_TRUE_MIN 0x0.0000000000001p-1022

// IEC 60559 interchange types (C23)

// binary16
#define FLT16_DIG 3
#define FLT16_EPSILON 0x1p-10f16
#define FLT16_MANT_DIG 11
#define FLT16_MAX 0x1.ffcp+15f16
#define FLT16_MAX_10_EXP 4
#define FLT16_MAX_EXP 16
#define FLT16_MIN 0x1p-14f16
#define FLT16_MIN_10_EXP -4
#define FLT16_MIN_EXP -13
#define FLT16_TRUE_MIN 0x1p-24f16

// binary32 (= float)
#define FLT32_DIG 6
#define FLT32_EPSILON 0x1p-23f32
#define FLT32_MANT_DIG 24
#define FLT32_MAX 0x1.fffffep+127f32
#define FLT32_MAX_10_EXP 38
#define FLT32_MAX_EXP 128
#define FLT32_MIN 0x1p-126f32
#define FLT32_MIN_10_EXP -37
#define FLT32_MIN_EXP -125
#define FLT32_TRUE_MIN 0x1p-149f32

// binary64 (= double)
#define FLT64_DIG 15
#define FLT64_EPSILON 0x1p-52f64
#define FLT64_MANT_DIG 53
#define FLT64_MAX 0x1.fffffffffffffp+1023f64
#define FLT64_MAX_10_EXP 308
#define FLT64_MAX_EXP 1024
#define FLT64_MIN 0x1p-1022f64
#define FLT64_MIN_10_EXP -307
#define FLT64_MIN_EXP -1021
#define FLT64_TRUE_MIN 0x0.0000000000001p-1022f64

// binary128
#define FLT128_DIG 33
#define FLT128_EPSILON 0x1p-112f128
#define FLT128_MANT_DIG 113
#define FLT128_MAX 0x1.ffffffffffffffffffffffffffffp+16383f128
#define FLT128_MAX_10_EXP 4932
#define FLT128_MAX_EXP 16384
#define FLT128_MIN 0x1p-16382f128
#define FLT128_MIN_10_EXP -4931
#define FLT128_MIN_EXP -16381
#define FLT128_TRUE_MIN 0x1p-16494f128

#endif
