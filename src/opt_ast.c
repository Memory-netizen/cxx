#include "cxx.h"

// Returns true if node is an integer constant. All widths share the
// Int128 storage (node->ival).
static bool is_int_const(Node *node) { return node && node->kind == ND_NUM && is_integer(node->ty); }

// Returns true if node is a floating constant of any format. All of
// them share the Fp128 storage (node->fpval, already rounded to the
// declared format).
static bool is_fp_const(Node *node) { return node && node->kind == ND_NUM && is_flonum(node->ty); }

// Returns true if node is a pointer constant.
static bool is_ptr_const(Node *node) {
    return node && node->kind == ND_EXCAST && is_pointer(node->ty) && is_int_const(node->lhs);
}

// The truth value of x as an int, i.e. `x != 0`.
//
// The tree is the one the parser builds for a written-out `x != 0`, so a
// folded `1 && x` is indistinguishable from the source form; it lowers to
// two IR instructions (icmp/fcmp + zext) where a double negation costs
// four, and it matches clang's -O0 output exactly for integral and
// floating operands.
//
// The zero goes through an ND_IMCAST to the operand's own type, which is
// what makes the constant come out right for every operand: a pointer
// compares against `null`, a floating operand against 0.0 (see cast() in
// irgen.c -- an integer 0 next to an IR `ptr` would not be valid IR).
static Node *new_truth(Node *x) {
    Node *zero = emalloc(sizeof(Node));
    zero->kind = ND_NUM;
    zero->ty = T.ty_int;
    zero->tok = x->tok;

    Node *cast = emalloc(sizeof(Node));
    cast->kind = ND_IMCAST;
    cast->lhs = zero;
    cast->ty = x->ty;
    cast->tok = x->tok;

    Node *node = emalloc(sizeof(Node));
    node->kind = ND_NE;
    node->lhs = x;
    node->rhs = cast;
    node->ty = T.ty_int;
    node->tok = x->tok;
    return node;
}

// Create a folded integer constant node. The value is normalized to
// the declared width (_BitInt uses its bit width, standard types their
// storage width), so truncation, sign extension and wrap-around all
// follow from the type.
static Node *folded_int(Int128 v, Type *ty, Node *tmpl) {
    Node *node = emalloc(sizeof(Node));
    node->kind = ND_NUM;
    node->ty = ty;
    node->tok = tmpl->tok;
    int width = (ty->kind & TY_BITINT) ? bitint_width(ty) : ty->size * 8;
    node->ival = int128_normalize(v, width, ty->is_unsigned ? UNSIGNED : SIGNED);
    return node;
}

// Fold __builtin_bswapN when the argument is an integer constant. A byte
// swap is a pure bit permutation, so the result is computed exactly here
// rather than left to the llvm.bswap.iN intrinsic.
//
// result = OR over i of ((v >> 8*i) & 0xff) << (8*(bytes - 1 - i))
static Node *fold_bswap(Node *call) {
    Node *arg = call->args;
    if (!is_int_const(arg)) return NULL;

    int bits = call->ty->size * 8;
    int bytes = bits / 8;

    Int128 v = arg->ival;
    Int128 mask = int128_set_ui(0xff);
    Int128 acc = int128_set_ui(0);
    for (int i = 0; i < bytes; i++) {
        Int128 byte = int128_and(int128_shr(v, 8 * i, UNSIGNED), mask);
        acc = int128_or(acc, int128_shl(byte, 8 * (bytes - 1 - i)));
    }
    return folded_int(acc, call->ty, call);
}

// Fold __builtin_clz/ctz/popcount and the l/ll variants. All return int.
// The argument has already been converted to the declared parameter type,
// so that type's width says how many bits are counted.
static Node *fold_bitcount(Node *call, int kind) {
    Node *arg = call->args;
    if (!is_int_const(arg)) return NULL;

    int width = arg->ty->size * 8;
    // The operand is an unsigned n-bit value; clear anything above it so a
    // negative argument does not count bits the type does not have.
    Int128 v = int128_normalize(arg->ival, width, UNSIGNED);

    int r;
    switch (kind) {
        case BUILTIN_CLZ:
        case BUILTIN_CLZL:
        case BUILTIN_CLZLL:
            // bit_width() has no zero case (it reports 1), and the builtin
            // specifies clz(0) == width, so zero is answered directly.
            r = int128_is_zero(v) ? width : width - int128_bit_width(v, UNSIGNED);
            break;
        case BUILTIN_CTZ:
        case BUILTIN_CTZL:
        case BUILTIN_CTZLL:
            r = width;
            for (int i = 0; i < width; i++)
                if (int128_and(int128_lshr(v, i), int128_one).limb[0] & 1u) {
                    r = i;
                    break;
                }
            break;
        default: {
            int n = 0;
            for (int limb = 0; limb < 4; limb++) n += __builtin_popcount(v.limb[limb]);
            r = n;
            break;
        }
    }
    return folded_int(int128_set_i(r), call->ty, call);
}

