#include "int128.h"

Int128 int128_and(Int128 a, Int128 b) {
    Int128 r;
    r.limb[0] = a.limb[0] & b.limb[0];
    r.limb[1] = a.limb[1] & b.limb[1];
    r.limb[2] = a.limb[2] & b.limb[2];
    r.limb[3] = a.limb[3] & b.limb[3];
    return r;
}

Int128 int128_or(Int128 a, Int128 b) {
    Int128 r;
    r.limb[0] = a.limb[0] | b.limb[0];
    r.limb[1] = a.limb[1] | b.limb[1];
    r.limb[2] = a.limb[2] | b.limb[2];
    r.limb[3] = a.limb[3] | b.limb[3];
    return r;
}

Int128 int128_xor(Int128 a, Int128 b) {
    Int128 r;
    r.limb[0] = a.limb[0] ^ b.limb[0];
    r.limb[1] = a.limb[1] ^ b.limb[1];
    r.limb[2] = a.limb[2] ^ b.limb[2];
    r.limb[3] = a.limb[3] ^ b.limb[3];
    return r;
}

Int128 int128_not(Int128 a) {
    Int128 r;
    r.limb[0] = ~a.limb[0];
    r.limb[1] = ~a.limb[1];
    r.limb[2] = ~a.limb[2];
    r.limb[3] = ~a.limb[3];
    return r;
}

Int128 int128_shl(Int128 a, int amount) {
    Int128 r;
    if (amount <= 0) return a;
    if (amount >= 128) {
        r.limb[0] = r.limb[1] = r.limb[2] = r.limb[3] = 0;
        return r;
    }

    int limb_shift = amount / 32;
    int bit_shift = amount % 32;

    if (bit_shift == 0) {
        if (limb_shift == 1) {
            r.limb[0] = 0;
            r.limb[1] = a.limb[0];
            r.limb[2] = a.limb[1];
            r.limb[3] = a.limb[2];
        } else if (limb_shift == 2) {
            r.limb[0] = 0;
            r.limb[1] = 0;
            r.limb[2] = a.limb[0];
            r.limb[3] = a.limb[1];
        } else if (limb_shift == 3) {
            r.limb[0] = 0;
            r.limb[1] = 0;
            r.limb[2] = 0;
            r.limb[3] = a.limb[0];
        } else {
            r = a;
        }
        return r;
    }

    if (limb_shift == 0) {
        r.limb[3] = (a.limb[3] << bit_shift) | (a.limb[2] >> (32 - bit_shift));
        r.limb[2] = (a.limb[2] << bit_shift) | (a.limb[1] >> (32 - bit_shift));
        r.limb[1] = (a.limb[1] << bit_shift) | (a.limb[0] >> (32 - bit_shift));
        r.limb[0] = a.limb[0] << bit_shift;
    } else if (limb_shift == 1) {
        r.limb[3] = (a.limb[2] << bit_shift) | (a.limb[1] >> (32 - bit_shift));
        r.limb[2] = (a.limb[1] << bit_shift) | (a.limb[0] >> (32 - bit_shift));
        r.limb[1] = a.limb[0] << bit_shift;
        r.limb[0] = 0;
    } else if (limb_shift == 2) {
        r.limb[3] = (a.limb[1] << bit_shift) | (a.limb[0] >> (32 - bit_shift));
        r.limb[2] = a.limb[0] << bit_shift;
        r.limb[1] = 0;
        r.limb[0] = 0;
    } else {
        r.limb[3] = a.limb[0] << bit_shift;
        r.limb[2] = 0;
        r.limb[1] = 0;
        r.limb[0] = 0;
    }
    return r;
}

