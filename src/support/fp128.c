#include "fp128.h"

#include "int128.h"

bool fp128_get_sign(Fp128 v) { return (v.limb[3] & LIMB_SIGN_BIT) != 0; }

uint16_t fp128_get_exp(Fp128 v) { return (v.limb[3] & ~LIMB_SIGN_BIT) >> 16; }

Int128 fp128_get_m(Fp128 v) {
    Int128 r = {{v.limb[0], v.limb[1], v.limb[2], v.limb[3] & 0xFFFFu}};
    return r;
}

bool fp128_is_nan(Fp128 v) {
    uint16_t e = fp128_get_exp(v);
    Int128 m = fp128_get_m(v);
    return e == 0x7FFF && !int128_is_zero(m);
}

bool fp128_is_inf(Fp128 v) {
    uint16_t e = fp128_get_exp(v);
    Int128 m = fp128_get_m(v);
    return e == 0x7FFF && int128_is_zero(m);
}

bool fp128_is_zero(Fp128 v) {
    uint16_t e = fp128_get_exp(v);
    Int128 m = fp128_get_m(v);
    return e == 0 && int128_is_zero(m);
}

bool fp128_is_negative(Fp128 v) { return fp128_get_sign(v) && !fp128_is_zero(v); }

bool fp128_is_subnormal(Fp128 v) { return fp128_get_exp(v) == 0 && !int128_is_zero(fp128_get_m(v)); }

bool fp128_is_finite(Fp128 v) { return fp128_get_exp(v) != 0x7FFF; }

bool fp128_is_normal(Fp128 v) {
    uint16_t e = fp128_get_exp(v);
    return e != 0 && e != 0x7FFF;
}

int fp128_cmp(Fp128 a, Fp128 b) {
    /* NaN: unordered */
    if (fp128_is_nan(a) || fp128_is_nan(b)) return 2;

    /* +-0 compare equal */
    if (fp128_is_zero(a) && fp128_is_zero(b)) return 0;

    int sa = fp128_get_sign(a) ? 1 : 0;
    int sb = fp128_get_sign(b) ? 1 : 0;
    if (sa != sb) return sa ? -1 : 1;

    int ea = fp128_get_exp(a);
    int eb = fp128_get_exp(b);
    if (ea != eb) {
        int r = (ea > eb) ? 1 : -1;
        return sa ? -r : r;
    }

    Int128 ma = fp128_get_m(a);
    Int128 mb = fp128_get_m(b);
    int r = int128_cmp_unsigned(ma, mb);
    return sa ? -r : r;
}

/* Round a significand m to 113 bits and pack it into an Fp128.
 * The value is m x 2^(E - 16383 - 115) plus, when sticky is set, an
 * amount strictly below one unit of bit 0. The msb of m may be anywhere
 * in [0, 115]; bits 2,1,0 are the G/R/S data bits and sticky carries any
 * nonzero bits below bit 0 separately, so a left-normalization never
 * shifts a sticky flag into the significand. */
Fp128 fp128_round_and_pack(Int128 m, int E, int sign, bool sticky) {
    int bw = int128_bit_width(m, UNSIGNED);

    Int128 t;
    int e_out;
    int Eres = E + bw - 116; /* exponent of the 113-bit normalized form */

    if (Eres >= 1) {
        if (bw > 113) {
            /* Round at the msb-relative position: keep the top 113 bits */
            int rshift = bw - 113;
            bool G = (m.limb[(rshift - 1) / 32] >> ((rshift - 1) % 32)) & 1;
            bool R = false;
            if (rshift >= 2) R = (m.limb[(rshift - 2) / 32] >> ((rshift - 2) % 32)) & 1;
            bool S = sticky;
            if (rshift >= 3) S = S || !int128_is_zero(int128_normalize(m, rshift - 2, UNSIGNED));
            t = int128_shr(m, rshift, UNSIGNED);
            bool L = t.limb[0] & 1;
            if (G && (R || S || L)) {
                t = int128_add(t, (Int128){{1, 0, 0, 0}});
                if (int128_bit_width(t, UNSIGNED) > 113) {
                    t = int128_shr(t, 1, UNSIGNED);
                    Eres++;
                }
            }
        } else {
            /* Exact: G = R = 0, no rounding; sticky stays below the result */
            t = int128_shl(m, 113 - bw);
        }
        e_out = Eres;
    } else {
        /* fp128 subnormal: the fraction is m x 2^(E - 4) */
        int rshift = 4 - E;
        if (rshift >= 128) {
            t = (Int128){{0, 0, 0, 0}};
        } else if (rshift > 0) {
            bool G = (m.limb[(rshift - 1) / 32] >> ((rshift - 1) % 32)) & 1;
            bool R = false;
            if (rshift >= 2) R = (m.limb[(rshift - 2) / 32] >> ((rshift - 2) % 32)) & 1;
            bool S = sticky;
            if (rshift >= 3) S = S || !int128_is_zero(int128_normalize(m, rshift - 2, UNSIGNED));
            t = int128_shr(m, rshift, UNSIGNED);
            bool L = t.limb[0] & 1;
            if (G && (R || S || L)) t = int128_add(t, (Int128){{1, 0, 0, 0}});
        } else {
            /* E >= 4: the fraction is large enough to need a left shift */
            t = int128_shl(m, -rshift);
        }
        e_out = 0;
        /* Carrying to bit 112 turns the max subnormal into the min normal */
        if (t.limb[3] & 0x10000u) {
            e_out = 1;
            t.limb[3] &= 0xFFFFu;
        }
    }

    /* Overflow */
    if (e_out >= 0x7FFF) {
        Fp128 inf;
        inf.limb[0] = inf.limb[1] = inf.limb[2] = 0;
        inf.limb[3] = (0x7FFFu << 16) | (sign ? LIMB_SIGN_BIT : 0);
        return inf;
    }

    /* Assemble */
    Fp128 result;
    result.limb[0] = t.limb[0];
    result.limb[1] = t.limb[1];
    result.limb[2] = t.limb[2];
    result.limb[3] = (t.limb[3] & 0xFFFFu) | ((uint32_t)e_out << 16) | (sign ? LIMB_SIGN_BIT : 0);
    return result;
}

Fp128 fp128_neg(Fp128 a) {
    a.limb[3] = a.limb[3] ^ LIMB_SIGN_BIT;
    return a;
}

Fp128 fp128_abs(Fp128 a) {
    a.limb[3] = a.limb[3] & ~LIMB_SIGN_BIT;
    return a;
}

