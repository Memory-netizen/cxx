#include "cxx.h"

Type *infer_numtype(Token *tok) {
    uint64_t val = tok->val;
    uint32_t flags = tok->lit_suffix & ~SUF_NONDEC;
    bool nondec = tok->lit_suffix & SUF_NONDEC;
    Type *ty;
    if (flags == SUF_FLOAT) return ty_float;
    if (flags == SUF_DOUBLE) return ty_double;
    if (flags == SUF_LDOUBLE) return ty_ldouble;

    if (!nondec) {
        switch (flags) {
            case SUF_UNSIGNED | SUF_LLONG:
                ty = ty_ullong;
                break;
            case SUF_LLONG:
                ty = ty_llong;
                break;
            case SUF_UNSIGNED | SUF_LONG:
                ty = val <= T.ulong_max ? ty_ulong : ty_ullong;
                break;
            case SUF_LONG:
                ty = val <= T.long_max ? ty_long : val <= T.llong_max ? ty_llong : ty_ullong;
                break;
            case SUF_UNSIGNED:
                ty = val <= T.uint_max ? ty_uint : val <= T.ulong_max ? ty_ulong : ty_ullong;
                break;
            default:
                ty = val <= T.int_max     ? ty_int
                     : val <= T.long_max  ? ty_long
                     : val <= T.llong_max ? ty_llong
                                          : ty_ullong;
                break;
        }
    } else {
        switch (flags) {
            case SUF_UNSIGNED | SUF_LLONG:
                ty = ty_ullong;
                break;
            case SUF_LLONG:
                ty = val <= T.llong_max ? ty_llong : ty_ullong;
                break;
            case SUF_UNSIGNED | SUF_LONG:
                ty = val <= T.ulong_max ? ty_ulong : ty_ullong;
                break;
            case SUF_LONG:
                ty = val <= T.long_max    ? ty_long
                     : val <= T.ulong_max ? ty_ulong
                     : val <= T.llong_max ? ty_llong
                                          : ty_ullong;
                break;
            case SUF_UNSIGNED:
                ty = val <= T.uint_max ? ty_uint : val <= T.ulong_max ? ty_ulong : ty_ullong;
                break;
            default:
                ty = val <= T.int_max     ? ty_int
                     : val <= T.uint_max  ? ty_uint
                     : val <= T.long_max  ? ty_long
                     : val <= T.ulong_max ? ty_ulong
                     : val <= T.llong_max ? ty_llong
                                          : ty_ullong;
                break;
        }
    }
    return ty;
}

Type *infer_chartype(Token *tok) {
    uint32_t prefix = tok->enc_prefix;
    return prefix == PREFIX_NONE ? ty_int
           : prefix == PREFIX_L  ? ty_int
           : prefix == PREFIX_U  ? ty_uint
           : prefix == PREFIX_u  ? ty_ushort
                                 : ty_uchar;
}

Type *infer_strtype(Token *tok) {
    uint32_t prefix = tok->enc_prefix;
    Type *ty = prefix == PREFIX_NONE ? ty_char
               : prefix == PREFIX_L  ? ty_int
               : prefix == PREFIX_U  ? ty_uint
               : prefix == PREFIX_u  ? ty_ushort
                                     : ty_uchar;
    uint32_t len = str_len(tok->id) / ty->size;
    return array_of(ty, len + 1);
}
