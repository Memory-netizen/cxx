#include "cxx.h"

static FILE *out_file;
static Module *curm;

static const char *op_str[][3] = {
    [IR_ADD] = {"add", "add", "fadd"},
    [IR_SUB] = {"sub", "sub", "fsub"},
    [IR_MUL] = {"mul", "mul", "fmul"},
    [IR_DIV] = {"sdiv", "udiv", "fdiv"},
    [IR_REM] = {"srem", "urem", NULL},
    [IR_AND] = {"and", "and", NULL},
    [IR_OR] = {"or", "or", NULL},
    [IR_XOR] = {"xor", "xor", NULL},
    [IR_SHL] = {"shl", "shl", NULL},
    [IR_SHR] = {"ashr", "lshr", NULL},
    [IR_CMP_EQ] = {"icmp eq", "icmp eq", "fcmp oeq"},
    [IR_CMP_NE] = {"icmp ne", "icmp ne", "fcmp une"},
    [IR_CMP_LE] = {"icmp sle", "icmp ule", "fcmp ole"},
    [IR_CMP_LT] = {"icmp slt", "icmp ult", "fcmp olt"},
    [IR_EXT] = {"sext", "zext", "fpext"},
    [IR_TRUNC] = {"trunc", "trunc", "fptrunc"},
    [IR_FPTOINT] = {"fptosi", "fptoui", NULL},
    [IR_INTTOFP] = {"sitofp", "uitofp", NULL},
    [IR_PTRTOINT] = {"ptrtoint", "ptrtoint", NULL},
    [IR_INTTOPTR] = {"inttoptr", "inttoptr", NULL},
};

static const char *ty_str[] = {
    [TY_VOID] = "void", [TY_I1] = "i1",       [TY_I32] = "i32",   [TY_I64] = "i64",     [TY_BOOL] = "i8",
    [TY_CHAR] = "i8",   [TY_SCHAR] = "i8",    [TY_UCHAR] = "i8",  [TY_SHORT] = "i16",   [TY_INT] = "i32",
    [TY_ENUM] = "i32",  [TY_LONG] = "i64",    [TY_LLONG] = "i64", [TY_FLOAT] = "float", [TY_DOUBLE] = "double",
    [TY_PTR] = "ptr",   [TY_NULLPTR] = "ptr",
};

static void print_ident(uint32_t id) {
    char *ident = str(id);
    int len = str_len(id);

    bool needs_quote = false;
    for (int i = 0; i < len; i++) {
        unsigned char c = ident[i];
        if (c > 0x7F || c == '$') {
            needs_quote = true;
            break;
        }
    }

    if (!needs_quote) {
        fprintf(out_file, "%s", ident);
        return;
    }

    fprintf(out_file, "\"");
    for (int i = 0; i < len; i++) {
        unsigned char c = ident[i];
        if (c <= 0x7F) {
            fputc(c, out_file);
        } else
            fprintf(out_file, "\\%02X", c);
    }
    fprintf(out_file, "\"");
}

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
        fprintf(out_file, "%%");
        print_ident(ty->uid);
        return;
    }
    fprintf(out_file, "%s", ty_str[ty->kind]);
}

static void printcon(Con *c, Type *ty) {
    if (c->type == CBits) {
        if (is_flonum(ty))
            fprintf(out_file, "0x%016" PRIx64, c->bits.i);
        else
            fprintf(out_file, "%" PRIi64, c->bits.i);
    } else if (c->type == CAddr) {
        if (c->bits.i) {
            fprintf(out_file, "getelementptr (i8, ptr @");
            print_ident(c->sym);
            fprintf(out_file, ", i64 %" PRIi64 ")", c->bits.i);
        } else if (c->sym) {
            fprintf(out_file, "@");
            print_ident(c->sym);
        } else {
            fprintf(out_file, "null");
        }
    }
}