Fp128 fp128_add(Fp128 a, Fp128 b) {
    /* 1. Special values */
    if (fp128_is_nan(a) || fp128_is_nan(b)) return FP128_NAN;
    bool a_inf = fp128_is_inf(a), b_inf = fp128_is_inf(b);
    if (a_inf && b_inf) {
        if (fp128_get_sign(a) != fp128_get_sign(b)) return FP128_NAN;
        return a;
    }
    if (a_inf) return a;
    if (b_inf) return b;
    bool a_zero = fp128_is_zero(a), b_zero = fp128_is_zero(b);
    if (a_zero && b_zero) {
        if (fp128_get_sign(a) == fp128_get_sign(b)) return a;
        return (Fp128){{0, 0, 0, 0}};
    }
    if (a_zero) return b;
    if (b_zero) return a;

    /* 2. Decompose and add the implicit bit (at bit 112) */
    int sa = fp128_get_sign(a) ? 1 : 0;
    int sb = fp128_get_sign(b) ? 1 : 0;
    int ea = fp128_get_exp(a);
    int eb = fp128_get_exp(b);
    Int128 ma = fp128_get_m(a);
    Int128 mb = fp128_get_m(b);
    if (ea != 0)
        ma.limb[3] |= 0x00010000u;
    else
        ea = 1;
    if (eb != 0)
        mb.limb[3] |= 0x00010000u;
    else
        eb = 1;

    /* 3. Shift left 3: reserve G/R/S room, implicit bit to bit 115 */
    ma = int128_shl(ma, 3);
    mb = int128_shl(mb, 3);

    /* 4. Align; the shifted-out bits of the smaller operand are merged into
     * its bit 0 so the adder/subtractor treats them as data. This matters
     * for subtraction, where the borrow must propagate into the GRS bits. */
    int E = (ea > eb) ? ea : eb;
    int da = E - ea, db = E - eb;

    if (da > 0) {
        bool S = !int128_is_zero(int128_normalize(ma, da, UNSIGNED));
        ma = int128_lshr(ma, da);
        ma.limb[0] |= S;
    }
    if (db > 0) {
        bool S = !int128_is_zero(int128_normalize(mb, db, UNSIGNED));
        mb = int128_lshr(mb, db);
        mb.limb[0] |= S;
    }

    /* 5. Add or subtract (116-bit adder) */
    bool is_add = (sa == sb);
    int s;
    Int128 m;
    if (is_add) {
        m = int128_add(ma, mb);
        s = sa;
    } else {
        if (int128_cmp_unsigned(ma, mb) >= 0) {
            m = int128_sub(ma, mb);
            s = sa;
        } else {
            m = int128_sub(mb, ma);
            s = sb;
        }
    }
    if (int128_is_zero(m)) return (Fp128){{0, 0, 0, 0}};

    /* Compress to 116 bits; the dropped bit becomes sticky */
    bool sticky = false;
    if (m.limb[3] & 0x100000) {
        sticky = (m.limb[0] & 1) != 0;
        m = int128_lshr(m, 1);
        E++;
    }

    /* 6. Normalize + round + assemble */
    return fp128_round_and_pack(m, E, s, sticky);
}

Fp128 fp128_sub(Fp128 a, Fp128 b) { return fp128_add(a, fp128_neg(b)); }

Fp128 fp128_mul(Fp128 a, Fp128 b) {
    /* 1. Special values */
    if (fp128_is_nan(a) || fp128_is_nan(b)) return FP128_NAN;

    bool a_inf = fp128_is_inf(a), b_inf = fp128_is_inf(b);
    bool a_zero = fp128_is_zero(a), b_zero = fp128_is_zero(b);
    int s = (fp128_get_sign(a) ? 1 : 0) ^ (fp128_get_sign(b) ? 1 : 0);

    if ((a_inf && b_zero) || (b_inf && a_zero)) return FP128_NAN;
    if (a_inf || b_inf) {
        Fp128 inf;
        inf.limb[0] = inf.limb[1] = inf.limb[2] = 0;
        inf.limb[3] = (0x7FFFu << 16) | (s ? LIMB_SIGN_BIT : 0);
        return inf;
    }
    if (a_zero || b_zero) {
        Fp128 z = {{0, 0, 0, 0}};
        z.limb[3] = s ? LIMB_SIGN_BIT : 0;
        return z;
    }

    /* 2. Decompose and add implicit bits */
    int ea = fp128_get_exp(a), eb = fp128_get_exp(b);
    Int128 ma = fp128_get_m(a), mb = fp128_get_m(b);
    if (ea != 0)
        ma.limb[3] |= 0x00010000u;
    else
        ea = 1;
    if (eb != 0)
        mb.limb[3] |= 0x00010000u;
    else
        eb = 1;

    /* 3. 256-bit product */
    Int256 P = int128_mul_full(ma, mb);
    Int128 hi = {{P.limb[4], P.limb[5], P.limb[6], P.limb[7]}};
    Int128 lo = {{P.limb[0], P.limb[1], P.limb[2], P.limb[3]}};

    /* 4. Find the msb */
    int msb;
    if (!int128_is_zero(hi))
        msb = 128 + int128_bit_width(hi, UNSIGNED) - 1;
    else
        msb = int128_bit_width(lo, UNSIGNED) - 1;

    /* 5. Unified shift amount: normal compression + underflow compensation.
     * When E <= 0 the value is subnormal; absorb one exponent unit into the
     * shift so the convention value = r x 2^(E - 16383 - 115) holds with
     * E = 1. */
    int E = ea + eb - 16383 + (msb - 224);
    int extra = (E <= 0) ? (1 - E) : 0;
    int total_shift = msb - 115 + extra;
    if (E <= 0) E = 1;

    /* 6. Shift once and capture sticky */
    Int128 r = (Int128){{0, 0, 0, 0}};
    int sticky = 0;

    if (total_shift > msb) {
        /* Shifted out entirely: result is zero but sticky */
        sticky = 1;
    } else if (total_shift >= 128) {
        /* Mostly shifted out of hi */
        int hs = total_shift - 128;
        sticky = !int128_is_zero(lo);
        if (hs < 128) {
            if (hs > 0) {
                sticky |= !int128_is_zero(int128_normalize(hi, hs, UNSIGNED));
            }
            r = int128_shr(hi, hs, UNSIGNED);
        } else {
            sticky |= !int128_is_zero(hi);
        }
    } else if (total_shift > 0) {
        /* total_shift ∈ [1, 127] */
        sticky |= !int128_is_zero(int128_normalize(lo, total_shift, UNSIGNED));
        r = int128_or(int128_shr(lo, total_shift, UNSIGNED), int128_shl(hi, 128 - total_shift));
    } else if (total_shift == 0) {
        r = lo;
    } else {
        /* Left shift */
        int ls = -total_shift;
        if (ls < 128) {
            r = int128_or(int128_shl(lo, ls), int128_shr(hi, 128 - ls, UNSIGNED));
        } else {
            r = int128_shl(hi, ls - 128);
        }
    }

    return fp128_round_and_pack(r, E, s, sticky != 0);
}

