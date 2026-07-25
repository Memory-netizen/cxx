#include "cxx.h"

static int depth;

static void print_indent(void) {
    for (int i = 0; i < depth; i++) fprintf(stdout, "  ");
}

static const char *node_kind_name[] = {
    [ND_NOP] = "NOP",
    [ND_COMMA] = "COMMA",
    [ND_AS] = "AS",
    [ND_ADDAS] = "ADDAS",
    [ND_SUBAS] = "SUBAS",
    [ND_PTRAS] = "PTRAS",
    [ND_MULAS] = "MULAS",
    [ND_DIVAS] = "DIVAS",
    [ND_MODAS] = "MODAS",
    [ND_ANDAS] = "ANDAS",
    [ND_ORAS] = "ORAS",
    [ND_XORAS] = "XORAS",
    [ND_LEFTAS] = "LEFTAS",
    [ND_RIGHTAS] = "RIGHTAS",
    [ND_BOR] = "BOR",
    [ND_XOR] = "XOR",
    [ND_BAND] = "BAND",
    [ND_EQ] = "EQ",
    [ND_NE] = "NE",
    [ND_LT] = "LT",
    [ND_LE] = "LE",
    [ND_LEFT] = "LEFT",
    [ND_RIGHT] = "RIGHT",
    [ND_ADD] = "ADD",
    [ND_SUB] = "SUB",
    [ND_MUL] = "MUL",
    [ND_DIV] = "DIV",
    [ND_MOD] = "MOD",
    [ND_PLUS] = "PLUS",
    [ND_NEG] = "NEG",
    [ND_NOT] = "NOT",
    [ND_INVERT] = "INVERT",
    [ND_ADDR] = "ADDR",
    [ND_DEREF] = "DEREF",
    [ND_MEMBER] = "MEMBER",
    [ND_PTRADD] = "PTRADD",
    [ND_PREINC] = "PREINC",
    [ND_PREDEC] = "PREDEC",
    [ND_POSTINC] = "POSTINC",
    [ND_POSTDEC] = "POSTDEC",
    [ND_FUNCALL] = "FUNCALL",
    [ND_IMCAST] = "IMCAST",
    [ND_EXCAST] = "EXCAST",
    [ND_LVTOR] = "LVTOR",
    [ND_LOGAND] = "LOGAND",
    [ND_LOGOR] = "LOGOR",
    [ND_COND] = "COND",
    [ND_MEMZERO] = "MEMZERO",
    [ND_RETURN] = "RETURN",
    [ND_IF] = "IF",
    [ND_WHILE] = "WHILE",
    [ND_DO] = "DO",
    [ND_FOR] = "FOR",
    [ND_EXPR_STMT] = "EXPR_STMT",
    [ND_STMT_EXPR] = "STMT_EXPR",
    [ND_COMP_STMT] = "COMP_STMT",
    [ND_GOTO] = "GOTO",
    [ND_LABEL] = "LABEL",
    [ND_BREAK] = "BREAK",
    [ND_CONTINUE] = "CONTINUE",
    [ND_SWITCH] = "SWITCH",
    [ND_CASE] = "CASE",
    [ND_DECL] = "DECL",
    [ND_VAR] = "VAR",
    [ND_NUM] = "NUM",
};

static void print_type(Type *ty) {
    if (!ty) {
        fprintf(stdout, "null");
        return;
    }
    switch (ty->kind) {
        case TY_VOID:
            fprintf(stdout, "void");
            break;
        case TY_I1:
            fprintf(stdout, "i1");
            break;
        case TY_I64:
            fprintf(stdout, "i64");
            break;
        case TY_CHAR:
            fprintf(stdout, "char");
            break;
        case TY_BOOL:
            fprintf(stdout, "bool");
            break;
        case TY_SHORT:
            fprintf(stdout, "short");
            break;
        case TY_INT:
            fprintf(stdout, "int");
            break;
        case TY_LONG:
            fprintf(stdout, "long");
            break;
        case TY_LLONG:
            fprintf(stdout, "llong");
            break;
        case TY_ENUM:
            fprintf(stdout, "enum");
            break;
        case TY_PTR:
            fprintf(stdout, "ptr to ");
            print_type(ty->base);
            break;
        case TY_ARRAY:
            fprintf(stdout, "[%d]", ty->len);
            print_type(ty->base);
            break;
        case TY_FUNC:
            fprintf(stdout, "func(");
            print_type(ty->ret);
            fprintf(stdout, ")");
            break;
        case TY_STRUCT:
            fprintf(stdout, "struct %s", str(ty->uid));
            break;
        case TY_UNION:
            fprintf(stdout, "union %s", str(ty->uid));
            break;
    }
}

