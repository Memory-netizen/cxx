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
    [TK_EXTERN] = SC_EXTERN,   [TK_REGISTER] = SC_REG, [TK_STATIC] = SC_STATIC,       [TK_THREAD] = SC_THREAD,
    [TK_TYPEDEF] = SC_TYPEDEF, [TK_AUTO] = SC_AUTO,    [TK_CONSTEXPR] = SC_CONSTEXPR,
};

static const char *sclass_name[] = {
    [SC_NONE] = "none",           [SC_TYPEDEF] = "typedef", [SC_EXTERN] = "extern", [SC_STATIC] = "static",
    [SC_THREAD] = "thread_local", [SC_REG] = "register",    [SC_AUTO] = "auto",     [SC_CONSTEXPR] = "constexpr",
};

static Type *declspecs(Token **rest, Token *tok, SClass *sclass, int *align, int *funcspec);
static Type *decl_suffix(Token **rest, Token *tok, Type *ty, bool is_param);
static Type *declarator(Token **rest, Token *tok, Type *ty);
static Node *declaration(Token **rest, Token *tok, Type *ty, SClass sclass, int align, int funcspec);
static Node *stmt(Token **rest, Token *tok);
static Node *compound_stmt(Token **rest, Token *tok);
static Node *expr(Token **rest, Token *tok);
static Node *assign(Token **rest, Token *tok);
static Node *cast(Token **rest, Token *tok);
static int64_t eval(Node *node);
static int64_t eval2(Node *node, uint32_t *sym);
static int64_t eval_rval(Node *node, uint32_t *sym);
static Fp128 eval_fp128(Node *node);
static Int128 eval_int128(Node *node);
static void array_initializer2(Token **rest, Token *tok, Initializer *init, int i);
static void struct_initializer2(Token **rest, Token *tok, Initializer *init, Member *mem);
static Member *get_struct_member(Member *mem, Token *tok);

static Node *new_node(NodeKind kind, Token *tok) {
    Node *node = emalloc(sizeof(Node));
    node->kind = kind;
    node->tok = tok;
    return node;
}

static Node *new_num(int64_t val, Token *tok) {
    Node *node = new_node(ND_NUM, tok);
    node->ival = int128_set_i(val);
    return node;
}

// Build an ND_NUM node for TK_NUM. The lexer has already parsed the
// literal: integer constants live in tok->ival, floating constants in
// tok->fpval (single-rounded to the target format); there is no
// re-parsing here.
static Node *new_num_node(Token *tok) {
    Node *node = new_node(ND_NUM, tok);
    node->ty = infer_numtype(tok);

    if (is_fpval(node->ty) || is_flonum(node->ty)) {
        node->fpval = tok->fpval;
        return node;
    }

    if (tok->lit_suffix & SUF_BITINT) {
        // The lexer already parsed the full value into tok->ival (and
        // rejected literals beyond 128 bits).
        bool uns = (tok->lit_suffix & SUF_UNSIGNED) != 0;
        Int128 v = tok->ival;
        // Width per C23: signed wb needs a sign bit on top of the value
        // bits (minimum 2); unsigned uwb is just the value width (min 1).
        int w = uns ? int128_bit_width(v, UNSIGNED) : MAX(2, int128_bit_width(v, UNSIGNED) + 1);
        if (w > 128) error(tok, "width of _BitInt literal exceeds __BITINT_MAXWIDTH__");
        node->ty = bitint[w][uns];
        if (!node->ty) error(tok, "invalid _BitInt width %d", w);
        node->ival = v;
        return node;
    }

    node->ival = tok->ival;
    return node;
}

static Node *new_long(int64_t val, Token *tok) {
    Node *node = new_node(ND_NUM, tok);
    node->ival = int128_set_i(val);
    node->ty = T.ty_long;
    return node;
}

static Node *new_ulong(int64_t val, Token *tok) {
    Node *node = new_node(ND_NUM, tok);
    node->ival = int128_set_i(val);
    node->ty = T.ty_ulong;
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
    NameSpace *next;   // declaration list link
    NameSpace *hnext;  // hash chain link
    NameSpace *prev;   // Link multiple declarations using the same identifier
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
    TagNameSpace *next;   // declaration list link
    TagNameSpace *hnext;  // hash chain link
    TagNameSpace *prev;   // Link multiple tags using the same identifier
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
    // Hash table over vars (chain addressing) so identifier lookup
    // stays O(1) in scopes with many declarations.
    NameSpace **ht;
    int ht_cap;
    int ht_n;
    TagNameSpace *tags;
    // Hash table over tags, same scheme.
    TagNameSpace **tht;
    int tht_cap;
    int tht_n;
    int vla_num;
    Node **vla_expr;
    Sym *stack_top;
    bool sp_saved;
};

// Represents currently scope.
static Scope *scope;
static Scope *file_scope;

static void enter_scope(void) {
    Scope *sc = emalloc(sizeof(Scope));
    sc->vla_expr = vnew(2, sizeof(Node *));
    sc->next = scope;
    scope = sc;
}

static Node *leave_scope(Token *tok) {
    Node *node = NULL;
    if (scope->sp_saved) {
        node = new_var_node(scope->stack_top, tok);
        add_type(node);
        lvalue_convert(&node);
        node = new_unary(ND_SP_RESTORE, node, tok);
        node->ty = T.ty_void;
    }
    scope = scope->next;
    return node;
}

static bool is_file_scope(void) { return scope == file_scope; }

// All local variable instances created during parsing are
// accumulated to this list.
static Sym *locals;
static Sym *globals;
static Type *types;

// Points to the function object the parser is currently parsing.
static Sym *cur_fn;

// Block accounting: the parser mirrors irgen's per-construct block
// allocation so all blocks live in one per-function array
// (Sym.blks). Every construct that gen_* allocates blocks for must
// count here; irgen asserts the totals match.
static void cnt_blk(int n) {
    if (cur_fn) cur_fn->num_blk += n;
}

// Lists of all goto statements and labels in the curent function.
static Node *gotos;
static Node *labels;
static Node *named_loop;

static Node *cur_sw;

