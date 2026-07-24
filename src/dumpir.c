#include "cxx.h"

static FILE *out_file;
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
    [TY_VOID] = "void", [TY_I1] = "i1",    [TY_I64] = "i64",  [TY_BOOL] = "i8",   [TY_CHAR] = "i8", [TY_SHORT] = "i16",
    [TY_INT] = "i32",   [TY_ENUM] = "i32", [TY_LONG] = "i64", [TY_LLONG] = "i64", [TY_PTR] = "ptr",
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
            case IR_MEMSET:
                fprintf(out_file, "call void @llvm.memset.p0.i64(ptr ");
                print_operand(ir->args[0]);
                fprintf(out_file, ", i8 ");
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

    if (fn->sclass == SC_STATIC)
        fprintf(out_file, "internal ");
    else
        fprintf(out_file, "dso_local ");

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
    fprintf(out_file, "declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)\n\n");

    for (Type *ty = md->tys; ty; ty = ty->next) dump_type(ty);
    if (md->tys) fprintf(out_file, "\n");

    for (Sym *var = md->data; var; var = var->next) dump_data(var);
    if (md->data) fprintf(out_file, "\n");

    for (Sym *fn = md->fns; fn; fn = fn->next) dump_fn(fn);
}