static void dump_node(Node *node);

static void dump_node_list(Node *node) {
    while (node) {
        dump_node(node);
        node = node->next;
    }
}

static void dump_node(Node *node) {
    if (!node) return;

    print_indent();
    fprintf(stdout, "%s", node_kind_name[node->kind]);

    if (node->ty) {
        fprintf(stdout, "  ty=");
        print_type(node->ty);
    }

    switch (node->kind) {
        case ND_NUM:
            fprintf(stdout, "  val=%ld", node->val);
            fprintf(stdout, "\n");
            break;

        case ND_VAR:
            fprintf(stdout, "  name=‘%s’", str(node->var->id));
            fprintf(stdout, "  lvalue=%d", node->is_lvalue);
            fprintf(stdout, "\n");
            break;

        case ND_MEMBER:
            fprintf(stdout, "  member=‘%s’\n", str(node->member->name->id));
            depth++;
            dump_node(node->lhs);
            depth--;
            break;

        case ND_FUNCALL:
            fprintf(stdout, "  func=‘%s’  narg=%u\n", str(node->func), node->narg);
            depth++;
            dump_node_list(node->args);
            depth--;
            break;

        case ND_IMCAST:
        case ND_EXCAST:
        case ND_LVTOR:
        case ND_PLUS:
        case ND_NEG:
        case ND_NOT:
        case ND_INVERT:
        case ND_ADDR:
        case ND_DEREF:
        case ND_PREINC:
        case ND_PREDEC:
        case ND_POSTINC:
        case ND_POSTDEC:
        case ND_RETURN:
        case ND_EXPR_STMT:
        case ND_MEMZERO:
            fprintf(stdout, "\n");
            depth++;
            dump_node(node->lhs);
            depth--;
            break;

        case ND_COMMA:
        case ND_AS:
        case ND_ADDAS:
        case ND_SUBAS:
        case ND_MULAS:
        case ND_DIVAS:
        case ND_MODAS:
        case ND_ANDAS:
        case ND_ORAS:
        case ND_XORAS:
        case ND_LEFTAS:
        case ND_RIGHTAS:
        case ND_BOR:
        case ND_XOR:
        case ND_BAND:
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
        case ND_LEFT:
        case ND_RIGHT:
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_MOD:
        case ND_PTRADD:
        case ND_LOGAND:
        case ND_LOGOR:
        case ND_PTRAS:
            fprintf(stdout, "\n");
            depth++;
            dump_node(node->lhs);
            dump_node(node->rhs);
            depth--;
            break;

        case ND_COND:
            fprintf(stdout, "\n");
            depth++;
            print_indent();
            fprintf(stdout, "cond:\n");
            depth++;
            dump_node(node->cond);
            depth--;
            print_indent();
            fprintf(stdout, "then:\n");
            depth++;
            dump_node(node->then);
            depth--;
            print_indent();
            fprintf(stdout, "els:\n");
            depth++;
            dump_node(node->els);
            depth--;
            depth--;
            break;

        case ND_IF:
            fprintf(stdout, "\n");
            depth++;
            print_indent();
            fprintf(stdout, "cond:\n");
            depth++;
            dump_node(node->cond);
            depth--;
            print_indent();
            fprintf(stdout, "then:\n");
            depth++;
            dump_node(node->then);
            depth--;
            if (node->els) {
                print_indent();
                fprintf(stdout, "els:\n");
                depth++;
                dump_node(node->els);
                depth--;
            }
            depth--;
            break;

        case ND_WHILE:
            fprintf(stdout, "\n");
            depth++;
            print_indent();
            fprintf(stdout, "cond:\n");
            depth++;
            dump_node(node->cond);
            depth--;
            print_indent();
            fprintf(stdout, "body:\n");
            depth++;
            dump_node(node->then);
            depth--;
            depth--;
            break;

        case ND_DO:
            fprintf(stdout, "\n");
            depth++;
            print_indent();
            fprintf(stdout, "body:\n");
            depth++;
            dump_node(node->body);
            depth--;
            print_indent();
            fprintf(stdout, "cond:\n");
            depth++;
            dump_node(node->cond);
            depth--;
            depth--;
            break;

        case ND_FOR:
            fprintf(stdout, "\n");
            depth++;
            if (node->init) {
                print_indent();
                fprintf(stdout, "init:\n");
                depth++;
                dump_node(node->init);
                depth--;
            }
            if (node->cond) {
                print_indent();
                fprintf(stdout, "cond:\n");
                depth++;
                dump_node(node->cond);
                depth--;
            }
            if (node->inc) {
                print_indent();
                fprintf(stdout, "inc:\n");
                depth++;
                dump_node(node->inc);
                depth--;
            }
            print_indent();
            fprintf(stdout, "body:\n");
            depth++;
            dump_node(node->body);
            depth--;
            depth--;
            break;

        case ND_SWITCH:
            fprintf(stdout, "\n");
            depth++;
            print_indent();
            fprintf(stdout, "cond:\n");
            depth++;
            dump_node(node->cond);
            depth--;
            print_indent();
            fprintf(stdout, "body:\n");
            depth++;
            dump_node(node->body);
            depth--;
            depth--;
            break;

        case ND_CASE:
            fprintf(stdout, "  val=%ld\n", node->val);
            depth++;
            dump_node(node->label_body);
            depth--;
            break;

        case ND_LABEL:
            fprintf(stdout, "  label=‘%s’\n", str(node->label));
            depth++;
            dump_node(node->label_body);
            depth--;
            break;

        case ND_GOTO:
            fprintf(stdout, "  label=‘%s’\n", str(node->label));
            break;

        case ND_BREAK:
        case ND_CONTINUE:
        case ND_NOP:
            fprintf(stdout, "\n");
            break;

        case ND_STMT_EXPR:
        case ND_COMP_STMT:
        case ND_DECL:
            fprintf(stdout, "\n");
            depth++;
            dump_node_list(node->body);
            depth--;
            break;
    }
}

