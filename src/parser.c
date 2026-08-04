#include "cxx.h"

#define reverse_list(type, init, next_field)          \
    ({                                                \
        type *prev = NULL, *cur = init, *next = NULL; \
        while (cur) {                                 \
            next = cur->next_field;                   \
            cur->next_field = prev;                   \
            prev = cur;                               \
            cur = next;                               \
        }                                             \
        prev;                                         \
    })

static Module *curm;

static const SClass sc_table[] = {
    [TK_EXTERN] = SC_EXTERN, [TK_REGISTER] = SC_REG,    [TK_STATIC] = SC_STATIC,
    [TK_THREAD] = SC_THREAD, [TK_TYPEDEF] = SC_TYPEDEF,
};

static Type *declspecs(Token **rest, Token *tok, SClass *sclass, int *align, int *funcspec);
static Type *decl_suffix(Token **rest, Token *tok, Type *ty, bool is_param);
static Type *declarator(Token **rest, Token *tok, Type *ty);
static Node *declaration(Token **rest, Token *tok, Type *ty, SClass sclass, int align);
static Node *stmt(Token **rest, Token *tok);
static Node *compound_stmt(Token **rest, Token *tok);
static Node *expr(Token **rest, Token *tok);
static Node *assign(Token **rest, Token *tok);
static Node *cast(Token **rest, Token *tok);
static int64_t const_expr(Token **rest, Token *tok);
static int64_t eval(Node *node);
static int64_t eval2(Node *node, uint32_t *sym);
static int64_t eval_rval(Node *node, uint32_t *sym);
static double eval_double(Node *node);

static Node *new_node(NodeKind kind, Token *tok) {
    Node *node = emalloc(sizeof(Node));
    memset(node, 0, sizeof(*node));
    node->kind = kind;
    node->tok = tok;
    return node;
}

static Node *new_num(int64_t val, Token *tok) {
    Node *node = new_node(ND_NUM, tok);
    node->val = val;
    return node;
}

static Node *new_long(int64_t val, Token *tok) {
    Node *node = new_node(ND_NUM, tok);
    node->val = val;
    node->ty = ty_long;
    return node;
}

static Node *new_ulong(int64_t val, Token *tok) {
    Node *node = new_node(ND_NUM, tok);
    node->val = val;
    node->ty = ty_ulong;
    return node;
}

static Node *new_var_node(Sym *var, Token *tok) {
    Node *node = new_node(ND_VAR, tok);
    node->var = var;
    return node;
}

Node *new_unary(NodeKind kind, Node *expr, Token *tok) {
    Node *node = new_node(kind, tok);
    node->lhs = expr;
    return node;
}

static Node *new_binary(NodeKind kind, Node *lhs, Node *rhs, Token *tok) {
    Node *node = new_node(kind, tok);
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

typedef enum {
    SYM_VAR,
    SYM_FUNC,
    SYM_ENUM,
    SYM_TYNAME,
} SymKind;

// NameSpace for local variables, global variables, typedefs
// or enum constants
typedef struct NameSpace NameSpace;
struct NameSpace {
    NameSpace *next;
    NameSpace *prev;  // Link multiple declarations using the same identifier
    SymKind kind;
    enum {
        LK_NONE,
        LK_EXTERN,
        LK_INTERN,
    } lnk;
    uint32_t id;
    Sym *var;
    Type *ty;
    int64_t enum_val;
    Token *loc;
};

// NameSpace for struct, union or enum tags
typedef struct TagNameSpace TagNameSpace;
struct TagNameSpace {
    TagNameSpace *next;
    TagNameSpace *prev;  // Link multiple tags using the same identifier
    uint32_t id;
    Type *ty;
    Token *loc;
};

// Represents a block scope.
typedef struct Scope Scope;
struct Scope {
    Scope *next;

    // C has two name spaces;
    // one is for struct/union/enum tags.
    // the other is for variables/function/enumerator/typedefs
    NameSpace *vars;
    TagNameSpace *tags;
};

// Represents currently scope.
static Scope *scope = &(Scope){0};
// Represents function prototype scope
static Scope *proto_scope;

static void enter_scope(void) {
    Scope *sc = emalloc(sizeof(Scope));
    sc->vars = NULL;
    sc->tags = NULL;
    sc->next = scope;
    scope = sc;
}

static void leave_scope(void) { scope = scope->next; }

static void push_proto_scope(void) { proto_scope = scope; }
static void pop_proto_scope(void) { scope = proto_scope; }

// All local variable instances created during parsing are
// accumulated to this list.
static Sym *locals;
static Sym *globals;
static Type *types;

// Points to the function object the parser is currently parsing.
static Sym *cur_fn;

// Lists of all goto statements and labels in the curent function.
static Node *gotos;
static Node *labels;
static Node *named_loop;

static Node *cur_sw;

// Find a identifier by name in ordinary name spaces.
static NameSpace *find_ident(Token *tok, bool search_par, bool is_extern) {
    Scope *sc = scope;
    while (sc) {
        for (NameSpace *ns = sc->vars; ns; ns = ns->next)
            if (tok->id == ns->id) {
                if (!is_extern) return ns;
                if (ns->lnk == LK_EXTERN || ns->lnk == LK_INTERN) return ns;
                if (sc == scope) {
                    diag(tok, "error", "extern declaration of ‘%.*s’ follows declaration with no linkage", tok->len,
                         tok->loc);
                    diag_exit(ns->loc, "note", "previous definition is here");
                }
                break;
            }

        if (!search_par && !is_extern) return NULL;
        sc = sc->next;
    }
    return NULL;
}

static TagNameSpace *find_tag(Token *tok, bool search_par) {
    Scope *sc = scope;
    while (sc) {
        for (TagNameSpace *ns = sc->tags; ns; ns = ns->next)
            if (tok->id == ns->id) return ns;
        if (!search_par) return NULL;
        sc = sc->next;
    }
    return NULL;
}

static NameSpace *push_namespace(uint32_t id, SymKind kind, Type *ty, Token *loc) {
    NameSpace *ns = emalloc(sizeof(NameSpace));
    ns->id = id;
    ns->kind = kind;
    ns->ty = ty;
    ns->loc = loc;
    ns->next = scope->vars;
    scope->vars = ns;
    return ns;
}

static void push_tag_namespace(uint32_t id, Type *ty, Token *loc) {
    TagNameSpace *ns = emalloc(sizeof(TagNameSpace));
    ns->id = id;
    ns->ty = ty;
    ns->loc = loc;
    ns->next = scope->tags;
    scope->tags = ns;
    ty->id = id;
}

static Sym *new_var(uint32_t id, Type *ty) {
    Sym *var = emalloc(sizeof(Sym));
    memset(var, 0, sizeof(Sym));
    var->id = id;
    var->ty = ty;
    var->align = ty->align;
    return var;
}

static Sym *new_lvar(uint32_t id, Type *ty) {
    Sym *var = new_var(id, ty);
    var->is_local = true;
    var->next = locals;
    locals = var;
    return var;
}

static Sym *new_gvar(uint32_t id, Type *ty) {
    Sym *var = new_var(id, ty);
    var->next = globals;
    globals = var;
    return var;
}

static uint32_t new_unique_varname(uint32_t id) {
    static int i = 1;
    bool same = false;
    Sym *t = globals;
    while (t) {
        if (t->id == id) {
            same = true;
            break;
        }
        t = t->next;
    }
    if (same) {
        char *name = format("%s.%d", str(id), i++);
        return intern(name, strlen(name));
    }
    return id;
}

static Sym *new_string_literal(Token *tok) {
    uint32_t uid = new_unique_varname(intern(".str", 4));
    Sym *var = new_gvar(uid, tok->ty);
    var->is_str = true;
    var->init_data = tok->id;
    return var;
}

static Type *find_typedef(Token *tok, bool search_par) {
    if (tok->kind == TK_IDENT) {
        NameSpace *sc = find_ident(tok, search_par, false);
        if (sc && sc->kind == SYM_TYNAME) return sc->ty;
    }
    return NULL;
}

static void check_decl_compatile(NameSpace *sym, SymKind kind, Type *ty) {
    if (sym->kind != kind) {
        diag(ty->name, "error", "‘%.*s’ redeclared as different kind of symbol", ty->name->len, ty->name->loc);
        goto note;
    }
    if (!is_compatible(sym->ty, ty)) {
        diag(ty->name, "error", "‘%.*s’ redeclared as conflicting type", ty->name->len, ty->name->loc);
        goto note;
    }
    return;
note:
    diag_exit(sym->loc, "note", "previous definition is here");
}

static void swap(Node **lhs, Node **rhs) {
    Node *tmp = *lhs;
    *lhs = *rhs;
    *rhs = tmp;
}

static Node *new_add(Node *lhs, Node *rhs, Token *tok) {
    add_type(lhs);
    add_type(rhs);

    // num + num
    if (is_arith(lhs->ty) && is_arith(rhs->ty)) return new_binary(ND_ADD, lhs, rhs, tok);

    // Canonicalize `num + ptr` to `ptr + num`.
    if (!is_pointer(lhs->ty) && is_pointer(rhs->ty)) swap(&lhs, &rhs);

    if (!is_pointer(lhs->ty) || !is_integer(rhs->ty)) error(tok, "invalid operands to binary ‘+’");

    // ptr + num
    return new_binary(ND_PTRADD, lhs, rhs, tok);
}

static Node *new_sub(Node *lhs, Node *rhs, Token *tok) {
    add_type(lhs);
    add_type(rhs);

    // num - num
    if (is_arith(lhs->ty) && is_arith(rhs->ty)) return new_binary(ND_SUB, lhs, rhs, tok);

    // ptr - num
    if (is_pointer(lhs->ty) && is_integer(rhs->ty))
        return new_binary(ND_PTRADD, lhs, new_unary(ND_NEG, rhs, rhs->tok), tok);

    if (!is_pointer(lhs->ty) || !is_pointer(rhs->ty) ||
        !is_compatible(type_unqual(lhs->ty->base), type_unqual(rhs->ty->base)))
        error(tok, "invalid operands to binary ‘-’");

    // ptr - ptr, which returns how many elements are between the two.
    size_t size = lhs->ty->base->size;
    lvalue_convert(&lhs);
    lvalue_convert(&rhs);
    new_imcast(&lhs, ty_long);
    new_imcast(&rhs, ty_long);
    Node *node = new_binary(ND_SUB, lhs, rhs, tok);

    if (size == 1) return node;
    return new_binary(ND_DIV, node, new_long(size, tok), tok);
}

// Returns true if a given token represents a type.
static bool is_typename(Token *tok, bool search_par) {
    if (TK_INLINE <= tok->kind && tok->kind <= TK_ALIGNAS) return true;
    return find_typedef(tok, search_par);
}

static uint32_t typequal(Token **rest, Token *tok) {
    uint32_t qual = 0;
    while (1) {
        if (tok->kind == TK_CONST)
            qual |= Q_CONST;
        else if (tok->kind == TK_VOLATILE)
            qual |= Q_VOLATILE;
        else if (tok->kind == TK_RESTRICT)
            qual |= Q_RESTRICT;
        else
            break;
        tok = tok->next;
    }
    *rest = tok;
    return qual;
}

// Ptr ::= ("*" TypeQual*)+
static Type *pointers(Token **rest, Token *tok, Type *ty) {
    while (match(&tok, tok, TK_STAR)) ty = pointer_to(ty, typequal(&tok, tok));
    *rest = tok;
    return ty;
}

// AbsDeclr    ::= Ptr DirAbsDeclr? | DirAbsDeclr
// DirAbsDeclr ::= "(" AbsDeclr ")"
//              | ArrAbsDeclr
//              | FuncAbsDeclr

// ArrAbsDeclr  ::= DirAbsDeclr? ArrDimen
// FuncAbsDeclr ::= DirAbsDeclr? "(" ParamList? ")"

static Type *abstract_declarator(Token **rest, Token *tok, Type *ty, bool is_param) {
    ty = pointers(&tok, tok, ty);

    if (tok->kind == TK_LPAREN &&
        (tok->next->kind != TK_RPAREN && tok->next->kind != TK_ELLIPSIS && !is_typename(tok->next, true))) {
        Token *start = tok;
        Type dummy = {};
        abstract_declarator(&tok, start->next, &dummy, is_param);
        tok = skip(tok, TK_RPAREN);
        ty = decl_suffix(rest, tok, ty, is_param);
        return abstract_declarator(&tok, start->next, ty, is_param);
    }

    Token *name = NULL;
    if (is_param && tok->kind == TK_IDENT) {
        name = tok;
        tok = tok->next;
    }
    ty = decl_suffix(rest, tok, ty, is_param);
    ty->name = name;
    return ty;
}

// TypeName ::= DeclSpecs AbsDeclr?
static Type *typename(Token **rest, Token *tok) {
    Type *ty = declspecs(&tok, tok, NULL, NULL, NULL);
    return abstract_declarator(rest, tok, ty, false);
}

static bool is_end(Token *tok) {
    return tok->kind == TK_RBRACE || (tok->kind == TK_COMMA && tok->next->kind == TK_RBRACE);
}

static bool consume_end(Token **rest, Token *tok) {
    if (tok->kind == TK_RBRACE) {
        *rest = tok->next;
        return true;
    }

    if (tok->kind == TK_COMMA && tok->next->kind == TK_RBRACE) {
        *rest = tok->next->next;
        return true;
    }

    return false;
}

static Token *skip_excess_element(Token *tok) {
    if (tok->kind == TK_LBRACE) {
        tok = tok->next;
        while (tok->kind != TK_RBRACE) {
            tok = skip_excess_element(tok);
            match(&tok, tok, TK_COMMA);
        }
        return skip(tok, TK_RBRACE);
    }

    assign(&tok, tok);
    return tok;
}

// For local variable initializer.
typedef struct InitDesg InitDesg;
struct InitDesg {
    InitDesg *next;
    int idx;
    Member *member;
    Sym *var;
};

static void initializer2(Token **rest, Token *tok, Initializer *init, bool need_brace);

static Initializer *new_initializer(Type *ty, bool is_flexible) {
    Initializer *init = emalloc(sizeof(Initializer));
    memset(init, 0, sizeof(Initializer));
    init->ty = ty;

    if (ty->kind == TY_ARRAY) {
        if (is_flexible && ty->size < 0) {
            init->is_flexible = true;
            return init;
        }
        init->child = emalloc(ty->len * sizeof(Initializer *));
        for (int i = 0; i < ty->len; i++) init->child[i] = new_initializer(ty->base, false);
        return init;
    }
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        // Count the number of struct members.
        int len = 0;
        for (Member *mem = ty->members; mem; mem = mem->next) len++;

        init->child = emalloc(len * sizeof(Initializer *));

        for (Member *mem = ty->members; mem; mem = mem->next) {
            if (is_flexible && ty->is_flexible && !mem->next) {
                Initializer *child = emalloc(sizeof(Initializer));
                child->ty = mem->ty;
                child->is_flexible = true;
                init->child[mem->idx] = child;
            } else {
                init->child[mem->idx] = new_initializer(mem->ty, false);
            }
        }
        return init;
    }

    return init;
}

