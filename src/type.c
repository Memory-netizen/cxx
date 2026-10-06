#include "cxx.h"

// Designated, like the targets' own TYPE macro: a positional initializer
// here would silently shift every field the day Type gains one.
#define TYPE(kind_, size_, align_, is_unsigned_) \
    &(Type) { .kind = kind_, .size = size_, .align = align_, .is_unsigned = is_unsigned_ }

Type *bitint[129][2] = {
    {NULL, NULL},
    // Signed _BitInt needs width >= 2, but unsigned _BitInt(1) is valid
    // (used by the uwb suffix for values 0..1).
    {NULL, TYPE(TY_BITINT | 1, 1, 1, true)},
    {TYPE(TY_BITINT | 2, 1, 1, false), TYPE(TY_BITINT | 2, 1, 1, true)},
    {TYPE(TY_BITINT | 3, 1, 1, false), TYPE(TY_BITINT | 3, 1, 1, true)},
    {TYPE(TY_BITINT | 4, 1, 1, false), TYPE(TY_BITINT | 4, 1, 1, true)},
    {TYPE(TY_BITINT | 5, 1, 1, false), TYPE(TY_BITINT | 5, 1, 1, true)},
    {TYPE(TY_BITINT | 6, 1, 1, false), TYPE(TY_BITINT | 6, 1, 1, true)},
    {TYPE(TY_BITINT | 7, 1, 1, false), TYPE(TY_BITINT | 7, 1, 1, true)},
    {TYPE(TY_BITINT | 8, 1, 1, false), TYPE(TY_BITINT | 8, 1, 1, true)},
    {TYPE(TY_BITINT | 9, 2, 2, false), TYPE(TY_BITINT | 9, 2, 2, true)},
    {TYPE(TY_BITINT | 10, 2, 2, false), TYPE(TY_BITINT | 10, 2, 2, true)},
    {TYPE(TY_BITINT | 11, 2, 2, false), TYPE(TY_BITINT | 11, 2, 2, true)},
    {TYPE(TY_BITINT | 12, 2, 2, false), TYPE(TY_BITINT | 12, 2, 2, true)},
    {TYPE(TY_BITINT | 13, 2, 2, false), TYPE(TY_BITINT | 13, 2, 2, true)},
    {TYPE(TY_BITINT | 14, 2, 2, false), TYPE(TY_BITINT | 14, 2, 2, true)},
    {TYPE(TY_BITINT | 15, 2, 2, false), TYPE(TY_BITINT | 15, 2, 2, true)},
    {TYPE(TY_BITINT | 16, 2, 2, false), TYPE(TY_BITINT | 16, 2, 2, true)},
    {TYPE(TY_BITINT | 17, 4, 4, false), TYPE(TY_BITINT | 17, 4, 4, true)},
    {TYPE(TY_BITINT | 18, 4, 4, false), TYPE(TY_BITINT | 18, 4, 4, true)},
    {TYPE(TY_BITINT | 19, 4, 4, false), TYPE(TY_BITINT | 19, 4, 4, true)},
    {TYPE(TY_BITINT | 20, 4, 4, false), TYPE(TY_BITINT | 20, 4, 4, true)},
    {TYPE(TY_BITINT | 21, 4, 4, false), TYPE(TY_BITINT | 21, 4, 4, true)},
    {TYPE(TY_BITINT | 22, 4, 4, false), TYPE(TY_BITINT | 22, 4, 4, true)},
    {TYPE(TY_BITINT | 23, 4, 4, false), TYPE(TY_BITINT | 23, 4, 4, true)},
    {TYPE(TY_BITINT | 24, 4, 4, false), TYPE(TY_BITINT | 24, 4, 4, true)},
    {TYPE(TY_BITINT | 25, 4, 4, false), TYPE(TY_BITINT | 25, 4, 4, true)},
    {TYPE(TY_BITINT | 26, 4, 4, false), TYPE(TY_BITINT | 26, 4, 4, true)},
    {TYPE(TY_BITINT | 27, 4, 4, false), TYPE(TY_BITINT | 27, 4, 4, true)},
    {TYPE(TY_BITINT | 28, 4, 4, false), TYPE(TY_BITINT | 28, 4, 4, true)},
    {TYPE(TY_BITINT | 29, 4, 4, false), TYPE(TY_BITINT | 29, 4, 4, true)},
    {TYPE(TY_BITINT | 30, 4, 4, false), TYPE(TY_BITINT | 30, 4, 4, true)},
    {TYPE(TY_BITINT | 31, 4, 4, false), TYPE(TY_BITINT | 31, 4, 4, true)},
    {TYPE(TY_BITINT | 32, 4, 4, false), TYPE(TY_BITINT | 32, 4, 4, true)},
    {TYPE(TY_BITINT | 33, 8, 8, false), TYPE(TY_BITINT | 33, 8, 8, true)},
    {TYPE(TY_BITINT | 34, 8, 8, false), TYPE(TY_BITINT | 34, 8, 8, true)},
    {TYPE(TY_BITINT | 35, 8, 8, false), TYPE(TY_BITINT | 35, 8, 8, true)},
    {TYPE(TY_BITINT | 36, 8, 8, false), TYPE(TY_BITINT | 36, 8, 8, true)},
    {TYPE(TY_BITINT | 37, 8, 8, false), TYPE(TY_BITINT | 37, 8, 8, true)},
    {TYPE(TY_BITINT | 38, 8, 8, false), TYPE(TY_BITINT | 38, 8, 8, true)},
    {TYPE(TY_BITINT | 39, 8, 8, false), TYPE(TY_BITINT | 39, 8, 8, true)},
    {TYPE(TY_BITINT | 40, 8, 8, false), TYPE(TY_BITINT | 40, 8, 8, true)},
    {TYPE(TY_BITINT | 41, 8, 8, false), TYPE(TY_BITINT | 41, 8, 8, true)},
    {TYPE(TY_BITINT | 42, 8, 8, false), TYPE(TY_BITINT | 42, 8, 8, true)},
    {TYPE(TY_BITINT | 43, 8, 8, false), TYPE(TY_BITINT | 43, 8, 8, true)},
    {TYPE(TY_BITINT | 44, 8, 8, false), TYPE(TY_BITINT | 44, 8, 8, true)},
    {TYPE(TY_BITINT | 45, 8, 8, false), TYPE(TY_BITINT | 45, 8, 8, true)},
    {TYPE(TY_BITINT | 46, 8, 8, false), TYPE(TY_BITINT | 46, 8, 8, true)},
    {TYPE(TY_BITINT | 47, 8, 8, false), TYPE(TY_BITINT | 47, 8, 8, true)},
    {TYPE(TY_BITINT | 48, 8, 8, false), TYPE(TY_BITINT | 48, 8, 8, true)},
    {TYPE(TY_BITINT | 49, 8, 8, false), TYPE(TY_BITINT | 49, 8, 8, true)},
    {TYPE(TY_BITINT | 50, 8, 8, false), TYPE(TY_BITINT | 50, 8, 8, true)},
    {TYPE(TY_BITINT | 51, 8, 8, false), TYPE(TY_BITINT | 51, 8, 8, true)},
    {TYPE(TY_BITINT | 52, 8, 8, false), TYPE(TY_BITINT | 52, 8, 8, true)},
    {TYPE(TY_BITINT | 53, 8, 8, false), TYPE(TY_BITINT | 53, 8, 8, true)},
    {TYPE(TY_BITINT | 54, 8, 8, false), TYPE(TY_BITINT | 54, 8, 8, true)},
    {TYPE(TY_BITINT | 55, 8, 8, false), TYPE(TY_BITINT | 55, 8, 8, true)},
    {TYPE(TY_BITINT | 56, 8, 8, false), TYPE(TY_BITINT | 56, 8, 8, true)},
    {TYPE(TY_BITINT | 57, 8, 8, false), TYPE(TY_BITINT | 57, 8, 8, true)},
    {TYPE(TY_BITINT | 58, 8, 8, false), TYPE(TY_BITINT | 58, 8, 8, true)},
    {TYPE(TY_BITINT | 59, 8, 8, false), TYPE(TY_BITINT | 59, 8, 8, true)},
    {TYPE(TY_BITINT | 60, 8, 8, false), TYPE(TY_BITINT | 60, 8, 8, true)},
    {TYPE(TY_BITINT | 61, 8, 8, false), TYPE(TY_BITINT | 61, 8, 8, true)},
    {TYPE(TY_BITINT | 62, 8, 8, false), TYPE(TY_BITINT | 62, 8, 8, true)},
    {TYPE(TY_BITINT | 63, 8, 8, false), TYPE(TY_BITINT | 63, 8, 8, true)},
    {TYPE(TY_BITINT | 64, 8, 8, false), TYPE(TY_BITINT | 64, 8, 8, true)},
    {TYPE(TY_BITINT | 65, 16, 8, false), TYPE(TY_BITINT | 65, 16, 8, true)},
    {TYPE(TY_BITINT | 66, 16, 8, false), TYPE(TY_BITINT | 66, 16, 8, true)},
    {TYPE(TY_BITINT | 67, 16, 8, false), TYPE(TY_BITINT | 67, 16, 8, true)},
    {TYPE(TY_BITINT | 68, 16, 8, false), TYPE(TY_BITINT | 68, 16, 8, true)},
    {TYPE(TY_BITINT | 69, 16, 8, false), TYPE(TY_BITINT | 69, 16, 8, true)},
    {TYPE(TY_BITINT | 70, 16, 8, false), TYPE(TY_BITINT | 70, 16, 8, true)},
    {TYPE(TY_BITINT | 71, 16, 8, false), TYPE(TY_BITINT | 71, 16, 8, true)},
    {TYPE(TY_BITINT | 72, 16, 8, false), TYPE(TY_BITINT | 72, 16, 8, true)},
    {TYPE(TY_BITINT | 73, 16, 8, false), TYPE(TY_BITINT | 73, 16, 8, true)},
    {TYPE(TY_BITINT | 74, 16, 8, false), TYPE(TY_BITINT | 74, 16, 8, true)},
    {TYPE(TY_BITINT | 75, 16, 8, false), TYPE(TY_BITINT | 75, 16, 8, true)},
    {TYPE(TY_BITINT | 76, 16, 8, false), TYPE(TY_BITINT | 76, 16, 8, true)},
    {TYPE(TY_BITINT | 77, 16, 8, false), TYPE(TY_BITINT | 77, 16, 8, true)},
    {TYPE(TY_BITINT | 78, 16, 8, false), TYPE(TY_BITINT | 78, 16, 8, true)},
    {TYPE(TY_BITINT | 79, 16, 8, false), TYPE(TY_BITINT | 79, 16, 8, true)},
    {TYPE(TY_BITINT | 80, 16, 8, false), TYPE(TY_BITINT | 80, 16, 8, true)},
    {TYPE(TY_BITINT | 81, 16, 8, false), TYPE(TY_BITINT | 81, 16, 8, true)},
    {TYPE(TY_BITINT | 82, 16, 8, false), TYPE(TY_BITINT | 82, 16, 8, true)},
    {TYPE(TY_BITINT | 83, 16, 8, false), TYPE(TY_BITINT | 83, 16, 8, true)},
    {TYPE(TY_BITINT | 84, 16, 8, false), TYPE(TY_BITINT | 84, 16, 8, true)},
    {TYPE(TY_BITINT | 85, 16, 8, false), TYPE(TY_BITINT | 85, 16, 8, true)},
    {TYPE(TY_BITINT | 86, 16, 8, false), TYPE(TY_BITINT | 86, 16, 8, true)},
    {TYPE(TY_BITINT | 87, 16, 8, false), TYPE(TY_BITINT | 87, 16, 8, true)},
    {TYPE(TY_BITINT | 88, 16, 8, false), TYPE(TY_BITINT | 88, 16, 8, true)},
    {TYPE(TY_BITINT | 89, 16, 8, false), TYPE(TY_BITINT | 89, 16, 8, true)},
    {TYPE(TY_BITINT | 90, 16, 8, false), TYPE(TY_BITINT | 90, 16, 8, true)},
    {TYPE(TY_BITINT | 91, 16, 8, false), TYPE(TY_BITINT | 91, 16, 8, true)},
    {TYPE(TY_BITINT | 92, 16, 8, false), TYPE(TY_BITINT | 92, 16, 8, true)},
    {TYPE(TY_BITINT | 93, 16, 8, false), TYPE(TY_BITINT | 93, 16, 8, true)},
    {TYPE(TY_BITINT | 94, 16, 8, false), TYPE(TY_BITINT | 94, 16, 8, true)},
    {TYPE(TY_BITINT | 95, 16, 8, false), TYPE(TY_BITINT | 95, 16, 8, true)},
    {TYPE(TY_BITINT | 96, 16, 8, false), TYPE(TY_BITINT | 96, 16, 8, true)},
    {TYPE(TY_BITINT | 97, 16, 8, false), TYPE(TY_BITINT | 97, 16, 8, true)},
    {TYPE(TY_BITINT | 98, 16, 8, false), TYPE(TY_BITINT | 98, 16, 8, true)},
    {TYPE(TY_BITINT | 99, 16, 8, false), TYPE(TY_BITINT | 99, 16, 8, true)},
    {TYPE(TY_BITINT | 100, 16, 8, false), TYPE(TY_BITINT | 100, 16, 8, true)},
    {TYPE(TY_BITINT | 101, 16, 8, false), TYPE(TY_BITINT | 101, 16, 8, true)},
    {TYPE(TY_BITINT | 102, 16, 8, false), TYPE(TY_BITINT | 102, 16, 8, true)},
    {TYPE(TY_BITINT | 103, 16, 8, false), TYPE(TY_BITINT | 103, 16, 8, true)},
    {TYPE(TY_BITINT | 104, 16, 8, false), TYPE(TY_BITINT | 104, 16, 8, true)},
    {TYPE(TY_BITINT | 105, 16, 8, false), TYPE(TY_BITINT | 105, 16, 8, true)},
    {TYPE(TY_BITINT | 106, 16, 8, false), TYPE(TY_BITINT | 106, 16, 8, true)},
    {TYPE(TY_BITINT | 107, 16, 8, false), TYPE(TY_BITINT | 107, 16, 8, true)},
    {TYPE(TY_BITINT | 108, 16, 8, false), TYPE(TY_BITINT | 108, 16, 8, true)},
    {TYPE(TY_BITINT | 109, 16, 8, false), TYPE(TY_BITINT | 109, 16, 8, true)},
    {TYPE(TY_BITINT | 110, 16, 8, false), TYPE(TY_BITINT | 110, 16, 8, true)},
    {TYPE(TY_BITINT | 111, 16, 8, false), TYPE(TY_BITINT | 111, 16, 8, true)},
    {TYPE(TY_BITINT | 112, 16, 8, false), TYPE(TY_BITINT | 112, 16, 8, true)},
    {TYPE(TY_BITINT | 113, 16, 8, false), TYPE(TY_BITINT | 113, 16, 8, true)},
    {TYPE(TY_BITINT | 114, 16, 8, false), TYPE(TY_BITINT | 114, 16, 8, true)},
    {TYPE(TY_BITINT | 115, 16, 8, false), TYPE(TY_BITINT | 115, 16, 8, true)},
    {TYPE(TY_BITINT | 116, 16, 8, false), TYPE(TY_BITINT | 116, 16, 8, true)},
    {TYPE(TY_BITINT | 117, 16, 8, false), TYPE(TY_BITINT | 117, 16, 8, true)},
    {TYPE(TY_BITINT | 118, 16, 8, false), TYPE(TY_BITINT | 118, 16, 8, true)},
    {TYPE(TY_BITINT | 119, 16, 8, false), TYPE(TY_BITINT | 119, 16, 8, true)},
    {TYPE(TY_BITINT | 120, 16, 8, false), TYPE(TY_BITINT | 120, 16, 8, true)},
    {TYPE(TY_BITINT | 121, 16, 8, false), TYPE(TY_BITINT | 121, 16, 8, true)},
    {TYPE(TY_BITINT | 122, 16, 8, false), TYPE(TY_BITINT | 122, 16, 8, true)},
    {TYPE(TY_BITINT | 123, 16, 8, false), TYPE(TY_BITINT | 123, 16, 8, true)},
    {TYPE(TY_BITINT | 124, 16, 8, false), TYPE(TY_BITINT | 124, 16, 8, true)},
    {TYPE(TY_BITINT | 125, 16, 8, false), TYPE(TY_BITINT | 125, 16, 8, true)},
    {TYPE(TY_BITINT | 126, 16, 8, false), TYPE(TY_BITINT | 126, 16, 8, true)},
    {TYPE(TY_BITINT | 127, 16, 8, false), TYPE(TY_BITINT | 127, 16, 8, true)},
    {TYPE(TY_BITINT | 128, 16, 8, false), TYPE(TY_BITINT | 128, 16, 8, true)},
};

