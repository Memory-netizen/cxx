#include "cxx.h"

#define TYPE(a, b, c, d)  \
    {                     \
        .kind = a,        \
        .size = b,        \
        .align = c,       \
        .is_unsigned = d, \
    }

static Type ty_none_ = TYPE(TY_NONE, -1, 1, false);
static Type ty_void_ = TYPE(TY_VOID, 1, 1, false);
static Type ty_nullptr_ = TYPE(TY_NULLPTR, 8, 8, true);
// void *, the type of NULLPTR. Spelled once here so that the many places
// needing a void pointer share one Type instead of building one with
// pointer_to(T.ty_void, 0) each time.
static Type ty_voidptr_ = {
    .kind = TY_PTR,
    .size = 8,
    .align = 8,
    .is_unsigned = true,
    .base = &ty_void_,
};
static Type ty_bool_ = TYPE(TY_BOOL, 1, 1, true);
static Type ty_char_ = TYPE(TY_CHAR, 1, 1, true);
static Type ty_schar_ = TYPE(TY_SCHAR, 1, 1, false);
static Type ty_uchar_ = TYPE(TY_UCHAR, 1, 1, true);
static Type ty_short_ = TYPE(TY_SHORT, 2, 2, false);
static Type ty_ushort_ = TYPE(TY_SHORT, 2, 2, true);
static Type ty_int_ = TYPE(TY_INT, 4, 4, false);
static Type ty_uint_ = TYPE(TY_INT, 4, 4, true);
static Type ty_long_ = TYPE(TY_LONG, 8, 8, false);
static Type ty_ulong_ = TYPE(TY_LONG, 8, 8, true);
static Type ty_llong_ = TYPE(TY_LLONG, 8, 8, false);
static Type ty_ullong_ = TYPE(TY_LLONG, 8, 8, true);
static Type ty_float_ = TYPE(TY_FLOAT, 4, 4, false);
static Type ty_double_ = TYPE(TY_DOUBLE, 8, 8, false);
static Type ty_ldouble_ = TYPE(TY_LDOUBLE, 16, 16, false);

// AAPCS64: stdarg.h declares
//   { void *__stack; void *__gr_top; void *__vr_top;
//     int __gr_offs; int __vr_offs; }
// A negative offset means the named registers are used up, so the argument
// comes from the stack area. While it is non-negative it indexes the
// general-purpose or SIMD save area, and one step of 8 or 16 bytes is
// taken there.
static VaArgOps va_arg_gp = {
    .kind = VA_MEM_REGS,
    .offset_ty = &ty_int_,
    .offset_field = 3,
    .offset_bound = 0,
    .offset_negative = true,
    .reg_field = 1,
    .mem_field = 0,
    .reg_step = 8,
    .mem_step = 8,
};

// In the save area one SIMD register holds one argument and the registers
// are sixteen bytes apart, but on the stack a float or double takes the
// ordinary eight-byte slot. The two cursors therefore move by different
// amounts, and using the register stride on the stack walks the cursor past
// every argument that follows.
static VaArgOps va_arg_fp = {
    .kind = VA_MEM_REGS,
    .offset_ty = &ty_int_,
    .offset_field = 4,
    .offset_bound = 0,
    .offset_negative = true,
    .reg_field = 2,
    .mem_field = 0,
    .reg_step = 16,
    .mem_step = 8,
};

// A 128-bit float is the one floating type that fills a whole stack slot by
// itself.
static VaArgOps va_arg_fp16 = {
    .kind = VA_MEM_REGS,
    .offset_ty = &ty_int_,
    .offset_field = 4,
    .offset_bound = 0,
    .offset_negative = true,
    .reg_field = 2,
    .mem_field = 0,
    .reg_step = 16,
    .mem_step = 16,
};

// Which register class a requested type is passed in.
// As va_arg_gp, but for a type that occupies two eightbytes in the
// general-purpose registers (_BitInt(> 64), __int128). Both the register
// and the stack cursor must move a whole 16 bytes, or the next argument is
// read from the wrong slot.
static VaArgOps va_arg_gp16 = {
    .kind = VA_MEM_REGS,
    .offset_ty = &ty_int_,
    .offset_field = 3,
    .offset_bound = 0,
    .offset_negative = true,
    .reg_field = 1,
    .mem_field = 0,
    .reg_step = 16,
    .mem_step = 16,
};

// An HFA arrives one element per SIMD register. The table names the cursor,
// the save area and the stride; the element count comes from the type, so the
// reading itself is done where the aggregate is known.
static VaArgOps va_arg_hfa = {
    .kind = VA_MEM_SIMD,
    .offset_ty = &ty_int_,
    .offset_field = 4,
    .offset_negative = true,
    .reg_field = 2,
    .mem_field = 0,
    .reg_stride = 16,
    .offset_limit = 0,
};

// A composite too large for the registers is passed by reference: the slot
// holds a pointer to the caller's copy, so it takes one register however big
// the composite is, and the cursor moves one slot.
static VaArgOps va_arg_agg_ptr = {
    .kind = VA_MEM_REGS,
    .offset_ty = &ty_int_,
    .offset_field = 3,
    .offset_bound = 0,
    .offset_negative = true,
    .reg_field = 1,
    .mem_field = 0,
    .reg_step = 8,
    .mem_step = 8,
    .agg_by_ptr = true,
};

