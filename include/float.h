#ifndef __STDFLOAT_H
#define __STDFLOAT_H

// All values come from the per-target predefined macros (the single source
// of truth), so float.h stays architecture-independent.

#define DECIMAL_DIG __DECIMAL_DIG__
#define FLT_EVAL_METHOD 0  // C11 5.2.4.2.2p9
#define FLT_RADIX __FLT_RADIX__
#define FLT_ROUNDS 1  // C11 5.2.4.2.2p8: to nearest

#define FLT_DIG __FLT_DIG__
#define FLT_EPSILON __FLT_EPSILON__
#define FLT_MANT_DIG __FLT_MANT_DIG__
#define FLT_MAX __FLT_MAX__
#define FLT_MAX_10_EXP __FLT_MAX_10_EXP__
#define FLT_MAX_EXP __FLT_MAX_EXP__
#define FLT_MIN __FLT_MIN__
#define FLT_MIN_10_EXP __FLT_MIN_10_EXP__
#define FLT_MIN_EXP __FLT_MIN_EXP__
#define FLT_TRUE_MIN __FLT_DENORM_MIN__

#define DBL_DIG __DBL_DIG__
#define DBL_EPSILON __DBL_EPSILON__
#define DBL_MANT_DIG __DBL_MANT_DIG__
#define DBL_MAX __DBL_MAX__
#define DBL_MAX_10_EXP __DBL_MAX_10_EXP__
#define DBL_MAX_EXP __DBL_MAX_EXP__
#define DBL_MIN __DBL_MIN__
#define DBL_MIN_10_EXP __DBL_MIN_10_EXP__
#define DBL_MIN_EXP __DBL_MIN_EXP__
#define DBL_TRUE_MIN __DBL_DENORM_MIN__

// long double is target-dependent (x87 80-bit on amd64, binary128
// elsewhere); the values come from the per-target predefined macros.
#define LDBL_DIG __LDBL_DIG__
#define LDBL_EPSILON __LDBL_EPSILON__
#define LDBL_MANT_DIG __LDBL_MANT_DIG__
#define LDBL_MAX __LDBL_MAX__
#define LDBL_MAX_10_EXP __LDBL_MAX_10_EXP__
#define LDBL_MAX_EXP __LDBL_MAX_EXP__
#define LDBL_MIN __LDBL_MIN__
#define LDBL_MIN_10_EXP __LDBL_MIN_10_EXP__
#define LDBL_MIN_EXP __LDBL_MIN_EXP__
#define LDBL_TRUE_MIN __LDBL_DENORM_MIN__

// IEC 60559 interchange types (C23)

// binary16
#define FLT16_DIG __FLT16_DIG__
#define FLT16_EPSILON __FLT16_EPSILON__
#define FLT16_MANT_DIG __FLT16_MANT_DIG__
#define FLT16_MAX __FLT16_MAX__
#define FLT16_MAX_10_EXP __FLT16_MAX_10_EXP__
#define FLT16_MAX_EXP __FLT16_MAX_EXP__
#define FLT16_MIN __FLT16_MIN__
#define FLT16_MIN_10_EXP __FLT16_MIN_10_EXP__
#define FLT16_MIN_EXP __FLT16_MIN_EXP__
#define FLT16_TRUE_MIN __FLT16_DENORM_MIN__

// binary32
#define FLT32_DIG __FLT32_DIG__
#define FLT32_EPSILON __FLT32_EPSILON__
#define FLT32_MANT_DIG __FLT32_MANT_DIG__
#define FLT32_MAX __FLT32_MAX__
#define FLT32_MAX_10_EXP __FLT32_MAX_10_EXP__
#define FLT32_MAX_EXP __FLT32_MAX_EXP__
#define FLT32_MIN __FLT32_MIN__
#define FLT32_MIN_10_EXP __FLT32_MIN_10_EXP__
#define FLT32_MIN_EXP __FLT32_MIN_EXP__
#define FLT32_TRUE_MIN __FLT32_DENORM_MIN__

// binary64
#define FLT64_DIG __FLT64_DIG__
#define FLT64_EPSILON __FLT64_EPSILON__
#define FLT64_MANT_DIG __FLT64_MANT_DIG__
#define FLT64_MAX __FLT64_MAX__
#define FLT64_MAX_10_EXP __FLT64_MAX_10_EXP__
#define FLT64_MAX_EXP __FLT64_MAX_EXP__
#define FLT64_MIN __FLT64_MIN__
#define FLT64_MIN_10_EXP __FLT64_MIN_10_EXP__
#define FLT64_MIN_EXP __FLT64_MIN_EXP__
#define FLT64_TRUE_MIN __FLT64_DENORM_MIN__

// binary128
#define FLT128_DIG __FLT128_DIG__
#define FLT128_EPSILON __FLT128_EPSILON__
#define FLT128_MANT_DIG __FLT128_MANT_DIG__
#define FLT128_MAX __FLT128_MAX__
#define FLT128_MAX_10_EXP __FLT128_MAX_10_EXP__
#define FLT128_MAX_EXP __FLT128_MAX_EXP__
#define FLT128_MIN __FLT128_MIN__
#define FLT128_MIN_10_EXP __FLT128_MIN_10_EXP__
#define FLT128_MIN_EXP __FLT128_MIN_EXP__
#define FLT128_TRUE_MIN __FLT128_DENORM_MIN__

#endif