static void print_operand(Ref r) {
    if (r.type == RCon) {
        printcon(&curm->con[r.val], r.ty);
    } else if (r.type == RGlb) {
        fprintf(out_file, "@");
        print_ident(r.val);
    } else {
        fprintf(out_file, "%%%d", r.val);
    }
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
            // fneg
            case IR_NEG:
                fprintf(out_file, "fneg ");
                print_type(ir->args[0].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[0]);
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
    fprintf(out_file, "%%");
    print_ident(ty->uid);
    fprintf(out_file, " = type { ");
    Member *mem = ty->members;
    if (ty->kind == TY_STRUCT) {
        int pos = 0;
        while (mem) {
            bool is_bitfield = mem->is_bitfield;
            Type *memty = is_bitfield ? mem->unit_ty : mem->ty;
            print_type(memty);
            pos += memty->size;
            int off = mem->offset;
            do {
                mem = mem->next;
            } while (mem && mem->offset == off);
            if (!mem) break;
            fprintf(out_file, ", ");
            if (pos < mem->offset) {
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
            if (mem->is_bitfield) {
                Member *after = mem->next;
                int off = mem->offset;
                while (after && after->offset == off) after = after->next;
                int64_t val = 0;
                for (Member *m = mem; m != after; m = m->next) {
                    int width = m->bit_width;
                    int boff = m->bit_offset;
                    Con *bit_val = init->child[m->idx]->val;
                    int trunc = bit_val ? bit_val->bits.i & ((1ULL << width) - 1) : 0;
                    val |= trunc << boff;
                }
                print_type(mem->unit_ty);
                fprintf(out_file, " ");
                printcon(&(Con){CBits, 0, {val}}, mem->unit_ty);
                pos += mem->unit_ty->size;
                mem = after;
            } else {
                dump_init(init->child[mem->idx], mem->ty);
                pos += mem->ty->size;
                mem = mem->next;
            }
            if (!mem) break;
            fprintf(out_file, ", ");
            if (pos != mem->offset) {
                fprintf(out_file, "[%d x i8] zeroinitializer, ", mem->offset - pos);
                pos = mem->offset;
            }
        }
        if (pos < ty->size) fprintf(out_file, ", [%d x i8] zeroinitializer", ty->size - pos);
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
    fprintf(out_file, "@");
    print_ident(data->id);
    fprintf(out_file, " = ");
    if (data->is_str) {
        char *p = str(data->init_data);
        int len = data->ty->len;
        fprintf(out_file, "private unnamed_addr constant ");
        print_type(data->ty);
        if (len == 1) {
            fprintf(out_file, " zeroinitializer");
        } else {
            if (data->ty->base->size == 1) {
                fprintf(out_file, " c\"");
                for (int i = 0; i < len; i++) fprintf(out_file, "%s", escape_char_to_string(p[i]));
                fprintf(out_file, "\"");
            } else if (data->ty->base->size == 2) {
                uint16_t *buf = (uint16_t *)p;
                fprintf(out_file, " [");
                for (int i = 0; i < data->ty->len; i++) {
                    if (i) fprintf(out_file, ", ");
                    fprintf(out_file, "i16 %d", buf[i]);
                }
                fprintf(out_file, "]");
            } else if (data->ty->base->size == 4) {
                uint32_t *buf = (uint32_t *)p;
                fprintf(out_file, " [");
                for (int i = 0; i < data->ty->len; i++) {
                    if (i) fprintf(out_file, ", ");
                    fprintf(out_file, "i32 %d", buf[i]);
                }
                fprintf(out_file, "]");
            }
        }
        fprintf(out_file, ", align %d\n", data->align);
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
    fprintf(out_file, " @");
    print_ident(fn->id);
    fprintf(out_file, "(");

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
    SrcFile **files = get_input_files();
    fprintf(out_file, "; ModuleID = '%s'\nsource_filename = \"%s\"\n\n", files[0]->name, files[0]->name);
    fprintf(out_file, "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)\n");
    fprintf(out_file, "declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)\n\n");

    for (Type *ty = md->tys; ty; ty = ty->next) dump_type(ty);
    if (md->tys) fprintf(out_file, "\n");

    for (Sym *var = md->data; var; var = var->next) dump_data(var);
    if (md->data) fprintf(out_file, "\n");

    for (Sym *fn = md->fns; fn; fn = fn->next) dump_fn(fn);
}