// Constant-fold __builtin_ffs, __builtin_parity and __builtin_clrsb. The
// argument has already been converted to the builtin's parameter type, so
// its width is the one the sequences count in; the result is always int.
static Node *fold_scan(Node *call, int kind) {
    Node *arg = call->args;
    if (!is_int_const(arg)) return NULL;

    int width = arg->ty->size * 8;
    bool is_ffs = kind == BUILTIN_FFS || kind == BUILTIN_FFSL || kind == BUILTIN_FFSLL;
    bool is_parity = kind == BUILTIN_PARITY || kind == BUILTIN_PARITYL || kind == BUILTIN_PARITYLL;

    int r;
    if (is_ffs) {
        // ffs counts from the low end, where sign extension makes no
        // difference, and answers zero for a zero operand.
        uint64_t v = (uint64_t)int128_to_i64(int128_normalize(arg->ival, width, SIGNED));
        r = v ? __builtin_ctzll(v) + 1 : 0;
    } else if (is_parity) {
        uint64_t v = (uint64_t)int128_to_i64(int128_normalize(arg->ival, width, UNSIGNED));
        r = __builtin_popcountll(v) & 1;
    } else {
        // clrsb counts the bits equal to the sign bit, not counting the
        // sign bit itself: 31 for both 0 and -1 in a 32-bit operand, 30
        // for 1, 0 for INT_MIN. Spreading the sign bit across the width
        // gives a mask agreeing with v exactly on those bits, so the
        // highest bit where the two differ is one past the count; when
        // they never differ (0 and -1) the count is the width less one.
        Int128 v = int128_normalize(arg->ival, width, SIGNED);
        Int128 sign = int128_ashr(v, width - 1);
        Int128 differs = int128_xor(v, sign);
        r = int128_is_zero(differs) ? width - 1 : width - int128_bit_width(differs, UNSIGNED) - 1;
    }
    return folded_int(int128_set_i(r), call->ty, call);
}

// Constant-fold a call to a builtin, or return NULL when it cannot be
// folded (a non-constant argument, or a builtin with no folder yet).
// Arguments have already been folded by the caller, so this only has to
// look at their values.
Node *fold_builtin_call(int kind, Node *call) {
    switch (kind) {
        case BUILTIN_BSWAP16:
        case BUILTIN_BSWAP32:
        case BUILTIN_BSWAP64:
            return fold_bswap(call);
        // The level is an immarg: LLVM wants a literal in that operand, and
        // gcc asks for a constant integer as well. Saying so here is the
        // difference between a diagnostic and an LLVM complaint about the IR
        // cxx handed it. Neither folds to a constant -- the frame's address
        // is only known at run time -- so both answer NULL.
        case BUILTIN_FRAME_ADDRESS:
        case BUILTIN_RETURN_ADDRESS: {
            Node *level = call->args;
            if (!level || level->kind != ND_NUM)
                error(level ? level->tok : call->tok, "argument to ‘%s’ must be a constant integer",
                      BUILTIN_ROW(kind)->name);
            return NULL;
        }
        case BUILTIN_CLZ:
        case BUILTIN_CLZL:
        case BUILTIN_CLZLL:
        case BUILTIN_CTZ:
        case BUILTIN_CTZL:
        case BUILTIN_CTZLL:
        case BUILTIN_POPCOUNT:
        case BUILTIN_POPCOUNTL:
        case BUILTIN_POPCOUNTLL:
            return fold_bitcount(call, kind);
        case BUILTIN_FFS:
        case BUILTIN_FFSL:
        case BUILTIN_FFSLL:
        case BUILTIN_PARITY:
        case BUILTIN_PARITYL:
        case BUILTIN_PARITYLL:
        case BUILTIN_CLRSB:
        case BUILTIN_CLRSBL:
        case BUILTIN_CLRSBLL:
            return fold_scan(call, kind);
        // The special-class builtins never reach here: they do not produce
        // ND_FUNCALL. Listing them keeps -Wswitch honest.
        case BUILTIN_FN_ALLOCA:
        case BUILTIN_ALLOCA_WITH_ALIGN:
        case BUILTIN_CONSTANT_P:
        case BUILTIN_TYPES_COMPATIBLE_P:
        case ATOMIC_STORE:
        case ATOMIC_LOAD:
        case ATOMIC_COMPARE_EXCHANGE_GENERIC:
        case ATOMIC_EXCHANGE:
        case ATOMIC_FETCH_ADD:
        case ATOMIC_FETCH_SUB:
        case ATOMIC_FETCH_AND:
        case ATOMIC_FETCH_OR:
        case ATOMIC_FETCH_XOR:
        case ATOMIC_COMPARE_EXCHANGE_WEAK:
        case ATOMIC_COMPARE_EXCHANGE_STRONG:
        case ATOMIC_THREAD_FENCE:
        case ATOMIC_SIGNAL_FENCE:
        case ATOMIC_IS_LOCK_FREE:
        case BUILTIN_NONE:
            return NULL;
    }
    return NULL;
}