Fp128 fp128_div(Fp128 a, Fp128 b) {
    /* 1. Special values */
    if (fp128_is_nan(a) || fp128_is_nan(b)) return FP128_NAN;

    bool a_inf = fp128_is_inf(a), b_inf = fp128_is_inf(b);
    bool a_zero = fp128_is_zero(a), b_zero = fp128_is_zero(b);
    int s = (fp128_get_sign(a) ? 1 : 0) ^ (fp128_get_sign(b) ? 1 : 0);

    if (a_inf && b_inf) /* Inf / Inf = NaN */
        return FP128_NAN;
    if (a_zero && b_zero) /* 0 / 0 = NaN */
        return FP128_NAN;
    if (a_inf || b_zero) { /* Inf / finite, or finite / 0 = Inf */
        Fp128 inf;
        inf.limb[0] = inf.limb[1] = inf.limb[2] = 0;
        inf.limb[3] = (0x7FFFu << 16) | (s ? LIMB_SIGN_BIT : 0);
        return inf;
    }
    if (b_inf || a_zero) { /* finite / Inf, or 0 / finite = 0 */
        Fp128 z = {{0, 0, 0, 0}};
        z.limb[3] = s ? LIMB_SIGN_BIT : 0;
        return z;
    }

    /* 2. Decompose and add implicit bits */
    int ea = fp128_get_exp(a), eb = fp128_get_exp(b);
    Int128 ma = fp128_get_m(a), mb = fp128_get_m(b);
    if (ea != 0)
        ma.limb[3] |= 0x00010000u;
    else
        ea = 1;
    if (eb != 0)
        mb.limb[3] |= 0x00010000u;
    else
        eb = 1;

    /* 3. Scale the smaller operand left so that both have the same msb,
     * giving ma/mb in [0.5, 2); compensate in the exponent. Without this,
     * subnormal operands would make the quotient overflow the 116-bit
     * window. */
    int E = ea - eb + 16383;
    int bwa = int128_bit_width(ma, UNSIGNED);
    int bwb = int128_bit_width(mb, UNSIGNED);
    if (bwa > bwb) {
        mb = int128_shl(mb, bwa - bwb);
        E += bwa - bwb;
    } else if (bwb > bwa) {
        ma = int128_shl(ma, bwb - bwa);
        E -= bwb - bwa;
    }

    /* 4. Long division: Q = floor(ma x 2^115 / mb), 116 bits, implicit bit
     * at bit 115 */
    Int128 R = ma;
    Int128 Q = {{0, 0, 0, 0}};
    for (int i = 115; i >= 0; i--) {
        if (int128_cmp_unsigned(R, mb) >= 0) {
            Q.limb[i / 32] |= 1u << (i % 32);
            R = int128_sub(R, mb);
        }
        if (i > 0) R = int128_shl(R, 1);
    }

    /* Nonzero remainder -> sticky */
    bool sticky = !int128_is_zero(R);

    /* 5. value = Q x 2^(E - 16383 - 115); subnormals are handled inside
     * fp128_round_and_pack */

    /* 6. Round + pack */
    return fp128_round_and_pack(Q, E, s, sticky);
}

Fp128 fp128_from_fp16(uint16_t bits) {
    uint32_t s = (bits >> 15) & 1;
    uint32_t e = (bits >> 10) & 0x1F;
    uint32_t m = bits & 0x3FFu;

    Fp128 r = {{0, 0, 0, 0}};

    if (e == 0x1F) {
        r.limb[3] = (0x7FFFu << 16) | (s ? LIMB_SIGN_BIT : 0);
        if (m) r.limb[3] |= 0x8000u;
        return r;
    }
    if (e == 0 && m == 0) {
        r.limb[3] = s ? LIMB_SIGN_BIT : 0;
        return r;
    }

    uint32_t E;
    uint32_t m_norm;
    if (e == 0) {
        /* Subnormal: value = m x 2^-24; after normalization the msb sits at
         * bit 9 of the field */
        int b = 9;
        while (b > 0 && !(m & (1u << b))) b--;
        m_norm = (m << (10 - b)) & 0x3FFu;
        E = b + 16359;
    } else {
        m_norm = m;
        E = e + 16368;
    }

    /* Fraction (10 bits) placed at value bits 111..102 */
    r.limb[3] = (m_norm << 6) | (E << 16) | (s ? LIMB_SIGN_BIT : 0);
    return r;
}

Fp128 fp128_from_fp32(uint32_t bits) {
    uint32_t s = (bits >> 31) & 1;
    uint32_t e = (bits >> 23) & 0xFF;
    uint32_t m = bits & 0x7FFFFFu;

    Fp128 r = {{0, 0, 0, 0}};

    if (e == 0xFF) {
        r.limb[3] = (0x7FFFu << 16) | (s ? LIMB_SIGN_BIT : 0);
        if (m) r.limb[3] |= 0x8000u;
        return r;
    }
    if (e == 0 && m == 0) {
        r.limb[3] = s ? LIMB_SIGN_BIT : 0;
        return r;
    }

    uint32_t E;
    uint32_t m_norm;
    if (e == 0) {
        int b = 22;
        while (b > 0 && !(m & (1u << b))) b--;
        m_norm = (m << (23 - b)) & 0x7FFFFFu;
        E = b + 16234;
    } else {
        m_norm = m;
        E = e + 16256;
    }

    r.limb[2] = m_norm << 25;
    r.limb[3] = (m_norm >> 7) | (E << 16) | (s ? LIMB_SIGN_BIT : 0);
    return r;
}

Fp128 fp128_from_fp64(uint64_t bits) {
    uint64_t s = (bits >> 63) & 1;
    uint32_t e = (bits >> 52) & 0x7FF;
    uint64_t m = bits & 0xFFFFFFFFFFFFFULL;

    Fp128 r = {{0, 0, 0, 0}};

    if (e == 0x7FF) {
        r.limb[3] = (0x7FFFu << 16) | (s ? LIMB_SIGN_BIT : 0);
        if (m) r.limb[3] |= 0x8000u;
        return r;
    }
    if (e == 0 && m == 0) {
        r.limb[3] = s ? LIMB_SIGN_BIT : 0;
        return r;
    }

    uint32_t E;
    uint64_t m_norm;
    if (e == 0) {
        int b = 51;
        while (b > 0 && !(m & (1ULL << b))) b--;
        m_norm = (m << (52 - b)) & 0xFFFFFFFFFFFFFULL;
        E = b + 15309;
    } else {
        m_norm = m;
        E = e + 15360;
    }

    r.limb[1] = (uint32_t)(m_norm & 0xF) << 28;
    r.limb[2] = (uint32_t)(m_norm >> 4);
    r.limb[3] = (uint32_t)((m_norm >> 36) & 0xFFFFu) | (E << 16) | (s ? LIMB_SIGN_BIT : 0);
    return r;
}

Fp128 fp128_from_fp80(uint64_t mantissa, uint16_t sign_exp) {
    uint32_t s = (sign_exp >> 15) & 1;
    uint32_t e = sign_exp & 0x7FFF;
    uint64_t m = mantissa;

    Fp128 r = {{0, 0, 0, 0}};

    /* Inf / NaN: e all ones; the top bit of m is the explicit integer bit */
    if (e == 0x7FFF) {
        r.limb[3] = (0x7FFFu << 16) | (s ? LIMB_SIGN_BIT : 0);
        if (m != 0x8000000000000000ULL) /* Inf only when integer bit=1, frac=0 */
            r.limb[3] |= 0x8000u;       /* otherwise NaN */
        return r;
    }

    /* ±0 */
    if (e == 0 && m == 0) {
        r.limb[3] = s ? LIMB_SIGN_BIT : 0;
        return r;
    }

    if (e == 0) {
        /* Subnormal: value = m x 2^-16445, f = m << 49 in fp128 (bit 63 of m
         * is always 0, same placement as the normal branch; the integer bit
         * happens to be 0) */
        r.limb[1] = (uint32_t)((m & 0x7FFFu) << 17);
        r.limb[2] = (uint32_t)(m >> 15);
        r.limb[3] = (uint32_t)((m >> 47) & 0xFFFFu) | (s ? LIMB_SIGN_BIT : 0);
    } else {
        /* Normal: value = m x 2^(e - 16446), f = m x 2^49 - 2^112 in fp128,
         * i.e. significand = m << 49; bit 112 is the implicit integer bit and
         * bits 0-111 are f. The mask must be 0xFFFF: bit 16 of m>>47 is the
         * implicit bit, and letting it leak into the exponent doubles every
         * even-exponent value. */
        r.limb[1] = (uint32_t)((m & 0x7FFFu) << 17);
        r.limb[2] = (uint32_t)(m >> 15);
        r.limb[3] = (uint32_t)((m >> 47) & 0xFFFFu) | (e << 16) | (s ? LIMB_SIGN_BIT : 0);
    }
    return r;
}

