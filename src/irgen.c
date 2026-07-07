#include <stdio.h>

#include "cxx.h"

static Fn *curf;
static Blk *curb;
static Blk dummy;
static Blk *tail;
static Blk *unreach = &dummy;
static int tmp_id;

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
    memset(b, 0, sizeof(*b));
    tail = tail->next = b;
    return b;
}

static Ref gen_stmt(Node *node);
static Ref gen_expr(Node *node);

static Ref gen_addr(Node *node) {
    switch (node->kind) {
        case ND_VAR:
            return SLOT(node->var->vreg, node->ty);
        case ND_DEREF:
            return gen_expr(node->lhs);
        default:
            exit(1);
    }
}

static Ref gen_expr(Node *node) {
    if (!node) return R;
    Ref dst;
    switch (node->kind) {
        case ND_NUM:
            return INT(node->val);
        case ND_VAR: {
            Ref ops[1] = {gen_addr(node)};
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_LORD, dst, ops, 1);
            return dst;
        }
        case ND_DEREF: {
            Ref ops[1] = {gen_expr(node->lhs)};
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_LORD, dst, ops, 1);
            return dst;
        }
        case ND_ADDR:
            return gen_addr(node->lhs);
        case ND_AS: {
            Ref addr = gen_addr(node->lhs);
            addr.ty = node->lhs->ty;
            dst = gen_expr(node->rhs);
            dst.ty = node->rhs->ty;
            Ref ops[2] = {dst, addr};
            new_ins(IR_STR, R, ops, 2);
            return dst;
        }
        case ND_FUNCALL: {
            int nargs = node->narg;
            Ref call_ops[nargs + 1];
            call_ops[0] = GLB(node->func);

            int idx = 1;
            for (Node *arg = node->args; arg; arg = arg->next) call_ops[idx++] = gen_expr(arg);

            Ref dst = TMP(tmp_id++, ty_int);
            new_ins(IR_CALL, dst, call_ops, nargs + 1);
            return dst;
        }
        default:
            break;
    }

    Ref lr = gen_expr(node->lhs);
    lr.ty = node->lhs->ty;
    if (node->kind == ND_PLUS) return lr;

    // unary arithmetic operation
    switch (node->kind) {
        case ND_NEG:
            if (node->lhs->kind == ND_NUM) return INT(-lr.val);
            Ref ops[2] = {INT(0), lr};
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_SUB, dst, ops, 2);
            return dst;
        case ND_INVERT: {
            Ref ops[2] = {lr, INT(-1)};
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_XOR, dst, ops, 2);
            return dst;
        }
        case ND_NOT: {
            Ref tmp = TMP(tmp_id++, ty_i1);
            Ref cmp_ops[2] = {lr, INT(0)};
            new_ins(IR_CMP_EQ, tmp, cmp_ops, 2);
            Ref zext_ops[1] = {tmp};
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_ZEXT, dst, zext_ops, 1);
            return dst;
        }
        default:
            break;
    }

    Ref rr = gen_expr(node->rhs);
    rr.ty = node->rhs->ty;
    if (node->kind == ND_COMMA) return rr;

    switch (node->kind) {
        case ND_PTRSUB: {
            Ref tmp = TMP(tmp_id++, rr.ty);
            Ref sub_ops[2] = {INT(0), rr};
            new_ins(IR_SUB, tmp, sub_ops, 2);
            Ref sext_reg = TMP(tmp_id++, ty_i64);
            Ref sext_ops[1] = {tmp};
            new_ins(IR_SEXT, sext_reg, sext_ops, 1);
            Ref gep_ops[2] = {lr, sext_reg};
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_GETELEMPTR, dst, gep_ops, 2);
            break;
        }
        case ND_PTRADD: {
            Ref sext_reg = TMP(tmp_id++, ty_i64);
            Ref sext_ops[1] = {rr};
            new_ins(IR_SEXT, sext_reg, sext_ops, 1);
            Ref gep_ops[2] = {lr, sext_reg};
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_GETELEMPTR, dst, gep_ops, 2);
            break;
        }
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
            Ref ops[2] = {lr, rr};
            dst = TMP(tmp_id++, node->ty);
            new_ins(bin_op[node->kind], dst, ops, 2);
            break;
        }
        // Comparison operations：icmp return i1，zext to i32
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_GT:
        case ND_LE:
        case ND_GE: {
            static int cmp_op[] = {
                [ND_EQ] = IR_CMP_EQ, [ND_NE] = IR_CMP_NE, [ND_LT] = IR_CMP_LT,
                [ND_GT] = IR_CMP_GT, [ND_LE] = IR_CMP_LE, [ND_GE] = IR_CMP_GE,
            };
            Ref tmp = TMP(tmp_id++, ty_i1);
            Ref cmp_ops[2] = {lr, rr};
            new_ins(cmp_op[node->kind], tmp, cmp_ops, 2);
            Ref zext_ops[1] = {tmp};
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_ZEXT, dst, zext_ops, 1);
            break;
        }
        default:
            fprintf(stderr, "gen_expr: unknown node kind %d\n", node->kind);
            exit(1);
    }
    return dst;
}