Int128 int128_shr(Int128 a, int amount, SignKind sign) {
    Int128 r;
    if (amount <= 0) return a;

    bool is_negative = (sign == SIGNED) && (a.limb[3] & LIMB_SIGN_BIT);
    uint32_t fill = is_negative ? 0xFFFFFFFF : 0;

    if (amount >= 128) {
        r.limb[0] = r.limb[1] = r.limb[2] = r.limb[3] = fill;
        return r;
    }

    int limb_shift = amount / 32;
    int bit_shift = amount % 32;

    if (bit_shift == 0) {
        if (limb_shift == 1) {
            r.limb[0] = a.limb[1];
            r.limb[1] = a.limb[2];
            r.limb[2] = a.limb[3];
            r.limb[3] = fill;
        } else if (limb_shift == 2) {
            r.limb[0] = a.limb[2];
            r.limb[1] = a.limb[3];
            r.limb[2] = fill;
            r.limb[3] = fill;
        } else if (limb_shift == 3) {
            r.limb[0] = a.limb[3];
            r.limb[1] = fill;
            r.limb[2] = fill;
            r.limb[3] = fill;
        } else {
            r = a;
        }
        return r;
    }

    if (limb_shift == 0) {
        r.limb[0] = (a.limb[0] >> bit_shift) | (a.limb[1] << (32 - bit_shift));
        r.limb[1] = (a.limb[1] >> bit_shift) | (a.limb[2] << (32 - bit_shift));
        r.limb[2] = (a.limb[2] >> bit_shift) | (a.limb[3] << (32 - bit_shift));
        r.limb[3] = (a.limb[3] >> bit_shift) | (fill << (32 - bit_shift));
    } else if (limb_shift == 1) {
        r.limb[0] = (a.limb[1] >> bit_shift) | (a.limb[2] << (32 - bit_shift));
        r.limb[1] = (a.limb[2] >> bit_shift) | (a.limb[3] << (32 - bit_shift));
        r.limb[2] = (a.limb[3] >> bit_shift) | (fill << (32 - bit_shift));
        r.limb[3] = fill;
    } else if (limb_shift == 2) {
        r.limb[0] = (a.limb[2] >> bit_shift) | (a.limb[3] << (32 - bit_shift));
        r.limb[1] = (a.limb[3] >> bit_shift) | (fill << (32 - bit_shift));
        r.limb[2] = fill;
        r.limb[3] = fill;
    } else {
        r.limb[0] = (a.limb[3] >> bit_shift) | (fill << (32 - bit_shift));
        r.limb[1] = fill;
        r.limb[2] = fill;
        r.limb[3] = fill;
    }

    return r;
}

Int128 int128_ashr(Int128 a, int amount) { return int128_shr(a, amount, SIGNED); }
Int128 int128_lshr(Int128 a, int amount) { return int128_shr(a, amount, UNSIGNED); }

static Int128 int128_add2(Int128 a, Int128 b, int c0) {
    Int128 r;
    uint64_t s;

    s = (uint64_t)a.limb[0] + b.limb[0] + c0;
    r.limb[0] = (uint32_t)s;

    s = (uint64_t)a.limb[1] + b.limb[1] + (s >> 32);
    r.limb[1] = (uint32_t)s;

    s = (uint64_t)a.limb[2] + b.limb[2] + (s >> 32);
    r.limb[2] = (uint32_t)s;

    s = (uint64_t)a.limb[3] + b.limb[3] + (s >> 32);
    r.limb[3] = (uint32_t)s;

    return r;
}

Int128 int128_add(Int128 a, Int128 b) { return int128_add2(a, b, 0); }

Int128 int128_sub(Int128 a, Int128 b) { return int128_add2(a, int128_not(b), 1); }

Int128 int128_neg(Int128 a) {
    Int128 zero = {{0, 0, 0, 0}};
    return int128_add2(int128_not(a), zero, 1);
}

Int128 int128_abs(Int128 a) {
    if (a.limb[3] & LIMB_SIGN_BIT) return int128_neg(a);
    return a;
}