Type *f16 = TYPE(TY_F16, 2, 2, false);
Type *f32 = TYPE(TY_F32, 4, 4, false);
Type *f64 = TYPE(TY_F64, 8, 8, false);
Type *f128 = TYPE(TY_F128, 16, 16, false);

#undef TYPE

bool is_bool(Type *ty) { return ty->kind == TY_BOOL; }
bool is_void(Type *ty) { return ty->kind == TY_VOID; }
bool is_char(Type *ty) { return ty->kind == TY_CHAR || ty->kind == TY_UCHAR || ty->kind == TY_SCHAR; }
bool is_obj(Type *ty) { return ty->kind != TY_VOID && ty->kind != TY_FUNC; }

bool is_objptr(Type *ty) {
    if (ty->kind != TY_PTR) return false;
    return is_obj(ty->base);
}

bool is_complete(Type *ty) { return ty->size > 0; }

bool is_complete_objptr(Type *ty) { return is_objptr(ty) && is_complete(ty->base); }

bool is_voidptr(Type *ty) {
    if (ty->kind != TY_PTR) return false;
    return is_void(ty->base);
}

bool is_funcptr(Type *ty) {
    if (ty->kind != TY_PTR) return false;
    return ty->base->kind == TY_FUNC;
}

bool is_integer(Type *ty) {
    return ty->kind == TY_BOOL || ty->kind == TY_CHAR || ty->kind == TY_SCHAR || ty->kind == TY_UCHAR ||
           ty->kind == TY_SHORT || ty->kind == TY_INT || ty->kind == TY_LONG || ty->kind == TY_LLONG ||
           ty->kind == TY_ENUM || ty->kind & TY_BITINT;
}

