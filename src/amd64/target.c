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
static Type ty_char_ = TYPE(TY_CHAR, 1, 1, false);
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

// System V AMD64: stdarg.h declares
//   { unsigned gp_offset; unsigned fp_offset; void *overflow_arg_area;
//     void *reg_save_area; }
// The offsets are byte positions into a 176-byte register save area where
// the general-purpose registers occupy 0..47 and the SSE registers 48..175.
// A class is exhausted once its next slot would run past the area, which
// is an offset bound of 40 for a general-purpose argument and 160 for an
// SSE one; the steps are 8 and 16 bytes.
static VaArgOps va_arg_gp = {
    .kind = VA_MEM_REGS,
    .offset_ty = &ty_uint_,
    .offset_field = 0,
    .offset_bound = 40,
    .reg_field = 3,
    .mem_field = 2,
    .reg_step = 8,
    .mem_step = 8,
};

static VaArgOps va_arg_fp = {
    .kind = VA_MEM_REGS,
    .offset_ty = &ty_uint_,
    .offset_field = 1,
    .offset_bound = 160,
    .reg_field = 3,
    .mem_field = 2,
    .reg_step = 16,
    .mem_step = 16,
};

// A long double is 16-byte aligned while the overflow area advances 8 bytes
// at a time, so its cursor is raised to that alignment before reading; the
// register class is still the general-purpose one, which is where an
// argument of this size never fits.
// As va_arg_gp, but for a type that occupies two eightbytes in the
// integer registers (_BitInt(> 64), __int128). The cursor must move a
// whole 16 bytes or the next argument is read from the wrong slot.
static VaArgOps va_arg_gp16 = {
    .kind = VA_MEM_REGS,
    .offset_ty = &ty_uint_,
    .offset_field = 0,
    .offset_bound = 40,
    .reg_field = 3,
    .mem_field = 2,
    .reg_step = 16,
    .mem_step = 16,
};

static VaArgOps va_arg_mem16 = {
    .kind = VA_MEM_REGS,
    .offset_ty = &ty_uint_,
    .offset_field = 0,
    // Always the overflow area: gp_offset is at least 8 after va_start,
    // so "still has room in a register" is never true for this class.
    .offset_bound = 0,
    .reg_field = 3,
    .mem_field = 2,
    .reg_step = 8,
    // long double and _Float128 are 16 bytes wide on the stack, and
    // belong to the MEMORY class, so the cursor always moves a whole
    // one of them -- never the 8 that va_arg_gp uses.
    .mem_step = 16,
    .mem_align = 16,
};

// One piece is SSE class when every scalar in it is a floating type; any
// integer member makes the whole piece INTEGER (SysV AMD64 3.2.3).
static bool piece_is_sse(Type *agg, int lo, int hi) {
    for (Member *m = agg->members; m; m = m->next) {
        int mlo = m->offset;
        int mhi = m->offset + m->ty->size;
        if (mhi <= lo || mlo >= hi) continue;
        if (m->ty->kind == TY_STRUCT || m->ty->kind == TY_UNION) return piece_is_sse(m->ty, lo - mlo, hi - mlo);
        if (!is_flonum(m->ty)) return false;
    }
    return true;
}

static bool amd64_is_memory_class(Type *agg) {
    if (agg->size <= 0 || agg->size > 16) return true;
    for (Member *m = agg->members; m; m = m->next)
        if (m->ty->align > 8) return true;
    return false;
}