/* Extract (sign, unbiased exponent, normalized sig) from an fp128.
 * The sig always has its implicit bit at bit 112.
 * Subnormal inputs are normalized by shifting left with the exponent
 * adjusted accordingly. */
static void fp128_decompose_norm(Fp128 v, int *s, int *exp_unbiased, Int128 *sig) {
    *s = fp128_get_sign(v) ? 1 : 0;
    int e = fp128_get_exp(v);
    Int128 m = fp128_get_m(v);

    if (e != 0) {
        *sig = int128_or(m, int128_shl(INT128_ONE, 112));
        *exp_unbiased = e - 16383;
    } else if (int128_is_zero(m)) {
        /* Subnormal zero: callers usually check for zero first; defensive */
        *sig = (Int128){{0, 0, 0, 0}};
        *exp_unbiased = 0;
    } else {
        /* Subnormal: normalize so the implicit bit lands at bit 112 */
        int msb = int128_bit_width(m, UNSIGNED) - 1;
        int sh = 112 - msb;
        *sig = int128_shl(m, sh);
        *exp_unbiased = -16382 - sh; /* = msb - 16494 */
    }
}

/* Shift sig (implicit bit at 112) right by (113-p + extra) bits, round to
 * p bits (implicit bit at bit p-1), and return the rounded t.
 * If the rounding carries into the implicit bit, *e is updated. */
static Int128 fp128_round_to_p(Int128 sig, int p, int extra, int *e) {
    int shift = (113 - p) + extra;
    bool G = false, R = false, S = false;

    if (shift >= 128) {
        S = !int128_is_zero(sig);
    } else if (shift > 0) {
        int gpos = shift - 1;
        G = (sig.limb[gpos / 32] >> (gpos % 32)) & 1;
        if (shift >= 2) {
            int rpos = shift - 2;
            R = (sig.limb[rpos / 32] >> (rpos % 32)) & 1;
        }
        if (shift >= 3) S = !int128_is_zero(int128_normalize(sig, shift - 2, UNSIGNED));
    }

    Int128 t = (shift == 0) ? sig : (shift >= 128) ? (Int128){{0, 0, 0, 0}} : int128_shr(sig, shift, UNSIGNED);

    bool L = t.limb[0] & 1;
    if (G && (R || S || L)) {
        t = int128_add(t, (Int128){{1, 0, 0, 0}});
        if (*e == 0) {
            /* Subnormal: carrying into the implicit bit makes it the min normal */
            if ((unsigned)int128_bit_width(t, UNSIGNED) > (unsigned)(p - 1)) *e = 1;
        } else {
            /* Normal: carrying to p+1 bits needs one more right shift */
            if ((unsigned)int128_bit_width(t, UNSIGNED) > (unsigned)p) {
                t = int128_shr(t, 1, UNSIGNED);
                (*e)++;
            }
        }
    }
    return t;
}

/* Like fp128_round_to_p, but the input is a 116-bit sig (implicit bit at
 * 115, sticky already merged into bit 0) and shift = (116 - p) + extra.
 * With p=113 and extra=0 this is bitwise equivalent to the rounding in
 * fp128_round_and_pack. */
static Int128 round116_to_p(Int128 sig, int p, int extra, int *e, bool sticky) {
    int shift = (116 - p) + extra;
    bool G = false, R = false, S = false;

    if (shift >= 128) {
        S = sticky || !int128_is_zero(sig);
    } else if (shift > 0) {
        int gpos = shift - 1;
        G = (sig.limb[gpos / 32] >> (gpos % 32)) & 1;
        if (shift >= 2) {
            int rpos = shift - 2;
            R = (sig.limb[rpos / 32] >> (rpos % 32)) & 1;
        }
        if (shift >= 3) S = sticky || !int128_is_zero(int128_normalize(sig, shift - 2, UNSIGNED));
    }

    Int128 t = (shift == 0) ? sig : (shift >= 128) ? (Int128){{0, 0, 0, 0}} : int128_shr(sig, shift, UNSIGNED);

    bool L = t.limb[0] & 1;
    if (G && (R || S || L)) {
        t = int128_add(t, (Int128){{1, 0, 0, 0}});
        if (*e == 0) {
            if ((unsigned)int128_bit_width(t, UNSIGNED) > (unsigned)(p - 1)) *e = 1;
        } else {
            if ((unsigned)int128_bit_width(t, UNSIGNED) > (unsigned)p) {
                t = int128_shr(t, 1, UNSIGNED);
                (*e)++;
            }
        }
    }
    return t;
}

/* Given a 116-bit sig and biased exponent E_fp128 (value =
 * sig x 2^(E_fp128 - 16383 - 115), implicit bit at 115), round once
 * directly to the target format and extend the result back to canonical
 * fp128 storage. Unlike "round to fp128 first, then narrow with
 * fp128_round_to", this eliminates double-rounding errors. */
static Fp128 round116_to_target(Int128 sig, int E_fp128, int sign, FpFormat target, bool sticky) {
    int p, bias, max_exp;
    switch (target) {
        case FP16:
            p = 11, bias = 15, max_exp = 30;
            break;
        case FP32:
            p = 24, bias = 127, max_exp = 254;
            break;
        case FP64:
            p = 53, bias = 1023, max_exp = 2046;
            break;
        default: /* FP80 */
            p = 64, bias = 16383, max_exp = 0x7FFE;
            break;
    }

    int e_target = E_fp128 - 16383 + bias;
    if (e_target > max_exp) {
        Fp128 inf = {{0, 0, 0, 0}};
        inf.limb[3] = (0x7FFFu << 16) | (sign ? LIMB_SIGN_BIT : 0);
        return inf;
    }

    int extra = 0;
    if (e_target < 1) {
        extra = 1 - e_target;
        e_target = 0;
    }

    Int128 t = round116_to_p(sig, p, extra, &e_target, sticky);
    if (e_target > max_exp) {
        Fp128 inf = {{0, 0, 0, 0}};
        inf.limb[3] = (0x7FFFu << 16) | (sign ? LIMB_SIGN_BIT : 0);
        return inf;
    }
    if (int128_is_zero(t)) {
        Fp128 z = {{0, 0, 0, 0}};
        z.limb[3] = sign ? LIMB_SIGN_BIT : 0;
        return z;
    }

    /* Extend back to canonical fp128 storage: implicit bit to bit 112.
     * value = t x 2^(vexp - (p-1)), where vexp is the unbiased exponent in
     * the target format (1 - bias for subnormals). */
    int msb_t = int128_bit_width(t, UNSIGNED) - 1;
    Int128 sig128 = int128_shl(t, 112 - msb_t);
    int vexp = (e_target == 0) ? (1 - bias) : (e_target - bias);
    int E128 = vexp - (p - 1) + msb_t + 16383;

    Fp128 r;
    r.limb[0] = sig128.limb[0];
    r.limb[1] = sig128.limb[1];
    r.limb[2] = sig128.limb[2];
    r.limb[3] = (sig128.limb[3] & 0xFFFFu) | ((uint32_t)E128 << 16) | (sign ? LIMB_SIGN_BIT : 0);
    return r;
}