// Fold a binary integer node. Both operands are Int128 constants of
// any width; the arithmetic runs in the Int128 domain and folded_int
// normalizes the result to the declared width.
static Node *fold_binary_int(Node *node) {
    Node *lhs = node->lhs;
    Node *rhs = node->rhs;
    if (!is_int_const(lhs) || !is_int_const(rhs)) return NULL;

    Int128 l = lhs->ival, r = rhs->ival;
    bool unsig = lhs->ty->is_unsigned;
    SignKind sign = unsig ? UNSIGNED : SIGNED;
    int width = (lhs->ty->kind & TY_BITINT) ? bitint_width(lhs->ty) : lhs->ty->size * 8;

    switch (node->kind) {
        case ND_ADD:
            return folded_int(int128_add(l, r), lhs->ty, node);
        case ND_SUB:
            return folded_int(int128_sub(l, r), lhs->ty, node);
        case ND_MUL:
            return folded_int(int128_mul(l, r), lhs->ty, node);
        case ND_DIV:
            if (int128_is_zero(r)) return NULL;
            return folded_int(unsig ? int128_div_unsigned(l, r) : int128_div_signed(l, r), lhs->ty, node);
        case ND_MOD:
            if (int128_is_zero(r)) return NULL;
            return folded_int(unsig ? int128_mod_unsigned(l, r) : int128_mod_signed(l, r), lhs->ty, node);
        case ND_BAND:
            return folded_int(int128_and(l, r), lhs->ty, node);
        case ND_BOR:
            return folded_int(int128_or(l, r), lhs->ty, node);
        case ND_XOR:
            return folded_int(int128_xor(l, r), lhs->ty, node);
        case ND_LEFT:
        case ND_RIGHT: {
            int64_t sh = int128_to_i64(r);
            if (sh < 0 || sh >= width) return NULL;
            if (node->kind == ND_RIGHT) return folded_int(int128_shr(l, (int)sh, sign), lhs->ty, node);
            // Don't fold a signed left shift that overflows a standard
            // type (UB); _BitInt and unsigned wrap bitwise.
            if (!unsig && !(lhs->ty->kind & TY_BITINT) && sh > 0 &&
                int128_cmp_signed(int128_ashr(int128_shl(l, (int)sh), (int)sh), l) != 0)
                return NULL;
            return folded_int(int128_shl(l, (int)sh), lhs->ty, node);
        }
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
        case ND_GT:
        case ND_GE: {
            int c = int128_cmp(l, r, sign);
            bool res = node->kind == ND_EQ   ? c == 0
                       : node->kind == ND_NE ? c != 0
                       : node->kind == ND_LT ? c < 0
                       : node->kind == ND_LE ? c <= 0
                       : node->kind == ND_GT ? c > 0
                                             : c >= 0;
            return folded_int(int128_set_i(res), T.ty_int, node);
        }
        default:
            return NULL;
    }
}

// Create a folded fpval constant node (interchange types and long
// double), rounded once to the target format.
static Node *folded_fp128(Fp128 v, Type *ty, Node *tmpl) {
    Node *node = emalloc(sizeof(Node));
    node->kind = ND_NUM;
    node->ty = ty;
    node->tok = tmpl->tok;
    node->fpval = fp128_round_to(v, fmt_of(ty));
    return node;
}

// Fold a binary floating-point node of any format, computed in the
// binary128 domain and rounded once to the operand type. All float
// constants are stored as Fp128 already rounded to their format;
// division goes through the 113-bit intermediate.
static Node *fold_binary_fp(Node *node) {
    Node *lhs = node->lhs;
    Node *rhs = node->rhs;
    if (!is_fp_const(lhs) || !is_fp_const(rhs)) return NULL;

    Fp128 l = lhs->fpval, r = rhs->fpval;
    switch (node->kind) {
        case ND_ADD:
            return folded_fp128(fp128_add(l, r), lhs->ty, node);
        case ND_SUB:
            return folded_fp128(fp128_sub(l, r), lhs->ty, node);
        case ND_MUL:
            return folded_fp128(fp128_mul(l, r), lhs->ty, node);
        case ND_DIV:
            if (fp128_is_zero(r)) return NULL;  // leave NaN/Inf to codegen
            return folded_fp128(fp128_div_rounded(l, r, fmt_of(lhs->ty)), lhs->ty, node);
        case ND_EQ:
            return folded_int(int128_set_i(fp128_cmp(l, r) == 0), T.ty_int, node);
        case ND_NE:
            return folded_int(int128_set_i(fp128_cmp(l, r) != 0), T.ty_int, node);
        case ND_LT:
            return folded_int(int128_set_i(fp128_cmp(l, r) == -1), T.ty_int, node);
        case ND_LE:
            return folded_int(int128_set_i(fp128_cmp(l, r) == -1 || fp128_cmp(l, r) == 0), T.ty_int, node);
        case ND_GT:
            return folded_int(int128_set_i(fp128_cmp(l, r) == 1), T.ty_int, node);
        case ND_GE:
            return folded_int(int128_set_i(fp128_cmp(l, r) == 0 || fp128_cmp(l, r) == 1), T.ty_int, node);
        default:
            return NULL;
    }
}