static void gen_if(Node *node) {
    Blk *t_blk = new_blk();
    Blk *f_blk = node->els ? new_blk() : NULL;
    Blk *m_blk = new_blk();

    // cond
    Ref tmp = gen_expr(node->cond);
    Ref cond = TMP(tmp_id++, ty_i1);
    Ref cmp_ops[2] = {tmp, INT(0)};
    new_ins(IR_CMP_NE, cond, cmp_ops, 2);
    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = t_blk;
    curb->succ2 = f_blk ? f_blk : m_blk;

    // then
    curb = t_blk;
    curb->blk_id = tmp_id++;
    gen_stmt(node->then);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;

    // else
    if (f_blk) {
        curb = f_blk;
        curb->blk_id = tmp_id++;
        gen_stmt(node->els);
        curb->jmp.type = IR_JMP;
        curb->succ1 = m_blk;
    }
    curb = m_blk;
    curb->blk_id = tmp_id++;
}

static void gen_for(Node *node) {
    Blk *cond_blk = new_blk();
    Blk *body_blk = new_blk();
    Blk *merge_blk = new_blk();

    // init
    gen_expr(node->init);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;

    // cond
    curb = cond_blk;
    curb->blk_id = tmp_id++;
    if (node->cond) {
        Ref tmp = gen_expr(node->cond);
        Ref cond = TMP(tmp_id++, ty_i1);
        Ref cmp_ops[2] = {tmp, INT(0)};
        new_ins(IR_CMP_NE, cond, cmp_ops, 2);
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
    curb->blk_id = tmp_id++;
    gen_stmt(node->body);
    // incr
    gen_expr(node->inc);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;

    curb = merge_blk;
    curb->blk_id = tmp_id++;
}

static void gen_while(Node *node) {
    Blk *cond_blk = new_blk();
    Blk *body_blk = new_blk();
    Blk *merge_blk = new_blk();

    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;

    // cond
    curb = cond_blk;
    curb->blk_id = tmp_id++;
    Ref tmp = gen_expr(node->cond);
    Ref cond = TMP(tmp_id++, ty_i1);
    Ref cmp_ops[2] = {tmp, INT(0)};
    new_ins(IR_CMP_NE, cond, cmp_ops, 2);
    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = body_blk;
    curb->succ2 = merge_blk;

    // body
    curb = body_blk;
    curb->blk_id = tmp_id++;
    gen_stmt(node->body);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;

    curb = merge_blk;
    curb->blk_id = tmp_id++;
}

static void gen_do(Node *node) {
    Blk *body_blk = new_blk();
    Blk *cond_blk = new_blk();
    Blk *merge_blk = new_blk();

    curb->jmp.type = IR_JMP;
    curb->succ1 = body_blk;

    // body
    curb = body_blk;
    curb->blk_id = tmp_id++;
    gen_stmt(node->body);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;

    // cond
    curb = cond_blk;
    curb->blk_id = tmp_id++;
    Ref tmp = gen_expr(node->cond);
    Ref cond = TMP(tmp_id++, ty_i1);
    Ref cmp_ops[2] = {tmp, INT(0)};
    new_ins(IR_CMP_NE, cond, cmp_ops, 2);
    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = body_blk;
    curb->succ2 = merge_blk;

    curb = merge_blk;
    curb->blk_id = tmp_id++;
}

static void gen_ret(Node *n) {
    Ref result = gen_expr(n->lhs);
    if (!refeq(result, R)) {
        Ref ops[2] = {result, SLOT(curf->nparam + 1, ty_int)};
        new_ins(IR_STR, R, ops, 2);
    }

    curb->jmp.type = IR_JMP;
    curb->succ1 = curf->end;
    curb = unreach;
}

static Ref gen_stmt(Node *node) {
    Ref reg = R;
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
        case ND_DECL:
        case ND_COMP_STMT:
            for (Node *n = node->body; n; n = n->next) reg = gen_stmt(n);
            break;
        case ND_RETURN:
            gen_ret(node);
            break;
        case ND_EXPR_STMT:
            reg = gen_expr(node->lhs);
            break;
        default:
            exit(1);
    }
    return reg;
}

