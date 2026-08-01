#include "cxx.h"

static Module *curm;
static Sym *curf;
static Blk *curb;
static Blk dummy;
static Blk *tail;
static Blk *unreach = &dummy;
static int tmp_id;
static Blk *brk_blk;
static Blk *cont_blk;

static Ir *new_ins(IrKind op, Ref dst, Ref *args, uint32_t narg) {
    Ir *new = emalloc(sizeof(Ir));
    new->op = op;
    new->dst = dst;
    new->narg = narg;
    if (narg > 0 && args) {
        new->args = emalloc(narg * sizeof(Ref));
        memcpy(new->args, args, narg * sizeof(Ref));
    } else {
        new->args = NULL;
    }

    if (curb->head)
        curb->tail = curb->tail->next = new;
    else
        curb->head = curb->tail = new;
    return new;
}

Blk *new_blk(void) {
    Blk *b = emalloc(sizeof(Blk));
    memset(b, 0, sizeof(Blk));
    return b;
}

void insert_blk(Blk *b) {
    b->blk_id = tmp_id++;
    tail = tail->next = b;
}

static Ref gen_stmt(Node *node);
static Ref gen_expr(Node *node);

static Ref gen_addr(Node *node) {
    switch (node->kind) {
        case ND_VAR:
            gen_expr(node->var_init);
            if (node->var->is_local)
                return SLOT(node->var->vreg, pointer_to(node->ty, 0));
            else
                // Global variable
                return GLB(node->var->id, pointer_to(node->ty, 0));
        case ND_DEREF:
            return gen_expr(node->lhs);
        case ND_MEMBER: {
            Ref addr = gen_expr(node->lhs);
            if (node->lhs->ty->kind == TY_UNION) {
                addr.ty = pointer_to(node->member->ty, 0);
                return addr;
            }
            int pos = 0;
            int idx = 0;
            Member *mem = node->member;
            Member *cur = node->lhs->ty->members;
            while (cur) {
                if (pos == mem->offset) break;
                pos += cur->ty->size;
                cur = cur->next;
                if (!cur) break;
                if (pos != cur->offset) {
                    pos = cur->offset;
                    idx++;
                }
            }
            Ref gep_ops[] = {addr, INT(0), INT(mem->idx + idx)};
            Ref dst = TMP(tmp_id++, pointer_to(node->ty, 0));
            new_ins(IR_GEP, dst, gep_ops, 3);
            return dst;
        }
        default:
            break;
    }
    error(node->tok, "not a lvalue");
    return R;
}

static Ref load(Ref addr, Type *ty, int align) {
    Ref dst = TMP(tmp_id++, ty);
    new_ins(IR_LORD, dst, (Ref[]){addr, INT(align)}, 2);
    return dst;
}

static Ref cast(Ref val, Type *src_ty, Type *target_ty) {
    if (target_ty->kind == TY_BOOL) {
        Ref tmp = TMP(tmp_id++, ty_i1);
        new_ins(IR_CMP_NE, tmp, (Ref[]){val, INT(0)}, 2);

        Ref dst = TMP(tmp_id++, target_ty);
        new_ins(IR_ZEXT, dst, (Ref[]){tmp}, 1);
        return dst;
    }
    if (is_pointer(src_ty) && is_integer(target_ty)) {
        Ref dst = TMP(tmp_id++, target_ty);
        new_ins(IR_PTRTOINT, dst, (Ref[]){val}, 1);
        return dst;
    }
    if (is_integer(src_ty) && is_pointer(target_ty)) {
        Ref dst = TMP(tmp_id++, target_ty);
        new_ins(IR_INTTOPTR, dst, (Ref[]){val}, 1);
        return dst;
    }
    if (target_ty->kind == TY_VOID) return val;
    if (target_ty->size == src_ty->size) {
        val.ty = target_ty;
        return val;
    }
    Ref dst = TMP(tmp_id++, target_ty);
    if (target_ty->size > src_ty->size) {
        if (src_ty->is_unsigned)
            new_ins(IR_ZEXT, dst, (Ref[]){val}, 1);
        else
            new_ins(IR_SEXT, dst, (Ref[]){val}, 1);
    } else {
        new_ins(IR_TRUNC, dst, (Ref[]){val}, 1);
    }
    return dst;
}