// Fold a unary floating-point node of any format in the Fp128 domain.
static Node *fold_unary_fp(Node *node) {
    Node *lhs = node->lhs;
    if (!is_fp_const(lhs)) return NULL;
    switch (node->kind) {
        case ND_PLUS:
            return folded_fp128(lhs->fpval, lhs->ty, node);
        case ND_NEG:
            return folded_fp128(fp128_neg(lhs->fpval), lhs->ty, node);
        default:
            return NULL;
    }
}

// Fold a unary integer node in the Int128 domain (all widths).
static Node *fold_unary_int(Node *node) {
    Node *lhs = node->lhs;
    if (!is_int_const(lhs)) return NULL;

    Int128 v = lhs->ival;
    switch (node->kind) {
        case ND_PLUS:
            return folded_int(v, lhs->ty, node);
        case ND_NEG:
            // Don't fold unsigned negation of standard types — the
            // result depends on the promotion rules; _BitInt wraps.
            if (lhs->ty->is_unsigned && !(lhs->ty->kind & TY_BITINT)) return NULL;
            return folded_int(int128_neg(v), lhs->ty, node);
        case ND_NOT:
            return folded_int(int128_set_i(int128_is_zero(v)), T.ty_int, node);
        case ND_INVERT:
            return folded_int(int128_not(v), lhs->ty, node);
        default:
            return NULL;
    }
}

// Fold a cast node (IMCAST or EXCAST) where the inner expression is
// a constant. Integer conversions just re-interpret the Int128 value
// at the target width (folded_int normalizes); float conversions round
// once to the target format.
// Does the value fit the range of an integer type? Unlike the integer-to-
// integer rule above, a negative value never fits an unsigned type here:
// 6.3.1.4 leaves the floating conversions that reach this undefined rather
// than modular, which is why clang calls both out of range.
bool fits_target(Int128 v, Type *ty) {
    int width = (ty->kind & TY_BITINT) ? bitint_width(ty) : ty->size * 8;
    SignKind sign = ty->is_unsigned ? UNSIGNED : SIGNED;
    if (ty->is_unsigned && int128_is_negative(v)) return false;
    return int128_cmp(int128_normalize(v, width, sign), v, sign) == 0;
}

// The two values a conversion changed. The compiler runs on the host and
// its own diagnostics print floating values through the host's printf, as
// the AST dumper does; that is far more precision than these name.
static char *conv_int_str(Int128 v, SignKind sign) {
    char buf[64];
    int128_to_str(v, sign, 10, buf, sizeof(buf));
    return format("%s", buf);
}

// Enough digits for the type the value belongs to: nine for a float, which
// is where its shortest round trip lives, and seventeen for the wider ones.
// A coarser setting prints `float f = 1.1;` as "from 1.1 to 1.1".
// The range of an integer type, as the two decimal values the note below
// prints. Saying "this is undefined" without saying what would fit leaves
// the reader to work it out; the type's own limits are the answer.
static void conv_int_limits(Type *ty, Int128 *min, Int128 *max) {
    int width = (ty->kind & TY_BITINT) ? bitint_width(ty) : ty->size * 8;
    Int128 unit = int128_set_i(1);
    if (ty->is_unsigned) {
        *min = int128_set_i(0);
        *max = width >= 128 ? int128_set_i(-1) : int128_sub(int128_shl(unit, width), unit);
    } else {
        *max = int128_sub(int128_shl(unit, width - 1), unit);
        *min = int128_neg(int128_shl(unit, width - 1));
    }
}

static char *conv_int_range_str(Type *ty) {
    Int128 min, max;
    conv_int_limits(ty, &min, &max);
    SignKind sign = ty->is_unsigned ? UNSIGNED : SIGNED;
    return format("%s to %s", conv_int_str(min, sign), conv_int_str(max, sign));
}

// The note an out-of-range conversion prints: what the target holds, and --
// unless the source is a NaN, which is near nothing -- the value closest to
// the one that was written. gcc prints the same saturated value ("changes
// value from 1.0e+20 to 2147483647"); after a refusal it is the one number
// that answers "so what do I write instead?".
static char *conv_oor_note(Fp128 v, Type *ty) {
    if (fp128_is_nan(v)) return format("\u2018%s\u2019 holds %s", diag_ty_name(ty), conv_int_range_str(ty));
    Int128 min, max;
    conv_int_limits(ty, &min, &max);
    SignKind sign = ty->is_unsigned ? UNSIGNED : SIGNED;
    bool ok;
    Int128 t = fp128_to_int128(v, sign, &ok);
    Int128 nearest;
    if (!ok)
        nearest = fp128_get_sign(v) ? min : max;  // past even Int128: the far end
    else if (int128_cmp(t, min, sign) < 0)
        nearest = min;
    else if (int128_cmp(t, max, sign) > 0)
        nearest = max;
    else
        nearest = t;
    return format("\u2018%s\u2019 holds %s; the nearest representable value is %s", diag_ty_name(ty),
                  conv_int_range_str(ty), conv_int_str(nearest, sign));
}

