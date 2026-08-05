#include "cxx.h"

static Module *curm;
static Sym *curf;
static Blk *curb;
static Blk dummy;
static Blk *tail;
static Blk *unreach = &(Blk){};
static int tmp_id;
static Blk *brk_blk;
static Blk *cont_blk;

static Ref gen_stmt(Node *node);
static Ref gen_expr(Node *node);
static Ref gen_cond(Node *node);
static Ref gen_logand(Node *node);
static Ref gen_logor(Node *node);

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

    new->prev = curb->tail;
    new->next = NULL;
    if (curb->head)
        curb->tail = curb->tail->next = new;
    else
        curb->head = curb->tail = new;

    return new;
}

static Blk *new_blk(void) {
    Blk *b = emalloc(sizeof(Blk));
    memset(b, 0, sizeof(Blk));
    b->pred = vnew(2, sizeof(Blk *));
    return b;
}

static void insert_blk(Blk *b) {
    b->blk_id = tmp_id++;
    tail = tail->next = b;
}

static void add_pred(Blk *bp, Blk *b) {
    if (!b || bp == curf->end || bp == unreach) {
        return;
    }
    for (uint32_t i = 0; i < b->num_pred; i++) {
        if (b->pred[i] == bp) return;
    }
    b->pred = vgrow(b->pred, b->num_pred + 1);
    b->pred[b->num_pred++] = bp;
}

static Phi *new_phi(Ref res) {
    Phi *new = emalloc(sizeof(Phi));
    new->result = res;
    new->arg = vnew(0, sizeof(Ref));
    new->blk = vnew(0, sizeof(Blk *));
    new->num_arg = 0;
    new->next = NULL;
    return new;
}

static void add_phi_arg(Phi *phi, Blk *blk, Ref arg) {
    phi->num_arg++;
    phi->arg = vgrow(phi->arg, phi->num_arg);
    phi->blk = vgrow(phi->blk, phi->num_arg);
    phi->arg[phi->num_arg - 1] = arg;
    phi->blk[phi->num_arg - 1] = blk;
}

static void insert_phi(Blk *blk, Phi *phi) {
    phi->next = blk->phi;
    blk->phi = phi;
}

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
        Ref zr = INT(0);
        zr.ty = src_ty;
        new_ins(IR_CMP_NE, tmp, (Ref[]){val, zr}, 2);

        Ref dst = TMP(tmp_id++, target_ty);
        new_ins(IR_EXT, dst, (Ref[]){tmp}, 1);
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
    if (is_flonum(src_ty) && is_integer(target_ty)) {
        Ref dst = TMP(tmp_id++, target_ty);
        new_ins(IR_FPTOINT, dst, (Ref[]){val}, 1);
        return dst;
    }
    if (is_integer(src_ty) && is_flonum(target_ty)) {
        Ref dst = TMP(tmp_id++, target_ty);
        new_ins(IR_INTTOFP, dst, (Ref[]){val}, 1);
        return dst;
    }

    if (target_ty->kind == TY_VOID) return val;
    if (target_ty->size == src_ty->size) {
        val.ty = target_ty;
        return val;
    }
    Ref dst = TMP(tmp_id++, target_ty);
    if (target_ty->size > src_ty->size)
        new_ins(IR_EXT, dst, (Ref[]){val}, 1);
    else
        new_ins(IR_TRUNC, dst, (Ref[]){val}, 1);

    return dst;
}

static Ref convert(Node *lhs, Type *target_ty) {
    Ref lr = gen_expr(lhs);
    if (lhs->ty->kind == TY_FUNC && lhs->kind == ND_VAR) return lr;
    if (lhs->ty->kind == TY_ARRAY && is_pointer(target_ty)) {
        Ref dst = TMP(tmp_id++, target_ty);
        new_ins(IR_GEP, dst, (Ref[]){lr, LONG(0)}, 2);
        return dst;
    }
    return cast(lr, lhs->ty, target_ty);
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
            if (node->ty->kind == TY_FLOAT) return FLOAT(node->val);
            if (node->ty->kind == TY_DOUBLE) return DOUBLE(node->val);
            if (node->ty->size == 1)
                dst = BOOL(node->val);
            else if (node->ty->size == 4)
                dst = INT(node->val);
            else
                dst = LONG(node->val);
            dst.ty = node->ty;
            return dst;
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
        case ND_ADDR:
            return gen_addr(node->lhs);
        case ND_DEREF:
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
            union {
                double f64;
                uint64_t bits;
            } u = {addend};
            Ref rr = is_flonum(node->ty) ? DOUBLE(u.bits) : INT(addend);
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
            call_ops[0] = gen_expr(node->func);

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
            if (is_flonum(node->ty)) {
                new_ins(IR_NEG, dst, (Ref[]){lr}, 1);
                return dst;
            }
            Ref zr = node->ty->size == 8 ? LONG(0) : INT(0);
            new_ins(IR_SUB, dst, (Ref[]){zr, lr}, 2);
            return dst;
        case ND_INVERT:
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_XOR, dst, (Ref[]){lr, INT(-1)}, 2);
            return dst;
        case ND_NOT: {
            Ref tmp = TMP(tmp_id++, ty_i1);
            Ref zr = INT(0);
            zr.ty = node->lhs->ty;
            new_ins(IR_CMP_EQ, tmp, (Ref[]){lr, zr}, 2);

            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_EXT, dst, (Ref[]){tmp}, 1);
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
            new_ins(IR_EXT, dst, (Ref[]){tmp}, 1);
            return dst;
        }
        default:
            fatal("gen_expr: unknown node kind %d\n", node->kind);
    }
    return R;
}

