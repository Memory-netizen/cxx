#include "cxx.h"

static int depth;

static void print_indent(void) {
    for (int i = 0; i < depth; i++) fprintf(stdout, "  ");
}

// Source location of a node's representative token, clang-style. A node
// synthesised by the parser borrows a token from the construct it came
// from, so this resolves to something meaningful for every node.
static void print_loc(Node *node) {
    if (!node->tok) return;
    int line, col;
    get_location(node->tok->file, node->tok->loc, &line, &col);
    fprintf(stdout, "  Loc=<%s:%d:%d>", str(node->tok->filename), line + node->tok->line_delta, col);
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
    [ND_GT] = "GT",
    [ND_GE] = "GE",
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
    [ND_GOTO_EXPR] = "INDIRECTGOTO",
    [ND_LABEL] = "LABEL",
    [ND_LABEL_VAL] = "ADDRLABELEXPR",
    [ND_BREAK] = "BREAK",
    [ND_CONTINUE] = "CONTINUE",
    [ND_SWITCH] = "SWITCH",
    [ND_CASE] = "CASE",
    [ND_DECL] = "DECL",
    [ND_INIT] = "INIT",
    [ND_VAR] = "VAR",
    [ND_NUM] = "NUM",
    [ND_NULLPTR] = "NULLPTR",
    [ND_SUBACCESS] = "SUBACCESS",
    [ND_ALLOCA] = "ALLOCA",
    [ND_ATOMICRMW] = "ATOMICRMW",
    [ND_CAS] = "CAS",
    [ND_FENCE] = "FENCE",
    [ND_VA_START] = "VA_START",
    [ND_VA_END] = "VA_END",
    [ND_VA_ARG] = "VA_ARG",
    [ND_VA_COPY] = "VA_COPY",
    [ND_SP_SAVE] = "SP_SAVE",
    [ND_SP_RESTORE] = "SP_RESTORE",
};

// Type qualifiers (6.7.3). They live on the qualified type itself, so the
// caller must print them where the type is named.
static void print_qual(uint32_t qual) {
    if (qual & Q_CONST) fprintf(stdout, "const ");
    if (qual & Q_VOLATILE) fprintf(stdout, "volatile ");
    if (qual & Q_RESTRICT) fprintf(stdout, "restrict ");
    if (qual & Q_ATOMIC) fprintf(stdout, "_Atomic ");
}

// Array length, recovered from the source. Every array type records the
// '[' that produced it, so the text between that bracket and its matching
// ']' is exactly what the programmer wrote -- for a VLA (`int a[n + 1]`)
// and for a fixed array (`int a[3]`) alike. The tokens are the original
// ones, so no re-parsing of the expression is needed.
static void print_array_len(Type *ty);
static void print_type(Type *ty);

// Print the dimensions of an array type in declarator order. The levels
// are collected walking down the bases, which is already declarator
// order: for 'int a[3][4]' the type is TY_ARRAY(len 3) whose base is
// TY_ARRAY(len 4), and the declaration reads [3][4].
static void print_array_dimensions(Type *ty) {
    Type **stack = vnew(4, sizeof(Type *));
    int n = 0;

    for (; ty->kind == TY_ARRAY || ty->kind == TY_VLA; ty = ty->base) {
        stack = vgrow(stack, n + 1);
        stack[n++] = ty;
    }
    print_type(ty);

    for (int i = 0; i < n; i++) {
        fprintf(stdout, "[");
        print_array_len(stack[i]);
        fprintf(stdout, "]");
    }
}

// ---- Array length recovery -------------------------------------------
//
// A dumper should show the length the programmer actually wrote. The Type
// structure cannot carry the '[' of an array declarator -- Type.name is
// taken (it holds the declarator identifier until the declaration has been
// processed) and the union must not gain a member, since vla_len/vla_cnt
// share it with len/is_static/is_star. So the bracket is kept in a side
// table here, keyed by the Type pointer, which is stable and unique per
// array instance.
//
// The bracket is recorded by the parser through array_bracket_note() as
// each dimension is built, because no field of Type identifies which
// bracket of a declarator belongs to which nesting level.
typedef struct BracketEntry {
    struct BracketEntry *next;
    Type *ty;
    Token *l_bracket;
} BracketEntry;