static Ref convert(Node *lhs, Type *target_ty) {
    if (lhs->ty->kind == TY_ARRAY && is_pointer(target_ty)) {
        Ref addr = gen_expr(lhs);
        Ref dst = TMP(tmp_id++, target_ty);
        new_ins(IR_GEP, dst, (Ref[]){addr, LONG(0)}, 2);
        return dst;
    }
    return cast(gen_expr(lhs), lhs->ty, target_ty);
}

static Ref gen_cond(Node *node) {
    Blk *t_blk = new_blk();
    Blk *f_blk = new_blk();
    Blk *m_blk = new_blk();

    bool is_valid = node->ty->kind != TY_VOID;
    Ref res;
    int align = node->ty->align;
    if (is_valid) {
        int res_id = tmp_id++;
        res = TMP(res_id, node->ty);
        new_ins(IR_ALLOCA, TMP(res_id, pointer_to(node->ty, 0)), (Ref[]){INT(align)}, 1);
    }

    // cond
    Ref tmp = gen_expr(node->cond);
    Ref cond = TMP(tmp_id++, ty_i1);
    new_ins(IR_CMP_NE, cond, (Ref[]){tmp, INT(0)}, 2);
    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = t_blk;
    curb->succ2 = f_blk;

    // then
    curb = t_blk;
    insert_blk(curb);
    Ref true_r = gen_expr(node->then);
    if (is_valid) new_ins(IR_STR, R, (Ref[]){true_r, res, INT(align)}, 3);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;

    // else
    curb = f_blk;
    insert_blk(curb);
    Ref false_r = gen_expr(node->els);
    if (is_valid) new_ins(IR_STR, R, (Ref[]){false_r, res, INT(align)}, 3);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;

    curb = m_blk;
    insert_blk(curb);
    if (is_valid)
        return load(res, node->ty, node->ty->align);
    else
        return R;
}

static Ref gen_logor(Node *node) {
    Blk *t_blk = new_blk();
    Blk *f_blk = new_blk();
    Blk *m_blk = new_blk();

    int res_id = tmp_id++;
    Ref res = TMP(res_id, ty_int);
    new_ins(IR_ALLOCA, TMP(res_id, pointer_to(ty_int, 0)), (Ref[]){INT(4)}, 1);

    // lhs
    Ref lr = gen_expr(node->lhs);
    Ref cond = TMP(tmp_id++, ty_i1);
    new_ins(IR_CMP_NE, cond, (Ref[]){lr, INT(0)}, 2);
    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = t_blk;
    curb->succ2 = f_blk;

    curb = t_blk;
    insert_blk(curb);
    new_ins(IR_STR, R, (Ref[]){INT(1), res, INT(4)}, 3);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;

    // rhs
    curb = f_blk;
    insert_blk(curb);
    Ref rr = gen_expr(node->rhs);
    Ref res_r = TMP(tmp_id++, ty_i1);
    new_ins(IR_CMP_NE, res_r, (Ref[]){rr, INT(0)}, 2);
    Ref r_ext = TMP(tmp_id++, ty_int);
    new_ins(IR_ZEXT, r_ext, (Ref[]){res_r}, 1);
    new_ins(IR_STR, R, (Ref[]){r_ext, res, INT(4)}, 3);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;

    curb = m_blk;
    insert_blk(curb);
    return load(res, ty_int, 4);
}

static Ref gen_logand(Node *node) {
    Blk *f_blk = new_blk();
    Blk *t_blk = new_blk();
    Blk *m_blk = new_blk();

    int res_id = tmp_id++;
    Ref res = TMP(res_id, ty_int);
    new_ins(IR_ALLOCA, TMP(res_id, pointer_to(ty_int, 0)), (Ref[]){INT(4)}, 1);

    // lhs
    Ref lr = gen_expr(node->lhs);
    Ref cond = TMP(tmp_id++, ty_i1);
    new_ins(IR_CMP_EQ, cond, (Ref[]){lr, INT(0)}, 2);

    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = f_blk;
    curb->succ2 = t_blk;

    curb = f_blk;
    insert_blk(curb);
    new_ins(IR_STR, R, (Ref[]){INT(0), res, INT(4)}, 3);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;

    // rhs
    curb = t_blk;
    insert_blk(curb);
    Ref rr = gen_expr(node->rhs);
    Ref res_r = TMP(tmp_id++, ty_i1);
    new_ins(IR_CMP_NE, res_r, (Ref[]){rr, INT(0)}, 2);
    Ref r_ext = TMP(tmp_id++, ty_int);
    new_ins(IR_ZEXT, r_ext, (Ref[]){res_r}, 1);
    new_ins(IR_STR, R, (Ref[]){r_ext, res, INT(4)}, 3);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;

    curb = m_blk;
    insert_blk(curb);
    return load(res, ty_int, 4);
}

