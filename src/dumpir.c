#include "cxx.h"

static FILE *out_file;
static Module *curm;
static const char *op_str[][3] = {
    [IR_ADD] = {"add", "add", "fadd"},
    [IR_SUB] = {"sub", "sub", "fsub"},
    [IR_MUL] = {"mul", "mul", "fmul"},
    [IR_DIV] = {"sdiv", "udiv", "fdiv"},
    [IR_REM] = {"srem", "urem", "frem"},
    [IR_AND] = {"and", "and", "and"},
    [IR_OR] = {"or", "or", "or"},
    [IR_XOR] = {"xor", "xor", "xor"},
    [IR_SHL] = {"shl", "shl", "shl"},
    [IR_SHR] = {"ashr", "lshr", "ashr"},
    [IR_CMP_EQ] = {"icmp eq", "icmp eq", "fcmp oeq"},
    [IR_CMP_NE] = {"icmp ne", "icmp ne", "fcmp one"},
    [IR_CMP_LE] = {"icmp sle", "icmp ule", "fcmp ole"},
    [IR_CMP_LT] = {"icmp slt", "icmp ult", "fcmp olt"},
    [IR_EXT] = {"sext", "zext", "fpext"},
    [IR_TRUNC] = {"trunc", "trunc", "fptrunc"},
    [IR_FPTOINT] = {"fptosi", "fptoui"},
    [IR_INTTOFP] = {"sitofp", "uitofp"},
    [IR_PTRTOINT] = {"ptrtoint", "ptrtoint", "ptrtoint"},
    [IR_INTTOPTR] = {"inttoptr", "inttoptr", "inttoptr"},
};

