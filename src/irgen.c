#include "cxx.h"

static FILE *out_file;
static Sym *curf;
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
    memset(b, 0, sizeof(Blk));
    tail = tail->next = b;
    return b;
}

static Ref gen_stmt(Node *node);
static Ref gen_expr(Node *node);

static Ref gen_addr(Node *node) {
    switch (node->kind) {
        case ND_VAR:
            if (node->var->is_local)
                return SLOT(node->var->vreg, pointer_to(node->ty));
            else
                // Global variable
                return GLB(node->var->id, pointer_to(node->ty));
        case ND_DEREF:
            return gen_expr(node->lhs);
        case ND_MEMBER: {
            Ref addr = gen_addr(node->lhs);
            if (node->lhs->ty->kind == TY_UNION) {
                addr.ty = pointer_to(node->member->ty);
                return addr;
            }
            int nmem = 0;
            for (Member *mem = node->member; mem; mem = mem->next) nmem++;
            Ref gep_ops[nmem + 2];
            gep_ops[0] = addr;
            gep_ops[1] = INT(0);

            int idx = 2;
            for (Member *mem = node->member; mem; mem = mem->next) gep_ops[idx++] = INT(mem->idx);

            Ref dst = TMP(tmp_id++, pointer_to(node->ty));
            new_ins(IR_GEP, dst, gep_ops, nmem + 2);
            return dst;
        }
        default:
    }
    printf("error\n");
    exit(1);
}

static Ref load(Ref addr, Type *ty) {
    Ref dst = TMP(tmp_id++, ty);
    new_ins(IR_LORD, dst, (Ref[]){addr}, 1);
    return dst;
}

static Ref convert(Node *node) {
    if (node->lhs->ty->kind == TY_ARRAY && is_pointer(node->ty)) {
        Ref addr = gen_addr(node->lhs);
        Ref dst = TMP(tmp_id++, node->ty);
        new_ins(IR_GEP, dst, (Ref[]){addr, LONG(0)}, 2);
        return dst;
    }
    Ref ops[1] = {gen_expr(node->lhs)};
    if (is_pointer(node->lhs->ty) && is_integer(node->ty)) {
        Ref dst = TMP(tmp_id++, node->ty);
        new_ins(IR_PTRTOINT, dst, ops, 1);
        return dst;
    }
    if (is_integer(node->lhs->ty) && is_pointer(node->ty)) {
        Ref dst = TMP(tmp_id++, node->ty);
        new_ins(IR_INTTOPTR, dst, ops, 1);
        return dst;
    }
    if (node->ty->kind == TY_VOID || node->ty->size == node->lhs->ty->size) {
        ops[0].ty = node->ty;
        return ops[0];
    }
    Ref dst = TMP(tmp_id++, node->ty);
    if (node->ty->size > node->lhs->ty->size)
        new_ins(IR_SEXT, dst, ops, 1);
    else
        new_ins(IR_TRUNC, dst, ops, 1);
    return dst;
}

