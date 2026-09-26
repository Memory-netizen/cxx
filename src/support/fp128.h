#ifndef FP128_H
#define FP128_H

#include "int128.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t limb[4];
} Fp128;

typedef enum {
  FP16,  // binary16
  FP32,  // binary32
  FP64,  // binary64
  FP80,  // x86 80bits
  FP128, // binary128
} FpFormat;

bool fp128_get_sign(Fp128 v);
uint16_t fp128_get_exp(Fp128 v);
Int128 fp128_get_m(Fp128 v);
bool fp128_is_nan(Fp128 v);
bool fp128_is_inf(Fp128 v);
bool fp128_is_zero(Fp128 v);
bool fp128_is_negative(Fp128 v);
bool fp128_is_subnormal(Fp128 v);
bool fp128_is_finite(Fp128 v);
bool fp128_is_normal(Fp128 v);
/* Compare two fp128 values.
 * Returns: -1 (a<b), 0 (a==b), 1 (a>b), 2 (unordered, at least one NaN) */
int fp128_cmp(Fp128 a, Fp128 b);

Fp128 fp128_neg(Fp128 a);
Fp128 fp128_abs(Fp128 a);
Fp128 fp128_add(Fp128 a, Fp128 b);
Fp128 fp128_sub(Fp128 a, Fp128 b);
Fp128 fp128_mul(Fp128 a, Fp128 b);
Fp128 fp128_div(Fp128 a, Fp128 b);

Fp128 fp128_from_fp16(uint16_t bits);
Fp128 fp128_from_fp32(uint32_t bits);
Fp128 fp128_from_fp64(uint64_t bits);
Fp128 fp128_from_fp80(uint64_t mantissa, uint16_t sign_exp);

Fp128 fp128_round_to(Fp128 v, FpFormat target);
uint16_t fp128_to_fp16_bits(Fp128 v);
uint32_t fp128_to_fp32_bits(Fp128 v);
uint64_t fp128_to_fp64_bits(Fp128 v);
void fp128_to_fp80_bits(Fp128 v, uint64_t *mantissa, uint16_t *sign_exp);
Fp128 fp128_from_int128(Int128 v, SignKind sign);
Int128 fp128_to_int128(Fp128 v, SignKind sign, bool *ok);

bool fp128_set_str(Fp128 *v, const char *str, FpFormat target);
bool fp128_set_hex_str(Fp128 *v, const char *str, FpFormat target);

/* Round a significand m (value = m x 2^(E - 16383 - 115), msb <= 115,
 * bits 2,1,0 = G/R/S data) to 113 bits and pack it into an Fp128.
 * `sticky` carries any nonzero bits below bit 0. */
Fp128 fp128_round_and_pack(Int128 m, int E, int sign, bool sticky);
/* Format as a C99 %a-style hex float string (e.g. "-0x1.921fb54442d18p+1").
 * The value is rounded once to `target` before printing. Returns the length
 * excluding the NUL, or -1 if the buffer is too small. */
int fp128_to_hexstr(Fp128 *v, char *buf, size_t bufsize, FpFormat target);

extern const Fp128 FP128_ZERO;
extern const Fp128 FP128_ONE;
extern const Fp128 FP128_INF;
extern const Fp128 FP128_NAN;

#ifdef __cplusplus
}
#endif

#endif
