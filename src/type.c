#include "cxx.h"

#define TYPE(kind, size, align, is_unsigned) \
    &(Type) { kind, 0, size, align, is_unsigned, 0, 0, NULL, NULL, NULL, NULL, {0} }

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
    {TYPE(TY_BITINT | 65, 16, 16, false), TYPE(TY_BITINT | 65, 16, 16, true)},
    {TYPE(TY_BITINT | 66, 16, 16, false), TYPE(TY_BITINT | 66, 16, 16, true)},
    {TYPE(TY_BITINT | 67, 16, 16, false), TYPE(TY_BITINT | 67, 16, 16, true)},
    {TYPE(TY_BITINT | 68, 16, 16, false), TYPE(TY_BITINT | 68, 16, 16, true)},
    {TYPE(TY_BITINT | 69, 16, 16, false), TYPE(TY_BITINT | 69, 16, 16, true)},
    {TYPE(TY_BITINT | 70, 16, 16, false), TYPE(TY_BITINT | 70, 16, 16, true)},
    {TYPE(TY_BITINT | 71, 16, 16, false), TYPE(TY_BITINT | 71, 16, 16, true)},
    {TYPE(TY_BITINT | 72, 16, 16, false), TYPE(TY_BITINT | 72, 16, 16, true)},
    {TYPE(TY_BITINT | 73, 16, 16, false), TYPE(TY_BITINT | 73, 16, 16, true)},
    {TYPE(TY_BITINT | 74, 16, 16, false), TYPE(TY_BITINT | 74, 16, 16, true)},
    {TYPE(TY_BITINT | 75, 16, 16, false), TYPE(TY_BITINT | 75, 16, 16, true)},
    {TYPE(TY_BITINT | 76, 16, 16, false), TYPE(TY_BITINT | 76, 16, 16, true)},
    {TYPE(TY_BITINT | 77, 16, 16, false), TYPE(TY_BITINT | 77, 16, 16, true)},
    {TYPE(TY_BITINT | 78, 16, 16, false), TYPE(TY_BITINT | 78, 16, 16, true)},
    {TYPE(TY_BITINT | 79, 16, 16, false), TYPE(TY_BITINT | 79, 16, 16, true)},
    {TYPE(TY_BITINT | 80, 16, 16, false), TYPE(TY_BITINT | 80, 16, 16, true)},
    {TYPE(TY_BITINT | 81, 16, 16, false), TYPE(TY_BITINT | 81, 16, 16, true)},
    {TYPE(TY_BITINT | 82, 16, 16, false), TYPE(TY_BITINT | 82, 16, 16, true)},
    {TYPE(TY_BITINT | 83, 16, 16, false), TYPE(TY_BITINT | 83, 16, 16, true)},
    {TYPE(TY_BITINT | 84, 16, 16, false), TYPE(TY_BITINT | 84, 16, 16, true)},
    {TYPE(TY_BITINT | 85, 16, 16, false), TYPE(TY_BITINT | 85, 16, 16, true)},
    {TYPE(TY_BITINT | 86, 16, 16, false), TYPE(TY_BITINT | 86, 16, 16, true)},
    {TYPE(TY_BITINT | 87, 16, 16, false), TYPE(TY_BITINT | 87, 16, 16, true)},
    {TYPE(TY_BITINT | 88, 16, 16, false), TYPE(TY_BITINT | 88, 16, 16, true)},
    {TYPE(TY_BITINT | 89, 16, 16, false), TYPE(TY_BITINT | 89, 16, 16, true)},
    {TYPE(TY_BITINT | 90, 16, 16, false), TYPE(TY_BITINT | 90, 16, 16, true)},
    {TYPE(TY_BITINT | 91, 16, 16, false), TYPE(TY_BITINT | 91, 16, 16, true)},
    {TYPE(TY_BITINT | 92, 16, 16, false), TYPE(TY_BITINT | 92, 16, 16, true)},
    {TYPE(TY_BITINT | 93, 16, 16, false), TYPE(TY_BITINT | 93, 16, 16, true)},
    {TYPE(TY_BITINT | 94, 16, 16, false), TYPE(TY_BITINT | 94, 16, 16, true)},
    {TYPE(TY_BITINT | 95, 16, 16, false), TYPE(TY_BITINT | 95, 16, 16, true)},
    {TYPE(TY_BITINT | 96, 16, 16, false), TYPE(TY_BITINT | 96, 16, 16, true)},
    {TYPE(TY_BITINT | 97, 16, 16, false), TYPE(TY_BITINT | 97, 16, 16, true)},
    {TYPE(TY_BITINT | 98, 16, 16, false), TYPE(TY_BITINT | 98, 16, 16, true)},
    {TYPE(TY_BITINT | 99, 16, 16, false), TYPE(TY_BITINT | 99, 16, 16, true)},
    {TYPE(TY_BITINT | 100, 16, 16, false), TYPE(TY_BITINT | 100, 16, 16, true)},
    {TYPE(TY_BITINT | 101, 16, 16, false), TYPE(TY_BITINT | 101, 16, 16, true)},
    {TYPE(TY_BITINT | 102, 16, 16, false), TYPE(TY_BITINT | 102, 16, 16, true)},
    {TYPE(TY_BITINT | 103, 16, 16, false), TYPE(TY_BITINT | 103, 16, 16, true)},
    {TYPE(TY_BITINT | 104, 16, 16, false), TYPE(TY_BITINT | 104, 16, 16, true)},
    {TYPE(TY_BITINT | 105, 16, 16, false), TYPE(TY_BITINT | 105, 16, 16, true)},
    {TYPE(TY_BITINT | 106, 16, 16, false), TYPE(TY_BITINT | 106, 16, 16, true)},
    {TYPE(TY_BITINT | 107, 16, 16, false), TYPE(TY_BITINT | 107, 16, 16, true)},
    {TYPE(TY_BITINT | 108, 16, 16, false), TYPE(TY_BITINT | 108, 16, 16, true)},
    {TYPE(TY_BITINT | 109, 16, 16, false), TYPE(TY_BITINT | 109, 16, 16, true)},
    {TYPE(TY_BITINT | 110, 16, 16, false), TYPE(TY_BITINT | 110, 16, 16, true)},
    {TYPE(TY_BITINT | 111, 16, 16, false), TYPE(TY_BITINT | 111, 16, 16, true)},
    {TYPE(TY_BITINT | 112, 16, 16, false), TYPE(TY_BITINT | 112, 16, 16, true)},
    {TYPE(TY_BITINT | 113, 16, 16, false), TYPE(TY_BITINT | 113, 16, 16, true)},
    {TYPE(TY_BITINT | 114, 16, 16, false), TYPE(TY_BITINT | 114, 16, 16, true)},
    {TYPE(TY_BITINT | 115, 16, 16, false), TYPE(TY_BITINT | 115, 16, 16, true)},
    {TYPE(TY_BITINT | 116, 16, 16, false), TYPE(TY_BITINT | 116, 16, 16, true)},
    {TYPE(TY_BITINT | 117, 16, 16, false), TYPE(TY_BITINT | 117, 16, 16, true)},
    {TYPE(TY_BITINT | 118, 16, 16, false), TYPE(TY_BITINT | 118, 16, 16, true)},
    {TYPE(TY_BITINT | 119, 16, 16, false), TYPE(TY_BITINT | 119, 16, 16, true)},
    {TYPE(TY_BITINT | 120, 16, 16, false), TYPE(TY_BITINT | 120, 16, 16, true)},
    {TYPE(TY_BITINT | 121, 16, 16, false), TYPE(TY_BITINT | 121, 16, 16, true)},
    {TYPE(TY_BITINT | 122, 16, 16, false), TYPE(TY_BITINT | 122, 16, 16, true)},
    {TYPE(TY_BITINT | 123, 16, 16, false), TYPE(TY_BITINT | 123, 16, 16, true)},
    {TYPE(TY_BITINT | 124, 16, 16, false), TYPE(TY_BITINT | 124, 16, 16, true)},
    {TYPE(TY_BITINT | 125, 16, 16, false), TYPE(TY_BITINT | 125, 16, 16, true)},
    {TYPE(TY_BITINT | 126, 16, 16, false), TYPE(TY_BITINT | 126, 16, 16, true)},
    {TYPE(TY_BITINT | 127, 16, 16, false), TYPE(TY_BITINT | 127, 16, 16, true)},
    {TYPE(TY_BITINT | 128, 16, 16, false), TYPE(TY_BITINT | 128, 16, 16, true)},
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