static Ref gen_expr(Node *node) {
    if (!node) return R;
    Ref dst;
    switch (node->kind) {
        case ND_NUM:
            if (node->ty->size == 4)
                return INT(node->val);
            else
                return LONG(node->val);
        case ND_STMT_EXPR:
            for (Node *n = node->body; n; n = n->next) dst = gen_stmt(n);
            return dst;
        case ND_VAR:
        case ND_MEMBER:
            return load(gen_addr(node), node->ty);
        case ND_DEREF:
            return load(gen_expr(node->lhs), node->ty);
        case ND_ADDR:
            return gen_addr(node->lhs);
        case ND_IMCAST:
        case ND_EXCAST:
            return convert(node);
        case ND_AS: {
            Ref addr = gen_addr(node->lhs);
            if (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION) {
                Ref src = gen_addr(node->rhs);
                Ref ops[] = {addr, src, INT(node->ty->size)};
                new_ins(IR_MEMCPY, R, ops, 3);
                return R;
            }
            dst = gen_expr(node->rhs);
            dst.ty = addr.ty->base;
            new_ins(IR_STR, R, (Ref[]){dst, addr}, 2);
            return dst;
        }
        case ND_FUNCALL: {
            int nargs = node->narg;
            Ref call_ops[nargs + 1];
            call_ops[0] = GLB(node->func, ty_int);

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
            if (node->lhs->kind == ND_NUM) return INT(-lr.val);
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_SUB, dst, (Ref[]){INT(0), lr}, 2);
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
    Ref tmp = gen_expr(node->cond);
    Ref cond = TMP(tmp_id++, ty_i1);
    new_ins(IR_CMP_NE, cond, (Ref[]){tmp, INT(0)}, 2);

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
    new_ins(IR_CMP_NE, cond, (Ref[]){tmp, INT(0)}, 2);

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
    new_ins(IR_CMP_NE, cond, (Ref[]){tmp, INT(0)}, 2);

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
            fatal("unknown node kind %d", node->kind);
    }
    return R;
}

Module *irgen(Module *md) {
    for (Sym *fn = md->fns; fn; fn = fn->next) {
        curf = fn;
        tmp_id = fn->nparam;
        tail = &dummy;
        fn->start = new_blk();
        fn->end = emalloc(sizeof(Blk));
        memset(fn->end, 0, sizeof(Blk));

        curb = fn->start;
        curb->blk_id = tmp_id++;
        // Entry
        new_ins(IR_ALLOCA, TMP(tmp_id++, pointer_to(fn->ty->ret)), NULL, 0);

        for (Sym *var = fn->locals; var; var = var->next)
            new_ins(IR_ALLOCA, TMP(var->vreg = tmp_id++, pointer_to(var->ty)), NULL, 0);

        new_ins(IR_STR, R, (Ref[]){INT(0), TMP(fn->nparam + 1, ty_int)}, 2);

        Sym *var = fn->locals;
        for (uint32_t i = 0; i < fn->nparam; ++i, var = var->next)
            new_ins(IR_STR, R, (Ref[]){TMP(i, var->ty), TMP(var->vreg, var->ty)}, 2);

        // Body
        gen_stmt(fn->body);

        // End
        curb->jmp.type = IR_JMP;
        curb = curb->succ1 = fn->end;
        curb->blk_id = tmp_id++;

        new_ins(IR_LORD, TMP(tmp_id, fn->ty->ret), (Ref[]){SLOT(curf->nparam + 1, fn->ty->ret)}, 1);
        curb->jmp.type = IR_RET;
        curb->jmp.arg = TMP(tmp_id, fn->ty->ret);
        tail->next = fn->end;
    }
    return md;
}

static const char *op_str[] = {
    [IR_ADD] = "add",
    [IR_SUB] = "sub",
    [IR_MUL] = "mul",
    [IR_DIV] = "sdiv",
    [IR_REM] = "srem",
    [IR_AND] = "and",
    [IR_OR] = "or",
    [IR_XOR] = "xor",
    [IR_SHL] = "shl",
    [IR_SHR] = "ashr",
    [IR_CMP_EQ] = "icmp eq",
    [IR_CMP_NE] = "icmp ne",
    [IR_CMP_LE] = "icmp sle",
    [IR_CMP_LT] = "icmp slt",
    [IR_SEXT] = "sext",
    [IR_ZEXT] = "zext",
    [IR_TRUNC] = "trunc",
    [IR_PTRTOINT] = "ptrtoint",
    [IR_INTTOPTR] = "inttoptr",
};

static const char *ty_str[] = {
    [TY_VOID] = "void", [TY_I1] = "i1",    [TY_I64] = "i64",   [TY_CHAR] = "i8", [TY_SHORT] = "i16",
    [TY_INT] = "i32",   [TY_LONG] = "i64", [TY_LLONG] = "i64", [TY_PTR] = "ptr",
};

