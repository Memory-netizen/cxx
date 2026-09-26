#ifndef INT128_H
#define INT128_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sign bit of the top limb (signed two's-complement interpretation) */
#define LIMB_SIGN_BIT 0x80000000u

typedef struct {
  uint32_t limb[4];
} Int128;

typedef struct {
  uint32_t limb[8];
} Int256;

typedef enum {
  SIGNED,
  UNSIGNED,
} SignKind;

Int128 int128_and(Int128 a, Int128 b);
Int128 int128_or(Int128 a, Int128 b);
Int128 int128_xor(Int128 a, Int128 b);
Int128 int128_not(Int128 a);

Int128 int128_shl(Int128 a, int amount);
Int128 int128_ashr(Int128 a, int amount);
Int128 int128_lshr(Int128 a, int amount);
Int128 int128_shr(Int128 a, int amount, SignKind sign);

Int128 int128_add(Int128 a, Int128 b);
Int128 int128_sub(Int128 a, Int128 b);
Int128 int128_neg(Int128 a);
Int128 int128_abs(Int128 a);

int int128_cmp_unsigned(Int128 a, Int128 b);
int int128_cmp_signed(Int128 a, Int128 b);
int int128_cmp(Int128 a, Int128 b, SignKind sign);

Int256 int128_mul_full(Int128 a, Int128 b);
Int128 int128_mul(Int128 a, Int128 b);
/* Division by zero convention: returns q=0, r=a (division by zero is UB
 * in C; a deterministic result is returned instead). */
Int128 int128_div_unsigned(Int128 a, Int128 b);
Int128 int128_mod_unsigned(Int128 a, Int128 b);
/* Signed division truncates toward zero; the modulus takes the sign of the
 * dividend. Two's-complement wrap semantics: INT128_MIN / -1 and
 * -INT128_MIN both wrap to INT128_MIN. */
Int128 int128_div_signed(Int128 a, Int128 b);
Int128 int128_mod_signed(Int128 a, Int128 b);
Int128 int128_div(Int128 a, Int128 b, SignKind sign);
Int128 int128_mod(Int128 a, Int128 b, SignKind sign);

Int128 int128_normalize(Int128 v, int width, SignKind sign);
/* bit_width(0) returns 1 (no msb); callers must check for zero first. */
int int128_bit_width(Int128 v, SignKind sign);
bool int128_fits(Int128 v, int width, SignKind sign);

bool int128_is_zero(Int128 v);
bool int128_is_negative(Int128 v);

Int128 int128_set_i(int64_t i);
Int128 int128_set_ui(uint64_t i);

/* Parse an unsigned digit string in base 2..16, with an optional +/- sign
 * (the magnitude is parsed first and then negated, so
 * "-170141183460469231731687303715884105728" yields INT128_MIN).
 * A 0x/0X prefix is allowed for base 16. Returns false on overflow. */
bool int128_set_str(Int128 *v, const char *str, int base);
/* Returns the length written (excluding the NUL), or -1 if the buffer is
 * too small. */
int int128_to_str(Int128 v, SignKind sign, int base, char *buf, size_t bufsize);

extern const Int128 int128_min;
extern const Int128 int128_max;
extern const Int128 uint128_min;
extern const Int128 uint128_max;
extern const Int128 int128_zero;
extern const Int128 int128_one;

#define INT128_MIN int128_min
#define INT128_MAX int128_max
#define UINT128_MAX uint128_max
#define INT128_ZERO int128_zero
#define INT128_ONE int128_one

#ifdef __cplusplus
}
#endif

#endif /* INT128_H */