// AAPCS64: a homogeneous floating-point aggregate has every leaf of the same
// floating-point type, at most four of them. Its elements travel in the SIMD
// registers, one each, which the IR records as an array of that element type.
static int hfa_collect(Type *ty, Type **elem) {
    int n = 0;
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        for (Member *m = ty->members; m; m = m->next) {
            int k = hfa_collect(m->ty, elem);
            if (k < 0) return -1;
            if (!*elem) *elem = is_fp_leaf(m->ty) ? m->ty : NULL;
            n += k;
        }
        return n;
    }
    if (ty->kind == TY_ARRAY) {
        for (int i = 0; i < ty->len; i++) {
            int k = hfa_collect(ty->base, elem);
            if (k < 0) return -1;
            n += k;
        }
        return n;
    }
    if (is_fp_leaf(ty)) {
        if (!*elem) *elem = ty;
        // Homogeneous means the same representation, so float and _Float32
        // count as one element type: they are the same width and the same IR
        // type, and only their spelling differs.
        return ty->size == (*elem)->size ? 1 : -1;
    }
    return -1;
}

static void arm64_classify_aggregate(Type *agg, AggClass *out) {
    out->npiece = 0;
    out->size = agg ? agg->size : 0;
    out->shape_array = false;
    out->is_hfa = false;
    if (!agg || (agg->kind != TY_STRUCT && agg->kind != TY_UNION)) return;
    if (agg->size <= 0) return;

    // The homogeneous case first: it decides both the shape and the register
    // file, and it applies even beyond sixteen bytes (up to four elements).
    Type *elem = NULL;
    int n = hfa_collect(agg, &elem);
    if (n > 0 && n <= 4 && elem) {
        out->is_hfa = true;
        out->npiece = n;
        // Even one element stays an array: AAPCS64 spells struct { float }
        // as [1 x float], not as a bare float.
        out->shape_array = true;
        for (int i = 0; i < n; i++) {
            out->piece[i].off = i * elem->size;
            out->piece[i].size = elem->size;
            out->piece[i].ty = elem;
        }
        return;
    }
    {
    }

    // Otherwise a composite of up to sixteen bytes goes in the general-purpose
    // registers, eight bytes per register, and anything larger in memory.
    if (agg->size > 16) return;
    // Every such composite travels in whole eight-byte registers: a struct
    // { char } arrives as an i64-sized value, not as an i8. The IR records
    // that as an array of eightbytes, one per register.
    int nslots = (agg->size + 7) / 8;
    out->npiece = nslots;
    out->shape_array = true;
    for (int i = 0; i < nslots; i++) {
        int off = i * 8;
        int w = agg->size - off < 8 ? agg->size - off : 8;
        out->piece[i].off = off;
        out->piece[i].size = w;
        // The declaration keeps the exact width; widening to a whole
        // register happens where the argument is passed, not here.
        out->piece[i].ty = w > 4 ? &ty_long_ : w > 2 ? &ty_int_ : w > 1 ? &ty_short_ : &ty_schar_;
    }
}

static Type *arm64_pieces_type(Type *agg) {
    if (!agg || (agg->kind != TY_STRUCT && agg->kind != TY_UNION)) return NULL;
    AggClass c;
    arm64_classify_aggregate(agg, &c);
    return agg_shape_type(&c);
}

static void arm64_classify_publish(void) {}

static int arm64_param_slots(Type *ty) {
    if (!ty || (ty->kind != TY_STRUCT && ty->kind != TY_UNION)) return 1;
    AggClass c;
    arm64_classify_aggregate(ty, &c);
    return agg_param_slots(ty, &c);
}

static VaArgOps *arm64_va_arg(Type *want) {
    // A composite is classified before anything else: which registers it
    // travels in, and therefore which cursor finds it, is the classifier's
    // answer and not the size's.
    if (want->kind == TY_STRUCT || want->kind == TY_UNION) {
        AggClass c;
        arm64_classify_aggregate(want, &c);
        if (c.npiece == 0) return &va_arg_agg_ptr;
        if (c.is_hfa) return &va_arg_hfa;
    }
    if (is_flonum(want)) return want->size > 8 ? &va_arg_fp16 : &va_arg_fp;
    // An integer wider than one eightbyte takes two GP registers. Its
    // alignment is still 8, so size is what distinguishes it.
    if (want->size > 8) return &va_arg_gp16;
    return &va_arg_gp;
}

// The AAPCS64 va_list: the stack area, the two register save area tops and
// the two running offsets. Passed by reference, so it is a plain structure.
static Type *arm64_va_list_type(void) {
    static Type elem;
    static bool done;
    if (!done) {
        elem.kind = TY_STRUCT;
        elem.size = 32;
        elem.align = 8;
        elem.is_unsigned = true;
        Member *m;

        m = emalloc(sizeof(Member));
        m->ty = &ty_voidptr_;
        m->offset = 0;
        m->align = 8;
        elem.members = m;

        m->next = emalloc(sizeof(Member));
        m = m->next;
        m->ty = &ty_voidptr_;
        m->offset = 8;
        m->align = 8;

        m->next = emalloc(sizeof(Member));
        m = m->next;
        m->ty = &ty_voidptr_;
        m->offset = 16;
        m->align = 8;

        m->next = emalloc(sizeof(Member));
        m = m->next;
        m->ty = &ty_int_;
        m->offset = 24;
        m->align = 4;

        m->next = emalloc(sizeof(Member));
        m = m->next;
        m->ty = &ty_int_;
        m->offset = 28;
        m->align = 4;
        m->next = NULL;

        done = true;
    }
    return &elem;
}