// Find a identifier by name in ordinary name spaces.
static NameSpace *find_ident(Token *tok, bool search_par, bool is_extern) {
    Scope *sc = scope;
    while (sc) {
        NameSpace *hit = NULL;
        if (sc->ht)
            for (NameSpace *ns = sc->ht[tok->id & (sc->ht_cap - 1)]; ns; ns = ns->hnext)
                if (tok->id == ns->id) {
                    hit = ns;
                    break;
                }
        for (NameSpace *ns = hit ?: sc->vars; ns; ns = ns->next)
            if (tok->id == ns->id) {
                if (!is_extern) return ns;
                if (ns->lnk == LK_EXTERN || ns->lnk == LK_INTERN) return ns;
                if (sc == scope) {
                    diag("error", tok, "extern declaration of ‘%s’ follows declaration with no linkage", str(tok->id));
                    diag_exit("note", ns->loc, "previous definition is here");
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
        TagNameSpace *hit = NULL;
        if (sc->tht)
            for (TagNameSpace *ns = sc->tht[tok->id & (sc->tht_cap - 1)]; ns; ns = ns->hnext)
                if (tok->id == ns->id) {
                    hit = ns;
                    break;
                }
        for (TagNameSpace *ns = hit ?: sc->tags; ns; ns = ns->next)
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

    if (!scope->ht) {
        scope->ht_cap = 64;
        scope->ht = vnew(scope->ht_cap, sizeof(NameSpace *));
    } else if (scope->ht_n >= scope->ht_cap * 2) {
        int cap = scope->ht_cap * 2;
        NameSpace **ht = vnew(cap, sizeof(NameSpace *));
        NameSpace **tail = vnew(cap, sizeof(NameSpace *));
        for (int i = 0; i < cap; i++) ht[i] = tail[i] = NULL;
        for (int i = 0; i < scope->ht_cap; i++)
            for (NameSpace *x = scope->ht[i]; x;) {
                NameSpace *next = x->hnext;  // saved before the link is rewired
                int h = x->id & (cap - 1);
                // tail-insert keeps the chain order (newest declaration
                // first, as in the vars list)
                x->hnext = NULL;
                if (tail[h])
                    tail[h]->hnext = x;
                else
                    ht[h] = x;
                tail[h] = x;
                x = next;
            }
        scope->ht = ht;
        scope->ht_cap = cap;
    }
    int h = id & (scope->ht_cap - 1);
    ns->hnext = scope->ht[h];
    scope->ht[h] = ns;
    scope->ht_n++;
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

    if (!scope->tht) {
        scope->tht_cap = 64;
        scope->tht = vnew(scope->tht_cap, sizeof(TagNameSpace *));
    } else if (scope->tht_n >= scope->tht_cap * 2) {
        int cap = scope->tht_cap * 2;
        TagNameSpace **tht = vnew(cap, sizeof(TagNameSpace *));
        TagNameSpace **tail = vnew(cap, sizeof(TagNameSpace *));
        for (int i = 0; i < cap; i++) tht[i] = tail[i] = NULL;
        for (int i = 0; i < scope->tht_cap; i++)
            for (TagNameSpace *x = scope->tht[i]; x;) {
                TagNameSpace *next = x->hnext;  // saved before the link is rewired
                int h = x->id & (cap - 1);
                // tail-insert keeps the chain order (newest tag first)
                x->hnext = NULL;
                if (tail[h])
                    tail[h]->hnext = x;
                else
                    tht[h] = x;
                tail[h] = x;
                x = next;
            }
        scope->tht = tht;
        scope->tht_cap = cap;
    }
    int h = id & (scope->tht_cap - 1);
    ns->hnext = scope->tht[h];
    scope->tht[h] = ns;
    scope->tht_n++;
}

static Sym *new_var(uint32_t id, Type *ty) {
    Sym *var = emalloc(sizeof(Sym));
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

// Identical string literals share one global (content is interned, so
// the id is the content hash); generated files can repeat a literal
// thousands of times, and each distinct global would otherwise cost a
// linear constant-pool scan.
static Sym **strlit_ht;
static int strlit_cap;
static int strlit_n;

static Sym *new_string_literal(uint32_t id, Type *ty) {
    if (!strlit_cap) {
        strlit_cap = 64;
        strlit_ht = vnew(strlit_cap, sizeof(Sym *));
    } else if (strlit_n >= strlit_cap * 2) {
        int cap = strlit_cap * 2;
        Sym **ht = vnew(cap, sizeof(Sym *));
        Sym **tail = vnew(cap, sizeof(Sym *));
        for (int i = 0; i < cap; i++) ht[i] = tail[i] = NULL;
        for (int i = 0; i < strlit_cap; i++)
            for (Sym *x = strlit_ht[i]; x;) {
                Sym *next = x->str_next;  // saved before the link is rewired
                int h = x->init_data & (cap - 1);
                x->str_next = NULL;
                if (tail[h])
                    tail[h]->str_next = x;
                else
                    ht[h] = x;
                tail[h] = x;
                x = next;
            }
        strlit_ht = ht;
        strlit_cap = cap;
    }
    int h = id & (strlit_cap - 1);
    for (Sym *v = strlit_ht[h]; v; v = v->str_next)
        // infer_strtype builds a fresh array Type per literal, but the
        // element types are the canonical target singletons and the
        // length follows from the interned content id: the element
        // pointer alone distinguishes u8"x"/"x"/L"x"/U"x"/u"x".
        if (v->init_data == id && v->ty->base == ty->base) return v;
    uint32_t uid = new_unique_varname(intern(".str", 4));
    Sym *var = new_gvar(uid, ty);
    var->is_str = true;
    var->init_data = id;
    var->str_next = strlit_ht[h];
    strlit_ht[h] = var;
    strlit_n++;
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
        diag("error", ty->name, "‘%s’ redeclared as different kind of symbol", str(ty->name->id));
        goto note;
    }
    if (!is_compatible(sym->ty, ty)) {
        diag("error", ty->name, "‘%s’ redeclared as conflicting type", str(ty->name->id));
        goto note;
    }
    return;
note:
    diag_exit("note", sym->loc, "previous definition is here");
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

    // VLA + num
    if (lhs->ty->base->kind == TY_VLA) {
        Type *base_ty = lhs->ty->base;
        while (base_ty->kind == TY_VLA) {
            rhs = new_binary(ND_MUL, rhs, new_var_node(base_ty->vla_cnt, tok), tok);
            base_ty = base_ty->base;
        }
        return new_binary(ND_PTRADD, lhs, rhs, tok);
    }

    // ptr + num
    return new_binary(ND_PTRADD, lhs, rhs, tok);
}

static Node *new_sub(Node *lhs, Node *rhs, Token *tok) {
    add_type(lhs);
    add_type(rhs);

    // num - num
    if (is_arith(lhs->ty) && is_arith(rhs->ty)) return new_binary(ND_SUB, lhs, rhs, tok);

    // ptr - num
    if (is_pointer(lhs->ty) && is_integer(rhs->ty)) return new_add(lhs, new_unary(ND_NEG, rhs, rhs->tok), tok);

    if (!is_pointer(lhs->ty) || !is_pointer(rhs->ty) ||
        !is_compatible(type_unqual(lhs->ty->base), type_unqual(rhs->ty->base)))
        error(tok, "invalid operands to binary ‘-’");

    // ptr - ptr, which returns how many elements are between the two.
    size_t size = lhs->ty->base->size;
    lvalue_convert(&lhs);
    lvalue_convert(&rhs);
    new_imcast(&lhs, T.ty_long);
    new_imcast(&rhs, T.ty_long);
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
    Type *ty = infer_strtype(tok);
    int arrlen = ty->len;
    if (init->is_flexible) *init = *new_initializer(array_of(init->ty->base, arrlen), false);

    char *string = str(tok->id);
    int len = MIN(init->ty->len, arrlen);

    switch (init->ty->base->size) {
        case 1: {
            char *str = string;
            for (int i = 0; i < len; i++) init->child[i]->expr = new_num(str[i], tok);
            break;
        }
        case 2: {
            uint16_t *str = (uint16_t *)string;
            for (int i = 0; i < len; i++) init->child[i]->expr = new_num(str[i], tok);
            break;
        }
        case 4: {
            uint32_t *str = (uint32_t *)string;
            for (int i = 0; i < len; i++) init->child[i]->expr = new_num(str[i], tok);
            break;
        }
        default:
            break;
    }

    *rest = tok->next;
}

static void array_designator(Token **rest, Token *tok, Type *ty, int *begin, int *end) {
    *begin = const_expr(&tok, tok->next);
    if (*begin >= ty->len) error(tok, "array designator index exceeds array bounds");

    if (tok->kind == TK_ELLIPSIS) {
        *end = const_expr(&tok, tok->next);
        if (*end >= ty->len) error(tok, "array designator index exceeds array bounds");
        if (*end < *begin) error(tok, "array designator range [%d, %d] is empty", *begin, *end);
    } else {
        *end = *begin;
    }
    *rest = skip(tok, TK_RBRACKET);
}

static Member *struct_designator(Token **rest, Token *tok, Type *ty) {
    Token *start = tok;
    tok = skip(tok, TK_DOT);
    if (tok->kind != TK_IDENT) error(tok, "expected a field designator");

    for (Member *mem = ty->members; mem; mem = mem->next) {
        // Anonymous struct member
        if (mem->ty->kind == TY_STRUCT && !mem->name) {
            if (get_struct_member(mem, tok)) {
                *rest = start;
                return mem;
            }
            continue;
        }

        // Regular struct member
        if (mem->name->id == tok->id) {
            *rest = tok->next;
            return mem;
        }
    }

    error(tok, "struct has no such member");
    return NULL;
}

// Desig ::= "[" (ConstExp | ConstRangeExp) "]" | "." Ident
static void designation(Token **rest, Token *tok, Initializer *init) {
    if (tok->kind == TK_LBRACKET) {
        if (init->ty->kind != TY_ARRAY) error(tok, "array index in non-array initializer");
        int begin, end;
        array_designator(&tok, tok, init->ty, &begin, &end);
        Token *tok2;
        for (int i = begin; i <= end; i++) designation(&tok2, tok, init->child[i]);

        array_initializer2(rest, tok2, init, end + 1);
        return;
    }

    if (tok->kind == TK_DOT && init->ty->kind == TY_STRUCT) {
        Member *mem = struct_designator(&tok, tok, init->ty);
        designation(&tok, tok, init->child[mem->idx]);
        init->expr = NULL;
        struct_initializer2(rest, tok, init, mem->next);
        return;
    }

    if (tok->kind == TK_DOT && init->ty->kind == TY_UNION) {
        Member *mem = struct_designator(&tok, tok, init->ty);
        init->mem = mem;
        designation(rest, tok, init->child[mem->idx]);
        return;
    }

    if (tok->kind == TK_DOT) error(tok, "field name not in struct or union initializer");

    tok = skip(tok, TK_AS);
    initializer2(rest, tok, init, false);
}

static int count_array_init_elements(Token *tok, Type *ty) {
    Initializer *dummy = new_initializer(ty->base, true);

    bool first = true;
    int i = 0, max = 0;
    while (!consume_end(&tok, tok)) {
        if (!first) tok = skip(tok, TK_COMMA);
        first = false;
        if (tok->kind == TK_LBRACKET) {
            i = const_expr(&tok, tok->next);
            if (tok->kind == TK_ELLIPSIS) i = const_expr(&tok, tok->next);
            tok = skip(tok, TK_RBRACKET);
            designation(&tok, tok, dummy);
        } else {
            initializer2(&tok, tok, dummy, false);
        }

        i++;
        max = MAX(max, i);
    }
    return max;
}

static void array_initializer1(Token **rest, Token *tok, Initializer *init) {
    tok = skip(tok, TK_LBRACE);

    if (init->is_flexible) {
        int len = count_array_init_elements(tok, init->ty);
        *init = *new_initializer(array_of(init->ty->base, len), false);
    }

    bool first = true;
    for (int i = 0; !consume_end(rest, tok); i++) {
        if (!first) tok = skip(tok, TK_COMMA);

        first = false;
        if (tok->kind == TK_LBRACKET) {
            int begin, end;
            array_designator(&tok, tok, init->ty, &begin, &end);

            Token *tok2;
            for (int j = begin; j <= end; j++) designation(&tok2, tok, init->child[j]);
            tok = tok2;
            i = end;
            continue;
        }
        if (i < init->ty->len)
            initializer2(&tok, tok, init->child[i], false);
        else
            tok = skip_excess_element(tok);
    }
    return;
}

static void array_initializer2(Token **rest, Token *tok, Initializer *init, int i) {
    if (init->is_flexible) {
        int len = count_array_init_elements(tok, init->ty);
        *init = *new_initializer(array_of(init->ty->base, len), false);
    }

    for (; i < init->ty->len && !is_end(tok); i++) {
        Token *start = tok;
        if (i > 0) tok = skip(tok, TK_COMMA);
        if (tok->kind == TK_LBRACKET || tok->kind == TK_DOT) {
            *rest = start;
            return;
        }
        initializer2(&tok, tok, init->child[i], false);
    }
    *rest = tok;
}

static void struct_initializer1(Token **rest, Token *tok, Initializer *init) {
    tok = skip(tok, TK_LBRACE);

    Member *mem = init->ty->members;
    bool first = true;
    while (!consume_end(rest, tok)) {
        if (!first) tok = skip(tok, TK_COMMA);
        first = false;
        if (tok->kind == TK_DOT) {
            mem = struct_designator(&tok, tok, init->ty);
            designation(&tok, tok, init->child[mem->idx]);
            mem = mem->next;
            continue;
        }

        if (mem && !mem->name && !is_record(mem->ty)) mem = mem->next;
        if (mem) {
            initializer2(&tok, tok, init->child[mem->idx], false);
            mem = mem->next;
        } else {
            tok = skip_excess_element(tok);
        }
    }

    return;
}

static void struct_initializer2(Token **rest, Token *tok, Initializer *init, Member *mem) {
    bool first = true;
    for (; mem && !is_end(tok); mem = mem->next) {
        Token *start = tok;
        if (!first) tok = skip(tok, TK_COMMA);
        first = false;
        if (tok->kind == TK_LBRACKET || tok->kind == TK_DOT) {
            *rest = start;
            return;
        }

        if (mem && !mem->name && !is_record(mem->ty)) mem = mem->next;
        initializer2(&tok, tok, init->child[mem->idx], false);
    }
    *rest = tok;
}

static void union_initializer1(Token **rest, Token *tok, Initializer *init) {
    tok = skip(tok, TK_LBRACE);

    Member *mem = init->ty->members;
    bool first = true;
    while (!consume_end(rest, tok)) {
        if (!first) tok = skip(tok, TK_COMMA);
        first = false;
        if (tok->kind == TK_DOT) {
            mem = struct_designator(&tok, tok, init->ty);
            init->mem = mem;
            designation(&tok, tok, init->child[mem->idx]);
            continue;
        }

        if (!init->mem) {
            init->mem = mem;
            initializer2(&tok, tok, init->child[mem->idx], false);
            continue;
        }

        tok = skip_excess_element(tok);
    }

    return;
}

static void union_initializer2(Token **rest, Token *tok, Initializer *init) {
    init->mem = init->ty->members;
    initializer2(rest, tok, init->child[0], false);
}

// Init       ::= AsExp | BracedInit
// BracedInit ::= "{" ((Desig+ "=")? Init ("," (Desig+ "=")? Init)* ","?)? "}"
static void initializer2(Token **rest, Token *tok, Initializer *init, bool need_brace) {
    if (init->ty->kind == TY_ARRAY) {
        if (tok->kind == TK_STRLIT ||
            (tok->kind == TK_LBRACE && tok->next->kind == TK_STRLIT && tok->next->next->kind == TK_RBRACE)) {
            bool has_brace = match(&tok, tok, TK_LBRACE);
            Type *ty = infer_strtype(tok);
            if (!(is_char(init->ty->base) && is_char(ty->base)) && !is_compatible(ty, init->ty))
                error(tok, "array of inappropriate type initialized from string constant");
            string_initializer(&tok, tok, init);
            if (has_brace) tok = skip(tok, TK_RBRACE);
            *rest = tok;
            return;
        }
    }

    if (init->ty->kind == TY_ARRAY) {
        if (tok->kind == TK_LBRACE)
            array_initializer1(rest, tok, init);
        else if (!need_brace)
            array_initializer2(rest, tok, init, 0);
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
            struct_initializer2(rest, tok, init, init->ty->members);
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
    if (ty->kind == TY_NONE) {
        Token *dummy;
        Type *ty_init = assign(&dummy, tok)->ty;
        ty = type_qual(ty_init, ty->qual);
    }
    Initializer *init = new_initializer(ty, true);
    initializer2(rest, tok, init, true);
    if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->is_flexible) {
        ty = copy_type(ty);
        ty->origin = NULL;
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
        Member *mem = init->mem ? init->mem : ty->members;
        InitDesg desg2 = {desg, 0, mem, NULL};
        return create_lvar_init(init->child[mem->idx], mem->ty, &desg2, tok);
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
            for (int i = 0; i < ty->len; i++)
                if (!is_fully_initialized(init->child[i], ty->base)) return false;
            return true;
        case TY_STRUCT:
            for (Member *mem = ty->members; mem; mem = mem->next)
                if (!is_fully_initialized(init->child[mem->idx], mem->ty)) return false;
            return true;
        case TY_UNION: {
            Member *mem = init->mem ? init->mem : ty->members;
            if (!is_fully_initialized(init->child[mem->idx], mem->ty)) return false;
            return mem->ty->size == ty->size;
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
        Member *mem = init->mem ? init->mem : ty->members;
        eval_gvar_data(init->child[mem->idx], mem->ty);
        init->is_inited |= init->child[mem->idx]->is_inited;
    }

    if (init->expr) {
        uint32_t sym = 0;
        union {
            int64_t val;
            double fval;
        } u;

        if (is_fpval(ty)) {
            Fp128 v = eval_fp128(init->expr);
            Con c = {.type = CBits128};
            switch (ty->kind) {
                case TY_F16:
                    c.bits.i128.limb[0] = fp128_to_fp16_bits(v);
                    break;
                case TY_LDOUBLE:
                    if (T.ldouble_is_fp80) {
                        uint64_t m;
                        uint16_t se;
                        fp128_to_fp80_bits(v, &m, &se);
                        c.bits.i128.limb[0] = (uint32_t)m;
                        c.bits.i128.limb[1] = (uint32_t)(m >> 32);
                        c.bits.i128.limb[2] = se;
                    } else {
                        c.bits.f128 = v;
                    }
                    break;
                default: {
                    uint64_t b = fp128_to_fp64_bits(v);
                    if (ty->kind == TY_F128) {
                        c.bits.f128 = v;
                    } else {
                        c.bits.i128.limb[0] = (uint32_t)b;
                        c.bits.i128.limb[1] = (uint32_t)(b >> 32);
                    }
                    break;
                }
            }
            Ref r = newcon(&c, curm);
            init->val = &curm->con[r.val];
            init->is_inited = true;
            return;
        }

        if (is_bitint128(ty)) {
            Int128 v = eval_int128(init->expr);
            Con c = {.type = CBits128, .bits.i128 = v};
            Ref r = newcon(&c, curm);
            init->val = &curm->con[r.val];
            init->is_inited = true;
            return;
        }

        if (is_flonum(ty)) {
            uint64_t b = fp128_to_fp64_bits(eval_fp128(init->expr));
            memcpy(&u.fval, &b, 8);
        } else {
            u.val = eval2(init->expr, &sym);
        }

        Con *con = &(Con){0, sym ? CAddr : CBits, sym, {u.val}};

        Ref r = newcon(con, curm);
        r.ty = ty;
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

enum {
    BUILTIN_FN_ALLOCA = 1,
    BUILTIN_ALLOCA_WITH_ALIGN,
    BUILTIN_CONSTANT_P,
    BUILTIN_TYPES_COMPATIBLE_P,
    ATOMIC_STORE,
    ATOMIC_LOAD,
    ATOMIC_EXCHANGE,
    ATOMIC_FETCH_ADD,
    ATOMIC_FETCH_SUB,
    ATOMIC_FETCH_AND,
    ATOMIC_FETCH_OR,
    ATOMIC_FETCH_XOR,
    ATOMIC_COMPARE_EXCHANGE_WEAK,
    ATOMIC_COMPARE_EXCHANGE_STRONG,
    ATOMIC_THREAD_FENCE,
    ATOMIC_SIGNAL_FENCE,
    ATOMIC_IS_LOCK_FREE,
};

// Interned once at the top of parse(): the anonymous name for
// compiler-generated temporaries and __func__/__FUNCTION__.
static uint32_t id_anon;
static uint32_t id_func;
static uint32_t id_function;

static struct {
    char *name;
    uint32_t id;
    int kind;
} builtin_fn[] = {
    {"__builtin_alloca", 0, BUILTIN_FN_ALLOCA},
    {"__builtin_alloca_with_align", 0, BUILTIN_ALLOCA_WITH_ALIGN},
    {"__builtin_constant_p", 0, BUILTIN_CONSTANT_P},
    {"__builtin_types_compatible_p", 0, BUILTIN_TYPES_COMPATIBLE_P},
    {"__c11_atomic_store", 0, ATOMIC_STORE},
    {"__c11_atomic_load", 0, ATOMIC_LOAD},
    {"__c11_atomic_exchange", 0, ATOMIC_EXCHANGE},
    {"__c11_atomic_fetch_add", 0, ATOMIC_FETCH_ADD},
    {"__c11_atomic_fetch_sub", 0, ATOMIC_FETCH_SUB},
    {"__c11_atomic_fetch_and", 0, ATOMIC_FETCH_AND},
    {"__c11_atomic_fetch_or", 0, ATOMIC_FETCH_OR},
    {"__c11_atomic_fetch_xor", 0, ATOMIC_FETCH_XOR},
    {"__c11_atomic_compare_exchange_weak", 0, ATOMIC_COMPARE_EXCHANGE_WEAK},
    {"__c11_atomic_compare_exchange_strong", 0, ATOMIC_COMPARE_EXCHANGE_STRONG},
    {"__c11_atomic_thread_fence", 0, ATOMIC_THREAD_FENCE},
    {"__c11_atomic_signal_fence", 0, ATOMIC_SIGNAL_FENCE},
    {"__c11_atomic_is_lock_free", 0, ATOMIC_IS_LOCK_FREE},
};

// The table ids are interned lazily on first use: the preprocessor
// calls this for __has_builtin before parse() runs.
int is_builtin_fn(uint32_t id) {
    if (!builtin_fn[0].id) {
        for (size_t i = 0; i < sizeof(builtin_fn) / sizeof(builtin_fn[0]); ++i)
            builtin_fn[i].id = intern(builtin_fn[i].name, strlen(builtin_fn[i].name));
    }
    for (size_t i = 0; i < sizeof(builtin_fn) / sizeof(builtin_fn[0]); ++i)
        if (id == builtin_fn[i].id) return builtin_fn[i].kind;
    return 0;
}

static bool is_const_expr(Node *node) {
    node = fold_node(node);
    return node->kind == ND_NUM;
}

// C11 7.17.3: each operation accepts a subset of the six orders. Like
// clang, warn on any other value and fall back to a sane default.
enum {
    MO_STORE,
    MO_LOAD,
    MO_RMW,        // cmpxchg success: any of the six orders
    MO_CAS_FAIL,   // cmpxchg failure: no release/acq_rel
    MO_ATOMICRMW,  // single-order RMW (exchange, fetch_*, fences): any of the six orders
};

static int check_mem_order(Token *tok, int order, int mode) {
    bool ok;
    switch (mode) {
        case MO_STORE:
            ok = order == MEM_ORDER_RELAXED || order == MEM_ORDER_RELEASE || order == MEM_ORDER_SEQ_CST;
            break;
        case MO_LOAD:
        case MO_CAS_FAIL:
            ok = order == MEM_ORDER_RELAXED || order == MEM_ORDER_CONSUME || order == MEM_ORDER_ACQUIRE ||
                 order == MEM_ORDER_SEQ_CST;
            break;
        default:  // MO_RMW / MO_ATOMICRMW
            ok = MEM_ORDER_RELAXED <= order && order <= MEM_ORDER_SEQ_CST;
            break;
    }
    if (ok) return order;
    if (mode == MO_RMW)
        warning(tok, "success memory order argument to atomic operation is invalid");
    else if (mode == MO_CAS_FAIL)
        warning(tok, "failure memory order argument to atomic operation is invalid");
    else
        warning(tok, "memory order argument to atomic operation is invalid");
    return mode == MO_CAS_FAIL ? MEM_ORDER_RELAXED : MEM_ORDER_SEQ_CST;
}

// atomicrmw op for each exchange/fetch builtin; fetch_add/sub on
// floating types use fadd/fsub instead (adjusted at the call site).
static int armw_op_of[] = {
    [ATOMIC_EXCHANGE] = A_XCHG, [ATOMIC_FETCH_ADD] = A_ADD, [ATOMIC_FETCH_SUB] = A_SUB,
    [ATOMIC_FETCH_AND] = A_AND, [ATOMIC_FETCH_OR] = A_OR,   [ATOMIC_FETCH_XOR] = A_XOR,
};

// Parse the object argument of an atomic builtin: any expression of
// pointer-to-_Atomic type.
static Node *atomic_object(Token **tok, Token *start) {
    Node *object = assign(tok, *tok);
    if (!is_pointer(object->ty) || (object->ty->base->qual & Q_ATOMIC) == 0)
        error(start, "address argument to atomic operation must be a pointer to _Atomic type");
    return object;
}

// Parse a memory-order argument: a constant expression validated
// against the orders the operation allows.
static int atomic_order(Token **tok, int mode) {
    Token *order_tok = *tok;
    int order = const_expr(tok, *tok);
    return check_mem_order(order_tok, order, mode);
}

// Assign value into a fresh anonymous temp; builtin arguments are
// evaluated exactly once through such temps.
static Node *temp_assign(Sym *temp, Node *value, Token *tok) {
    return new_binary(ND_AS, new_var_node(temp, tok), value, tok);
}

// Sequence: run operand_init (stores the argument temp), perform the
// atomic operation, and yield the result temp's value.
static Node *atomic_result(Node *operand_init, Node *op_assign, Sym *result, Token *tok) {
    Node *seq = new_binary(ND_COMMA, operand_init, op_assign, tok);
    return new_binary(ND_COMMA, seq, new_var_node(result, tok), tok);
}

// Builtins lower to dedicated node kinds (ND_ATOMICRMW, ND_CAS,
// ND_ALLOCA, ...) carrying the memory order; arguments are parsed with
// the normal expression grammar and stored into anonymous temps so they
// are evaluated exactly once before the atomic operation.
static Node *parse_builtin_fn(Token **rest, Token *tok, int kind) {
    Token *start = tok;
    bool is_weak = false;
    bool is_signal = false;
    switch (kind) {
        case BUILTIN_TYPES_COMPATIBLE_P: {
            tok = skip(tok->next, TK_LPAREN);
            Type *type1 = typename(&tok, tok);
            tok = skip(tok, TK_COMMA);
            Type *type2 = typename(&tok, tok);
            *rest = skip(tok, TK_RPAREN);
            return new_num(is_compatible(type_unqual(type1), type_unqual(type2)), start);
        }
        case BUILTIN_CONSTANT_P: {
            tok = skip(tok->next, TK_LPAREN);
            Node *operand = assign(&tok, tok);
            *rest = skip(tok, TK_RPAREN);
            return new_num(is_const_expr(operand), start);
        }
        case BUILTIN_FN_ALLOCA:
        case BUILTIN_ALLOCA_WITH_ALIGN: {
            if (tok->next->kind != TK_LPAREN) error(tok, "builtin functions must be directly called");
            tok = skip(tok->next, TK_LPAREN);
            Node *size = assign(&tok, tok);
            lvalue_convert(&size);
            new_imcast(&size, T.ty_ulong);
            int64_t align = 16;  // Plain alloca: 16 bytes, like clang.
            if (kind == BUILTIN_ALLOCA_WITH_ALIGN) {
                tok = skip(tok, TK_COMMA);
                Token *align_tok = tok;
                Node *align_arg = assign(&tok, tok);
                if (!is_integer(align_arg->ty))
                    error(align_tok, "argument to ‘__builtin_alloca_with_align’ must be a constant integer");
                fold_node(align_arg);
                if (align_arg->kind != ND_NUM)
                    error(align_tok, "argument to ‘__builtin_alloca_with_align’ must be a constant integer");
                int64_t align_bits = int128_to_i64(align_arg->ival);
                if (align_bits & (align_bits - 1))
                    error(align_tok, "requested alignment ‘%ld’ is not a positive power of 2", align_bits);
                if (align_bits < 8) error(align_tok, "requested alignment must be 8 or greater");
                align = align_bits / 8;
            }
            *rest = skip(tok, TK_RPAREN);
            Node *alloc = new_node(ND_ALLOCA, tok);
            alloc->lhs = size;
            alloc->rhs = new_ulong(align, tok);
            return alloc;
        }
        case ATOMIC_STORE: {
            // temp = desired; *object = temp with the given order.
            tok = skip(tok->next, TK_LPAREN);
            Node *object = atomic_object(&tok, start);
            tok = skip(tok, TK_COMMA);
            Node *desired = assign(&tok, tok);
            tok = skip(tok, TK_COMMA);
            int order = atomic_order(&tok, MO_STORE);
            *rest = skip(tok, TK_RPAREN);

            Sym *desired_sym = new_lvar(id_anon, type_unqual(object->ty->base));
            Node *desired_init = new_binary(ND_INIT, new_var_node(desired_sym, start), desired, tok);
            Node *target = new_unary(ND_DEREF, object, tok);
            Node *store = new_binary(ND_AS, target, new_var_node(desired_sym, start), tok);
            store->mem_order = order + 1;
            return new_binary(ND_COMMA, desired_init, store, tok);
        }
        case ATOMIC_LOAD: {
            // temp = *object with the given order; yield temp.
            tok = skip(tok->next, TK_LPAREN);
            Node *object = atomic_object(&tok, start);
            tok = skip(tok, TK_COMMA);
            int order = atomic_order(&tok, MO_LOAD);
            *rest = skip(tok, TK_RPAREN);

            Sym *result_sym = new_lvar(id_anon, type_unqual(object->ty->base));
            Node *src = new_unary(ND_DEREF, object, tok);
            src->mem_order = order + 1;
            Node *load = new_binary(ND_AS, new_var_node(result_sym, start), src, tok);
            return new_binary(ND_COMMA, load, new_var_node(result_sym, start), tok);
        }
        case ATOMIC_EXCHANGE:
        case ATOMIC_FETCH_ADD:
        case ATOMIC_FETCH_SUB:
        case ATOMIC_FETCH_AND:
        case ATOMIC_FETCH_OR:
        case ATOMIC_FETCH_XOR: {
            // operand = temp (converted); result = temp;
            // result = atomicrmw(object, operand).
            tok = skip(tok->next, TK_LPAREN);
            Node *object = atomic_object(&tok, start);
            Type *value_ty = type_unqual(object->ty->base);
            bool is_addsub = kind == ATOMIC_FETCH_ADD || kind == ATOMIC_FETCH_SUB;
            if (kind != ATOMIC_EXCHANGE) {
                // C11 7.17.7.5: fetch_* apply to atomic integer types;
                // like clang, also accept pointer and floating types for
                // fetch_add/sub (fadd/fsub).
                bool ok = is_addsub ? (is_integer(value_ty) || is_pointer(value_ty) || is_flonum(value_ty))
                                    : is_integer(value_ty);
                if (!ok) error(object->tok, "address argument to atomic operation must be a pointer to atomic integer");
            }
            // atomicrmw add/sub on a pointer takes a pointer-sized
            // integer operand; LLVM's pointer atomicrmw operates on raw
            // integers, so scale by the element size like clang does.
            Type *operand_ty = is_addsub && is_pointer(value_ty) ? T.ty_long : value_ty;
            tok = skip(tok, TK_COMMA);
            Node *operand = assign(&tok, tok);
            lvalue_convert(&operand);
            new_imcast(&operand, operand_ty);
            if (is_addsub && is_pointer(value_ty))
                operand = new_binary(ND_MUL, operand, new_num(value_ty->base->size, operand->tok), operand->tok);
            tok = skip(tok, TK_COMMA);
            int order = atomic_order(&tok, MO_ATOMICRMW);
            *rest = skip(tok, TK_RPAREN);

            int aop = armw_op_of[kind];
            if (is_flonum(value_ty) && (aop == A_ADD || aop == A_SUB)) aop = aop == A_ADD ? A_FADD : A_FSUB;

            Sym *operand_sym = new_lvar(id_anon, operand_ty);
            Node *operand_init = temp_assign(operand_sym, operand, tok);

            Sym *result_sym = new_lvar(id_anon, value_ty);
            Node *rmw = new_node(ND_ATOMICRMW, start);
            rmw->lhs = object;
            rmw->desired = new_var_node(operand_sym, start);
            rmw->mem_order = order + 1;
            rmw->armw_op = aop;
            add_type(rmw);
            Node *rmw_assign = temp_assign(result_sym, rmw, tok);
            return atomic_result(operand_init, rmw_assign, result_sym, tok);
        }
        case ATOMIC_COMPARE_EXCHANGE_WEAK:
            is_weak = true;
        // fall through
        case ATOMIC_COMPARE_EXCHANGE_STRONG: {
            // desired = temp; result = temp (bool);
            // result = cas(object, &expected, desired).
            tok = skip(tok->next, TK_LPAREN);
            Node *object = atomic_object(&tok, start);
            tok = skip(tok, TK_COMMA);
            Node *expected = assign(&tok, tok);
            if (!is_pointer(expected->ty) || (expected->ty->base->qual & Q_ATOMIC))
                error(expected->tok, "second argument to atomic operation must be a pointer to non-atomic type");
            Type *value_ty = type_unqual(object->ty->base);
            if (!is_compatible(type_unqual(expected->ty->base), value_ty))
                error(expected->tok, "second argument to atomic operation must be a pointer to the same type");
            tok = skip(tok, TK_COMMA);
            Node *desired = assign(&tok, tok);
            lvalue_convert(&desired);
            new_imcast(&desired, value_ty);
            tok = skip(tok, TK_COMMA);
            int success_order = atomic_order(&tok, MO_RMW);
            tok = skip(tok, TK_COMMA);
            int failure_order = atomic_order(&tok, MO_CAS_FAIL);
            *rest = skip(tok, TK_RPAREN);

            Sym *desired_sym = new_lvar(id_anon, value_ty);
            Node *desired_init = temp_assign(desired_sym, desired, tok);

            Sym *result_sym = new_lvar(id_anon, T.ty_bool);
            Node *cas = new_node(ND_CAS, start);
            cas->lhs = object;
            cas->rhs = expected;
            cas->desired = new_var_node(desired_sym, start);
            cas->mem_order = success_order + 1;
            cas->mem_order1 = failure_order + 1;
            cas->is_weak = is_weak;
            add_type(cas);
            Node *cas_assign = temp_assign(result_sym, cas, tok);
            cnt_blk(2);  // ND_CAS: failure + merge blocks
            return atomic_result(desired_init, cas_assign, result_sym, tok);
        }
        case ATOMIC_SIGNAL_FENCE:
            is_signal = true;
        // fall through
        case ATOMIC_THREAD_FENCE: {
            Node *fence = new_node(ND_FENCE, tok);
            tok = skip(tok->next, TK_LPAREN);
            int order = atomic_order(&tok, MO_ATOMICRMW);
            *rest = skip(tok, TK_RPAREN);
            fence->mem_order = order + 1;
            fence->is_signal = is_signal;
            fence->ty = T.ty_void;
            return fence;
        }
        case ATOMIC_IS_LOCK_FREE: {
            // Every target provides native atomics up to the pointer
            // size (riscv32's 8-byte atomics use libcalls).
            tok = skip(tok->next, TK_LPAREN);
            int64_t size = const_expr(&tok, tok);
            *rest = skip(tok, TK_RPAREN);
            return new_num(size <= T.ty_nullptr->size, start);
        }
    }
    return NULL;
}

// The plain binary node kind for each compound-assign node kind.
static NodeKind as_binop[] = {
    [ND_ADDAS] = ND_ADD,  [ND_SUBAS] = ND_SUB, [ND_MULAS] = ND_MUL, [ND_DIVAS] = ND_DIV,   [ND_MODAS] = ND_MOD,
    [ND_ANDAS] = ND_BAND, [ND_ORAS] = ND_BOR,  [ND_XORAS] = ND_XOR, [ND_LEFTAS] = ND_LEFT, [ND_RIGHTAS] = ND_RIGHT,
};

// Lower an atomic compound assignment or ++/-- (C11 6.5.16.2p3: the
// operation is a single atomic evaluation) into a statement expression.
// Integer +,-,&,|,^ use atomicrmw with a local recompute of the new
// value (like clang); *,/,%,<<,>> and pointers use a CAS loop. The
// postfix ++/-- value is the old value, prefix/compound the new one.
static Node *atomic_compound_assign(Node *lhs, NodeKind op, Node *rhs, bool postfix, Token *tok) {
    Node wrapper = {.lhs = lhs, .tok = tok};
    modifiable_lvalue(&wrapper);

    Type *at = lhs->ty;
    Type *t = type_unqual(at);
    bool is_ptr = is_pointer(t);

    if (is_flonum(t) && (op == ND_MULAS || op == ND_DIVAS))
        error(tok, "atomic compound assignment with '%s' on a floating type is not supported",
              op == ND_MULAS ? "*" : "/");

    // Type the rhs exactly like the plain compound assignment would and
    // steal the converted operand and the common type. The scratch lhs
    // is unqualified, so get_common_type sees the same kinds.
    Sym *scratch = new_lvar(id_anon, t);
    Node *bin = new_binary(op, new_var_node(scratch, tok), rhs, tok);
    add_type(bin);
    rhs = bin->rhs;
    Type *val_ty = is_ptr ? T.ty_long : bin->compute_ty;

    bool armw = !is_ptr && op != ND_MULAS && op != ND_DIVAS && op != ND_MODAS && op != ND_LEFTAS && op != ND_RIGHTAS;

    Sym *v_addr = new_lvar(id_anon, pointer_to(at, 0));
    Sym *v_val = new_lvar(id_anon, val_ty);
    Sym *v_old = new_lvar(id_anon, t);
    Sym *v_new = new_lvar(id_anon, t);
    Sym *v_r = new_lvar(id_anon, t);
    Sym *v_res = new_lvar(id_anon, t);

    Node *stmt = new_node(ND_STMT_EXPR, tok);
    Node dummy, *cur = &dummy;

    // addr = &A: the address expression is evaluated once.
    Node *as = new_binary(ND_AS, new_var_node(v_addr, tok), new_unary(ND_ADDR, lhs, tok), tok);
    add_type(as);
    cur = cur->next = new_unary(ND_EXPR_STMT, as, tok);

    // val = B: the operand is evaluated once.
    as = new_binary(ND_AS, new_var_node(v_val, tok), rhs, tok);
    add_type(as);
    cur = cur->next = new_unary(ND_EXPR_STMT, as, tok);

    if (armw) {
        Node *desired = new_var_node(v_val, tok);
        add_type(desired);
        lvalue_convert(&desired);
        new_imcast(&desired, t);
        Node *arm = new_node(ND_ATOMICRMW, tok);
        arm->lhs = new_var_node(v_addr, tok);
        arm->desired = desired;
        if (is_flonum(t)) {
            arm->armw_op = op == ND_ADDAS ? A_FADD : A_FSUB;
        } else {
            switch (op) {
                case ND_ADDAS:
                    arm->armw_op = A_ADD;
                    break;
                case ND_SUBAS:
                    arm->armw_op = A_SUB;
                    break;
                case ND_ANDAS:
                    arm->armw_op = A_AND;
                    break;
                case ND_ORAS:
                    arm->armw_op = A_OR;
                    break;
                default:
                    arm->armw_op = A_XOR;
                    break;
            }
        }
        add_type(arm);
        as = new_binary(ND_AS, new_var_node(v_r, tok), arm, tok);
        add_type(as);
        cur = cur->next = new_unary(ND_EXPR_STMT, as, tok);

        Sym *value_var = v_r;
        if (!postfix) {
            // new = (T1)(old op val), recomputed locally from the
            // atomicrmw result (the old value).
            Node *rb = new_binary(as_binop[op], new_var_node(v_r, tok), new_var_node(v_val, tok), tok);
            add_type(rb);
            new_imcast(&rb, t);
            as = new_binary(ND_AS, new_var_node(v_res, tok), rb, tok);
            add_type(as);
            cur = cur->next = new_unary(ND_EXPR_STMT, as, tok);
            value_var = v_res;
        }
        cur = cur->next = new_unary(ND_EXPR_STMT, new_var_node(value_var, tok), tok);
        add_type(cur);
    } else {
        // old = *addr (a seq_cst atomic load).
        as = new_binary(ND_AS, new_var_node(v_old, tok), new_unary(ND_DEREF, new_var_node(v_addr, tok), tok), tok);
        add_type(as);
        cur = cur->next = new_unary(ND_EXPR_STMT, as, tok);

        // do { new = (T1)(old op val); } while (!cas(addr, &old, new));
        Node *rb = is_ptr ? new_binary(ND_PTRADD, new_var_node(v_old, tok), new_var_node(v_val, tok), tok)
                          : new_binary(as_binop[op], new_var_node(v_old, tok), new_var_node(v_val, tok), tok);
        add_type(rb);
        new_imcast(&rb, t);
        as = new_binary(ND_AS, new_var_node(v_new, tok), rb, tok);
        add_type(as);
        Node *body = new_unary(ND_EXPR_STMT, as, tok);

        Node *cas = new_node(ND_CAS, tok);
        cas->lhs = new_var_node(v_addr, tok);
        cas->rhs = new_unary(ND_ADDR, new_var_node(v_old, tok), tok);
        cas->desired = new_var_node(v_new, tok);
        add_type(cas);
        Node *cond = new_unary(ND_NOT, cas, tok);
        add_type(cond);
        Node *loop = new_node(ND_DO, tok);
        loop->body = body;
        loop->cond = cond;
        cnt_blk(5);  // gen_do: 3 blocks + ND_CAS: 2 blocks
        cur = cur->next = loop;

        cur = cur->next = new_unary(ND_EXPR_STMT, new_var_node(postfix ? v_old : v_new, tok), tok);
        add_type(cur);
    }

    stmt->body = dummy.next;
    stmt->ty = t;
    return stmt;
}

typedef struct {
    Type **generic_ty;
    Token **generic_tok;
    int generic_num;
} Generic_s;

static void push_generic(Type *ty, Token *tok, Generic_s *gen) {
    for (int i = 0; i < gen->generic_num; i++)
        if (is_compatible(ty, gen->generic_ty[i])) {
            diag("error", tok, "‘_Generic’ specifies two compatible types");
            diag_exit("note", gen->generic_tok[i], "compatible type is here");
        }
    gen->generic_ty = vgrow(gen->generic_ty, gen->generic_num + 1);
    gen->generic_tok = vgrow(gen->generic_tok, gen->generic_num + 1);
    gen->generic_ty[gen->generic_num] = ty;
    gen->generic_tok[gen->generic_num] = tok;
    gen->generic_num++;
}

// GenSel   ::= "_Generic" "(" (AsExp | TypeName) "," GenAssoc ("," GenAssoc)* ")"
// GenAssoc ::= TypeName ":" AsExp | "default" ":" AsExp
static Node *generic_selection(Token **rest, Token *tok) {
    Token *start = tok = skip(tok->next, TK_LPAREN);

    Type *t1;
    if (is_typename(tok, true)) {
        t1 = typename(&tok, tok);
    } else {
        Node *expr = assign(&tok, tok);
        lvalue_convert(&expr);
        t1 = expr->ty;
    }

    if (t1->kind == TY_FUNC)
        t1 = pointer_to(t1, 0);
    else if (t1->kind == TY_ARRAY || t1->kind == TY_VLA)
        t1 = pointer_to(t1->base, 0);

    Generic_s dummy, *gen = &dummy;
    gen->generic_ty = vnew(16, sizeof(Type *));
    gen->generic_tok = vnew(16, sizeof(Token *));
    gen->generic_num = 0;

    Node *ret_expr, *default_expr;
    Token *ret_tok = NULL, *default_tok = NULL;
    // Accounting state before the default association; rolled back if a
    // type association is selected instead.
    int def_blk = 0, def_lbl = 0;
    Node *def_labels = NULL, *def_gotos = NULL;

    while (!match(rest, tok, TK_RPAREN)) {
        Token *as_tok = tok = skip(tok, TK_COMMA);

        if (tok->kind == TK_DEFAULT) {
            if (default_tok) {
                diag("error", tok, "duplicate ‘default’ case in ‘_Generic’");
                diag_exit("note", default_tok, "original ‘default’ is here");
            }
            default_tok = tok;
            tok = skip(tok->next, TK_COLON);
            def_blk = cur_fn ? cur_fn->num_blk : 0;
            def_lbl = cur_fn ? cur_fn->num_lbl : 0;
            def_labels = labels;
            def_gotos = gotos;
            default_expr = assign(&tok, tok);
            continue;
        }

        Type *t2 = typename(&tok, tok);
        tok = skip(tok, TK_COLON);

        // Unselected associations are discarded after parsing: their
        // block/label accounting must not leak into the function.
        int saved_blk = cur_fn ? cur_fn->num_blk : 0;
        int saved_lbl = cur_fn ? cur_fn->num_lbl : 0;
        Node *saved_labels = labels;
        Node *saved_gotos = gotos;

        Node *node = assign(&tok, tok);
        push_generic(t2, as_tok, gen);
        if (is_compatible(t1, t2)) {
            if (ret_tok) {
                diag("error", as_tok, "‘_Generic’ selector matches multiple associations");
                diag_exit("note", ret_tok, "other match is here");
            }
            ret_tok = as_tok;
            ret_expr = node;
        } else {
            if (cur_fn) {
                cur_fn->num_blk = saved_blk;
                cur_fn->num_lbl = saved_lbl;
            }
            labels = saved_labels;
            gotos = saved_gotos;
        }
    }

    if (ret_tok) {
        if (default_tok) {
            // the default association is discarded
            if (cur_fn) {
                cur_fn->num_blk = def_blk;
                cur_fn->num_lbl = def_lbl;
            }
            labels = def_labels;
            gotos = def_gotos;
        }
        return ret_expr;
    }
    if (default_tok) return default_expr;
    error(start, "‘_Generic’ selector is not compatible with any association");
    return NULL;
}

// PrimExp     ::= "true" | "false" | "nullptr"
// | Num | Str | Ident
// | "(" Exp ")" | "(" CompStmt ")"
// | GenSel
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
        node->ty = T.ty_bool;
        *rest = tok->next;
        return node;
    }
    if (tok->kind == TK_FALSE) {
        node = new_num(0, tok);
        node->ty = T.ty_bool;
        *rest = tok->next;
        return node;
    }
    if (tok->kind == TK_NULLPTR) {
        node = new_node(ND_NULLPTR, tok);
        node->ty = T.ty_nullptr;
        *rest = tok->next;
        return node;
    }
    if (tok->kind == TK_NUM) {
        node = new_num_node(tok);
        *rest = tok->next;
        return node;
    }
    if (tok->kind == TK_CHARLIT) {
        node = new_num(int128_to_i64(tok->ival), tok);
        node->ty = infer_chartype(tok);
        *rest = tok->next;
        return node;
    }
    if (tok->kind == TK_STRLIT) {
        Sym *var = new_string_literal(tok->id, infer_strtype(tok));
        *rest = tok->next;
        return new_var_node(var, tok);
    }
    if (tok->kind == TK_GENERIC) {
        return generic_selection(rest, tok);
    }
    if (tok->kind == TK_IDENT) {
        // builtin_fnuction
        int builtin_fn_kind = is_builtin_fn(tok->id);
        if (builtin_fn_kind) {
            Node *node = parse_builtin_fn(rest, tok, builtin_fn_kind);
            if (node) return node;
        }
        // Variable, function or enum constant
        NameSpace *sc = find_ident(tok, true, false);
        if (!sc) {
            if (tok->next->kind == TK_LPAREN)
                error(tok, "implicit declaration of function ‘%s’", str(tok->id));
            else
                error(tok, "use of undeclared identifier ‘%s’", str(tok->id));
        }
        while (sc->prev) sc = sc->prev;
        if (sc->kind == SYM_TYNAME) error(tok, "unexpected type name ‘%s’: expected expression", str(tok->id));
        if (sc->kind == SYM_ENUM)
            node = new_num(sc->enum_val, tok);
        else
            node = new_var_node(sc->var, tok);
        *rest = tok->next;
        return node;
    }
    error(tok, "expected expression before ‘%.*s’", tok->len, tok_text(tok));
    return NULL;
}

static Node *fncall(Token **rest, Token *tok, Node *fn) {
    if (fn->ty->kind != TY_FUNC && !is_funcptr(fn->ty))
        error(tok, "called object ‘%.*s’ is not a function or function pointer", fn->tok->len, tok_text(fn->tok));

    Node *node = new_node(ND_FUNCALL, tok);
    lvalue_convert(&fn);
    node->func = fn;

    Type *ty = (fn->ty->kind == TY_FUNC) ? fn->ty : fn->ty->base;
    Type *param_ty = ty->params;
    node->ty = ty->ret;

    tok = tok->next;

    if (tok->kind == TK_RPAREN) {
        if (param_ty)
            error(tok, "too few arguments to function ‘%.*s’; expected %d", ty->name->len, tok_text(ty->name),
                  ty->nparam);
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
            // Default argument promotions (6.5.2.2p7): the integer
            // promotions apply to the standard integer types but never
            // to _BitInt; float and _Float32 promote to double;
            // _Float16 and _Float64 stay as they are (gcc/clang both
            // keep _Float16; clang promotes _Float32).
            if (is_integer(arg->ty)) integer_promotion(&arg);
            if (arg->ty->kind == TY_FLOAT || arg->ty->kind == TY_F32) new_imcast(&arg, T.ty_double);
            lvalue_convert(&arg);
        } else {
            error(tok, "too many arguments to function ‘%.*s’; expected %d", ty->name->len, tok_text(ty->name),
                  ty->nparam);
        }
        ++i;
        cur = cur->next = arg;
    } while (match(&tok, tok, TK_COMMA));

    if (param_ty)
        error(tok, "too few arguments to function ‘%.*s’; expected %d", ty->name->len, tok_text(ty->name), ty->nparam);

    *rest = skip(tok, TK_RPAREN);

    node->args = dummy.next;
    node->narg = i;
    return node;
}

// Find a struct member by name.
static Member *get_struct_member(Member *mem, Token *tok) {
    for (; mem; mem = mem->next) {
        // Anonymous struct member
        if (!mem->name) {
            if (!is_record(mem->ty)) continue;
            if (get_struct_member(mem->ty->members, tok)) return mem;
            continue;
        }

        // Regular struct member
        if (mem->name->id == tok->id) return mem;
    }
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
            sclass |= sc_table[tok->kind];
            tok = tok->next;
        }
        Type *ty = typename(&tok, tok);
        tok = skip(tok, TK_RPAREN);
        Sym *var;
        if (is_file_scope() || sclass & SC_STATIC) {
            uint32_t uid = new_unique_varname(intern(".compoundliteral", 16));
            var = new_gvar(uid, ty);
            gvar_initializer(&tok, tok, var);
        } else {
            var = new_lvar(id_anon, ty);
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
        if (node->ty->kind == TY_VLA) new_imcast(&node, pointer_to(node->ty->base, 0));
        if (node->ty->kind == TY_FUNC) new_imcast(&node, pointer_to(node->ty, 0));
        switch (tok->kind) {
            case TK_LPAREN:
                // foo()
                node = fncall(&tok, tok, node);
                continue;
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
                uint32_t mem_id = get_ident(tok);

                if (ty->kind != TY_STRUCT && ty->kind != TY_UNION)
                    error(dot, "request for member ‘%s’ in something not a structure or union", str(mem_id));
                Member *mem = get_struct_member(ty->members, tok);
                if (!mem) error(tok, "no member named ‘%s’ in ‘%s’", str(tok->id), str(ty->uid));

                while (!mem->name) {
                    node = new_unary(ND_MEMBER, node, dot);
                    node->member = mem;
                    mem = get_struct_member(mem->ty->members, tok);
                }

                node = new_unary(ND_MEMBER, node, dot);
                node->member = mem;
                tok = tok->next;
                continue;
            }
            case TK_INC:
                if (node->ty->qual & Q_ATOMIC)
                    node = atomic_compound_assign(node, ND_ADDAS, new_num(1, tok), true, tok);
                else
                    node = new_unary(ND_POSTINC, node, tok);
                tok = tok->next;
                continue;
            case TK_DEC:
                if (node->ty->qual & Q_ATOMIC)
                    node = atomic_compound_assign(node, ND_SUBAS, new_num(1, tok), true, tok);
                else
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
//          | "_Countof" UnaryExp | "_Countof" "(" TypeName ")"
//          | "&&" Ident
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
            if (node->kind == ND_IMCAST && node->lhs->ty->kind == TY_VLA) node = node->lhs;
            if (node->kind == ND_IMCAST && node->lhs->ty->kind == TY_FUNC) node = node->lhs;
            return new_unary(ND_ADDR, node, tok);
        }
        case TK_STAR: {
            Node *node = new_unary(ND_DEREF, cast(rest, tok->next), tok);
            add_type(node);
            if (node->ty->kind == TY_ARRAY) new_imcast(&node, pointer_to(node->ty->base, 0));
            if (node->ty->kind == TY_VLA) new_imcast(&node, pointer_to(node->ty->base, 0));
            if (node->ty->kind == TY_FUNC) new_imcast(&node, pointer_to(node->ty, 0));
            return node;
        }
        case TK_INC: {
            Node *operand = unary(rest, tok->next);
            if (operand->ty->qual & Q_ATOMIC)
                return atomic_compound_assign(operand, ND_ADDAS, new_num(1, tok), false, tok);
            return new_unary(ND_PREINC, operand, tok);
        }
        case TK_DEC: {
            Node *operand = unary(rest, tok->next);
            if (operand->ty->qual & Q_ATOMIC)
                return atomic_compound_assign(operand, ND_SUBAS, new_num(1, tok), false, tok);
            return new_unary(ND_PREDEC, operand, tok);
        }
        case TK_ALIGNOF:
        case TK_COUNTOF:
        case TK_SIZEOF: {
            Token *start = tok;
            Type *ty;
            bool tyname = false;
            if (tok->next->kind == TK_LPAREN && is_typename(tok->next->next, true)) {
                scope->vla_num = 0;
                ty = typename(&tok, tok->next->next);
                *rest = skip(tok, TK_RPAREN);
                tyname = true;
            } else {
                Node *node = unary(rest, tok->next);
                add_type(node);
                if (node->kind == ND_IMCAST && node->lhs->ty->kind == TY_ARRAY)
                    node = node->lhs;
                else if (node->kind == ND_IMCAST && node->lhs->ty->kind == TY_VLA)
                    node = node->lhs;
                else if (node->kind == ND_IMCAST && node->lhs->ty->kind == TY_FUNC)
                    node = node->lhs;
                ty = node->ty;
            }
            if (ty->size < 0 && (ty->kind != TY_ARRAY && ty->kind != TY_VLA)) {
                error(start, "invalid application of ‘%*.s’ to incomplete type", start->len, tok_text(start));
            }
            if (start->kind == TK_ALIGNOF) return new_ulong(ty->align, start);
            Node *size = NULL;
            if (tyname) {
                size = new_node(ND_NOP, tok);
                for (int i = 0; i < scope->vla_num; i++) {
                    size = new_binary(ND_COMMA, size, scope->vla_expr[i], tok);
                }
            }
            scope->vla_num = 0;
            if (start->kind == TK_COUNTOF) {
                if (ty->kind != TY_ARRAY && ty->kind != TY_VLA)
                    error(start, "‘_Countof’ requires an argument of array type");
                if (ty->kind == TY_VLA) {
                    return new_binary(ND_COMMA, size, new_var_node(ty->vla_cnt, start), start);
                }
                if (ty->size < 0) error(start, "invalid application of ‘_Countof’ to incomplete type");
                return new_ulong(ty->len, start);
            }
            if (ty->kind == TY_VLA) {
                Node *vla_len = new_var_node(ty->vla_cnt, start);
                Type *base_ty = ty->base;
                while (base_ty->kind == TY_VLA) {
                    vla_len = new_binary(ND_MUL, vla_len, new_var_node(base_ty->vla_cnt, start), start);
                    base_ty = base_ty->base;
                }
                Node *base_sz = new_ulong(base_ty->size, start);
                return new_binary(ND_COMMA, size, new_binary(ND_MUL, vla_len, base_sz, start), start);
            }
            if (ty->size < 0) error(start, "invalid application of ‘sizeof’ to incomplete type");
            return new_ulong(ty->size, start);
        }
        // [GNU] labels-as-values
        case TK_AND: {
            Node *node = new_node(ND_LABEL_VAL, tok);
            node->label = get_ident(tok->next);

            node->goto_next = gotos;
            gotos = node;

            *rest = tok->next->next;
            return node;
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
            sclass |= sc_table[tok->kind];
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
    static int op_table[TK_NKIND][2] = {
        [TK_OR] = {20, ND_LOGOR},    [TK_AND] = {30, ND_LOGAND}, [TK_BOR] = {40, ND_BOR},    [TK_XOR] = {50, ND_XOR},
        [TK_BAND] = {60, ND_BAND},   [TK_EQ] = {70, ND_EQ},      [TK_NE] = {70, ND_NE},      [TK_LT] = {80, ND_LT},
        [TK_GT] = {80, ND_GT},       [TK_LE] = {80, ND_LE},      [TK_GE] = {80, ND_GE},      [TK_LEFT] = {90, ND_LEFT},
        [TK_RIGHT] = {90, ND_RIGHT}, [TK_PLUS] = {100, ND_ADD},  [TK_MINUS] = {100, ND_SUB}, [TK_STAR] = {110, ND_MUL},
        [TK_SLASH] = {110, ND_DIV},  [TK_MOD] = {110, ND_MOD},
    };

    Node *lhs = cast(&tok, tok);
    add_type(lhs);

    // Precedence 0 marks every non-operator kind; min_prec is never negative.
    while (op_table[tok->kind][0] > min_prec) {
        Token *op_tok = tok;
        int cur_prec = op_table[op_tok->kind][0];
        NodeKind expr_op = op_table[op_tok->kind][1];

        Node *rhs = binexpr(&tok, tok->next, cur_prec);
        add_type(rhs);

        if (expr_op == ND_ADD)
            lhs = new_add(lhs, rhs, op_tok);
        else if (expr_op == ND_SUB)
            lhs = new_sub(lhs, rhs, op_tok);
        else
            lhs = new_binary(expr_op, lhs, rhs, op_tok);
        if (expr_op == ND_LOGOR || expr_op == ND_LOGAND) cnt_blk(2);  // gen_logor/gen_logand

        add_type(lhs);
    }
    *rest = tok;
    return lhs;
}

// CondExp ::= LOrExp ("?" Exp? ":" CondExp)?
static Node *conditional(Token **rest, Token *tok) {
    Node *cond = binexpr(&tok, tok, 0);

    if (tok->kind != TK_QUESTION) {
        *rest = tok;
        return cond;
    }

    if (tok->next->kind == TK_COLON) {
        // [GNU] Compile `a ?: b` as `tmp = a, tmp ? tmp : b`.
        // Omitting the middle operand uses the value already computed
        // without the undesirable effects of recomputing it
        Sym *var = new_lvar(id_anon, cond->ty);
        Node *lhs = new_binary(ND_AS, new_var_node(var, tok), cond, tok);
        Node *rhs = new_node(ND_COND, tok);
        rhs->cond = new_var_node(var, tok);
        rhs->then = new_var_node(var, tok);
        rhs->els = conditional(rest, tok->next->next);
        cnt_blk(3);  // gen_cond: then / else / merge
        return new_binary(ND_COMMA, lhs, rhs, tok);
    }

    Node *node = new_node(ND_COND, tok);
    node->cond = cond;
    node->then = expr(&tok, tok->next);
    tok = skip(tok, TK_COLON);
    node->els = conditional(rest, tok);
    cnt_blk(3);  // gen_cond: then / else / merge
    return node;
}

// Evaluate a given node as a constant expression.
//
// Compile-time evaluation in the Fp128 domain for every floating
// format (all float constants live in node->fpval, already rounded to
// their declared format).
static Fp128 eval_fp128(Node *node) {
    add_type(node);
    switch (node->kind) {
        case ND_NUM:
            if (is_flonum(node->ty)) return node->fpval;
            break;
        case ND_ADD:
            return fp128_add(eval_fp128(node->lhs), eval_fp128(node->rhs));
        case ND_SUB:
            return fp128_sub(eval_fp128(node->lhs), eval_fp128(node->rhs));
        case ND_MUL:
            return fp128_mul(eval_fp128(node->lhs), eval_fp128(node->rhs));
        case ND_DIV: {
            Fp128 r = eval_fp128(node->rhs);
            if (fp128_is_zero(r)) error(node->tok, "division by zero");
            return fp128_div_rounded(eval_fp128(node->lhs), r, fmt_of(node->ty));
        }
        case ND_NEG:
            return fp128_neg(eval_fp128(node->lhs));
        case ND_COND:
            // The condition may be an integer (0/1 selects the branch).
            if (is_flonum(node->cond->ty))
                return fp128_is_zero(eval_fp128(node->cond)) ? eval_fp128(node->els) : eval_fp128(node->then);
            return eval(node->cond) ? eval_fp128(node->then) : eval_fp128(node->els);
        case ND_COMMA:
            eval(node->lhs);
            return eval_fp128(node->rhs);
        case ND_IMCAST:
        case ND_EXCAST: {
            // Source may be an integer (or a float of another format);
            // round once to the target format.
            if (is_flonum(node->lhs->ty)) return fp128_round_to(eval_fp128(node->lhs), fmt_of(node->ty));
            Int128 iv = is_bitint128(node->lhs->ty)  ? eval_int128(node->lhs)
                        : node->lhs->ty->is_unsigned ? int128_set_ui((uint64_t)eval(node->lhs))
                                                     : int128_set_i(eval(node->lhs));
            return fp128_from_int128(iv, node->lhs->ty->is_unsigned ? UNSIGNED : SIGNED);
        }
        default:
            break;
    }
    error(node->tok, "not a compile-time constant");
    return (Fp128){{0, 0, 0, 0}};
}

// Compile-time evaluation in the Int128 domain for _BitInt constants
// (all widths; node->ival holds the value).
static Int128 eval_int128(Node *node) {
    add_type(node);
    switch (node->kind) {
        case ND_NUM:
            return node->ival;
        case ND_ADD:
            return int128_add(eval_int128(node->lhs), eval_int128(node->rhs));
        case ND_SUB:
            return int128_sub(eval_int128(node->lhs), eval_int128(node->rhs));
        case ND_MUL:
            return int128_mul(eval_int128(node->lhs), eval_int128(node->rhs));
        case ND_DIV: {
            Int128 r = eval_int128(node->rhs);
            if (int128_is_zero(r)) error(node->tok, "division by zero");
            return node->ty->is_unsigned ? int128_div_unsigned(eval_int128(node->lhs), r)
                                         : int128_div_signed(eval_int128(node->lhs), r);
        }
        case ND_MOD: {
            Int128 r = eval_int128(node->rhs);
            if (int128_is_zero(r)) error(node->tok, "division by zero");
            return node->ty->is_unsigned ? int128_mod_unsigned(eval_int128(node->lhs), r)
                                         : int128_mod_signed(eval_int128(node->lhs), r);
        }
        case ND_NEG:
            return int128_neg(eval_int128(node->lhs));
        case ND_INVERT:
            return int128_not(eval_int128(node->lhs));
        case ND_BAND:
            return int128_and(eval_int128(node->lhs), eval_int128(node->rhs));
        case ND_BOR:
            return int128_or(eval_int128(node->lhs), eval_int128(node->rhs));
        case ND_XOR:
            return int128_xor(eval_int128(node->lhs), eval_int128(node->rhs));
        case ND_LEFT:
            return int128_shl(eval_int128(node->lhs), (int)eval(node->rhs));
        case ND_RIGHT:
            return int128_shr(eval_int128(node->lhs), (int)eval(node->rhs), node->ty->is_unsigned ? UNSIGNED : SIGNED);
        case ND_NOT:
            return int128_set_i(int128_is_zero(eval_int128(node->lhs)));
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
        case ND_GT:
        case ND_GE: {
            Int128 l = eval_int128(node->lhs), r = eval_int128(node->rhs);
            int c = node->lhs->ty->is_unsigned ? int128_cmp_unsigned(l, r) : int128_cmp_signed(l, r);
            switch (node->kind) {
                case ND_EQ:
                    return int128_set_i(c == 0);
                case ND_NE:
                    return int128_set_i(c != 0);
                case ND_LT:
                    return int128_set_i(c < 0);
                case ND_LE:
                    return int128_set_i(c <= 0);
                case ND_GT:
                    return int128_set_i(c > 0);
                default:
                    return int128_set_i(c >= 0);
            }
        }
        case ND_LOGAND: {
            if (int128_is_zero(eval_int128(node->lhs))) return int128_set_i(0);
            return int128_set_i(!int128_is_zero(eval_int128(node->rhs)));
        }
        case ND_LOGOR: {
            if (!int128_is_zero(eval_int128(node->lhs))) return int128_set_i(1);
            return int128_set_i(!int128_is_zero(eval_int128(node->rhs)));
        }
        case ND_COND:
            return int128_is_zero(eval_int128(node->cond)) ? eval_int128(node->els) : eval_int128(node->then);
        case ND_COMMA:
            eval_int128(node->lhs);
            return eval_int128(node->rhs);
        case ND_IMCAST:
        case ND_EXCAST: {
            if (is_flonum(node->lhs->ty)) {
                bool ok;
                Int128 v = fp128_to_int128(eval_fp128(node->lhs), node->ty->is_unsigned ? UNSIGNED : SIGNED, &ok);
                if (!ok) error(node->tok, "floating constant out of range");
                return v;
            }
            if (is_bitint128(node->lhs->ty)) return eval_int128(node->lhs);
            return node->lhs->ty->is_unsigned ? int128_set_ui((uint64_t)eval(node->lhs))
                                              : int128_set_i(eval(node->lhs));
        }
        default:
            break;
    }
    error(node->tok, "not a compile-time constant");
    return (Int128){{0, 0, 0, 0}};
}

static int64_t eval_ty(int64_t val, Type *ty) {
    if (is_integer(ty)) {
        if (ty->kind & TY_BITINT) return norm_bits(val, bitint_width(ty), ty->is_unsigned);
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
    if (is_flonum(node->ty)) {
        // Every floating format evaluates in the Fp128 domain; int64
        // contexts take the double bit pattern of the result.
        return (int64_t)fp128_to_fp64_bits(eval_fp128(node));
    }
    if (is_bitint128(node->ty)) {
        // #if (and other int64 const-expr contexts) takes the value
        // truncated to 64 bits.
        return int128_to_i64(int128_normalize(eval_int128(node), 64, UNSIGNED));
    }

    switch (node->kind) {
        case ND_NUM:
            return int128_to_i64(node->ival);
        case ND_PLUS:
            return eval(node->lhs);
        case ND_NEG:
        case ND_INVERT:
        // Integer arithmetic runs in the Int128 domain; the 64-bit
        // truncation reproduces the wrap of the standard types.
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_MOD:
        case ND_BAND:
        case ND_BOR:
        case ND_XOR:
        case ND_LEFT:
        case ND_RIGHT:
            return int128_to_i64(int128_normalize(eval_int128(node), 64, UNSIGNED));
        case ND_NOT:
            return int128_is_zero(eval_int128(node->lhs)) ? 1 : 0;
        case ND_COMMA:
            eval(node->lhs);
            return eval2(node->rhs, sym);
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
        case ND_GT:
        case ND_GE: {
            if (is_flonum(node->lhs->ty)) {
                // fp128_cmp returns 2 for unordered (NaN), which must
                // compare false for every ordered predicate.
                int c = fp128_cmp(eval_fp128(node->lhs), eval_fp128(node->rhs));
                switch (node->kind) {
                    case ND_EQ:
                        return c == 0;
                    case ND_NE:
                        return c != 0;
                    case ND_LT:
                        return c == -1;
                    case ND_LE:
                        return c == -1 || c == 0;
                    case ND_GT:
                        return c == 1;
                    default:
                        return c == 0 || c == 1;
                }
            }
            if ((node->lhs->ty->kind & TY_BITINT) || (node->rhs->ty->kind & TY_BITINT)) {
                // Any _BitInt width: the int64 path would sign-interpret
                // values >= 2^63 and use the result type's signedness.
                Int128 l = eval_int128(node->lhs), r = eval_int128(node->rhs);
                bool uns = node->lhs->ty->is_unsigned;
                int c = uns ? int128_cmp_unsigned(l, r) : int128_cmp_signed(l, r);
                switch (node->kind) {
                    case ND_EQ:
                        return c == 0;
                    case ND_NE:
                        return c != 0;
                    case ND_LT:
                        return c < 0;
                    case ND_LE:
                        return c <= 0;
                    case ND_GT:
                        return c > 0;
                    default:
                        return c >= 0;
                }
            }
            if (node->kind == ND_EQ) return eval(node->lhs) == eval(node->rhs);
            if (node->kind == ND_NE) return eval(node->lhs) != eval(node->rhs);
            switch (node->kind) {
                case ND_LT:
                    return node->ty->is_unsigned ? (uint64_t)eval(node->lhs) < (uint64_t)eval(node->rhs)
                                                 : eval(node->lhs) < eval(node->rhs);
                case ND_LE:
                    return node->ty->is_unsigned ? (uint64_t)eval(node->lhs) <= (uint64_t)eval(node->rhs)
                                                 : eval(node->lhs) <= eval(node->rhs);
                case ND_GT:
                    return node->ty->is_unsigned ? (uint64_t)eval(node->lhs) > (uint64_t)eval(node->rhs)
                                                 : eval(node->lhs) > eval(node->rhs);
                default:
                    return node->ty->is_unsigned ? (uint64_t)eval(node->lhs) >= (uint64_t)eval(node->rhs)
                                                 : eval(node->lhs) >= eval(node->rhs);
            }
        }
        case ND_LOGAND: {
            bool l = is_flonum(node->lhs->ty) ? !fp128_is_zero(eval_fp128(node->lhs))
                                              : !int128_is_zero(eval_int128(node->lhs));
            if (!l) return 0;
            bool r = is_flonum(node->rhs->ty) ? !fp128_is_zero(eval_fp128(node->rhs))
                                              : !int128_is_zero(eval_int128(node->rhs));
            return r;
        }
        case ND_LOGOR: {
            bool l = is_flonum(node->lhs->ty) ? !fp128_is_zero(eval_fp128(node->lhs))
                                              : !int128_is_zero(eval_int128(node->lhs));
            if (l) return 1;
            bool r = is_flonum(node->rhs->ty) ? !fp128_is_zero(eval_fp128(node->rhs))
                                              : !int128_is_zero(eval_int128(node->rhs));
            return r;
        }
        case ND_COND:
            if (is_flonum(node->cond->ty))
                return fp128_is_zero(eval_fp128(node->cond)) ? eval2(node->els, sym) : eval2(node->then, sym);
            return eval(node->cond) ? eval2(node->then, sym) : eval2(node->els, sym);
        case ND_PTRADD:
            return eval2(node->lhs, sym) + eval(node->rhs) * node->ty->base->size;
        case ND_IMCAST:
        case ND_EXCAST: {
            if (is_flonum(node->lhs->ty)) {
                bool ok;
                Int128 v = fp128_to_int128(eval_fp128(node->lhs), node->ty->is_unsigned ? UNSIGNED : SIGNED, &ok);
                if (!ok) error(node->tok, "floating constant out of range");
                return int128_to_i64(v);
            }
            if (is_bitint128(node->lhs->ty)) {
                return int128_to_i64(int128_normalize(eval_int128(node->lhs), 64, UNSIGNED));
            }
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
            if (node->var->ty->kind != TY_ARRAY && node->var->ty->kind != TY_VLA && node->var->ty->kind != TY_FUNC)
                error(node->tok, "invalid initializer");
            *sym = node->var->id;
            return 0;
        case ND_LABEL_VAL:
            if (!sym || !cur_fn) error(node->tok, "not a compile-time constant");
            char *lbl = format("%s..%s", str(cur_fn->id), str(node->label));
            *sym = intern(lbl, strlen(lbl));
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
// Evaluate a full integer constant expression. Values wider than 64 bits
// are constraint violations in their contexts (array sizes must fit
// size_t, case values the switch type, enumerators the underlying type);
// #if is the exception and pre-truncates its operands to uintmax_t.
static int64_t eval_ice(Node *node) {
    add_type(node);
    if (!is_integer(node->ty)) error(node->tok, "expression is not an integer constant expression");
    if (is_bitint128(node->ty)) {
        Int128 v = eval_int128(node);
        if (!int128_fits(v, 64, node->ty->is_unsigned ? UNSIGNED : SIGNED))
            error(node->tok, "integer constant expression does not fit in 64 bits");
        return int128_to_i64(v);
    }
    return eval(node);
}

int64_t const_expr(Token **rest, Token *tok) {
    Node *node = conditional(rest, tok);
    return eval_ice(node);
}

// AsOP  ::= "=" | "*=" | "/=" | "%=" | "+=" | "-="
//         | "<<=" | ">>=" | "&=" | "^=" | "|="
static int as_op[TK_NKIND] = {
    [TK_AS] = ND_AS,       [TK_ADDAS] = ND_ADDAS,   [TK_SUBAS] = ND_SUBAS,     [TK_MULAS] = ND_MULAS,
    [TK_DIVAS] = ND_DIVAS, [TK_MODAS] = ND_MODAS,   [TK_ANDAS] = ND_ANDAS,     [TK_ORAS] = ND_ORAS,
    [TK_XORAS] = ND_XORAS, [TK_LEFTAS] = ND_LEFTAS, [TK_RIGHTAS] = ND_RIGHTAS,
};

static inline bool is_assignop(Token *tok) { return as_op[tok->kind] != 0; }

// AsExp ::= CondExp (AsOP AsExp)?
static Node *assign(Token **rest, Token *tok) {
    Node *node = conditional(&tok, tok);
    while (is_assignop(tok)) {
        Token *as = tok;
        Node *rhs = assign(&tok, tok->next);
        NodeKind op = as_op[as->kind];
        if (op != ND_AS && (node->ty->qual & Q_ATOMIC))
            node = atomic_compound_assign(node, op, rhs, false, as);
        else
            node = new_binary(op, node, rhs, as);
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
static Node *init_decl_list(Token **rest, Token *tok, Type *basety, SClass sclass, int align, int funcspec) {
    bool is_static = sclass & SC_STATIC;
    bool is_constexpr = sclass & SC_CONSTEXPR;
    bool is_typedef = sclass & SC_TYPEDEF;

    Node dummy, *cur = &dummy;
    do {
        Token *start = tok;
        scope->vla_num = 0;
        Type *ty = declarator(&tok, tok, basety);
        Token *var_name = ty->name;

        if (ty->kind == TY_VOID) error(start, "variable ‘%s’ declared void", str(var_name->id));

        bool is_fn = ty->kind == TY_FUNC;
        if (funcspec && !is_fn) {
            if (funcspec & Q_NORETURN) error(tok, "‘noreturn’ can only appear on functions");
            if (funcspec & Q_INLINE) error(tok, "‘inline’ can only appear on functions");
        }
        SymKind symkind = is_fn ? SYM_FUNC : SYM_VAR;
        if (is_fn || is_typedef) {
            if (tok->kind == TK_AS)
                error(var_name,
                      "illegal initializer (only variables can be "
                      "initialized)");
            if (is_fn) {
                if (tok->kind == TK_LBRACE) error(var_name, "function definition is not allowed here");
                if (is_static) error(start, "function declared in block scope cannot have 'static' storage class");
            }
        }
        if (is_constexpr && tok->kind != TK_AS) error(tok, "‘constexpr’ requires an initialized data declaration");

        bool is_extern = sclass & SC_EXTERN || is_fn;
        Sym *var;
        NameSpace *ns = find_ident(var_name, false, is_extern);
        uint32_t id = get_ident(var_name);
        if (ns) {
            if (!is_extern) {
                diag("error", var_name, "redefinition of ‘%s’", str(var_name->id));
                diag_exit("note", ns->loc, "previous definition is here");
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
        NameSpace *new_ns = push_namespace(id, symkind, ty, var_name);
        new_ns->var = var;
        new_ns->prev = ns;
        if (is_extern) {
            if (ns)
                new_ns->lnk = ns->lnk;
            else
                new_ns->lnk = LK_EXTERN;
        }
        var->sclass = sclass;
        var->align = MAX(align, ty->align);
        var->is_function = is_fn;
        var->funcspec |= funcspec;
        if (tok->kind == TK_AS) {
            if (is_extern)
                error(var_name, "declaration of block scope identifier ‘%s’ with linkage cannot have an initializer",
                      str(var_name->id));
            if (is_static) {
                gvar_initializer(&tok, tok->next, var);
            } else {
                Node *expr = lvar_initializer(&tok, tok->next, var);
                cur = cur->next = new_unary(ND_EXPR_STMT, expr, tok);
            }
        }
        for (int i = 0; i < scope->vla_num; i++) {
            cur = cur->next = new_unary(ND_EXPR_STMT, scope->vla_expr[i], scope->vla_expr[i]->tok);
        }
        if (var->ty->kind == TY_VLA) {
            if (!scope->sp_saved) {
                scope->sp_saved = true;
                curm->has_vla = true;
                Node *sp = new_var_node(scope->stack_top, tok);
                add_type(sp);
                Node *save = new_node(ND_SP_SAVE, tok);
                save->ty = sp->ty;
                Node *save_expr = new_binary(ND_AS, sp, save, tok);
                save_expr->ty = pointer_to(T.ty_void, 0);
                cur = cur->next = save_expr;
            }
            Node *size = scope->vla_expr[0];
            for (int i = 1; i < scope->vla_num; i++) {
                size = new_binary(ND_MUL, size, scope->vla_expr[i], tok);
            }
            Type *base_ty = var->ty;
            while (base_ty->kind == TY_VLA) base_ty = base_ty->base;
            add_type(size);
            Node *alloc = new_node(ND_ALLOCA, tok);
            alloc->lhs = size;
            alloc->rhs = new_ulong(var->align, tok);
            alloc->base_ty = base_ty;
            add_type(alloc);
            Node *vla_var = new_var_node(var, tok);
            add_type(vla_var);
            Node *expr = new_binary(ND_AS, vla_var, alloc, tok);
            expr->ty = var->ty;
            cur = cur->next = expr;
        }
        scope->vla_num = 0;
        if (var->ty->size < 0 && (var->ty->kind != TY_ARRAY && var->ty->kind != TY_VLA))
            error(var_name, "variable ‘%s’ has incomplete type", str(var_name->id));
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
        if (tok->kind != TK_SEMI) node->body = init_decl_list(&tok, tok, basety, sclass, align, funcspec);
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
    cnt_blk(node->els ? 3 : 2);  // gen_if: then / (else) / merge
    *rest = tok;

    Node *restore = leave_scope(tok);
    if (restore) node = new_binary(ND_COMMA, node, restore, tok);
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
    cnt_blk(1);  // gen_switch: merge (case labels count in label())

    brk_depth--;
    cur_sw = sw;
    node->case_next = reverse_list(Node, node->case_next, case_next);

    Node *restore = leave_scope(tok);
    if (restore) node = new_binary(ND_COMMA, node, restore, tok);
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
    cnt_blk(3);  // gen_while: cond / body / merge

    cont_depth--;
    brk_depth--;
    Node *restore = leave_scope(tok);
    if (restore) node = new_binary(ND_COMMA, node, restore, tok);
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
    cnt_blk(3);  // gen_do: body / cond / merge

    cont_depth--;
    brk_depth--;
    Node *restore = leave_scope(tok);
    if (restore) node = new_binary(ND_COMMA, node, restore, tok);
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
        node->init = declaration(&tok, tok, basety, sclass, align, funcspec);
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
    cnt_blk(4);  // gen_for: cond / body / incr / merge

    cont_depth--;
    brk_depth--;
    Node *restore = leave_scope(tok);
    if (restore) node = new_binary(ND_COMMA, node, restore, tok);
    return node;
}

// JmpStmt ::= "goto" (Ident | "*" Exp) ";"
//          | "continue" Ident? ";"
//          | "break" Ident? ";"
//          | "return" Exp? ";"
// GotoStmt ::= "goto" Ident ";"
static Node *goto_stmt(Token **rest, Token *tok) {
    if (tok->next->kind == TK_STAR) {
        // [GNU] `goto *ptr` jumps to the address specified by `ptr`.
        Node *node = new_node(ND_GOTO_EXPR, tok);
        node->lhs = expr(&tok, tok->next->next);
        lvalue_convert(&node->lhs);
        if (!is_pointer(node->lhs->ty)) error(node->lhs->tok, "computed goto must be pointer type");
        *rest = skip(tok, TK_SEMI);
        return node;
    }
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
    if (cur_fn->funcspec & Q_NORETURN)
        warning(tok, "function ‘%s’ declared 'noreturn' should not return", str(cur_fn->id));
    Node *node = new_node(ND_RETURN, tok);
    Type *ret = cur_fn->ty->ret;
    if (tok->next->kind == TK_SEMI) {
        if (ret->kind != TY_VOID) error(tok, "non-void function ‘%s’ should return a value", str(cur_fn->id));
        *rest = tok->next->next;
        return node;
    }

    node->lhs = expr(&tok, tok->next);
    if (ret->kind == TY_VOID) error(node->tok, "void function ‘%s’ should not return a value", str(cur_fn->id));
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
            diag("error", tok, "redefinition of label ‘%s’", str(label));
            diag_exit("note", cur->tok, "previous definition is here");
        }
        cur = cur->goto_next;
    }
}

static void check_case(int64_t val, Token *tok) {
    Node *cur = cur_sw->case_next;
    while (cur) {
        if (int128_to_i64(cur->ival) == val) {
            diag("error", tok, "duplicate case value ‘%ld’", val);
            diag_exit("note", cur->tok, "previous case defined here");
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
    Node dummy = {}, *cur = &dummy;
    int idx = -1;
    while (1) {
        if (tok->kind == TK_IDENT && tok->next->kind == TK_COLON) {
            Node *node = new_node(ND_LABEL, tok);
            node->label = tok->id;
            check_label(node->label, tok);
            node->goto_next = labels;
            labels = node;
            if (idx < 0 && cur_fn) {
                idx = cur_fn->num_lbl++;
                cnt_blk(1);
            }
            node->blk_idx = idx;

            cur = cur->label_ring = node;
            tok = tok->next->next;
            continue;
        }
        if (tok->kind == TK_DEFAULT) {
            if (!cur_sw) error(tok, "‘default’ label not within a switch statement");
            if (cur_sw->default_case) {
                diag("error", tok, "multiple default labels in one switch");
                diag_exit("note", cur_sw->default_case->tok, "this is the first default label");
            }
            Node *node = new_node(ND_CASE, tok);
            tok = skip(tok->next, TK_COLON);
            cur_sw->default_case = node;
            if (idx < 0 && cur_fn) {
                idx = cur_fn->num_lbl++;
                cnt_blk(1);
            }
            node->blk_idx = idx;
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
                node->ival = int128_set_i(val1);
                if (idx < 0 && cur_fn) {
                    idx = cur_fn->num_lbl++;
                    cnt_blk(1);
                }
                node->blk_idx = idx;

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
                node->ival = int128_set_i(i);
                if (idx < 0 && cur_fn) {
                    idx = cur_fn->num_lbl++;
                    cnt_blk(1);
                }
                node->blk_idx = idx;
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
    cur->label_ring = dummy.label_ring;
    return dummy.label_ring;
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

// StaticAssertDecl ::= ("static_assert" | "_Static_assert") "(" ConstExp ("," StrLit)? ")" ";"
// The message is optional in C23; a failing assertion is a hard error
// carrying the message text.
static Node *static_assert_decl(Token **rest, Token *tok) {
    Token *start = tok;
    tok = skip(tok->next, TK_LPAREN);
    int64_t v = const_expr(&tok, tok);
    if (tok->kind == TK_COMMA) {
        Token *msg = tok->next;
        if (msg->kind != TK_STRLIT) error(msg, "static assertion message must be a string literal");
        if (!v) error(start, "static assertion failed: %s", str(msg->id));
        tok = msg->next;
    } else if (!v) {
        error(start, "static assertion failed");
    }
    tok = skip(tok, TK_RPAREN);
    *rest = skip(tok, TK_SEMI);
    return new_node(ND_NOP, start);
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
        case TK_STATIC_ASSERT:
            stmt = static_assert_decl(rest, tok);
            break;
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
    Node dummy, *cur = &dummy;
    Node *node = new_node(ND_COMP_STMT, tok);

    if (!is_func_body) {
        enter_scope();
    } else {
        Scope *scp = scope->next;
        for (int i = 0; i < scp->vla_num; i++) {
            cur = cur->next = new_unary(ND_EXPR_STMT, scp->vla_expr[i]->rhs, scp->vla_expr[i]->rhs->tok);
            add_type(cur);
        }
        scp->vla_num = 0;
    }

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
                if (tok->kind == TK_AS)
                    error(tok,
                          "illegal initializer (only variables can be "
                          "initialized)");
                push_namespace(get_ident(ty->name), SYM_TYNAME, ty, ty->name);
            } else {
                cur = cur->next = declaration(&tok, tok, basety, sclass, align, funcspec);
            }

            add_type(cur);
            continue;
        }

        // UnLabelStmt
        cur = cur->next = stmt(&tok, tok);
        add_type(cur);
    }

    if (!is_func_body) {
        Node *restore = leave_scope(tok);
        if (restore) cur = cur->next = restore;
    }
    cur->next = NULL;
    *rest = skip(tok, TK_RBRACE);

    node->body = dummy.next;
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
                diag("error", tag, "use of ‘%s’ with tag type that does not match previous declaration", str(tag->id));
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
                diag("error", tag, "use of ‘%s’ with tag type that does not match previous declaration", str(tag->id));
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
    EnumVal dummy = {};
    EnumVal *cur = &dummy;
    int i = 0;
    int64_t val = 0;
    while (!consume_end(rest, tok)) {
        if (i++ > 0) tok = skip(tok, TK_COMMA);

        Token *enm_name = tok;
        uint32_t name = get_ident(enm_name);
        EnumVal *tmp = dummy.next;
        while (tmp) {
            if (tmp->name->id == name) {
                diag("error", enm_name, "redeclaration of enumerator ‘%s’", str(enm_name->id));
                diag_exit("note", tmp->name, "previous definition is here");
                exit(1);
            }
            tmp = tmp->next;
        }
        if (!redefine) {
            NameSpace *ns2 = find_ident(enm_name, false, false);
            if (ns2) {
                diag("error", enm_name, "redeclaration of ‘%s’", str(enm_name->id));
                diag_exit("note", ns2->loc, "previous definition is here");
            }
        }
        tok = tok->next;

        if (tok->kind == TK_AS) val = const_expr(&tok, tok->next);

        push_namespace(name, SYM_ENUM, ty, enm_name)->enum_val = val;
        EnumVal *enm = emalloc(sizeof(EnumVal));
        enm->name = enm_name;
        enm->val = val++;
        cur = cur->next = enm;
    }

    if (!dummy.next) error(tok, "empty enum is invalid");
    ty->enumvals = dummy.next;
    if (redefine) {
        if (!is_compatible(ty, exist_ty)) {
            diag("error", tag, "conflicting redefinition of enum ‘enum %s’", str(tag->id));
            goto note;
        }
        return exist_ty;
    }
    return ty;

note:
    diag_exit("note", ns->loc, "previous definition is here");
    return NULL;
}

static void check_anon_mem(Member *mem1, Member *mem2) {
    for (; mem2; mem2 = mem2->next) {
        // Anonymous struct member
        if ((mem2->ty->kind == TY_STRUCT || mem2->ty->kind == TY_UNION) && !mem2->name)
            check_anon_mem(mem1, mem2->ty->members);

        // Regular struct member
        Member *exist = get_struct_member(mem1, mem2->name);
        if (exist) {
            while (!exist->name) exist = get_struct_member(exist->ty->members, mem2->name);
            diag("error", mem2->name, "duplicate member ‘%s’", str(mem2->name->id));
            diag_exit("note", exist->name, "previous declaration is here");
        }
    }
}

// MemDecl  ::= TypeSpec+ (MemDeclr ("," MemDeclr)*)? ";"
// MemDeclr ::= Declr
// A variably modified type (C11 6.7.6.2p2): a VLA or any type derived
// from one (pointer to VLA, array of VLA) — not allowed as a
// struct/union member.
static bool is_variably_modified(Type *ty) {
    for (;;) {
        if (ty->kind == TY_VLA) return true;
        if (ty->kind == TY_PTR || ty->kind == TY_ARRAY) {
            ty = ty->base;
            continue;
        }
        return false;
    }
}

// A member type that makes the containing struct/union unassignable:
// const-qualified, or a struct/union that itself has such a member
// (Q_MEMCONST). Array element const lives on the base type.
static bool is_memconst(Type *ty) {
    for (;;) {
        if (ty->qual & (Q_CONST | Q_MEMCONST)) return true;
        if (ty->kind == TY_ARRAY) {
            ty = ty->base;
            continue;
        }
        return false;
    }
}

static void struct_members(Token **rest, Token *tok, Type *ty) {
    Member dummy = {};
    Member *cur = &dummy;

    while (tok->kind != TK_RBRACE) {
        // static_assert-declaration is a member-declaration (6.7.2.1);
        // it declares no member
        if (tok->kind == TK_STATIC_ASSERT) {
            static_assert_decl(&tok, tok);
            continue;
        }
        int align = 0;
        Type *basety = declspecs(&tok, tok, NULL, &align, NULL);
        int i = 0;
        Token *start = tok;

        // Anonymous struct member
        if (match(&tok, tok, TK_SEMI)) {
            if (!is_record(basety)) {
                warning(start, "declaration does not declare anything");
                continue;
            }
            Member *mem = emalloc(sizeof(Member));
            mem->ty = basety;
            if (is_memconst(basety)) ty->qual |= Q_MEMCONST;
            if (align) mem->is_align = true;
            mem->align = MAX(align, mem->ty->align);
            check_anon_mem(dummy.next, mem->ty->members);
            cur = cur->next = mem;
            continue;
        }

        // Regular struct members
        while (!match(&tok, tok, TK_SEMI)) {
            if (i++) tok = skip(tok, TK_COMMA);
            Member *mem = emalloc(sizeof(Member));

            if (match(&tok, tok, TK_COLON)) {
                if (align) error(start, "'_Alignas' cannot be applied to a bit-field");
                mem->ty = basety;
                if (is_memconst(basety)) ty->qual |= Q_MEMCONST;
                mem->align = mem->ty->align;
                mem->is_bitfield = true;
                mem->bit_width = const_expr(&tok, tok);
                cur = cur->next = mem;
                continue;
            }

            mem->ty = declarator(&tok, tok, basety);
            if (is_memconst(mem->ty)) ty->qual |= Q_MEMCONST;
            Token *mem_name = mem->ty->name;
            if (align) mem->is_align = true;
            mem->align = MAX(align, mem->ty->align);
            if (mem->ty->kind == TY_VOID) error(mem_name, "field ‘%s’ declared void", str(mem_name->id));
            if (mem->ty->kind == TY_FUNC) error(mem_name, "field ‘%s’ declared as a function", str(mem_name->id));
            if (is_variably_modified(mem->ty))
                error(mem_name, "field ‘%s’ has variably modified type", str(mem_name->id));
            if (mem->ty->size < 0 && tok->next->kind != TK_RBRACE)
                error(mem_name, "variable ‘%s’ has incomplete type", str(mem_name->id));

            mem->name = mem_name;
            Member *exist = get_struct_member(dummy.next, mem_name);
            if (exist) {
                while (!exist->name) exist = get_struct_member(exist->ty->members, mem_name);
                diag("error", mem_name, "duplicate member ‘%s’", str(mem_name->id));
                diag_exit("note", exist->name, "previous declaration is here");
            }

            if (match(&tok, tok, TK_COLON)) {
                if (align) error(start, "'_Alignas' cannot be applied to a bit-field");
                mem->is_bitfield = true;
                mem->bit_width = const_expr(&tok, tok);
                if (mem->bit_width == 0) error(mem_name, "zero width for bit-field ‘%s’", str(mem_name->id));
                if (mem->bit_width > mem->ty->size * 8)
                    error(mem_name, "width of ‘%s’ exceeds its type", str(mem_name->id));
            }

            cur = cur->next = mem;
        }
    }

    if (cur != &dummy && cur->ty->kind == TY_ARRAY && cur->ty->len < 0) {
        cur->ty = array_of(cur->ty->base, 0);
        ty->is_flexible = true;
    }

    *rest = tok->next;
    ty->members = dummy.next;
}

// Fix it
static Type *get_unit_ty(int bytes, bool is_unsigned) {
    if (bytes == 1) return is_unsigned ? T.ty_uchar : T.ty_schar;
    if (bytes == 2) return is_unsigned ? T.ty_ushort : T.ty_short;
    if (bytes == 4) return is_unsigned ? T.ty_uint : T.ty_int;
    // 8 bytes on every target: long is only 4 bytes on ILP32.
    return is_unsigned ? T.ty_ullong : T.ty_llong;
}

static int min_bytes_for_bits(int bits) {
    if (bits <= 8) return 1;
    if (bits <= 16) return 2;
    if (bits <= 32) return 4;
    return 8;
}

static void layout_struct(Type *ty, bool is_union) {
    ty->align = 1;
    int offset = 0;
    int bits = 0;
    int unit_size = 0;
    uint32_t idx = 0;

#define END_UNIT()                                      \
    do {                                                \
        if (unit_size && bits > 0) offset += unit_size; \
        unit_size = 0;                                  \
        bits = 0;                                       \
    } while (0)

    for (Member *mem = ty->members; mem; mem = mem->next) {
        ty->align = MAX(ty->align, mem->align);
        mem->idx = idx++;

        if (is_union) {
            offset = MAX(offset, mem->ty->size);
            continue;
        }

        if (mem->is_bitfield) {
            int width = mem->bit_width;

            if (width == 0) {
                END_UNIT();
                offset = ALIGN_UP(offset, mem->align);
            }

            if (unit_size == 0) {
                int total_bits = width;
                for (Member *m = mem->next; m && m->is_bitfield && m->bit_width; m = m->next)
                    total_bits += m->bit_width;

                unit_size = min_bytes_for_bits(total_bits);
                bits = 0;
            }

            if (bits + width > unit_size * 8) {
                END_UNIT();

                int total_bits = width;
                for (Member *m = mem->next; m && m->is_bitfield && m->bit_width; m = m->next)
                    total_bits += m->bit_width;

                unit_size = min_bytes_for_bits(total_bits);
                bits = 0;
            }
            mem->offset = offset;
            mem->bit_offset = bits;
            mem->unit_ty = get_unit_ty(unit_size, mem->ty->is_unsigned);
            bits += width;
        } else {
            END_UNIT();
            offset = ALIGN_UP(offset, mem->align);
            mem->offset = offset;
            mem->unit_ty = mem->ty;
            offset += mem->ty->size;
        }
    }

    END_UNIT();
    ty->size = ALIGN_UP(offset, ty->align);
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
                diag("error", tag, "use of ‘%s’ with tag type that does not match previous declaration", str(tag->id));
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
                diag("error", tag, "use of ‘%s’ with tag type that does not match previous declaration", str(tag->id));
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
    layout_struct(ty, is_union);

    if (redefine) {
        if (!is_compatible(ty, exist_ty)) {
            diag("error", tag, "redefinition of struct or union ‘%s %s’", ty_kind, str(tag->id));
            goto note;
        }
        return exist_ty;
    }
    insert_ty(ty, ty_kind);
    return ty;
note:
    diag_exit("note", ns->loc, "previous definition is here");
    return NULL;
}

// TypeofSpec ::= ("typeof" | "typeof_unqual") "(" (Exp | TypeName) ")"
static Type *typeof_specifier(Token **rest, Token *tok, bool is_unqual) {
    tok = skip(tok->next, TK_LPAREN);

    Type *ty;
    if (is_typename(tok, true)) {
        ty = typename(&tok, tok);
    } else {
        Node *node = expr(&tok, tok);
        add_type(node);
        if (node->kind == ND_IMCAST && node->lhs->ty->kind == TY_ARRAY) node = node->lhs;
        if (node->kind == ND_IMCAST && node->lhs->ty->kind == TY_VLA) node = node->lhs;
        if (node->kind == ND_IMCAST && node->lhs->ty->kind == TY_FUNC) node = node->lhs;
        if (node->kind == ND_MEMBER && node->member->is_bitfield)
            error(node->tok, "invalid application of 'typeof' to bit-field ‘%s’", str(node->member->name->id));
        ty = node->ty;
    }
    if (is_unqual) ty = type_unqual(ty);
    *rest = skip(tok, TK_RPAREN);
    return ty;
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
//            | TypeofSpec
// AlignSpec ::= "alignas" "(" (TypeName | ConstExp) ")"
// TypeQual  ::= "const" | "restrict" | "volatile"
// FuncSpec  ::= "inline" | "_Noreturn"
static Type *declspecs(Token **rest, Token *tok, SClass *sclass, int *align, int *funcspec) {
    Type *ty;
    bool seen_auto = false;
    bool is_constexpr = false;
    bool is_thread = false;
    int typespec_cnt = 0;
    int qual = 0;
    int bitint_w = -1;  // width parsed from _BitInt(N)
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
        F16 = 1 << 16,
        F32 = 1 << 18,
        F64 = 1 << 20,
        F128 = 1 << 22,
        BITINT = 1 << 24,
        OTHER = 1 << 26,
        SIGNED = 1 << 28,
        UNSIGNED = 1 << 29,
    };

    while (is_typename(tok, true)) {
        Token *ty_tok = tok;
        switch (tok->kind) {
            case TK_AUTO:
                if (seen_auto) error(tok, "duplicate ‘auto’");
                seen_auto = true;
                break;
            case TK_CONSTEXPR:
                if (is_constexpr) error(tok, "duplicate ‘constexpr’");
                is_constexpr = true;
                break;
            case TK_THREAD:
                if (is_thread) error(tok, "duplicate ‘thread_local’");
                is_thread = true;
                break;
            case TK_TYPEDEF:
            case TK_STATIC:
            case TK_EXTERN:
            case TK_REGISTER: {
                SClass sc = sc_table[tok->kind];
                if (!sclass) error(tok, "storage class specifier is not allowed in this context");
                if (*sclass) {
                    if (*sclass & sc)
                        error(tok, "duplicate ‘%s’", sclass_name[sc]);
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
            case TK_ATOMIC: {
                Token *start = tok;
                qual |= Q_ATOMIC;
                if (tok->next->kind != TK_LPAREN) break;
                ty = typename(&tok, tok->next->next);
                if (is_array(ty)) error(start, "_Atomic cannot be applied to array type");
                if (ty->kind == TY_FUNC) error(start, "_Atomic cannot be applied to function type");
                if (ty->qual & Q_ATOMIC) error(start, "_Atomic cannot be applied to atomic type");
                if (ty->qual != 0) error(start, "_Atomic cannot be applied to qualified type");
                typespec_cnt += OTHER;
                tok = skip(tok, TK_RPAREN);
                goto check_type;
            }
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
            case TK_TYPEOF:
            case TK_TYPEOF_U:
                ty = typeof_specifier(&tok, tok, tok->kind == TK_TYPEOF_U);
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
                    error(ty_tok, "requested alignment ‘%d’ is not a positive power of 2", *align);
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
            case TK_F16:
                typespec_cnt += F16;
                break;
            case TK_F32:
                typespec_cnt += F32;
                break;
            case TK_F64:
                typespec_cnt += F64;
                break;
            case TK_F128:
                typespec_cnt += F128;
                break;
            case TK_BITINT: {
                if (bitint_w >= 0) error(tok, "duplicate ‘_BitInt’");
                tok = skip(tok->next, TK_LPAREN);
                bitint_w = (int)const_expr(&tok, tok);
                tok = skip(tok, TK_RPAREN);
                if (bitint_w < 1 || bitint_w > 128) error(tok, "width of ‘_BitInt’ must be between 1 and %d", 128);
                typespec_cnt += BITINT;
                goto check_type;
            }
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
                ty = T.ty_void;
                break;
            case BOOL:
                ty = T.ty_bool;
                break;
            case CHAR:
                ty = T.ty_char;
                break;
            case SIGNED + CHAR:
                ty = T.ty_schar;
                break;
            case UNSIGNED + CHAR:
                ty = T.ty_uchar;
                break;
            case SHORT:
            case SHORT + INT:
            case SIGNED + SHORT:
            case SIGNED + SHORT + INT:
                ty = T.ty_short;
                break;
            case UNSIGNED + SHORT:
            case UNSIGNED + SHORT + INT:
                ty = T.ty_ushort;
                break;
            case INT:
            case SIGNED:
            case SIGNED + INT:
                ty = T.ty_int;
                break;
            case UNSIGNED:
            case UNSIGNED + INT:
                ty = T.ty_uint;
                break;
            case LONG:
            case LONG + INT:
            case SIGNED + LONG:
            case SIGNED + LONG + INT:
                ty = T.ty_long;
                break;
            case UNSIGNED + LONG:
            case UNSIGNED + LONG + INT:
                ty = T.ty_ulong;
                break;
            case LONG + LONG:
            case LONG + LONG + INT:
            case SIGNED + LONG + LONG:
            case SIGNED + LONG + LONG + INT:
                ty = T.ty_llong;
                break;
            case UNSIGNED + LONG + LONG:
            case UNSIGNED + LONG + LONG + INT:
                ty = T.ty_ullong;
                break;
            case FLOAT:
                ty = T.ty_float;
                break;
            case DOUBLE:
                ty = T.ty_double;
                break;
            case LONG + DOUBLE:
                ty = T.ty_ldouble;
                break;
            case F16:
                ty = f16;
                break;
            case F32:
                ty = f32;
                break;
            case F64:
                ty = f64;
                break;
            case F128:
                ty = f128;
                break;
            case BITINT:
            case SIGNED + BITINT:
                ty = bitint[bitint_w][0];
                if (!ty) error(ty_tok, "signed ‘_BitInt’ must have a bit size of at least 2");
                break;
            case UNSIGNED + BITINT:
                ty = bitint[bitint_w][1];
                if (!ty) error(ty_tok, "invalid ‘_BitInt’ width");
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
    if (sclass && *sclass == SC_TYPEDEF) {
        if (seen_auto) error(tok, "‘auto’ not allowed in typedef");
        if (is_constexpr) error(tok, "‘constexpr’ not allowed in typedef");
        if (is_thread) error(tok, "‘thread_local’ not allowed in typedef");
    }

    if (!typespec_cnt) {
        if (!seen_auto) error(tok, "a type specifier is required for all declarations");
        ty = T.ty_none;
        seen_auto = false;
    }

    if (seen_auto || is_constexpr || is_thread) {
        if (!sclass) error(tok, "storage class specifier is not allowed in this context");
    }

    if (seen_auto) {
        if (*sclass) error(tok, "multiple storage classes in declaration specifiers");
        *sclass = SC_AUTO;
    }

    if (is_constexpr) {
        if (*sclass & ~(SC_AUTO | SC_REG | SC_STATIC)) error(tok, "‘constexpr’ used with ‘%s’", sclass_name[*sclass]);
        if (is_thread) error(tok, "‘constexpr’ used with ‘thread_local’");
        if (qual & Q_VOLATILE) error(tok, "constexpr variable cannot have qualifiers ‘volatile’");
        *sclass |= SC_CONSTEXPR;
        qual |= Q_CONST;
    }

    if (is_thread) {
        if (*sclass & ~(SC_EXTERN | SC_STATIC)) error(tok, "‘thread_local’ used with ‘%s’", sclass_name[*sclass]);
        if (!is_file_scope() && !(*sclass & (SC_EXTERN | SC_STATIC)))
            error(tok, "‘thread_local’ variables must have global storage");
        *sclass |= SC_THREAD;
    }

    *rest = tok;
    return type_qual(ty, qual);
}

static Type *func_param(Token **rest, Token *tok, Type *ty) {
    tok = skip(tok, TK_LPAREN);
    if (tok->kind == TK_VOID && tok->next->kind == TK_RPAREN) {
        *rest = tok->next->next;
        return func_type(ty);
    }

    uint32_t nparam = 0;
    bool is_variadic = false;
    Type dummy = {}, *cur = &dummy;

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
        if (paramty->kind == TY_VOID) error(start, "argument may not have ‘void’ type");
        // "array of T" is converted to "pointer to T" in the parameter
        // context. For example, *argv[] is converted to **argv by this.

        if (paramty->kind == TY_ARRAY || paramty->kind == TY_VLA) {
            Type *arr = paramty;
            paramty = pointer_to(paramty->base, paramty->qual);
            paramty->name = arr->name;
            paramty->is_star = arr->is_star;
            paramty->is_static = arr->is_static;
        }
        if (paramty->kind == TY_FUNC) {
            Type *fn = paramty;
            paramty = pointer_to(paramty, 0);
            paramty->name = fn->name;
        }

        if (paramty->size < 0)
            error(paramty->name, "parameter ‘%.*s’ has incomplete type", paramty->name->len, tok_text(paramty->name));
        if (paramty->name) {
            uint32_t id = get_ident(paramty->name);
            for (Type *p = dummy.next; p && p->name; p = p->next) {
                if (id == p->name->id) {
                    diag("error", paramty->name, "redefinition of parameter ‘%s’", str(id));
                    diag_exit("note", p->name, "previous definition is here");
                }
            }
        }
        cur = cur->next = copy_type(paramty);
        nparam++;
    }

    *rest = skip(tok, TK_RPAREN);

    ty = func_type(ty);
    ty->is_variadic = is_variadic;
    ty->params = dummy.next;
    ty->nparam = nparam;

    return ty;
}

// ArrDimen ::= "[" TypeQual* AsExp? "]"
//           | "[" "static" TypeQual* AsExp "]"
//           | "[" TypeQual+ "static" AsExp "]"
//           | "[" TypeQual* "*" "]"
static Type *array_dimensions(Token **rest, Token *tok, Type *ty, bool is_param) {
    Node *len = NULL;
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
        len = assign(&tok, tok);
    } else if (tok->kind == TK_STAR && tok->next->kind == TK_RBRACKET) {
        if (!is_param) error(tok, "[*] used outside of function prototype");
        is_star = true;
        tok = tok->next;
    } else if (tok->kind != TK_RBRACKET) {
        len = assign(&tok, tok);
    }

    tok = skip(tok, TK_RBRACKET);
    ty = decl_suffix(rest, tok, ty, is_param);

    if (!len) {
        ty = array_of(ty, -1);
    } else if (ty->kind == TY_VLA || !is_const_expr(len)) {
        ty = vla_of(ty, len);
        if (!scope->stack_top) scope->stack_top = new_lvar(id_anon, pointer_to(T.ty_void, 0));
        ty->vla_cnt = new_lvar(id_anon, T.ty_ulong);
        ty->vla_len = len;
        Node *expr = new_binary(ND_AS, new_var_node(ty->vla_cnt, tok), len, tok);
        scope->vla_expr = vgrow(scope->vla_expr, scope->vla_num + 1);
        scope->vla_expr[scope->vla_num++] = expr;
    } else {
        ty = array_of(ty, eval_ice(len));
    }

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
    if ((ty->kind == TY_ARRAY || ty->kind == TY_VLA) && ty->base->kind == TY_FUNC)
        error(tok, "declaration as array of functions");
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
static Node *declaration(Token **rest, Token *tok, Type *basety, SClass sclass, int align, int funcspec) {
    Node *node = new_node(ND_DECL, tok);
    if (tok->kind == TK_SEMI) {
        if (sclass & SC_CONSTEXPR) error(tok, "‘constexpr’ requires an initialized data declaration");
        *rest = tok->next;
        return node;
    }
    node->body = init_decl_list(&tok, tok, basety, sclass, align, funcspec);
    *rest = skip(tok, TK_SEMI);
    return node;
}

static void resolve_goto_labels(void) {
    for (Node *x = gotos; x; x = x->goto_next) {
        for (Node *y = labels; y; y = y->goto_next)
            if (x->label == y->label) {
                x->target = y;
                y->is_ref = true;
                if (x->kind == ND_LABEL_VAL) y->is_addr = true;
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

    if (tok->kind == TK_STATIC_ASSERT) {
        static_assert_decl(&tok, tok);
        return tok;
    }

    SClass sclass = 0;
    int align = 0;
    int funcspec = 0;
    Type *basety = declspecs(&tok, tok, &sclass, &align, &funcspec);
    if (tok->kind == TK_SEMI) return tok->next;

    int cnt = -1;
    while (1) {
        cnt++;
        Type *ty = declarator(&tok, tok, basety);
        Token *var_name = ty->name;
        NameSpace *ns = find_ident(var_name, false, false);
        Sym *var;
        bool is_fn = ty->kind == TY_FUNC;
        if (funcspec && !is_fn) {
            if (funcspec & Q_NORETURN) error(tok, "‘noreturn’ can only appear on functions");
            if (funcspec & Q_INLINE) error(tok, "‘inline’ can only appear on functions");
        }

        // function-definition
        if (tok->kind == TK_LBRACE) {
            if (cnt || !is_fn) error(tok, "expected ‘=’, ‘,’, ‘;’ before ‘{’ token");
            if (sclass & SC_TYPEDEF) error(tok, "function definition declared ‘typedef’");
            if (sclass & SC_THREAD) error(tok, "function definition declared ‘thread_local’");
            if (sclass & SC_CONSTEXPR) error(tok, "function definition declared ‘constexpr’");
            if (sclass & SC_REG) error(tok, "function definition declared ‘register’");
            if (sclass & SC_AUTO) error(tok, "function definition declared ‘auto’");

            if (ns) {
                check_decl_compatile(ns, SYM_FUNC, ty);
                var = ns->var;
                if (var->is_defined) {
                    diag("error", var_name, "redefinition of ‘%s’", str(var_name->id));
                    goto note;
                }
                if (sclass == SC_STATIC && var->sclass != SC_STATIC) {
                    diag("error", var_name, "static declaration of ‘%s’ follows non-static declaration",
                         str(var_name->id));
                    goto note;
                }
            } else {
                var = new_gvar(get_ident(var_name), ty);
                ns = push_namespace(var->id, SYM_FUNC, ty, var_name);
                ns->var = var;
                ns->lnk = sclass == SC_STATIC ? LK_INTERN : LK_EXTERN;
                var->is_function = true;
                var->sclass = sclass;
            }

            var->is_defined = true;
            var->funcspec |= funcspec;
            cur_fn = var;
            cur_fn->num_blk = 2;  // fn->start + fn->end
            cur_fn->num_lbl = 0;
            locals = NULL;
            enter_scope();

            Type *param = ty->params;
            while (param) {
                if (is_pointer(param) && param->is_star)
                    error(var_name, "‘[*]’ not allowed in other than function prototype scope");
                uint32_t id = id_anon;
                if (param->name) id = get_ident(param->name);
                push_namespace(id, SYM_VAR, ty, param->name)->var = new_lvar(id, param);
                param = param->next;
            }

            //  "__func__" is automatically defined as if
            // static const char __func__[] = "function-name";
            // [GNU] "__FUNCTION__" is yet another name of "__func__".
            Type *fn_name = array_of(T.ty_char, str_len(var->id) + 1);

            NameSpace *tmp = push_namespace(id_func, SYM_VAR, fn_name, var_name);
            NameSpace *tmp2 = push_namespace(id_function, SYM_VAR, fn_name, var_name);

            tmp2->var = tmp->var = new_string_literal(var->id, fn_name);

            var->body = compound_stmt2(&tok, tok, true);

            var->locals = reverse_list(Sym, locals, next);
            var->labels = labels;
            resolve_goto_labels();

            Node *restore = leave_scope(tok);
            if (restore) {
                Node *stmt = var->body->body;
                while (stmt->next) stmt = stmt->next;
                stmt->next = restore;
            }
            cur_fn = NULL;
            return tok;
        }

        // declaration
        SymKind symkind = is_fn ? SYM_FUNC : SYM_VAR;
        if (tok->kind == TK_AS) {
            if (is_fn || sclass & SC_TYPEDEF)
                error(var_name,
                      "illegal initializer (only variables can be "
                      "initialized)");
        } else if (sclass & SC_CONSTEXPR) {
            error(var_name, "‘constexpr’ requires an initialized data declaration");
        }

        if (sclass & SC_REG) error(var_name, "file-scope declaration of ‘%s’ specifies ‘register’", str(var_name->id));
        if (sclass & SC_AUTO) error(var_name, "file-scope declaration of ‘%s’ specifies ‘auto’", str(var_name->id));

        if (sclass & SC_TYPEDEF) {
            if (ns)
                check_decl_compatile(ns, SYM_TYNAME, ty);
            else
                push_namespace(get_ident(var_name), SYM_TYNAME, ty, var_name);
        } else {
            if (ns) {
                check_decl_compatile(ns, symkind, ty);
                var = ns->var;
                if (var->is_defined && tok->kind == TK_AS) {
                    diag("error", var_name, "redefinition of ‘%s’", str(var_name->id));
                    goto note;
                }
                if (var->sclass & SC_STATIC) {
                    if (!is_fn && !(sclass & (SC_EXTERN | SC_STATIC))) {
                        diag("error", var_name, "non-static declaration of ‘%s’ follows static declaration",
                             str(var_name->id));
                        goto note;
                    }
                }
                if (sclass & SC_STATIC) {
                    if (!(var->sclass & SC_STATIC)) {
                        diag("error", var_name, "static declaration of ‘%s’ follows non-static declaration",
                             str(var_name->id));
                        goto note;
                    }
                }
            } else {
                var = new_gvar(get_ident(var_name), ty);
                var->is_function = is_fn;
                var->sclass = sclass;
                var->align = MAX(align, ty->align);
                ns = push_namespace(var->id, symkind, ty, var_name);
                ns->var = var;
                ns->lnk = sclass & (SC_STATIC | SC_CONSTEXPR) ? LK_INTERN : LK_EXTERN;
            }

            if (ty->kind == TY_VOID) error(var_name, "variable ‘%s’ declared void", str(var_name->id));

            if (tok->kind == TK_AS) {
                gvar_initializer(&tok, tok->next, var);
                var->is_defined = true;
            }
            var->funcspec |= funcspec;
            if (var->ty->size < 0 && (var->ty->kind != TY_ARRAY && var->ty->kind != TY_VLA))
                error(var_name, "variable ‘%s’ has incomplete type", str(var_name->id));
        }
        if (match(&tok, tok, TK_COMMA))
            continue;
        else if (tok->kind == TK_SEMI)
            return tok->next;
        else
            error(tok, "expected ‘;’ after top level declarator");
    note:
        diag_exit("note", ns->loc, "previous definition is here");
    }
}

// TransUnit ::= ExDecl+
Module *parse(Token *tok) {
    Module *md = emalloc(sizeof(Module));
    md->con = vnew(2, sizeof md->con[0]);
    curm = md;

    // Intern the shared identifiers once; intern() dedups, so repeated
    // parses reuse the same ids.
    id_anon = intern("", 0);
    id_func = intern("__func__", 8);
    id_function = intern("__FUNCTION__", 12);

    cont_depth = 0;
    brk_depth = 0;
    globals = NULL;

    enter_scope();
    file_scope = scope;

    while (tok->kind != TK_EOF) tok = external_declaration(tok);
    leave_scope(tok);

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