static const char *sclass_name[] = {
    [SC_NONE] = "none",     [SC_AUTO] = "auto",           [SC_TYPEDEF] = "typedef", [SC_EXTERN] = "extern",
    [SC_STATIC] = "static", [SC_THREAD] = "thread_local", [SC_REG] = "register",
};

void dump_ast(Module *prog) {
    for (Sym *var = prog->data; var; var = var->next) {
        fprintf(stdout, "GLOBAL %s  ty=", str(var->id));
        print_type(var->ty);
        fprintf(stdout, "  sclass=%s", sclass_name[var->sclass]);
        if (var->is_str) fprintf(stdout, "  [string_literal]");
        fprintf(stdout, "\n");
    }

    if (prog->data && prog->fns) fprintf(stdout, "\n");

    for (Sym *fn = prog->fns; fn; fn = fn->next) {
        fprintf(stdout, "FUNCTION %s  ret=", str(fn->id));
        print_type(fn->ty->ret);
        fprintf(stdout, "  sclass=%s", sclass_name[fn->sclass]);
        if (!fn->is_definition) {
            fprintf(stdout, "  [declaration only]\n\n");
            continue;
        }
        fprintf(stdout, "\n");

        if (fn->params) {
            fprintf(stdout, "  params:\n");
            for (Sym *p = fn->params; p; p = p->next) {
                fprintf(stdout, "    %s: ", str(p->id));
                print_type(p->ty);
                fprintf(stdout, "\n");
            }
        }

        fprintf(stdout, "  locals (%u):\n", fn->nparam);
        for (Sym *v = fn->locals; v; v = v->next) {
            fprintf(stdout, "    %s: ", str(v->id));
            print_type(v->ty);
            fprintf(stdout, "\n");
        }

        fprintf(stdout, "  body:\n");
        depth = 1;
        dump_node(fn->body);
        fprintf(stdout, "\n");
    }
}