static void string_initializer(Token **rest, Token *tok, Initializer *init) {
    char *string = str(tok->id);
    int arrlen = str_len(tok->id) + 1;
    if (init->is_flexible) *init = *new_initializer(array_of(init->ty->base, arrlen), false);

    int len = MIN(init->ty->len, arrlen);

    for (int i = 0; i < len; i++) init->child[i]->expr = new_num(string[i], tok);
    *rest = tok->next;
}

static int count_array_init_elements(Token *tok, Type *ty) {
    Initializer *dummy = new_initializer(ty->base, false);

    int i;
    for (i = 0; !consume_end(&tok, tok); i++) {
        if (i > 0) tok = skip(tok, TK_COMMA);
        initializer2(&tok, tok, dummy, false);
    }
    return i;
}

static void array_initializer1(Token **rest, Token *tok, Initializer *init) {
    tok = skip(tok, TK_LBRACE);

    if (init->is_flexible) {
        int len = count_array_init_elements(tok, init->ty);
        *init = *new_initializer(array_of(init->ty->base, len), false);
    }

    for (int i = 0; !consume_end(rest, tok); i++) {
        if (i > 0) tok = skip(tok, TK_COMMA);
        if (i < init->ty->len)
            initializer2(&tok, tok, init->child[i], false);
        else
            tok = skip_excess_element(tok);
    }

    return;
}

static void array_initializer2(Token **rest, Token *tok, Initializer *init) {
    if (init->is_flexible) {
        int len = count_array_init_elements(tok, init->ty);
        *init = *new_initializer(array_of(init->ty->base, len), false);
    }

    for (int i = 0; i < init->ty->len && !is_end(tok); i++) {
        if (i > 0) tok = skip(tok, TK_COMMA);
        initializer2(&tok, tok, init->child[i], false);
    }
    *rest = tok;
}

static void struct_initializer1(Token **rest, Token *tok, Initializer *init) {
    tok = skip(tok, TK_LBRACE);

    Member *mem = init->ty->members;

    while (!consume_end(rest, tok)) {
        if (mem != init->ty->members) tok = skip(tok, TK_COMMA);

        if (mem) {
            initializer2(&tok, tok, init->child[mem->idx], false);
            mem = mem->next;
        } else {
            tok = skip_excess_element(tok);
        }
    }

    return;
}

static void struct_initializer2(Token **rest, Token *tok, Initializer *init) {
    bool first = true;

    for (Member *mem = init->ty->members; mem && !is_end(tok); mem = mem->next) {
        if (!first) tok = skip(tok, TK_COMMA);
        first = false;
        initializer2(&tok, tok, init->child[mem->idx], false);
    }
    *rest = tok;
}

static void union_initializer1(Token **rest, Token *tok, Initializer *init) {
    tok = skip(tok, TK_LBRACE);
    initializer2(&tok, tok, init->child[0], false);
    match(&tok, tok, TK_COMMA);
    *rest = skip(tok, TK_RBRACE);
}

static void union_initializer2(Token **rest, Token *tok, Initializer *init) {
    initializer2(rest, tok, init->child[0], false);
}

// Init       ::= AsExp | BracedInit
// BracedInit ::= "{" Init ("," Init)*)? ","? "}"
static void initializer2(Token **rest, Token *tok, Initializer *init, bool need_brace) {
    if (init->ty->kind == TY_ARRAY && tok->kind == TK_STRLIT) {
        string_initializer(rest, tok, init);
        return;
    }

    if (init->ty->kind == TY_ARRAY) {
        if (tok->kind == TK_LBRACE)
            array_initializer1(rest, tok, init);
        else if (!need_brace)
            array_initializer2(rest, tok, init);
        else
            error(tok, "array initializer must be an initializer list");
        return;
    }

    if (init->ty->kind == TY_STRUCT) {
        if (tok->kind == TK_LBRACE) {
            struct_initializer1(rest, tok, init);
            return;
        }
        Node *expr = assign(rest, tok);
        add_type(expr);
        if (expr->ty->kind == TY_STRUCT) {
            init->expr = expr;
            return;
        }
        if (!need_brace)
            struct_initializer2(rest, tok, init);
        else
            error(tok, "invalid initializer");
        return;
    }

    if (init->ty->kind == TY_UNION) {
        if (tok->kind == TK_LBRACE) {
            union_initializer1(rest, tok, init);
            return;
        }
        Node *expr = assign(rest, tok);
        add_type(expr);
        if (expr->ty->kind == TY_UNION) {
            init->expr = expr;
            return;
        }
        if (!need_brace)
            union_initializer2(rest, tok, init);
        else
            error(tok, "invalid initializer");
        return;
    }

    if (tok->kind == TK_LBRACE) {
        tok = tok->next;
        for (int i = 0; !consume_end(rest, tok); i++) {
            if (i > 0) {
                tok = skip(tok, TK_COMMA);
                tok = skip_excess_element(tok);
            } else {
                init->expr = assign(&tok, tok);
            }
        }
        return;
    }
    init->expr = assign(rest, tok);
}

static void insert_ty(Type *ty, char *kind) {
    int i = -1;
    Type *t = types;
    while (t) {
        if (t->id == ty->id) i++;
        t = t->next;
    }
    char *name;
    if (i >= 0) {
        name = format("%s.%s.%d", kind, str(ty->id), i);
    } else {
        name = format("%s.%s", kind, str(ty->id));
    }
    ty->uid = intern(name, strlen(name));
    ty->next = types;
    types = ty;
}

static Initializer *initializer(Token **rest, Token *tok, Type *ty, Type **new_ty) {
    Initializer *init = new_initializer(ty, true);
    initializer2(rest, tok, init, true);
    if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->is_flexible) {
        ty = copy_type(ty);
        insert_ty(ty, ty->kind == TY_UNION ? "union" : "struct");

        Member *mem = ty->members;
        while (mem->next) mem = mem->next;
        mem->ty = init->child[mem->idx]->ty;
        ty->size += mem->ty->size;

        *new_ty = ty;
        return init;
    }
    *new_ty = init->ty;
    return init;
}

static Member *copy_mem(Member *mem) {
    Member *new = emalloc(sizeof(Member));
    *new = *mem;
    return new;
}

static Node *init_desg_expr(InitDesg *desg, Token *tok) {
    if (desg->var) return new_var_node(desg->var, tok);

    if (desg->member) {
        Node *node = new_unary(ND_MEMBER, init_desg_expr(desg->next, tok), tok);
        node->member = copy_mem(desg->member);
        node->member->next = NULL;
        return node;
    }

    Node *lhs = init_desg_expr(desg->next, tok);
    add_type(lhs);
    if (lhs->ty->kind == TY_ARRAY) new_imcast(&lhs, pointer_to(lhs->ty->base, 0));
    Node *rhs = new_num(desg->idx, tok);
    return new_unary(ND_DEREF, new_add(lhs, rhs, tok), tok);
}

static Node *create_lvar_init(Initializer *init, Type *ty, InitDesg *desg, Token *tok) {
    if (ty->kind == TY_ARRAY) {
        Node *node = new_node(ND_NOP, tok);
        for (int i = 0; i < ty->len; i++) {
            InitDesg desg2 = {desg, i, NULL, NULL};
            Node *rhs = create_lvar_init(init->child[i], ty->base, &desg2, tok);
            node = new_binary(ND_COMMA, node, rhs, tok);
        }
        return node;
    }
    if (ty->kind == TY_STRUCT && !init->expr) {
        Node *node = new_node(ND_NOP, tok);

        for (Member *mem = ty->members; mem; mem = mem->next) {
            InitDesg desg2 = {desg, 0, mem, NULL};
            Node *rhs = create_lvar_init(init->child[mem->idx], mem->ty, &desg2, tok);
            add_type(rhs);
            node = new_binary(ND_COMMA, node, rhs, tok);
        }
        return node;
    }

    if (ty->kind == TY_UNION && !init->expr) {
        InitDesg desg2 = {desg, 0, ty->members, NULL};
        return create_lvar_init(init->child[0], ty->members->ty, &desg2, tok);
    }

    if (!init->expr) return new_node(ND_NOP, tok);

    Node *lhs = init_desg_expr(desg, tok);
    Node *rhs = init->expr;
    Node *node = new_binary(ND_INIT, lhs, rhs, tok);
    add_type(node);
    return node;
}

static bool is_fully_initialized(Initializer *init, Type *ty) {
    switch (ty->kind) {
        case TY_ARRAY:
            if (init->is_flexible && !init->child) return false;
            for (int i = 0; i < ty->len; i++) {
                if (!is_fully_initialized(init->child[i], ty->base)) return false;
            }
            return true;
        case TY_STRUCT:
            for (Member *mem = ty->members; mem; mem = mem->next) {
                if (!is_fully_initialized(init->child[mem->idx], mem->ty)) return false;
            }
            return true;
        case TY_UNION: {
            if (!is_fully_initialized(init->child[0], ty->members->ty)) return false;
            return ty->members->ty->size == ty->size;
        }
        default:
            return init->expr != NULL;
    }
}