uint16_t fp128_to_fp16_bits(Fp128 v) {
    uint16_t s_bit = (uint16_t)((v.limb[3] >> 31) << 15);

    if (fp128_is_nan(v)) return (uint16_t)(s_bit | 0x7E00u);
    if (fp128_is_inf(v)) return (uint16_t)(s_bit | 0x7C00u);
    if (fp128_is_zero(v)) return s_bit;

    int s, exp_unbiased;
    Int128 sig;
    fp128_decompose_norm(v, &s, &exp_unbiased, &sig);

    int e_target = exp_unbiased + 15;
    if (e_target > 30) return (uint16_t)((s << 15) | 0x7C00u);

    int extra = 0;
    if (e_target < 1) {
        extra = 1 - e_target;
        e_target = 0;
    }

    Int128 t = fp128_round_to_p(sig, 11, extra, &e_target);
    if (e_target > 30) return (uint16_t)((s << 15) | 0x7C00u);

    uint32_t mant = t.limb[0] & 0x3FFu;
    if (e_target == 0) return (uint16_t)((s << 15) | mant);
    return (uint16_t)((s << 15) | ((uint16_t)e_target << 10) | mant);
}

uint32_t fp128_to_fp32_bits(Fp128 v) {
    uint32_t s_bit = (v.limb[3] >> 31) << 31;

    if (fp128_is_nan(v)) return s_bit | 0x7FC00000u;
    if (fp128_is_inf(v)) return s_bit | 0x7F800000u;
    if (fp128_is_zero(v)) return s_bit;

    int s, exp_unbiased;
    Int128 sig;
    fp128_decompose_norm(v, &s, &exp_unbiased, &sig);

    int e_target = exp_unbiased + 127;
    if (e_target > 254) return (s << 31) | 0x7F800000u;

    int extra = 0;
    if (e_target < 1) {
        extra = 1 - e_target;
        e_target = 0;
    }

    Int128 t = fp128_round_to_p(sig, 24, extra, &e_target);
    if (e_target > 254) return (s << 31) | 0x7F800000u;

    uint32_t mant = t.limb[0] & 0x7FFFFFu;
    if (e_target == 0) return (s << 31) | mant;
    return (s << 31) | ((uint32_t)e_target << 23) | mant;
}

uint64_t fp128_to_fp64_bits(Fp128 v) {
    uint64_t s_bit = (uint64_t)(v.limb[3] >> 31) << 63;

    if (fp128_is_nan(v)) return s_bit | 0x7FF8000000000000ULL;
    if (fp128_is_inf(v)) return s_bit | 0x7FF0000000000000ULL;
    if (fp128_is_zero(v)) return s_bit;

    int s, exp_unbiased;
    Int128 sig;
    fp128_decompose_norm(v, &s, &exp_unbiased, &sig);

    int e_target = exp_unbiased + 1023;
    if (e_target > 2046) return ((uint64_t)s << 63) | 0x7FF0000000000000ULL;

    int extra = 0;
    if (e_target < 1) {
        extra = 1 - e_target;
        e_target = 0;
    }

    Int128 t = fp128_round_to_p(sig, 53, extra, &e_target);
    if (e_target > 2046) return ((uint64_t)s << 63) | 0x7FF0000000000000ULL;

    uint64_t mant = ((uint64_t)t.limb[1] << 32 | t.limb[0]) & 0xFFFFFFFFFFFFFULL;
    if (e_target == 0) return ((uint64_t)s << 63) | mant;
    return ((uint64_t)s << 63) | ((uint64_t)e_target << 52) | mant;
}

void fp128_to_fp80_bits(Fp128 v, uint64_t *mantissa, uint16_t *sign_exp) {
    uint16_t s_bit = (uint16_t)((v.limb[3] >> 31) << 15);

    if (fp128_is_nan(v)) {
        *mantissa = 0xC000000000000000ULL;
        *sign_exp = s_bit | 0x7FFF;
        return;
    }
    if (fp128_is_inf(v)) {
        *mantissa = 0x8000000000000000ULL;
        *sign_exp = s_bit | 0x7FFF;
        return;
    }
    if (fp128_is_zero(v)) {
        *mantissa = 0;
        *sign_exp = s_bit;
        return;
    }

    int s, exp_unbiased;
    Int128 sig;
    fp128_decompose_norm(v, &s, &exp_unbiased, &sig);

    int e_target = exp_unbiased + 16383;
    if (e_target > 0x7FFE) {
        *mantissa = 0x8000000000000000ULL;
        *sign_exp = (uint16_t)(s << 15) | 0x7FFF;
        return;
    }

    int extra = 0;
    if (e_target < 1) {
        extra = 1 - e_target;
        e_target = 0;
    }

    Int128 t = fp128_round_to_p(sig, 64, extra, &e_target);
    if (e_target > 0x7FFE) {
        *mantissa = 0x8000000000000000ULL;
        *sign_exp = (uint16_t)(s << 15) | 0x7FFF;
        return;
    }

    /* fp80 mantissa is 64 explicit bits (integer bit included), no mask */
    *mantissa = ((uint64_t)t.limb[1] << 32) | t.limb[0];
    *sign_exp = (uint16_t)(s << 15) | (uint16_t)e_target;
}

Fp128 fp128_round_to(Fp128 v, FpFormat target) {
    switch (target) {
        case FP16:
            return fp128_from_fp16(fp128_to_fp16_bits(v));
        case FP32:
            return fp128_from_fp32(fp128_to_fp32_bits(v));
        case FP64:
            return fp128_from_fp64(fp128_to_fp64_bits(v));
        case FP80: {
            uint64_t m;
            uint16_t se;
            fp128_to_fp80_bits(v, &m, &se);
            return fp128_from_fp80(m, se);
        }
        case FP128:
        default:
            return v;
    }
}

Fp128 fp128_from_int128(Int128 v, SignKind sign) {
    if (int128_is_zero(v)) return (Fp128){0};

    int s = 0;
    if (sign == SIGNED && (v.limb[3] & LIMB_SIGN_BIT)) {
        s = 1;
        v = int128_neg(v); /* absolute value; INT128_MIN wraps to itself */
    }

    int msb = int128_bit_width(v, UNSIGNED) - 1;

    if (msb <= 112) {
        /* Exact: shift v left so the implicit bit lands at bit 112 */
        Int128 sig = int128_shl(v, 112 - msb);
        uint32_t E = msb + 16383;
        Fp128 r;
        r.limb[0] = sig.limb[0];
        r.limb[1] = sig.limb[1];
        r.limb[2] = sig.limb[2];
        r.limb[3] = (sig.limb[3] & 0xFFFFu) | (E << 16) | (s ? LIMB_SIGN_BIT : 0);
        return r;
    }

    /* msb > 112: needs rounding; shift right by msb - 112 */
    int shift = msb - 112;
    bool G = false, R = false, S = false;
    if (shift >= 1) G = (v.limb[(shift - 1) / 32] >> ((shift - 1) % 32)) & 1;
    if (shift >= 2) R = (v.limb[(shift - 2) / 32] >> ((shift - 2) % 32)) & 1;
    if (shift >= 3) S = !int128_is_zero(int128_normalize(v, shift - 2, UNSIGNED));

    Int128 t = int128_lshr(v, shift);
    bool L = t.limb[0] & 1;

    if (G && (R || S || L)) {
        t = int128_add(t, (Int128){{1, 0, 0, 0}});
        if (int128_bit_width(t, UNSIGNED) > 113) {
            t = int128_shr(t, 1, UNSIGNED);
            msb++;
        }
    }

    int E = msb + 16383;
    Fp128 r;
    r.limb[0] = t.limb[0];
    r.limb[1] = t.limb[1];
    r.limb[2] = t.limb[2];
    r.limb[3] = (t.limb[3] & 0xFFFFu) | ((uint32_t)E << 16) | (s ? LIMB_SIGN_BIT : 0);
    return r;
}