static void amd64_classify_aggregate(Type *agg, AggClass *out) {
    out->npiece = 0;
    out->size = agg->size;
    // Only a record is classified: a pointer, however it is spelled, is one
    // value and must never be expanded into the pieces of what it points at.
    if (!agg || (agg->kind != TY_STRUCT && agg->kind != TY_UNION)) return;
    if (amd64_is_memory_class(agg)) return;

    int n = (agg->size + 7) / 8;
    out->npiece = n;
    for (int i = 0; i < n; i++) {
        int lo = i * 8;
        int hi = lo + 8 < agg->size ? lo + 8 : agg->size;
        // Width is that of the last member ending inside this piece.
        int end = 0;
        bool sse = piece_is_sse(agg, lo, hi);
        for (Member *m = agg->members; m; m = m->next) {
            int mhi = m->offset + m->ty->size;
            if (mhi <= lo || m->offset >= hi) continue;
            if (mhi > end) end = mhi;
        }
        if (!end) end = hi;
        int sz = end - lo;
        Type *ty;
        if (sse)
            ty = sz > 4 ? &ty_double_ : &ty_float_;
        else if (sz > 4)
            ty = &ty_long_;
        else if (sz > 2)
            ty = &ty_int_;
        else if (sz > 1)
            ty = &ty_short_;
        else
            ty = &ty_schar_;
        out->piece[i].off = lo;
        out->piece[i].size = sz;
        out->piece[i].ty = ty;
    }
}

static Type *pieces_cache[8];
static int pieces_key[8];
static int pieces_cache_n;

static void classify_publish(void) { pieces_cache_n = 0; }

static Type *amd64_pieces_type(Type *agg) {
    if (!agg || (agg->kind != TY_STRUCT && agg->kind != TY_UNION)) return NULL;
    AggClass c;
    amd64_classify_aggregate(agg, &c);
    if (c.npiece == 0) return NULL;
    // One piece is not a record: the value travels as the piece itself, so
    // an all-floating piece must reach the SSE registers and an integer one
    // the general-purpose registers. Wrapping it in a record would send the
    // floating case to the wrong file.
    if (c.npiece == 1) return c.piece[0].ty;

    int key = c.npiece;
    for (int i = 0; i < c.npiece; i++) key = key * 31 + (is_flonum(c.piece[i].ty) ? 1 : 0) * 16 + c.piece[i].size;
    for (int i = 0; i < pieces_cache_n; i++)
        if (pieces_key[i] == key) return pieces_cache[i];

    Type *ty = emalloc(sizeof(Type));
    ty->kind = TY_STRUCT;
    ty->size = c.npiece * 8;
    ty->align = 8;
    Member *tail = NULL;
    for (int i = 0; i < c.npiece; i++) {
        Member *m = emalloc(sizeof(Member));
        m->ty = c.piece[i].ty;
        m->offset = i * 8;
        m->align = m->ty->align;
        m->idx = i;
        if (tail)
            tail = tail->next = m;
        else
            ty->members = tail = m;
    }
    ty->uid = 0;
    if (pieces_cache_n < 8) {
        pieces_key[pieces_cache_n] = key;
        pieces_cache[pieces_cache_n++] = ty;
    }
    return ty;
}

// One C parameter becomes one IR parameter, unless the ABI flattens an
// aggregate into one per register piece. A pointer is never flattened.
static int amd64_param_slots(Type *ty) {
    if (!ty || (ty->kind != TY_STRUCT && ty->kind != TY_UNION)) return 1;
    AggClass c;
    amd64_classify_aggregate(ty, &c);
    return c.npiece == 0 ? 1 : c.npiece;
}

// An aggregate whose pieces split between the two register files. The
// general-purpose offset lives at field 0 (bounded by 40) and the
// floating-point one at field 4 (bounded by 304, the end of the SSE area);
// reg_field 3 and fp_reg_field 4 name the two save areas.
static VaArgOps va_arg_mixed = {
    .kind = VA_MEM_MIXED,
    .offset_ty = &ty_uint_,
    .offset_field = 0,
    .offset_bound = 40,
    .reg_field = 3,
    .mem_field = 2,
    // Both register files save into one block: reg_save_area holds the
    // general-purpose registers and then the SSE ones, so the two cursors
    // read the same field with different offsets. fp_offset is the second
    // field of the va_list, gp_offset the first.
    .fp_offset_field = 1,
    .fp_reg_field = 3,
    .fp_offset_bound = 304,
    .reg_step = 8,
    .mem_step = 8,
};

static VaArgOps *amd64_va_arg_mixed(void) { return &va_arg_mixed; }