Fn *irgen(Fn *prog) {
    for (Fn *fn = prog; fn; fn = fn->next) {
        curf = fn;
        tmp_id = fn->nparam;
        tail = &dummy;
        fn->start = new_blk();
        fn->end = emalloc(sizeof(Blk));

        curb = fn->start;
        curb->blk_id = tmp_id++;
        // Entry
        new_ins(IR_ALLOCA, TMP(tmp_id++, ty_int), NULL, 0);

        for (Obj *var = fn->locals; var; var = var->next)
            new_ins(IR_ALLOCA, TMP(var->vreg = tmp_id++, var->ty), NULL, 0);

        Obj *var = fn->locals;
        for (uint32_t i = 0; i < fn->nparam; ++i) {
            Ref ops[2] = {TMP(i, var->ty), TMP(var->vreg, var->ty)};
            new_ins(IR_STR, R, ops, 2);
            var = var->next;
        }

        // Body
        gen_stmt(fn->body);

        // End
        curb->jmp.type = IR_JMP;
        curb = curb->succ1 = fn->end;
        curb->blk_id = tmp_id++;

        Ref load_ops[1] = {SLOT(curf->nparam + 1, ty_int)};
        new_ins(IR_LORD, TMP(tmp_id, ty_int), load_ops, 1);
        curb->jmp.type = IR_RET;
        curb->jmp.arg = TMP(tmp_id, ty_int);
        tail->next = fn->end;
    }
    return prog;
}

static void print_operand(Ref r) {
    if (r.type == RInt)
        printf("%d", r.val);
    else
        printf("%%%d", r.val);
}

static void print_binop(const char *op, Ir *ir) {
    printf("%s i32 ", op);
    print_operand(ir->args[0]);
    printf(", ");
    print_operand(ir->args[1]);
    printf("\n");
}

static const char *ty_str[] = {
    [TY_I1] = "i1",
    [TY_I64] = "i64",
    [TY_INT] = "i32",
    [TY_PTR] = "ptr",
};

static void print_type(Type *ty) { printf("%s ", ty_str[ty->kind]); }