bool is_flonum(Type *ty) {
    return ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE || ty->kind == TY_F16 ||
           ty->kind == TY_F32 || ty->kind == TY_F64 || ty->kind == TY_F128;
}

char *ext_attr(Type *ty) {
    if (!is_integer(ty)) return NULL;
    // The IR names a _BitInt by its declared width, so that is the width the
    // rule is about; for every other integer the printed type is the storage.
    bool bitint = (ty->kind & TY_BITINT) != 0;
    // A _BitInt is named by its declared width and every other integer by its
    // storage, but both are the width the printed type says.
    int bits = bitint ? bitint_width(ty) : ty->size * 8;
    // Only the targets that mark every _BitInt reach the width test for one
    // wider than XLEN; everywhere else a width at or above it goes unmarked.
    if (!(bitint && T.ext_bitint) && bits >= T.ext_bits) return NULL;
    bool sign = !ty->is_unsigned || (bits == 32 && 32 < T.ext_bits);
    return sign ? "signext" : "zeroext";
}

bool is_fp_leaf(Type *ty) {
    if (!is_flonum(ty)) return false;
    // An x87 long double is passed in memory, not in a register file, however
    // wide it is.
    if (ty->kind == TY_LDOUBLE && T.ldouble_is_fp80) return false;
    return ty->size * 8 <= T.fp_reg_bits;
}

// The IEC 60559 interchange types (_Float16/32/64/128) — distinct,
// incompatible types (C23 6.2.5). Excludes long double.
bool is_interchange(Type *ty) {
    return ty->kind == TY_F16 || ty->kind == TY_F32 || ty->kind == TY_F64 || ty->kind == TY_F128;
}

// The interchange types plus long double (whose format is
// target-dependent: x87 80-bit on amd64, binary128 elsewhere). These
// take the fp128 library paths (CBits128 codegen, Fp128 folding);
// classic float/double also store constants in node->fpval but are
// emitted as 16-digit double bit patterns and folded in the double
// domain.
bool is_fpval(Type *ty) { return is_interchange(ty) || ty->kind == TY_LDOUBLE; }

FpFormat fmt_of(Type *ty) {
    switch (ty->kind) {
        case TY_F16:
            return FP16;
        case TY_F32:
        case TY_FLOAT:
            return FP32;
        case TY_F64:
        case TY_DOUBLE:
            return FP64;
        case TY_LDOUBLE:
            return T.ldouble_is_fp80 ? FP80 : FP128;
        default:
            return FP128;
    }
}

int bitint_width(Type *ty) { return ty->kind & 0xFFF; }

void bitint_align_wide(int align) {
    for (int w = 65; w <= 128; w++) {
        bitint[w][0]->align = align;
        bitint[w][1]->align = align;
    }
}

bool is_bitint128(Type *ty) { return (ty->kind & TY_BITINT) && bitint_width(ty) > 64; }

// Truncate or sign-extend a 64-bit value to the given width.
int64_t norm_bits(int64_t v, int width, bool is_unsigned) {
    if (width >= 64) return v;
    uint64_t mask = (1ULL << width) - 1;
    uint64_t u = (uint64_t)v & mask;
    if (!is_unsigned && (u >> (width - 1))) u |= ~mask;
    return (int64_t)u;
}

bool is_arith(Type *ty) { return is_integer(ty) || is_flonum(ty); }

bool is_pointer(Type *ty) { return ty->kind == TY_PTR; }

bool is_nullptr(Type *ty) { return ty->kind == TY_NULLPTR; }

bool is_null_constant(Node *node) {
    // The full 128-bit value must be zero: a _BitInt(>64) whose low 64
    // bits happen to be zero (e.g. 2^64) is not a null pointer constant.
    if (node->kind == ND_NUM && int128_is_zero(node->ival) && is_integer(node->ty)) return true;
    if (node->kind == ND_NULLPTR) return true;
    if (node->kind == ND_EXCAST && is_voidptr(node->ty) && is_null_constant(node->lhs)) return true;
    return false;
}

bool is_scalar(Type *ty) { return is_arith(ty) || is_pointer(ty) || is_nullptr(ty); }

bool is_record(Type *ty) { return ty->kind == TY_STRUCT || ty->kind == TY_UNION; }

bool is_array(Type *ty) { return ty->kind == TY_ARRAY || ty->kind == TY_VLA; }

static void copy_struct_type(Type *dst, Type *src) {
    Member dummy = {};
    Member *cur = &dummy;
    for (Member *mem = src->members; mem; mem = mem->next) {
        Member *new = emalloc(sizeof(Member));
        *new = *mem;
        cur = cur->next = new;
    }
    dst->members = dummy.next;
}

Type *copy_type(Type *ty) {
    Type *ret = emalloc(sizeof(Type));
    *ret = *ty;
    // A copy is a fresh node, so it starts out unlinked: the source's next
    // belongs to whichever list that type is already on (the module's type
    // list, or a chain of parameters), and carrying it over would splice
    // this copy into that list.
    ret->next = NULL;
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) copy_struct_type(ret, ty);
    ret->origin = ty->origin ? ty->origin : ty;
    return ret;
}

Type *pointer_to(Type *base, uint32_t qual) {
    Type *ty = emalloc(sizeof(Type));
    ty->kind = TY_PTR;
    ty->qual = qual;
    ty->size = T.ty_voidptr->size;
    ty->align = T.ty_voidptr->align;
    ty->is_unsigned = true;
    ty->base = base;
    return ty;
}

Type *func_type(Type *return_ty) {
    Type *ty = emalloc(sizeof(Type));
    ty->kind = TY_FUNC;
    // The C spec disallows sizeof(<function type>) and
    // _Alignof(<function type>), but
    // GCC allows them.
    // sizeof(<function type>) is evaluated to 1.
    // _Alignof(<function type>) is evaluated to 4.
    ty->size = 1;
    ty->align = 4;
    ty->ret = return_ty;
    return ty;
}