static char *conv_fp_str(Fp128 v, Type *ty) {
    if (fp128_is_nan(v)) return format("%snan", fp128_get_sign(v) ? "-" : "");
    if (fp128_is_inf(v)) return format("%sinf", fp128_get_sign(v) ? "-" : "");
    uint64_t bits = fp128_to_fp64_bits(v);
    double d;
    memcpy(&d, &bits, sizeof(d));
    return format("%.*g", ty->kind == TY_FLOAT ? 9 : 17, d);
}

// Rounding a constant to its target type is where C's conversions become
// visible, so this is where gcc and clang warn about the ones that change
// the value. Explicit casts are the programmer saying so, and only implicit
// conversions (ND_IMCAST) are diagnosed.
static Node *fold_cast(Node *node) {
    Node *lhs = node->lhs;
    bool implicit = node->kind == ND_IMCAST;

    // int → int
    if (is_int_const(lhs) && is_integer(node->ty)) {
        if (is_bool(node->ty)) return folded_int(int128_set_i(!int128_is_zero(lhs->ival)), T.ty_bool, node);
        if (implicit && lhs->ty->kind != TY_BOOL) {
            // 6.3.1.3: the value has to be representable in the target type.
            // A negative value and an unsigned target are measured against
            // that type's *signed* range, which is what leaves the
            // `unsigned u = -1;` idiom alone while `unsigned char c = -300;`
            // loses bits -- gcc and clang agree on both.
            int width = (node->ty->kind & TY_BITINT) ? bitint_width(node->ty) : node->ty->size * 8;
            SignKind sign = node->ty->is_unsigned ? (int128_is_negative(lhs->ival) ? SIGNED : UNSIGNED) : SIGNED;
            Int128 target = int128_normalize(lhs->ival, width, sign);
            if (int128_cmp(target, lhs->ival, sign) != 0)
                warning(WG_CONSTANT_CONVERSION, node->tok,
                        "implicit conversion from \u2018%s\u2019 to \u2018%s\u2019 changes value from %s to %s",
                        diag_ty_name(lhs->ty), diag_ty_name(node->ty),
                        conv_int_str(lhs->ival, lhs->ty->is_unsigned ? UNSIGNED : SIGNED), conv_int_str(target, sign));
        }
        return folded_int(lhs->ival, node->ty, node);
    }

    // int → any float: round once to the target format. This is the one
    // conversion whose loss is a *constant* fact -- the program never has the
    // integer it wrote -- so it has its own group and that group is on by
    // default, which is where clang keeps the same check.
    if (is_int_const(lhs) && is_flonum(node->ty)) {
        Fp128 v = fp128_from_int128(lhs->ival, lhs->ty->is_unsigned ? UNSIGNED : SIGNED);
        Fp128 rounded = fp128_round_to(v, fmt_of(node->ty));
        if (implicit && fp128_cmp(rounded, v) != 0 && !fp128_is_nan(v))
            warning(
                WG_CONST_INT_FLOAT_CONVERSION, node->tok,
                "conversion from \u2018%s\u2019 to \u2018%s\u2019 changes value from \u2018%s\u2019 to \u2018%s\u2019",
                diag_ty_name(lhs->ty), diag_ty_name(node->ty),
                conv_int_str(lhs->ival, lhs->ty->is_unsigned ? UNSIGNED : SIGNED), conv_fp_str(rounded, node->ty));
        return folded_fp128(v, node->ty, node);
    }

    // float → float: round once to the target format
    if (is_fp_const(lhs) && is_flonum(node->ty)) {
        Fp128 rounded = fp128_round_to(lhs->fpval, fmt_of(node->ty));
        if (implicit && !fp128_is_nan(lhs->fpval) && fp128_cmp(rounded, lhs->fpval) != 0)
            warning(
                WG_FLOAT_CONVERSION, node->tok,
                "conversion from \u2018%s\u2019 to \u2018%s\u2019 changes value from \u2018%s\u2019 to \u2018%s\u2019",
                diag_ty_name(lhs->ty), diag_ty_name(node->ty), conv_fp_str(lhs->fpval, lhs->ty),
                conv_fp_str(rounded, node->ty));
        return folded_fp128(lhs->fpval, node->ty, node);
    }

    // float → int: truncate toward zero, range-checked
    if (is_fp_const(lhs) && is_integer(node->ty)) {
        if (is_bool(node->ty)) return folded_int(int128_set_i(!fp128_is_zero(lhs->fpval)), T.ty_bool, node);
        bool ok;
        Int128 v = fp128_to_int128(lhs->fpval, node->ty->is_unsigned ? UNSIGNED : SIGNED, &ok);
        if (!ok || !fits_target(v, node->ty)) {
            // A static initializer has no value to be initialised with, so
            // it is refused; inside a function this is the warning clang has
            // on by default and gcc behind -Wconversion. Either way the
            // reader is told what the target type can hold: a refusal that
            // does not say what would fit leaves the choice to guesswork.
            if (in_static_init) {
                diag("error", node->tok,
                     "conversion of out of range value from \u2018%s\u2019 to \u2018%s\u2019 is undefined",
                     diag_ty_name(lhs->ty), diag_ty_name(node->ty));
                diag_exit("note", node->tok, "%s", conv_oor_note(lhs->fpval, node->ty));
            } else if (implicit && wg_enabled(WG_LITERAL_CONVERSION)) {
                warning(WG_LITERAL_CONVERSION, node->tok,
                        "implicit conversion of out of range value from \u2018%s\u2019 to \u2018%s\u2019 is undefined",
                        diag_ty_name(lhs->ty), diag_ty_name(node->ty));
                diag("note", node->tok, "%s", conv_oor_note(lhs->fpval, node->ty));
            }
        } else if (implicit && !fp128_is_nan(lhs->fpval) &&
                   fp128_cmp(fp128_from_int128(v, node->ty->is_unsigned ? UNSIGNED : SIGNED), lhs->fpval) != 0) {
            warning(WG_LITERAL_CONVERSION, node->tok,
                    "implicit conversion from \u2018%s\u2019 to \u2018%s\u2019 changes value from %s to %s",
                    diag_ty_name(lhs->ty), diag_ty_name(node->ty), conv_fp_str(lhs->fpval, lhs->ty),
                    conv_int_str(v, node->ty->is_unsigned ? UNSIGNED : SIGNED));
        }
        if (!ok) return NULL;
        return folded_int(v, node->ty, node);
    }

    // pointer → int
    if (is_integer(node->ty)) {
        if (lhs->kind == ND_EXCAST || lhs->kind == ND_IMCAST) {
            if (is_pointer(lhs->ty) && is_integer(lhs->lhs->ty)) {
                if (is_int_const(lhs->lhs)) return folded_int(lhs->lhs->ival, node->ty, node);
            }
        }
    }
    return NULL;
}