static Ref gen_cond(Node *node) {
    Blk *t_blk = new_blk();
    Blk *f_blk = new_blk();
    Blk *m_blk = new_blk();

    // cond
    Ref tmp = gen_expr(node->cond);
    Ref cond = TMP(tmp_id++, ty_i1);
    Ref zr = INT(0);
    zr.ty = tmp.ty;
    new_ins(IR_CMP_NE, cond, (Ref[]){tmp, zr}, 2);
    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = t_blk;
    curb->succ2 = f_blk;
    add_pred(curb, curb->succ1);
    add_pred(curb, curb->succ2);

    // then
    curb = t_blk;
    insert_blk(curb);
    Ref true_r = gen_expr(node->then);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;
    add_pred(curb, curb->succ1);

    // else
    curb = f_blk;
    insert_blk(curb);
    Ref false_r = gen_expr(node->els);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;
    add_pred(curb, curb->succ1);

    curb = m_blk;
    insert_blk(curb);

    if (node->ty->kind != TY_VOID) {
        Ref result = TMP(tmp_id++, node->ty);
        Phi *phi = new_phi(result);
        add_phi_arg(phi, t_blk, true_r);
        add_phi_arg(phi, f_blk, false_r);
        insert_phi(curb, phi);
        return result;
    }
    return R;
}

static Ref gen_logor(Node *node) {
    Blk *f_blk = new_blk();
    Blk *m_blk = new_blk();

    // lhs
    Ref lr = gen_expr(node->lhs);
    Ref cond = TMP(tmp_id++, ty_i1);
    Ref zr = INT(0);
    zr.ty = lr.ty;
    new_ins(IR_CMP_NE, cond, (Ref[]){lr, zr}, 2);
    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = m_blk;
    curb->succ2 = f_blk;
    add_pred(curb, curb->succ1);
    add_pred(curb, curb->succ2);
    Blk *sel = curb;

    // rhs
    curb = f_blk;
    insert_blk(curb);
    Ref rr = gen_expr(node->rhs);
    Ref res_r = TMP(tmp_id++, ty_i1);
    zr.ty = rr.ty;
    new_ins(IR_CMP_NE, res_r, (Ref[]){rr, zr}, 2);
    Ref r_ext = TMP(tmp_id++, ty_int);
    new_ins(IR_EXT, r_ext, (Ref[]){res_r}, 1);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;
    add_pred(curb, curb->succ1);

    curb = m_blk;
    insert_blk(curb);

    Ref result = TMP(tmp_id++, ty_int);
    Phi *phi = new_phi(result);
    add_phi_arg(phi, sel, INT(1));
    add_phi_arg(phi, f_blk, r_ext);
    insert_phi(curb, phi);
    return result;
}

static Ref gen_logand(Node *node) {
    Blk *t_blk = new_blk();
    Blk *m_blk = new_blk();
    // lhs
    Ref lr = gen_expr(node->lhs);
    Ref cond = TMP(tmp_id++, ty_i1);
    Ref zr = INT(0);
    zr.ty = lr.ty;
    new_ins(IR_CMP_NE, cond, (Ref[]){lr, zr}, 2);

    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = t_blk;
    curb->succ2 = m_blk;
    add_pred(curb, curb->succ1);
    add_pred(curb, curb->succ2);
    Blk *sel = curb;

    // rhs
    curb = t_blk;
    insert_blk(curb);
    Ref rr = gen_expr(node->rhs);
    Ref res_r = TMP(tmp_id++, ty_i1);
    zr.ty = rr.ty;
    new_ins(IR_CMP_NE, res_r, (Ref[]){rr, zr}, 2);
    Ref r_ext = TMP(tmp_id++, ty_int);
    new_ins(IR_EXT, r_ext, (Ref[]){res_r}, 1);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;
    add_pred(curb, curb->succ1);

    curb = m_blk;
    insert_blk(curb);
    Ref result = TMP(tmp_id++, ty_int);
    Phi *phi = new_phi(result);
    add_phi_arg(phi, t_blk, r_ext);
    add_phi_arg(phi, sel, INT(0));
    insert_phi(curb, phi);
    return result;
}