static Node *lvar_initializer(Token **rest, Token *tok, Sym *var) {
    Initializer *init = initializer(rest, tok, var->ty, &var->ty);
    InitDesg desg = {NULL, 0, NULL, var};
    // When a variable is not a scalar
    // and the initializer does not explicitly cover all fields
    // zero-initialize the entire memory region of a variable
    Node *lhs;
    if (is_scalar(var->ty) || is_fully_initialized(init, var->ty))
        lhs = new_node(ND_NOP, tok);
    else
        lhs = new_unary(ND_MEMZERO, new_var_node(var, tok), tok);

    Node *rhs = create_lvar_init(init, var->ty, &desg, tok);
    return new_binary(ND_COMMA, lhs, rhs, tok);
}

static void eval_gvar_data(Initializer *init, Type *ty) {
    if (ty->kind == TY_ARRAY) {
        for (int i = 0; i < ty->len; i++) {
            eval_gvar_data(init->child[i], ty->base);
            init->is_inited |= init->child[i]->is_inited;
        }
        return;
    }

    if (ty->kind == TY_STRUCT) {
        for (Member *mem = ty->members; mem; mem = mem->next) {
            eval_gvar_data(init->child[mem->idx], mem->ty);
            init->is_inited |= init->child[mem->idx]->is_inited;
        }
        return;
    }

    if (ty->kind == TY_UNION) {
        eval_gvar_data(init->child[0], ty->members->ty);
        init->is_inited |= init->child[0]->is_inited;
    }

    if (init->expr) {
        uint32_t sym = 0;
        union {
            int64_t val;
            double fval;
        } u;

        if (is_flonum(ty))
            u.fval = eval_double(init->expr);
        else
            u.val = eval2(init->expr, &sym);

        Con *con = &(Con){sym ? CAddr : CBits, sym, {u.val}};

        Ref r = newcon(con, curm, ty);
        init->val = &curm->con[r.val];
        init->is_inited = true;
    }
}

// Initializers for global variables are evaluated at compile-time and
// embedded to .data section. It is a compile error if an
// initializer list contains a non-constant expression.
static void gvar_initializer(Token **rest, Token *tok, Sym *var) {
    Initializer *init = initializer(rest, tok, var->ty, &var->ty);

    eval_gvar_data(init, var->ty);
    var->init = init;
}

static uint32_t get_ident(Token *tok) {
    if (tok->kind != TK_IDENT) error(tok, "expected identifier");
    return tok->id;
}

static Node *fncall(Token **rest, Token *tok) {
    NameSpace *ns = find_ident(tok, true, false);
    if (!ns) error(tok, "implicit declaration of function ‘%.*s’", tok->len, tok->loc);
    if (ns->kind != SYM_FUNC)
        error(tok, "called object ‘%.*s’ is not a function or function pointer", tok->len, tok->loc);

    Node *node = new_node(ND_FUNCALL, tok);
    node->func = tok->id;

    Type *ty = ns->ty;
    Type *param_ty = ty->params;
    node->func_ty = ty;
    node->ty = ty->ret;

    tok = tok->next->next;

    if (tok->kind == TK_RPAREN) {
        if (param_ty)
            error(tok, "too few arguments to function ‘%.*s’; expected %d", ty->name->len, ty->name->loc,
                  ns->var->nparam);
        *rest = tok->next;
        return node;
    }

    Node dummy, *cur = &dummy;
    uint32_t i = 0;

    do {
        Node *arg = assign(&tok, tok);
        if (param_ty) {
            if (param_ty->kind == TY_STRUCT || param_ty->kind == TY_UNION)
                error(arg->tok, "passing struct or union is not supported yet");
            check_asop(param_ty, arg, CTX_CALL);
            lvalue_convert(&arg);
            new_imcast(&arg, param_ty);
            param_ty = param_ty->next;
        } else if (ty->is_variadic) {
            // If parameter type is omitted (e.g. in "..."),
            // "char", "unsinged char" and "signed char" are promoted to "int" or "unsigned int"
            // float arguments are promoted to double.
            if (is_integer(arg->ty)) integer_promotion(&arg);
            if (arg->ty->kind == TY_FLOAT) new_imcast(&arg, ty_double);
            lvalue_convert(&arg);
        } else {
            error(tok, "too many arguments to function ‘%.*s’; expected %d", ty->name->len, ty->name->loc,
                  ns->var->nparam);
        }
        ++i;
        cur = cur->next = arg;
    } while (match(&tok, tok, TK_COMMA));

    if (param_ty)
        error(tok, "too few arguments to function ‘%.*s’; expected %d", ty->name->len, ty->name->loc, ns->var->nparam);

    *rest = skip(tok, TK_RPAREN);

    node->args = dummy.next;
    node->narg = i;
    return node;
}

// PrimExp ::= "true" | "false" | "nullptr" | Num | Str | Ident | "(" Exp ")" | "(" CompStmt ")"
static Node *primary(Token **rest, Token *tok) {
    Node *node;
    if (tok->kind == TK_LPAREN && tok->next->kind == TK_LBRACE) {
        // This is a GNU statement expresssion.
        node = new_node(ND_STMT_EXPR, tok);
        node->body = compound_stmt(&tok, tok->next)->body;
        *rest = skip(tok, TK_RPAREN);
        return node;
    }
    if (tok->kind == TK_LPAREN) {
        node = expr(&tok, tok->next);
        *rest = skip(tok, TK_RPAREN);
        return node;
    }
    if (tok->kind == TK_TRUE) {
        node = new_num(1, tok);
        node->ty = ty_bool;
        *rest = tok->next;
        return node;
    }
    if (tok->kind == TK_FALSE) {
        node = new_num(0, tok);
        node->ty = ty_bool;
        *rest = tok->next;
        return node;
    }
    if (tok->kind == TK_NULLPTR) {
        node = new_node(ND_NULLPTR, tok);
        node->ty = ty_nullptr;
        *rest = tok->next;
        return node;
    }
    if (tok->kind == TK_NUM) {
        node = new_num(tok->val, tok);
        node->ty = tok->ty;
        *rest = tok->next;
        return node;
    }
    if (tok->kind == TK_STRLIT) {
        Sym *var = new_string_literal(tok);
        *rest = tok->next;
        return new_var_node(var, tok);
    }
    if (tok->kind == TK_IDENT) {
        // Function call
        if (tok->next->kind == TK_LPAREN) return fncall(rest, tok);
        // Variable or enum constant
        NameSpace *sc = find_ident(tok, true, false);
        if (!sc) error(tok, "use of undeclared identifier ‘%.*s’", tok->len, tok->loc);
        while (sc->prev) sc = sc->prev;
        if (sc->kind == SYM_TYNAME) error(tok, "unexpected type name ‘%.*s’: expected expression", tok->len, tok->loc);
        if (sc->kind == SYM_VAR)
            node = new_var_node(sc->var, tok);
        else
            node = new_num(sc->enum_val, tok);
        *rest = tok->next;
        return node;
    }
    error(tok, "expected expression before ‘%.*s’", tok->len, tok->loc);
    return NULL;
}

static Member *get_struct_member(Type *ty, Token *tok) {
    for (Member *mem = ty->members; mem; mem = mem->next)
        if (mem->name->id == tok->id) return mem;
    error(tok, "no member named ‘%.*s’ in ‘%s’", tok->len, tok->loc, str(ty->uid));
    return NULL;
}

// PostExp  ::= (PrimExp | CompLit) PostFix*
// CompLit  ::= "(" SCSpec* TypeName ")" BracedInit
// PostFix  ::= "(" ArgList? ")" | "[" Exp "]" | "." Ident | "++" | "--"
// ArgList  ::= AsExp ("," AsExp)*
static Node *postfix(Token **rest, Token *tok) {
    Node *node, *init = NULL;
    Token *start = tok;
    if (tok->kind == TK_LPAREN && is_typename(tok->next, true)) {
        tok = tok->next;
        // Compound literal
        SClass sclass = 0;
        while (TK_CONSTEXPR <= tok->kind && tok->kind <= TK_TYPEDEF) {
            sclass = sc_table[tok->kind];
            tok = tok->next;
        }
        Type *ty = typename(&tok, tok);
        tok = skip(tok, TK_RPAREN);
        Sym *var;
        if (scope->next == NULL || sclass & SC_STATIC) {
            uint32_t uid = new_unique_varname(intern(".compoundliteral", 16));
            var = new_gvar(uid, ty);
            gvar_initializer(&tok, tok, var);
        } else {
            var = new_lvar(intern("", 0), ty);
            init = lvar_initializer(&tok, tok, var);
        }
        var->sclass = sclass;
        node = new_var_node(var, start);
        node->var_init = init;
    } else {
        node = primary(&tok, tok);
    }
    while (1) {
        add_type(node);
        if (node->ty->kind == TY_ARRAY) new_imcast(&node, pointer_to(node->ty->base, 0));
        switch (tok->kind) {
                // x[y] is short for *(x+y)
            case TK_LBRACKET: {
                Token *start = tok;
                Node *idx = expr(&tok, tok->next);
                if (!is_pointer(node->ty) && is_pointer(idx->ty)) swap(&node, &idx);
                if (!is_pointer(node->ty)) error(start, "subscripted value is neither array nor pointer");
                if (!is_integer(idx->ty)) error(start, "array subscript is not an integer");
                if (is_funcptr(node->ty)) error(start, "subscripted value is pointer to function");
                tok = skip(tok, TK_RBRACKET);
                node = new_unary(ND_DEREF, new_add(node, idx, start), start);
                continue;
            }
            case TK_ARROW:
                // x->y is short for (*x).y
                get_ident(tok->next);
                if (!is_pointer(node->ty)) error(tok, "invalid type argument of ‘->’");
                node = new_unary(ND_DEREF, node, tok);
                add_type(node);
                // fall through
            case TK_DOT: {
                Type *ty = node->ty;
                Token *dot = tok;
                tok = tok->next;
                get_ident(tok);
                if (ty->kind != TY_STRUCT && ty->kind != TY_UNION)
                    error(dot, "request for member ‘%.*s’ in something not a structure or union", tok->len, tok->loc);
                Member *mem = copy_mem(get_struct_member(ty, tok));
                mem->next = NULL;
                tok = tok->next;
                node = new_unary(ND_MEMBER, node, dot);
                node->member = mem;
                continue;
            }
            case TK_INC:
                node = new_unary(ND_POSTINC, node, tok);
                tok = tok->next;
                continue;
            case TK_DEC:
                node = new_unary(ND_POSTDEC, node, tok);
                tok = tok->next;
                continue;
            default:
                break;
        }
        break;
    }
    *rest = tok;
    return node;
}