Int128 fp128_to_int128(Fp128 v, SignKind sign, bool *ok) {
    *ok = true;

    /* NaN / Inf: failure */
    if (fp128_is_nan(v) || fp128_is_inf(v)) {
        *ok = false;
        return (Int128){{0, 0, 0, 0}};
    }
    /* ±0 */
    if (fp128_is_zero(v)) {
        return (Int128){{0, 0, 0, 0}};
    }

    int s = fp128_get_sign(v) ? 1 : 0;
    int e = fp128_get_exp(v);
    Int128 m = fp128_get_m(v);

    /* Subnormal: |value| < 2^-16382 < 1, truncates to 0 */
    if (e == 0) {
        return (Int128){{0, 0, 0, 0}};
    }

    /* Add the implicit bit, compute the unbiased exponent */
    Int128 sig = int128_or(m, int128_shl(INT128_ONE, 112));
    int exp = e - 16383;

    /* |value| = sig x 2^(exp - 112) */
    Int128 uval;
    if (exp < 0) {
        /* |value| < 1, truncates to 0 */
        return (Int128){{0, 0, 0, 0}};
    } else if (exp <= 112) {
        /* uval = sig >> (112 - exp), fractional part truncated */
        uval = int128_shr(sig, 112 - exp, UNSIGNED);
        /* For exp <= 112, uval < 2^113, cannot overflow any target type */
    } else {
        int lsh = exp - 112;
        /* lsh >= 16: sig << 16 >= 2^128, always overflows */
        if (lsh >= 16) {
            *ok = false;
            return (Int128){{0, 0, 0, 0}};
        }
        uval = int128_shl(sig, lsh);
    }

    /* Range check */
    if (sign == SIGNED) {
        if (s) {
            /* Negative: |value| <= 2^127 */
            Int128 limit = int128_shl(INT128_ONE, 127);
            int cmp = int128_cmp_unsigned(uval, limit);
            if (cmp > 0) {
                *ok = false;
                return (Int128){{0, 0, 0, 0}};
            }
            if (cmp == 0) return limit; /* INT128_MIN */
            return int128_neg(uval);
        } else {
            /* Positive: value <= 2^127 - 1 */
            Int128 limit = int128_sub(int128_shl(INT128_ONE, 127), INT128_ONE);
            if (int128_cmp_unsigned(uval, limit) > 0) {
                *ok = false;
                return (Int128){{0, 0, 0, 0}};
            }
            return uval;
        }
    } else {
        /* Unsigned: converting a negative float is UB, report failure */
        if (s) {
            *ok = false;
            return (Int128){{0, 0, 0, 0}};
        }
        return uval;
    }
}

const Fp128 FP128_ZERO = {{0, 0, 0, 0}};
const Fp128 FP128_ONE = {{0, 0, 0, 0x3FFF0000u}};
const Fp128 FP128_INF = {{0, 0, 0, 0x7FFF0000u}};
const Fp128 FP128_NAN = {{0, 0, 0, 0x7FFF8000u}};

static Int256 i256_zero(void) {
    Int256 r;
    for (int i = 0; i < 8; i++) r.limb[i] = 0;
    return r;
}

static int i256_msb(Int256 a) {
    for (int i = 7; i >= 0; i--) {
        if (a.limb[i]) {
            int b = 31;
            uint32_t v = a.limb[i];
            while (!(v & 0x80000000u)) {
                v <<= 1;
                b--;
            }
            return i * 32 + b;
        }
    }
    return -1;
}

static Int256 i256_shr(Int256 a, int n) {
    Int256 r = i256_zero();
    if (n <= 0) return a;
    if (n >= 256) return r;
    int ln = n / 32, bn = n % 32;
    for (int i = 0; i < 8; i++) {
        int src = i + ln;
        if (src >= 8) break;
        uint32_t v = a.limb[src] >> bn;
        if (bn > 0 && src + 1 < 8) v |= a.limb[src + 1] << (32 - bn);
        r.limb[i] = v;
    }
    return r;
}

static Int256 i256_shl(Int256 a, int n) {
    Int256 r = i256_zero();
    if (n <= 0) return a;
    if (n >= 256) return r;
    int ln = n / 32, bn = n % 32;

    for (int i = 0; i < 8; i++) {
        int d = i + ln;
        if (d >= 8) break;                                                /* destination past the top; stop */
        r.limb[d] |= a.limb[i] << bn;                                     /* low 32-bn bits land in limb[d] */
        if (bn > 0 && d + 1 < 8) r.limb[d + 1] |= a.limb[i] >> (32 - bn); /* high bn bits in limb[d+1] */
    }
    return r;
}

static void i256_mul_full(Int256 a, Int256 b, Int256 *hi, Int256 *lo) {
    uint32_t r[16] = {0};
    for (int i = 0; i < 8; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < 8; j++) {
            uint64_t v = (uint64_t)a.limb[i] * b.limb[j] + r[i + j] + carry;
            r[i + j] = (uint32_t)v;
            carry = v >> 32;
        }
        r[i + 8] = (uint32_t)carry;
    }
    for (int i = 0; i < 8; i++) {
        hi->limb[i] = r[i + 8];
        lo->limb[i] = r[i];
    }
}

static bool i256_is_zero(Int256 a) {
    for (int i = 0; i < 8; i++)
        if (a.limb[i]) return false;
    return true;
}

static int i256_cmp(Int256 a, Int256 b) {
    for (int i = 7; i >= 0; i--) {
        if (a.limb[i] != b.limb[i]) return a.limb[i] > b.limb[i] ? 1 : -1;
    }
    return 0;
}

static Int256 i256_sub(Int256 a, Int256 b) {
    Int256 r;
    uint64_t borrow = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t sub = (uint64_t)b.limb[i] + borrow;
        uint64_t x = (uint64_t)a.limb[i];
        r.limb[i] = (uint32_t)(x - sub);
        borrow = (x < sub) ? 1 : 0;
    }
    return r;
}

static bool i256_low_nonzero(Int256 a, int n) {
    if (n <= 0) return false;
    if (n >= 256) {
        for (int i = 0; i < 8; i++)
            if (a.limb[i]) return true;
        return false;
    }
    int limbs = (n + 31) / 32, rem = n % 32;
    for (int i = 0; i < limbs - 1; i++)
        if (a.limb[i]) return true;
    uint32_t mask = (rem == 0) ? 0xFFFFFFFFu : ((1u << rem) - 1);
    return (a.limb[limbs - 1] & mask) != 0;
}

/* Long division: the top 256 bits of Q = (M x 2^256) / T; a nonzero
 * remainder is reported through rem_nonzero (for the caller's sticky) */
static Int256 i256_div(Int256 M, Int256 T, bool *rem_nonzero) {
    Int256 Q = i256_zero(), R = i256_zero();
    for (int i = 511; i >= 0; i--) {
        bool overflow = (R.limb[7] & 0x80000000u) != 0;
        R = i256_shl(R, 1);
        if (i >= 256) {
            int pos = i - 256;
            if ((M.limb[pos / 32] >> (pos % 32)) & 1) R.limb[0] |= 1;
        }
        if (overflow || i256_cmp(R, T) >= 0) {
            R = i256_sub(R, T);
            if (i < 256) {
                Q.limb[i / 32] |= 1u << (i % 32);
            }
        }
    }
    if (rem_nonzero) *rem_nonzero = !i256_is_zero(R);
    return Q;
}
/* value = m x 2^e, m a 256-bit normalized mantissa (MSB = bit 255) */
typedef struct {
    Int256 m;
    int e;
} Pow5;