static Ref gen_expr(Node *node) {
    if (!node) return R;
    Ref dst;
    switch (node->kind) {
        case ND_NOP:
            return R;
        case ND_NULLPTR:
            return NULLPTR;
        case ND_NUM:
            if (node->ty->size == 1) return BOOL(node->val);
            if (node->ty->size == 4) return INT(node->val);
            return LONG(node->val);
        case ND_STMT_EXPR:
            for (Node *n = node->body; n; n = n->next) dst = gen_stmt(n);
            return dst;
        case ND_LVTOR: {
            int align = node->ty->align;
            if (node->lhs->kind == ND_VAR) align = node->lhs->var->align;
            return load(gen_expr(node->lhs), node->ty, align);
        }
        case ND_VAR:
        case ND_MEMBER:
            return gen_addr(node);
        case ND_DEREF:
            return gen_expr(node->lhs);
        case ND_ADDR:
            return gen_expr(node->lhs);
        case ND_IMCAST:
        case ND_EXCAST:
            return convert(node->lhs, node->ty);
        case ND_MEMZERO: {
            Ref addr = gen_expr(node->lhs);
            Ref ops[] = {addr, INT(0), INT(node->var->ty->size)};
            new_ins(IR_MEMSET, R, ops, 3);
            return R;
        }
        case ND_INIT:
        case ND_AS: {
            Ref addr = gen_expr(node->lhs);
            int align = node->ty->align;
            if (node->lhs->kind == ND_VAR) align = node->lhs->var->align;
            if (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION) {
                Ref src = gen_expr(node->rhs);
                Ref ops[] = {addr, src, INT(node->ty->size)};
                new_ins(IR_MEMCPY, R, ops, 3);
                return addr;
            }
            dst = gen_expr(node->rhs);
            new_ins(IR_STR, R, (Ref[]){dst, addr, INT(align)}, 3);
            return dst;
        }
        case ND_PREINC:
        case ND_PREDEC:
        case ND_POSTINC:
        case ND_POSTDEC: {
            int ir_op = is_pointer(node->ty) ? IR_GEP : IR_ADD;
            Ref addr = gen_expr(node->lhs);
            int align = node->ty->align;
            if (node->lhs->kind == ND_VAR) align = node->lhs->var->align;
            Ref lr = load(addr, node->ty, align);
            int addend = (node->kind == ND_PREINC || node->kind == ND_POSTINC) ? 1 : -1;
            Ref rr = INT(addend);
            rr.ty = is_pointer(node->ty) ? ty_long : node->ty;
            dst = TMP(tmp_id++, node->ty);
            new_ins(ir_op, dst, (Ref[]){lr, rr}, 2);
            new_ins(IR_STR, R, (Ref[]){dst, addr, INT(align)}, 3);
            if (node->kind == ND_PREINC || node->kind == ND_PREDEC)
                return dst;
            else
                return lr;
        }
        case ND_PTRAS: {
            Ref addr = gen_expr(node->lhs);
            int align = node->ty->align;
            if (node->lhs->kind == ND_VAR) align = node->lhs->var->align;
            Ref lr = load(addr, node->ty, align);
            Ref rr = gen_expr(node->rhs);
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_GEP, dst, (Ref[]){lr, rr}, 2);
            new_ins(IR_STR, R, (Ref[]){dst, addr, INT(align)}, 3);
            return dst;
        }
        case ND_ADDAS:
        case ND_SUBAS:
        case ND_MULAS:
        case ND_DIVAS:
        case ND_MODAS:
        case ND_ANDAS:
        case ND_ORAS:
        case ND_XORAS:
        case ND_LEFTAS:
        case ND_RIGHTAS: {
            Ref addr = gen_expr(node->lhs);
            int align = node->ty->align;
            if (node->lhs->kind == ND_VAR) align = node->lhs->var->align;
            Ref lr = load(addr, node->ty, align);
            lr = cast(lr, node->ty, node->compute_ty);
            Ref rr = gen_expr(node->rhs);
            rr = cast(rr, node->rhs->ty, node->compute_ty);
            static int bin_op[] = {
                [ND_ADDAS] = IR_ADD, [ND_SUBAS] = IR_SUB,  [ND_MULAS] = IR_MUL,   [ND_DIVAS] = IR_DIV,
                [ND_MODAS] = IR_REM, [ND_LEFTAS] = IR_SHL, [ND_RIGHTAS] = IR_SHR, [ND_ANDAS] = IR_AND,
                [ND_ORAS] = IR_OR,   [ND_XORAS] = IR_XOR,
            };
            Ref res = TMP(tmp_id++, node->compute_ty);
            new_ins(bin_op[node->kind], res, (Ref[]){lr, rr}, 2);
            dst = cast(res, node->compute_ty, node->ty);
            new_ins(IR_STR, R, (Ref[]){dst, addr, INT(align)}, 3);
            return dst;
        }
        case ND_LOGOR:
            return gen_logor(node);
        case ND_LOGAND:
            return gen_logand(node);
        case ND_COND:
            return gen_cond(node);
        case ND_FUNCALL: {
            int nargs = node->narg;
            Ref call_ops[nargs + 1];
            call_ops[0] = GLB(node->func, NULL);

            int idx = 1;
            for (Node *arg = node->args; arg; arg = arg->next) call_ops[idx++] = gen_expr(arg);

            if (node->ty->kind == TY_VOID)
                dst = R;
            else
                dst = TMP(tmp_id++, node->ty);
            new_ins(IR_CALL, dst, call_ops, nargs + 1);
            return dst;
        }
        default:
            break;
    }

    Ref lr = gen_expr(node->lhs);
    if (node->kind == ND_PLUS) return lr;

    // unary arithmetic operation
    switch (node->kind) {
        case ND_NEG:
            dst = TMP(tmp_id++, node->ty);
            Ref zr = node->ty->size == 8 ? LONG(0) : INT(0);
            new_ins(IR_SUB, dst, (Ref[]){zr, lr}, 2);
            return dst;
        case ND_INVERT:
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_XOR, dst, (Ref[]){lr, INT(-1)}, 2);
            return dst;
        case ND_NOT: {
            Ref tmp = TMP(tmp_id++, ty_i1);
            new_ins(IR_CMP_EQ, tmp, (Ref[]){lr, INT(0)}, 2);

            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_ZEXT, dst, (Ref[]){tmp}, 1);
            return dst;
        }
        default:
            break;
    }

    Ref rr = gen_expr(node->rhs);
    if (node->kind == ND_COMMA) return rr;

    switch (node->kind) {
        case ND_PTRADD:
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_GEP, dst, (Ref[]){lr, rr}, 2);
            return dst;
        // binary and bit arithmetic operation
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_MOD:
        case ND_LEFT:
        case ND_RIGHT:
        case ND_BAND:
        case ND_BOR:
        case ND_XOR: {
            static int bin_op[] = {
                [ND_ADD] = IR_ADD,  [ND_SUB] = IR_SUB,   [ND_MUL] = IR_MUL,  [ND_DIV] = IR_DIV, [ND_MOD] = IR_REM,
                [ND_LEFT] = IR_SHL, [ND_RIGHT] = IR_SHR, [ND_BAND] = IR_AND, [ND_BOR] = IR_OR,  [ND_XOR] = IR_XOR,
            };
            dst = TMP(tmp_id++, node->ty);
            new_ins(bin_op[node->kind], dst, (Ref[]){lr, rr}, 2);
            return dst;
        }
        // Comparison operations：icmp return i1，zext to i32
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE: {
            static int cmp_op[] = {
                [ND_EQ] = IR_CMP_EQ,
                [ND_NE] = IR_CMP_NE,
                [ND_LT] = IR_CMP_LT,
                [ND_LE] = IR_CMP_LE,
            };
            Ref tmp = TMP(tmp_id++, ty_i1);
            new_ins(cmp_op[node->kind], tmp, (Ref[]){lr, rr}, 2);

            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_ZEXT, dst, (Ref[]){tmp}, 1);
            return dst;
        }
        default:
            fatal("gen_expr: unknown node kind %d\n", node->kind);
    }
    return R;
}