int int128_cmp_unsigned(Int128 a, Int128 b) {
    if (a.limb[3] > b.limb[3])
        return 1;
    else if (a.limb[3] < b.limb[3])
        return -1;

    if (a.limb[2] > b.limb[2])
        return 1;
    else if (a.limb[2] < b.limb[2])
        return -1;

    if (a.limb[1] > b.limb[1])
        return 1;
    else if (a.limb[1] < b.limb[1])
        return -1;

    if (a.limb[0] > b.limb[0])
        return 1;
    else if (a.limb[0] < b.limb[0])
        return -1;

    return 0;
}

int int128_cmp_signed(Int128 a, Int128 b) {
    a.limb[3] ^= LIMB_SIGN_BIT;
    b.limb[3] ^= LIMB_SIGN_BIT;
    return int128_cmp_unsigned(a, b);
}

int int128_cmp(Int128 a, Int128 b, SignKind sign) {
    return sign == SIGNED ? int128_cmp_signed(a, b) : int128_cmp_unsigned(a, b);
}

Int256 int128_mul_full(Int128 a, Int128 b) {
    Int256 r = {{0}};
    for (int i = 0; i < 4; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < 4; j++) {
            uint64_t prod = (uint64_t)a.limb[i] * b.limb[j] + r.limb[i + j] + carry;
            r.limb[i + j] = (uint32_t)prod;
            carry = prod >> 32;
        }
        r.limb[i + 4] = (uint32_t)carry;
    }
    return r;
}

static Int128 int128_mul2(Int128 a, Int128 b, bool hi) {
    Int256 full = int128_mul_full(a, b);
    Int128 r;
    int base = hi ? 4 : 0;
    r.limb[0] = full.limb[base + 0];
    r.limb[1] = full.limb[base + 1];
    r.limb[2] = full.limb[base + 2];
    r.limb[3] = full.limb[base + 3];
    return r;
}

Int128 int128_mul(Int128 a, Int128 b) { return int128_mul2(a, b, 0); }

static void int128_udivmod(Int128 a, Int128 b, Int128 *q_out, Int128 *r_out) {
    Int128 q = {{0, 0, 0, 0}};
    Int128 r = {{0, 0, 0, 0}};

    /* Division by zero convention: q=0, r=a (avoid silently returning
     * garbage) */
    if (int128_is_zero(b)) {
        *q_out = q;
        *r_out = a;
        return;
    }

    for (int i = 127; i >= 0; i--) {
        r = int128_shl(r, 1);
        int limb_idx = i / 32;
        int bit_idx = i % 32;
        r.limb[0] |= (a.limb[limb_idx] >> bit_idx) & 1;
        if (int128_cmp_unsigned(r, b) >= 0) {
            r = int128_sub(r, b);
            q.limb[limb_idx] |= (1u << bit_idx);
        }
    }

    *q_out = q;
    *r_out = r;
}

Int128 int128_div_unsigned(Int128 a, Int128 b) {
    Int128 q, r;
    int128_udivmod(a, b, &q, &r);
    return q;
}

Int128 int128_mod_unsigned(Int128 a, Int128 b) {
    Int128 q, r;
    int128_udivmod(a, b, &q, &r);
    return r;
}

Int128 int128_div_signed(Int128 a, Int128 b) {
    bool a_neg = (a.limb[3] & LIMB_SIGN_BIT) != 0;
    bool b_neg = (b.limb[3] & LIMB_SIGN_BIT) != 0;
    Int128 abs_a = a_neg ? int128_neg(a) : a;
    Int128 abs_b = b_neg ? int128_neg(b) : b;
    Int128 q = int128_div_unsigned(abs_a, abs_b);
    return (a_neg != b_neg) ? int128_neg(q) : q;
}

Int128 int128_mod_signed(Int128 a, Int128 b) {
    bool a_neg = (a.limb[3] & LIMB_SIGN_BIT) != 0;
    bool b_neg = (b.limb[3] & LIMB_SIGN_BIT) != 0;
    Int128 abs_a = a_neg ? int128_neg(a) : a;
    Int128 abs_b = b_neg ? int128_neg(b) : b;
    Int128 r = int128_mod_unsigned(abs_a, abs_b);
    return a_neg ? int128_neg(r) : r;
}