static void print_type(Type *ty) {
    if (!ty) {
        fprintf(out_file, "void");
        return;
    }
    if (ty->kind == TY_ARRAY) {
        fprintf(out_file, "[%d x ", ty->len);
        print_type(ty->base);
        fprintf(out_file, "]");
        return;
    }
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        fprintf(out_file, "%%%s", str(ty->uid));
        return;
    }
    fprintf(out_file, "%s", ty_str[ty->kind]);
}

static void print_operand(Ref r) {
    if (r.type == RInt)
        fprintf(out_file, "%d", r.val);
    else if (r.type == RGlb)
        fprintf(out_file, "@%s", str(r.val));
    else
        fprintf(out_file, "%%%d", r.val);
}

void dump_blk(Blk *b) {
    fprintf(out_file, "%d:\n", b->blk_id);

    Ir *ir = b->head;
    while (ir) {
        fprintf(out_file, "  ");
        if (!refeq(ir->dst, R)) fprintf(out_file, "%%%d = ", ir->dst.val);

        switch (ir->op) {
            // memmory
            case IR_ALLOCA:
                fprintf(out_file, "alloca ");
                print_type(ir->dst.ty->base);
                fprintf(out_file, ", align %d\n", ir->dst.ty->base->align);
                break;
            case IR_LORD:
                fprintf(out_file, "load ");
                print_type(ir->dst.ty);
                fprintf(out_file, ", ptr ");
                print_operand(ir->args[0]);
                fprintf(out_file, ", align %d\n", ir->dst.ty->align);
                break;
            case IR_STR:
                fprintf(out_file, "store ");
                print_type(ir->args[0].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[0]);
                fprintf(out_file, ", ptr ");
                print_operand(ir->args[1]);
                fprintf(out_file, ", align %d\n", ir->args[0].ty->align);
                break;
            case IR_GEP:
                fprintf(out_file, "getelementptr ");
                print_type(ir->args[0].ty->base);
                fprintf(out_file, ", ptr ");
                print_operand(ir->args[0]);
                for (uint32_t i = 1; i < ir->narg; i++) {
                    fprintf(out_file, ", ");
                    print_type(ir->args[i].ty);
                    fprintf(out_file, " ");
                    print_operand(ir->args[i]);
                }
                fprintf(out_file, "\n");
                break;
            case IR_MEMCPY:
                fprintf(out_file, "call void @llvm.memcpy.p0.p0.i64(ptr ");
                print_operand(ir->args[0]);
                fprintf(out_file, ", ptr ");
                print_operand(ir->args[1]);
                fprintf(out_file, ", i64 %d, i1 false)\n", ir->args[2].val);
                break;
            case IR_CALL:
                fprintf(out_file, "call ");
                if (refeq(ir->dst, R))
                    fprintf(out_file, "void");
                else
                    print_type(ir->dst.ty);
                fprintf(out_file, " @%s(", str(ir->args[0].val));
                for (uint32_t i = 1; i < ir->narg; i++) {
                    print_type(ir->args[i].ty);
                    fprintf(out_file, " ");
                    print_operand(ir->args[i]);
                    if (i < ir->narg - 1) fprintf(out_file, ", ");
                }
                fprintf(out_file, ")\n");
                break;
            // conversion
            case IR_SEXT:
            case IR_ZEXT:
            case IR_TRUNC:
            case IR_PTRTOINT:
            case IR_INTTOPTR:
                fprintf(out_file, "%s ", op_str[ir->op]);
                print_type(ir->args[0].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[0]);
                fprintf(out_file, " to ");
                print_type(ir->dst.ty);
                fprintf(out_file, "\n");
                break;
            // arithmetic
            case IR_ADD:
            case IR_SUB:
            case IR_MUL:
            case IR_DIV:
            case IR_REM:
            case IR_AND:
            case IR_OR:
            case IR_XOR:
            case IR_SHL:
            case IR_SHR:
            case IR_CMP_EQ:
            case IR_CMP_NE:
            case IR_CMP_LE:
            case IR_CMP_LT:
                fprintf(out_file, "%s ", op_str[ir->op]);
                print_type(ir->args[0].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[0]);
                fprintf(out_file, ", ");
                print_operand(ir->args[1]);
                fprintf(out_file, "\n");
                break;
            default:
                fatal("unknown ir op kind %d", ir->op);
        }
        ir = ir->next;
    }

    fprintf(out_file, "  ");
    switch (b->jmp.type) {
        case IR_RET:
            fprintf(out_file, "ret ");
            print_type(b->jmp.arg.ty);
            fprintf(out_file, " ");
            print_operand(b->jmp.arg);
            fprintf(out_file, "\n");
            break;
        case IR_JMP:
            fprintf(out_file, "br label %%%d\n", b->succ1->blk_id);
            break;
        case IR_JNZ:
            fprintf(out_file, "br i1 ");
            print_operand(b->jmp.arg);
            fprintf(out_file, ", label %%%d, label %%%d\n", b->succ1->blk_id, b->succ2->blk_id);
            break;
        default:
            break;
    }
}

