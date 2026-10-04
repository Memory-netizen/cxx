#include "cxx.h"

Type *infer_numtype(Token *tok) {
    uint32_t flags = tok->lit_suffix & ~SUF_NONDEC;
    bool nondec = tok->lit_suffix & SUF_NONDEC;
    Type *ty;
    if (flags & SUF_FLOAT) return T.ty_float;
    if (flags & SUF_DOUBLE) return T.ty_double;
    if (flags & SUF_LDOUBLE) return T.ty_ldouble;
    if (flags & SUF_F16) return f16;
    if (flags & SUF_F32) return f32;
    if (flags & SUF_F64) return f64;
    if (flags & SUF_F128) return f128;
    // The _BitInt width follows the value (tok->ival); the parser
    // overrides this stub.
    if (flags & SUF_BITINT) return T.ty_int;

    // The literal magnitude is non-negative (the sign is a separate
    // unary minus token), so unsigned comparisons pick the type.
    Int128 val = tok->ival;

    if (!nondec) {
        switch (flags) {
            case SUF_UNSIGNED | SUF_LLONG:
                ty = T.ty_ullong;
                break;
            case SUF_LLONG:
                ty = T.ty_llong;
                break;
            case SUF_UNSIGNED | SUF_LONG:
                ty = int128_cmp_unsigned(val, int128_set_ui(T.ulong_max)) <= 0 ? T.ty_ulong : T.ty_ullong;
                break;
            case SUF_LONG:
                ty = int128_cmp_unsigned(val, int128_set_ui(T.long_max)) <= 0    ? T.ty_long
                     : int128_cmp_unsigned(val, int128_set_ui(T.llong_max)) <= 0 ? T.ty_llong
                                                                                 : T.ty_ullong;
                break;
            case SUF_UNSIGNED:
                ty = int128_cmp_unsigned(val, int128_set_ui(T.uint_max)) <= 0    ? T.ty_uint
                     : int128_cmp_unsigned(val, int128_set_ui(T.ulong_max)) <= 0 ? T.ty_ulong
                                                                                 : T.ty_ullong;
                break;
            default:
                ty = int128_cmp_unsigned(val, int128_set_ui(T.int_max)) <= 0     ? T.ty_int
                     : int128_cmp_unsigned(val, int128_set_ui(T.long_max)) <= 0  ? T.ty_long
                     : int128_cmp_unsigned(val, int128_set_ui(T.llong_max)) <= 0 ? T.ty_llong
                                                                                 : T.ty_ullong;
                break;
        }
    } else {
        switch (flags) {
            case SUF_UNSIGNED | SUF_LLONG:
                ty = T.ty_ullong;
                break;
            case SUF_LLONG:
                ty = int128_cmp_unsigned(val, int128_set_ui(T.llong_max)) <= 0 ? T.ty_llong : T.ty_ullong;
                break;
            case SUF_UNSIGNED | SUF_LONG:
                ty = int128_cmp_unsigned(val, int128_set_ui(T.ulong_max)) <= 0 ? T.ty_ulong : T.ty_ullong;
                break;
            case SUF_LONG:
                ty = int128_cmp_unsigned(val, int128_set_ui(T.long_max)) <= 0    ? T.ty_long
                     : int128_cmp_unsigned(val, int128_set_ui(T.ulong_max)) <= 0 ? T.ty_ulong
                     : int128_cmp_unsigned(val, int128_set_ui(T.llong_max)) <= 0 ? T.ty_llong
                                                                                 : T.ty_ullong;
                break;
            case SUF_UNSIGNED:
                ty = int128_cmp_unsigned(val, int128_set_ui(T.uint_max)) <= 0    ? T.ty_uint
                     : int128_cmp_unsigned(val, int128_set_ui(T.ulong_max)) <= 0 ? T.ty_ulong
                                                                                 : T.ty_ullong;
                break;
            default:
                ty = int128_cmp_unsigned(val, int128_set_ui(T.int_max)) <= 0     ? T.ty_int
                     : int128_cmp_unsigned(val, int128_set_ui(T.uint_max)) <= 0  ? T.ty_uint
                     : int128_cmp_unsigned(val, int128_set_ui(T.long_max)) <= 0  ? T.ty_long
                     : int128_cmp_unsigned(val, int128_set_ui(T.ulong_max)) <= 0 ? T.ty_ulong
                     : int128_cmp_unsigned(val, int128_set_ui(T.llong_max)) <= 0 ? T.ty_llong
                                                                                 : T.ty_ullong;
                break;
        }
    }
    return ty;
}

Type *infer_chartype(Token *tok) {
    uint32_t prefix = tok->enc_prefix;
    Type *ty = prefix == PREFIX_NONE ? T.ty_int
               : prefix == PREFIX_L  ? T.ty_wchar
               : prefix == PREFIX_U  ? T.ty_uint
               : prefix == PREFIX_u  ? T.ty_ushort
                                     : T.ty_uchar;
    tok->ival = int128_normalize(tok->ival, ty->size * 8, ty->is_unsigned ? UNSIGNED : SIGNED);
    return ty;
}

Type *infer_strtype(Token *tok) {
    uint32_t prefix = tok->enc_prefix;
    Type *ty = prefix == PREFIX_NONE ? T.ty_char
               : prefix == PREFIX_L  ? T.ty_wchar
               : prefix == PREFIX_U  ? T.ty_uint
               : prefix == PREFIX_u  ? T.ty_ushort
                                     : T.ty_uchar;
    uint32_t len = str_len(tok->id) / ty->size;
    return array_of(ty, len + 1);
}