Int128 int128_div(Int128 a, Int128 b, SignKind sign) {
    return sign == SIGNED ? int128_div_signed(a, b) : int128_div_unsigned(a, b);
}

Int128 int128_mod(Int128 a, Int128 b, SignKind sign) {
    return sign == SIGNED ? int128_mod_signed(a, b) : int128_mod_unsigned(a, b);
}

Int128 int128_normalize(Int128 v, int width, SignKind sign) {
    if (width >= 128) return v;
    if (width <= 0) return (Int128){0};

    uint32_t nlimbs = width / 32;         /* fully covered limbs */
    uint32_t top_limb = (width - 1) / 32; /* index of the top limb */
    uint32_t top_bits = width % 32;       /* significant bits in the top limb */
    uint32_t top_bit = (width - 1) % 32;  /* bit index of the top bit */

    bool is_negative = (sign == SIGNED) && (v.limb[top_limb] & (1u << top_bit));
    uint32_t fill = is_negative ? 0xFFFFFFFF : 0;

    if (top_bits == 0) {
        switch (nlimbs) {
            case 1:
                v.limb[1] = fill;
                // fallthrough
            case 2:
                v.limb[2] = fill;
                // fallthrough
            case 3:
                v.limb[3] = fill;
                // fallthrough
            default:
                return v;
        }
    }

    uint32_t bit_shift = 32 - top_bits;
    v.limb[top_limb] = (v.limb[top_limb] << bit_shift >> bit_shift) | (fill << top_bits);

    switch (top_limb) {
        case 0:
            v.limb[1] = fill;
            // fallthrough
        case 1:
            v.limb[2] = fill;
            // fallthrough
        case 2:
            v.limb[3] = fill;
            // fallthrough
        default:
            return v;
    }
}