static void gen_if(Node *node) {
    Blk *t_blk = new_blk();
    Blk *f_blk = node->els ? new_blk() : NULL;
    Blk *m_blk = new_blk();

    // cond
    Ref tmp = gen_stmt(node->cond);
    Ref cond = TMP(tmp_id++, ty_i1);
    new_ins(IR_CMP_NE, cond, (Ref[]){tmp, INT(0)}, 2);

    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = t_blk;
    curb->succ2 = f_blk ? f_blk : m_blk;

    // then
    curb = t_blk;
    insert_blk(curb);
    gen_stmt(node->then);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;

    // else
    if (f_blk) {
        curb = f_blk;
        insert_blk(curb);
        gen_stmt(node->els);
        curb->jmp.type = IR_JMP;
        curb->succ1 = m_blk;
    }
    curb = m_blk;
    insert_blk(curb);
}

static void gen_for(Node *node) {
    Blk *cond_blk = new_blk();
    Blk *body_blk = new_blk();
    Blk *incr_blk = new_blk();
    Blk *merge_blk = new_blk();

    Blk *brk = brk_blk;
    Blk *cont = cont_blk;
    brk_blk = merge_blk;
    cont_blk = incr_blk;

    // init
    gen_stmt(node->init);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;

    // cond
    curb = cond_blk;
    insert_blk(curb);
    if (node->cond) {
        Ref tmp = gen_expr(node->cond);
        Ref cond = TMP(tmp_id++, ty_i1);
        new_ins(IR_CMP_NE, cond, (Ref[]){tmp, INT(0)}, 2);

        curb->jmp.type = IR_JNZ;
        curb->jmp.arg = cond;
        curb->succ1 = body_blk;
        curb->succ2 = merge_blk;
    } else {
        curb->jmp.type = IR_JMP;
        curb->succ1 = body_blk;
    }

    // body
    curb = body_blk;
    insert_blk(curb);
    gen_stmt(node->body);
    curb->jmp.type = IR_JMP;
    curb->succ1 = incr_blk;

    // incr
    curb = incr_blk;
    insert_blk(curb);
    gen_expr(node->inc);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;

    curb = merge_blk;
    insert_blk(curb);

    brk_blk = brk;
    cont_blk = cont;
}

