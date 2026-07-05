#include <stdio.h>

#include "cxx.h"

static Blk *curb;
static Blk *entry;
static Blk *end;
static Blk *tail;
int tmp_id = 1;

static Ir *new_ins(IrKind op, Ref dst, Ref arg1, Ref arg2) {
    Ir *new = emalloc(sizeof(Ir));
    new->op = op;
    new->dst = dst;
    new->args[0] = arg1;
    new->args[1] = arg2;

    if (curb->head)
        curb->tail = curb->tail->next = new;
    else
        curb->head = curb->tail = new;
    return new;
}

Blk *new_blk(void) {
    static int blk_id = 0;
    Blk *b = emalloc(sizeof(Blk));
    memset(b, 0, sizeof(*b));
    b->blk_id = blk_id++;
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
    Ref reg, addr;
    switch (node->kind) {
        case ND_NUM:
            return INT(node->val);
        case ND_VAR:
            addr = gen_addr(node);
            reg = TMP(tmp_id++, node->ty);
            new_ins(IR_LORD, reg, addr, R);
            return reg;
        case ND_DEREF:
            addr = gen_expr(node->lhs);
            reg = TMP(tmp_id++, node->ty);
            new_ins(IR_LORD, reg, addr, R);
            return reg;
        case ND_ADDR:
            return gen_addr(node->lhs);
        case ND_AS:
            addr = gen_addr(node->lhs);
            addr.ty = node->lhs->ty;
            reg = gen_expr(node->rhs);
            reg.ty = node->rhs->ty;
            new_ins(IR_STR, addr, reg, R);
            return reg;
        default:
            break;
    }

    Ref lr = gen_expr(node->lhs);
    lr.ty = node->lhs->ty;
    reg = TMP(tmp_id++, node->ty);

    // unary arithmetic operation
    switch (node->kind) {
        case ND_PLUS:
            return lr;
        case ND_NEG:
            if (node->lhs->kind == ND_NUM) return INT(-lr.val);
            new_ins(IR_SUB, reg, INT(0), lr);
            return reg;
        case ND_INVERT:
            new_ins(IR_XOR, reg, lr, INT(-1));
            return reg;
        case ND_NOT: {
            Ref tmp = TMP(tmp_id++, ty_i1);
            new_ins(IR_CMP_EQ, tmp, lr, INT(0));
            new_ins(IR_ZEXT, reg, tmp, R);
            return reg;
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
            new_ins(IR_SUB, tmp, INT(0), rr);
            Ref sext_reg = TMP(tmp_id++, ty_i64);
            new_ins(IR_SEXT, sext_reg, tmp, R);
            new_ins(IR_GETELEMPTR, reg, lr, sext_reg);
            break;
        }
        case ND_PTRADD: {
            Ref sext_reg = TMP(tmp_id++, ty_i64);
            new_ins(IR_SEXT, sext_reg, rr, R);
            new_ins(IR_GETELEMPTR, reg, lr, sext_reg);
            break;
        }
        // binary arithmetic operation
        case ND_ADD:
            new_ins(IR_ADD, reg, lr, rr);
            break;
        case ND_SUB:
            new_ins(IR_SUB, reg, lr, rr);
            break;
        case ND_MUL:
            new_ins(IR_MUL, reg, lr, rr);
            break;
        case ND_DIV:
            new_ins(IR_DIV, reg, lr, rr);
            break;
        case ND_MOD:
            new_ins(IR_REM, reg, lr, rr);
            break;
        // shift operation
        case ND_LEFT:
            new_ins(IR_SHL, reg, lr, rr);
            break;
        case ND_RIGHT:
            new_ins(IR_SHR, reg, lr, rr);
            break;
        // bit operation
        case ND_BAND:
            new_ins(IR_AND, reg, lr, rr);
            break;
        case ND_BOR:
            new_ins(IR_OR, reg, lr, rr);
            break;
        case ND_XOR:
            new_ins(IR_XOR, reg, lr, rr);
            break;
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
            new_ins(cmp_op[node->kind], tmp, lr, rr);
            new_ins(IR_ZEXT, reg, tmp, R);
            return reg;
        }
        default:
            fprintf(stderr, "gen_expr: unknown node kind %d\n", node->kind);
            exit(1);
    }
    return reg;
}

