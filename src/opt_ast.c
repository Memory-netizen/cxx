#include "cxx.h"

// Returns true if node is an integer constant of width <= 64.
static bool is_int_const(Node *node) {
    return node && node->kind == ND_NUM && is_integer(node->ty) && !is_bitint128(node->ty);
}

// Returns true if node is a float/double/long double constant.
static bool is_float_const(Node *node) {
    return node && node->kind == ND_NUM && is_flonum(node->ty) && !is_new_flonum(node->ty);
}

// Returns true if node is an _Float16/32/64/128 constant (fpval storage).
static bool is_fp128_const(Node *node) { return node && node->kind == ND_NUM && is_new_flonum(node->ty); }

// Returns true if node is a _BitInt(65..128) constant (ival storage).
static bool is_i128_const(Node *node) { return node && node->kind == ND_NUM && is_bitint128(node->ty); }

// Returns true if node is a pointer constant.
static bool is_ptr_const(Node *node) {
    return node && node->kind == ND_EXCAST && is_pointer(node->ty) && is_int_const(node->lhs);
}

static Node *new_lognot(Node *tmpl) {
    Node *node = emalloc(sizeof(Node));
    node->kind = ND_NOT;
    node->lhs = tmpl;
    node->ty = T.ty_int;
    node->tok = tmpl->tok;
    return node;
}

// Create a folded integer constant node with the given type.
static Node *folded_int(int64_t val, Type *ty, Node *tmpl) {
    Node *node = emalloc(sizeof(Node));
    node->kind = ND_NUM;
    node->ty = ty;
    node->tok = tmpl->tok;

    if (is_integer(ty)) {
        int bits = ty->size * 8;
        if (bits > 0 && bits < 64) {
            uint64_t mask = ((uint64_t)1u << bits) - 1;
            uint64_t u = (uint64_t)val & mask;
            if (!ty->is_unsigned && (u >> (bits - 1)))
                val = (int64_t)(u | ~mask);
            else
                val = (int64_t)u;
        }
        // bits == 64: int64_t
    }

    node->val = val;
    return node;
}

// Create a folded floating-point constant node.
// Truncates to float precision when the target type is float
// (FLT_EVAL_METHOD=0 semantics: result must fit the declared type).
static Node *folded_float(double val, Type *ty, Node *tmpl) {
    Node *node = emalloc(sizeof(Node));
    node->kind = ND_NUM;
    node->ty = ty;
    node->tok = tmpl->tok;
    if (ty->kind == TY_FLOAT) val = (float)val;
    node->fval = val;
    return node;
}

// Fold a binary arithmetic node. Returns a new ND_NUM node if both
// operands are integer constants, or NULL if folding is not possible.
static Node *fold_binary(Node *node) {
    Node *lhs = node->lhs;
    Node *rhs = node->rhs;

    if (!is_int_const(lhs) || !is_int_const(rhs)) return NULL;

    int64_t l = lhs->val;
    int64_t r = rhs->val;

    // Use unsigned arithmetic when the type is unsigned, so that
    // shifts and division behave correctly for the full bit width.
    bool unsig = lhs->ty->is_unsigned;
    uint64_t ul = (uint64_t)l;
    uint64_t ur = (uint64_t)r;
    int width = lhs->ty->size * 8;

    switch (node->kind) {
        case ND_ADD:
            return folded_int(l + r, lhs->ty, node);
        case ND_SUB:
            return folded_int(l - r, lhs->ty, node);
        case ND_MUL:
            return folded_int(l * r, lhs->ty, node);
        case ND_DIV:
            if (r == 0) return NULL;
            return folded_int(unsig ? (int64_t)(ul / ur) : l / r, lhs->ty, node);
        case ND_MOD:
            if (r == 0) return NULL;
            return folded_int(unsig ? (int64_t)(ul % ur) : l % r, lhs->ty, node);
        case ND_BAND:
            return folded_int(l & r, lhs->ty, node);
        case ND_BOR:
            return folded_int(l | r, lhs->ty, node);
        case ND_XOR:
            return folded_int(l ^ r, lhs->ty, node);
        case ND_LEFT:
            if (r >= width || r < 0) return NULL;
            // Don't fold left shift if it overflows the type.
            if (!unsig && r > 0 && (ul << (r - 1)) >> (r - 1) != ul) return NULL;
            return folded_int((int64_t)(ul << r), lhs->ty, node);
        case ND_RIGHT:
            if (r >= width || r < 0) return NULL;
            // Don't fold signed right shift — the result of >> on
            // negative values is platform-dependent (arithmetic vs logical).
            if (!unsig)
                return folded_int((l >> r), lhs->ty, node);
            else
                return folded_int((int64_t)(ul >> r), lhs->ty, node);
        case ND_EQ:
            return folded_int(l == r, T.ty_int, node);
        case ND_NE:
            return folded_int(l != r, T.ty_int, node);
        case ND_LT:
            return folded_int(unsig ? (int64_t)(ul < ur) : (l < r), T.ty_int, node);
        case ND_LE:
            return folded_int(unsig ? (int64_t)(ul <= ur) : (l <= r), T.ty_int, node);
        default:
            return NULL;
    }
}