static void gen_if(Node *node) {
    Blk *t_blk = new_blk();
    Blk *f_blk = node->els ? new_blk() : NULL;
    Blk *m_blk = new_blk();

    // cond
    Ref tmp = gen_stmt(node->cond);
    Ref cond = TMP(tmp_id++, ty_i1);
    Ref zr = INT(0);
    zr.ty = tmp.ty;
    new_ins(IR_CMP_NE, cond, (Ref[]){tmp, zr}, 2);

    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = t_blk;
    curb->succ2 = f_blk ? f_blk : m_blk;
    add_pred(curb, curb->succ1);
    add_pred(curb, curb->succ2);

    // then
    curb = t_blk;
    insert_blk(curb);
    gen_stmt(node->then);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;
    add_pred(curb, curb->succ1);

    // else
    if (f_blk) {
        curb = f_blk;
        insert_blk(curb);
        gen_stmt(node->els);
        curb->jmp.type = IR_JMP;
        curb->succ1 = m_blk;
        add_pred(curb, curb->succ1);
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

    node->brk_blk = brk_blk = merge_blk;
    node->cont_blk = cont_blk = incr_blk;

    // init
    gen_stmt(node->init);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;
    add_pred(curb, curb->succ1);

    // cond
    curb = cond_blk;
    insert_blk(curb);
    if (node->cond) {
        Ref tmp = gen_expr(node->cond);
        Ref cond = TMP(tmp_id++, ty_i1);
        Ref zr = INT(0);
        zr.ty = tmp.ty;
        new_ins(IR_CMP_NE, cond, (Ref[]){tmp, zr}, 2);

        curb->jmp.type = IR_JNZ;
        curb->jmp.arg = cond;
        curb->succ1 = body_blk;
        curb->succ2 = merge_blk;
        add_pred(curb, curb->succ1);
        add_pred(curb, curb->succ2);
    } else {
        curb->jmp.type = IR_JMP;
        curb->succ1 = body_blk;
        add_pred(curb, curb->succ1);
    }

    // body
    curb = body_blk;
    insert_blk(curb);
    gen_stmt(node->body);
    curb->jmp.type = IR_JMP;
    curb->succ1 = incr_blk;
    add_pred(curb, curb->succ1);

    // incr
    curb = incr_blk;
    insert_blk(curb);
    gen_expr(node->inc);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;
    add_pred(curb, curb->succ1);

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
    node->brk_blk = brk_blk = merge_blk;
    node->cont_blk = cont_blk = cond_blk;

    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;
    add_pred(curb, curb->succ1);

    // cond
    curb = cond_blk;
    insert_blk(curb);
    Ref tmp = gen_expr(node->cond);
    Ref cond = TMP(tmp_id++, ty_i1);
    Ref zr = INT(0);
    zr.ty = tmp.ty;
    new_ins(IR_CMP_NE, cond, (Ref[]){tmp, zr}, 2);

    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = body_blk;
    curb->succ2 = merge_blk;
    add_pred(curb, curb->succ1);
    add_pred(curb, curb->succ2);

    // body
    curb = body_blk;
    insert_blk(curb);
    gen_stmt(node->body);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;
    add_pred(curb, curb->succ1);

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
    node->brk_blk = brk_blk = merge_blk;
    node->cont_blk = cont_blk = cond_blk;

    curb->jmp.type = IR_JMP;
    curb->succ1 = body_blk;
    add_pred(curb, curb->succ1);

    // body
    curb = body_blk;
    insert_blk(curb);
    gen_stmt(node->body);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;
    add_pred(curb, curb->succ1);

    // cond
    Ref zr = INT(0);
    curb = cond_blk;
    insert_blk(curb);
    Ref tmp = gen_expr(node->cond);
    Ref cond = TMP(tmp_id++, ty_i1);
    zr.ty = tmp.ty;
    new_ins(IR_CMP_NE, cond, (Ref[]){tmp, zr}, 2);

    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = body_blk;
    curb->succ2 = merge_blk;
    add_pred(curb, curb->succ1);
    add_pred(curb, curb->succ2);

    curb = merge_blk;
    insert_blk(curb);

    brk_blk = brk;
    cont_blk = cont;
}

static void gen_switch(Node *n) {
    Blk *merge_blk = new_blk();
    int i = 0;
    for (Node *y = n->case_next; y; y = y->case_next) {
        if (!y->blk) y->blk = new_blk();
        Node *tmp = y->label_ring;
        while (tmp != y) {
            tmp->blk = y->blk;
            tmp = tmp->label_ring;
        }
        ++i;
    }
    curb->narg = i;

    if (n->default_case && !n->default_case->blk) n->default_case->blk = new_blk();

    Blk *brk = brk_blk;
    n->brk_blk = brk_blk = merge_blk;

    Ref cond = gen_stmt(n->cond);
    curb->jmp.type = IR_SWITCH;
    curb->jmp.arg = cond;

    curb->jmp.args = emalloc(i * sizeof(Ref));
    curb->succ = emalloc(i * sizeof(Blk *));
    if (n->default_case)
        curb->succ1 = n->default_case->blk;
    else
        curb->succ1 = merge_blk;
    add_pred(curb, curb->succ1);

    Node *y = n->case_next;
    for (int j = 0; j < i; ++j) {
        curb->jmp.args[j] = cond.ty->size == 8 ? LONG(y->val) : INT(y->val);
        curb->succ[j] = y->blk;
        add_pred(curb, curb->succ[j]);
        y = y->case_next;
    }

    curb = unreach;
    gen_stmt(n->body);

    curb->jmp.type = IR_JMP;
    curb->succ1 = merge_blk;
    add_pred(curb, curb->succ1);
    curb = merge_blk;
    insert_blk(curb);
    brk_blk = brk;
}

static void gen_label(Node *n) {
    curb->jmp.type = IR_JMP;
    curb->succ1 = n->blk;
    add_pred(curb, curb->succ1);
    curb = n->blk;
    insert_blk(curb);
    gen_stmt(n->label_body);
}

static void gen_case(Node *n) {
    curb->jmp.type = IR_JMP;
    curb->succ1 = n->blk;
    add_pred(curb, curb->succ1);
    curb = n->blk;
    insert_blk(curb);
    gen_stmt(n->label_body);
}

static void gen_goto(Node *n) {
    curb->jmp.type = IR_JMP;
    curb->succ1 = n->target->blk;
    add_pred(curb, curb->succ1);
    curb = unreach;
}

static void gen_break(Node *n) {
    curb->jmp.type = IR_JMP;
    if (n->target) {
        Node *target = n->target;
        while (target->kind == ND_LABEL) target = target->label_body;
        curb->succ1 = target->brk_blk;
    } else {
        curb->succ1 = brk_blk;
    }
    add_pred(curb, curb->succ1);
    curb = unreach;
}

static void gen_continue(Node *n) {
    curb->jmp.type = IR_JMP;
    curb->succ1 = n->target ? n->target->label_body->cont_blk : cont_blk;
    add_pred(curb, curb->succ1);
    curb = unreach;
}

static void gen_ret(Node *n) {
    Ref result = gen_expr(n->lhs);
    if (!refeq(result, R)) {
        Type *ty = curf->ty;
        Ref ops[] = {result, SLOT(ty->nparam + 1, pointer_to(ty->ret, 0)), INT(ty->ret->align)};
        new_ins(IR_STR, R, ops, 3);
    }

    curb->jmp.type = IR_JMP;
    curb->succ1 = curf->end;
    add_pred(curb, curb->succ1);
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
        if (!fn->is_defined) continue;
        curf = fn;
        uint32_t nparam = tmp_id = fn->ty->nparam;
        tail = &dummy;
        fn->start = new_blk();
        fn->end = new_blk();
        for (Node *y = fn->labels; y; y = y->goto_next) {
            if (!y->blk) y->blk = new_blk();
            Node *tmp = y->label_ring;
            while (tmp != y) {
                tmp->blk = y->blk;
                tmp = tmp->label_ring;
            }
        }
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
        for (uint32_t i = 0; i < nparam; ++i, var = var->next)
            new_ins(IR_STR, R, (Ref[]){TMP(i, var->ty), TMP(var->vreg, pointer_to(var->ty, 0)), INT(var->align)}, 3);

        // Body
        gen_stmt(fn->body);

        // End
        curb->jmp.type = IR_JMP;
        curb = curb->succ1 = fn->end;
        insert_blk(curb);

        if (is_valid)
            new_ins(IR_LORD, TMP(tmp_id, ty), (Ref[]){SLOT(nparam + 1, pointer_to(ty, 0)), INT(ty->align)}, 2);
        curb->jmp.type = IR_RET;
        curb->jmp.arg = is_valid ? TMP(tmp_id, ty) : R;
    }
    return md;
}