// UnaryExp ::= PostExp | UnaryOP CastExp | ("++" | "--") UnaryExp
//          | "sizeof" UnaryExp | "sizeof" "(" TypeName ")"
//          | "alignof" UnaryExp | "alignof" "(" TypeName ")"
// UnaryOp  ::= "+" | "-" | "~" | "!" | "&" | "*"
static Node *unary(Token **rest, Token *tok) {
    switch (tok->kind) {
        case TK_PLUS:
            return new_unary(ND_PLUS, cast(rest, tok->next), tok);
        case TK_MINUS:
            return new_unary(ND_NEG, cast(rest, tok->next), tok);
        case TK_INVERT:
            return new_unary(ND_INVERT, cast(rest, tok->next), tok);
        case TK_NOT:
            return new_unary(ND_NOT, cast(rest, tok->next), tok);
        case TK_BAND: {
            Node *node = cast(rest, tok->next);
            add_type(node);
            if (node->kind == ND_IMCAST && node->lhs->ty->kind == TY_ARRAY) node = node->lhs;
            return new_unary(ND_ADDR, node, tok);
        }
        case TK_STAR: {
            Node *node = new_unary(ND_DEREF, cast(rest, tok->next), tok);
            add_type(node);
            if (node->ty->kind == TY_ARRAY) new_imcast(&node, pointer_to(node->ty->base, 0));
            return node;
        }
        case TK_INC:
            return new_unary(ND_PREINC, unary(rest, tok->next), tok);
        case TK_DEC:
            return new_unary(ND_PREDEC, unary(rest, tok->next), tok);
        case TK_SIZEOF: {
            Token *start = tok;
            if (tok->next->kind == TK_LPAREN && is_typename(tok->next->next, true)) {
                Type *ty = typename(&tok, tok->next->next);
                *rest = skip(tok, TK_RPAREN);
                if (ty->size < 0) error(start, "invalid application of ‘sizeof’ to incomplete type");
                return new_ulong(ty->size, start);
            }
            Node *node = unary(rest, tok->next);
            add_type(node);
            if (node->kind == ND_IMCAST && node->lhs->ty->kind == TY_ARRAY) node = node->lhs;
            if (node->ty->size < 0) error(start, "invalid application of ‘sizeof’ to incomplete type");
            return new_ulong(node->ty->size, tok);
        }
        case TK_ALIGNOF: {
            Token *start = tok;
            if (tok->next->kind == TK_LPAREN && is_typename(tok->next->next, true)) {
                Type *ty = typename(&tok, tok->next->next);
                *rest = skip(tok, TK_RPAREN);
                return new_ulong(ty->align, start);
            }
            Node *node = unary(rest, tok->next);
            add_type(node);
            if (node->kind == ND_IMCAST && node->lhs->ty->kind == TY_ARRAY) node = node->lhs;
            if (node->ty->size < 0) error(start, "invalid application of ‘alignof’ to incomplete type");
            return new_ulong(node->ty->align, tok);
        }
        default:
            break;
    }
    return postfix(rest, tok);
}

static Node *new_excast(Node *expr, Type *ty, Token *tok) {
    add_type(expr);
    lvalue_convert(&expr);

    if (!is_void(ty) && !is_scalar(ty)) error(tok, "scalar or void type is required in here");
    if (!is_void(ty) && !is_scalar(expr->ty)) error(tok, "scalar type is required in here");
    if (is_flonum(expr->ty) && is_pointer(ty)) error(tok, "cannot cast floating-point value to pointer type");
    if (is_flonum(ty) && is_pointer(expr->ty)) error(tok, "cannot cast pointer to floating-point type");

    if (is_nullptr(ty)) {
        if (!is_null_constant(expr) && !is_nullptr(expr->ty))
            error(tok, "only ‘typeof (nullptr)’ or a null pointer constant can be converted to ‘typeof (nullptr)’");
    }
    if (is_nullptr(expr->ty)) {
        if (!is_pointer(ty) && !is_bool(ty) && !is_void(ty))
            error(tok, "cannot cast nullptr_t to non-void, non-bool, non-pointer type");
    }

    Node *node = new_node(ND_EXCAST, tok);
    node->lhs = expr;
    node->ty = ty;
    return node;
}

// CastExp ::= UnaryExp | "(" TypeName ")" CastExp
static Node *cast(Token **rest, Token *tok) {
    if (tok->kind == TK_LPAREN && is_typename(tok->next, true)) {
        Token *start = tok;
        tok = tok->next;
        SClass sclass = 0;
        while (TK_CONSTEXPR <= tok->kind && tok->kind <= TK_TYPEDEF) {
            sclass = sc_table[tok->kind];
            tok = tok->next;
        }
        Type *ty = typename(&tok, tok);
        tok = skip(tok, TK_RPAREN);
        // compound literal
        if (tok->kind == TK_LBRACE) return unary(rest, start);

        // type cast
        if (sclass) error(start, "storage class specifier is not allowed in this context");
        Node *node = new_excast(cast(rest, tok), ty, start);
        node->tok = start;
        return node;
    }

    return unary(rest, tok);
}

// MulExp   ::= CastExp (("*" | "/" | "%") CastExp)*
// AddExp   ::= MulExp   (("+" | "-") MulExp)*
// ShiftExp ::= AddExp   (("<<" | ">>") AddExp)*
// RelExp   ::= ShiftExp (("<" | ">" | "<=" | ">=") ShiftExp)*
// EqExp    ::= RelExp   (("==" | "!=") RelExp)*
// BAndExp  ::= EqExp    ("&" EqExp)*
// XorExp   ::= BAndExp  ("^" BAndExp)*
// BOrExp   ::= XorExp   ("|" XorExp)*
// LAndExp  ::= BOrExp   ("&&" BOrExp)*;
// LOrExp   ::= LAndExp  ("||" LAndExp)*;
static Node *binexpr(Token **rest, Token *tok, int min_prec) {
    static int op_table[][2] = {
        [TK_OR] = {20, ND_LOGOR},    [TK_AND] = {30, ND_LOGAND}, [TK_BOR] = {40, ND_BOR},    [TK_XOR] = {50, ND_XOR},
        [TK_BAND] = {60, ND_BAND},   [TK_EQ] = {70, ND_EQ},      [TK_NE] = {70, ND_NE},      [TK_LT] = {80, ND_LT},
        [TK_GT] = {80, ND_LT},       [TK_LE] = {80, ND_LE},      [TK_GE] = {80, ND_LE},      [TK_LEFT] = {90, ND_LEFT},
        [TK_RIGHT] = {90, ND_RIGHT}, [TK_PLUS] = {100, ND_ADD},  [TK_MINUS] = {100, ND_SUB}, [TK_STAR] = {110, ND_MUL},
        [TK_SLASH] = {110, ND_DIV},  [TK_MOD] = {110, ND_MOD},
    };

    Node *lhs = cast(&tok, tok);
    add_type(lhs);

    while (TK_OR <= tok->kind && tok->kind <= TK_MOD) {
        Token *op_tok = tok;
        int cur_prec = op_table[op_tok->kind][0];
        if (cur_prec <= min_prec) break;
        NodeKind expr_op = op_table[op_tok->kind][1];

        Node *rhs = binexpr(&tok, tok->next, cur_prec);
        add_type(rhs);

        if (op_tok->kind == TK_GT || op_tok->kind == TK_GE) swap(&lhs, &rhs);

        if (expr_op == ND_ADD)
            lhs = new_add(lhs, rhs, op_tok);
        else if (expr_op == ND_SUB)
            lhs = new_sub(lhs, rhs, op_tok);
        else
            lhs = new_binary(expr_op, lhs, rhs, op_tok);

        add_type(lhs);
    }
    *rest = tok;
    return lhs;
}

// CondExp ::= LOrExp ("?" Exp ":" CondExp)?
static Node *conditional(Token **rest, Token *tok) {
    Node *cond = binexpr(&tok, tok, 0);

    if (tok->kind != TK_QUESTION) {
        *rest = tok;
        return cond;
    }

    Node *node = new_node(ND_COND, tok);
    node->cond = cond;
    node->then = expr(&tok, tok->next);
    tok = skip(tok, TK_COLON);
    node->els = conditional(rest, tok);
    return node;
}

// Evaluate a given node as a constant expression.
//
// A constant expression is either just a number or ptr+n where ptr
// is a pointer to a global variable and n is a postiive/negative
// number. The latter form is accepted only as an initialization
// expression for a global variable.
static double eval_double(Node *node) {
    add_type(node);
    if (is_integer(node->ty)) {
        if (node->ty->is_unsigned) return (unsigned long)eval(node);
        return eval(node);
    }
    switch (node->kind) {
        case ND_ADD:
            return eval_double(node->lhs) + eval_double(node->rhs);
        case ND_SUB:
            return eval_double(node->lhs) - eval_double(node->rhs);
        case ND_MUL:
            return eval_double(node->lhs) * eval_double(node->rhs);
        case ND_DIV:
            return eval_double(node->lhs) / eval_double(node->rhs);
        case ND_NEG:
            return -eval_double(node->lhs);
        case ND_COND:
            return eval_double(node->cond) ? eval_double(node->then) : eval_double(node->els);
        case ND_COMMA:
            return eval_double(node->rhs);
        case ND_IMCAST:
        case ND_EXCAST:
            if (is_flonum(node->lhs->ty)) return eval_double(node->lhs);
            return eval(node->lhs);
        case ND_NUM:
            return node->fval;
        default:
            break;
    }
    error(node->tok, "not a compile-time constant");
    return 0;
}

static int64_t eval_ty(int64_t val, Type *ty) {
    if (is_integer(ty)) {
        switch (ty->size) {
            case 1:
                return ty->is_unsigned ? (int64_t)(uint8_t)val : (int8_t)val;
            case 2:
                return ty->is_unsigned ? (int64_t)(uint16_t)val : (int16_t)val;
            case 4:
                return ty->is_unsigned ? (int64_t)(uint32_t)val : (int32_t)val;
        }
    }
    return val;
}

static int64_t eval(Node *node) { return eval2(node, NULL); }

static int64_t eval2(Node *node, uint32_t *sym) {
    add_type(node);
    if (is_flonum(node->ty)) return eval_double(node);

    switch (node->kind) {
        case ND_NUM:
            return node->val;
        case ND_PLUS:
            return eval(node->lhs);
        case ND_NEG:
            return -eval(node->lhs);
        case ND_NOT:
            return !eval(node->lhs);
        case ND_INVERT:
            return ~eval(node->lhs);
        case ND_COMMA:
            return eval2(node->rhs, sym);
        case ND_ADD:
            return eval(node->lhs) + eval(node->rhs);
        case ND_SUB:
            return eval(node->lhs) - eval(node->rhs);
        case ND_MUL:
            return eval(node->lhs) * eval(node->rhs);
        case ND_DIV:
            if (node->ty->is_unsigned) return (uint64_t)eval(node->lhs) / (uint64_t)eval(node->rhs);
            return eval(node->lhs) / eval(node->rhs);
        case ND_MOD:
            if (node->ty->is_unsigned) return (uint64_t)eval(node->lhs) % (uint64_t)eval(node->rhs);
            return eval(node->lhs) % eval(node->rhs);
        case ND_BAND:
            return eval(node->lhs) & eval(node->rhs);
        case ND_BOR:
            return eval(node->lhs) | eval(node->rhs);
        case ND_XOR:
            return eval(node->lhs) ^ eval(node->rhs);
        case ND_LEFT:
            return eval(node->lhs) << eval(node->rhs);
        case ND_RIGHT:
            if (node->ty->is_unsigned) return (uint64_t)eval(node->lhs) >> eval(node->rhs);
            return eval(node->lhs) >> eval(node->rhs);
        case ND_EQ:
            return eval(node->lhs) == eval(node->rhs);
        case ND_NE:
            return eval(node->lhs) != eval(node->rhs);
        case ND_LT:
            if (node->ty->is_unsigned) return (uint64_t)eval(node->lhs) < (uint64_t)eval(node->rhs);
            return eval(node->lhs) < eval(node->rhs);
        case ND_LE:
            if (node->ty->is_unsigned) return (uint64_t)eval(node->lhs) <= (uint64_t)eval(node->rhs);
            return eval(node->lhs) <= eval(node->rhs);
        case ND_LOGAND:
            return eval(node->lhs) && eval(node->rhs);
        case ND_LOGOR:
            return eval(node->lhs) || eval(node->rhs);
        case ND_COND:
            return eval(node->cond) ? eval2(node->then, sym) : eval2(node->els, sym);
        case ND_PTRADD:
            return eval2(node->lhs, sym) + eval(node->rhs) * node->ty->base->size;
        case ND_IMCAST:
        case ND_EXCAST: {
            int64_t val = eval2(node->lhs, sym);
            return eval_ty(val, node->ty);
        }
        case ND_ADDR:
            return eval_rval(node->lhs, sym);
        case ND_MEMBER:
            if (!sym) error(node->tok, "not a compile-time constant");
            if (node->ty->kind != TY_ARRAY) error(node->tok, "invalid initializer");
            return eval_rval(node->lhs, sym) + node->member->offset;
        case ND_VAR:
            if (!sym) error(node->tok, "not a compile-time constant");
            if (node->var->ty->kind != TY_ARRAY && node->var->ty->kind != TY_FUNC)
                error(node->tok, "invalid initializer");
            *sym = node->var->id;
            return 0;
        default:
            error(node->tok, "not a compile-time constant");
    }
    return 0;
}