#define BRACKET_BUCKETS 512
static BracketEntry *bracket_table[BRACKET_BUCKETS];

static uint32_t bracket_hash(Type *ty) { return (uint32_t)(((uintptr_t)ty >> 4) % BRACKET_BUCKETS); }

// The '[' that declared this dimension. The parser records it as the
// dimension is built, because nothing on the Type identifies which of a
// declarator's brackets belongs to which level once parsing has finished
// (Type.name is assigned once, after the whole declarator).
void array_bracket_note(Type *ty, Token *l_bracket) {
    if (!ty) return;
    uint32_t h = bracket_hash(ty);
    for (BracketEntry *e = bracket_table[h]; e; e = e->next) {
        if (e->ty == ty) {
            e->l_bracket = l_bracket;
            return;
        }
    }
    BracketEntry *e = emalloc(sizeof(BracketEntry));
    e->ty = ty;
    e->l_bracket = l_bracket;
    e->next = bracket_table[h];
    bracket_table[h] = e;
}

static Token *bracket_of(Type *ty) {
    uint32_t h = bracket_hash(ty);
    for (BracketEntry *e = bracket_table[h]; e; e = e->next)
        if (e->ty == ty) return e->l_bracket;
    return NULL;
}

// Print the length of one array dimension. The text between the declaring
// '[' and its matching ']' is exactly what was written -- for a VLA
// (`int a[n + 1]`) and a fixed array (`int a[3]`) alike -- and needs no
// re-parsing. Nesting is counted so `a[f(x[0])]` works.
static void print_array_len(Type *ty) {
    Token *lb = bracket_of(ty);
    if (lb) {
        int level = 0;
        for (Token *t = lb; t && t->kind != TK_EOF; t = t->next) {
            if (t->kind == TK_LBRACKET) {
                level++;
                continue;
            }
            if (t->kind == TK_RBRACKET && --level == 0) {
                char *from = tok_text(lb) + lb->len;
                uint32_t len = (uint32_t)(tok_text(t) - from);
                fprintf(stdout, "%.*s", (int)len, from);
                return;
            }
        }
    }
    // No bracket could be attributed (a synthesised array type): fall back
    // to the stored length, which is only meaningful for a fixed array.
    if (ty->kind == TY_VLA) {
        fprintf(stdout, "?");
        return;
    }
    fprintf(stdout, "%d", ty->len);
}

