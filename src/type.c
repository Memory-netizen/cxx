#include "cxx.h"

Type *ty_int = &(Type){TY_INT, 4, 4, NULL};
Type *ty_i1 = &(Type){TY_I1, 1, 1, NULL};
Type *ty_i64 = &(Type){TY_I64, 8, 8, NULL};

bool is_integer(Type *ty) { return ty->kind == TY_INT; }
bool is_prointer(Type *ty) { return ty->kind == TY_PTR; }

Type *pointer_to(Type *base) {
    Type *ty = emalloc(sizeof(Type));
    ty->kind = TY_PTR;
    ty->size = 8;
    ty->align = 8;
    ty->base = base;
    return ty;
}

void add_type(Node *node) {
    if (!node || node->ty) return;

    switch (node->kind) {
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
        case ND_PTRSUB:
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
        case ND_GT:
        case ND_GE:
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
            if (node->lhs->ty->kind != TY_PTR) exit(1);
            node->ty = node->lhs->ty->base;
            break;
        default:
            break;
    }
}