// The IR type that carries a lowered aggregate: the single piece itself when
// there is one (so a lone floating piece reaches the SIMD registers and a
// lone integer one the general-purpose registers), an array of one repeated
// element type when the ABI says so (AAPCS64 homogeneous floating-point
// aggregates), or a record of the pieces otherwise. Types built here are
// never published: they are spelled inline wherever they appear.
// How many parameters one lowered aggregate becomes. It has to follow the
// shape the target chose or the two sides of a call disagree: a record of the
// pieces is one parameter per piece, while an array or a bare piece is a
// single parameter carrying the whole value.
// The shape an aggregate takes as a *parameter*, which is not always the
// shape it takes as a return value: AAPCS64 hands every non-homogeneous
// composite over in whole eight-byte registers, so struct { char } arrives as
// an i64, while the same type is still returned as an i8. The classifier
// keeps the exact widths; the widening belongs to the parameter spelling.
Type *agg_param_shape_type(Type *ty, AggClass *c) {
    (void)ty;
    if (!c || c->npiece <= 0) return NULL;
    if (T.agg_full_regs && !c->is_hfa) {
        AggClass w = *c;
        for (int i = 0; i < w.npiece; i++) {
            w.piece[i].ty = T.ty_long;
            w.piece[i].size = 8;
        }
        return agg_shape_type(&w);
    }
    return agg_shape_type(c);
}

bool agg_is_per_piece(AggClass *c) { return c && c->npiece > 1 && !c->shape_array; }

int agg_param_slots(Type *ty, AggClass *c) {
    if (!c || c->npiece == 0) return 1;
    // An array shape is always a single parameter, however many elements it
    // has; a record of the pieces is one parameter per piece.
    if (c->shape_array) return 1;
    (void)ty;
    return c->npiece;
}

Type *agg_shape_type(AggClass *c) {
    if (c->npiece <= 0) return NULL;
    // A lone piece travels as itself unless the ABI still calls it an array
    // (AAPCS64 spells struct { float } as [1 x float], not as a float).
    if (c->npiece == 1 && !(c->shape_array && c->is_hfa && T.agg_always_array)) return c->piece[0].ty;
    if (c->shape_array) {
        Type *ty = array_of(c->piece[0].ty, c->npiece);
        ty->uid = 0;
        return ty;
    }
    Type *ty = emalloc(sizeof(Type));
    ty->kind = TY_STRUCT;
    ty->size = 0;
    ty->align = 8;
    Member *tail = NULL;
    for (int i = 0; i < c->npiece; i++) {
        Member *m = emalloc(sizeof(Member));
        m->ty = c->piece[i].ty;
        m->offset = c->piece[i].off;
        m->align = m->ty->align;
        m->idx = i;
        if (tail)
            tail = tail->next = m;
        else
            ty->members = tail = m;
        if (m->offset + m->ty->size > ty->size) ty->size = m->offset + m->ty->size;
    }
    ty->size = (ty->size + ty->align - 1) / ty->align * ty->align;
    ty->uid = 0;
    return ty;
}

Type *array_of(Type *base, int len) {
    Type *ty = emalloc(sizeof(Type));
    ty->kind = TY_ARRAY;
    ty->size = base->size * len;
    ty->align = base->align;
    ty->base = base;
    ty->len = len;
    return ty;
}

Type *vla_of(Type *base, Node *len) {
    Type *ty = emalloc(sizeof(Type));
    ty->kind = TY_VLA;
    ty->size = -1;
    ty->align = base->align;
    ty->base = base;
    ty->vla_len = len;
    return ty;
}

Type *struct_type(bool is_union) {
    Type *ty = emalloc(sizeof(Type));
    ty->kind = is_union ? TY_UNION : TY_STRUCT;
    ty->align = 1;
    return ty;
}

Type *enum_type(void) {
    Type *ty = emalloc(sizeof(Type));
    ty->kind = TY_ENUM;
    ty->size = 4;
    ty->align = 4;
    return ty;
}

void enum_set_underlying(Type *ty, EnumVal *vals) {
    Int128 zero = int128_set_i(0);
    Int128 lo = zero, hi = zero;
    for (EnumVal *v = vals; v; v = v->next) {
        if (int128_cmp_signed(v->val, lo) < 0) lo = v->val;
        if (int128_cmp_signed(v->val, hi) > 0) hi = v->val;
    }
    bool negative = int128_cmp_signed(lo, zero) < 0;
    ty->size = 4;
    ty->align = 4;
    // clang asks how many bits the positive values need and picks a signed
    // type only when an enumerator is actually negative; an enum whose values
    // are all non-negative gets an unsigned type however few bits it needs.
    // That is why 0x7fffffffffffffffL makes an unsigned long.
    if (int128_fits(hi, 32, negative ? SIGNED : UNSIGNED) && (!negative || int128_fits(lo, 32, SIGNED))) {
        ty->is_unsigned = !negative && !int128_fits(hi, 32, SIGNED);
        return;
    }
    // Nothing narrower holds it. The 64-bit type is long where long is 64
    // bits and long long where it is not; the size and alignment are the same
    // either way, and those are what a layout sees.
    ty->size = 8;
    ty->align = T.ty_long->size >= 8 ? 8 : T.ty_llong->align;
    ty->is_unsigned = !negative;
}

Type *type_qual(Type *ty, uint32_t qual) {
    if ((ty->qual & qual) == qual) return ty;
    ty = copy_type(ty);
    ty->qual |= qual;
    return ty;
}

// Qualify the element type of an array (rebuilding the array chain);
// plain types are qualified directly.
static Type *array_elem_qual(Type *ty, uint32_t qual) {
    if (ty->kind != TY_ARRAY && ty->kind != TY_VLA) return type_qual(ty, qual);
    Type *copy = copy_type(ty);
    copy->base = array_elem_qual(ty->base, qual);
    return copy;
}

Type *type_unqual(Type *ty) {
    if (ty->qual == 0) return ty;
    ty = copy_type(ty);
    ty->qual = 0;
    return ty;
}

static struct {
    Type *t1;
    Type *t2;
} cmpset[64];
static int depth;

static void push_cmp(Type *t1, Type *t2) {
    cmpset[depth].t1 = t1;
    cmpset[depth++].t2 = t2;
}

static void pop_cmp() { depth--; }

static bool check_set(Type *t1, Type *t2) {
    for (int i = 0; i < depth; i++)
        if (t1 == cmpset[i].t1 && t2 == cmpset[i].t2) return true;
    return false;
}