static Pow5 pow5_mul(Pow5 a, Pow5 b) {
    Int256 hi, lo;
    i256_mul_full(a.m, b.m, &hi, &lo);
    Pow5 r;
    if (i256_msb(hi) == 255) {
        r.m = hi;
        r.e = a.e + b.e + 256;
    } else {
        r.m = i256_shl(hi, 1);
        if (lo.limb[7] & 0x80000000u) r.m.limb[0] |= 1;
        r.e = a.e + b.e + 255;
    }
    return r;
}

static Pow5 pow5_pow(int k) {
    /* r = 1 (mantissa 2^255, exponent -255) */
    Pow5 r = {i256_zero(), -255};
    r.m.limb[7] = 0x80000000u;

    /* base = 5 (mantissa 5 << 253, exponent -253) */
    Pow5 base = {i256_zero(), -253};
    base.m.limb[0] = 5;
    base.m = i256_shl(base.m, 253);

    while (k > 0) {
        if (k & 1) r = pow5_mul(r, base);
        k >>= 1;
        if (k > 0) base = pow5_mul(base, base);
    }
    return r;
}

#define DIGITS_MAX 60

/* Case-insensitive prefix match (for the inf/nan spellings) */
static bool prefix_ci(const char *s, const char *prefix) {
    while (*prefix) {
        char c = *s++;
        char p = *prefix++;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != p) return false;
    }
    return true;
}

bool fp128_set_str(Fp128 *v, const char *str, FpFormat target) {
    int sign = 0;
    uint8_t digits[DIGITS_MAX];
    int ndigits = 0, exp10 = 0, exp_adj = 0;
    bool point = false, tail_sticky = false;

    while (*str == ' ' || *str == '\t') str++;
    if (*str == '+')
        str++;
    else if (*str == '-') {
        sign = 1;
        str++;
    }

    /* inf / infinity / nan spellings (must be checked before the digit loop) */
    if (*str != '.' && !(*str >= '0' && *str <= '9')) {
        const char *w = NULL;
        if (prefix_ci(str, "infinity"))
            w = str + 8;
        else if (prefix_ci(str, "inf"))
            w = str + 3;
        if (w) {
            while (*w == ' ' || *w == '\t') w++;
            if (*w != '\0') return false;
            Fp128 inf = {{0, 0, 0, 0}};
            inf.limb[3] = (0x7FFFu << 16) | (sign ? LIMB_SIGN_BIT : 0);
            *v = inf;
            return true;
        }
        if (prefix_ci(str, "nan")) {
            const char *n = str + 3;
            while (*n == ' ' || *n == '\t') n++;
            if (*n != '\0') return false;
            *v = sign ? fp128_neg(FP128_NAN) : FP128_NAN;
            return true;
        }
    }

    while (*str == '0') str++; /* leading zeros */

    while (*str) {
        char c = *str++;
        if (c >= '0' && c <= '9') {
            if (ndigits < DIGITS_MAX)
                digits[ndigits++] = c - '0';
            else if (c != '0')
                tail_sticky = true;
            /* Saturate exponent/digit counters to avoid int overflow on
             * very long inputs */
            if (point && exp_adj > -100000) exp_adj--;
        } else if (c == '.' && !point) {
            point = true;
        } else if (c == 'e' || c == 'E') {
            int esign = 1;
            if (*str == '+')
                str++;
            else if (*str == '-') {
                esign = -1;
                str++;
            }
            int ev = 0;
            while (*str >= '0' && *str <= '9') {
                if (ev < 100000) ev = ev * 10 + (*str - '0');
                str++;
            }
            exp10 = ev * esign + exp_adj;
            goto parsed;
        } else
            break;
    }
    exp10 = exp_adj;
parsed:

    /* Only trailing whitespace is allowed; anything else is invalid */
    while (*str == ' ' || *str == '\t') str++;
    if (*str != '\0') return false;

    if (ndigits == 0) {
        Fp128 z = {{0, 0, 0, 0}};
        z.limb[3] = sign ? LIMB_SIGN_BIT : 0;
        *v = z;
        return true;
    }

    /* Fast range check: beyond the fp128 range (~1.19e4932) return Inf/0 */
    int log10_est = ndigits + exp10;
    if (log10_est > 4967) {
        Fp128 inf = {{0, 0, 0, 0}};
        inf.limb[3] = (0x7FFFu << 16) | (sign ? LIMB_SIGN_BIT : 0);
        *v = inf;
        return true;
    }
    if (log10_est < -4967) {
        Fp128 z = {{0, 0, 0, 0}};
        z.limb[3] = sign ? LIMB_SIGN_BIT : 0;
        *v = z;
        return true;
    }

    /* Convert decimal digits to Int256 */
    Int256 M = i256_zero();
    for (int i = 0; i < ndigits; i++) {
        uint64_t carry = digits[i];
        for (int j = 0; j < 8; j++) {
            uint64_t x = (uint64_t)M.limb[j] * 10 + carry;
            M.limb[j] = (uint32_t)x;
            carry = x >> 32;
        }
    }

    /* Normalize M to bit 200 */
    int M_shift = 200 - i256_msb(M);
    M = i256_shl(M, M_shift);
    int E = -M_shift;

    /* Apply 10^exp10. Nonzero bits below the 256-bit window only affect
     * sticky; record them separately. */
    bool sticky256 = false;
    if (exp10 > 0) {
        Pow5 p = pow5_pow(exp10);
        Int256 hi, lo;
        i256_mul_full(M, p.m, &hi, &lo);
        M = hi;
        sticky256 = !i256_is_zero(lo);
        E += p.e + 256 + exp10;
    } else if (exp10 < 0) {
        Pow5 p = pow5_pow(-exp10);
        M = i256_div(M, p.m, &sticky256);
        E += -p.e - 256 + exp10;
    }

    if (i256_msb(M) < 0) {
        Fp128 z = {{0, 0, 0, 0}};
        z.limb[3] = sign ? LIMB_SIGN_BIT : 0;
        *v = z;
        return true;
    }

    /* ---------- 5. Normalize: MSB to bit 115 ---------- */
    int msb = i256_msb(M);
    int shift = msb - 115;
    bool S_low = (shift > 0) ? i256_low_nonzero(M, shift) : false;

    if (shift > 0) {
        M = i256_shr(M, shift);
        E += shift;
    } else if (shift < 0) {
        M = i256_shl(M, -shift);
        E += shift;
    }

    /* ---------- 6. Build the 116-bit sig ---------- */
    /* After the right shift, bit 115 of M is the implicit bit, bits 114..3
     * are the 112-bit fraction, bit 2 = G, bit 1 = R, bit 0 = first S bit.
     * The shifted-out low bits and tail_sticky are tracked separately. */
    Int128 sig;
    sig.limb[0] = M.limb[0];
    sig.limb[1] = M.limb[1];
    sig.limb[2] = M.limb[2];
    sig.limb[3] = M.limb[3];
    bool sticky = S_low || tail_sticky || sticky256;

    /* ---------- 7. Exponent ---------- */
    int E_fp128 = E + 115 + 16383;

    /* Non-FP128 target: single rounding to the target precision, then
     * extend (avoids double-rounding errors) */
    if (target != FP128) {
        *v = round116_to_target(sig, E_fp128, sign, target, sticky);
        return true;
    }

    /* Overflow/underflow are handled inside fp128_round_and_pack */
    *v = fp128_round_and_pack(sig, E_fp128, sign, sticky);
    return true;
}