// Fold a conditional (?:) node where the condition is a constant.
static Node *fold_cond(Node *node) {
    if (!is_int_const(node->cond)) return NULL;
    return !int128_is_zero(node->cond->ival) ? node->then : node->els;
}

// Fold a logical AND/OR where one side is a constant.
// 0 && x → 0,  1 && x → x != 0,  0 || x → x != 0,  1 || x → 1
static bool const_truthy(Node *n) {
    if (is_int_const(n)) return !int128_is_zero(n->ival);
    return !fp128_is_zero(n->fpval);  // float const: ±0.0 is false
}

static Node *fold_logical(Node *node) {
    Node *lhs = node->lhs;
    if (!is_int_const(lhs) && !is_fp_const(lhs)) return NULL;
    Node *rhs = node->rhs;
    if (is_int_const(rhs) || is_fp_const(rhs)) {
        bool lv = const_truthy(lhs);
        bool rv = const_truthy(rhs);
        if (node->kind == ND_LOGAND)
            return folded_int(int128_set_i(lv && rv), T.ty_int, node);
        else
            return folded_int(int128_set_i(lv || rv), T.ty_int, node);
    }

    // The surviving operand is not the result: && and || yield an int that
    // is 0 or 1, so x must be replaced by its truth value.
    if (node->kind == ND_LOGAND)
        return const_truthy(lhs) ? new_truth(node->rhs) : folded_int(int128_set_i(0), T.ty_int, node);
    else  // ND_LOGOR
        return const_truthy(lhs) ? folded_int(int128_set_i(1), T.ty_int, node) : new_truth(node->rhs);
}

// Fold a boolean conversion to an integer constant when possible.
//   (bool)0 → 0,  (bool)5 → 1
static Node *fold_bool(Node *node) {
    Node *lhs = node->lhs;
    if (!is_int_const(lhs)) return NULL;
    if (node->ty->kind != TY_BOOL) return NULL;
    return folded_int(int128_set_i(!int128_is_zero(lhs->ival)), T.ty_bool, node);
}

static Node *fold_ptradd(Node *node) {
    Node *lhs = node->lhs;
    if (!is_ptr_const(lhs)) return NULL;
    Node *rhs = node->rhs;
    if (!is_int_const(rhs) || is_bitint128(rhs->ty)) return NULL;
    lhs->lhs->ival = int128_set_i(int128_to_i64(lhs->lhs->ival) + int128_to_i64(rhs->ival) * node->ty->base->size);
    return lhs->lhs;
}