void dump_type(Type *ty) {
    fprintf(out_file, "%%%s = type { ", str(ty->uid));
    Member *mem = ty->members;
    if (ty->kind == TY_STRUCT) {
        while (mem) {
            print_type(mem->ty);
            mem = mem->next;
            if (!mem) break;
            fprintf(out_file, ", ");
        }
    } else if (ty->kind == TY_UNION) {
        while (mem && mem->ty->align != ty->align) mem = mem->next;
        print_type(mem->ty);
        if (mem->ty->size < ty->size) printf(", [ %d x i8]", ty->size - mem->ty->size);
    }
    fprintf(out_file, " }\n");
}

void dump_data(Sym *data) {
    fprintf(out_file, "@%s = global ", str(data->id));
    print_type(data->ty);
    if (data->is_str) {
        char *p = str(data->init_data);
        int len = data->ty->len;
        if (len == 1)
            fprintf(out_file, " zeroinitializer");
        else {
            fprintf(out_file, " c\"");
            for (int i = 0; i < len; i++) fprintf(out_file, "%s", escape_char_to_string(p[i]));
            fprintf(out_file, "\"");
        }
    } else if (data->ty->kind == TY_ARRAY)
        fprintf(out_file, " zeroinitializer");
    else if (data->ty->kind == TY_STRUCT)
        fprintf(out_file, " zeroinitializer");
    else
        fprintf(out_file, " 0");
    fprintf(out_file, ", align %d\n", data->ty->align);
}

void dump_fn(Sym *fn) {
    if (!fn->is_definition) return;
    fprintf(out_file, "define ");
    print_type(fn->ty->ret);
    fprintf(out_file, " @%s(", str(fn->id));
    Sym *var = fn->locals;
    for (uint32_t i = 0; i < fn->nparam; i++) {
        print_type(var->ty);
        fprintf(out_file, " ");
        fprintf(out_file, "%%%d", i);
        if (i < fn->nparam - 1) fprintf(out_file, ", ");
        var = var->next;
    }
    fprintf(out_file, ") {\n");
    Blk *curb = fn->start;
    while (curb) {
        dump_blk(curb);
        curb = curb->next;
    }
    fprintf(out_file, "}\n\n");
}

void dump_module(Module *md, FILE *out) {
    out_file = out;
    fprintf(out_file, "declare void @assert(i32, i32, ptr)\n");
    fprintf(out_file, "declare i32 @printf(ptr, ...)\n");
    fprintf(out_file, "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)\n");

    for (Type *ty = md->tys; ty; ty = ty->next) dump_type(ty);
    fprintf(out_file, "\n");

    for (Sym *var = md->data; var; var = var->next) dump_data(var);
    fprintf(out_file, "\n");

    for (Sym *fn = md->fns; fn; fn = fn->next) dump_fn(fn);
}
