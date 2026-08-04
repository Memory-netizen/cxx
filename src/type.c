#include "cxx.h"

#define TYPE(kind, size, align, is_unsigned) \
    &(Type) { kind, 0, size, align, is_unsigned, 0, 0, NULL, NULL, NULL, {0} }

Type *ty_void = TYPE(TY_VOID, 1, 1, false);
Type *ty_nullptr = TYPE(TY_NULLPTR, 8, 8, true);
Type *ty_bool = TYPE(TY_BOOL, 1, 1, true);
Type *ty_char = TYPE(TY_CHAR, 1, 1, CHAR_MIN == 0);
Type *ty_schar = TYPE(TY_SCHAR, 1, 1, false);
Type *ty_uchar = TYPE(TY_UCHAR, 1, 1, true);
Type *ty_short = TYPE(TY_SHORT, 2, 2, false);
Type *ty_ushort = TYPE(TY_SHORT, 2, 2, true);
Type *ty_int = TYPE(TY_INT, 4, 4, false);
Type *ty_uint = TYPE(TY_INT, 4, 4, true);
Type *ty_long = TYPE(TY_LONG, 8, 8, false);
Type *ty_ulong = TYPE(TY_LONG, 8, 8, true);
Type *ty_llong = TYPE(TY_LLONG, 8, 8, false);
Type *ty_ullong = TYPE(TY_LLONG, 8, 8, true);
Type *ty_float = TYPE(TY_FLOAT, 4, 4, false);
Type *ty_double = TYPE(TY_DOUBLE, 8, 8, false);
Type *ty_ldouble = TYPE(TY_LDOUBLE, 8, 8, false);
Type *ty_i1 = TYPE(TY_I1, 1, 1, true);
Type *ty_i32 = TYPE(TY_I32, 4, 4, false);
Type *ty_i64 = TYPE(TY_I64, 8, 8, false);

#undef TYPE

bool is_bool(Type *ty) { return ty->kind == TY_BOOL; }
bool is_void(Type *ty) { return ty->kind == TY_VOID; }
bool is_obj(Type *ty) { return ty->kind != TY_VOID && ty->kind != TY_FUNC; }

bool is_objptr(Type *ty) {
    if (ty->kind != TY_PTR) return false;
    return is_obj(ty->base);
}

bool is_complete(Type *ty) { return ty->size > 0; }

bool is_complete_objptr(Type *ty) { return is_objptr(ty) && is_complete(ty->base); }

bool is_voidptr(Type *ty) {
    if (ty->kind != TY_PTR) return false;
    return is_void(ty->base);
}

bool is_funcptr(Type *ty) {
    if (ty->kind != TY_PTR) return false;
    return ty->base->kind == TY_FUNC;
}

bool is_integer(Type *ty) {
    return ty->kind == TY_BOOL || ty->kind == TY_CHAR || ty->kind == TY_SCHAR || ty->kind == TY_UCHAR ||
           ty->kind == TY_SHORT || ty->kind == TY_INT || ty->kind == TY_LONG || ty->kind == TY_LLONG ||
           ty->kind == TY_ENUM || ty->kind == TY_I1 || ty->kind == TY_I32 || ty->kind == TY_I64;
}

bool is_flonum(Type *ty) { return ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE; }

bool is_arith(Type *ty) { return is_integer(ty) || is_flonum(ty); }

bool is_pointer(Type *ty) { return ty->kind == TY_PTR; }

bool is_nullptr(Type *ty) { return ty->kind == TY_NULLPTR; }

bool is_null_constant(Node *node) {
    if (node->kind == ND_NUM && node->val == 0 && is_integer(node->ty)) return true;
    if (node->kind == ND_NULLPTR) return true;
    if (node->kind == ND_EXCAST && is_voidptr(node->ty) && is_null_constant(node->lhs)) return true;
    return false;
}

bool is_scalar(Type *ty) { return is_arith(ty) || is_pointer(ty) || is_nullptr(ty); }

bool is_record(Type *ty) { return ty->kind == TY_STRUCT || ty->kind == TY_UNION; }

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

Type *struct_type(bool is_union) {
    Type *ty = emalloc(sizeof(Type));
    memset(ty, 0, sizeof(Type));
    ty->kind = is_union ? TY_UNION : TY_STRUCT;
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
    if ((ty->qual & qual) == qual) return ty;
    ty = copy_type(ty);
    ty->qual |= qual;
    return ty;
}