// Recursively fold an AST subtree. Returns the folded node
// (which may be the original or a replacement).
// -Wshift-count-negative / -Wshift-count-overflow. Both are on by default
// in gcc and clang. This is the only place a count that became a constant by
// folding is visible -- `x << (2 + 1)` -- and the left operand need not be a
// constant for 6.5.7 to be violated, so only the count is inspected.
static void check_shift_count(Node *node) {
    Node *cnt = node->rhs;
    if (!is_integer(node->lhs->ty) || !cnt || cnt->kind != ND_NUM || !is_integer(cnt->ty)) return;
    int width = node->lhs->ty->size * 8;
    int64_t n = int128_to_i64(cnt->ival);
    // An unsigned count whose top bit is set reads back negative, but as an
    // unsigned value it is far larger than any width.
    bool uns = cnt->ty->is_unsigned;
    if (n < 0 && !uns)
        warning(WG_SHIFT_COUNT_NEGATIVE, node->tok, "shift count is negative");
    else if (n >= width || n < 0)
        warning(WG_SHIFT_COUNT_OVERFLOW, node->tok, "shift count >= width of type");
}

// Whether the function being folded defines any label. A goto -- an asm goto
// included -- names its target by label, so a branch may only be dropped when
// there is no label in the function for it to name. The parser keeps the ring
// on the function's Sym.
static bool fn_has_labels;