// A record larger than two eightbytes is MEMORY class: it is always passed
// on the stack, so the register save area never holds it and the offset test
// the other classes use would pick the wrong branch.
static VaArgOps va_arg_overflow = {
    .kind = VA_MEM_OVERFLOW,
    .offset_ty = &ty_uint_,
    .offset_field = 0,
    .reg_field = 3,
    .mem_field = 2,
    .mem_align = 8,
};

// Which register class a requested type is passed in.
// Two SSE pieces: both eightbytes come from the floating-point area and the
// cursor steps by the record's whole width. struct { double; double } is the
// common case.
static VaArgOps va_arg_fp16 = {
    .kind = VA_MEM_REGS,
    .offset_ty = &ty_uint_,
    .offset_field = 1,
    .offset_bound = 304,
    .reg_field = 3,
    .mem_field = 2,
    .reg_step = 16,
    .mem_step = 16,
    .mem_align = 8,
};

static VaArgOps *amd64_va_arg(Type *want) {
    // Records come first. The scalar rules below would otherwise claim a
    // record wider than one eightbyte and read it as an integer pair, and a
    // record larger than two eightbytes is MEMORY class -- always on the
    // stack -- so the offset test the register classes use would send it to
    // the register save area, where it never is.
    if (want->kind == TY_STRUCT || want->kind == TY_UNION) {
        AggClass c;
        amd64_classify_aggregate(want, &c);
        if (c.npiece == 0) return &va_arg_overflow;
        // A single floating-point piece travels in the SSE registers exactly
        // as a scalar of that type does: struct { double } arrives as a
        // double, so it has to be read from the same save area.
        if (c.npiece == 1 && is_flonum(c.piece[0].ty)) return &va_arg_fp;
        // Two pieces in different register files need both cursors, and the
        // two cursors live in different fields of the va_list.
        if (c.npiece == 2 && is_flonum(c.piece[0].ty) != is_flonum(c.piece[1].ty)) return &va_arg_mixed;
        // Both pieces integer, or both floating: one file, and the offset
        // steps by the record's whole width.
        if (c.npiece == 2) return is_flonum(c.piece[0].ty) ? &va_arg_fp16 : &va_arg_gp16;
        return &va_arg_gp;
    }
    // An integer type wider than one eightbyte takes two integer registers,
    // so the running offset steps by 16 rather than 8. Its alignment is
    // still 8, which is why the align test below does not already cover it.
    if (want->size > 8 && !is_flonum(want)) return &va_arg_gp16;
    // Order matters: long double and _Float128 are floating types *and*
    // 16-byte aligned, and SysV AMD64 puts both in the X87/SSEUP class,
    // which is always passed in memory -- never in the SSE register save
    // area. Testing is_flonum() first sent them to the fp path, which read
    // the wrong area (and made the mem16 entry unreachable).
    if (want->align > 8) return &va_arg_mem16;
    if (is_flonum(want)) return &va_arg_fp;
    return &va_arg_gp;
}

// stdarg.h declares `typedef __builtin_va_list va_list;` and nothing more.
// The layout is the ABI's: a register save area, the offsets into it, and
// the overflow area. It is an array of one so that passing a va_list
// passes a pointer to the element, exactly as the ABI requires.
static Type *amd64_va_list_type(void) {
    static Type elem;
    static Type arr;
    static bool done;
    if (!done) {
        elem.kind = TY_STRUCT;
        elem.size = 24;
        elem.align = 8;
        elem.is_unsigned = true;
        Member *m;

        m = emalloc(sizeof(Member));
        m->ty = &ty_uint_;
        m->offset = 0;
        m->align = 4;
        m->name = NULL;
        elem.members = m;

        m->next = emalloc(sizeof(Member));
        m = m->next;
        m->ty = &ty_uint_;
        m->offset = 4;
        m->align = 4;

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
        m->next = NULL;

        arr.kind = TY_ARRAY;
        arr.base = &elem;
        arr.len = 1;
        arr.size = elem.size;
        arr.align = elem.align;

        done = true;
    }
    return &arr;
}

#undef TYPE