// Fold a binary floating-point node. Only folds safe operations
// (no division by zero, no overflow to infinity).
static Node *fold_binary_float(Node *node) {
    Node *lhs = node->lhs;
    Node *rhs = node->rhs;

    if (!is_float_const(lhs) || !is_float_const(rhs)) return NULL;

    double l = lhs->fval;
    double r = rhs->fval;
    double result;

    switch (node->kind) {
        case ND_ADD:
            result = l + r;
            break;
        case ND_SUB:
            result = l - r;
            break;
        case ND_MUL:
            result = l * r;
            break;
        case ND_DIV:
            if (r == 0.0) return NULL;  // unsafe: NaN/Inf/SIGFPE
            result = l / r;
            break;
        default:
            return NULL;
    }

    // Don't fold if the result overflowed to infinity.
    if (isinf(result)) return NULL;

    return folded_float(result, lhs->ty, node);
}

// Create a folded _Float16/32/64/128 constant node (fpval storage),
// rounded once to the target format.
static Node *folded_fp128(Fp128 v, Type *ty, Node *tmpl) {
    Node *node = emalloc(sizeof(Node));
    node->kind = ND_NUM;
    node->ty = ty;
    node->tok = tmpl->tok;
    node->fpval = fp128_round_to(v, fmt_of(ty));
    return node;
}

// Create a folded _BitInt(65..128) constant node (ival storage).
static Node *folded_i128(Int128 v, Type *ty, Node *tmpl) {
    Node *node = emalloc(sizeof(Node));
    node->kind = ND_NUM;
    node->ty = ty;
    node->tok = tmpl->tok;
    node->ival = int128_normalize(v, bitint_width(ty), ty->is_unsigned ? UNSIGNED : SIGNED);
    return node;
}

static Int128 i128_of_node(Node *node) {
    if (is_i128_const(node)) return node->ival;
    if (node->ty->is_unsigned) return int128_set_ui((uint64_t)node->val);
    return int128_set_i(node->val);
}

// Fold a binary node with _Float16/32/64/128 constant operands. The
// arithmetic is computed in binary128 (exact for +,-,*) and rounded once
// to the operand type; division goes through the 113-bit intermediate,
// which matches the runtime fp128 libcalls.
static Node *fold_binary_fp128(Node *node) {
    Node *lhs = node->lhs;
    Node *rhs = node->rhs;
    if (!is_fp128_const(lhs) || !is_fp128_const(rhs)) return NULL;

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
            return folded_fp128(fp128_div(l, r), lhs->ty, node);
        case ND_EQ:
            return folded_int(fp128_cmp(l, r) == 0, T.ty_int, node);
        case ND_NE:
            return folded_int(fp128_cmp(l, r) != 0, T.ty_int, node);
        case ND_LT:
            return folded_int(fp128_cmp(l, r) < 0, T.ty_int, node);
        case ND_LE:
            return folded_int(fp128_cmp(l, r) <= 0, T.ty_int, node);
        default:
            return NULL;
    }
}