Type *type_unqual(Type *ty) {
    if (ty->qual == 0) return ty;
    ty = copy_type(ty);
    ty->qual = 0;
    return ty;
}

static struct {
    Type *t1;
    Type *t2;
} cmpset[64];
static int depth;

static void push_cmp(Type *t1, Type *t2) {
    cmpset[depth].t1 = t1;
    cmpset[depth++].t2 = t2;
}

static void pop_cmp() { depth--; }

static bool check_set(Type *t1, Type *t2) {
    for (int i = 0; i < depth; i++)
        if (t1 == cmpset[i].t1 && t2 == cmpset[i].t2) return true;
    return false;
}

bool is_compatible(Type *t1, Type *t2) {
    if (t1 == t2) return true;

    if (t1->kind != t2->kind) return false;
    if (t1->qual != t2->qual) return false;
    if (check_set(t1, t2)) return true;

    switch (t1->kind) {
        case TY_SHORT:
        case TY_INT:
        case TY_LONG:
        case TY_LLONG:
            return t1->is_unsigned == t2->is_unsigned;
        case TY_I1:
        case TY_I32:
        case TY_I64:
        case TY_NULLPTR:
        case TY_VOID:
        case TY_BOOL:
        case TY_CHAR:
        case TY_SCHAR:
        case TY_UCHAR:
        case TY_FLOAT:
        case TY_DOUBLE:
        case TY_LDOUBLE:
            return true;
        case TY_PTR:
            return is_compatible(t1->base, t2->base);
        case TY_FUNC:
            if (!is_compatible(t1->ret, t2->ret)) return false;
            if (t1->is_variadic != t2->is_variadic) return false;

            Type *p1 = t1->params;
            Type *p2 = t2->params;
            for (; p1 && p2; p1 = p1->next, p2 = p2->next)
                if (!is_compatible(p1, p2)) return false;
            return p1 == NULL && p2 == NULL;
        case TY_ARRAY:
            if (!is_compatible(t1->base, t2->base)) return false;
            return t1->len < 0 || t2->len < 0 || t1->len == t2->len;
        case TY_STRUCT:
        case TY_UNION:
            if (t1->is_anon || t2->is_anon) return false;
            if (t1->id != t2->id) return false;
            Member *m1 = t1->members;
            Member *m2 = t2->members;
            push_cmp(t1, t2);
            for (; m1 && m2; m1 = m1->next, m2 = m2->next) {
                if (m1->name->id != m2->name->id) return false;
                if (m1->is_align != m2->is_align) return false;
                if (m1->align != m2->align) return false;
                if (!is_compatible(m1->ty, m2->ty)) return false;
            }
            pop_cmp();
            return m1 == NULL && m2 == NULL;
        case TY_ENUM:
            if (t1->is_anon || t2->is_anon) return false;
            if (t1->id != t2->id) return false;
            EnumVal *enm1 = t1->enumvals;
            EnumVal *enm2 = t2->enumvals;
            for (; enm1 && enm2; enm1 = enm1->next, enm2 = enm2->next) {
                if (enm1->name != enm2->name) return false;
                if (enm1->val != enm2->val) return false;
            }
            return enm1 == NULL && enm2 == NULL;
    }
    return false;
}

void add_type(Node *node);