bool is_compatible(Type *t1, Type *t2) {
    if (t1 == t2) return true;

    // C23 6.2.5: each interchange floating type (_FloatN) is not
    // compatible with any other type, even one with the same format;
    // _BitInt(N) is likewise distinct from the standard integer types.
    if (t1->kind != t2->kind) {
        if (t1->kind != TY_VLA && t1->kind != TY_ARRAY) return false;
        if (t2->kind != TY_VLA && t2->kind != TY_ARRAY) return false;
    }
    if (t1->qual != t2->qual) return false;

    if (t1->origin) t1 = t1->origin;
    if (t2->origin) t2 = t2->origin;

    if (t1 == t2) return true;

    if (check_set(t1, t2)) return true;

    // _BitInt widths are encoded in the kind, so equal kinds already imply
    // equal widths; only signedness remains.
    if (t1->kind & TY_BITINT) return t1->is_unsigned == t2->is_unsigned;

    switch (t1->kind) {
        case TY_SHORT:
        case TY_INT:
        case TY_LONG:
        case TY_LLONG:
            return t1->is_unsigned == t2->is_unsigned;
        case TY_NULLPTR:
        case TY_VOID:
        case TY_BOOL:
        case TY_CHAR:
        case TY_SCHAR:
        case TY_UCHAR:
        case TY_FLOAT:
        case TY_DOUBLE:
        case TY_LDOUBLE:
        case TY_BITINT:
        case TY_F16:
        case TY_F32:
        case TY_F64:
        case TY_F128:
            return true;
        case TY_PTR:
            return is_compatible(t1->base, t2->base);
        case TY_FUNC:
            if (!is_compatible(t1->ret, t2->ret)) return false;
            if (t1->is_variadic != t2->is_variadic) return false;

            Type *p1 = t1->params;
            Type *p2 = t2->params;
            for (; p1 && p2; p1 = p1->next, p2 = p2->next)
                if (!is_compatible(p1, p2)) return false;
            return p1 == NULL && p2 == NULL;
        case TY_VLA:
        case TY_ARRAY:
            if (!is_compatible(t1->base, t2->base)) return false;
            // A variable length array keeps its length as the expression that
            // computes it, and that pointer shares its storage with `len`:
            // reading `len` here would compare the low half of an address and
            // answer differently from one run to the next. A variable length
            // array therefore matches any length -- which is what gcc and
            // clang accept for `int (*)[3]` against `&vla`.
            if (t1->kind == TY_VLA || t2->kind == TY_VLA) return true;
            return t1->len < 0 || t2->len < 0 || t1->len == t2->len;
        case TY_STRUCT:
        case TY_UNION:
            if (t1->is_anon || t2->is_anon) return false;
            if (t1->id != t2->id) return false;
            Member *m1 = t1->members;
            Member *m2 = t2->members;
            push_cmp(t1, t2);
            for (; m1 && m2; m1 = m1->next, m2 = m2->next) {
                if (m1->name->id != m2->name->id) return false;
                if (m1->is_align != m2->is_align) return false;
                if (m1->align != m2->align) return false;
                if (!is_compatible(m1->ty, m2->ty)) return false;
            }
            pop_cmp();
            return m1 == NULL && m2 == NULL;
        case TY_ENUM:
            if (t1->is_anon || t2->is_anon) return false;
            if (t1->id != t2->id) return false;
            EnumVal *enm1 = t1->enumvals;
            EnumVal *enm2 = t2->enumvals;
            for (; enm1 && enm2; enm1 = enm1->next, enm2 = enm2->next) {
                if (enm1->name->id != enm2->name->id) return false;
                if (int128_cmp_signed(enm1->val, enm2->val) != 0) return false;
            }
            return enm1 == NULL && enm2 == NULL;
        case TY_NONE:
            return false;
    }
    return false;
}

void add_type(Node *node);

void check_unop(Node *node) {
    add_type(node->lhs);
    Type *lhs = node->lhs->ty;
    switch (node->kind) {
        case ND_PLUS:
        case ND_NEG:
            if (is_arith(lhs)) return;
            break;
        case ND_INVERT:
            if (is_integer(lhs)) return;
            break;
        case ND_NOT:
            if (is_scalar(lhs)) return;
            break;
        case ND_PREINC:
        case ND_PREDEC:
        case ND_POSTINC:
        case ND_POSTDEC:
            if (is_arith(lhs) || is_pointer(lhs)) return;
            break;
        default:
            return;
    }
    error(node->tok, "wrong type argument to unary ‘%.*s’", node->tok->len, tok_text(node->tok));
}

void check_binop(Node *node) {
    add_type(node->lhs);
    add_type(node->rhs);
    Type *lhs = node->lhs->ty;
    Type *rhs = node->rhs->ty;
    switch (node->kind) {
        case ND_ADD:
        case ND_ADDAS:
        case ND_SUB:
        case ND_SUBAS:
        case ND_MUL:
        case ND_DIV:
        case ND_MULAS:
        case ND_DIVAS:
            if (is_arith(lhs) && is_arith(rhs)) return;
            break;
        case ND_MOD:
        case ND_MODAS:
        case ND_BAND:
        case ND_BOR:
        case ND_XOR:
        case ND_ANDAS:
        case ND_ORAS:
        case ND_XORAS:
        case ND_LEFT:
        case ND_RIGHT:
        case ND_LEFTAS:
        case ND_RIGHTAS:
            if (is_integer(lhs) && is_integer(rhs)) return;
            break;
        case ND_LT:
        case ND_LE:
        case ND_GT:
        case ND_GE:
            if (is_arith(lhs) && is_arith(rhs)) return;
            if (is_pointer(lhs) && is_pointer(rhs))
                if (is_compatible(type_unqual(lhs->base), type_unqual(rhs->base))) return;
            //-----
            if (is_pointer(lhs) && is_pointer(rhs)) return;
            if (is_integer(lhs) && is_pointer(rhs)) return;
            if (is_pointer(lhs) && is_integer(rhs)) return;
            break;
        case ND_EQ:
        case ND_NE:
            if (is_arith(lhs) && is_arith(rhs)) return;
            if (is_pointer(lhs) && is_pointer(rhs))
                if (is_compatible(type_unqual(lhs->base), type_unqual(rhs->base))) return;

            if (is_objptr(lhs) && is_voidptr(rhs)) return;
            if (is_objptr(rhs) && is_voidptr(lhs)) return;

            if (is_nullptr(lhs) && is_nullptr(rhs)) return;

            if (is_nullptr(lhs) && is_null_constant(node->rhs)) return;
            if (is_nullptr(rhs) && is_null_constant(node->lhs)) return;

            if (is_pointer(lhs) && (is_null_constant(node->rhs) || is_nullptr(rhs))) return;
            if (is_pointer(rhs) && (is_null_constant(node->lhs) || is_nullptr(lhs))) return;

            // -----
            if (is_pointer(lhs) && is_pointer(rhs)) return;
            if (is_integer(lhs) && is_pointer(rhs)) return;
            if (is_pointer(lhs) && is_integer(rhs)) return;
            break;
        case ND_LOGAND:
        case ND_LOGOR:
            if (is_scalar(lhs) && is_scalar(rhs)) return;
            break;
        case ND_PTRADD:
        case ND_PTRAS:
            if (is_complete_objptr(lhs) && is_integer(rhs)) return;
            //---
            if (is_pointer(lhs) && is_integer(rhs)) return;
            break;
        default:
            return;
    }
    error(node->tok, "invalid operands to binary ‘%.*s’", node->tok->len, tok_text(node->tok));
}

void check_condop(Node *node) {
    add_type(node->cond);
    add_type(node->then);
    add_type(node->els);
    if (!is_scalar(node->cond->ty)) error(node->cond->tok, "scalar type is required in here");
    Type *lhs = node->then->ty;
    Type *rhs = node->els->ty;

    if (is_arith(lhs) && is_arith(rhs)) return;
    if (is_record(lhs) && is_compatible(lhs, rhs)) return;
    if (is_void(lhs) && is_void(rhs)) return;
    if (is_pointer(lhs) && is_pointer(rhs))
        if (is_compatible(type_unqual(lhs->base), type_unqual(rhs->base))) return;

    if (is_objptr(lhs) && is_voidptr(rhs)) return;
    if (is_objptr(rhs) && is_voidptr(lhs)) return;

    if (is_nullptr(lhs) && is_nullptr(rhs)) return;

    if (is_pointer(lhs) && (is_null_constant(node->els) || is_nullptr(rhs))) return;
    if (is_pointer(rhs) && (is_null_constant(node->then) || is_nullptr(lhs))) return;

    // ------
    if (is_nullptr(lhs) && is_null_constant(node->els)) return;
    if (is_nullptr(rhs) && is_null_constant(node->then)) return;

    if (is_pointer(lhs) && is_pointer(rhs)) return;
    if (is_integer(lhs) && is_pointer(rhs)) return;
    if (is_pointer(lhs) && is_integer(rhs)) return;
}

void check_asop(Type *dst, Node *src, int ctx) {
    static char *msg[] = {
        [CTX_AS] = "assigning",
        [CTX_RET] = "returning",
        [CTX_INIT] = "initializing",
        [CTX_CALL] = "passing argument",
    };
    add_type(src);
    Type *src_ty = src->ty;
    if (is_arith(dst) && is_arith(src_ty)) return;
    if (is_record(dst) && is_compatible(type_unqual(dst), type_unqual(src_ty))) return;
    if (is_pointer(dst) && is_pointer(src_ty) && is_compatible(type_unqual(dst->base), type_unqual(src_ty->base)))
        if (BIT_SUPERSET(dst->base->qual, src_ty->base->qual)) return;

    if (is_objptr(dst) && is_voidptr(src_ty))
        if (BIT_SUPERSET(dst->base->qual, src_ty->base->qual)) return;
    if (is_objptr(src_ty) && is_voidptr(dst))
        if (BIT_SUPERSET(dst->base->qual, src_ty->base->qual)) return;

    if (is_nullptr(dst) && is_null_constant(src)) return;
    if (is_nullptr(dst) && is_nullptr(src_ty)) return;
    if (is_pointer(dst) && (is_null_constant(src) || is_nullptr(src_ty))) return;

    if (is_bool(dst) && (is_pointer(src_ty) || is_nullptr(src_ty))) return;
    error(src->tok, "incompatible types when %s", msg[ctx]);
}