static int64_t eval_rval(Node *node, uint32_t *sym) {
    switch (node->kind) {
        case ND_VAR:
            if (node->var->is_local) error(node->tok, "not a compile-time constant");
            *sym = node->var->id;
            return 0;
        case ND_DEREF:
            return eval2(node->lhs, sym);
        case ND_MEMBER:
            return eval_rval(node->lhs, sym) + node->member->offset;
        default:
            error(node->tok, "invalid initializer");
    }
    return 0;
}

// ConstExp ::= CondExp
static int64_t const_expr(Token **rest, Token *tok) {
    Node *node = conditional(rest, tok);
    return eval(node);
}

// AsOP  ::= "=" | "*=" | "/=" | "%=" | "+=" | "-="
//         | "<<=" | ">>=" | "&=" | "^=" | "|="
static inline bool is_assignop(Token *tok) { return TK_AS <= tok->kind && tok->kind <= TK_RIGHTAS; }

// AsExp ::= CondExp (AsOP AsExp)?
static Node *assign(Token **rest, Token *tok) {
    Node *node = conditional(&tok, tok);
    static int as_op[] = {
        [TK_AS] = ND_AS,       [TK_ADDAS] = ND_ADDAS,   [TK_SUBAS] = ND_SUBAS,     [TK_MULAS] = ND_MULAS,
        [TK_DIVAS] = ND_DIVAS, [TK_MODAS] = ND_MODAS,   [TK_ANDAS] = ND_ANDAS,     [TK_ORAS] = ND_ORAS,
        [TK_XORAS] = ND_XORAS, [TK_LEFTAS] = ND_LEFTAS, [TK_RIGHTAS] = ND_RIGHTAS,
    };
    while (is_assignop(tok)) {
        Token *as = tok;
        node = new_binary(as_op[as->kind], node, assign(&tok, tok->next), as);
    }
    *rest = tok;
    add_type(node);
    return node;
}

// Exp ::= AsExp ("," AsExp)*
static Node *expr(Token **rest, Token *tok) {
    Node *node = assign(&tok, tok);
    while (tok->kind == TK_COMMA) {
        Token *comma = tok;
        node = new_binary(ND_COMMA, node, assign(&tok, tok->next), comma);
    }
    *rest = tok;
    add_type(node);
    return node;
}

// InitDecls ::= InitDeclr ("," InitDeclr)*
// InitDeclr ::= Declr ("=" Init)?
static Node *init_decl_list(Token **rest, Token *tok, Type *basety, SClass sclass, int align) {
    Node dummy, *cur = &dummy;
    do {
        Token *start = tok;
        Type *ty = declarator(&tok, tok, basety);
        if (ty->kind == TY_VOID) error(start, "variable ‘%.*s’ declared void", start->len, start->loc);

        bool is_fn = ty->kind == TY_FUNC;
        SymKind symkind = is_fn ? SYM_FUNC : SYM_VAR;
        bool is_static = sclass & SC_STATIC;
        if (is_fn) {
            if (tok->kind == TK_AS)
                error(ty->name,
                      "illegal initializer (only variables can be "
                      "initialized)");
            if (tok->kind == TK_LBRACE) error(ty->name, "function definition is not allowed here");
            if (is_static) error(start, "function declared in block scope cannot have 'static' storage class");
            sclass |= SC_EXTERN;
        }
        bool is_extern = sclass & SC_EXTERN;

        Sym *var;
        NameSpace *ns = find_ident(ty->name, false, is_extern);
        uint32_t id = get_ident(ty->name);
        if (ns) {
            if (!is_extern) {
                diag(ty->name, "error", "redefinition of ‘%.*s’", ty->name->len, ty->name->loc);
                diag_exit(ns->loc, "note", "previous definition is here");
            }
            check_decl_compatile(ns, symkind, ty);
            var = new_lvar(id, ty);
        } else if (is_extern) {
            var = new_gvar(id, ty);
        } else if (is_static) {
            char *name = format("%s.%s", str(cur_fn->id), str(id));
            uint32_t uid = new_unique_varname(intern(name, strlen(name)));
            var = new_gvar(uid, ty);
        } else {
            var = new_lvar(id, ty);
        }
        NameSpace *new_ns = push_namespace(id, symkind, ty, ty->name);
        new_ns->var = var;
        new_ns->prev = ns;
        new_ns->lnk = is_extern ? ns ? ns->lnk : LK_NONE : LK_NONE;
        var->sclass = sclass;
        var->align = MAX(align, ty->align);
        var->is_function = is_fn;
        if (tok->kind == TK_AS) {
            if (is_extern)
                error(ty->name, "declaration of block scope identifier ‘%.*s’ with linkage cannot have an initializer",
                      ty->name->len, ty->name->loc);
            if (is_static) {
                gvar_initializer(&tok, tok->next, var);
            } else {
                Node *expr = lvar_initializer(&tok, tok->next, var);
                cur = cur->next = new_unary(ND_EXPR_STMT, expr, tok);
            }
        }
        if (var->ty->size < 0) error(start, "variable ‘%.*s’ has incomplete type", start->len, start->loc);
    } while (match(&tok, tok, TK_COMMA));

    *rest = tok;
    cur->next = NULL;
    return dummy.next;
}

// ExpStmt ::= ";" | Exp ";"
static Node *expr_stmt(Token **rest, Token *tok) {
    Node *node = new_node(ND_EXPR_STMT, tok);

    if (tok->kind == TK_SEMI) {
        *rest = tok->next;
        return node;
    }

    node->lhs = expr(&tok, tok);

    *rest = skip(tok, TK_SEMI);
    return node;
}

static int cont_depth;
static int brk_depth;

// SelHead ::= Exp | Decl Exp | SimDecl
// SimDecl ::= DeclSpecs Declr "=" Init
static Node *select_head(Token **rest, Token *tok) {
    Node *node;
    if (is_typename(tok, true)) {
        SClass sclass = 0;
        int align = 0;
        int funcspec = 0;
        Type *basety = declspecs(&tok, tok, &sclass, &align, &funcspec);
        node = new_node(ND_DECL, tok);
        if (tok->kind != TK_SEMI) node->body = init_decl_list(&tok, tok, basety, sclass, align);
        if (tok->kind == TK_SEMI) {
            Node *stmt = node->body;
            while (stmt->next) stmt = stmt->next;
            stmt->next = expr(&tok, tok->next);
            lvalue_convert(&stmt->next);
        }
    } else {
        node = expr(&tok, tok);
    }
    *rest = tok;
    return node;
}

// SelStmt ::= "if" "(" SelHead ")" Stmt ("else" Stmt)?
//          | "switch" "(" SelHead ")" Stmt
// IfStmt ::= "if" "(" SelHead ")" Stmt ("else" Stmt)?
static Node *if_stmt(Token **rest, Token *tok) {
    enter_scope();
    Node *node = new_node(ND_IF, tok);
    tok = skip(tok->next, TK_LPAREN);
    // Cond
    node->cond = select_head(&tok, tok);
    tok = skip(tok, TK_RPAREN);
    // Then
    node->then = stmt(&tok, tok);
    // Else
    if (tok->kind == TK_ELSE) node->els = stmt(&tok, tok->next);
    *rest = tok;
    leave_scope();
    return node;
}

// SwitchStmt ::= "switch" "(" SelHead ")" Stmt
static Node *switch_stmt(Token **rest, Token *tok) {
    enter_scope();
    brk_depth++;
    Node *node = new_node(ND_SWITCH, tok);
    Node *sw = cur_sw;
    cur_sw = node;

    // cond
    tok = skip(tok->next, TK_LPAREN);
    node->cond = select_head(&tok, tok);
    add_type(node);
    tok = skip(tok, TK_RPAREN);

    // body
    node->body = stmt(rest, tok);

    brk_depth--;
    leave_scope();
    cur_sw = sw;

    node->case_next = reverse_list(Node, node->case_next, case_next);

    return node;
}

// IterStmt ::= "while" "(" Exp ")" Stmt
//           | "do" Stmt "while" "(" Exp ")" ";"
//           | "for" "(" (Decl | Exp? ";") Exp? ";" Exp? ")" Stmt
// WhileStmt ::= "while" "(" Exp ")" Stmt
static Node *while_stmt(Token **rest, Token *tok) {
    enter_scope();
    cont_depth++;
    brk_depth++;
    Node *node = new_node(ND_WHILE, tok);

    tok = skip(tok->next, TK_LPAREN);
    // Cond
    node->cond = expr(&tok, tok);
    tok = skip(tok, TK_RPAREN);
    // Body
    node->then = stmt(rest, tok);

    cont_depth--;
    brk_depth--;
    leave_scope();
    return node;
}

// DoStmt ::= "do" Stmt "while" "(" Exp ")" ";"
static Node *do_stmt(Token **rest, Token *tok) {
    enter_scope();
    cont_depth++;
    brk_depth++;
    Node *node = new_node(ND_DO, tok);

    // Body
    node->body = stmt(&tok, tok->next);
    // Cond
    tok = skip(tok, TK_WHILE);
    tok = skip(tok, TK_LPAREN);
    node->cond = expr(&tok, tok);
    tok = skip(tok, TK_RPAREN);
    *rest = skip(tok, TK_SEMI);

    cont_depth--;
    brk_depth--;
    leave_scope();
    return node;
}

static Node *for_stmt(Token **rest, Token *tok) {
    enter_scope();
    cont_depth++;
    brk_depth++;
    Node *node = new_node(ND_FOR, tok);
    tok = skip(tok->next, TK_LPAREN);

    // Init
    if (is_typename(tok, true)) {
        SClass sclass = 0;
        int align = 0;
        int funcspec = 0;
        Type *basety = declspecs(&tok, tok, &sclass, &align, &funcspec);
        node->init = declaration(&tok, tok, basety, sclass, align);
    } else {
        node->init = expr_stmt(&tok, tok);
    }

    // Cond
    if (tok->kind != TK_SEMI) node->cond = expr(&tok, tok);
    tok = skip(tok, TK_SEMI);

    // Inc
    if (tok->kind != TK_RPAREN) node->inc = expr(&tok, tok);
    tok = skip(tok, TK_RPAREN);

    // Body
    node->body = stmt(rest, tok);

    cont_depth--;
    brk_depth--;
    leave_scope();
    return node;
}

// JmpStmt ::= "goto" Ident ";"
//          | "continue" Ident? ";"
//          | "break" Ident? ";"
//          | "return" Exp? ";"
// GotoStmt ::= "goto" Ident ";"
static Node *goto_stmt(Token **rest, Token *tok) {
    Node *node = new_node(ND_GOTO, tok);
    node->label = get_ident(tok->next);

    node->goto_next = gotos;
    gotos = node;

    *rest = skip(tok->next->next, TK_SEMI);
    return node;
}

static Node *get_named_loop(Token **rest, Token *tok, bool is_break) {
    uint32_t label_id = tok->id;
    Node *cur = named_loop;
    bool match = false;
    while (cur) {
        if (cur->label == label_id) {
            if (cur->is_loop) match = true;
            if (cur->is_switch && is_break) match = true;
            break;
        }
        cur = cur->loop_next;
    }
    if (!match) {
        char *kind = is_break ? "break" : "continue";
        char *suf = is_break ? " or ‘switch’" : "";
        error(tok, "‘%s’ statement operand ‘%s’ does not refer to a named loop%s", kind, str(label_id), suf);
    }
    *rest = tok->next;
    return cur;
}