static void print_type(Type *ty) {
    if (!ty) {
        fprintf(stdout, "null");
        return;
    }
    switch (ty->kind) {
        case TY_NULLPTR:
            fprintf(stdout, "nullptr_t");
            break;
        case TY_VOID:
            print_qual(ty->qual);
            fprintf(stdout, "void");
            break;
        case TY_CHAR:
            print_qual(ty->qual);
            fprintf(stdout, "char");
            break;
        case TY_SCHAR:
            print_qual(ty->qual);
            fprintf(stdout, "signed char");
            break;
        case TY_UCHAR:
            print_qual(ty->qual);
            fprintf(stdout, "unsigned char");
            break;
        case TY_BOOL:
            print_qual(ty->qual);
            fprintf(stdout, "bool");
            break;
        case TY_SHORT:
            print_qual(ty->qual);
            if (ty->is_unsigned) fprintf(stdout, "unsigned ");
            fprintf(stdout, "short");
            break;
        case TY_INT:
            print_qual(ty->qual);
            if (ty->is_unsigned) fprintf(stdout, "unsigned ");
            fprintf(stdout, "int");
            break;
        case TY_LONG:
            print_qual(ty->qual);
            if (ty->is_unsigned) fprintf(stdout, "unsigned ");
            fprintf(stdout, "long");
            break;
        case TY_LLONG:
            print_qual(ty->qual);
            if (ty->is_unsigned) fprintf(stdout, "unsigned ");
            fprintf(stdout, "llong");
            break;
        case TY_FLOAT:
            fprintf(stdout, "float");
            break;
        case TY_DOUBLE:
            fprintf(stdout, "double");
            break;
        case TY_LDOUBLE:
            fprintf(stdout, "long double");
            break;
        case TY_F16:
            fprintf(stdout, "_Float16");
            break;
        case TY_F32:
            fprintf(stdout, "_Float32");
            break;
        case TY_F64:
            fprintf(stdout, "_Float64");
            break;
        case TY_F128:
            fprintf(stdout, "_Float128");
            break;
        case TY_ENUM:
            fprintf(stdout, "enum");
            break;
        case TY_PTR: {
            Type *base = ty->base;
            // The qualifiers of the pointee belong to the pointed-to type
            // (`const int *`), while those of the pointer itself are part
            // of the declarator and are printed after the `*` below.
            if (base->kind == TY_ARRAY || base->kind == TY_VLA) {
                // int (*)[3][4]: the base type first, then the declarator
                // with the dimensions attached to the *.
                Type *elem = base;
                while (elem->kind == TY_ARRAY || elem->kind == TY_VLA) elem = elem->base;
                print_type(elem);
                fprintf(stdout, " (*)");
                for (Type *t = base; t->kind == TY_ARRAY || t->kind == TY_VLA; t = t->base) {
                    fprintf(stdout, "[");
                    print_array_len(t);
                    fprintf(stdout, "]");
                }
            } else if (base->kind == TY_FUNC) {
                print_type(base->ret);
                fprintf(stdout, " (*)(");
                for (Type *param = base->params; param;) {
                    print_type(param);
                    param = param->next;
                    if (param) fprintf(stdout, ", ");
                }
                if (base->is_variadic) fprintf(stdout, ", ...");
                fprintf(stdout, ")");
            } else {
                print_type(base);
                fprintf(stdout, " *");
            }
            // Qualifiers written after the `*`: `int * const p`.
            print_qual(ty->qual);
            break;
        }
        case TY_VLA:
        case TY_ARRAY:
            print_array_dimensions(ty);
            break;
        case TY_FUNC:
            print_type(ty->ret);
            fprintf(stdout, " (");
            for (Type *param = ty->params; param;) {
                print_type(param);
                param = param->next;
                if (param) fprintf(stdout, ", ");
            }
            if (ty->is_variadic) fprintf(stdout, ", ...");
            fprintf(stdout, ")");
            break;
        case TY_STRUCT:
            fprintf(stdout, "struct %s", str(ty->uid));
            break;
        case TY_UNION:
            fprintf(stdout, "union %s", str(ty->uid));
            break;
        case TY_NONE:
            break;
        default:
            if (ty->kind & TY_BITINT) {
                if (ty->is_unsigned) fprintf(stdout, "unsigned ");
                fprintf(stdout, "_BitInt(%d)", ty->kind & 0xFFF);
            }
            break;
    }
}

// Memory order and atomicrmw operation names, for the atomic node kinds.
static const char *mem_order_name[] = {
    [0] = "relaxed", [1] = "consume", [2] = "acquire", [3] = "release", [4] = "acq_rel", [5] = "seq_cst",
};

