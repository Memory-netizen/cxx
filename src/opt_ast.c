#include "cxx.h"

// Returns true if node is an integer constant.
static bool is_int_const(Node *node) { return node && node->kind == ND_NUM && is_integer(node->ty); }

// Returns true if node is a floating-point constant.
static bool is_float_const(Node *node) { return node && node->kind == ND_NUM && is_flonum(node->ty); }

// Create a folded integer constant node with the given type.
static Node *folded_int(int64_t val, Type *ty, Node *tmpl) {
    Node *node = emalloc(sizeof(Node));
    node->kind = ND_NUM;
    node->ty = ty;
    node->tok = tmpl->tok;
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
            if (!unsig) return NULL;
            return folded_int((int64_t)(ul >> r), lhs->ty, node);
        case ND_EQ:
            return folded_int(l == r, ty_int, node);
        case ND_NE:
            return folded_int(l != r, ty_int, node);
        case ND_LT:
            return folded_int(unsig ? (int64_t)(ul < ur) : (l < r), ty_int, node);
        case ND_LE:
            return folded_int(unsig ? (int64_t)(ul <= ur) : (l <= r), ty_int, node);
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
            return folded_int(!v, ty_int, node);
        case ND_INVERT:
            return folded_int(~v, lhs->ty, node);
        default:
            return NULL;
    }
}

// Fold a cast node (IMCAST or EXCAST) where the inner expression is
// a constant. Handles int↔int, int→float, float→int, and float→float.
static Node *fold_cast(Node *node) {
    Node *lhs = node->lhs;

    // int → int cast
    if (is_int_const(lhs) && is_integer(node->ty)) {
        if (is_bool(node->ty)) return folded_int(lhs->val != 0, ty_bool, node);

        int64_t v = lhs->val;
        switch (node->ty->size) {
            case 1:
                v = node->ty->is_unsigned ? (uint8_t)v : (int8_t)v;
                break;
            case 2:
                v = node->ty->is_unsigned ? (uint16_t)v : (int16_t)v;
                break;
            case 4:
                v = node->ty->is_unsigned ? (uint32_t)v : (int32_t)v;
                break;
            default:
                break;
        }
        return folded_int(v, node->ty, node);
    }

    // int → float cast: (double)3, (float)42
    if (is_int_const(lhs) && is_flonum(node->ty)) return folded_float((double)lhs->val, node->ty, node);

    // float → int cast: (int)3.14
    if (is_float_const(lhs) && is_integer(node->ty)) {
        if (is_bool(node->ty)) return folded_int(lhs->fval != 0.0, ty_bool, node);

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

    // float → float cast: (float)3.14159, (double)1.0f
    if (is_float_const(lhs) && is_flonum(node->ty)) return folded_float(lhs->fval, node->ty, node);

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
    if (!is_int_const(lhs)) return NULL;

    if (node->kind == ND_LOGAND)
        return lhs->val ? node->rhs : folded_int(0, ty_int, node);
    else  // ND_LOGOR
        return lhs->val ? folded_int(1, ty_int, node) : node->rhs;
}

// Fold a boolean conversion to an integer constant when possible.
//   (bool)0 → 0,  (bool)5 → 1
static Node *fold_bool(Node *node) {
    Node *lhs = node->lhs;
    if (!is_int_const(lhs)) return NULL;
    if (node->ty->kind != TY_BOOL) return NULL;
    return folded_int(lhs->val != 0, ty_bool, node);
}

// Recursively fold an AST subtree. Returns the folded node
// (which may be the original or a replacement).
static Node *fold_node(Node *node) {
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
            return fold_binary(node) ?: fold_binary_float(node) ?: node;

        // Unary arithmetic
        case ND_PLUS:
        case ND_NEG:
        case ND_NOT:
        case ND_INVERT:
            node->lhs = fold_node(node->lhs);
            return fold_unary(node) ?: fold_unary_float(node) ?: node;

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
            node->rhs = fold_node(node->rhs);
            return node;

        // Not foldable: just recurse into children
        case ND_ADDR:
        case ND_DEREF:
        case ND_MEMBER:
            node->lhs = fold_node(node->lhs);
            return node;

        case ND_FUNCALL:
            for (Node *a = node->args; a; a = a->next) fold_node(a);
            return node;

        case ND_LABEL:
            node->label_body = fold_node(node->label_body);
            return node;

        default:
            return node;
    }
}

// Entry point: fold constants in the AST.
void fold_ast(Module *prog) {
    for (Sym *fn = prog->fns; fn; fn = fn->next) {
        if (!fn->is_defined) continue;
        fold_node(fn->body);
    }
}
