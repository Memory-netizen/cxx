#include "cxx.h"

#define TYPE(kind, size, align, is_unsigned) \
    &(Type) { kind, 0, size, align, is_unsigned, 0, 0, NULL, NULL, NULL, {0} }

Type *ty_void = TYPE(TY_VOID, 1, 1, false);
Type *ty_bool = TYPE(TY_BOOL, 1, 1, false);
Type *ty_char = TYPE(TY_CHAR, 1, 1, CHAR_MIN == 0);
Type *ty_schar = TYPE(TY_CHAR, 1, 1, false);
Type *ty_uchar = TYPE(TY_CHAR, 1, 1, true);
Type *ty_short = TYPE(TY_SHORT, 2, 2, false);
Type *ty_ushort = TYPE(TY_SHORT, 2, 2, true);
Type *ty_int = TYPE(TY_INT, 4, 4, false);
Type *ty_uint = TYPE(TY_INT, 4, 4, true);
Type *ty_long = TYPE(TY_LONG, 8, 8, false);
Type *ty_ulong = TYPE(TY_LONG, 8, 8, true);
Type *ty_llong = TYPE(TY_LLONG, 8, 8, false);
Type *ty_ullong = TYPE(TY_LLONG, 8, 8, true);
Type *ty_i1 = TYPE(TY_I1, 1, 1, false);
Type *ty_i32 = TYPE(TY_I32, 4, 4, false);
Type *ty_i64 = TYPE(TY_I64, 8, 8, false);

#undef TYPE

bool is_void(Type *ty) { return ty->kind == TY_VOID; }
bool is_obj(Type *ty) { return ty->kind != TY_VOID && ty->kind != TY_FUNC; }

bool is_objptr(Type *ty) {
    if (ty->kind != TY_PTR) return false;
    return is_obj(ty->base);
}

bool is_voidptr(Type *ty) {
    if (ty->kind != TY_PTR) return false;
    return is_void(ty->base);
}

bool is_funcptr(Type *ty) {
    if (ty->kind != TY_PTR) return false;
    return ty->base->kind == TY_FUNC;
}

bool is_integer(Type *ty) {
    return ty->kind == TY_BOOL || ty->kind == TY_CHAR || ty->kind == TY_SHORT || ty->kind == TY_INT ||
           ty->kind == TY_LONG || ty->kind == TY_LLONG || ty->kind == TY_ENUM || ty->kind == TY_I1 ||
           ty->kind == TY_I32 || ty->kind == TY_I64;
}

bool is_flonum(Type *ty) { return ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE; }

bool is_arith(Type *ty) { return is_integer(ty) || is_flonum(ty); }

bool is_pointer(Type *ty) { return ty->kind == TY_PTR; }

bool is_scalar(Type *ty) { return is_pointer(ty) || is_arith(ty); }

static void copy_struct_type(Type *dst, Type *src) {
    Member head = {};
    Member *cur = &head;
    for (Member *mem = src->members; mem; mem = mem->next) {
        Member *new = emalloc(sizeof(Member));
        *new = *mem;
        cur = cur->next = new;
    }
    dst->members = head.next;
}

Type *copy_type(Type *ty) {
    Type *ret = emalloc(sizeof(Type));
    *ret = *ty;
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) copy_struct_type(ret, ty);
    return ret;
}

Type *pointer_to(Type *base, uint32_t qual) {
    Type *ty = emalloc(sizeof(Type));
    ty->kind = TY_PTR;
    ty->qual = qual;
    ty->size = 8;
    ty->align = 8;
    ty->is_unsigned = true;
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

Type *type_qual(Type *ty, uint32_t qual) {
    if (!ty) return NULL;

    if (qual == 0) return ty;

    if ((ty->qual & qual) == qual) return ty;

    ty = copy_type(ty);
    ty->qual |= qual;
    return ty;
}

Type *type_unqual(Type *ty) {
    if (!ty) return NULL;

    if (ty->qual == 0) return ty;

    ty = copy_type(ty);
    ty->qual = 0;

    return ty;
}

void add_type(Node *node);

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
        case ND_EQ:
        case ND_NE:
        case ND_LOGAND:
        case ND_LOGOR:
            if (is_scalar(lhs) && is_scalar(rhs)) return;
            break;
        case ND_PTRADD:
        case ND_PTRAS:
            if (is_pointer(lhs) && is_integer(rhs)) return;
            break;
        default:
            return;
    }
    error(node->tok, "invalid operands to binary ‘%.*s’", node->tok->len, node->tok->loc);
}

void lvalue_convert(Node **expr) {
    if (!(*expr) || !(*expr)->is_lvalue) return;
    Node *node = new_unary(ND_LVTOR, (*expr), (*expr)->tok);
    node->ty = type_unqual((*expr)->ty);
    *expr = node;
}