// The IEC 60559 interchange types (_Float16/32/64/128) — distinct,
// incompatible types (C23 6.2.5). Excludes long double.
bool is_interchange(Type *ty) {
    return ty->kind == TY_F16 || ty->kind == TY_F32 || ty->kind == TY_F64 || ty->kind == TY_F128;
}

// Types whose constants are stored in node->fpval / CBits128: the
// interchange types plus long double (whose format is target-dependent:
// x87 80-bit on amd64, binary128 elsewhere).
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
    if (node->kind == ND_NUM && node->val == 0 && is_integer(node->ty)) return true;
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
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) copy_struct_type(ret, ty);
    ret->origin = ty->origin ? ty->origin : ty;
    return ret;
}

Type *pointer_to(Type *base, uint32_t qual) {
    Type *ty = emalloc(sizeof(Type));
    ty->kind = TY_PTR;
    ty->qual = qual;
    ty->size = T.ty_nullptr->size;
    ty->align = T.ty_nullptr->align;
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

Type *type_qual(Type *ty, uint32_t qual) {
    if ((ty->qual & qual) == qual) return ty;
    ty = copy_type(ty);
    ty->qual |= qual;
    return ty;
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
                if (enm1->val != enm2->val) return false;
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
    error(node->tok, "wrong type argument to unary ‘%.*s’", node->tok->len, node->tok->loc);
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
    error(node->tok, "invalid operands to binary ‘%.*s’", node->tok->len, node->tok->loc);
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
    if (is_record(dst) && is_compatible(type_unqual(dst), src_ty)) return;
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

static void modifiable_lvalue(Node *node) {
    add_type(node->lhs);
    Node *lhs = node->lhs;
    if (!lhs->is_lvalue || lhs->ty->kind == TY_FUNC)
        error(node->tok, "lvalue required as ‘%.*s’ operand", node->tok->len, node->tok->loc);
    if (lhs->ty->qual & Q_CONST || lhs->ty->qual & Q_MEMCONST) {
        if (lhs->kind == ND_VAR) {
            error(node->tok, "assignment of read-only variable ‘%s’", str(lhs->var->id));
        } else {
            char *start = lhs->tok->loc;
            Token *cur = lhs->tok;
            while (cur->next != node->tok) cur = cur->next;
            error(node->tok, "assignment of read-only location ‘%.*s’", (int)(cur->loc - start + cur->len), start);
        }
    }
    if (is_void(lhs->ty)) error(node->tok, "incomplete type ‘void’ is not assignable");
    if (lhs->kind == ND_IMCAST && lhs->lhs->ty->kind == TY_ARRAY)
        error(lhs->tok, "assignment to expression with array type");
}

void lvalue_convert(Node **expr) {
    if (!(*expr) || !(*expr)->is_lvalue) return;
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

    if (ty1->kind & TY_BITINT) ty1 = promote_bitint(ty1);
    if (ty2->kind & TY_BITINT) ty2 = promote_bitint(ty2);

    if (ty1->kind & TY_BITINT || ty2->kind & TY_BITINT) {
        // A wide _BitInt wins over standard integers; the standard side
        // converts to the _BitInt type.
        if (ty1->kind & TY_BITINT && ty2->kind & TY_BITINT) {
            if (ty1->size != ty2->size) return ty1->size < ty2->size ? ty2 : ty1;
            return ty2->is_unsigned ? ty2 : ty1;
        }
        return (ty1->kind & TY_BITINT) ? ty1 : ty2;
    }

    if (ty1->size < 4) ty1 = T.ty_int;
    if (ty2->size < 4) ty2 = T.ty_int;

    if (ty1->size != ty2->size) return (ty1->size < ty2->size) ? ty2 : ty1;

    if (ty1->kind != ty2->kind) {
        // Same size, different kinds (possible on ILP32: long vs int).
        // Pick the higher rank; a tie goes to the unsigned type.
        int k1 = int_rank(ty1), k2 = int_rank(ty2);
        if (k1 != k2) return k1 < k2 ? ty2 : ty1;
        return ty2->is_unsigned ? ty2 : ty1;
    }

    if (ty2->is_unsigned) return ty2;
    return ty1;
}

void integer_promotion(Node **expr) {
    Type *ty = get_common_type((*expr)->ty, T.ty_int);
    new_imcast(expr, ty);
}

static void usual_arith_conv(Node **lhs, Node **rhs) {
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
            integer_promotion(&node->lhs);
            integer_promotion(&node->rhs);
            node->ty = node->lhs->ty;
            break;
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
            check_binop(node);
            lvalue_convert(&node->lhs);
            lvalue_convert(&node->rhs);
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
                if (stmt->kind == ND_EXPR_STMT && stmt->lhs) node->ty = stmt->lhs->ty;
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
            node->ty = pointer_to(T.ty_void, 0);
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
            // Nothing to do
            break;
    }
}