static void gen_while(Node *node) {
    Blk *cond_blk = new_blk();
    Blk *body_blk = new_blk();
    Blk *merge_blk = new_blk();

    Blk *brk = brk_blk;
    Blk *cont = cont_blk;
    brk_blk = merge_blk;
    cont_blk = cond_blk;

    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;

    // cond
    curb = cond_blk;
    insert_blk(curb);
    Ref tmp = gen_expr(node->cond);
    Ref cond = TMP(tmp_id++, ty_i1);
    new_ins(IR_CMP_NE, cond, (Ref[]){tmp, INT(0)}, 2);

    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = body_blk;
    curb->succ2 = merge_blk;

    // body
    curb = body_blk;
    insert_blk(curb);
    gen_stmt(node->body);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;

    curb = merge_blk;
    insert_blk(curb);

    brk_blk = brk;
    cont_blk = cont;
}

static void gen_do(Node *node) {
    Blk *body_blk = new_blk();
    Blk *cond_blk = new_blk();
    Blk *merge_blk = new_blk();

    Blk *brk = brk_blk;
    Blk *cont = cont_blk;
    brk_blk = merge_blk;
    cont_blk = cond_blk;

    curb->jmp.type = IR_JMP;
    curb->succ1 = body_blk;

    // body
    curb = body_blk;
    insert_blk(curb);
    gen_stmt(node->body);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;

    // cond
    curb = cond_blk;
    insert_blk(curb);
    Ref tmp = gen_expr(node->cond);
    Ref cond = TMP(tmp_id++, ty_i1);
    new_ins(IR_CMP_NE, cond, (Ref[]){tmp, INT(0)}, 2);

    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = body_blk;
    curb->succ2 = merge_blk;

    curb = merge_blk;
    insert_blk(curb);

    brk_blk = brk;
    cont_blk = cont;
}

static void gen_switch(Node *n) {
    Blk *merge_blk = new_blk();
    int i = 0;
    for (Node *y = n->case_next; y; y = y->case_next) {
        y->blk = new_blk();
        ++i;
    }
    curb->narg = i;

    if (n->default_case) n->default_case->blk = new_blk();

    Blk *brk = brk_blk;
    brk_blk = merge_blk;

    Ref cond = gen_stmt(n->cond);
    curb->jmp.type = IR_SWITCH;
    curb->jmp.arg = cond;

    curb->jmp.args = emalloc(i * sizeof(Ref));
    curb->succ = emalloc(i * sizeof(Blk *));
    if (n->default_case)
        curb->succ1 = n->default_case->blk;
    else
        curb->succ1 = merge_blk;

    Node *y = n->case_next;
    for (int j = 0; j < i; ++j) {
        curb->jmp.args[j] = cond.ty->size == 8 ? LONG(y->val) : INT(y->val);
        curb->succ[j] = y->blk;
        y = y->case_next;
    }

    curb = unreach;
    gen_stmt(n->body);

    curb->jmp.type = IR_JMP;
    curb->succ1 = merge_blk;
    curb = merge_blk;
    insert_blk(curb);
    brk_blk = brk;
}

