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

static Node *new_lognot(Node *tmpl) {
    Node *node = emalloc(sizeof(Node));
    node->kind = ND_NOT;
    node->lhs = tmpl;
    node->ty = T.ty_int;
    node->tok = tmpl->tok;
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
static Node *fold_cast(Node *node) {
    Node *lhs = node->lhs;

    // int → int
    if (is_int_const(lhs) && is_integer(node->ty)) {
        if (is_bool(node->ty)) return folded_int(int128_set_i(!int128_is_zero(lhs->ival)), T.ty_bool, node);
        return folded_int(lhs->ival, node->ty, node);
    }

    // int → any float: round once to the target format
    if (is_int_const(lhs) && is_flonum(node->ty)) {
        Fp128 v = fp128_from_int128(lhs->ival, lhs->ty->is_unsigned ? UNSIGNED : SIGNED);
        return folded_fp128(v, node->ty, node);
    }

    // float → float: round once to the target format
    if (is_fp_const(lhs) && is_flonum(node->ty)) return folded_fp128(lhs->fpval, node->ty, node);

    // float → int: truncate toward zero, range-checked
    if (is_fp_const(lhs) && is_integer(node->ty)) {
        if (is_bool(node->ty)) return folded_int(int128_set_i(!fp128_is_zero(lhs->fpval)), T.ty_bool, node);
        bool ok;
        Int128 v = fp128_to_int128(lhs->fpval, node->ty->is_unsigned ? UNSIGNED : SIGNED, &ok);
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
// 0 && x → 0,  1 && x → x,  0 || x → x,  1 || x → 1
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

    if (node->kind == ND_LOGAND)
        return const_truthy(lhs) ? new_lognot(node->rhs) : folded_int(int128_set_i(0), T.ty_int, node);
    else  // ND_LOGOR
        return const_truthy(lhs) ? folded_int(int128_set_i(1), T.ty_int, node) : new_lognot(node->rhs);
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
            if (is_int_const(node->lhs) || is_fp_const(node->lhs)) return node->rhs;
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
        case ND_CAS:
        case ND_ATOMICRMW:
        case ND_FENCE:
            break;
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
        fold_node(fn->body);
    }
}