void modifiable_lvalue(Node *node) {
    add_type(node->lhs);
    Node *lhs = node->lhs;
    if (!lhs->is_lvalue || lhs->ty->kind == TY_FUNC)
        error(node->tok, "lvalue required as ‘%.*s’ operand", node->tok->len, tok_text(node->tok));
    if (lhs->ty->qual & Q_CONST || lhs->ty->qual & Q_MEMCONST) {
        if (lhs->kind == ND_VAR) {
            error(node->tok, "assignment of read-only variable ‘%s’", str(lhs->var->id));
        } else {
            uint32_t start = lhs->tok->loc;
            Token *cur = lhs->tok;
            while (cur->next != node->tok) cur = cur->next;
            error(node->tok, "assignment of read-only location ‘%.*s’", (int)(cur->loc - start + cur->len),
                  tok_text(lhs->tok));
        }
    }
    if (is_void(lhs->ty)) error(node->tok, "incomplete type ‘void’ is not assignable");
    if (lhs->kind == ND_IMCAST && lhs->lhs->ty->kind == TY_ARRAY)
        error(lhs->tok, "assignment to expression with array type");
}

void lvalue_convert(Node **expr) {
    if (!(*expr) || !(*expr)->is_lvalue) return;
    // A scalar constexpr variable read folds to the initializer's
    // constant value (C23 6.6): every scalar value load passes through
    // the lvalue conversion. Address-of and writes bypass it and keep
    // the real object; aggregates and other types deliberately fall
    // through to the normal load (no scalar constant to substitute).
    if ((*expr)->kind == ND_VAR && ((*expr)->var->sclass & SC_CONSTEXPR)) {
        int64_t v;
        uint32_t s = 0;
        if (constexpr_fold((*expr)->var, &v, &s) && !s && is_integer((*expr)->ty)) {
            Node *n = new_node(ND_NUM, (*expr)->tok);
            n->ival = int128_set_i(v);
            n->ty = (*expr)->ty;
            *expr = n;
            return;
        }
    }
    Node *node = new_unary(ND_LVTOR, (*expr), (*expr)->tok);
    node->ty = type_unqual((*expr)->ty);
    *expr = node;
}

void new_imcast(Node **expr, Type *ty) {
    if (is_compatible((*expr)->ty, ty)) return;
    Node *node = new_unary(ND_IMCAST, *expr, (*expr)->tok);
    node->ty = ty;
    *expr = node;
}

// Format rank of the value set, per C23 H.4.3: binary16 ⊂ binary32 ⊂
// binary64 ⊂ binary128 (the compiler models long double as binary128).
int float_rank(Type *ty) {
    switch (ty->kind) {
        case TY_F16:
            return 0;
        case TY_F32:
        case TY_FLOAT:
            return 1;
        case TY_F64:
        case TY_DOUBLE:
            return 2;
        case TY_LDOUBLE:
            // x87 80-bit sits between binary64 and binary128
            return T.ldouble_is_fp80 ? 3 : 4;
        case TY_F128:
            return 4;
        default:
            return -1;
    }
}

// Integer conversion rank by kind (not size).
static int int_rank(Type *ty) {
    switch (ty->kind) {
        case TY_LLONG:
            return 4;
        case TY_LONG:
            return 3;
        case TY_INT:
        case TY_ENUM:
            return 2;
        case TY_SHORT:
            return 1;
        default:
            return 0;
    }
}

// Integer promotions for _BitInt per C23 6.3.1.1.
static Type *promote_bitint(Type *ty) {
    int w = bitint_width(ty);
    if (ty->is_unsigned) {
        if (w <= 31) return T.ty_int;
        if (w == 32) return T.ty_uint;
    } else {
        if (w <= 32) return T.ty_int;
    }
    return ty;
}

// The integer-domain view of a type after the integer promotions:
// (bit width, signedness, resulting type). Small standard types
// promote to int; _BitInt promotes per 6.3.1.1 (signed N <= 32 -> int,
// unsigned N <= 31 -> int, N = 32 -> unsigned int); a wider _BitInt
// keeps its declared width.
typedef struct {
    int width;
    bool is_unsigned;
    Type *ty;
} IntSpec;

static IntSpec int_spec(Type *ty) {
    if (ty->kind & TY_BITINT) {
        Type *p = promote_bitint(ty);
        if (p != ty) return (IntSpec){p->size * 8, p->is_unsigned, p};
        return (IntSpec){bitint_width(ty), ty->is_unsigned, ty};
    }
    if (ty->size < 4) return (IntSpec){32, false, T.ty_int};
    return (IntSpec){ty->size * 8, ty->is_unsigned, ty};
}

static Type *get_common_type(Type *ty1, Type *ty2) {
    if (ty1->base) return pointer_to(ty1->base, 0);

    if (is_flonum(ty1) || is_flonum(ty2)) {
        int r1 = float_rank(ty1), r2 = float_rank(ty2);
        if (r1 != r2) return r1 < r2 ? ty2 : ty1;
        // Equivalent value sets (e.g. float and _Float32, both binary32):
        // an interchange floating type wins the tie (C23 H.4.3).
        if (is_interchange(ty1)) return ty1;
        if (is_interchange(ty2)) return ty2;
        switch (r1) {
            case 4:
            case 3:
                return T.ty_ldouble;
            case 2:
                return T.ty_double;
            default:
                return T.ty_float;
        }
    }

    // Two _BitInt operands stay in the _BitInt domain (C23 6.3.1.8):
    // the wider type wins, on equal width the unsigned one wins.
    // Same-type pairs therefore keep their wrapping arithmetic.
    // Compare by bit width, not byte size: _BitInt(3) and _BitInt(4)
    // both occupy 1 byte but are different types.
    if ((ty1->kind & TY_BITINT) && (ty2->kind & TY_BITINT)) {
        int w1 = bitint_width(ty1), w2 = bitint_width(ty2);
        if (w1 != w2) return w1 < w2 ? ty2 : ty1;
        return ty2->is_unsigned ? ty2 : ty1;
    }

    // Integer domain (clang-verified): reduce both sides to
    // (width, signedness) after the integer promotions and pick the
    // wider width. Equal widths: a standard type beats a _BitInt
    // (unsigned _BitInt(64) + long long -> unsigned long long);
    // standard pairs: unsigned wins, same-sign different kinds go to
    // the higher rank.
    IntSpec s1 = int_spec(ty1), s2 = int_spec(ty2);

    if (s1.width != s2.width) return s1.width < s2.width ? s2.ty : s1.ty;

    if ((s1.ty->kind & TY_BITINT) || (s2.ty->kind & TY_BITINT)) {
        Type *std = (s1.ty->kind & TY_BITINT) ? s2.ty : s1.ty;
        Type *b = (s1.ty->kind & TY_BITINT) ? s1.ty : s2.ty;
        if (b->is_unsigned && !std->is_unsigned)
            return std->kind == TY_LONG ? T.ty_ulong : std->kind == TY_INT ? T.ty_uint : T.ty_ullong;
        return std;
    }

    if (s1.ty->kind != s2.ty->kind) {
        // Same width, different kinds. An enum keeps its own type when
        // it ties with its underlying rank (clang: enum E + int stays
        // enum E). Otherwise 6.3.1.8: if the unsigned operand's rank is
        // >= the signed one's, the unsigned type wins; if not, the
        // signed type cannot represent all values of the unsigned one
        // at the same width, so both convert to the signed type's
        // unsigned counterpart (unsigned long + long long ->
        // unsigned long long).
        int k1 = int_rank(s1.ty), k2 = int_rank(s2.ty);
        if (k1 == k2) {
            // An enum acts as its unsigned-rank-2 underlying here
            // (clang): int + enum E -> enum E; uint + enum E -> uint.
            bool eu1 = s1.ty->is_unsigned || s1.ty->kind == TY_ENUM;
            bool eu2 = s2.ty->is_unsigned || s2.ty->kind == TY_ENUM;
            if (eu1 != eu2) return eu1 ? s1.ty : s2.ty;
            return s1.ty;
        }
        bool u1 = s1.ty->is_unsigned || s1.ty->kind == TY_ENUM;
        bool u2 = s2.ty->is_unsigned || s2.ty->kind == TY_ENUM;
        if (u1 != u2) {
            // An enum acts as its unsigned-rank-2 underlying here
            // (clang: enum E + long on ILP32 -> unsigned long).
            Type *u = u1 ? s1.ty : s2.ty;
            Type *sg = u1 ? s2.ty : s1.ty;
            if (int_rank(u) >= int_rank(sg)) return u;
            return sg->kind == TY_LONG ? T.ty_ulong : T.ty_ullong;
        }
        return k1 < k2 ? s2.ty : s1.ty;
    }

    if (s2.ty->is_unsigned) return s2.ty;
    return s1.ty;
}