void dump_blk(Blk *b) {
    printf("%d:\n", b->blk_id);

    Ir *ir = b->head;
    while (ir) {
        printf("  ");
        if (ir->op != IR_STR) printf("%%%d = ", ir->dst.val);

        switch (ir->op) {
            case IR_ALLOCA:
                printf("alloca %s", ty_str[ir->dst.ty->kind]);
                printf(", align %d\n", ir->dst.ty->align);
                break;
            case IR_LORD:
                printf("load %s, ptr ", ty_str[ir->dst.ty->kind]);
                print_operand(ir->args[0]);
                printf(", align %d\n", ir->dst.ty->align);
                break;
            case IR_STR:
                printf("store %s ", ty_str[ir->args[0].ty->kind]);
                print_operand(ir->args[0]);
                printf(", ptr ");
                print_operand(ir->args[1]);
                printf(", align %d\n", ir->args[1].ty->align);
                break;
            case IR_GETELEMPTR:
                printf("getelementptr %s ", ty_str[ir->args[0].ty->ptr.base->kind]);
                printf(", ptr ");
                print_operand(ir->args[0]);
                printf(", i64 ");
                print_operand(ir->args[1]);
                printf("\n");
                break;
            case IR_CALL:
                printf("call i32 @%s(", globals[ir->args[0].val]);
                for (uint32_t i = 1; i < ir->narg; i++) {
                    print_type(ir->args[i].ty);
                    print_operand(ir->args[i]);
                    if (i < ir->narg - 1) printf(", ");
                }
                printf(")\n");
                break;
            case IR_ADD:
                print_binop("add", ir);
                break;
            case IR_SUB:
                print_binop("sub", ir);
                break;
            case IR_MUL:
                print_binop("mul", ir);
                break;
            case IR_DIV:
                print_binop("sdiv", ir);
                break;
            case IR_REM:
                print_binop("srem", ir);
                break;
            case IR_AND:
                print_binop("and", ir);
                break;
            case IR_OR:
                print_binop("or", ir);
                break;
            case IR_XOR:
                print_binop("xor", ir);
                break;
            case IR_SHL:
                print_binop("shl", ir);
                break;
            case IR_SHR:
                print_binop("ashr", ir);
                break;

            case IR_NEG:
                printf("sub i32 0, ");
                print_operand(ir->args[0]);
                printf("\n");
                break;
            case IR_SEXT:
                printf("sext %s ", ty_str[ir->args[0].ty->kind]);
                print_operand(ir->args[0]);
                printf(" to %s\n", ty_str[ir->dst.ty->kind]);
                break;
            case IR_ZEXT:
                printf("zext %s ", ty_str[ir->args[0].ty->kind]);
                print_operand(ir->args[0]);
                printf(" to %s\n", ty_str[ir->dst.ty->kind]);
                break;

            case IR_CMP_EQ:
                print_binop("icmp eq", ir);
                break;
            case IR_CMP_NE:
                print_binop("icmp ne", ir);
                break;
            case IR_CMP_GE:
                print_binop("icmp sge", ir);
                break;
            case IR_CMP_GT:
                print_binop("icmp sgt", ir);
                break;
            case IR_CMP_LE:
                print_binop("icmp sle", ir);
                break;
            case IR_CMP_LT:
                print_binop("icmp slt", ir);
                break;

            default:
                break;
        }
        ir = ir->next;
    }

    printf("  ");
    switch (b->jmp.type) {
        case IR_RET:
            printf("ret i32 ");
            print_operand(b->jmp.arg);
            printf("\n");
            break;
        case IR_JMP:
            printf("br label %%%d\n", b->succ1->blk_id);
            break;
        case IR_JNZ:
            printf("br i1 ");
            print_operand(b->jmp.arg);
            printf(", label %%%d, label %%%d\n", b->succ1->blk_id, b->succ2->blk_id);
            break;
        default:
            break;
    }
}

void dump_fn(Fn *fn) {
    printf("declare i32 @ret3()\n");
    printf("declare i32 @ret5()\n");
    printf("declare i32 @add(i32, i32)\n");
    printf("declare i32 @sub(i32, i32)\n");
    printf("declare i32 @add6(i32, i32, i32, i32, i32, i32)\n");
    Fn *curf = fn;
    while (curf) {
        printf("define i32 @%s(", curf->name);
        Obj *var = curf->locals;
        for (uint32_t i = 0; i < curf->nparam; i++) {
            print_type(var->ty);
            printf("%%%d", i);
            if (i < curf->nparam - 1) printf(", ");
            var = var->next;
        }
        printf(") {\n");
        Blk *curb = curf->start;
        while (curb) {
            dump_blk(curb);
            curb = curb->next;
        }
        printf("}\n");
        curf = curf->next;
    }
}