static const uint8_t clz_table[256] = {
    8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static uint32_t clz32_table(uint32_t x) {
    if (x == 0) return 32;
    uint32_t n = 0;
    uint16_t hi = (uint16_t)(x >> 16);
    if (hi == 0) {
        n += 16;
        hi = (uint16_t)x;
    }
    uint8_t b = hi >> 8;
    if (b == 0) {
        n += 8;
        b = (uint8_t)hi;
    }
    n += clz_table[b];
    return n;
}

static int int128_bit_width_unsigned(Int128 v) {
    for (int i = 3; i >= 0; i--) {
        uint32_t x;
        if ((x = v.limb[i])) return i * 32 + 32 - clz32_table(x);
    }
    return 1;
}

int int128_bit_width(Int128 v, SignKind sign) {
    bool is_neg = (sign == SIGNED) && (v.limb[3] & LIMB_SIGN_BIT);

    if (is_neg) v = int128_not(v);
    uint32_t bits = int128_bit_width_unsigned(v);
    return sign == SIGNED ? bits + 1 : bits;
}

bool int128_fits(Int128 v, int width, SignKind sign) {
    if (width >= 128) return true;

    if (sign == SIGNED) {
        if (width < 2) return false;
    } else {
        if (width < 1) return false;
    }

    return int128_bit_width(v, sign) <= width;
}

bool int128_is_zero(Int128 v) { return !(v.limb[0] | v.limb[1] | v.limb[2] | v.limb[3]); }

bool int128_is_negative(Int128 v) { return v.limb[3] & LIMB_SIGN_BIT; }

Int128 int128_set_i(int64_t i) {
    uint32_t hi = (uint64_t)i >> 32;
    uint32_t lo = (uint32_t)i;
    uint32_t fill = hi & LIMB_SIGN_BIT ? 0xFFFFFFFF : 0;
    return (Int128){{lo, hi, fill, fill}};
}

Int128 int128_set_ui(uint64_t i) {
    uint32_t hi = i >> 32;
    uint32_t lo = i;
    return (Int128){{lo, hi, 0, 0}};
}

static inline int from_hex(char c) {
    unsigned int x = (unsigned char)c;
    unsigned int d = x - '0';
    if (d < 10) return (int)d;
    unsigned int l = (x | 0x20) - 'a';
    if (l < 6) return (int)(l + 10);
    return -1;
}

bool int128_set_str(Int128 *v, const char *str, int base) {
    if (!str || !*str) return false;
    if (base < 2 || base > 16) return false;

    bool neg = false;
    if (*str == '+')
        str++;
    else if (*str == '-') {
        neg = true;
        str++;
    }
    if (base == 16 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) str += 2;

    int safe_len;
    switch (base) {
        case 2:
            safe_len = 64;
            break;
        case 8:
            safe_len = 21;
            break;
        case 10:
            safe_len = 19;
            break;
        case 16:
            safe_len = 16;
            break;
        default:
            return false;
    }

    /* The library is freestanding-clean: no libc string functions */
    int len = 0;
    while (str[len] != '\0') len++;
    const char *p = str;
    uint64_t r64 = 0;

    int fast_count = (len <= safe_len) ? len : safe_len;
    for (int i = 0; i < fast_count; i++, p++) {
        int d = from_hex(*p);
        if (d < 0 || d >= base) return false;
        r64 = r64 * base + d;
    }

    Int128 r = {{(uint32_t)r64, (uint32_t)(r64 >> 32), 0, 0}};

    if (*p) {
        Int128 b = {{(uint32_t)base, 0, 0, 0}};
        for (; *p; p++) {
            int d = from_hex(*p);
            if (d < 0 || d >= base) return false;

            Int256 full = int128_mul_full(r, b);
            if (full.limb[4] || full.limb[5] || full.limb[6] || full.limb[7]) return false;

            r.limb[0] = full.limb[0];
            r.limb[1] = full.limb[1];
            r.limb[2] = full.limb[2];
            r.limb[3] = full.limb[3];

            Int128 d128 = {{(uint32_t)d, 0, 0, 0}};
            Int128 sum = int128_add(r, d128);
            if (int128_cmp_unsigned(sum, r) < 0) return false;
            r = sum;
        }
    }

    /* Negative magnitudes are capped at 2^127 (the magnitude of INT128_MIN);
     * anything larger would wrap, so report overflow */
    if (neg && (r.limb[3] > LIMB_SIGN_BIT || (r.limb[3] == LIMB_SIGN_BIT && (r.limb[0] | r.limb[1] | r.limb[2]))))
        return false;
    if (neg) r = int128_neg(r);
    *v = r;
    return true;
}

int int128_to_str(Int128 v, SignKind sign, int base, char *buf, size_t bufsize) {
    if (bufsize == 0) return -1;
    if (base < 2 || base > 16) return -1;

    char tmp[144];
    int n = 0;
    bool neg = false;

    if (sign == SIGNED && int128_is_negative(v)) {
        neg = true;
        v = int128_neg(v);
    }

    if (int128_is_zero(v)) {
        tmp[n++] = '0';
    } else {
        while (!int128_is_zero(v)) {
            Int128 rem = int128_mod_unsigned(v, (Int128){{(uint32_t)base, 0, 0, 0}});
            v = int128_div_unsigned(v, (Int128){{(uint32_t)base, 0, 0, 0}});
            uint32_t d = rem.limb[0];
            tmp[n++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
        }
    }

    if (neg) tmp[n++] = '-';

    if ((size_t)n + 1 > bufsize) return -1;

    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return n;
}

const Int128 int128_min = {{0, 0, 0, 0x80000000u}};
const Int128 int128_max = {{0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x7FFFFFFFu}};
const Int128 uint128_min = {{0, 0, 0, 0}};
const Int128 uint128_max = {{0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}};
const Int128 int128_zero = {{0, 0, 0, 0}};
const Int128 int128_one = {{1, 0, 0, 0}};