static const char *armw_op_name[] = {
    [0] = "xchg", [1] = "add",  [2] = "sub",  [3] = "and",  [4] = "or",
    [5] = "xor",  [6] = "nand", [7] = "fadd", [8] = "fsub",
};

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
    print_loc(node);

    if (node->ty) {
        fprintf(stdout, "  ty=");
        print_type(node->ty);
    }

    switch (node->kind) {
        case ND_NUM: {
            if (is_flonum(node->ty)) {
                uint64_t b = fp128_to_fp64_bits(node->fpval);
                double d;
                memcpy(&d, &b, 8);
                fprintf(stdout, "  val=%f\n", d);
            } else {
                fprintf(stdout, "  val=%ld\n", (long)int128_to_i64(node->ival));
            }
            break;
        }

        case ND_NULLPTR:
            fprintf(stdout, "  nullptr\n");
            break;

        case ND_VAR:
            fprintf(stdout, "  name=‘%s’", str(node->var->id));
            if (node->is_lvalue) fprintf(stdout, "  lvalue");
            fprintf(stdout, "\n");
            break;

        case ND_SUBACCESS:
            dump_node(node->lhs);
            dump_node(node->rhs);
            break;

        case ND_MEMBER:
            fprintf(stdout, "  member=‘%s’\n", str(node->member->name->id));
            depth++;
            dump_node(node->lhs);
            depth--;
            break;

        case ND_FUNCALL:
            fprintf(stdout, "  narg=%u\n", node->narg);
            depth++;
            dump_node(node->func);
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
        case ND_INIT:
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
        case ND_GT:
        case ND_GE:
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
            fprintf(stdout, "else:\n");
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
                fprintf(stdout, "else:\n");
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
                fprintf(stdout, "incr:\n");
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
            fprintf(stdout, "  val=%ld\n", (long)int128_to_i64(node->ival));
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
        case ND_LABEL_VAL:
            fprintf(stdout, "  label=‘%s’\n", str(node->label));
            break;

        case ND_GOTO:
            fprintf(stdout, "  label=‘%s’\n", str(node->label));
            break;
        case ND_GOTO_EXPR:
            fprintf(stdout, "\n");
            depth++;
            dump_node(node->lhs);
            depth--;
            break;

        case ND_BREAK:
        case ND_CONTINUE:
            if (node->label) fprintf(stdout, "  label=‘%s’\n", str(node->label));
            break;
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
        case ND_SP_SAVE:
            fprintf(stdout, "  stack save\n");
            break;
        case ND_SP_RESTORE:
            fprintf(stdout, "  stack restore\n");
            break;
        case ND_CAS:
            // mem_order / mem_order1 are stored +1, 0 meaning "unspecified
            // (seq_cst)".
            fprintf(stdout, "  order=%s  fail_order=%s  weak=%d\n",
                    mem_order_name[node->mem_order ? node->mem_order - 1 : 5],
                    mem_order_name[node->mem_order1 ? node->mem_order1 - 1 : 5], (int)node->is_weak);
            depth++;
            dump_node(node->lhs);
            dump_node(node->rhs);
            dump_node(node->desired);
            depth--;
            break;
        case ND_ATOMICRMW:
            fprintf(stdout, "  op=%s  order=%s\n", armw_op_name[node->armw_op],
                    mem_order_name[node->mem_order ? node->mem_order - 1 : 5]);
            depth++;
            dump_node(node->lhs);
            dump_node(node->rhs);
            depth--;
            break;
        case ND_ALLOCA:
            fprintf(stdout, "  alloca\n");
            break;
        case ND_FENCE:
            fprintf(stdout, "  fence\n");
            break;
        case ND_VA_START:
        case ND_VA_END:
            fprintf(stdout, "\n");
            depth++;
            dump_node(node->lhs);
            depth--;
            break;
        case ND_VA_ARG:
            fprintf(stdout, "  type=");
            print_type(node->ty);
            fprintf(stdout, "\n");
            depth++;
            dump_node(node->lhs);
            depth--;
            break;
        case ND_VA_COPY:
            fprintf(stdout, "\n");
            depth++;
            dump_node(node->lhs);
            dump_node(node->rhs);
            depth--;
            break;
    }
}

static const char *sclass_name[] = {
    [SC_NONE] = "none",           [SC_TYPEDEF] = "typedef", [SC_EXTERN] = "extern", [SC_STATIC] = "static",
    [SC_THREAD] = "thread_local", [SC_REG] = "register",    [SC_AUTO] = "auto",     [SC_CONSTEXPR] = "constexpr",
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
        fprintf(stdout, "FUNCTION %s  ty=", str(fn->id));
        print_type(fn->ty);
        fprintf(stdout, "  sclass=%s", sclass_name[fn->sclass]);
        if (!fn->is_defined) {
            fprintf(stdout, "  [declaration only]\n\n");
            continue;
        }
        fprintf(stdout, "\n");

        Sym *p = fn->locals;
        if (fn->ty->nparam) {
            fprintf(stdout, "  params:\n");
            for (uint32_t i = 0; i < fn->ty->nparam; p = p->next, i++) {
                fprintf(stdout, "    %s: ", str(p->id));
                print_type(p->ty);
                fprintf(stdout, "\n");
            }
        }
        if (fn->ty->is_variadic) fprintf(stdout, "    ...\n");

        fprintf(stdout, "  locals:\n");
        for (Sym *v = p; v; v = v->next) {
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