bool fp128_set_hex_str(Fp128 *v, const char *str, FpFormat target) {
    while (*str == ' ' || *str == '\t') str++;

    int sign = 0;

    /* Sign */
    if (*str == '+')
        str++;
    else if (*str == '-') {
        sign = 1;
        str++;
    }

    /* inf / infinity / nan spellings */
    if (!(str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))) {
        const char *w = NULL;
        if (prefix_ci(str, "infinity"))
            w = str + 8;
        else if (prefix_ci(str, "inf"))
            w = str + 3;
        if (w) {
            while (*w == ' ' || *w == '\t') w++;
            if (*w == '\0') {
                Fp128 inf = {{0, 0, 0, 0}};
                inf.limb[3] = (0x7FFFu << 16) | (sign ? LIMB_SIGN_BIT : 0);
                *v = inf;
                return true;
            }
            return false;
        }
        if (prefix_ci(str, "nan")) {
            const char *n = str + 3;
            while (*n == ' ' || *n == '\t') n++;
            if (*n == '\0') {
                *v = sign ? fp128_neg(FP128_NAN) : FP128_NAN;
                return true;
            }
            return false;
        }
        return false;
    }

    /* 0x / 0X prefix */
    str += 2;

    Int128 D = {{0, 0, 0, 0}};
    int bits_used = 0;       /* bits accumulated so far */
    int hex_after_point = 0; /* hex digits after the point */
    bool point = false;
    bool sticky = false; /* nonzero bits beyond bit 115 */
    bool seen_nonzero = false;

    while (*str) {
        if (*str == '.') {
            if (point) return false;
            point = true;
            str++;
            continue;
        }
        int d;
        char c = *str;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            break;

        str++;
        /* Saturate the counter to avoid int overflow on very long inputs */
        if (point && hex_after_point < 100000) hex_after_point++;

        /* Leading zeros skip D accumulation */
        if (!seen_nonzero && d == 0) continue;
        seen_nonzero = true;

        if (bits_used + 4 <= 128) {
            D = int128_shl(D, 4);
            D.limb[0] |= (uint32_t)d;
            bits_used += 4;
        } else {
            if (d != 0) sticky = true;
        }
    }

    /* Binary exponent p/P */
    int p_exp = 0;
    if (*str == 'p' || *str == 'P') {
        str++;
        int esign = 1;
        if (*str == '+')
            str++;
        else if (*str == '-') {
            esign = -1;
            str++;
        }
        if (*str < '0' || *str > '9') return false;
        while (*str >= '0' && *str <= '9') {
            if (p_exp < 100000) p_exp = p_exp * 10 + (*str - '0');
            str++;
        }
        p_exp *= esign;
    }
    while (*str == ' ' || *str == '\t') str++;
    if (*str != '\0') return false;

    /* All zero */
    if (!seen_nonzero) {
        Fp128 z = {{0, 0, 0, 0}};
        z.limb[3] = sign ? LIMB_SIGN_BIT : 0;
        *v = z;
        return true;
    }

    /* value = D × 2^(p_exp - 4 × hex_after_point) */
    int bin_exp = p_exp - 4 * hex_after_point;
    int msb = int128_bit_width(D, UNSIGNED) - 1;

    /* Normalize: implicit bit to bit 115; sticky stays separate */
    Int128 sig;
    if (msb <= 115) {
        sig = int128_shl(D, 115 - msb);
    } else {
        int shift = msb - 115;
        sticky |= !int128_is_zero(int128_normalize(D, shift, UNSIGNED));
        sig = int128_shr(D, shift, UNSIGNED);
    }

    /* fp128 exponent: value = 1.xxx x 2^(bin_exp + msb) */
    int E_fp128 = bin_exp + msb + 16383;

    /* Non-FP128 target: single rounding to the target precision, then
     * extend (avoids double-rounding errors) */
    if (target != FP128) {
        *v = round116_to_target(sig, E_fp128, sign, target, sticky);
        return true;
    }

    /* Overflow/underflow are handled inside fp128_round_and_pack */
    *v = fp128_round_and_pack(sig, E_fp128, sign, sticky);
    return true;
}

/* Number of mantissa hex digits for a target: fraction bits = p-1,
 * ndigits = ceil((p-1)/4) */
static int hexstr_digits(FpFormat target) {
    switch (target) {
        case FP16:
            return 3;
        case FP32:
            return 6;
        case FP64:
            return 13;
        case FP80:
            return 16;
        default:
            return 28;
    }
}

/* Write a fixed string; return -1 if the buffer is too small.
 * (No libc string functions: the library stays freestanding-clean.) */
static int hexstr_fixed(char *buf, size_t bufsize, const char *s) {
    size_t n = 0;
    while (s[n] != '\0') n++;
    if (n + 1 > bufsize) return -1;
    for (size_t i = 0; i <= n; i++) buf[i] = s[i];
    return (int)n;
}

int fp128_to_hexstr(Fp128 *v, char *buf, size_t bufsize, FpFormat target) {
    if (v == NULL || buf == NULL) return -1;

    /* Round once to the target format (identity for FP128), extend back to
     * fp128 storage, then print exactly */
    Fp128 x = fp128_round_to(*v, target);

    /* Special values (glibc printf %a style, sign preserved) */
    if (fp128_is_nan(x)) return hexstr_fixed(buf, bufsize, fp128_get_sign(x) ? "-nan" : "nan");
    if (fp128_is_inf(x)) return hexstr_fixed(buf, bufsize, fp128_get_sign(x) ? "-inf" : "inf");
    if (fp128_is_zero(x)) return hexstr_fixed(buf, bufsize, fp128_get_sign(x) ? "-0x0p+0" : "0x0p+0");

    int s, e;
    Int128 sig;
    fp128_decompose_norm(x, &s, &e, &sig);

    /* Mantissa: x has already been exactly rounded to the target precision
     * (low bits zero-padded), so taking the top 4*ndigits fraction bits is
     * exact - no second rounding. */
    int ndigits = hexstr_digits(target);
    Int128 frac = int128_lshr(sig, 112 - 4 * ndigits);

    /* Extract digits and strip trailing zeros (glibc %a minimal form) */
    char digits[28];
    int k = 0;
    for (int i = 4 * ndigits - 4; i >= 0; i -= 4) {
        uint32_t d = (frac.limb[i / 32] >> (i % 32)) & 0xF;
        digits[k++] = "0123456789abcdef"[d];
    }
    while (k > 0 && digits[k - 1] == '0') k--;

    /* Decimal digit count of the exponent */
    int ae = (e < 0) ? -e : e;
    int exp_digits = 1;
    for (int t = ae; t >= 10; t /= 10) exp_digits++;

    /* Exact required length: sign + "0x1" + [".digits"] + "p" + exponent
     * sign + exponent + NUL */
    size_t need = (size_t)((s ? 1 : 0) + 3 + (k > 0 ? 1 + k : 0) + 1 + 1 + exp_digits) + 1;
    if (need > bufsize) return -1;

    char *p = buf;
    if (s) *p++ = '-';
    *p++ = '0';
    *p++ = 'x';
    *p++ = '1';
    if (k > 0) {
        *p++ = '.';
        for (int i = 0; i < k; i++) *p++ = digits[i];
    }
    *p++ = 'p';
    if (e < 0) {
        *p++ = '-';
        e = -e;
    } else {
        *p++ = '+';
    }
    char tmp[8];
    int m = 0;
    do {
        tmp[m++] = (char)('0' + e % 10);
        e /= 10;
    } while (e > 0);
    while (m > 0) *p++ = tmp[--m];
    *p = '\0';
    return (int)(p - buf);
}