Target T_amd64 = {
    .llvm_features = NULL,
    .llvm_abi = NULL,
    .ldouble_is_fp80 = true,
    .name = "amd64",
    .triple = "x86_64-linux-gnu",
    .datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128",
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
    .ty_wchar = &ty_int_,
    .int_max = 2147483647,
    .uint_max = 4294967295U,
    .long_max = 9223372036854775807L,
    .ulong_max = 18446744073709551615UL,
    .llong_max = 9223372036854775807LL,
    .va_list_type = amd64_va_list_type,
    .va_arg_ops = amd64_va_arg,
    .classify_aggregate = amd64_classify_aggregate,
    .classify_publish = classify_publish,
    .pieces_type = amd64_pieces_type,
    .va_arg_ops_for_mixed = amd64_va_arg_mixed,
    .abi_param_slots = amd64_param_slots,
    .predef =
        "#define _LP64 1\n"
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
        "#define __FLOAT128__ 1\n"
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
        "#define __LDBL_DECIMAL_DIG__ 21\n"
        "#define __LDBL_DENORM_MIN__ 3.64519953188247460253e-4951L\n"
        "#define __LDBL_DIG__ 18\n"
        "#define __LDBL_EPSILON__ 1.08420217248550443401e-19L\n"
        "#define __LDBL_HAS_DENORM__ 1\n"
        "#define __LDBL_HAS_INFINITY__ 1\n"
        "#define __LDBL_HAS_QUIET_NAN__ 1\n"
        "#define __LDBL_MANT_DIG__ 64\n"
        "#define __LDBL_MAX_10_EXP__ 4932\n"
        "#define __LDBL_MAX_EXP__ 16384\n"
        "#define __LDBL_MAX__ 1.18973149535723176502e+4932L\n"
        "#define __LDBL_MIN_10_EXP__ (-4931)\n"
        "#define __LDBL_MIN_EXP__ (-16381)\n"
        "#define __LDBL_MIN__ 3.36210314311209350626e-4932L\n"
        "#define __LDBL_NORM_MAX__ 1.18973149535723176502e+4932L\n"
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
        "#define __REGISTER_PREFIX__ \n"
        "#define __SCHAR_MAX__ 127\n"
        "#define __SHRT_MAX__ 32767\n"
        "#define __SHRT_WIDTH__ 16\n"
        "#define __SIG_ATOMIC_MAX__ 2147483647\n"
        "#define __SIG_ATOMIC_WIDTH__ 32\n"
        "#define __SIZEOF_DOUBLE__ 8\n"
        "#define __SIZEOF_FLOAT128__ 16\n"
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
        "#define __STDC_NO_COMPLEX__ 1\n"
        "#define __STDC_UTF_16__ 1\n"
        "#define __STDC_UTF_32__ 1\n"
        "#define __STDC_VERSION__ 202311L\n"
        // GNU compatibility level. glibc's headers branch on __GNUC_PREREQ,
        // so declaring a GNU version steers them away from writing their own
        // fallbacks -- notably `typedef float _Float32;` in
        // bits/floatn-common.h, which would clash with cxx's _FloatN
        // keywords (C23 H.5.1). __GNUC_MINOR__ must be defined too:
        // __GNUC_PREREQ is a function-like macro here, so a bare __GNUC__
        // makes it expand to something non-callable.
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
        "#define __WCHAR_MAX__ 2147483647\n"
        "#define __WCHAR_TYPE__ int\n"
        "#define __WCHAR_WIDTH__ 32\n"
        "#define __WINT_MAX__ 4294967295U\n"
        "#define __WINT_TYPE__ unsigned int\n"
        "#define __WINT_UNSIGNED__ 1\n"
        "#define __WINT_WIDTH__ 32\n"
        "#define __amd64 1\n"
        "#define __amd64__ 1\n"
        "#define __code_model_small__ 1\n"
        "#define __cxx__ 1\n"
        "#define __gnu_linux__ 1\n"
        "#define __k8 1\n"
        "#define __k8__ 1\n"
        "#define __linux 1\n"
        "#define __linux__ 1\n"
        "#define __pic__ 2\n"
        "#define __pie__ 2\n"
        "#define __tune_k8__ 1\n"
        "#define __unix 1\n"
        "#define __unix__ 1\n"
        "#define __x86_64 1\n"
        "#define __x86_64__ 1\n"
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