// Fold a binary node with _BitInt(65..128) constant operands.
static Node *fold_binary_i128(Node *node) {
    Node *lhs = node->lhs;
    Node *rhs = node->rhs;
    if (!is_i128_const(lhs) || !is_i128_const(rhs)) return NULL;

    Int128 l = lhs->ival, r = rhs->ival;
    bool unsig = lhs->ty->is_unsigned;
    switch (node->kind) {
        case ND_ADD:
            return folded_i128(int128_add(l, r), lhs->ty, node);
        case ND_SUB:
            return folded_i128(int128_sub(l, r), lhs->ty, node);
        case ND_MUL:
            return folded_i128(int128_mul(l, r), lhs->ty, node);
        case ND_DIV:
            if (int128_is_zero(r)) return NULL;
            return folded_i128(unsig ? int128_div_unsigned(l, r) : int128_div_signed(l, r), lhs->ty, node);
        case ND_MOD:
            if (int128_is_zero(r)) return NULL;
            return folded_i128(unsig ? int128_mod_unsigned(l, r) : int128_mod_signed(l, r), lhs->ty, node);
        case ND_BAND:
            return folded_i128(int128_and(l, r), lhs->ty, node);
        case ND_BOR:
            return folded_i128(int128_or(l, r), lhs->ty, node);
        case ND_XOR:
            return folded_i128(int128_xor(l, r), lhs->ty, node);
        case ND_LEFT:
            if (r.limb[0] >= (uint32_t)bitint_width(lhs->ty)) return NULL;
            return folded_i128(int128_shl(l, (int)r.limb[0]), lhs->ty, node);
        case ND_RIGHT:
            if (r.limb[0] >= (uint32_t)bitint_width(lhs->ty)) return NULL;
            return folded_i128(int128_shr(l, (int)r.limb[0], unsig ? UNSIGNED : SIGNED), lhs->ty, node);
        case ND_EQ:
            return folded_int(unsig ? int128_cmp_unsigned(l, r) == 0 : int128_cmp_signed(l, r) == 0, T.ty_int, node);
        case ND_NE:
            return folded_int(unsig ? int128_cmp_unsigned(l, r) != 0 : int128_cmp_signed(l, r) != 0, T.ty_int, node);
        case ND_LT:
            return folded_int(unsig ? int128_cmp_unsigned(l, r) < 0 : int128_cmp_signed(l, r) < 0, T.ty_int, node);
        case ND_LE:
            return folded_int(unsig ? int128_cmp_unsigned(l, r) <= 0 : int128_cmp_signed(l, r) <= 0, T.ty_int, node);
        default:
            return NULL;
    }
}

// Fold a unary node with an _Float16/32/64/128 constant operand.
static Node *fold_unary_fp128(Node *node) {
    Node *lhs = node->lhs;
    if (!is_fp128_const(lhs)) return NULL;
    switch (node->kind) {
        case ND_PLUS:
            return folded_fp128(lhs->fpval, lhs->ty, node);
        case ND_NEG:
            return folded_fp128(fp128_neg(lhs->fpval), lhs->ty, node);
        default:
            return NULL;
    }
}

// Fold a unary node with a _BitInt(65..128) constant operand.
static Node *fold_unary_i128(Node *node) {
    Node *lhs = node->lhs;
    if (!is_i128_const(lhs)) return NULL;
    switch (node->kind) {
        case ND_PLUS:
            return folded_i128(lhs->ival, lhs->ty, node);
        case ND_NEG:
            return folded_i128(int128_neg(lhs->ival), lhs->ty, node);
        case ND_INVERT:
            return folded_i128(int128_not(lhs->ival), lhs->ty, node);
        default:
            return NULL;
    }
}

// Fold a unary floating-point node.
static Node *fold_unary_float(Node *node) {
    Node *lhs = node->lhs;
    if (!is_float_const(lhs)) return NULL;

    double v = lhs->fval;
    switch (node->kind) {
        case ND_PLUS:
            return folded_float(v, lhs->ty, node);
        case ND_NEG:
            return folded_float(-v, lhs->ty, node);
        default:
            return NULL;
    }
}

// Fold a unary arithmetic node.
static Node *fold_unary(Node *node) {
    Node *lhs = node->lhs;
    if (!is_int_const(lhs)) return NULL;

    int64_t v = lhs->val;
    switch (node->kind) {
        case ND_PLUS:
            return folded_int(v, lhs->ty, node);
        case ND_NEG:
            // Don't fold unsigned negation — the result depends on the
            // promotion rules and the type width; leave it to IR gen.
            if (lhs->ty->is_unsigned) return NULL;
            return folded_int(-v, lhs->ty, node);
        case ND_NOT:
            return folded_int(!v, T.ty_int, node);
        case ND_INVERT:
            return folded_int(~v, lhs->ty, node);
        default:
            return NULL;
    }
}