#undef TYPE

Target T_arm64 = {
    .llvm_features = NULL,
    .llvm_abi = NULL,
    .clang_mabi = NULL,
    .clang_march = NULL,
    .ldouble_is_fp80 = false,
    .bitint_align = 16,
    .fp_reg_bits = 128,
    // AAPCS64 leaves the upper bits of a narrow argument undefined.
    .ext_bits = 0,
    .name = "arm64",
    .triple = "aarch64-linux-gnu",
    .datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32",
    .sysroot = NULL,
    .ty_none = &ty_none_,
    .ty_void = &ty_void_,
    .ty_voidptr = &ty_voidptr_,
    .ty_nullptr = &ty_nullptr_,
    .ty_bool = &ty_bool_,
    .ty_char = &ty_char_,
    .ty_schar = &ty_schar_,
    .ty_uchar = &ty_uchar_,
    .ty_short = &ty_short_,
    .ty_ushort = &ty_ushort_,
    .ty_int = &ty_int_,
    .ty_uint = &ty_uint_,
    .ty_long = &ty_long_,
    .ty_ulong = &ty_ulong_,
    .ty_llong = &ty_llong_,
    .ty_ullong = &ty_ullong_,
    .ty_float = &ty_float_,
    .ty_double = &ty_double_,
    .ty_ldouble = &ty_ldouble_,
    .ty_wchar = &ty_uint_,
    .int_max = 2147483647,
    .uint_max = 4294967295U,
    .long_max = 9223372036854775807L,
    .ulong_max = 18446744073709551615UL,
    .llong_max = 9223372036854775807LL,
    .va_list_type = arm64_va_list_type,
    .va_arg_ops = arm64_va_arg,
    .classify_aggregate = arm64_classify_aggregate,
    .classify_publish = arm64_classify_publish,
    .pieces_type = arm64_pieces_type,
    .abi_param_slots = arm64_param_slots,
    .agg_byval_param = false,
    .agg_always_array = true,
    .agg_full_regs = true,
    .predef =
        "#define _LP64 1\n"
        "#define __AARCH64EL__ 1\n"
        "#define __AARCH64_CMODEL_SMALL__ 1\n"
        "#define __ARM_64BIT_STATE 1\n"

        "#define __ARM_ALIGN_MAX_STACK_PWR 4\n"
        "#define __ARM_ARCH 8\n"
        "#define __ARM_ARCH_ISA_A64 1\n"
        "#define __ARM_ARCH_PROFILE 'A'\n"

        "#define __ARM_PCS_AAPCS64 1\n"
        "#define __ARM_SIZEOF_MINIMAL_ENUM 4\n"
        "#define __ARM_SIZEOF_WCHAR_T 4\n"

        "#define __ATOMIC_ACQUIRE 2\n"
        "#define __ATOMIC_ACQ_REL 4\n"
        "#define __ATOMIC_CONSUME 1\n"
        "#define __ATOMIC_RELAXED 0\n"
        "#define __ATOMIC_RELEASE 3\n"
        "#define __ATOMIC_SEQ_CST 5\n"
        "#define __BIGGEST_ALIGNMENT__ 16\n"
        "#define __BITINT_MAXWIDTH__ 128\n"
        "#define __BOOL_WIDTH__ 1\n"
        "#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__\n"
        "#define __CHAR16_TYPE__ unsigned short\n"
        "#define __CHAR32_TYPE__ unsigned int\n"
        "#define __CHAR_BIT__ 8\n"
        "#define __CHAR_UNSIGNED__ 1\n"
        "#define __CLANG_ATOMIC_BOOL_LOCK_FREE 2\n"
        "#define __CLANG_ATOMIC_CHAR16_T_LOCK_FREE 2\n"
        "#define __CLANG_ATOMIC_CHAR32_T_LOCK_FREE 2\n"
        "#define __CLANG_ATOMIC_CHAR_LOCK_FREE 2\n"
        "#define __CLANG_ATOMIC_INT_LOCK_FREE 2\n"
        "#define __CLANG_ATOMIC_LLONG_LOCK_FREE 2\n"
        "#define __CLANG_ATOMIC_LONG_LOCK_FREE 2\n"
        "#define __CLANG_ATOMIC_POINTER_LOCK_FREE 2\n"
        "#define __CLANG_ATOMIC_SHORT_LOCK_FREE 2\n"
        "#define __CLANG_ATOMIC_WCHAR_T_LOCK_FREE 2\n"
        "#define __GCC_ATOMIC_BOOL_LOCK_FREE 2\n"
        "#define __GCC_ATOMIC_CHAR16_T_LOCK_FREE 2\n"
        "#define __GCC_ATOMIC_CHAR32_T_LOCK_FREE 2\n"
        "#define __GCC_ATOMIC_CHAR_LOCK_FREE 2\n"
        "#define __GCC_ATOMIC_INT_LOCK_FREE 2\n"
        "#define __GCC_ATOMIC_LLONG_LOCK_FREE 2\n"
        "#define __GCC_ATOMIC_LONG_LOCK_FREE 2\n"
        "#define __GCC_ATOMIC_POINTER_LOCK_FREE 2\n"
        "#define __GCC_ATOMIC_SHORT_LOCK_FREE 2\n"
        "#define __GCC_ATOMIC_WCHAR_T_LOCK_FREE 2\n"

        "#define __DBL_DECIMAL_DIG__ 17\n"
        "#define __DBL_DENORM_MIN__ 4.9406564584124654e-324\n"
        "#define __DBL_DIG__ 15\n"
        "#define __DBL_EPSILON__ 2.2204460492503131e-16\n"
        "#define __DBL_HAS_DENORM__ 1\n"
        "#define __DBL_HAS_INFINITY__ 1\n"
        "#define __DBL_HAS_QUIET_NAN__ 1\n"
        "#define __DBL_MANT_DIG__ 53\n"
        "#define __DBL_MAX_10_EXP__ 308\n"
        "#define __DBL_MAX_EXP__ 1024\n"
        "#define __DBL_MAX__ 1.7976931348623157e+308\n"
        "#define __DBL_MIN_10_EXP__ (-307)\n"
        "#define __DBL_MIN_EXP__ (-1021)\n"
        "#define __DBL_MIN__ 2.2250738585072014e-308\n"
        "#define __DBL_NORM_MAX__ 1.7976931348623157e+308\n"
        "#define __DECIMAL_DIG__ __LDBL_DECIMAL_DIG__\n"
        "#define __ELF__ 1\n"
        "#define __FINITE_MATH_ONLY__ 0\n"
        "#define __FLT16_DECIMAL_DIG__ 5\n"
        "#define __FLT16_DENORM_MIN__ 5.9604644775390625e-8F16\n"
        "#define __FLT16_DIG__ 3\n"
        "#define __FLT16_EPSILON__ 9.765625e-4F16\n"
        "#define __FLT16_HAS_DENORM__ 1\n"
        "#define __FLT16_HAS_INFINITY__ 1\n"
        "#define __FLT16_HAS_QUIET_NAN__ 1\n"
        "#define __FLT16_MANT_DIG__ 11\n"
        "#define __FLT16_MAX_10_EXP__ 4\n"
        "#define __FLT16_MAX_EXP__ 16\n"
        "#define __FLT16_MAX__ 6.5504e+4F16\n"
        "#define __FLT16_MIN_10_EXP__ (-4)\n"
        "#define __FLT16_MIN_EXP__ (-13)\n"
        "#define __FLT16_MIN__ 6.103515625e-5F16\n"
        "#define __FLT16_NORM_MAX__ 6.5504e+4F16\n"
        "#define __FLT_DECIMAL_DIG__ 9\n"
        "#define __FLT_DENORM_MIN__ 1.40129846e-45F\n"
        "#define __FLT_DIG__ 6\n"
        "#define __FLT_EPSILON__ 1.19209290e-7F\n"
        "#define __FLT_HAS_DENORM__ 1\n"
        "#define __FLT_HAS_INFINITY__ 1\n"
        "#define __FLT_HAS_QUIET_NAN__ 1\n"
        "#define __FLT_MANT_DIG__ 24\n"
        "#define __FLT_MAX_10_EXP__ 38\n"
        "#define __FLT_MAX_EXP__ 128\n"
        "#define __FLT_MAX__ 3.40282347e+38F\n"
        "#define __FLT_MIN_10_EXP__ (-37)\n"
        "#define __FLT_MIN_EXP__ (-125)\n"
        "#define __FLT_MIN__ 1.17549435e-38F\n"
        "#define __FLT_NORM_MAX__ 3.40282347e+38F\n"
        "#define __FLT_RADIX__ 2\n"
        "#define __FPCLASS_NEGINF 0x0004\n"
        "#define __FPCLASS_NEGNORMAL 0x0008\n"
        "#define __FPCLASS_NEGSUBNORMAL 0x0010\n"
        "#define __FPCLASS_NEGZERO 0x0020\n"
        "#define __FPCLASS_POSINF 0x0200\n"
        "#define __FPCLASS_POSNORMAL 0x0100\n"
        "#define __FPCLASS_POSSUBNORMAL 0x0080\n"
        "#define __FPCLASS_POSZERO 0x0040\n"
        "#define __FPCLASS_QNAN 0x0002\n"
        "#define __FPCLASS_SNAN 0x0001\n"

        "#define __INT16_C(c) c\n"
        "#define __INT16_C_SUFFIX__ \n"
        "#define __INT16_FMTd__ \"hd\"\n"
        "#define __INT16_FMTi__ \"hi\"\n"
        "#define __INT16_MAX__ 32767\n"
        "#define __INT16_TYPE__ short\n"
        "#define __INT32_C(c) c\n"
        "#define __INT32_C_SUFFIX__ \n"
        "#define __INT32_FMTd__ \"d\"\n"
        "#define __INT32_FMTi__ \"i\"\n"
        "#define __INT32_MAX__ 2147483647\n"
        "#define __INT32_TYPE__ int\n"
        "#define __INT64_C(c) c##L\n"
        "#define __INT64_C_SUFFIX__ L\n"
        "#define __INT64_FMTd__ \"ld\"\n"
        "#define __INT64_FMTi__ \"li\"\n"
        "#define __INT64_MAX__ 9223372036854775807L\n"
        "#define __INT64_TYPE__ long int\n"
        "#define __INT8_C(c) c\n"
        "#define __INT8_C_SUFFIX__ \n"
        "#define __INT8_FMTd__ \"hhd\"\n"
        "#define __INT8_FMTi__ \"hhi\"\n"
        "#define __INT8_MAX__ 127\n"
        "#define __INT8_TYPE__ signed char\n"
        "#define __INTMAX_C(c) c##L\n"
        "#define __INTMAX_C_SUFFIX__ L\n"
        "#define __INTMAX_FMTd__ \"ld\"\n"
        "#define __INTMAX_FMTi__ \"li\"\n"
        "#define __INTMAX_MAX__ 9223372036854775807L\n"
        "#define __INTMAX_TYPE__ long int\n"
        "#define __INTMAX_WIDTH__ 64\n"
        "#define __INTPTR_FMTd__ \"ld\"\n"
        "#define __INTPTR_FMTi__ \"li\"\n"
        "#define __INTPTR_MAX__ 9223372036854775807L\n"
        "#define __INTPTR_TYPE__ long int\n"
        "#define __INTPTR_WIDTH__ 64\n"
        "#define __INT_FAST16_FMTd__ \"hd\"\n"
        "#define __INT_FAST16_FMTi__ \"hi\"\n"
        "#define __INT_FAST16_MAX__ 32767\n"
        "#define __INT_FAST16_TYPE__ short\n"
        "#define __INT_FAST16_WIDTH__ 16\n"
        "#define __INT_FAST32_FMTd__ \"d\"\n"
        "#define __INT_FAST32_FMTi__ \"i\"\n"
        "#define __INT_FAST32_MAX__ 2147483647\n"
        "#define __INT_FAST32_TYPE__ int\n"
        "#define __INT_FAST32_WIDTH__ 32\n"
        "#define __INT_FAST64_FMTd__ \"ld\"\n"
        "#define __INT_FAST64_FMTi__ \"li\"\n"
        "#define __INT_FAST64_MAX__ 9223372036854775807L\n"
        "#define __INT_FAST64_TYPE__ long int\n"
        "#define __INT_FAST64_WIDTH__ 64\n"
        "#define __INT_FAST8_FMTd__ \"hhd\"\n"
        "#define __INT_FAST8_FMTi__ \"hhi\"\n"
        "#define __INT_FAST8_MAX__ 127\n"
        "#define __INT_FAST8_TYPE__ signed char\n"
        "#define __INT_FAST8_WIDTH__ 8\n"
        "#define __INT_LEAST16_FMTd__ \"hd\"\n"
        "#define __INT_LEAST16_FMTi__ \"hi\"\n"
        "#define __INT_LEAST16_MAX__ 32767\n"
        "#define __INT_LEAST16_TYPE__ short\n"
        "#define __INT_LEAST16_WIDTH__ 16\n"
        "#define __INT_LEAST32_FMTd__ \"d\"\n"
        "#define __INT_LEAST32_FMTi__ \"i\"\n"
        "#define __INT_LEAST32_MAX__ 2147483647\n"
        "#define __INT_LEAST32_TYPE__ int\n"
        "#define __INT_LEAST32_WIDTH__ 32\n"
        "#define __INT_LEAST64_FMTd__ \"ld\"\n"
        "#define __INT_LEAST64_FMTi__ \"li\"\n"
        "#define __INT_LEAST64_MAX__ 9223372036854775807L\n"
        "#define __INT_LEAST64_TYPE__ long int\n"
        "#define __INT_LEAST64_WIDTH__ 64\n"
        "#define __INT_LEAST8_FMTd__ \"hhd\"\n"
        "#define __INT_LEAST8_FMTi__ \"hhi\"\n"
        "#define __INT_LEAST8_MAX__ 127\n"
        "#define __INT_LEAST8_TYPE__ signed char\n"
        "#define __INT_LEAST8_WIDTH__ 8\n"
        "#define __INT_MAX__ 2147483647\n"
        "#define __INT_WIDTH__ 32\n"
        "#define __LDBL_DECIMAL_DIG__ 36\n"
        "#define __LDBL_DENORM_MIN__ 6.47517511943802511092443895822764655e-4966L\n"
        "#define __LDBL_DIG__ 33\n"
        "#define __LDBL_EPSILON__ 1.92592994438723585305597794258492732e-34L\n"
        "#define __LDBL_HAS_DENORM__ 1\n"
        "#define __LDBL_HAS_INFINITY__ 1\n"
        "#define __LDBL_HAS_QUIET_NAN__ 1\n"
        "#define __LDBL_MANT_DIG__ 113\n"
        "#define __LDBL_MAX_10_EXP__ 4932\n"
        "#define __LDBL_MAX_EXP__ 16384\n"
        "#define __LDBL_MAX__ 1.18973149535723176508575932662800702e+4932L\n"
        "#define __LDBL_MIN_10_EXP__ (-4931)\n"
        "#define __LDBL_MIN_EXP__ (-16381)\n"
        "#define __LDBL_MIN__ 3.36210314311209350626267781732175260e-4932L\n"
        "#define __LDBL_NORM_MAX__ 1.18973149535723176508575932662800702e+4932L\n"
        "#define __LITTLE_ENDIAN__ 1\n"
        "#define __LLONG_WIDTH__ 64\n"
        "#define __LONG_LONG_MAX__ 9223372036854775807LL\n"
        "#define __LONG_MAX__ 9223372036854775807L\n"
        "#define __LONG_WIDTH__ 64\n"
        "#define __LP64__ 1\n"

        "#define __ORDER_BIG_ENDIAN__ 4321\n"
        "#define __ORDER_LITTLE_ENDIAN__ 1234\n"
        "#define __ORDER_PDP_ENDIAN__ 3412\n"
        "#define __FLT32_DECIMAL_DIG__ 9\n"
        "#define __FLT32_DENORM_MIN__ 0x1p-149f32\n"
        "#define __FLT32_DIG__ 6\n"
        "#define __FLT32_EPSILON__ 0x1p-23f32\n"
        "#define __FLT32_HAS_DENORM__ 1\n"
        "#define __FLT32_HAS_INFINITY__ 1\n"
        "#define __FLT32_HAS_QUIET_NAN__ 1\n"
        "#define __FLT32_MANT_DIG__ 24\n"
        "#define __FLT32_MAX_10_EXP__ 38\n"
        "#define __FLT32_MAX_EXP__ 128\n"
        "#define __FLT32_MAX__ 0x1.fffffep+127f32\n"
        "#define __FLT32_MIN_10_EXP__ (-37)\n"
        "#define __FLT32_MIN_EXP__ (-125)\n"
        "#define __FLT32_MIN__ 0x1p-126f32\n"
        "#define __FLT32_NORM_MAX__ 0x1.fffffep+127f32\n"
        "#define __FLT64_DECIMAL_DIG__ 17\n"
        "#define __FLT64_DENORM_MIN__ 0x0.0000000000001p-1022f64\n"
        "#define __FLT64_DIG__ 15\n"
        "#define __FLT64_EPSILON__ 0x1p-52f64\n"
        "#define __FLT64_HAS_DENORM__ 1\n"
        "#define __FLT64_HAS_INFINITY__ 1\n"
        "#define __FLT64_HAS_QUIET_NAN__ 1\n"
        "#define __FLT64_MANT_DIG__ 53\n"
        "#define __FLT64_MAX_10_EXP__ 308\n"
        "#define __FLT64_MAX_EXP__ 1024\n"
        "#define __FLT64_MAX__ 0x1.fffffffffffffp+1023f64\n"
        "#define __FLT64_MIN_10_EXP__ (-307)\n"
        "#define __FLT64_MIN_EXP__ (-1021)\n"
        "#define __FLT64_MIN__ 0x1p-1022f64\n"
        "#define __FLT64_NORM_MAX__ 0x1.fffffffffffffp+1023f64\n"
        "#define __FLT128_DECIMAL_DIG__ 36\n"
        "#define __FLT128_DENORM_MIN__ 0x1p-16494f128\n"
        "#define __FLT128_DIG__ 33\n"
        "#define __FLT128_EPSILON__ 0x1p-112f128\n"
        "#define __FLT128_HAS_DENORM__ 1\n"
        "#define __FLT128_HAS_INFINITY__ 1\n"
        "#define __FLT128_HAS_QUIET_NAN__ 1\n"
        "#define __FLT128_MANT_DIG__ 113\n"
        "#define __FLT128_MAX_10_EXP__ 4932\n"
        "#define __FLT128_MAX_EXP__ 16384\n"
        "#define __FLT128_MAX__ 0x1.ffffffffffffffffffffffffffffp+16383f128\n"
        "#define __FLT128_MIN_10_EXP__ (-4931)\n"
        "#define __FLT128_MIN_EXP__ (-16381)\n"
        "#define __FLT128_MIN__ 0x1p-16382f128\n"
        "#define __FLT128_NORM_MAX__ 0x1.ffffffffffffffffffffffffffffp+16383f128\n"
        "#define __PIC__ 2\n"
        "#define __PIE__ 2\n"
        "#define __POINTER_WIDTH__ 64\n"
        "#define __PTRDIFF_FMTd__ \"ld\"\n"
        "#define __PTRDIFF_FMTi__ \"li\"\n"
        "#define __PTRDIFF_MAX__ 9223372036854775807L\n"
        "#define __PTRDIFF_TYPE__ long int\n"
        "#define __PTRDIFF_WIDTH__ 64\n"
        "#define __SCHAR_MAX__ 127\n"
        "#define __SHRT_MAX__ 32767\n"
        "#define __SHRT_WIDTH__ 16\n"
        "#define __SIG_ATOMIC_MAX__ 2147483647\n"
        "#define __SIG_ATOMIC_WIDTH__ 32\n"
        "#define __SIZEOF_DOUBLE__ 8\n"
        "#define __SIZEOF_FLOAT__ 4\n"
        "#define __SIZEOF_INT128__ 16\n"
        "#define __SIZEOF_INT__ 4\n"
        "#define __SIZEOF_LONG_DOUBLE__ 16\n"
        "#define __SIZEOF_LONG_LONG__ 8\n"
        "#define __SIZEOF_LONG__ 8\n"
        "#define __SIZEOF_POINTER__ 8\n"
        "#define __SIZEOF_PTRDIFF_T__ 8\n"
        "#define __SIZEOF_SHORT__ 2\n"
        "#define __SIZEOF_SIZE_T__ 8\n"
        "#define __SIZEOF_WCHAR_T__ 4\n"
        "#define __SIZEOF_WINT_T__ 4\n"
        "#define __SIZE_FMTX__ \"lX\"\n"
        "#define __SIZE_FMTo__ \"lo\"\n"
        "#define __SIZE_FMTu__ \"lu\"\n"
        "#define __SIZE_FMTx__ \"lx\"\n"
        "#define __SIZE_MAX__ 18446744073709551615UL\n"
        "#define __SIZE_TYPE__ long unsigned int\n"
        "#define __SIZE_WIDTH__ 64\n"
        "#define __STDC_EMBED_EMPTY__ 2\n"
        "#define __STDC_EMBED_FOUND__ 1\n"
        "#define __STDC_EMBED_NOT_FOUND__ 0\n"
        "#define __STDC_HOSTED__ 1\n"
        "#define __STDC_IEC_60559_TYPES__ 202311L\n"
        "#define __STDC_NO_COMPLEX__ 1\n"
        "#define __STDC_UTF_16__ 1\n"
        "#define __STDC_UTF_32__ 1\n"
        "#define __STDC_VERSION__ 202311L\n"
        "#define __GNUC__ 7\n"
        "#define __GNUC_MINOR__ 0\n"
        "#define __GNUC_PATCHLEVEL__ 0\n"
        "#define __STDC__ 1\n"
        "#define __UINT16_C(c) c\n"
        "#define __UINT16_C_SUFFIX__ \n"
        "#define __UINT16_FMTX__ \"hX\"\n"
        "#define __UINT16_FMTo__ \"ho\"\n"
        "#define __UINT16_FMTu__ \"hu\"\n"
        "#define __UINT16_FMTx__ \"hx\"\n"
        "#define __UINT16_MAX__ 65535\n"
        "#define __UINT16_TYPE__ unsigned short\n"
        "#define __UINT32_C(c) c##U\n"
        "#define __UINT32_C_SUFFIX__ U\n"
        "#define __UINT32_FMTX__ \"X\"\n"
        "#define __UINT32_FMTo__ \"o\"\n"
        "#define __UINT32_FMTu__ \"u\"\n"
        "#define __UINT32_FMTx__ \"x\"\n"
        "#define __UINT32_MAX__ 4294967295U\n"
        "#define __UINT32_TYPE__ unsigned int\n"
        "#define __UINT64_C(c) c##UL\n"
        "#define __UINT64_C_SUFFIX__ UL\n"
        "#define __UINT64_FMTX__ \"lX\"\n"
        "#define __UINT64_FMTo__ \"lo\"\n"
        "#define __UINT64_FMTu__ \"lu\"\n"
        "#define __UINT64_FMTx__ \"lx\"\n"
        "#define __UINT64_MAX__ 18446744073709551615UL\n"
        "#define __UINT64_TYPE__ long unsigned int\n"
        "#define __UINT8_C(c) c\n"
        "#define __UINT8_C_SUFFIX__ \n"
        "#define __UINT8_FMTX__ \"hhX\"\n"
        "#define __UINT8_FMTo__ \"hho\"\n"
        "#define __UINT8_FMTu__ \"hhu\"\n"
        "#define __UINT8_FMTx__ \"hhx\"\n"
        "#define __UINT8_MAX__ 255\n"
        "#define __UINT8_TYPE__ unsigned char\n"
        "#define __UINTMAX_C(c) c##UL\n"
        "#define __UINTMAX_C_SUFFIX__ UL\n"
        "#define __UINTMAX_FMTX__ \"lX\"\n"
        "#define __UINTMAX_FMTo__ \"lo\"\n"
        "#define __UINTMAX_FMTu__ \"lu\"\n"
        "#define __UINTMAX_FMTx__ \"lx\"\n"
        "#define __UINTMAX_MAX__ 18446744073709551615UL\n"
        "#define __UINTMAX_TYPE__ long unsigned int\n"
        "#define __UINTMAX_WIDTH__ 64\n"
        "#define __UINTPTR_FMTX__ \"lX\"\n"
        "#define __UINTPTR_FMTo__ \"lo\"\n"
        "#define __UINTPTR_FMTu__ \"lu\"\n"
        "#define __UINTPTR_FMTx__ \"lx\"\n"
        "#define __UINTPTR_MAX__ 18446744073709551615UL\n"
        "#define __UINTPTR_TYPE__ long unsigned int\n"
        "#define __UINTPTR_WIDTH__ 64\n"
        "#define __UINT_FAST16_FMTX__ \"hX\"\n"
        "#define __UINT_FAST16_FMTo__ \"ho\"\n"
        "#define __UINT_FAST16_FMTu__ \"hu\"\n"
        "#define __UINT_FAST16_FMTx__ \"hx\"\n"
        "#define __UINT_FAST16_MAX__ 65535\n"
        "#define __UINT_FAST16_TYPE__ unsigned short\n"
        "#define __UINT_FAST32_FMTX__ \"X\"\n"
        "#define __UINT_FAST32_FMTo__ \"o\"\n"
        "#define __UINT_FAST32_FMTu__ \"u\"\n"
        "#define __UINT_FAST32_FMTx__ \"x\"\n"
        "#define __UINT_FAST32_MAX__ 4294967295U\n"
        "#define __UINT_FAST32_TYPE__ unsigned int\n"
        "#define __UINT_FAST64_FMTX__ \"lX\"\n"
        "#define __UINT_FAST64_FMTo__ \"lo\"\n"
        "#define __UINT_FAST64_FMTu__ \"lu\"\n"
        "#define __UINT_FAST64_FMTx__ \"lx\"\n"
        "#define __UINT_FAST64_MAX__ 18446744073709551615UL\n"
        "#define __UINT_FAST64_TYPE__ long unsigned int\n"
        "#define __UINT_FAST8_FMTX__ \"hhX\"\n"
        "#define __UINT_FAST8_FMTo__ \"hho\"\n"
        "#define __UINT_FAST8_FMTu__ \"hhu\"\n"
        "#define __UINT_FAST8_FMTx__ \"hhx\"\n"
        "#define __UINT_FAST8_MAX__ 255\n"
        "#define __UINT_FAST8_TYPE__ unsigned char\n"
        "#define __UINT_LEAST16_FMTX__ \"hX\"\n"
        "#define __UINT_LEAST16_FMTo__ \"ho\"\n"
        "#define __UINT_LEAST16_FMTu__ \"hu\"\n"
        "#define __UINT_LEAST16_FMTx__ \"hx\"\n"
        "#define __UINT_LEAST16_MAX__ 65535\n"
        "#define __UINT_LEAST16_TYPE__ unsigned short\n"
        "#define __UINT_LEAST32_FMTX__ \"X\"\n"
        "#define __UINT_LEAST32_FMTo__ \"o\"\n"
        "#define __UINT_LEAST32_FMTu__ \"u\"\n"
        "#define __UINT_LEAST32_FMTx__ \"x\"\n"
        "#define __UINT_LEAST32_MAX__ 4294967295U\n"
        "#define __UINT_LEAST32_TYPE__ unsigned int\n"
        "#define __UINT_LEAST64_FMTX__ \"lX\"\n"
        "#define __UINT_LEAST64_FMTo__ \"lo\"\n"
        "#define __UINT_LEAST64_FMTu__ \"lu\"\n"
        "#define __UINT_LEAST64_FMTx__ \"lx\"\n"
        "#define __UINT_LEAST64_MAX__ 18446744073709551615UL\n"
        "#define __UINT_LEAST64_TYPE__ long unsigned int\n"
        "#define __UINT_LEAST8_FMTX__ \"hhX\"\n"
        "#define __UINT_LEAST8_FMTo__ \"hho\"\n"
        "#define __UINT_LEAST8_FMTu__ \"hhu\"\n"
        "#define __UINT_LEAST8_FMTx__ \"hhx\"\n"
        "#define __UINT_LEAST8_MAX__ 255\n"
        "#define __UINT_LEAST8_TYPE__ unsigned char\n"
        "#define __USER_LABEL_PREFIX__ \n"
        "#define __WCHAR_MAX__ 4294967295U\n"
        "#define __WCHAR_TYPE__ unsigned int\n"
        "#define __WCHAR_UNSIGNED__ 1\n"
        "#define __WCHAR_WIDTH__ 32\n"
        "#define __WINT_MAX__ 4294967295U\n"
        "#define __WINT_TYPE__ unsigned int\n"
        "#define __WINT_UNSIGNED__ 1\n"
        "#define __WINT_WIDTH__ 32\n"
        "#define __aarch64__ 1\n"

        "#define __cxx__ 1\n"
        "#define __gnu_linux__ 1\n"
        "#define __linux 1\n"
        "#define __linux__ 1\n"
        "#define __pic__ 2\n"
        "#define __pie__ 2\n"
        "#define __unix 1\n"
        "#define __unix__ 1\n"
        "#define linux 1\n"
        "#define unix 1\n"
        "#define __alignof__ _Alignof\n"
        "#define __const__ const\n"
        "#define __has_attribute __has_attribute\n"
        "#define __has_c_attribute __has_c_attribute\n"
        "#define __has_embed __has_embed\n"
        "#define __has_include __has_include\n"
        "#define __has_include_next __has_include_next\n"
        "#define __inline inline\n"
        "#define __inline__ inline\n"
        "#define __signed__ signed\n"
        "#define __typeof typeof\n"
        "#define __typeof__ typeof\n"
        "#define __volatile__ volatile\n",
};