void integer_promotion(Node **expr) {
    // _BitInt is never subject to the integer promotions (C23; clang keeps
    // _BitInt(3) in unary ops and varargs): only the mixed-operand usual
    // arithmetic conversions convert it.
    if ((*expr)->ty->kind & TY_BITINT) return;
    Type *ty = get_common_type((*expr)->ty, T.ty_int);
    new_imcast(expr, ty);
}

// -Wsign-compare: mixing signed and unsigned in a relational operator
// converts the signed operand to unsigned, which is how `-1 < 1u` becomes
// false. gcc and clang both keep this group out of the C defaults (it
// arrives with -Wextra there), so WG_SIGN_COMPARE is off unless asked for.
// The check runs after the operands' own types are known but before
// usual_arith_conv() rewrites them, which is what makes
// `unsigned char < int` silent: both sides promote to int first.
static void warn_sign_compare(Node *node) {
    // The four relational operators and the two equality ones: gcc's
    // -Wsign-compare covers all six (measured), and cxx types them along
    // this same path.
    if (node->kind != ND_LT && node->kind != ND_LE && node->kind != ND_GT && node->kind != ND_GE &&
        node->kind != ND_EQ && node->kind != ND_NE)
        return;
    if (!wg_enabled(WG_SIGN_COMPARE)) return;

    Node *l = node->lhs;
    Node *r = node->rhs;
    if (node->kind == ND_GT || node->kind == ND_GE) {
        Node *t = l;
        l = r;
        r = t;  // the message names the operands as they were written
    }
    if (!is_integer(l->ty) || !is_integer(r->ty)) return;
    if (is_bool(l->ty) || is_bool(r->ty)) return;

    // 6.3.1.1: the integer promotions come first, and they decide the
    // signedness the comparison actually uses.
    Type *lt = l->ty->size < 4 ? T.ty_int : l->ty;
    Type *rt = r->ty->size < 4 ? T.ty_int : r->ty;
    if (lt->is_unsigned == rt->is_unsigned) return;

    // A non-negative constant on the signed side fits the unsigned one, and
    // comparing against a literal zero is the idiom the references leave
    // alone.
    Node *signed_side = lt->is_unsigned ? r : l;
    Node *unsigned_side = lt->is_unsigned ? l : r;
    if (signed_side->kind == ND_NUM && !int128_is_negative(signed_side->ival)) return;
    if (unsigned_side->kind == ND_NUM && int128_is_zero(unsigned_side->ival)) return;

    warning(WG_SIGN_COMPARE, node->tok,
            "comparison of integer expressions of different signedness: \u2018%s\u2019 and \u2018%s\u2019",
            diag_ty_name(lt), diag_ty_name(rt));
}

void usual_arith_conv(Node **lhs, Node **rhs) {
    Type *ty = get_common_type((*lhs)->ty, (*rhs)->ty);
    new_imcast(lhs, ty);
    new_imcast(rhs, ty);
}