// ContinueStmt ::= "continue" Ident? ";"
static Node *continue_stmt(Token **rest, Token *tok) {
    if (!cont_depth) error(tok, "continue statement not within a loop");
    Node *node = new_node(ND_CONTINUE, tok);
    tok = tok->next;

    if (tok->kind == TK_IDENT) {
        node->label = tok->id;
        node->target = get_named_loop(&tok, tok, false);
    }

    *rest = skip(tok, TK_SEMI);
    return node;
}

// BreakStmt ::= "break" Ident? ";"
static Node *break_stmt(Token **rest, Token *tok) {
    if (!brk_depth) error(tok, "break statement not within loop or switch");
    Node *node = new_node(ND_BREAK, tok);
    tok = tok->next;

    if (tok->kind == TK_IDENT) {
        node->label = tok->id;
        node->target = get_named_loop(&tok, tok, true);
    }

    *rest = skip(tok, TK_SEMI);
    return node;
}

// RetStmt ::= "return" Exp? ";"
static Node *return_stmt(Token **rest, Token *tok) {
    Node *node = new_node(ND_RETURN, tok);
    Type *ret = cur_fn->ty->ret;
    if (tok->next->kind == TK_SEMI) {
        if (ret->kind != TY_VOID) error(tok, "non-void function ‘%s’ should return a value", str(cur_fn->id));
        *rest = tok->next->next;
        return node;
    }

    node->lhs = expr(&tok, tok->next);
    if (ret->kind == TY_VOID) error(tok, "void function ‘%s’ should not return a value", str(cur_fn->id));
    *rest = skip(tok, TK_SEMI);

    add_type(node);
    check_asop(ret, node->lhs, CTX_RET);
    new_imcast(&node->lhs, ret);

    return node;
}

static void check_label(uint32_t label, Token *tok) {
    Node *cur = labels;
    while (cur) {
        if (cur->label == label) {
            diag(tok, "error", "redefinition of label ‘%.*s’", tok->len, tok->loc);
            diag_exit(cur->tok, "note", "previous definition is here");
        }
        cur = cur->goto_next;
    }
}

static void check_case(int64_t val, Token *tok) {
    Node *cur = cur_sw->case_next;
    while (cur) {
        if (cur->val == val) {
            diag(tok, "error", "duplicate case value ‘%ld’", val);
            diag_exit(cur->tok, "note", "previous case defined here");
        }
        cur = cur->case_next;
    }
}

// Label ::= Ident ":"
//     | "case" ConstRangeExp ":"
//     | "case" ConstExp ":"
//     | "default" ":"
// ConstRangeExp ::= ConstExp "..." ConstExp
static Node *label(Token **rest, Token *tok) {
    Node head = {}, *cur = &head;
    while (1) {
        if (tok->kind == TK_IDENT && tok->next->kind == TK_COLON) {
            Node *node = new_node(ND_LABEL, tok);
            node->label = tok->id;
            check_label(node->label, tok);
            node->goto_next = labels;
            labels = node;

            cur = cur->label_ring = node;
            tok = tok->next->next;
            continue;
        }
        if (tok->kind == TK_DEFAULT) {
            if (!cur_sw) error(tok, "‘default’ label not within a switch statement");
            if (cur_sw->default_case) {
                diag(tok, "error", "multiple default labels in one switch");
                diag_exit(cur_sw->default_case->tok, "note", "this is the first default label");
            }
            Node *node = new_node(ND_CASE, tok);
            tok = skip(tok->next, TK_COLON);
            cur_sw->default_case = node;
            cur = cur->label_ring = node;
            continue;
        }
        if (tok->kind == TK_CASE) {
            if (!cur_sw) error(tok, "case label not within a switch statement");
            Token *tk_case = tok;
            int64_t val1, val2;
            val1 = const_expr(&tok, tok->next);
            val1 = eval_ty(val1, cur_sw->cond->ty);
            if (tok->kind == TK_COLON) {
                check_case(val1, tk_case);
                Node *node = new_node(ND_CASE, tk_case);
                node->val = val1;

                node->case_next = cur_sw->case_next;
                cur_sw->case_next = node;

                cur = cur->label_ring = node;
                tok = tok->next;
                continue;
            }

            tok = skip(tok, TK_ELLIPSIS);
            val2 = const_expr(&tok, tok);
            val2 = eval_ty(val2, cur_sw->cond->ty);
            for (int64_t i = val1; i <= val2; i++) {
                check_case(i, tk_case);
                Node *node = new_node(ND_CASE, tk_case);
                node->val = i;
                node->case_next = cur_sw->case_next;
                cur_sw->case_next = node;
                cur = cur->label_ring = node;
            }
            tok = skip(tok, TK_COLON);
            continue;
        }
        break;
    }
    *rest = tok;
    cur->label_ring = head.label_ring;
    return head.label_ring;
}

static uint32_t push_named_loop(Node *lb, Token *tok) {
    if (!lb) return 0;
    bool is_switch = false, is_loop = false;
    if (tok->kind == TK_DO || tok->kind == TK_WHILE || tok->kind == TK_FOR) is_loop = true;
    if (tok->kind == TK_SWITCH) is_switch = true;
    if (!is_loop && !is_switch) return 0;

    uint32_t i = 0;
    Node *tmp = lb;
    do {
        if (tmp->kind == ND_LABEL) {
            tmp->is_loop = is_loop;
            tmp->is_switch = is_switch;
            tmp->loop_next = named_loop;
            named_loop = tmp;
            i++;
        }
        tmp = tmp->label_ring;
    } while (tmp != lb);

    return i;
}

// Stmt        ::= LabelStmt | UnLabelStmt
// LabelStmt   ::= Label Stmt
// UnLabelStmt ::= ExpStmt | PrimBlk | JmpStmt
// PrimBlk     ::= CompStmt | SelStmt | IterStmt
static Node *stmt(Token **rest, Token *tok) {
    Node *lb = label(&tok, tok);
    uint32_t i = push_named_loop(lb, tok);
    Node *stmt;
    switch (tok->kind) {
        case TK_LBRACE:
            stmt = compound_stmt(rest, tok);
            break;
        case TK_IF:
            stmt = if_stmt(rest, tok);
            break;
        case TK_SWITCH:
            stmt = switch_stmt(rest, tok);
            break;
        case TK_WHILE:
            stmt = while_stmt(rest, tok);
            break;
        case TK_DO:
            stmt = do_stmt(rest, tok);
            break;
        case TK_FOR:
            stmt = for_stmt(rest, tok);
            break;
        case TK_GOTO:
            stmt = goto_stmt(rest, tok);
            break;
        case TK_CONTINUE:
            stmt = continue_stmt(rest, tok);
            break;
        case TK_BREAK:
            stmt = break_stmt(rest, tok);
            break;
        case TK_RETURN:
            stmt = return_stmt(rest, tok);
            break;
        default:
            stmt = expr_stmt(rest, tok);
            break;
    }
    while (i--) named_loop = named_loop->loop_next;

    if (lb) {
        lb->label_body = stmt;
        return lb;
    }
    return stmt;
}

// CompStmt ::= "{" BlkItem* "}"
// BlkItem  ::= Decl | UnLabelStmt | Label
static Node *compound_stmt2(Token **rest, Token *tok, bool is_func_body) {
    if (!is_func_body) enter_scope();
    Node *node = new_node(ND_COMP_STMT, tok);
    Node dummy, *cur = &dummy;

    tok = tok->next;
    while (tok->kind != TK_RBRACE) {
        Token *start = tok;

        // Label
        Node *lb = label(&tok, tok);
        if (lb) {
            uint32_t i = push_named_loop(lb, tok);

            if (tok->kind == TK_RBRACE)
                lb->label_body = new_node(ND_EXPR_STMT, start);
            else if (is_typename(tok, true))
                lb->label_body = new_node(ND_EXPR_STMT, start);
            else
                lb->label_body = stmt(&tok, tok);

            cur = cur->next = lb;
            add_type(cur);

            while (i--) named_loop = named_loop->loop_next;
            continue;
        }

        // Decl
        if (is_typename(tok, true)) {
            SClass sclass = 0;
            int align = 0;
            int funcspec = 0;
            Type *basety = declspecs(&tok, tok, &sclass, &align, &funcspec);

            if (sclass & SC_TYPEDEF) {
                Type *ty = declarator(&tok, tok, basety);
                push_namespace(get_ident(ty->name), SYM_TYNAME, ty, ty->name);
            } else {
                cur = cur->next = declaration(&tok, tok, basety, sclass, align);
            }

            add_type(cur);
            continue;
        }

        // UnLabelStmt
        cur = cur->next = stmt(&tok, tok);
        add_type(cur);
    }
    cur->next = NULL;
    *rest = skip(tok, TK_RBRACE);

    node->body = dummy.next;
    if (!is_func_body) leave_scope();
    return node;
}

static Node *compound_stmt(Token **rest, Token *tok) { return compound_stmt2(rest, tok, false); }

// EnumSpec ::= "enum" Ident? "{" Enumr ("," Enumr)* ","? "}"
//            | "enum" Ident
// Enumr    ::= Ident ("=" ConstExp)?
static Type *enum_decl(Token **rest, Token *tok) {
    tok = tok->next;
    // Read a enum tag.
    Token *tag = NULL;
    Type *ty = NULL;
    TagNameSpace *ns;
    if (tok->kind == TK_IDENT) {
        tag = tok;
        tok = tok->next;
    }

    if (tag && tok->kind != TK_LBRACE) {
        *rest = tok;
        ns = find_tag(tag, true);
        if (ns) {
            ty = ns->ty;
            if (ty->kind != TY_ENUM) {
                diag(tag, "error", "use of ‘%.*s’ with tag type that does not match previous declaration", tag->len,
                     tag->loc);
                goto note;
            }
            return ty;
        }

        ty = enum_type();
        ty->size = -1;
        push_tag_namespace(tag->id, ty, tag);
        return ty;
    }

    tok = skip(tok, TK_LBRACE);

    Type *exist_ty = NULL;
    bool redefine = false;
    if (tag) {
        ns = find_tag(tag, false);
        if (ns) {
            exist_ty = ns->ty;
            if (exist_ty->kind != TY_ENUM) {
                diag(tag, "error", "use of ‘%.*s’ with tag type that does not match previous declaration", tag->len,
                     tag->loc);
                goto note;
            }
            if (exist_ty->size == -1) {
                ty = exist_ty;
                ty->size = 4;
            } else {
                redefine = true;
                ty = enum_type();
                ty->id = tag->id;
            }
        } else {
            ty = enum_type();
            push_tag_namespace(tag->id, ty, tag);
        }
    } else {
        ty = enum_type();
        ty->is_anon = true;
    }

    // Read an enum-list.
    EnumVal head = {};
    EnumVal *cur = &head;
    int i = 0;
    int64_t val = 0;
    while (!consume_end(rest, tok)) {
        if (i++ > 0) tok = skip(tok, TK_COMMA);

        Token *enm_name = tok;
        uint32_t name = get_ident(enm_name);
        EnumVal *tmp = head.next;
        while (tmp) {
            if (tmp->name == name) {
                diag(enm_name, "error", "redeclaration of enumerator ‘%.*s’", enm_name->len, enm_name->loc);
                diag_exit(tmp->loc, "note", "previous definition is here");
                exit(1);
            }
            tmp = tmp->next;
        }
        if (!redefine) {
            NameSpace *ns2 = find_ident(enm_name, false, false);
            if (ns2) {
                diag(enm_name, "error", "redeclaration of ‘%.*s’", enm_name->len, enm_name->loc);
                diag_exit(ns2->loc, "note", "previous definition is here");
            }
        }
        tok = tok->next;

        if (tok->kind == TK_AS) val = const_expr(&tok, tok->next);

        push_namespace(name, SYM_ENUM, ty, enm_name)->enum_val = val;
        EnumVal *enm = emalloc(sizeof(EnumVal));
        enm->name = name;
        enm->val = val++;
        enm->loc = enm_name;
        cur = cur->next = enm;
    }

    if (!head.next) error(tok, "empty enum is invalid");
    ty->enumvals = head.next;
    if (redefine) {
        if (!is_compatible(ty, exist_ty)) {
            diag(tag, "error", "conflicting redefinition of enum ‘enum %.*s’", tag->len, tag->loc);
            goto note;
        }
        return exist_ty;
    }
    return ty;

note:
    diag_exit(ns->loc, "note", "previous definition is here");
    return NULL;
}