void check_unop(Node *node) {
    add_type(node->lhs);
    Type *lhs = node->lhs->ty;
    switch (node->kind) {
        case ND_PLUS:
        case ND_NEG:
            if (is_arith(lhs)) return;
            break;
        case ND_INVERT:
            if (is_integer(lhs)) return;
            break;
        case ND_NOT:
            if (is_scalar(lhs)) return;
            break;
        case ND_PREINC:
        case ND_PREDEC:
        case ND_POSTINC:
        case ND_POSTDEC:
            if (is_arith(lhs) || is_pointer(lhs)) return;
            break;
        default:
            return;
    }
    error(node->tok, "wrong type argument to unary ‘%.*s’", node->tok->len, node->tok->loc);
}

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
            if (is_arith(lhs) && is_arith(rhs)) return;
            if (is_pointer(lhs) && is_pointer(rhs))
                if (is_compatible(type_unqual(lhs->base), type_unqual(rhs->base))) return;
            //-----
            if (is_pointer(lhs) && is_pointer(rhs)) return;
            if (is_integer(lhs) && is_pointer(rhs)) return;
            if (is_pointer(lhs) && is_integer(rhs)) return;
            break;
        case ND_EQ:
        case ND_NE:
            if (is_arith(lhs) && is_arith(rhs)) return;
            if (is_pointer(lhs) && is_pointer(rhs))
                if (is_compatible(type_unqual(lhs->base), type_unqual(rhs->base))) return;

            if (is_objptr(lhs) && is_voidptr(rhs)) return;
            if (is_objptr(rhs) && is_voidptr(lhs)) return;

            if (is_nullptr(lhs) && is_nullptr(rhs)) return;

            if (is_nullptr(lhs) && is_null_constant(node->rhs)) return;
            if (is_nullptr(rhs) && is_null_constant(node->lhs)) return;

            if (is_pointer(lhs) && (is_null_constant(node->rhs) || is_nullptr(rhs))) return;
            if (is_pointer(rhs) && (is_null_constant(node->lhs) || is_nullptr(lhs))) return;

            // -----
            if (is_pointer(lhs) && is_pointer(rhs)) return;
            if (is_integer(lhs) && is_pointer(rhs)) return;
            if (is_pointer(lhs) && is_integer(rhs)) return;
            break;
        case ND_LOGAND:
        case ND_LOGOR:
            if (is_scalar(lhs) && is_scalar(rhs)) return;
            break;
        case ND_PTRADD:
        case ND_PTRAS:
            if (is_complete_objptr(lhs) && is_integer(rhs)) return;
            //---
            if (is_pointer(lhs) && is_integer(rhs)) return;
            break;
        default:
            return;
    }
    error(node->tok, "invalid operands to binary ‘%.*s’", node->tok->len, node->tok->loc);
}

void check_condop(Node *node) {
    add_type(node->cond);
    add_type(node->then);
    add_type(node->els);
    if (!is_scalar(node->cond->ty)) error(node->cond->tok, "scalar type is required in here");
    Type *lhs = node->then->ty;
    Type *rhs = node->els->ty;

    if (is_arith(lhs) && is_arith(rhs)) return;
    if (is_record(lhs) && is_compatible(lhs, rhs)) return;
    if (is_void(lhs) && is_void(rhs)) return;
    if (is_pointer(lhs) && is_pointer(rhs))
        if (is_compatible(type_unqual(lhs->base), type_unqual(rhs->base))) return;

    if (is_objptr(lhs) && is_voidptr(rhs)) return;
    if (is_objptr(rhs) && is_voidptr(lhs)) return;

    if (is_nullptr(lhs) && is_nullptr(rhs)) return;

    if (is_pointer(lhs) && (is_null_constant(node->rhs) || is_nullptr(rhs))) return;
    if (is_pointer(rhs) && (is_null_constant(node->lhs) || is_nullptr(lhs))) return;

    // ------
    if (is_nullptr(lhs) && is_null_constant(node->rhs)) return;
    if (is_nullptr(rhs) && is_null_constant(node->lhs)) return;

    if (is_pointer(lhs) && is_pointer(rhs)) return;
    if (is_integer(lhs) && is_pointer(rhs)) return;
    if (is_pointer(lhs) && is_integer(rhs)) return;
}

void check_asop(Type *dst, Node *src, int ctx) {
    static char *msg[] = {
        [CTX_AS] = "assigning",
        [CTX_RET] = "returning",
        [CTX_INIT] = "initializing",
        [CTX_CALL] = "passing argument",
    };
    add_type(src);
    Type *src_ty = src->ty;
    if (is_arith(dst) && is_arith(src_ty)) return;
    if (is_record(dst) && is_compatible(type_unqual(dst), src_ty)) return;
    if (is_pointer(dst) && is_pointer(src_ty) && is_compatible(type_unqual(dst->base), type_unqual(src_ty->base)))
        if (BIT_SUPERSET(dst->base->qual, src_ty->base->qual)) return;

    if (is_objptr(dst) && is_voidptr(src_ty))
        if (BIT_SUPERSET(dst->base->qual, src_ty->base->qual)) return;
    if (is_objptr(src_ty) && is_voidptr(dst))
        if (BIT_SUPERSET(dst->base->qual, src_ty->base->qual)) return;

    if (is_nullptr(dst) && is_null_constant(src)) return;
    if (is_nullptr(dst) && is_nullptr(src_ty)) return;
    if (is_pointer(dst) && (is_null_constant(src) || is_nullptr(src_ty))) return;

    if (is_bool(dst) && (is_pointer(src_ty) || is_nullptr(src_ty))) return;
    error(src->tok, "incompatible types when %s", msg[ctx]);
}