// Fold a cast node (IMCAST or EXCAST) where the inner expression is
// a constant. Handles int↔int, int→float, float→int, float→float, and
// the _FloatN / _BitInt(>64) families. Branch order matters: the classic
// double-based paths must not claim the new types (which store fpval/ival
// in the same union).
static Node *fold_cast(Node *node) {
    Node *lhs = node->lhs;

    // int → int cast (width <= 64 targets)
    if (is_int_const(lhs) && is_integer(node->ty) && !is_bitint128(node->ty)) {
        if (is_bool(node->ty)) return folded_int(lhs->val != 0, T.ty_bool, node);

        int64_t v = lhs->val;
        int bits = node->ty->size * 8;
        if (node->ty->kind & TY_BITINT) {
            return folded_int(norm_bits(v, bitint_width(node->ty), node->ty->is_unsigned), node->ty, node);
        }
        if (bits > 0 && bits < 64) {
            uint64_t mask = ((uint64_t)1u << bits) - 1;
            uint64_t u = (uint64_t)v & mask;
            if (!node->ty->is_unsigned && (u >> (bits - 1)))
                v = (int64_t)(u | ~mask);
            else
                v = (int64_t)u;
        }
        return folded_int(v, node->ty, node);
    }

    // int64 / _BitInt(<=64) → _BitInt(65..128) / _FloatN
    if (is_int_const(lhs)) {
        if (is_bitint128(node->ty)) return folded_i128(i128_of_node(lhs), node->ty, node);
        if (is_new_flonum(node->ty)) {
            Fp128 v = lhs->ty->is_unsigned ? fp128_from_int128(int128_set_ui((uint64_t)lhs->val), UNSIGNED)
                                           : fp128_from_int128(int128_set_i(lhs->val), SIGNED);
            return folded_fp128(v, node->ty, node);
        }
    }

    // _FloatN / _BitInt(>64) sources
    if (is_fp128_const(lhs)) {
        if (is_new_flonum(node->ty)) return folded_fp128(lhs->fpval, node->ty, node);
        if (is_flonum(node->ty)) {
            uint64_t b = fp128_to_fp64_bits(lhs->fpval);
            double d;
            memcpy(&d, &b, 8);
            return folded_float(d, node->ty, node);
        }
        if (is_integer(node->ty) && !is_bitint128(node->ty)) {
            bool ok;
            Int128 v = fp128_to_int128(lhs->fpval, node->ty->is_unsigned ? UNSIGNED : SIGNED, &ok);
            if (!ok) return NULL;
            return folded_int((int64_t)v.limb[0] | ((int64_t)v.limb[1] << 32), node->ty, node);
        }
        return NULL;
    }
    if (is_i128_const(lhs)) {
        if (is_bitint128(node->ty)) return folded_i128(lhs->ival, node->ty, node);
        if (is_integer(node->ty) && !is_bitint128(node->ty))
            return folded_int((int64_t)lhs->ival.limb[0] | ((int64_t)lhs->ival.limb[1] << 32), node->ty, node);
        if (is_new_flonum(node->ty)) {
            Fp128 v = fp128_from_int128(lhs->ival, lhs->ty->is_unsigned ? UNSIGNED : SIGNED);
            return folded_fp128(v, node->ty, node);
        }
        return NULL;
    }

    // int → classic float cast: (double)3, (float)42
    if (is_int_const(lhs) && is_flonum(node->ty)) return folded_float((double)lhs->val, node->ty, node);

    // float → int cast: (int)3.14
    if (is_float_const(lhs) && is_integer(node->ty) && !is_bitint128(node->ty)) {
        if (is_bool(node->ty)) return folded_int(lhs->fval != 0.0, T.ty_bool, node);

        double v = lhs->fval;
        int64_t iv;
        switch (node->ty->size) {
            case 1:
                iv = node->ty->is_unsigned ? (uint8_t)v : (int8_t)v;
                break;
            case 2:
                iv = node->ty->is_unsigned ? (uint16_t)v : (int16_t)v;
                break;
            case 4:
                iv = node->ty->is_unsigned ? (uint32_t)v : (int32_t)v;
                break;
            default:
                iv = (int64_t)v;
                break;
        }
        return folded_int(iv, node->ty, node);
    }

    // classic float ↔ classic float cast
    if (is_float_const(lhs) && is_flonum(node->ty) && !is_new_flonum(node->ty))
        return folded_float(lhs->fval, node->ty, node);

    // classic float → _FloatN cast
    if (is_float_const(lhs) && is_new_flonum(node->ty)) {
        uint64_t b;
        memcpy(&b, &lhs->fval, 8);
        return folded_fp128(fp128_from_fp64(b), node->ty, node);
    }

    if (is_integer(node->ty)) {
        if (lhs->kind == ND_EXCAST || lhs->kind == ND_IMCAST) {
            if (is_pointer(lhs->ty) && is_integer(lhs->lhs->ty)) {
                if (is_int_const(lhs->lhs)) return folded_int(lhs->lhs->val, node->ty, node);
            }
        }
    }
    return NULL;
}