// MemDecl  ::= TypeSpec+ (MemDeclr ("," MemDeclr)*)? ";"
// MemDeclr ::= Declr
static void struct_members(Token **rest, Token *tok, Type *ty) {
    Member head = {};
    Member *cur = &head;

    while (tok->kind != TK_RBRACE) {
        int align = 0;
        Type *basety = declspecs(&tok, tok, NULL, &align, NULL);
        int i = 0;

        while (!match(&tok, tok, TK_SEMI)) {
            if (i++) tok = skip(tok, TK_COMMA);
            Token *start = tok;
            Member *mem = emalloc(sizeof(Member));
            mem->ty = declarator(&tok, tok, basety);
            if (align) mem->is_align = true;
            mem->align = MAX(align, mem->ty->align);
            if (mem->ty->kind == TY_VOID) error(start, "field ‘%.*s’ declared void", start->len, start->loc);
            if (mem->ty->kind == TY_FUNC) error(start, "field ‘%.*s’ declared as a function", start->len, start->loc);
            if (mem->ty->size < 0 && tok->next->kind != TK_RBRACE)
                error(start, "variable ‘%.*s’ has incomplete type", start->len, start->loc);
            mem->name = mem->ty->name;
            Member *tmp = head.next;
            while (tmp) {
                if (tmp->name->id == mem->name->id) {
                    diag(mem->name, "error", "duplicate member ‘%.*s’", mem->name->len, mem->name->loc);
                    diag_exit(tmp->name, "note", "previous declaration is here");
                }
                tmp = tmp->next;
            }
            cur = cur->next = mem;
        }
    }

    if (cur != &head && cur->ty->kind == TY_ARRAY && cur->ty->len < 0) {
        cur->ty = array_of(cur->ty->base, 0);
        ty->is_flexible = true;
    }

    *rest = tok->next;
    ty->members = head.next;
}

// RecordSpec ::= Record Ident ("{" MemDecl+ "}")? | Record "{" MemDecl+ "}"
static Type *record_decl(Token **rest, Token *tok) {
    bool is_union = tok->kind == TK_UNION;
    char *ty_kind = tok->kind == TK_UNION ? "union" : "struct";
    tok = tok->next;
    // Read a tag.
    Token *tag = NULL;
    TagNameSpace *ns;
    Type *ty = NULL;
    if (tok->kind == TK_IDENT) {
        tag = tok;
        tok = tok->next;
    }

    if (tag && tok->kind != TK_LBRACE) {
        *rest = tok;
        ns = find_tag(tag, true);
        if (ns) {
            ty = ns->ty;
            bool match = false;
            if (ty->kind == TY_UNION && is_union)
                match = true;
            else if (ty->kind == TY_STRUCT && !is_union)
                match = true;
            if (!match) {
                diag(tag, "error", "use of ‘%.*s’ with tag type that does not match previous declaration", tag->len,
                     tag->loc);
                goto note;
            }
            return ty;
        }

        ty = struct_type(is_union);
        ty->size = -1;
        push_tag_namespace(tag->id, ty, tag);
        return ty;
    }

    // Construct a struct object.
    tok = skip(tok, TK_LBRACE);

    Type *exist_ty = NULL;
    bool redefine = false;
    if (tag) {
        ns = find_tag(tag, false);
        if (ns) {
            exist_ty = ns->ty;
            bool match = false;
            if (exist_ty->kind == TY_UNION && is_union)
                match = true;
            else if (exist_ty->kind == TY_STRUCT && !is_union)
                match = true;
            if (!match) {
                diag(tag, "error", "use of ‘%.*s’ with tag type that does not match previous declaration", tag->len,
                     tag->loc);
                goto note;
            }
            if (exist_ty->size == -1) {
                ty = exist_ty;
            } else {
                redefine = true;
                ty = struct_type(is_union);
                ty->id = tag->id;
            }
        } else {
            ty = struct_type(is_union);
            push_tag_namespace(tag->id, ty, tag);
        }
    } else {
        ty = struct_type(is_union);
        ty->is_anon = true;
        ty->id = intern("anon", 4);
    }

    struct_members(rest, tok, ty);

    // Assign offsets within the struct to members.
    int offset = 0;
    uint32_t idx = 0;
    for (Member *mem = ty->members; mem; mem = mem->next) {
        ty->align = MAX(ty->align, mem->align);
        mem->idx = idx++;
        if (is_union) {
            offset = MAX(offset, mem->ty->size);
            continue;
        }
        offset = ALIGN_UP(offset, mem->align);
        mem->offset = offset;
        offset += mem->ty->size;
    }
    ty->size = ALIGN_UP(offset, ty->align);

    if (redefine) {
        if (!is_compatible(ty, exist_ty)) {
            diag(tag, "error", "redefinition of struct or union ‘%s %.*s’", ty_kind, tag->len, tag->loc);
            goto note;
        }
        return exist_ty;
    }
    insert_ty(ty, ty_kind);
    return ty;
note:
    diag_exit(ns->loc, "note", "previous definition is here");
    return NULL;
}

// DeclSpecs ::= DeclSpec+
// DeclSpec  ::= SCSpec | TypeSpecQual | FuncSpec
// SCSpec    ::= "typedef" | "static" | "extern" | "register"
// TypeSpecQual ::= TypeSpec | TypeQual | AlignSpec
// TypeSpec  ::= "void" | "_Bool" | "char" | "short" | "int" | "long"
//            | "signed" | "unsigned"
//            | RecordSpec
//            | EnumSpec
//            | TypedefName
// AlignSpec ::= "alignas" "(" (TypeName | ConstExp) ")"
// TypeQual  ::= "const" | "restrict" | "volatile"
// FuncSpec  ::= "inline" | "_Noreturn"
static Type *declspecs(Token **rest, Token *tok, SClass *sclass, int *align, int *funcspec) {
    Type *ty = ty_int;
    int typespec_cnt = 0;
    int qual = 0;
    enum {
        NONE,
        VOID = 1 << 0,
        BOOL = 1 << 2,
        CHAR = 1 << 4,
        SHORT = 1 << 6,
        INT = 1 << 8,
        LONG = 1 << 10,
        FLOAT = 1 << 12,
        DOUBLE = 1 << 14,
        OTHER = 1 << 16,
        SIGNED = 1 << 17,
        UNSIGNED = 1 << 18,
    };

    while (is_typename(tok, true)) {
        Token *ty_tok = tok;
        switch (tok->kind) {
            case TK_TYPEDEF:
            case TK_STATIC:
            case TK_EXTERN:
            case TK_THREAD:
            case TK_REGISTER:
            case TK_CONSTEXPR: {
                SClass sc = sc_table[tok->kind];
                if (!sclass) error(tok, "storage class specifier is not allowed in this context");
                if (*sclass) {
                    if (*sclass & sc)
                        error(tok, "duplicate ‘%.*s’", tok->len, tok->loc);
                    else
                        error(tok, "multiple storage classes in declaration specifiers");
                };
                *sclass = sc;
                break;
            }
            case TK_NORETURN:
                if (!funcspec) error(tok, "function specifier is not allowed in this context");
                *funcspec |= Q_NORETURN;
                break;
            case TK_INLINE:
                if (!funcspec) error(tok, "function specifier is not allowed in this context");
                *funcspec |= Q_INLINE;
                break;
            case TK_CONST:
                qual |= Q_CONST;
                break;
            case TK_VOLATILE:
                qual |= Q_VOLATILE;
                break;
            case TK_RESTRICT:
                error(tok, "restrict requires a pointer or reference");
                break;
            case TK_IDENT: {
                if (typespec_cnt) goto loop_end;
                Type *orig = find_typedef(tok, true);
                if (orig) {
                    ty = orig;
                    typespec_cnt += OTHER;
                    break;
                }
                goto loop_end;
            }
            case TK_STRUCT:
            case TK_UNION:
                ty = record_decl(&tok, tok);
                typespec_cnt += OTHER;
                goto check_type;
            case TK_ENUM:
                ty = enum_decl(&tok, tok);
                typespec_cnt += OTHER;
                goto check_type;
            case TK_ALIGNAS:
                if (!align) error(tok, "alignas is not allowed in this context");
                tok = skip(tok->next, TK_LPAREN);

                if (is_typename(tok, true))
                    *align = typename(&tok, tok)->align;
                else
                    *align = const_expr(&tok, tok);
                if (*align & (*align - 1))
                    error(ty_tok, "requested alignment ‘%ld’ is not a positive power of 2", *align);
                tok = skip(tok, TK_RPAREN);
                continue;
            case TK_VOID:
                typespec_cnt += VOID;
                break;
            case TK_BOOL:
                typespec_cnt += BOOL;
                break;
            case TK_CHAR:
                typespec_cnt += CHAR;
                break;
            case TK_SHORT:
                typespec_cnt += SHORT;
                break;
            case TK_INT:
                typespec_cnt += INT;
                break;
            case TK_LONG:
                typespec_cnt += LONG;
                break;
            case TK_FLOAT:
                typespec_cnt += FLOAT;
                break;
            case TK_DOUBLE:
                typespec_cnt += DOUBLE;
                break;
            case TK_SIGNED:
                typespec_cnt |= SIGNED;
                break;
            case TK_UNSIGNED:
                typespec_cnt |= UNSIGNED;
                break;
            default:
                break;
        }
        tok = tok->next;
    check_type:
        switch (typespec_cnt) {
            case VOID:
                ty = ty_void;
                break;
            case BOOL:
                ty = ty_bool;
                break;
            case CHAR:
                ty = ty_char;
                break;
            case SIGNED + CHAR:
                ty = ty_schar;
                break;
            case UNSIGNED + CHAR:
                ty = ty_uchar;
                break;
            case SHORT:
            case SHORT + INT:
            case SIGNED + SHORT:
            case SIGNED + SHORT + INT:
                ty = ty_short;
                break;
            case UNSIGNED + SHORT:
            case UNSIGNED + SHORT + INT:
                ty = ty_ushort;
                break;
            case INT:
            case SIGNED:
            case SIGNED + INT:
                ty = ty_int;
                break;
            case UNSIGNED:
            case UNSIGNED + INT:
                ty = ty_uint;
                break;
            case LONG:
            case LONG + INT:
            case SIGNED + LONG:
            case SIGNED + LONG + INT:
                ty = ty_long;
                break;
            case UNSIGNED + LONG:
            case UNSIGNED + LONG + INT:
                ty = ty_ulong;
                break;
            case LONG + LONG:
            case LONG + LONG + INT:
            case SIGNED + LONG + LONG:
            case SIGNED + LONG + LONG + INT:
                ty = ty_llong;
                break;
            case UNSIGNED + LONG + LONG:
            case UNSIGNED + LONG + LONG + INT:
                ty = ty_ullong;
                break;
            case FLOAT:
                ty = ty_float;
                break;
            case DOUBLE:
                ty = ty_double;
                break;
            case LONG + DOUBLE:
                ty = ty_double;  // now "long double" as an alias for "double"
                break;
            case NONE:
            case OTHER:
                break;
            default:
                error(ty_tok,
                      "cannot combine with previous"
                      " declaration specifier");
        }
    }
loop_end:
    if (!typespec_cnt) error(tok, "a type specifier is required for all declarations");
    *rest = tok;
    return type_qual(ty, qual);
}