void add_type(Node *node) {
    if (!node || node->ty) return;
    switch (node->kind) {
        case ND_NUM:
            node->ty = T.ty_int;
            break;
        case ND_NULLPTR:
            break;
        case ND_VAR:
            add_type(node->var_init);
            node->ty = node->var->ty;
            node->is_lvalue = true;
            break;
        case ND_SUBACCESS:
            add_type(node->lhs);
            add_type(node->rhs);
            lvalue_convert(&node->rhs);
            node->ty = node->lhs->ty->base;
            break;
            // The parser already fixed this node's type; type the operand
            // so irgen never walks into an untyped child.
            add_type(node->lhs);
            lvalue_convert(&node->lhs);
            break;

        // unary
        case ND_PLUS:
        case ND_NEG:
        case ND_INVERT:
            check_unop(node);
            lvalue_convert(&node->lhs);
            integer_promotion(&node->lhs);
            node->ty = node->lhs->ty;
            break;
        case ND_NOT:
            check_unop(node);
            lvalue_convert(&node->lhs);
            node->ty = T.ty_int;
            break;
        case ND_LOGOR:
        case ND_LOGAND:
            check_binop(node);
            lvalue_convert(&node->lhs);
            lvalue_convert(&node->rhs);
            node->ty = T.ty_int;
            break;
        case ND_ADDR:
            add_type(node->lhs);
            if (!node->lhs->is_lvalue) error(node->tok, "lvalue required as unary ‘&’ operand");
            if (node->lhs->kind == ND_VAR && node->lhs->var->sclass & SC_REG)
                error(node->tok, "address of register variable ‘%s’ requested", str(node->lhs->var->id));
            if (node->lhs->kind == ND_MEMBER && node->lhs->member->is_bitfield)
                error(node->tok, "cannot take address of bit-field ‘%s’", str(node->lhs->member->name->id));
            node->ty = pointer_to(node->lhs->ty, 0);
            break;
        case ND_DEREF:
            add_type(node->lhs);
            lvalue_convert(&node->lhs);
            if (!is_pointer(node->lhs->ty)) error(node->lhs->tok, "invalid type argument of unary ‘*’");
            node->ty = node->lhs->ty->base;
            node->is_lvalue = true;
            break;
        case ND_MEMBER:
            add_type(node->lhs);
            node->ty = node->member->ty;
            if (node->lhs->ty->qual & Q_CONST) {
                // const propagates from the aggregate object to the
                // member (6.5.2.3p4); array element qualification
                // lives on the innermost base type
                node->ty = array_elem_qual(node->ty, Q_CONST);
            }
            node->is_lvalue = node->lhs->is_lvalue;
            break;
        // binary
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_MOD:
        case ND_BOR:
        case ND_XOR:
        case ND_BAND:
            check_binop(node);
            lvalue_convert(&node->lhs);
            lvalue_convert(&node->rhs);
            usual_arith_conv(&node->lhs, &node->rhs);
            node->ty = node->lhs->ty;
            break;
        case ND_PTRADD:
            check_binop(node);
            lvalue_convert(&node->lhs);
            lvalue_convert(&node->rhs);
            new_imcast(&node->rhs, T.ty_long);
            node->ty = node->lhs->ty;
            break;
        case ND_LEFT:
        case ND_RIGHT:
            check_binop(node);
            lvalue_convert(&node->lhs);
            lvalue_convert(&node->rhs);
            // 6.5.7: each operand is promoted separately and the result has
            // the promoted left operand's type -- the usual arithmetic
            // conversions do not apply, so the two may differ in width, and
            // the front end keeps them that way. Widening the amount to a
            // common type is an LLVM requirement, so irgen does it.
            integer_promotion(&node->lhs);
            integer_promotion(&node->rhs);
            node->ty = node->lhs->ty;
            break;
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
        case ND_GT:
        case ND_GE:
            check_binop(node);
            lvalue_convert(&node->lhs);
            lvalue_convert(&node->rhs);
            warn_sign_compare(node);
            usual_arith_conv(&node->lhs, &node->rhs);
            node->ty = T.ty_int;
            break;
        case ND_AS:
            modifiable_lvalue(node);
            check_asop(node->lhs->ty, node->rhs, CTX_AS);
            if (!is_record(node->lhs->ty)) lvalue_convert(&node->rhs);
            new_imcast(&node->rhs, node->lhs->ty);
            node->ty = node->lhs->ty;
            break;
        case ND_INIT:
            add_type(node->lhs);
            check_asop(node->lhs->ty, node->rhs, CTX_INIT);
            node->lhs->ty = type_unqual(node->lhs->ty);
            if (!is_record(node->lhs->ty)) lvalue_convert(&node->rhs);
            new_imcast(&node->rhs, node->lhs->ty);
            node->ty = node->lhs->ty;
            break;
        case ND_PREINC:
        case ND_PREDEC:
        case ND_POSTINC:
        case ND_POSTDEC:
            check_unop(node);
            modifiable_lvalue(node);
            node->ty = node->lhs->ty;
            break;
        case ND_ADDAS:
        case ND_SUBAS: {
            add_type(node->lhs);
            bool is_ptr = is_pointer(node->lhs->ty);
            if (is_ptr) {
                if (node->kind == ND_SUBAS) node->rhs = new_unary(ND_NEG, node->rhs, node->rhs->tok);
                node->kind = ND_PTRAS;
            }
            check_binop(node);
            modifiable_lvalue(node);
            lvalue_convert(&node->rhs);
            Type *ty = get_common_type(node->lhs->ty, node->rhs->ty);
            new_imcast(&node->rhs, is_ptr ? T.ty_long : ty);
            node->compute_ty = ty;
            node->ty = node->lhs->ty;
            break;
        }
        case ND_MULAS:
        case ND_DIVAS:
        case ND_MODAS:
        case ND_ANDAS:
        case ND_ORAS:
        case ND_XORAS: {
            check_binop(node);
            modifiable_lvalue(node);
            lvalue_convert(&node->rhs);
            Type *ty = get_common_type(node->lhs->ty, node->rhs->ty);
            new_imcast(&node->rhs, ty);
            node->compute_ty = ty;
            node->ty = node->lhs->ty;
            break;
        }
        case ND_LEFTAS:
        case ND_RIGHTAS:
            check_binop(node);
            modifiable_lvalue(node);
            lvalue_convert(&node->rhs);
            integer_promotion(&node->rhs);
            node->compute_ty = get_common_type(node->lhs->ty, T.ty_int);
            node->ty = node->lhs->ty;
            break;
        case ND_COMMA:
            add_type(node->lhs);
            add_type(node->rhs);
            lvalue_convert(&node->lhs);
            lvalue_convert(&node->rhs);
            node->ty = node->rhs->ty;
            break;
        case ND_COND:
            check_condop(node);
            lvalue_convert(&node->cond);
            lvalue_convert(&node->then);
            lvalue_convert(&node->els);
            if (is_void(node->then->ty) || is_void(node->els->ty)) {
                node->ty = T.ty_void;
            } else {
                usual_arith_conv(&node->then, &node->els);
                node->ty = node->then->ty;
            }
            break;
        case ND_STMT_EXPR:
            if (node->body) {
                Node *stmt = node->body;
                while (stmt->next && stmt->next->kind != ND_SP_RESTORE) stmt = stmt->next;
                // a trailing label wraps the value expression
                if (stmt->kind == ND_LABEL || stmt->kind == ND_CASE) stmt = stmt->label_body;
                if (stmt->kind == ND_EXPR_STMT && stmt->lhs)
                    node->ty = stmt->lhs->ty;
                else
                    node->ty = T.ty_void;  // no value expression: void
            }
            break;
        case ND_MEMZERO:
        case ND_IMCAST:
        case ND_EXCAST:
        case ND_LVTOR:
            add_type(node->lhs);
            break;
        case ND_RETURN:
        case ND_EXPR_STMT:
            add_type(node->lhs);
            lvalue_convert(&node->lhs);
            if (node->lhs) node->ty = node->lhs->ty;
            break;
        case ND_LABEL:
        case ND_CASE:
            add_type(node->label_body);
            break;
        case ND_SWITCH:
            add_type(node->cond);
            if (!is_integer(node->cond->ty)) error(node->cond->tok, "switch quantity not an integer");
            lvalue_convert(&node->cond);
            integer_promotion(&node->cond);
            add_type(node->body);
            break;
        case ND_IF:
        case ND_WHILE:
        case ND_DO:
        case ND_FOR:
            add_type(node->init);
            add_type(node->cond);
            if (node->cond && !is_scalar(node->cond->ty)) error(node->cond->tok, "scalar type is required in here");
            add_type(node->then);
            add_type(node->els);
            lvalue_convert(&node->cond);
            break;
        case ND_DECL:
        case ND_COMP_STMT: {
            Type *ty = NULL;
            for (Node *n = node->body; n; n = n->next) {
                add_type(n);
                ty = n->ty;
            }
            node->ty = ty;
            break;
        }
        case ND_LABEL_VAL:
            node->ty = T.ty_voidptr;
            break;
        case ND_CAS:
            add_type(node->lhs);
            add_type(node->rhs);
            add_type(node->desired);
            lvalue_convert(&node->lhs);
            lvalue_convert(&node->rhs);
            lvalue_convert(&node->desired);
            node->ty = T.ty_bool;
            break;
        case ND_ATOMICRMW:
            add_type(node->lhs);
            add_type(node->desired);
            lvalue_convert(&node->lhs);
            lvalue_convert(&node->desired);
            node->ty = type_unqual(node->lhs->ty->base);
            break;
        // Variadic access. va_start/va_end have no value; va_arg yields the
        // requested type, which the parser already recorded.
        case ND_VA_START:
        case ND_VA_END:
            add_type(node->lhs);
            node->ty = T.ty_void;
            break;
        case ND_VA_COPY:
            add_type(node->lhs);
            add_type(node->rhs);
            node->ty = T.ty_void;
            break;
        case ND_VA_ARG:
            add_type(node->lhs);
            // node->ty was set from the type name in the parser.
            break;
        case ND_ALLOCA:
            add_type(node->lhs);
            lvalue_convert(&node->lhs);
            // The C type is void *, like the old declared prototype; the
            // alloca element type (base_ty ?: char) is only for the IR.
            node->ty = T.ty_voidptr;
            break;
        // An asm statement: an input is a value the template reads, an
        // output an object it writes, and an indirect operand of either kind
        // is an address. The operands are the only expressions it has.
        case ND_ASM:
            for (AsmOperand *op = node->asm_ops; op; op = op->next) {
                add_type(op->expr);
                // An operand with a memory constraint is the object's
                // address, and a bit-field has none: gcc and clang both
                // refuse it, and what the template would be handed is the
                // whole access unit rather than the field.
                if (op->is_indirect && op->expr->kind == ND_MEMBER && op->expr->member->is_bitfield)
                    error(op->expr->tok, "cannot take address of bit-field ‘%s’", str(op->expr->member->name->id));
                if (!op->is_output) {
                    // A memory operand names an object, so an array among them
                    // is the array itself and not the pointer it decayed to:
                    // the address handed over is the array's, and the type
                    // LLVM is given for it is the array type.
                    if (op->is_indirect && op->expr->kind == ND_IMCAST && op->expr->lhs->ty->kind == TY_ARRAY)
                        op->expr = op->expr->lhs;
                    // A register input is a value, which is what the lvalue
                    // conversion produces; an indirect one keeps the object,
                    // whose address is what travels.
                    if (!op->is_indirect) lvalue_convert(&op->expr);
                    continue;
                }
                if (!op->expr->is_lvalue || op->expr->ty->kind == TY_FUNC)
                    error(op->expr->tok, "lvalue required in ‘asm’ statement");
                if (op->expr->ty->qual & (Q_CONST | Q_MEMCONST)) {
                    if (op->expr->kind == ND_VAR)
                        error(op->expr->tok, "read-only variable ‘%s’ used as ‘asm’ output", str(op->expr->var->id));
                    error(op->expr->tok, "read-only location used as ‘asm’ output");
                }
            }
            node->ty = T.ty_void;
            break;

        // other
        case ND_NOP:
        case ND_GOTO:
        case ND_GOTO_EXPR:
        case ND_BREAK:
        case ND_CONTINUE:
        case ND_PTRAS:
        case ND_FUNCALL:
        case ND_SP_SAVE:
        case ND_SP_RESTORE:
        case ND_FENCE:
            // Nothing to do
            break;
    }
}