static void gen_if(Node *node) {
    Blk *t_blk = new_blk();
    Blk *f_blk = node->els ? new_blk() : NULL;
    Blk *m_blk = new_blk();

    // cond
    Ref tmp = gen_expr(node->cond);
    Ref cond = TMP(tmp_id++, ty_i1);
    new_ins(IR_CMP_NE, cond, tmp, INT(0));
    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = t_blk;
    curb->succ2 = f_blk ? f_blk : m_blk;

    // then
    curb = t_blk;
    gen_stmt(node->then);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;

    // else
    if (f_blk) {
        curb = f_blk;
        gen_stmt(node->els);
        curb->jmp.type = IR_JMP;
        curb->succ1 = m_blk;
    }
    curb = m_blk;
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
    if (node->cond) {
        Ref tmp = gen_expr(node->cond);
        Ref cond = TMP(tmp_id++, ty_i1);
        new_ins(IR_CMP_NE, cond, tmp, INT(0));
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
    gen_stmt(node->body);
    // incr
    gen_expr(node->inc);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;

    curb = merge_blk;
}

static void gen_while(Node *node) {
    Blk *cond_blk = new_blk();
    Blk *body_blk = new_blk();
    Blk *merge_blk = new_blk();

    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;

    // cond
    curb = cond_blk;
    Ref tmp = gen_expr(node->cond);
    Ref cond = TMP(tmp_id++, ty_i1);
    new_ins(IR_CMP_NE, cond, tmp, INT(0));
    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = body_blk;
    curb->succ2 = merge_blk;

    // body
    curb = body_blk;
    gen_stmt(node->body);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;

    curb = merge_blk;
}

static void gen_do(Node *node) {
    Blk *body_blk = new_blk();
    Blk *cond_blk = new_blk();
    Blk *merge_blk = new_blk();

    curb->jmp.type = IR_JMP;
    curb->succ1 = body_blk;

    // body
    curb = body_blk;
    gen_stmt(node->body);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;

    // cond
    curb = cond_blk;
    Ref tmp = gen_expr(node->cond);
    Ref cond = TMP(tmp_id++, ty_i1);
    new_ins(IR_CMP_NE, cond, tmp, INT(0));
    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = body_blk;
    curb->succ2 = merge_blk;

    curb = merge_blk;
}

static void gen_ret(Node *n) {
    Ref result = gen_expr(n->lhs);
    if (!refeq(result, R)) new_ins(IR_STR, SLOT(1, ty_int), result, R);

    curb->jmp.type = IR_JMP;
    curb->succ1 = end;
    curb = end;
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

Blk *irgen(Function *prog) {
    Blk dummy;
    tail = &dummy;
    curb = entry = new_blk();
    end = new_blk();
    tail = entry;

    // Entry
    new_ins(IR_ALLOCA, TMP(tmp_id++, ty_int), R, R);
    for (Obj *var = prog->locals; var; var = var->next) new_ins(IR_ALLOCA, TMP(var->vreg = tmp_id++, var->ty), R, R);

    gen_stmt(prog->body);

    tail->next = end;
    curb->jmp.type = IR_JMP;
    curb->succ1 = end;

    curb = end;
    new_ins(IR_LORD, TMP(tmp_id, ty_int), SLOT(1, ty_int), R);
    curb->jmp.type = IR_RET;
    curb->jmp.arg = TMP(tmp_id, ty_int);
    return entry;
}

static void print_operand(Ref r) {
    if (r.type == RInt)
        printf("%d", r.val);
    else if (r.type == RTmp)
        printf("%%tmp%d", r.val);
    else if (r.type == RSlot) {
        printf("%%tmp%d", r.val);
    }
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

void dump_blk(Blk *b) {
    printf("blk%d:\n", b->blk_id);

    Ir *ir = b->head;
    while (ir) {
        printf("  ");
        if (ir->op != IR_STR) printf("%%tmp%d = ", ir->dst.val);

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
                print_operand(ir->dst);
                printf(", align %d\n", ir->dst.ty->align);
                break;
            case IR_GETELEMPTR:
                printf("getelementptr %s ", ty_str[ir->args[0].ty->base->kind]);
                printf(", ptr ");
                print_operand(ir->args[0]);
                printf(", i64 ");
                print_operand(ir->args[1]);
                printf("\n");
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
            printf("br label %%blk%d\n", b->succ1->blk_id);
            break;
        case IR_JNZ:
            printf("br i1 ");
            print_operand(b->jmp.arg);
            printf(", label %%blk%d, label %%blk%d\n", b->succ1->blk_id, b->succ2->blk_id);
            break;
        default:
            break;
    }
}

void dump_fn(Blk *b) {
    printf("define i32 @main() {\n");
    Blk *cur = b;
    while (cur) {
        dump_blk(cur);
        cur = cur->next;
    }
    printf("}\n");
}