static Type *func_param(Token **rest, Token *tok, Type *ty) {
    tok = skip(tok, TK_LPAREN);
    if (tok->kind == TK_VOID && tok->next->kind == TK_RPAREN) {
        *rest = tok->next->next;
        return func_type(ty);
    }

    enter_scope();
    push_proto_scope();
    locals = NULL;

    bool is_variadic = false;
    Type dummy, *cur = &dummy;

    while (tok->kind != TK_RPAREN) {
        if (cur != &dummy) tok = skip(tok, TK_COMMA);
        if (tok->kind == TK_ELLIPSIS) {
            is_variadic = true;
            tok = tok->next;
            break;
        }

        Token *start = tok;
        Type *basety = declspecs(&tok, tok, NULL, NULL, NULL);
        Type *paramty = abstract_declarator(&tok, tok, basety, true);
        if (paramty->kind == TY_VOID) error(start, "argument may not have ‘void’ type", start->len, start->loc);
        // "array of T" is converted to "pointer to T" in the parameter
        // context. For example, *argv[] is converted to **argv by this.
        if (paramty->kind == TY_ARRAY) {
            Type *arr = paramty;
            paramty = pointer_to(paramty->base, paramty->qual);
            paramty->name = arr->name;
            paramty->is_star = arr->is_star;
            paramty->is_static = arr->is_static;
        }

        if (paramty->size < 0)
            error(paramty->name, "parameter ‘%.*s’ has incomplete type", paramty->name->len, paramty->name->loc);
        cur = cur->next = copy_type(paramty);

        uint32_t id = intern("", 0);
        if (cur->name) {
            id = get_ident(cur->name);
            NameSpace *ns = find_ident(cur->name, false, false);
            if (ns) {
                diag(cur->name, "error", "redefinition of parameter ‘%s’", str(id));
                diag_exit(ns->loc, "note", "previous definition is here");
            }
        }
        push_namespace(id, SYM_VAR, ty, cur->name)->var = new_lvar(id, cur);
    }

    cur->next = NULL;
    *rest = skip(tok, TK_RPAREN);

    ty = func_type(ty);
    ty->is_variadic = is_variadic;
    ty->params = dummy.next;

    leave_scope();
    return ty;
}

// ArrDimen ::= "[" TypeQual* AsExp? "]"
//           | "[" "static" TypeQual* AsExp "]"
//           | "[" TypeQual+ "static" AsExp "]"
//           | "[" TypeQual* "*" "]"
static Type *array_dimensions(Token **rest, Token *tok, Type *ty, bool is_param) {
    int sz = -1;
    bool is_star = false;
    tok = skip(tok, TK_LBRACKET);

    Token *tmp = tok;
    uint32_t qual = typequal(&tok, tok);
    if (!is_param && qual) error(tmp, "type qualifier used in array declarator outside of function prototype");

    tmp = tok;
    bool is_static = match(&tok, tok, TK_STATIC);
    if (!is_param && is_static) error(tmp, "‘static’ used in array declarator outside of function prototype");

    tmp = tok;
    if (!qual) qual = typequal(&tok, tok);
    if (!is_param && qual) error(tmp, "type qualifier used in array declarator outside of function prototype");

    if (is_static) {
        sz = const_expr(&tok, tok);
    } else if (tok->kind == TK_STAR && tok->next->kind == TK_RBRACKET) {
        if (!is_param) error(tok, "[*] used outside of function prototype");
        is_star = true;
        tok = tok->next;
    } else if (tok->kind != TK_RBRACKET) {
        sz = const_expr(&tok, tok);
    }

    tok = skip(tok, TK_RBRACKET);
    ty = decl_suffix(rest, tok, ty, is_param);

    ty = array_of(ty, sz);
    ty->qual = qual;
    ty->is_static = is_static;
    ty->is_star = is_star;
    return ty;
}

// DeclrSuf  ::= "(" ParamList? ")" | "[" ConstExp "]"
// ParamList ::= ParamDecl ("," ParamDecl)* ("," "...")? | "..."
// ParamDecl ::= DeclSpecs Declr
static Type *decl_suffix(Token **rest, Token *tok, Type *ty, bool is_param) {
    if (tok->kind == TK_LPAREN)
        ty = func_param(&tok, tok, ty);
    else if (tok->kind == TK_LBRACKET)
        ty = array_dimensions(&tok, tok, ty, is_param);

    // int arr[]()
    if (ty->kind == TY_ARRAY && ty->base->kind == TY_FUNC) error(tok, "declaration as array of functions");
    // void foo()[]
    if (tok->kind == TK_LBRACKET) error(tok, "function cannot return array type");
    // void foo()()
    if (tok->kind == TK_LPAREN) error(tok, "function cannot return function type");

    *rest = tok;
    return ty;
}

// Declr    ::= Ptr? DirDeclr
// DirDeclr ::= Ident | "(" Declr ")" | ArrDecl | FuncDecl

// ArrDecl  ::= DirDeclr ArrDimen
// FuncDecl ::= DirDeclr "(" ParamList? ")"
static Type *declarator(Token **rest, Token *tok, Type *ty) {
    ty = pointers(&tok, tok, ty);

    if (tok->kind == TK_LPAREN) {
        Token *start = tok;
        Type dummy = {};
        declarator(&tok, start->next, &dummy);
        tok = skip(tok, TK_RPAREN);
        ty = decl_suffix(rest, tok, ty, false);
        return declarator(&tok, start->next, ty);
    }

    if (tok->kind != TK_IDENT) error(tok, "expected identifier or ‘(’");
    ty = decl_suffix(rest, tok->next, ty, false);
    ty->name = tok;
    return ty;
}

// Decl ::= DeclSpecs InitDecls? ";"
static Node *declaration(Token **rest, Token *tok, Type *basety, SClass sclass, int align) {
    Node *node = new_node(ND_DECL, tok);
    if (tok->kind == TK_SEMI) {
        *rest = tok->next;
        return node;
    }
    node->body = init_decl_list(&tok, tok, basety, sclass, align);
    *rest = skip(tok, TK_SEMI);
    return node;
}

static void resolve_goto_labels(void) {
    for (Node *x = gotos; x; x = x->goto_next) {
        for (Node *y = labels; y; y = y->goto_next)
            if (x->label == y->label) {
                x->target = y;
                break;
            }

        if (!x->target) error(x->tok->next, "use of undeclared label");
    }

    gotos = labels = NULL;
}

// ExDecl    ::= FuncDef | Decl
// FuncDef   ::= DeclSpecs Declr CompStmt
static Token *external_declaration(Token *tok) {
    while (match(&tok, tok, TK_SEMI));
    if (tok->kind == TK_EOF) return tok;

    SClass sclass = 0;
    int align = 0;
    int funcspec = 0;
    Type *basety = declspecs(&tok, tok, &sclass, &align, &funcspec);
    if (tok->kind == TK_SEMI) return tok->next;

    int cnt = -1;
    while (1) {
        cnt++;
        Token *start = tok;
        Type *ty = declarator(&tok, tok, basety);
        NameSpace *ns = find_ident(ty->name, false, false);
        Sym *var;
        bool is_func = ty->kind == TY_FUNC;

        // function-definition
        if (tok->kind == TK_LBRACE) {
            if (cnt || !is_func) error(tok, "expected ‘=’, ‘,’, ‘;’ before ‘{’ token");
            if (sclass & SC_TYPEDEF) error(tok, "function definition declared ‘typedef’");
            if (sclass & SC_THREAD) error(tok, "function definition declared ‘thread_local’");
            if (sclass & SC_REG) error(tok, "function definition declared ‘register’");

            if (ns) {
                check_decl_compatile(ns, SYM_FUNC, ty);
                var = ns->var;
                if (var->is_defined) {
                    diag(ty->name, "error", "redefinition of ‘%.*s’", ty->name->len, ty->name->loc);
                    goto note;
                }
                if (sclass == SC_STATIC && var->sclass != SC_STATIC) {
                    diag(ty->name, "error", "static declaration of ‘%.*s’ follows non-static declaration",
                         ty->name->len, ty->name->loc);
                    goto note;
                }
            } else {
                var = new_gvar(get_ident(ty->name), ty);
                ns = push_namespace(var->id, SYM_FUNC, ty, ty->name);
                ns->var = var;
                ns->lnk = sclass == SC_STATIC ? LK_INTERN : LK_EXTERN;
                var->is_function = true;
                var->sclass = sclass ? sclass : SC_EXTERN;
            }

            var->is_defined = true;
            var->funcspec |= funcspec;
            cur_fn = var;
            pop_proto_scope();

            Type *param = ty->params;
            while (param) {
                if (is_pointer(param) && param->is_star)
                    error(param->name, "‘[*]’ not allowed in other than function prototype scope");
                var->nparam++;
                param = param->next;
            }

            var->body = compound_stmt2(&tok, tok, true);

            var->locals = reverse_list(Sym, locals, next);
            var->labels = labels;
            resolve_goto_labels();

            leave_scope();
            return tok;
        }

        // declaration
        SymKind symkind = is_func ? SYM_FUNC : SYM_VAR;
        if (tok->kind == TK_AS) {
            if (is_func || sclass & SC_TYPEDEF)
                error(ty->name,
                      "illegal initializer (only variables can be "
                      "initialized)");
        }
        if (sclass & SC_REG) error(tok, "illegal storage class ‘register’ on file-scoped variable");

        if (sclass & SC_TYPEDEF) {
            if (ns)
                check_decl_compatile(ns, SYM_TYNAME, ty);
            else
                push_namespace(get_ident(ty->name), SYM_TYNAME, ty, ty->name);
        } else {
            if (ns) {
                check_decl_compatile(ns, symkind, ty);
                var = ns->var;
                if (var->is_defined && tok->kind == TK_AS) {
                    diag(ty->name, "error", "redefinition of ‘%.*s’", ty->name->len, ty->name->loc);
                    goto note;
                }
                if (var->sclass & SC_STATIC) {
                    if (!is_func && !(sclass & SC_STATIC)) {
                        diag(ty->name, "error", "non-static declaration of ‘%.*s’ follows static declaration",
                             ty->name->len, ty->name->loc);
                        goto note;
                    }
                }
                if (sclass & SC_STATIC) {
                    if (!(var->sclass & SC_STATIC)) {
                        diag(ty->name, "error", "static declaration of ‘%.*s’ follows non-static declaration",
                             ty->name->len, ty->name->loc);
                        goto note;
                    }
                }
            } else {
                var = new_gvar(get_ident(ty->name), ty);
                var->is_function = is_func;
                var->sclass = sclass;
                var->align = MAX(align, ty->align);
                ns = push_namespace(var->id, symkind, ty, ty->name);
                ns->var = var;
                ns->lnk = sclass & SC_STATIC ? LK_INTERN : LK_EXTERN;
            }

            if (ty->kind == TY_VOID) error(start, "variable ‘%.*s’ declared void", start->len, start->loc);

            if (tok->kind == TK_AS) {
                gvar_initializer(&tok, tok->next, var);
                var->is_defined = true;
            }
            if (var->ty->size < 0 && var->ty->kind != TY_ARRAY)
                error(start, "variable ‘%.*s’ has incomplete type", start->len, start->loc);
        }
        if (match(&tok, tok, TK_COMMA))
            continue;
        else if (tok->kind == TK_SEMI)
            return tok->next;
        else
            error(tok, "expected ‘;’ after top level declarator");
    note:
        diag_exit(ns->loc, "note", "previous definition is here");
    }
}

// TransUnit ::= ExDecl+
Module *parse(Token *tok) {
    Module *md = emalloc(sizeof(Module));
    memset(md, 0, sizeof(Module));
    md->con = vnew(2, sizeof md->con[0]);
    curm = md;

    cont_depth = 0;
    brk_depth = 0;
    globals = NULL;
    while (tok->kind != TK_EOF) tok = external_declaration(tok);

    for (Sym *sym = globals; sym;) {
        Sym *next = sym->next;
        if (sym->is_function) {
            sym->next = md->fns;
            md->fns = sym;
        } else {
            sym->next = md->data;
            md->data = sym;
        }
        sym = next;
    }
    md->tys = reverse_list(Type, types, next);
    return md;
}