static void gen_label(Node *n) {
    curb->jmp.type = IR_JMP;
    curb->succ1 = n->blk;
    curb = n->blk;
    insert_blk(curb);
    gen_stmt(n->label_body);
}

static void gen_case(Node *n) {
    curb->jmp.type = IR_JMP;
    curb->succ1 = n->blk;
    curb = n->blk;
    insert_blk(curb);
    gen_stmt(n->label_body);
}

static void gen_goto(Node *n) {
    curb->jmp.type = IR_JMP;
    curb->succ1 = n->target->blk;
    curb = unreach;
}

static void gen_break(Node *n) {
    (void)n;
    curb->jmp.type = IR_JMP;
    curb->succ1 = brk_blk;
    curb = unreach;
}

static void gen_continue(Node *n) {
    (void)n;
    curb->jmp.type = IR_JMP;
    curb->succ1 = cont_blk;
    curb = unreach;
}

static void gen_ret(Node *n) {
    Ref result = gen_expr(n->lhs);
    if (!refeq(result, R)) {
        Ref ops[] = {result, SLOT(curf->nparam + 1, curf->ty->ret), INT(curf->ty->ret->align)};
        new_ins(IR_STR, R, ops, 3);
    }

    curb->jmp.type = IR_JMP;
    curb->succ1 = curf->end;
    curb = unreach;
}

static Ref gen_stmt(Node *node) {
    if (!node) return R;
    Ref reg;
    switch (node->kind) {
        case ND_IF:
            gen_if(node);
            break;
        case ND_FOR:
            gen_for(node);
            break;
        case ND_WHILE:
            gen_while(node);
            break;
        case ND_DO:
            gen_do(node);
            break;
        case ND_SWITCH:
            gen_switch(node);
            break;
        case ND_CASE:
            gen_case(node);
            break;
        case ND_GOTO:
            gen_goto(node);
            break;
        case ND_BREAK:
            gen_break(node);
            break;
        case ND_CONTINUE:
            gen_continue(node);
            break;
        case ND_LABEL:
            gen_label(node);
            break;
        case ND_DECL:
        case ND_COMP_STMT:
            for (Node *n = node->body; n; n = n->next) reg = gen_stmt(n);
            return reg;
        case ND_RETURN:
            gen_ret(node);
            break;
        case ND_EXPR_STMT:
            return gen_expr(node->lhs);
        default:
            return gen_expr(node);
    }
    return R;
}

Module *irgen(Module *md) {
    curm = md;
    for (Sym *fn = md->fns; fn; fn = fn->next) {
        curf = fn;
        tmp_id = fn->nparam;
        tail = &dummy;
        fn->start = new_blk();
        fn->end = new_blk();
        for (Node *y = fn->labels; y; y = y->goto_next) y->blk = new_blk();
        brk_blk = cont_blk = NULL;

        curb = fn->start;
        insert_blk(curb);

        Type *ty = fn->ty->ret;
        bool is_valid = ty->kind != TY_VOID;
        // Entry
        if (is_valid) new_ins(IR_ALLOCA, TMP(tmp_id++, pointer_to(ty, 0)), (Ref[]){INT(ty->align)}, 1);

        for (Sym *var = fn->locals; var; var = var->next)
            new_ins(IR_ALLOCA, TMP(var->vreg = tmp_id++, pointer_to(var->ty, 0)), (Ref[]){INT(var->align)}, 1);

        Sym *var = fn->locals;
        for (uint32_t i = 0; i < fn->nparam; ++i, var = var->next)
            new_ins(IR_STR, R, (Ref[]){TMP(i, var->ty), TMP(var->vreg, var->ty), INT(var->align)}, 3);

        // Body
        gen_stmt(fn->body);

        // End
        curb->jmp.type = IR_JMP;
        curb = curb->succ1 = fn->end;
        insert_blk(curb);

        if (is_valid) new_ins(IR_LORD, TMP(tmp_id, ty), (Ref[]){SLOT(curf->nparam + 1, ty), INT(ty->align)}, 2);
        curb->jmp.type = IR_RET;
        curb->jmp.arg = is_valid ? TMP(tmp_id, fn->ty->ret) : R;
    }
    return md;
}