Node *fold_node(Node *node) {
    if (!node) return NULL;

    // Fold children first (bottom-up).
    switch (node->kind) {
        // Binary arithmetic
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_MOD:
        case ND_BAND:
        case ND_BOR:
        case ND_XOR:
        case ND_LEFT:
        case ND_RIGHT:
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
        case ND_GT:
        case ND_GE:
            node->lhs = fold_node(node->lhs);
            node->rhs = fold_node(node->rhs);
            // The count is a constant by now whenever it can be, whether it
            // was written as one or folded into one.
            if (node->kind == ND_LEFT || node->kind == ND_RIGHT) check_shift_count(node);
            return fold_binary_int(node) ?: fold_binary_fp(node) ?: node;
        case ND_PTRADD:
            node->lhs = fold_node(node->lhs);
            node->rhs = fold_node(node->rhs);
            return fold_ptradd(node) ?: node;
        // Unary arithmetic
        case ND_PLUS:
        case ND_NEG:
        case ND_NOT:
        case ND_INVERT:
            node->lhs = fold_node(node->lhs);
            return fold_unary_int(node) ?: fold_unary_fp(node) ?: node;

        // Cast
        case ND_IMCAST:
        case ND_EXCAST:
            node->lhs = fold_node(node->lhs);
            return fold_cast(node) ?: node;

        // Conditional
        case ND_COND:
            node->cond = fold_node(node->cond);
            node->then = fold_node(node->then);
            node->els = fold_node(node->els);
            return fold_cond(node) ?: node;

        // Logical
        case ND_LOGAND:
        case ND_LOGOR: {
            node->lhs = fold_node(node->lhs);
            node->rhs = fold_node(node->rhs);
            Node *folded = fold_logical(node);
            if (!folded) return node;
            // The replacement can carry a new subexpression (new_truth's
            // conversion of the zero), and it must end up folded like any
            // other node so the tree matches a written-out `x != 0`.
            return fold_node(folded);
        }

        // Boolean conversion (implicit cast to bool)
        case ND_LVTOR:
            node->lhs = fold_node(node->lhs);
            if (node->ty->kind == TY_BOOL) return fold_bool(node) ?: node;
            return node;

        // Comma: fold both sides, discard lhs if it has no side effects
        case ND_COMMA:
            node->lhs = fold_node(node->lhs);
            node->rhs = fold_node(node->rhs);
            // In a static initializer the comma has to survive the folding:
            // 6.7.9p4 asks for a constant expression, 6.6p3 says a constant
            // expression does not contain a comma operator, and folding it
            // to the right operand is exactly what hides that.
            // `static int x = (1, 3);` is refused, as gcc refuses it.
            // Everywhere else the fold is what turns cpython's
            // `((void)sizeof(int), 4)` into the constant it means.
            if (in_static_init) return node;
            if (is_int_const(node->lhs) || is_fp_const(node->lhs)) return node->rhs;
            // A cast to void throws the value away, and a constant under it
            // has nothing else to lose. `((void)sizeof(int), 4)` is the shape
            // cpython's Py_ARRAY_LENGTH() writes around its static assertion,
            // and clang reads it as the constant 4.
            if (node->lhs && (node->lhs->kind == ND_IMCAST || node->lhs->kind == ND_EXCAST) &&
                node->lhs->ty->kind == TY_VOID && is_int_const(node->lhs->lhs))
                return node->rhs;
            return node;

        // An asm statement's operands are read and written by the template,
        // and folding them is what turns `"i"(1 + 2)` into the constant LLVM
        // wants for an immediate constraint. An output is an lvalue, which
        // folding leaves alone.
        case ND_ASM:
            for (AsmOperand *op = node->asm_ops; op; op = op->next) op->expr = fold_node(op->expr);
            return node;

        // Statements: fold sub-expressions
        case ND_RETURN:
        case ND_EXPR_STMT:
            node->lhs = fold_node(node->lhs);
            return node;

        case ND_IF: {
            node->cond = fold_node(node->cond);
            node->then = fold_node(node->then);
            if (node->els) node->els = fold_node(node->els);
            // A condition that folded to a constant decides the branch here,
            // and only the branch that is taken is kept. Both references do
            // this: gcc's assembly for ffmpeg's
            // `if (__builtin_constant_p(s)) asm(.. "i" ..) else asm(.. "c" ..)`
            // holds just the second asm. Leaving the other in is not harmless
            // -- LLVM validates every function before it optimises, so an asm
            // whose operand is not constant for an immediate constraint fails
            // the whole module even though nothing can reach it. That is 176
            // objects of ffmpeg's build, reported without a file name.
            //
            // A branch that defines a label stays, however: a goto, including
            // one from an asm goto, can still name it.
            if (!fn_has_labels && node->cond && node->cond->kind == ND_NUM && node->cond->ty &&
                is_integer(node->cond->ty)) {
                Node *taken = int128_is_zero(node->cond->ival) ? node->els : node->then;
                if (!taken) {
                    // No else: the dead arm is the whole statement, and what
                    // is left of it is an empty one.
                    taken = new_node(ND_COMP_STMT, node->tok);
                    taken->ty = node->ty;
                }
                // The chosen arm takes the `if`'s place in place: a statement
                // list folds its `next` chain without writing the result
                // back, so returning a different node would be dropped.
                Node *next = node->next;
                *node = *taken;
                node->next = next;
            }
            return node;
        }

        case ND_WHILE:
        case ND_DO:
            node->cond = fold_node(node->cond);
            node->then = fold_node(node->then);
            if (node->els) node->els = fold_node(node->els);
            return node;

        case ND_FOR:
            if (node->init) node->init = fold_node(node->init);
            if (node->cond) node->cond = fold_node(node->cond);
            if (node->inc) node->inc = fold_node(node->inc);
            node->body = fold_node(node->body);
            return node;

        case ND_SWITCH:
            node->cond = fold_node(node->cond);
            node->body = fold_node(node->body);
            return node;

        case ND_COMP_STMT:
        case ND_STMT_EXPR:
        case ND_DECL:
            for (Node *n = node->body; n; n = n->next) fold_node(n);
            return node;

        // Assignment-like: fold rhs only
        case ND_INIT:
        case ND_AS:
        case ND_ADDAS:
        case ND_SUBAS:
        case ND_MULAS:
        case ND_DIVAS:
        case ND_MODAS:
        case ND_ANDAS:
        case ND_ORAS:
        case ND_XORAS:
        case ND_LEFTAS:
        case ND_RIGHTAS:
        case ND_PTRAS:
            node->lhs = fold_node(node->lhs);
            node->rhs = fold_node(node->rhs);
            return node;

        // Not foldable: just recurse into children
        case ND_ADDR:
        case ND_DEREF:
        case ND_MEMBER:
        case ND_PREINC:
        case ND_PREDEC:
        case ND_POSTINC:
        case ND_POSTDEC:
            node->lhs = fold_node(node->lhs);
            return node;

        case ND_FUNCALL: {
            // Fold each argument and splice the replacement node back into
            // the list. The previous version called fold_node() and threw
            // the result away, so no argument of any call was ever folded
            // (f(2 + 3) kept the add for the backend); every other case
            // here does write the result back.
            Node **p = &node->args;
            while (*p) {
                Node *folded = fold_node(*p);
                if (folded && folded != *p) {
                    folded->next = (*p)->next;
                    *p = folded;
                }
                p = &(*p)->next;
            }
            // A builtin call folds through its own routine; anything else
            // is left as an ordinary call.
            int kind = builtin_kind_of(node->func);
            if (kind) return fold_builtin_call(kind, node) ?: node;
            return node;
        }
        case ND_CASE:
        case ND_LABEL:
            node->label_body = fold_node(node->label_body);
            return node;
        case ND_GOTO_EXPR:
            break;
        case ND_LABEL_VAL:
            break;
        case ND_VAR:
            break;
        case ND_NUM:
            break;
        case ND_NULLPTR:
            break;
        case ND_SUBACCESS:
            break;
        case ND_NOP:
        case ND_MEMZERO:
        case ND_GOTO:
        case ND_BREAK:
        case ND_CONTINUE:
        case ND_SP_SAVE:
        case ND_SP_RESTORE:
        case ND_CAS:
        case ND_ATOMICRMW:
        case ND_FENCE:
        case ND_VA_COPY:
            // va_copy has no value to fold; its operands are lvalues.
            return node;
        case ND_VA_START:
        case ND_VA_END:
            // va_start/va_end have no value to fold; their operand is an
            // lvalue (the va_list), not something to constant-fold.
            return node;
        case ND_VA_ARG:
            // va_arg's result is only known at run time.
            return node;
        case ND_ALLOCA:
            node->lhs = fold_node(node->lhs);
            return node;
    }
    return node;
}

// Entry point: fold constants in the AST.
void fold_ast(Module *prog) {
    for (Sym *fn = prog->fns; fn; fn = fn->next) {
        if (!fn->is_defined) continue;
        fn_has_labels = fn->labels != NULL;
        fold_node(fn->body);
    }
}