static void modifiable_lvalue(Node *node) {
    add_type(node->lhs);
    Node *lhs = node->lhs;
    if (!lhs->is_lvalue || lhs->ty->kind == TY_FUNC)
        error(node->tok, "lvalue required as ‘%.*s’ operand", node->tok->len, node->tok->loc);
    if (lhs->ty->qual & Q_CONST || lhs->ty->qual & Q_MEMCONST) {
        if (lhs->kind == ND_VAR) {
            error(node->tok, "assignment of read-only variable ‘%s’", str(lhs->var->id));
        } else {
            char *start = lhs->tok->loc;
            Token *cur = lhs->tok;
            while (cur->next != node->tok) cur = cur->next;
            error(node->tok, "assignment of read-only location ‘%.*s’", cur->loc - start + cur->len, start);
        }
    }
    if (is_void(lhs->ty)) error(node->tok, "incomplete type ‘void’ is not assignable");
    if (lhs->kind == ND_IMCAST && lhs->lhs->ty->kind == TY_ARRAY)
        error(lhs->tok, "assignment to expression with array type");
}

void lvalue_convert(Node **expr) {
    if (!(*expr) || !(*expr)->is_lvalue) return;
    Node *node = new_unary(ND_LVTOR, (*expr), (*expr)->tok);
    node->ty = type_unqual((*expr)->ty);
    *expr = node;
}

void new_imcast(Node **expr, Type *ty) {
    if (is_compatible((*expr)->ty, ty)) return;
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
        case ND_NULLPTR:
            break;
        case ND_VAR:
            add_type(node->var_init);
            node->ty = node->var->ty;
            node->is_lvalue = true;
            break;

        // unary
        case ND_PLUS:
        case ND_NEG:
        case ND_INVERT:
            check_unop(node);
            lvalue_convert(&node->lhs);
            integer_promotion(&node->lhs);
            node->ty = node->lhs->ty;
            break;
        case ND_NOT:
            check_unop(node);
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
            modifiable_lvalue(node);
            check_asop(node->lhs->ty, node->rhs, CTX_AS);
            if (!is_record(node->lhs->ty)) lvalue_convert(&node->rhs);
            new_imcast(&node->rhs, node->lhs->ty);
            node->ty = node->lhs->ty;
            break;
        case ND_INIT:
            add_type(node->lhs);
            check_asop(node->lhs->ty, node->rhs, CTX_INIT);
            if (!is_record(node->lhs->ty)) lvalue_convert(&node->rhs);
            new_imcast(&node->rhs, node->lhs->ty);
            node->ty = node->lhs->ty;
            break;
        case ND_PREINC:
        case ND_PREDEC:
        case ND_POSTINC:
        case ND_POSTDEC:
            check_unop(node);
            modifiable_lvalue(node);
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
            modifiable_lvalue(node);
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
            modifiable_lvalue(node);
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
            modifiable_lvalue(node);
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
            check_condop(node);
            lvalue_convert(&node->cond);
            lvalue_convert(&node->then);
            lvalue_convert(&node->els);
            if (is_void(node->then->ty) || is_void(node->els->ty)) {
                node->ty = ty_void;
            } else {
                usual_arith_conv(&node->then, &node->els);
                node->ty = node->then->ty;
            }
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
            if (node->lhs) node->ty = node->lhs->ty;
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
            if (node->cond && !is_scalar(node->cond->ty)) error(node->cond->tok, "scalar type is required in here");
            add_type(node->then);
            add_type(node->els);
            lvalue_convert(&node->cond);
            break;
        case ND_DECL:
        case ND_COMP_STMT: {
            Type *ty;
            for (Node *n = node->body; n; n = n->next) {
                add_type(n);
                ty = n->ty;
            }
            node->ty = ty;
            break;
        }
        // other
        case ND_NOP:
        case ND_GOTO:
        case ND_BREAK:
        case ND_CONTINUE:
        case ND_PTRAS:
        case ND_FUNCALL:
            // Nothing to do
            break;
    }
}