static const char *ty_str[] = {
    [TY_VOID] = "void", [TY_I1] = "i1",       [TY_I32] = "i32",   [TY_I64] = "i64",     [TY_BOOL] = "i8",
    [TY_CHAR] = "i8",   [TY_SCHAR] = "i8",    [TY_UCHAR] = "i8",  [TY_SHORT] = "i16",   [TY_INT] = "i32",
    [TY_ENUM] = "i32",  [TY_LONG] = "i64",    [TY_LLONG] = "i64", [TY_FLOAT] = "float", [TY_DOUBLE] = "double",
    [TY_PTR] = "ptr",   [TY_NULLPTR] = "ptr",
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

static void printcon(Con *c, Type *ty) {
    if (c->type == CBits) {
        if (is_flonum(ty))
            fprintf(out_file, "%f", c->bits.d);
        else
            fprintf(out_file, "%" PRIi64, c->bits.i);
    } else if (c->type == CAddr) {
        if (c->bits.i)
            fprintf(out_file, "getelementptr (i8, ptr @%s, i64 %" PRIi64 ")", str(c->sym), c->bits.i);
        else if (c->sym)
            fprintf(out_file, "@%s", str(c->sym));
        else
            fprintf(out_file, "null");
    }
}

static void print_operand(Ref r) {
    if (r.type == RCon)
        printcon(&curm->con[r.val], r.ty);
    else if (r.type == RGlb)
        fprintf(out_file, "@%s", str(r.val));
    else
        fprintf(out_file, "%%%d", r.val);
}

void dump_blk(Blk *b) {
    int indent = fprintf(out_file, "%d:", b->blk_id);
    if (b->num_pred) {
        fprintf(out_file, "%*.s; preds = ", 48 - indent, "");
        for (uint32_t i = 0; i < b->num_pred; i++) {
            fprintf(out_file, "%%%d", b->pred[i]->blk_id);
            if (i < b->num_pred - 1) fprintf(out_file, ", ");
        }
    }
    fprintf(out_file, "\n");
    Phi *p = b->phi;
    while (p) {
        fprintf(out_file, "  %%%d = phi ", p->result.val);
        print_type(p->result.ty);
        for (int i = p->num_arg - 1; i >= 0; i--) {
            fprintf(out_file, " [ ");
            print_operand(p->arg[i]);
            fprintf(out_file, ", %%%d ]", p->blk[i]->blk_id);
            if (i) fprintf(out_file, ", ");
        }
        fprintf(out_file, "\n");
        p = p->next;
    }

    Ir *ir = b->head;
    while (ir) {
        fprintf(out_file, "  ");
        if (!refeq(ir->dst, R)) fprintf(out_file, "%%%d = ", ir->dst.val);

        switch (ir->op) {
            // memmory
            case IR_ALLOCA:
                fprintf(out_file, "alloca ");
                print_type(ir->dst.ty->base);
                fprintf(out_file, ", align ");
                print_operand(ir->args[0]);
                fprintf(out_file, "\n");
                break;
            case IR_LORD:
                fprintf(out_file, "load ");
                if (ir->args[0].ty->base->qual & Q_VOLATILE) fprintf(out_file, "volatile ");
                print_type(ir->dst.ty);
                fprintf(out_file, ", ptr ");
                print_operand(ir->args[0]);
                fprintf(out_file, ", align ");
                print_operand(ir->args[1]);
                fprintf(out_file, "\n");
                break;
            case IR_STR:
                fprintf(out_file, "store ");
                if (ir->args[1].ty->base->qual & Q_VOLATILE) fprintf(out_file, "volatile ");
                print_type(ir->args[0].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[0]);
                fprintf(out_file, ", ptr ");
                print_operand(ir->args[1]);
                fprintf(out_file, ", align ");
                print_operand(ir->args[2]);
                fprintf(out_file, "\n");
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
                fprintf(out_file, ", i64 ");
                print_operand(ir->args[2]);
                fprintf(out_file, ", i1 false)\n");
                break;
            case IR_MEMSET:
                fprintf(out_file, "call void @llvm.memset.p0.i64(ptr ");
                print_operand(ir->args[0]);
                fprintf(out_file, ", i8 ");
                print_operand(ir->args[1]);
                fprintf(out_file, ", i64 ");
                print_operand(ir->args[2]);
                fprintf(out_file, ", i1 false)\n");
                break;

            case IR_CALL:
                fprintf(out_file, "call ");
                if (refeq(ir->dst, R))
                    fprintf(out_file, "void");
                else
                    print_type(ir->dst.ty);
                fprintf(out_file, " ");
                print_operand(ir->args[0]);
                fprintf(out_file, "(");
                for (uint32_t i = 1; i < ir->narg; i++) {
                    print_type(ir->args[i].ty);
                    fprintf(out_file, " ");
                    print_operand(ir->args[i]);
                    if (i < ir->narg - 1) fprintf(out_file, ", ");
                }
                fprintf(out_file, ")\n");
                break;
            // conversion
            case IR_EXT:
            case IR_TRUNC:
            case IR_FPTOINT:
            case IR_INTTOFP:
            case IR_PTRTOINT:
            case IR_INTTOPTR: {
                Type *ty = ir->args[0].ty;
                int idx = ty->is_unsigned;
                if (is_flonum(ty)) {
                    if (!is_flonum(ir->dst.ty))
                        idx = ir->dst.ty->is_unsigned;
                    else
                        idx = 2;
                }
                fprintf(out_file, "%s ", op_str[ir->op][idx]);
                print_type(ir->args[0].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[0]);
                fprintf(out_file, " to ");
                print_type(ir->dst.ty);
                fprintf(out_file, "\n");
                break;
            }
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
            case IR_CMP_LT: {
                int idx = ir->args[0].ty->is_unsigned;
                if (is_flonum(ir->args[0].ty)) idx = 2;
                fprintf(out_file, "%s ", op_str[ir->op][idx]);
                print_type(ir->args[0].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[0]);
                fprintf(out_file, ", ");
                print_operand(ir->args[1]);
                fprintf(out_file, "\n");
                break;
            }
            default:
                fatal("unknown ir op kind %d", ir->op);
        }
        ir = ir->next;
    }

    fprintf(out_file, "  ");
    switch (b->jmp.type) {
        case IR_RET:
            fprintf(out_file, "ret ");
            if (!refeq(b->jmp.arg, R)) {
                print_type(b->jmp.arg.ty);
                fprintf(out_file, " ");
                print_operand(b->jmp.arg);
            } else {
                fprintf(out_file, "void");
            }
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
        case IR_SWITCH:
            fprintf(out_file, "switch ");
            print_type(b->jmp.arg.ty);
            fprintf(out_file, " ");
            print_operand(b->jmp.arg);
            fprintf(out_file, ", label %%%d", b->succ1->blk_id);
            if (b->narg) fprintf(out_file, " [\n");
            for (uint32_t i = 0; i < b->narg; i++) {
                fprintf(out_file, "    ");
                print_type(b->jmp.args[i].ty);
                fprintf(out_file, " ");
                print_operand(b->jmp.args[i]);
                fprintf(out_file, ", label %%%d\n", b->succ[i]->blk_id);
            }
            if (b->narg) fprintf(out_file, "  ]\n");

            break;
        default:
            break;
    }
}

void dump_type(Type *ty) {
    fprintf(out_file, "%%%s = type { ", str(ty->uid));
    Member *mem = ty->members;
    if (ty->kind == TY_STRUCT) {
        int pos = 0;
        while (mem) {
            print_type(mem->ty);
            pos += mem->ty->size;
            mem = mem->next;
            if (!mem) break;
            fprintf(out_file, ", ");
            if (pos != mem->offset) {
                fprintf(out_file, "[%d x i8], ", mem->offset - pos);
                pos = mem->offset;
            }
        }
        if (pos < ty->size) fprintf(out_file, ", [%d x i8]", ty->size - pos);
    } else if (ty->kind == TY_UNION) {
        print_type(mem->ty);
        if (mem->ty->size < ty->size) fprintf(out_file, ", [%d x i8]", ty->size - mem->ty->size);
    }
    fprintf(out_file, " }\n");
}

static void dump_init(Initializer *init, Type *ty) {
    print_type(ty);
    fprintf(out_file, " ");
    if (ty->kind == TY_ARRAY) {
        if (!init || !init->is_inited) {
            fprintf(out_file, "zeroinitializer");
            return;
        }
        if (ty->base->size == 1) {
            fprintf(out_file, "c\"");
            for (int i = 0; i < ty->len; i++) {
                if (!init->child[i]->val)
                    fprintf(out_file, "\\00");
                else
                    fprintf(out_file, "%s", escape_char_to_string(init->child[i]->val->bits.i));
            }
            fprintf(out_file, "\"");
        } else {
            fprintf(out_file, "[");
            for (int i = 0; i < ty->len; i++) {
                if (i) fprintf(out_file, ", ");
                dump_init(init->child[i], ty->base);
            }
            fprintf(out_file, "]");
        }
        return;
    }
    if (ty->kind == TY_STRUCT) {
        if (!init || !init->is_inited) {
            fprintf(out_file, "zeroinitializer");
            return;
        }
        fprintf(out_file, "{ ");
        Member *mem = ty->members;
        int pos = 0;
        while (mem) {
            dump_init(init->child[mem->idx], mem->ty);
            pos += mem->ty->size;
            mem = mem->next;
            if (!mem) break;
            fprintf(out_file, ", ");
            if (pos != mem->offset) {
                fprintf(out_file, "[%d x i8] zeroinitializer, ", mem->offset - pos);
                pos = mem->offset;
            }
        }
        if (pos < ty->size) fprintf(out_file, "[%d x i8] zeroinitializer, ", ty->size - pos);
        fprintf(out_file, " }");
        return;
    }
    if (ty->kind == TY_UNION) {
        if (!init || !init->is_inited) {
            fprintf(out_file, "zeroinitializer");
            return;
        }
        fprintf(out_file, "{ ");
        dump_init(init->child[0], ty->members->ty);

        if (ty->members->ty->size < ty->size)
            fprintf(out_file, ", [%d x i8] zeroinitializer", ty->size - ty->members->ty->size);
        fprintf(out_file, " }");
        return;
    }
    if (!init || !init->is_inited) {
        fprintf(out_file, "0");
        return;
    }
    if (init->ty->kind == TY_PTR && init->val->type == CBits) {
        fprintf(out_file, "inttoptr (i64 ");
        printcon(init->val, init->ty);
        fprintf(out_file, " to ptr)");
        return;
    }
    if (init->ty->kind != TY_PTR && init->val->type == CAddr) {
        fprintf(out_file, "ptrtoint (ptr ");
        printcon(init->val, init->ty);
        fprintf(out_file, " to i64)");
        return;
    }
    printcon(init->val, init->ty);
}

static const char *sclass_name[] = {
    [SC_NONE] = "dso_local",
    [SC_EXTERN] = "external",
    [SC_STATIC] = "internal",
    [SC_THREAD] = "thread_local",
};

void dump_data(Sym *data) {
    fprintf(out_file, "@%s = ", str(data->id));
    if (data->is_str) {
        char *p = str(data->init_data);
        int len = data->ty->len;
        fprintf(out_file, "private unnamed_addr constant [%d x i8]", len);
        if (len == 1)
            fprintf(out_file, " zeroinitializer");
        else {
            fprintf(out_file, " c\"");
            for (int i = 0; i < len; i++) fprintf(out_file, "%s", escape_char_to_string(p[i]));
            fprintf(out_file, "\"");
        }
        fprintf(out_file, ", align 1\n");
        return;
    }
    fprintf(out_file, "%s global ", sclass_name[data->sclass]);
    if (data->sclass == SC_EXTERN)
        print_type(data->ty);
    else
        dump_init(data->init, data->ty);

    fprintf(out_file, ", align %d\n", data->align);
}

void dump_fn(Sym *fn) {
    if (!fn->is_defined) {
        fprintf(out_file, "declare ");
    } else {
        fprintf(out_file, "define ");
        if (fn->sclass == SC_STATIC)
            fprintf(out_file, "internal ");
        else
            fprintf(out_file, "dso_local ");
    }

    print_type(fn->ty->ret);
    fprintf(out_file, " @%s(", str(fn->id));

    Type *param = fn->ty->params;
    for (uint32_t i = 0; param; i++) {
        print_type(param);
        if (fn->is_defined) fprintf(out_file, " %%%d", i);
        param = param->next;
        if (param) fprintf(out_file, ", ");
    }
    if (fn->ty->is_variadic) fprintf(out_file, ", ...");
    fprintf(out_file, ")");
    if (!fn->is_defined) {
        fprintf(out_file, "\n\n");
        return;
    }
    fprintf(out_file, " {\n");
    Blk *curb = fn->start;
    while (curb) {
        dump_blk(curb);
        curb = curb->next;
        if (curb) fprintf(out_file, "\n");
    }
    fprintf(out_file, "}\n\n");
}

void dump_module(Module *md, FILE *out) {
    out_file = out;
    curm = md;
    fprintf(out_file, "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)\n");
    fprintf(out_file, "declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)\n\n");

    for (Type *ty = md->tys; ty; ty = ty->next) dump_type(ty);
    if (md->tys) fprintf(out_file, "\n");

    for (Sym *var = md->data; var; var = var->next) dump_data(var);
    if (md->data) fprintf(out_file, "\n");

    for (Sym *fn = md->fns; fn; fn = fn->next) dump_fn(fn);
}
