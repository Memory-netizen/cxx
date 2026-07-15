#include "cxx.h"

#define TYPE(kind, size, align) &(Type){kind, size, align, 0, 0, NULL, NULL, NULL, {0}};

Type *ty_void = TYPE(TY_VOID, 0, 0);
Type *ty_char = TYPE(TY_CHAR, 1, 1);
Type *ty_short = TYPE(TY_SHORT, 2, 2);
Type *ty_int = TYPE(TY_INT, 4, 4);
Type *ty_long = TYPE(TY_LONG, 8, 8);
Type *ty_i1 = TYPE(TY_I1, 1, 1);
Type *ty_i64 = TYPE(TY_I64, 8, 8);

#undef TYPE

bool is_integer(Type *ty) {
    return ty->kind == TY_INT || ty->kind == TY_SHORT || ty->kind == TY_LONG || ty->kind == TY_CHAR ||
           ty->kind == TY_I64 || ty->kind == TY_I1;
}

bool is_pointer(Type *ty) { return ty->base != NULL; }

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
    Type *ty = calloc(1, sizeof(Type));
    ty->kind = TY_ARRAY;
    ty->size = base->size * len;
    ty->align = base->align;
    ty->name = NULL;
    ty->next = NULL;
    ty->base = base;
    ty->len = len;
    return ty;
}

void add_type(Node *node) {
    if (!node || node->ty) return;
    switch (node->kind) {
        case ND_FUNCALL:
            node->ty = ty_int;
            break;
        case ND_COMMA:
            add_type(node->lhs);
            add_type(node->rhs);
            node->ty = node->rhs->ty;
            break;
        case ND_AS:
        case ND_BOR:
        case ND_XOR:
        case ND_BAND:
        case ND_LEFT:
        case ND_RIGHT:
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_MOD:
        case ND_PTRADD:
            add_type(node->lhs);
            add_type(node->rhs);
            node->ty = node->lhs->ty;
            break;
        case ND_PLUS:
        case ND_NEG:
        case ND_INVERT:
            add_type(node->lhs);
            node->ty = node->lhs->ty;
            break;
        case ND_NOT:
            add_type(node->lhs);
            node->ty = ty_int;
            break;
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
            add_type(node->lhs);
            add_type(node->rhs);
            node->ty = ty_int;
            break;
        case ND_NUM:
            node->ty = ty_int;
            break;
        case ND_VAR:
            node->ty = node->var->ty;
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
        case ND_STMT_EXPR:
            if (node->body) {
                Node *stmt = node->body;
                while (stmt->next) stmt = stmt->next;
                if (stmt->kind == ND_EXPR_STMT && stmt->lhs) {
                    node->ty = stmt->lhs->ty;
                    return;
                }
            }
            break;
        case ND_MEMBER:
            node->ty = node->member->ty;
            break;
        case ND_IMCAST:
            add_type(node->lhs);
            break;
        case ND_EXCAST:
            add_type(node->lhs);
            break;
        case ND_RETURN:
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
        case ND_EXPR_STMT:
            if (node->lhs) {
                add_type(node->lhs);
                node->ty = node->lhs->ty;
            }
            break;
        case ND_DECL:
        case ND_COMP_STMT:
            for (Node *n = node->body; n; n = n->next) add_type(n);
            break;
    }
}
