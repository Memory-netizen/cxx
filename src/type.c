#include "cxx.h"

#define TYPE(kind, size, align) &(Type){kind, size, align, 0, 0, NULL, NULL, NULL, {0}};

Type *ty_void = TYPE(TY_VOID, 1, 1);
Type *ty_bool = TYPE(TY_BOOL, 1, 1);
Type *ty_char = TYPE(TY_CHAR, 1, 1);
Type *ty_short = TYPE(TY_SHORT, 2, 2);
Type *ty_int = TYPE(TY_INT, 4, 4);
Type *ty_long = TYPE(TY_LONG, 8, 8);
Type *ty_llong = TYPE(TY_LLONG, 8, 8);
Type *ty_i1 = TYPE(TY_I1, 1, 1);
Type *ty_i64 = TYPE(TY_I64, 8, 8);

#undef TYPE

bool is_integer(Type *ty) {
    return ty->kind == TY_BOOL || ty->kind == TY_INT || ty->kind == TY_SHORT || ty->kind == TY_LLONG ||
           ty->kind == TY_LONG || ty->kind == TY_ENUM || ty->kind == TY_CHAR || ty->kind == TY_I64 || ty->kind == TY_I1;
}

bool is_pointer(Type *ty) { return ty->kind == TY_PTR; }

Type *copy_type(Type *ty) {
    Type *ret = emalloc(sizeof(Type));
    *ret = *ty;
    return ret;
}

Type *pointer_to(Type *base) {
    Type *ty = emalloc(sizeof(Type));
    ty->kind = TY_PTR;
    ty->size = 8;
    ty->align = 8;
    ty->name = NULL;
    ty->next = NULL;
    ty->base = base;
    return ty;
}

Type *func_type(Type *return_ty) {
    Type *ty = emalloc(sizeof(Type));
    memset(ty, 0, sizeof(Type));
    ty->kind = TY_FUNC;
    ty->ret = return_ty;
    return ty;
}

Type *array_of(Type *base, int len) {
    Type *ty = emalloc(sizeof(Type));
    ty->kind = TY_ARRAY;
    ty->size = base->size * len;
    ty->align = base->align;
    ty->name = NULL;
    ty->next = NULL;
    ty->base = base;
    ty->len = len;
    return ty;
}

Type *struct_type(void) {
    Type *ty = emalloc(sizeof(Type));
    memset(ty, 0, sizeof(Type));
    ty->kind = TY_STRUCT;
    ty->size = 0;
    ty->align = 1;
    return ty;
}

Type *enum_type(void) {
    Type *ty = emalloc(sizeof(Type));
    memset(ty, 0, sizeof(Type));
    ty->kind = TY_ENUM;
    ty->size = 4;
    ty->align = 4;
    return ty;
}

static Type *get_common_type(Type *ty1, Type *ty2) {
    if (ty1->base) return pointer_to(ty1->base);
    if (ty1->size == 8 || ty2->size == 8) return ty_long;
    return ty_int;
}

static void integer_promotion(Node **expr) {
    Type *ty = get_common_type((*expr)->ty, ty_int);
    *expr = new_imcast(*expr, ty);
}

static void usual_arith_conv(Node **lhs, Node **rhs) {
    Type *ty = get_common_type((*lhs)->ty, (*rhs)->ty);
    *lhs = new_imcast(*lhs, ty);
    *rhs = new_imcast(*rhs, ty);
}

void add_type(Node *node) {
    if (!node || node->ty) return;
    switch (node->kind) {
        case ND_NUM:
            node->ty = (node->val == (int)node->val) ? ty_int : ty_long;
            break;
        case ND_VAR:
            node->ty = node->var->ty;
            break;

        // unary
        case ND_PLUS:
        case ND_NEG:
        case ND_INVERT:
            add_type(node->lhs);
            integer_promotion(&node->lhs);
            node->ty = node->lhs->ty;
            break;
        case ND_NOT:
            add_type(node->lhs);
            node->ty = ty_int;
            break;
        case ND_LOGOR:
        case ND_LOGAND:
            add_type(node->lhs);
            add_type(node->rhs);
            node->ty = ty_int;
            break;
        case ND_ADDR:
            add_type(node->lhs);
            node->ty = pointer_to(node->lhs->ty);
            break;
        case ND_DEREF:
            add_type(node->lhs);
            if (!is_pointer(node->lhs->ty)) exit(1);
            node->ty = node->lhs->ty->base;
            break;
        case ND_MEMBER:
            node->ty = node->member->ty;
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
            add_type(node->lhs);
            add_type(node->rhs);
            usual_arith_conv(&node->lhs, &node->rhs);
            node->ty = node->lhs->ty;
            break;
        case ND_PTRADD:
            add_type(node->lhs);
            add_type(node->rhs);
            node->rhs = new_imcast(node->rhs, ty_long);
            node->ty = node->lhs->ty;
            break;
        case ND_LEFT:
        case ND_RIGHT:
            add_type(node->lhs);
            add_type(node->rhs);
            integer_promotion(&node->lhs);
            integer_promotion(&node->rhs);
            node->ty = node->lhs->ty;
            break;
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
            add_type(node->lhs);
            add_type(node->rhs);
            usual_arith_conv(&node->lhs, &node->rhs);
            node->ty = ty_int;
            break;
        case ND_AS:
            add_type(node->lhs);
            add_type(node->rhs);
            if (node->lhs->ty->kind == TY_ARRAY) error(node->lhs->tok->loc, "not an lvalue");
            if (node->lhs->ty->kind != TY_STRUCT && node->lhs->ty->kind != TY_UNION)
                node->rhs = new_imcast(node->rhs, node->lhs->ty);
            node->ty = node->lhs->ty;
            break;
        case ND_PREINC:
        case ND_PREDEC:
        case ND_POSTINC:
        case ND_POSTDEC:
            add_type(node->lhs);
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
            add_type(node->rhs);
            Type *ty = get_common_type(node->lhs->ty, node->rhs->ty);
            new_imcast(node->rhs, is_ptr ? ty_long : ty);
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
            add_type(node->lhs);
            add_type(node->rhs);
            Type *ty = get_common_type(node->lhs->ty, node->rhs->ty);
            new_imcast(node->rhs, ty);
            node->compute_ty = ty;
            node->ty = node->lhs->ty;
            break;
        }
        case ND_LEFTAS:
        case ND_RIGHTAS:
            add_type(node->lhs);
            add_type(node->rhs);
            integer_promotion(&node->rhs);
            node->compute_ty = get_common_type(node->lhs->ty, ty_int);
            node->ty = node->lhs->ty;
            break;
        case ND_COMMA:
            add_type(node->lhs);
            add_type(node->rhs);
            node->ty = node->rhs->ty;
            break;
        // other
        case ND_PTRAS:
        case ND_FUNCALL:
            // Nothing to do
            break;
        case ND_STMT_EXPR:
            if (node->body) {
                Node *stmt = node->body;
                while (stmt->next) stmt = stmt->next;
                if (stmt->kind == ND_EXPR_STMT && stmt->lhs) node->ty = stmt->lhs->ty;
            }
            break;
        case ND_IMCAST:
        case ND_EXCAST:
        case ND_RETURN:
        case ND_EXPR_STMT:
            add_type(node->lhs);
            break;
        case ND_IF:
        case ND_WHILE:
        case ND_DO:
        case ND_FOR:
            add_type(node->init);
            add_type(node->cond);
            add_type(node->then);
            add_type(node->els);
            break;
        case ND_DECL:
        case ND_COMP_STMT:
            for (Node *n = node->body; n; n = n->next) add_type(n);
            break;
    }
}