// Fold a conditional (?:) node where the condition is a constant.
static Node *fold_cond(Node *node) {
    if (!is_int_const(node->cond)) return NULL;
    return node->cond->val ? node->then : node->els;
}

// Fold a logical AND/OR where one side is a constant.
// 0 && x → 0,  1 && x → x,  0 || x → x,  1 || x → 1
static Node *fold_logical(Node *node) {
    Node *lhs = node->lhs;
    if (!is_int_const(lhs) && !is_float_const(lhs)) return NULL;
    Node *rhs = node->rhs;
    if (is_int_const(rhs) || is_float_const(rhs)) {
        int64_t lv = lhs->val;
        int64_t rv = rhs->val;
        if (node->kind == ND_LOGAND)
            return folded_int(lv && rv, T.ty_int, node);
        else
            return folded_int(lv || rv, T.ty_int, node);
    }

    if (node->kind == ND_LOGAND)
        return lhs->val ? new_lognot(node->rhs) : folded_int(0, T.ty_int, node);
    else  // ND_LOGOR
        return lhs->val ? folded_int(1, T.ty_int, node) : new_lognot(node->rhs);
}

// Fold a boolean conversion to an integer constant when possible.
//   (bool)0 → 0,  (bool)5 → 1
static Node *fold_bool(Node *node) {
    Node *lhs = node->lhs;
    if (!is_int_const(lhs)) return NULL;
    if (node->ty->kind != TY_BOOL) return NULL;
    return folded_int(lhs->val != 0, T.ty_bool, node);
}

static Node *fold_ptradd(Node *node) {
    Node *lhs = node->lhs;
    if (!is_ptr_const(lhs)) return NULL;
    Node *rhs = node->rhs;
    if (!is_int_const(rhs)) return NULL;
    lhs->lhs->val += rhs->val * node->ty->base->size;
    return lhs->lhs;
}

// Recursively fold an AST subtree. Returns the folded node
// (which may be the original or a replacement).
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
            node->lhs = fold_node(node->lhs);
            node->rhs = fold_node(node->rhs);
            return fold_binary(node) ?: fold_binary_fp128(node) ?: fold_binary_i128(node) ?: fold_binary_float(node) ?: node;
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
            return fold_unary(node) ?: fold_unary_fp128(node) ?: fold_unary_i128(node) ?: fold_unary_float(node) ?: node;

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
        case ND_LOGOR:
            node->lhs = fold_node(node->lhs);
            node->rhs = fold_node(node->rhs);
            return fold_logical(node) ?: node;

        // Boolean conversion (implicit cast to bool)
        case ND_LVTOR:
            node->lhs = fold_node(node->lhs);
            if (node->ty->kind == TY_BOOL) return fold_bool(node) ?: node;
            return node;

        // Comma: fold both sides, discard lhs if it has no side effects
        case ND_COMMA:
            node->lhs = fold_node(node->lhs);
            node->rhs = fold_node(node->rhs);
            if (is_int_const(node->lhs) || is_float_const(node->lhs)) return node->rhs;
            return node;

        // Statements: fold sub-expressions
        case ND_RETURN:
        case ND_EXPR_STMT:
            node->lhs = fold_node(node->lhs);
            return node;

        case ND_IF:
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

        case ND_FUNCALL:
            for (Node *a = node->args; a; a = a->next) fold_node(a);
            return node;
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
        case ND_NOP:
        case ND_MEMZERO:
        case ND_GOTO:
        case ND_BREAK:
        case ND_CONTINUE:
        case ND_SP_SAVE:
        case ND_SP_RESTORE:
            break;
    }
    return node;
}

// Entry point: fold constants in the AST.
void fold_ast(Module *prog) {
    for (Sym *fn = prog->fns; fn; fn = fn->next) {
        if (!fn->is_defined) continue;
        fold_node(fn->body);
    }
}