void new_imcast(Node **expr, Type *ty) {
    Node *node = new_unary(ND_IMCAST, *expr, (*expr)->tok);
    node->ty = ty;
    *expr = node;
}

static Type *get_common_type(Type *ty1, Type *ty2) {
    if (ty1->base) return pointer_to(ty1->base, 0);

    if (ty1->size < 4) ty1 = ty_int;
    if (ty2->size < 4) ty2 = ty_int;

    if (ty1->size != ty2->size) return (ty1->size < ty2->size) ? ty2 : ty1;

    if (ty1->kind != ty2->kind) return ty_llong;

    if (ty2->is_unsigned) return ty2;
    return ty1;
}

static void integer_promotion(Node **expr) {
    Type *ty = get_common_type((*expr)->ty, ty_int);
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
            node->ty = ty_int;
            break;
        case ND_VAR:
            add_type(node->var_init);
            node->ty = node->var->ty;
            node->is_lvalue = true;
            break;

        // unary
        case ND_PLUS:
        case ND_NEG:
            add_type(node->lhs);
            if (!is_arith(node->lhs->ty))
                error(node->tok, "wrong type argument to unary ‘%c’", node->kind == ND_PLUS ? '+' : '-');
            lvalue_convert(&node->lhs);
            integer_promotion(&node->lhs);
            node->ty = node->lhs->ty;
            break;
        case ND_INVERT:
            add_type(node->lhs);
            if (!is_integer(node->lhs->ty)) error(node->tok, "wrong type argument to unary ‘~’");
            lvalue_convert(&node->lhs);
            integer_promotion(&node->lhs);
            node->ty = node->lhs->ty;
            break;
        case ND_NOT:
            add_type(node->lhs);
            if (!is_scalar(node->lhs->ty)) error(node->tok, "wrong type argument to unary ‘!’");
            lvalue_convert(&node->lhs);
            node->ty = ty_int;
            break;
        case ND_LOGOR:
        case ND_LOGAND:
            check_binop(node);
            lvalue_convert(&node->lhs);
            lvalue_convert(&node->rhs);
            node->ty = ty_int;
            break;
        case ND_ADDR:
            add_type(node->lhs);
            if (!node->lhs->is_lvalue) error(node->tok, "lvalue required as unary ‘&’ operand");
            if (node->lhs->kind == ND_VAR && node->lhs->var->sclass & SC_REG)
                error(node->tok, "address of register variable ‘%s’ requested", str(node->lhs->var->id));
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
            new_imcast(&node->rhs, ty_long);
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
            node->ty = ty_int;
            break;
        case ND_AS:
            add_type(node->lhs);
            add_type(node->rhs);
            if (!node->lhs->is_lvalue || node->lhs->ty->kind == TY_FUNC)
                error(node->tok, "lvalue required as left operand of assignment");
            if (node->lhs->ty->qual & Q_CONST)
                error(node->tok, "assignment of read-only variable ‘%s’", str(node->lhs->var->id));
            if (node->lhs->kind == ND_IMCAST && node->lhs->lhs->ty->kind == TY_ARRAY)
                error(node->lhs->tok, "array type is not assignable");
            if (node->lhs->ty->kind != TY_STRUCT && node->lhs->ty->kind != TY_UNION) {
                lvalue_convert(&node->rhs);
                new_imcast(&node->rhs, node->lhs->ty);
            }
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
            check_binop(node);
            lvalue_convert(&node->rhs);
            Type *ty = get_common_type(node->lhs->ty, node->rhs->ty);
            new_imcast(&node->rhs, is_ptr ? ty_long : ty);
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
            lvalue_convert(&node->rhs);
            integer_promotion(&node->rhs);
            node->compute_ty = get_common_type(node->lhs->ty, ty_int);
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
            add_type(node->cond);
            add_type(node->then);
            add_type(node->els);
            lvalue_convert(&node->cond);
            lvalue_convert(&node->then);
            lvalue_convert(&node->els);
            if (node->then->ty->kind == TY_VOID || node->els->ty->kind == TY_VOID) {
                node->ty = ty_void;
            } else {
                usual_arith_conv(&node->then, &node->els);
                node->ty = node->then->ty;
            }
            break;
        // other
        case ND_NOP:
        case ND_GOTO:
        case ND_BREAK:
        case ND_CONTINUE:
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
            add_type(node->then);
            add_type(node->els);
            lvalue_convert(&node->cond);
            break;
        case ND_DECL:
        case ND_COMP_STMT:
            for (Node *n = node->body; n; n = n->next) add_type(n);
            break;
    }
}
