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

static Type *declspecs(Token **rest, Token *tok, SClass *sclass, int *align, int *funcspec, Attr **attrs);
static Type *decl_suffix(Token **rest, Token *tok, Type *ty, bool is_param);
static Type *declarator(Token **rest, Token *tok, Type *ty);
static void parse_asm_name(Token **rest, Token *tok, char **name_out);
static void set_asm_name(Sym *var, char *name);
static Node *declaration(Token **rest, Token *tok, Type *ty, SClass sclass, int align, int funcspec, Attr *attrs);

static void attr_decl_apply(Attr *attrs, int *funcspec, int *align, bool gnu_only);
static void sym_attr_flags(Sym *var, Attr *attrs, bool gnu_only);
static bool is_attr_start(Token *tok);
static Token *skip_leading_attrs(Token *tok);
static bool attr_decl_then_semi(Token *tok);
static bool attr_then_typename(Token *tok);
static Token *attr_decl(Token *tok);
static Initializer *constexpr_elem(Node *node, Initializer *init);
static Node *elem_root(Node *node);
static Type *decl_attrs(Token **rest, Token *tok, Type *ty);
static void apply_postdecl_attrs(Type *ty);
static char *attr_disp_name(Attr *a);
static Attr *attr_list_gnu(Token **rest, Token *tok);
static Attr *attr_list_c23(Token **rest, Token *tok);
static void ty_prepend_attrs(Type *ty, Attr *attrs);
static Node *stmt(Token **rest, Token *tok);
static Node *compound_stmt(Token **rest, Token *tok);
static Node *expr(Token **rest, Token *tok);
static Node *assign(Token **rest, Token *tok);
static Node *cast(Token **rest, Token *tok);
static int64_t eval(Node *node);
static int64_t eval2(Node *node, uint32_t *sym);
static int64_t eval_rval(Node *node, uint32_t *sym);
Fp128 eval_fp128(Node *node);
static Int128 eval_int128(Node *node);
static void array_initializer2(Token **rest, Token *tok, Initializer *init, int i);
static void struct_initializer2(Token **rest, Token *tok, Initializer *init, Member *mem);
static Member *get_struct_member(Member *mem, Token *tok);

Node *new_node(NodeKind kind, Token *tok) {
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

// A number whose value does not fit an int64_t: an enumerator, or any other
// constant the full-width folder produced.
static Node *new_num128(Int128 val, Token *tok) {
    Node *node = new_node(ND_NUM, tok);
    node->ival = val;
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
    Int128 enum_val;
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

// The target's canonical va_list type, published as __builtin_va_list.
static Type *va_list_ty;

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

static NameSpace *push_namespace(Scope *sc, uint32_t id, SymKind kind, Type *ty, Token *loc) {
    NameSpace *ns = emalloc(sizeof(NameSpace));
    ns->id = id;
    ns->kind = kind;
    ns->ty = ty;
    ns->loc = loc;
    ns->next = sc->vars;
    sc->vars = ns;

    if (!sc->ht) {
        sc->ht_cap = 64;
        sc->ht = vnew(sc->ht_cap, sizeof(NameSpace *));
    } else if (sc->ht_n >= sc->ht_cap * 2) {
        int cap = sc->ht_cap * 2;
        NameSpace **ht = vnew(cap, sizeof(NameSpace *));
        NameSpace **tail = vnew(cap, sizeof(NameSpace *));
        for (int i = 0; i < cap; i++) ht[i] = tail[i] = NULL;
        for (int i = 0; i < sc->ht_cap; i++)
            for (NameSpace *x = sc->ht[i]; x;) {
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
        sc->ht = ht;
        sc->ht_cap = cap;
    }
    int h = id & (sc->ht_cap - 1);
    ns->hnext = sc->ht[h];
    sc->ht[h] = ns;
    sc->ht_n++;
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

// asm-name ::= ("asm" | "__asm" | "__asm__") "(" string-literal ")"
//
// GNU extension (and required by glibc's <sys/cdefs.h> __REDIRECT):
// the declarator keeps the C identifier for name lookup, but the symbol
// emitted into and referenced from the object file is the given name.
//
// glibc spells this as `__asm__ (__ASMNAME ("alias"))`, and __ASMNAME
// prepends __USER_LABEL_PREFIX__, which is empty on this target, so the
// argument looks like a run of adjacent string literals:
//
//     __asm__ ("" "__isoc23_fscanf")
//
// join_adjacent_string_literals() has already merged any such run into a
// single TK_STRLIT before parse() runs, so the name is just that token's
// string content: str(tok->id), which is also the decoded form (escape
// sequences resolved). Do not re-join tokens here -- concatenating the
// text by hand would bypass the lexer's string handling.
//
// The label is parsed before the symbol exists (it can precede a
// function *definition*, whose Sym is only created inside that branch),
// so it is returned through *name_out for the caller to attach.
static void parse_asm_name(Token **rest, Token *tok, char **name_out) {
    *name_out = NULL;
    if (tok->kind != TK_ASM) {
        *rest = tok;
        return;
    }

    tok = tok->next;
    if (tok->kind != TK_LPAREN) error(tok, "expected ‘(’ after ‘asm’");
    tok = tok->next;

    if (tok->kind != TK_STRLIT) error(tok, "expected string literal in ‘asm’ name");
    if (tok->enc_prefix != PREFIX_NONE) error(tok, "expected a plain string literal in ‘asm’ name");

    char *name = str(tok->id);
    if (!name[0]) error(tok, "expected non-empty string in ‘asm’ name");

    *rest = skip(tok->next, TK_RPAREN);
    *name_out = name;
}

// Attach a label parsed by parse_asm_name to an already-created symbol.
static void set_asm_name(Sym *var, char *name) {
    if (!var || !name) return;
    var->asm_name = name;
    register_asm_name(var->id, name);
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
    while (match(&tok, tok, TK_STAR)) {
        ty = pointer_to(ty, typequal(&tok, tok));
        ty = decl_attrs(&tok, tok, ty);
    }
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
        ty = decl_attrs(rest, *rest, ty);
        return abstract_declarator(&tok, start->next, ty, is_param);
    }

    Token *name = NULL;
    if (is_param && tok->kind == TK_IDENT) {
        name = tok;
        tok = tok->next;
    }
    ty = decl_suffix(rest, tok, ty, is_param);
    ty->name = name;
    ty = decl_attrs(rest, *rest, ty);
    return ty;
}

// TypeName ::= DeclSpecs AbsDeclr?
static Type *typename(Token **rest, Token *tok) {
    Type *ty = declspecs(&tok, tok, NULL, NULL, NULL, NULL);
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
    add_type(init->expr);
    // Convert a numeric initializer expression to the target type (a
    // double initializing an int member, ...).
    if ((is_integer(init->ty) || is_flonum(init->ty)) && (is_integer(init->expr->ty) || is_flonum(init->expr->ty)) &&
        !is_compatible(init->expr->ty, init->ty)) {
        check_asop(init->ty, init->expr, CTX_INIT);
        new_imcast(&init->expr, init->ty);
    }
}

void insert_ty(Type *ty, char *kind) {
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

// A target builds its va_list type itself, so nothing has registered the
// records it contains. They have to go through the same list every other
// record does, or the IR would name a type it never defines.
static void publish_records(Type *ty) {
    if (!ty) return;
    if (ty->kind == TY_ARRAY) ty = ty->base;
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        if (!ty->uid) insert_ty(ty, ty->kind == TY_STRUCT ? "struct" : "union");
    }
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
    // Mirror the subscript rule the parser applies (postfix `[`): an array
    // operand is subscripted directly and its pointer decay is suppressed,
    // while a pointer operand is rewritten as *(p + i). Building the
    // decay form here as well would contradict that and lose the
    // "array is the subscripted object" shape that the constant
    // evaluator relies on.
    if (lhs->ty->kind == TY_ARRAY) {
        Node *sub = new_binary(ND_SUBACCESS, lhs, new_num(desg->idx, tok), tok);
        sub->is_lvalue = lhs->is_lvalue;
        return sub;
    }
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
    // Scalar initializers load the value (record copies keep the
    // address-based memcpy form). The target conversion may wrap the
    // expression in ND_IMCAST; the lvalue (and the constexpr fold)
    // lives underneath.
    if (is_scalar(init->ty)) {
        add_type(rhs);
        Node *lval = rhs;
        if (lval->kind == ND_IMCAST && !lval->is_lvalue) lval = lval->lhs;
        // Only scalar lvalues load (arrays decay to their address).
        if (lval->is_lvalue && is_scalar(lval->ty)) {
            lvalue_convert(&lval);
            if (rhs->kind == ND_IMCAST)
                rhs->lhs = lval;
            else
                rhs = lval;
        }
    }
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
    var->init = init;
    return new_binary(ND_COMMA, lhs, rhs, tok);
}

// Mount a constexpr aggregate's initializer root onto a copy
// initializer: the children are already in place under the source root
// and follow it. NULL (an uninitialized part) keeps the zero fill.
static void mount_gvar_data(Initializer *dst, Initializer *src) {
    if (!src) return;
    *dst = *src;
}

static void eval_gvar_data(Initializer *init, Type *ty) {
    // A whole-aggregate copy from a constexpr source mounts the source's
    // children at the matching positions (the element-wise evaluation
    // below then folds their expressions).
    if (init->expr && (ty->kind == TY_ARRAY || ty->kind == TY_STRUCT || ty->kind == TY_UNION)) {
        Node *root = elem_root(init->expr);
        if (root->kind == ND_VAR && (root->var->sclass & SC_CONSTEXPR) && root->var->init) {
            Initializer *src = constexpr_elem(init->expr, root->var->init);
            mount_gvar_data(init, src);
            init->expr = NULL;
        }
    }

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

// Interned once at the top of parse(): the anonymous name for
// compiler-generated temporaries and __func__/__FUNCTION__.
static uint32_t id_anon;
static uint32_t id_func;
static uint32_t id_function;

// The one place a builtin is described. Every row names its own kind, so
// the table's order carries no meaning and cannot drift from the enum: a
// kind indexes its row directly, with BUILTIN_NONE (0) left empty. A
// BCLASS_SPECIAL row carries BT_NONE and a NULL intrinsic because
// parse_builtin_fn() builds its shape from the arguments rather than from a
// prototype. For a BCLASS_DECL row the fields after `uniform` are: the
// literal operand appended to the intrinsic call (-1 for none), how many
// operands that call takes, and how many parameters the builtin declares.
BuiltinDef builtin_defs[NUM_BUILTINFN] = {
    // Irreducible: no C prototype expresses these, so parse_builtin_fn()
    // builds their shapes from the arguments.
    [BUILTIN_FN_ALLOCA] = {"__builtin_alloca", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_ALLOCA_WITH_ALIGN] = {"__builtin_alloca_with_align", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0,
                                   0, NULL, 0},
    [BUILTIN_CONSTANT_P] = {"__builtin_constant_p", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_TYPES_COMPATIBLE_P] = {"__builtin_types_compatible_p", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1,
                                    0, 0, NULL, 0},
    [ATOMIC_STORE] = {"__c11_atomic_store", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [ATOMIC_LOAD] = {"__c11_atomic_load", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [ATOMIC_EXCHANGE] = {"__c11_atomic_exchange", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [ATOMIC_FETCH_ADD] = {"__c11_atomic_fetch_add", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [ATOMIC_FETCH_SUB] = {"__c11_atomic_fetch_sub", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [ATOMIC_FETCH_AND] = {"__c11_atomic_fetch_and", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [ATOMIC_FETCH_OR] = {"__c11_atomic_fetch_or", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [ATOMIC_FETCH_XOR] = {"__c11_atomic_fetch_xor", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [ATOMIC_COMPARE_EXCHANGE_WEAK] = {"__c11_atomic_compare_exchange_weak", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE,
                                      false, -1, 0, 0, NULL, 0},
    [ATOMIC_COMPARE_EXCHANGE_STRONG] = {"__c11_atomic_compare_exchange_strong", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE,
                                        false, -1, 0, 0, NULL, 0},
    [ATOMIC_THREAD_FENCE] = {"__c11_atomic_thread_fence", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL,
                             0},
    [ATOMIC_SIGNAL_FENCE] = {"__c11_atomic_signal_fence", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL,
                             0},
    [ATOMIC_IS_LOCK_FREE] = {"__c11_atomic_is_lock_free", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL,
                             0},

    // A byte swap: one intrinsic, one argument whose type is also the
    // result's, so the width comes from either side.
    [BUILTIN_BSWAP16] = {"__builtin_bswap16", BCLASS_DECL, "llvm.bswap.i%d", BT_USHORT, BT_USHORT, true, -1, 1, 1, NULL,
                         0},
    [BUILTIN_BSWAP32] = {"__builtin_bswap32", BCLASS_DECL, "llvm.bswap.i%d", BT_UINT, BT_UINT, true, -1, 1, 1, NULL, 0},
    [BUILTIN_BSWAP64] = {"__builtin_bswap64", BCLASS_DECL, "llvm.bswap.i%d", BT_ULLONG, BT_ULLONG, true, -1, 1, 1, NULL,
                         0},

    // Bit counting. All return int whatever the operand width, so the
    // prototype is fixed and the width comes from the operand type: the
    // argument converts to the declared parameter type first, which is what
    // makes a narrow operand count within 32 bits. clz/ctz append the
    // immarg is_zero_undef, hence two operands to the intrinsic call.
    [BUILTIN_CLZ] = {"__builtin_clz", BCLASS_DECL, "llvm.ctlz.i%d", BT_INT, BT_UINT, true, 1, 2, 1, NULL, 0},
    [BUILTIN_CLZL] = {"__builtin_clzl", BCLASS_DECL, "llvm.ctlz.i%d", BT_INT, BT_ULONG, true, 1, 2, 1, NULL, 0},
    [BUILTIN_CLZLL] = {"__builtin_clzll", BCLASS_DECL, "llvm.ctlz.i%d", BT_INT, BT_ULLONG, true, 1, 2, 1, NULL, 0},
    [BUILTIN_CTZ] = {"__builtin_ctz", BCLASS_DECL, "llvm.cttz.i%d", BT_INT, BT_UINT, true, 1, 2, 1, NULL, 0},
    [BUILTIN_CTZL] = {"__builtin_ctzl", BCLASS_DECL, "llvm.cttz.i%d", BT_INT, BT_ULONG, true, 1, 2, 1, NULL, 0},
    [BUILTIN_CTZLL] = {"__builtin_ctzll", BCLASS_DECL, "llvm.cttz.i%d", BT_INT, BT_ULLONG, true, 1, 2, 1, NULL, 0},
    [BUILTIN_POPCOUNT] = {"__builtin_popcount", BCLASS_DECL, "llvm.ctpop.i%d", BT_INT, BT_UINT, true, -1, 1, 1, NULL,
                          0},
    [BUILTIN_POPCOUNTL] = {"__builtin_popcountl", BCLASS_DECL, "llvm.ctpop.i%d", BT_INT, BT_ULONG, true, -1, 1, 1, NULL,
                           0},
    [BUILTIN_POPCOUNTLL] = {"__builtin_popcountll", BCLASS_DECL, "llvm.ctpop.i%d", BT_INT, BT_ULLONG, true, -1, 1, 1,
                            NULL, 0},

    // Variadic argument access: their shapes come from parse_builtin_fn(),
    // and irgen lowers them to the llvm.va_* intrinsics so the backend
    // expands them for the target's va_list layout.
    [BUILTIN_VA_START] = {"__builtin_va_start", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_VA_END] = {"__builtin_va_end", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_VA_ARG] = {"__builtin_va_arg", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_VA_COPY] = {"__builtin_va_copy", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},

    // Bit scanning: no intrinsic exists, so irgen expands each one. The
    // table still describes the prototype, which is what makes the argument
    // arrive at the right width and signedness for the expansion. ffs and
    // clrsb declare a signed parameter (their expansions test the sign or
    // compare against zero), parity an unsigned one.
    [BUILTIN_FFS] = {"__builtin_ffs", BCLASS_DECL, NULL, BT_INT, BT_INT, true, -1, 0, 1, NULL, 0},
    [BUILTIN_FFSL] = {"__builtin_ffsl", BCLASS_DECL, NULL, BT_INT, BT_LONG, true, -1, 0, 1, NULL, 0},
    [BUILTIN_FFSLL] = {"__builtin_ffsll", BCLASS_DECL, NULL, BT_INT, BT_LLONG, true, -1, 0, 1, NULL, 0},
    [BUILTIN_PARITY] = {"__builtin_parity", BCLASS_DECL, NULL, BT_INT, BT_UINT, true, -1, 0, 1, NULL, 0},
    [BUILTIN_PARITYL] = {"__builtin_parityl", BCLASS_DECL, NULL, BT_INT, BT_ULONG, true, -1, 0, 1, NULL, 0},
    [BUILTIN_PARITYLL] = {"__builtin_parityll", BCLASS_DECL, NULL, BT_INT, BT_ULLONG, true, -1, 0, 1, NULL, 0},
    [BUILTIN_CLRSB] = {"__builtin_clrsb", BCLASS_DECL, NULL, BT_INT, BT_INT, true, -1, 0, 1, NULL, 0},
    [BUILTIN_CLRSBL] = {"__builtin_clrsbl", BCLASS_DECL, NULL, BT_INT, BT_LONG, true, -1, 0, 1, NULL, 0},
    [BUILTIN_CLRSBLL] = {"__builtin_clrsbll", BCLASS_DECL, NULL, BT_INT, BT_LLONG, true, -1, 0, 1, NULL, 0},

    // Arithmetic with overflow reporting. Special class: the operands must
    // reach irgen at their own width, so there is no prototype to convert
    // them to -- a declared parameter would widen a short operand to int
    // and lose the width the intrinsic name is built from. The IR result is
    // { iN, i1 }, the value and an overflow flag, which is why these are
    // the only builtins whose call yields an aggregate.
    [BUILTIN_ADD_OVERFLOW] = {"__builtin_add_overflow", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL,
                              0},
    [BUILTIN_SUB_OVERFLOW] = {"__builtin_sub_overflow", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL,
                              0},
    [BUILTIN_MUL_OVERFLOW] = {"__builtin_mul_overflow", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL,
                              0},
    // The floating constant producers. A call is a constant, so there is
    // no intrinsic; nan/nans additionally take the payload string, which
    // parse_math_const() accepts and does not use.
    [BUILTIN_HUGE_VAL] = {"__builtin_huge_val", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_HUGE_VALF] = {"__builtin_huge_valf", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_HUGE_VALL] = {"__builtin_huge_vall", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_INF] = {"__builtin_inf", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_INFF] = {"__builtin_inff", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_INFL] = {"__builtin_infl", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_NANF] = {"__builtin_nanf", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_NAN] = {"__builtin_nan", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_NANL] = {"__builtin_nanl", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
};

// The array is indexed by kind and sized by the enum, so a kind cannot land
// on the wrong row. What can still go wrong is a kind left without one,
// which would be a zeroed entry -- silent rather than obvious -- and C's
// integer constant expressions cannot read an array element, so that is
// checked once at run time below rather than statically.
_Static_assert(NUM_BUILTINFN == (int)(sizeof(builtin_defs) / sizeof(builtin_defs[0])),
               "builtin_defs[] must have one row per BUILTIN_* kind");

// The table has one slot per enumerator, kind 0 (BUILTIN_NONE) included and
// left empty, so iterating it uses the enum's own bound.
#define builtin_row_count NUM_BUILTINFN

// The definitions live in cxx.h so that every stage shares one table; only
// the interned ids are filled in here, lazily, because the preprocessor
// calls is_builtin_fn() for __has_builtin before parse() runs.
// Every kind's row must be filled in. A missing one is all zeros, so its
// name is null; the table being indexed by kind means that is the only way
// a kind can end up unhandled.
static void check_builtin_rows(void) {
    static bool done;
    if (done) return;
    done = true;
    for (int k = BUILTIN_NONE + 1; k < NUM_BUILTINFN; k++)
        if (!builtin_defs[k].name) fatal("builtin_defs[] has no row for kind %d", k);
}

static bool builtin_ids_ready;

static void intern_builtin_ids(void) {
    if (builtin_ids_ready) return;
    for (size_t i = 0; i < builtin_row_count; ++i) {
        if (!builtin_defs[i].name) continue;  // BUILTIN_NONE's empty slot
        builtin_defs[i].id = intern(builtin_defs[i].name, strlen(builtin_defs[i].name));
    }
    builtin_ids_ready = true;
}

// The single scan over the table. Everything else is a projection of it,
// so a new lookup cannot drift from the others.
static size_t builtin_find(uint32_t id) {
    intern_builtin_ids();
    for (size_t i = 0; i < builtin_row_count; ++i)
        if (builtin_defs[i].name && builtin_defs[i].id == id) return i;
    return builtin_row_count;  // not a builtin
}

// The table is written in BUILTIN_* order, so a kind indexes its row.
BuiltinDef *builtin_def(int kind) {
    check_builtin_rows();
    intern_builtin_ids();
    return (kind > BUILTIN_NONE && kind < NUM_BUILTINFN && builtin_defs[kind].name) ? &builtin_defs[kind] : NULL;
}

int is_builtin_fn(uint32_t id) {
    size_t i = builtin_find(id);
    // The row's index *is* its kind; the search already rejects the empty
    // BUILTIN_NONE slot.
    return i < builtin_row_count ? (int)i : BUILTIN_NONE;
}

BuiltinClass builtin_class(int kind) {
    BuiltinDef *d = builtin_def(kind);
    return d ? d->cls : BCLASS_SPECIAL;
}

// The builtin kind of a callee, or BUILTIN_NONE. postfix() decays a
// function designator to a pointer, so the callee arrives as
// ND_IMCAST(ND_VAR) in practice; a bare ND_VAR is accepted too.
int builtin_kind_of(Node *func) {
    while (func && (func->kind == ND_IMCAST || func->kind == ND_LVTOR)) func = func->lhs;
    if (!func || func->kind != ND_VAR) return BUILTIN_NONE;
    // A builtin with no prototype carries its kind on the type, because
    // there is no injected symbol of that name to look up: the whole point
    // of such a builtin is that its arguments keep the types written at
    // the call site.
    if (func->ty && func->ty->is_builtin) return (int)func->ty->id;
    Sym *sym = func->var;
    // Both flags matter: a user function declared under a builtin's name is
    // an ordinary function and must not be lowered to the LLVM intrinsic.
    if (!sym || !sym->is_function || !sym->is_builtin) return BUILTIN_NONE;
    return is_builtin_fn(sym->id);
}

static bool is_const_expr(Node *node) {
    node = fold_node(node);
    if (node->kind == ND_NUM) return true;
    // A constexpr variable or an element of a constexpr aggregate is
    // usable in constant expressions (C23 6.6).
    Node *root = node;
    while (root->kind == ND_SUBACCESS || root->kind == ND_MEMBER) root = root->lhs;
    return root->kind == ND_VAR && (root->var->sclass & SC_CONSTEXPR);
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

// The prototypes of the A-class builtins, i.e. the ones a user could
// write by hand. Because they become ordinary declarations, everything
// downstream -- argument conversion, the address-of operator, assigning
// to a function pointer, calling through that pointer -- works with no
// builtin-specific code at all.
//
// The bswap family takes and returns an N-bit unsigned integer. clang
// promotes nothing here, so the parameter type is what performs the
// conversion for `__builtin_bswap16(short)`.
// Turn a BuiltinDef selector into the target's canonical Type.
static Type *builtin_target_type(int sel) {
    switch (sel) {
        case BT_VOID:
            return T.ty_void;
        case BT_BOOL:
            return T.ty_bool;
        case BT_SHORT:
            return T.ty_short;
        case BT_USHORT:
            return T.ty_ushort;
        case BT_INT:
            return T.ty_int;
        case BT_UINT:
            return T.ty_uint;
        case BT_LONG:
            return T.ty_long;
        case BT_ULONG:
            return T.ty_ulong;
        case BT_LLONG:
            return T.ty_llong;
        case BT_ULLONG:
            return T.ty_ullong;
        case BT_VOIDPTR:
            return T.ty_voidptr;
        default:
            return NULL;
    }
}

// The declared type of an A-class builtin, built from its row: the return
// type, then `nargs` parameters. func_type() alone sets only the return
// type, and fncall() walks params and reports arity with nparam, so both
// have to be attached here.
Type *builtin_type(int kind) {
    BuiltinDef *d = builtin_def(kind);
    if (!d || d->ret == BT_NONE) return NULL;

    Type *fty = func_type(builtin_target_type(d->ret));

    Type *tail = NULL;
    for (uint32_t i = 0; i < d->nargs; i++) {
        Type *pt = copy_type(builtin_target_type(d->args));
        if (tail)
            tail = tail->next = pt;
        else
            fty->params = tail = pt;
    }
    fty->nparam = d->nargs;
    return fty;
}

// Inject a declaration for every A-class builtin into the file scope, once
// per parse(). Doing it up front avoids any dependence on where the name
// is first mentioned. The injection only happens when the identifier is
// not already declared, so a user declaration wins -- which is how C
// looks up a name, and the opposite of the old behaviour where a builtin
// always beat a user declaration.
//
// The symbols are deliberately not added to `globals`: they must not
// reach the module's function list, or dump_module() would emit a
// `declare` built from cxx's Type, whose signature need not match the
// real LLVM intrinsic. Letting LLVM auto-declare on first use yields the
// overload the call site actually needs.
static void declare_builtin(Token *tok, int kind) {
    Type *fty = builtin_type(kind);
    if (!fty) return;

    // Search enclosing scopes: a declaration made at file scope must be
    // found from inside a function, or every occurrence would inject
    // another copy into the current block.
    if (find_ident(tok, true, false)) return;  // the user declared it

    // The identifier at the use site is exactly the token to record: it
    // spells the builtin's name and points into the source, so a diagnostic
    // that goes through ty->name lands on the call rather than on a
    // synthesised location.
    fty->name = tok;

    // Declare it in the file scope, like any other function: a builtin is
    // not a block-local object, and putting it there keeps one declaration
    // per translation unit regardless of where it is first mentioned.
    Sym *sym = new_var(tok->id, fty);
    sym->is_function = true;
    sym->is_builtin = true;
    // The compiler supplies the body, so this counts as a definition: a
    // later user definition of the same name is diagnosed as a
    // redefinition instead of silently replacing (or being dropped in
    // favour of) this one. The symbol never enters the module's function
    // list, so nothing is emitted for it either way.
    sym->is_defined = true;
    push_namespace(file_scope, tok->id, SYM_FUNC, fty, tok)->var = sym;
    sym->sclass = SC_EXTERN;
}

// Builtins lower to dedicated node kinds (ND_ATOMICRMW, ND_CAS,
// ND_ALLOCA, ...) carrying the memory order; arguments are parsed with
// the normal expression grammar and stored into anonymous temps so they
// are evaluated exactly once before the atomic operation.
// Parse a va_list operand and yield its address, which is what the
// llvm.va_* intrinsics take. An array va_list (amd64) decays to a pointer
// to its first element, which is that same address; a struct or scalar one
// (arm64, rv64, rv32) needs the address taken.
static Node *va_list_addr(Token **rest, Token *tok) {
    Node *ap = assign(rest, tok);
    add_type(ap);

    // The operand may arrive already decayed (an array va_list, as on
    // amd64, converts to a pointer on the way in), so it is checked against
    // both the declared type and the type that conversion yields.
    Type *want = type_unqual(va_list_ty);
    Type *got = type_unqual(ap->ty);
    Type *decayed = want->kind == TY_ARRAY ? pointer_to(want->base, 0) : want;
    if (!is_compatible(got, want) && !is_compatible(got, decayed)) error(tok, "expected a va_list argument");

    // The intrinsics take the address of the va_list object. When that
    // object is an array or a pointer, the operand already *is* that
    // address; only a structure or scalar operand needs it taken.
    if (want->kind == TY_ARRAY || want->kind == TY_PTR) return ap;
    Node *addr = new_unary(ND_ADDR, ap, tok);
    add_type(addr);
    return addr;
}

// __builtin_huge_val{,f,l}, __builtin_inf{,f,l}, __builtin_nan{,f,l} and
// __builtin_nans{,f,l} (7.12.11.2, F.10.11). Each names one value of one
// format, so the call folds to that constant here rather than reaching
// irgen: `inf` and `huge_val` are the same value reached through two
// headers, and the two differ only in the row they are declared in.
//
// Both values are written as binary128 patterns, which is the library's
// own storage format and also a valid pattern for every narrower format:
// the sign and exponent fields sit in the same place, and the payload is
// zero, so no rounding is needed.
//
// The payload string of nan/nans is accepted and not decoded. 7.12.11.2
// leaves its interpretation implementation-defined, and the headers only
// ever pass "" (NAN and SNAN are defined as __builtin_nanf("") and
// friends), so decoding it would add a code path nothing reaches.
static Node *parse_math_const(Token **rest, Token *tok, int kind) {
    Token *start = tok;
    bool is_nan = false;
    Type *ty;
    switch (kind) {
        case BUILTIN_HUGE_VALF:
        case BUILTIN_INFF:
            ty = T.ty_float;
            break;
        case BUILTIN_HUGE_VALL:
        case BUILTIN_INFL:
            ty = T.ty_ldouble;
            break;
        case BUILTIN_NANF:
            ty = T.ty_float, is_nan = true;
            break;
        case BUILTIN_NAN:
            ty = T.ty_double, is_nan = true;
            break;
        case BUILTIN_NANL:
            ty = T.ty_ldouble, is_nan = true;
            break;
        default:
            ty = T.ty_double;
            break;  // HUGE_VAL and INF
    }

    tok = skip(tok->next, TK_LPAREN);
    if (is_nan) {
        if (tok->kind != TK_STRLIT) error(start, "\u2018%s\u2019 requires a string literal", str(start->id));
        tok = tok->next;
    }
    *rest = skip(tok, TK_RPAREN);

    Fp128 v = is_nan ? FP128_NAN : FP128_INF;

    Node *node = new_node(ND_NUM, start);
    node->ty = ty;
    node->fpval = v;
    return node;
}

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
        // The floating constant producers. Each is folded to a constant
        // right here; see parse_math_const().
        case BUILTIN_HUGE_VAL:
        case BUILTIN_HUGE_VALF:
        case BUILTIN_HUGE_VALL:
        case BUILTIN_INF:
        case BUILTIN_INFF:
        case BUILTIN_INFL:
        case BUILTIN_NANF:
        case BUILTIN_NAN:
        case BUILTIN_NANL:
            return parse_math_const(rest, tok, kind);
        case BUILTIN_VA_ARG: {
            // __builtin_va_arg(ap, type): the second operand is a type
            // name, not an expression.
            if (!cur_fn || !cur_fn->ty->is_variadic)
                error(tok, "‘__builtin_va_arg’ used in a function that is not variadic");
            tok = skip(tok->next, TK_LPAREN);
            // Same operand handling as the other variadic builtins: the
            // address of the va_list object, which is what the expansion
            // walks. gen_expr() of that address is a pointer to the
            // structure even for an array va_list, whose decay is this
            // very pointer.
            Node *ap = va_list_addr(&tok, tok);
            tok = skip(tok, TK_COMMA);
            Type *ty = typename(&tok, tok);
            *rest = skip(tok, TK_RPAREN);

            if (ty->kind == TY_ARRAY || ty->kind == TY_FUNC || ty->kind == TY_VOID)
                error(start, "invalid type in ‘__builtin_va_arg’");

            // irgen expands va_arg with three blocks (register path,
            // overflow path, join), and the block totals must agree.
            cnt_blk(3);  // ND_VA_ARG: register / overflow / join

            Node *node = new_node(ND_VA_ARG, start);
            node->lhs = ap;
            node->ty = ty;
            return node;
        }
        case BUILTIN_VA_COPY: {
            // __builtin_va_copy(dst, src). LLVM has a real intrinsic for
            // this, so the copy itself is left to the backend.
            if (!cur_fn || !cur_fn->ty->is_variadic)
                error(tok, "‘__builtin_va_copy’ used in a function that is not variadic");
            tok = skip(tok->next, TK_LPAREN);
            Node *dst_ap = va_list_addr(&tok, tok);
            tok = skip(tok, TK_COMMA);
            Node *src_ap = va_list_addr(&tok, tok);
            *rest = skip(tok, TK_RPAREN);

            Node *node = new_node(ND_VA_COPY, start);
            node->lhs = dst_ap;
            node->rhs = src_ap;
            return node;
        }
        case BUILTIN_VA_START:
        case BUILTIN_VA_END: {
            // __builtin_va_start(ap, last) and __builtin_va_end(ap). The
            // last parameter is accepted and ignored: the ABI's register
            // save area already covers the named parameters, and LLVM's
            // va_start takes only the va_list.
            if (!cur_fn || !cur_fn->ty->is_variadic)
                error(tok, "‘%s’ used in a function that is not variadic",
                      kind == BUILTIN_VA_START ? "__builtin_va_start" : "__builtin_va_end");
            tok = skip(tok->next, TK_LPAREN);
            Node *addr = va_list_addr(&tok, tok);
            if (kind == BUILTIN_VA_START && tok->kind == TK_COMMA) {
                // The second operand names the last parameter. C23 also
                // allows va_start(ap) alone, for a definition whose
                // parameter list is a bare "...": there is no named
                // parameter to pass, and the operand is only documentation
                // -- the register save area already covers the named ones.
                tok = tok->next;
                assign(&tok, tok);
            }
            *rest = skip(tok, TK_RPAREN);

            Node *node = new_node(kind == BUILTIN_VA_START ? ND_VA_START : ND_VA_END, start);
            node->lhs = addr;
            return node;
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
        case BUILTIN_ADD_OVERFLOW:
        case BUILTIN_SUB_OVERFLOW:
        case BUILTIN_MUL_OVERFLOW: {
            // __builtin_{add,sub,mul}_overflow(a, b, r). The arguments keep
            // their own types: the LLVM intrinsic is named for the
            // operand's width, so a declared prototype would be exactly
            // wrong -- a short operand converted to int would ask for the
            // i32 form, and the result would be computed at the wrong
            // width.
            //
            // The callee is a variable whose type is marked as this
            // builtin, which is how the call is recognised downstream; it
            // is not a scope lookup, so a user declaration of the same
            // name is unaffected.
            tok = skip(tok->next, TK_LPAREN);
            Node dummy, *cur = &dummy;
            for (int i = 0; i < 3; i++) {
                if (i) tok = skip(tok, TK_COMMA);
                Node *arg = assign(&tok, tok);
                add_type(arg);
                lvalue_convert(&arg);
                cur = cur->next = arg;
            }
            *rest = skip(tok, TK_RPAREN);

            Node *lhs = dummy.next;
            Node *rhs = lhs->next;
            Node *out = rhs->next;

            // The value is written through the third operand, so it has to
            // point at an object of the operation's type.
            if (!is_pointer(out->ty) || out->ty->base->kind == TY_VOID)
                error(out->tok, "third argument of ‘%s’ must be a pointer to an object", builtin_def(kind)->name);

            Type *fty = func_type(T.ty_int);
            fty->is_builtin = true;
            fty->id = kind;
            fty->name = start;

            Sym *sym = new_var(start->id, fty);
            sym->is_function = true;
            sym->is_builtin = true;

            Node *node = new_node(ND_FUNCALL, start);
            node->func = new_var_node(sym, start);
            node->args = dummy.next;
            node->narg = 3;
            node->ty = T.ty_int;
            return node;
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
        Type *ty = infer_chartype(tok);
        node = new_num(int128_to_i64(tok->ival), tok);
        node->ty = ty;
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
        int kind = is_builtin_fn(tok->id);
        if (kind && builtin_class(kind) == BCLASS_SPECIAL) {
            Node *node = parse_builtin_fn(rest, tok, kind);
            if (node) return node;
        }
        // An A-class builtin becomes an ordinary declaration, injected here
        // on first use so the lookup below finds it and the call takes the
        // normal path. Injecting lazily also means the declaration carries
        // the identifier token from this occurrence.
        if (kind) declare_builtin(tok, kind);
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
        if (sc->kind == SYM_ENUM) {
            node = new_num128(sc->enum_val, tok);
            // An enumeration constant has type int when its value is
            // representable there and the enumerated type otherwise, and an
            // enumerated type is compatible with the type it is represented
            // in. That type has to be named, not left as "enum", for the
            // arithmetic and for _Generic to see it.
            if (!int128_fits(sc->enum_val, 32, SIGNED)) {
                Type *et = sc->ty;
                if (et->size <= T.ty_int->size)
                    node->ty = et->is_unsigned ? T.ty_uint : T.ty_int;
                else if (et->size <= T.ty_long->size)
                    node->ty = et->is_unsigned ? T.ty_ulong : T.ty_long;
                else
                    node->ty = et->is_unsigned ? T.ty_ullong : T.ty_llong;
            }
        } else {
            if (sc->var->is_deprecated) warning(tok, "‘%s’ is deprecated", str(sc->var->id));
            node = new_var_node(sc->var, tok);
        }
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
            check_asop(param_ty, arg, CTX_CALL);
            // lvalue conversion must come before the cast: it wraps the
            // operand in ND_LVTOR, and integer_promotion() would otherwise
            // hide the lvalue and the load would never happen. For a record
            // it produces no load -- the caller's value *is* its address --
            // which irgen turns into a by-value argument.
            lvalue_convert(&arg);
            new_imcast(&arg, param_ty);
            param_ty = param_ty->next;
        } else if (ty->is_variadic) {
            // Default argument promotions (6.5.2.2p7): the integer
            // promotions apply to the standard integer types but never to
            // _BitInt; float and _Float32 promote to double; _Float16 and
            // _Float64 stay as they are.
            //
            // _Float32 is promoted to match clang: it warns that
            // va_arg(ap, _Float32) is undefined because "arguments will be
            // promoted to 'double'", and reading it back as double is what
            // observes clang's behaviour. Measured: promoted gives
            // va_arg(_Float32)=0 / va_arg(double)=3, exactly clang's
            // numbers; not promoting gives 3/0, which is gcc's.
            //
            // lvalue conversion must come *first*: integer_promotion()
            // wraps the operand in ND_IMCAST, which drops the is_lvalue
            // flag, so converting afterwards is a no-op and the load the
            // value needs never happens -- the cast is then applied to
            // the variable's address (an "invalid cast opcode for cast
            // from 'ptr'" in the backend). The non-variadic branch below
            // already converts before casting.
            lvalue_convert(&arg);
            if (is_integer(arg->ty)) integer_promotion(&arg);
            if (arg->ty->kind == TY_FLOAT || arg->ty->kind == TY_F32) new_imcast(&arg, T.ty_double);
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
        if (node->ty->kind == TY_ARRAY && tok->kind != TK_LBRACKET) new_imcast(&node, pointer_to(node->ty->base, 0));
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
                if (!is_integer(idx->ty)) error(start, "array subscript is not an integer");
                if (!is_pointer(node->ty) && node->ty->kind != TY_ARRAY)
                    error(start, "subscripted value is neither array nor pointer");
                if (is_funcptr(node->ty)) error(start, "subscripted value is pointer to function");
                // C2y 6.5.3.2: an array operand designates the element
                // directly (no pointer rewrite); the decay was already
                // suppressed for the subscript (6.3.3.1).
                if (node->ty->kind == TY_ARRAY) {
                    if (idx->kind == ND_NUM && int128_to_i64(idx->ival) < 0)
                        error(idx->tok, "array subscript is negative");
                    node = new_binary(ND_SUBACCESS, node, idx, start);
                    node->is_lvalue = node->lhs->is_lvalue;
                    tok = skip(tok, TK_RBRACKET);
                    continue;
                }
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
                if (ty->qual & Q_ATOMIC)
                    error(dot, "accessing a member of an atomic structure or union is undefined behavior");
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
    // GNU `__extension__` is a no-op marker that suppresses pedantic
    // diagnostics; it may prefix an expression (glibc writes
    // `__extension__ ({ ... })` for statement expressions).
    if (tok->kind == TK_EXTENSION) {
        *rest = tok->next;
        return unary(rest, tok->next);
    }

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
Fp128 eval_fp128(Node *node) {
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

// Descend a constexpr aggregate's initializer tree along an element
// access expression (ND_SUBACCESS / ND_MEMBER chains ending in ND_VAR):
// the base descends first, then the subscript indexes the array child
// and the member its member child (structs and unions alike). Returns
// the element's Initializer (NULL for uninitialized parts).
static Initializer *constexpr_elem(Node *node, Initializer *init) {
    switch (node->kind) {
        case ND_SUBACCESS: {
            Initializer *base = constexpr_elem(node->lhs, init);
            int idx = (int)eval(node->rhs);
            if (!base || !base->is_inited || idx < 0 || idx >= base->ty->len) return NULL;
            return base->child[idx];
        }
        case ND_MEMBER:
            return constexpr_elem(node->lhs, init)->child[node->member->idx];
        case ND_VAR:
            return init;
        default:
            return NULL;
    }
}

// The ND_VAR root of an element access chain.
static Node *elem_root(Node *node) {
    while (node->kind == ND_SUBACCESS || node->kind == ND_MEMBER) node = node->lhs;
    return node;
}

// Fold a constexpr variable to its initializer's constant value (C23
// 6.6). Only scalar types fold; aggregates keep their normal storage.
bool constexpr_fold(Sym *var, int64_t *val, uint32_t *sym) {
    if (!(var->sclass & SC_CONSTEXPR)) return false;
    Initializer *init = var->init;
    if (!init || !init->expr) return false;
    if (!is_integer(init->expr->ty) && !is_flonum(init->expr->ty) && !is_pointer(init->expr->ty)) return false;
    static int depth;
    if (++depth > 100) error(init->expr->tok, "not a compile-time constant");
    *sym = 0;
    *val = eval2(init->expr, sym);
    depth--;
    return true;
}

// Outcome of const_array_elem, so the caller can tell "not an element read
// of a const object" from "a constant", and, within the latter, a value
// that is zero because the initializer left the subobject implicit -- which
// is still a constant: 6.6 takes the object's value, and an omitted
// initialiser means zero.
typedef enum {
    BCE_NOT_CONST,  // the root is not a const object: the caller decides
    BCE_CONST,      // *out is the element's constant initializer
    BCE_ZERO,       // the element is implicitly initialised -> 0
} BuiltinConstElem;

// Fold an element read of a const-qualified object whose initializer is
// known, for use in a constant expression. clang folds any such object,
// not only a `constexpr` one.
//
// The access chain is collected from the outside in -- `m[1][0]` parses as
// (m[1])[0], so the collected order is outer-first while the initializer
// tree descends inner-first -- and replayed in reverse. The chain lives in
// a dynamically grown array, so its length is bounded only by memory and
// not by a fixed path buffer.
//
// *out is set for BCE_CONST only; *offset_out, when given, receives the
// byte offset from the root variable. An access that cannot be resolved
// once the root is known to be const is definitely not a constant, so it
// is diagnosed here.
static BuiltinConstElem const_array_elem(Node *node, Node **out, int64_t *offset_out) {
    struct Access {
        Node *sub;    // the ND_SUBACCESS / ND_MEMBER node
        Node *index;  // its subscript index, or NULL for a member
        Member *member;
    };

    struct Access *path = vnew(8, sizeof(struct Access));
    int depth = 0;

    for (;;) {
        if (node->kind != ND_MEMBER && node->kind != ND_SUBACCESS) break;
        path = vgrow(path, depth + 1);
        path[depth].sub = node;
        if (node->kind == ND_MEMBER) {
            path[depth].index = NULL;
            path[depth].member = node->member;
        } else {
            path[depth].index = node->rhs;
            path[depth].member = NULL;
        }
        depth++;
        node = node->lhs;
    }
    while (node->kind == ND_LVTOR) node = node->lhs;
    if (node->kind != ND_VAR || !node->var->init) return BCE_NOT_CONST;

    // Only a const-qualified object (or a constexpr one) may be read as a
    // constant. The qualifier of an array sits on its innermost element
    // type, not on the array type itself -- `const int a[3]` gives
    // a->qual == 0 while a->base->qual holds Q_CONST -- so walk down to it.
    // A string literal has its own path in the caller and never reaches
    // here.
    Type *elem = node->var->ty;
    while (elem->kind == TY_ARRAY) elem = elem->base;
    if (!(elem->qual & Q_CONST) && !(node->var->sclass & SC_CONSTEXPR)) return BCE_NOT_CONST;

    // Replay inner-first.
    Initializer *init = node->var->init;
    int64_t offset = 0;
    for (int i = depth - 1; i >= 0; i--) {
        // A subobject that is not inited at all is implicitly zero.
        if (!init->is_inited) return BCE_ZERO;
        if (path[i].index) {
            int64_t idx = eval(path[i].index);
            Type *arr = init->ty;
            if (arr->kind != TY_ARRAY || idx < 0 || idx >= arr->len)
                error(path[i].sub->tok, "initializer element is not a compile-time constant");
            init = init->child[idx];
            offset += idx * arr->base->size;
        } else {
            offset += path[i].member->offset;
            init = init->child[path[i].member->idx];
        }
    }

    if (offset_out) *offset_out = offset;
    // Inited but with no expression of its own: left implicit inside an
    // initializer that is otherwise present.
    if (!init->is_inited || !init->expr) return BCE_ZERO;
    if (!is_integer(init->expr->ty) && !is_flonum(init->expr->ty)) return BCE_NOT_CONST;

    *out = init->expr;
    return BCE_CONST;
}

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
        case ND_FUNCALL: {
            // A builtin call is a constant expression when its arguments
            // are: fold it with the same routine the optimiser uses.
            // Global initialisers reach here directly, without passing
            // through fold_ast(), so the folding cannot be left to the
            // optimiser alone.
            // add_type() below only types the call node itself; the
            // callee and the arguments need it too before the callee's
            // shape can be recognised and its argument value read.
            add_type(node->func);
            for (Node **p = &node->args; *p;) {
                Node *a = *p;
                add_type(a);
                // The optimiser folds arguments, but it runs over function
                // bodies only; a global initialiser is evaluated here and
                // never sees fold_ast(). Fold them now so a constant
                // argument is recognisable -- it usually arrives wrapped in
                // an implicit cast. fold_node() returns the replacement, so
                // it must be written back, keeping the list linked.
                Node *folded = fold_node(a);
                if (folded && folded != a) {
                    folded->next = a->next;
                    *p = folded;
                }
                p = &(*p)->next;
            }
            int kind = builtin_kind_of(node->func);
            Node *folded = kind ? fold_builtin_call(kind, node) : NULL;
            if (!folded || folded->kind != ND_NUM) error(node->tok, "not a compile-time constant");
            return int128_to_i64(folded->ival);
        }
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
            bool uns = node->lhs->ty->is_unsigned;
            switch (node->kind) {
                case ND_LT:
                    return uns ? (uint64_t)eval(node->lhs) < (uint64_t)eval(node->rhs)
                               : eval(node->lhs) < eval(node->rhs);
                case ND_LE:
                    return uns ? (uint64_t)eval(node->lhs) <= (uint64_t)eval(node->rhs)
                               : eval(node->lhs) <= eval(node->rhs);
                case ND_GT:
                    return uns ? (uint64_t)eval(node->lhs) > (uint64_t)eval(node->rhs)
                               : eval(node->lhs) > eval(node->rhs);
                default:
                    return uns ? (uint64_t)eval(node->lhs) >= (uint64_t)eval(node->rhs)
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
        case ND_NULLPTR:
            return 0;
        case ND_ADDR:
            return eval_rval(node->lhs, sym);
        case ND_SUBACCESS:
        case ND_MEMBER: {
            // An element read of a string literal: its bytes are known
            // here, so the subscript folds directly. clang accepts this
            // in a static initializer (int t[1] = { L"ab"[1] };) even
            // though a subscript of a non-constexpr array is rejected,
            // because a string literal is an object the translator
            // already knows the value of.
            if (node->kind == ND_SUBACCESS) {
                Node *base = node->lhs;
                if (base->kind == ND_LVTOR) base = base->lhs;
                if (base->kind == ND_VAR && base->var->is_str) {
                    int64_t idx = eval(node->rhs);
                    Type *elem = base->var->ty->base;
                    if (idx < 0 || idx >= base->var->ty->len - 1)
                        error(node->rhs->tok, "index %lld is out of range for a string literal", (long long)idx);
                    char *bytes = str(base->var->init_data) + idx * elem->size;
                    int64_t v = elem->size == 1 ? (int64_t)(uint8_t)bytes[0]
                                : elem->size == 2
                                    ? (int64_t)(uint16_t)((uint8_t)bytes[0] | (uint16_t)(uint8_t)bytes[1] << 8)
                                    : (int64_t)(uint32_t)((uint8_t)bytes[0] | (uint32_t)(uint8_t)bytes[1] << 8 |
                                                          (uint32_t)(uint8_t)bytes[2] << 16 |
                                                          (uint32_t)(uint8_t)bytes[3] << 24);
                    return eval_ty(v, node->ty);
                }
            }
            // An element read of a const-qualified object whose
            // initializer is known folds through the initializer tree, as
            // clang does: this is not limited to `constexpr` aggregates,
            // so a `const int a[]` subscript is a constant too.
            Node *elem = NULL;
            switch (const_array_elem(node, &elem, NULL)) {
                case BCE_CONST: {
                    uint32_t s = 0;
                    int64_t v = eval2(elem, &s);
                    if (s && sym) *sym = s;
                    return v;
                }
                case BCE_ZERO:
                    // A subobject left implicitly initialised has value
                    // zero, which is a constant (6.6).
                    return 0;
                case BCE_NOT_CONST:
                    break;
            }
            // Reaching here, the read is not a constant. An address is
            // only accepted when the result type is what array-to-pointer
            // decay produced -- a scalar member (`s.a`) is not an address,
            // and keeping this test also preserves the accepted form
            // `int *p = g.a.a;` where the member is an array.
            if (node->ty->kind != TY_ARRAY) error(node->tok, "invalid initializer");
            return eval_rval(node->lhs, sym) + node->member->offset;
        }
        case ND_VAR:
            // A constexpr variable with a scalar constant initializer is
            // usable in constant expressions (C23 6.6).
            if (node->var->sclass & SC_CONSTEXPR) {
                int64_t v;
                uint32_t s = 0;
                if (constexpr_fold(node->var, &v, &s)) {
                    if (s && sym) *sym = s;
                    return v;
                }
            }
            if (!sym) error(node->tok, "not a compile-time constant");
            if (node->var->ty->kind != TY_ARRAY && node->var->ty->kind != TY_VLA && node->var->ty->kind != TY_FUNC)
                error(node->tok, "not a compile-time constant");
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
        case ND_SUBACCESS:
            return eval_rval(node->lhs, sym) + eval(node->rhs) * node->lhs->ty->base->size;
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

// As const_expr, but keeping the whole value. An enumerator may be any value
// its underlying type holds, and when no type is fixed the width is chosen
// from the enumerators, so a value that does not fit 64 bits is an answer
// here -- unlike an array size or a case value, which have to fit size_t or
// the switch type.
static Int128 const_expr128(Token **rest, Token *tok) {
    Node *node = conditional(rest, tok);
    add_type(node);
    if (!is_integer(node->ty)) error(node->tok, "expression is not an integer constant expression");
    int bits = (node->ty->kind & TY_BITINT) ? bitint_width(node->ty) : node->ty->size * 8;
    return int128_normalize(eval_int128(node), bits, node->ty->is_unsigned ? UNSIGNED : SIGNED);
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
static Node *init_decl_list(Token **rest, Token *tok, Type *basety, SClass sclass, int align, int funcspec,
                            Attr *attrs) {
    bool is_static = sclass & SC_STATIC;
    bool is_constexpr = sclass & SC_CONSTEXPR;
    bool is_typedef = sclass & SC_TYPEDEF;

    Node dummy, *cur = &dummy;
    do {
        Token *start = tok;
        scope->vla_num = 0;
        Type *ty = declarator(&tok, tok, basety);
        Token *var_name = ty->name;
        apply_postdecl_attrs(ty);

        // GNU post-declarator attributes attach to the declaration.
        int fspec = funcspec;
        attr_decl_apply(attrs, &fspec, &align, false);
        attr_decl_apply(ty->attrs, &fspec, &align, true);

        if (ty->kind == TY_VOID) error(start, "variable ‘%s’ declared void", str(var_name->id));

        bool is_fn = ty->kind == TY_FUNC;
        if (fspec && !is_fn) {
            if (fspec & Q_NORETURN) error(tok, "‘noreturn’ can only appear on functions");
            if (fspec & Q_INLINE) error(tok, "‘inline’ can only appear on functions");
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
        NameSpace *new_ns = push_namespace(scope, id, symkind, ty, var_name);
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
        var->funcspec |= fspec;
        // GNU asm label and/or post-declarator attributes may follow the
        // declarator in either order; glibc's __REDIRECT_NTH writes
        // `proto __asm__("...") __attribute__((...))`, so parse the
        // label first and let decl_attrs() consume any trailing
        // attributes.
        {
            char *a = NULL;
            parse_asm_name(&tok, tok, &a);
            ty = decl_attrs(&tok, tok, ty);
            set_asm_name(var, a);
        }
        sym_attr_flags(var, attrs, false);
        sym_attr_flags(var, ty->attrs, true);
        if (tok->kind == TK_AS) {
            if (is_extern)
                error(var_name, "declaration of block scope identifier ‘%s’ with linkage cannot have an initializer",
                      str(var_name->id));
            // Like clang: atomic aggregates cannot be brace-initialized
            // (copy-initialization from another object stays legal).
            if ((ty->qual & Q_ATOMIC) && (ty->kind == TY_STRUCT || ty->kind == TY_UNION) &&
                tok->next->kind == TK_LBRACE)
                error(var_name, "illegal initializer type '_Atomic(%s)'", str(ty->uid));
            if (is_static) {
                gvar_initializer(&tok, tok->next, var);
            } else {
                Node *expr = lvar_initializer(&tok, tok->next, var);
                cur = cur->next = new_unary(ND_EXPR_STMT, expr, tok);
            }
        }
        if (is_constexpr) {
            // A constexpr initializer must be a constant expression.
            int64_t v;
            uint32_t s = 0;
            constexpr_fold(var, &v, &s);
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
                save_expr->ty = T.ty_voidptr;
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

    // A discarded call to a nodiscard function warns (as in clang).
    Node *call = node->lhs;
    while (call->kind == ND_LVTOR || call->kind == ND_IMCAST || call->kind == ND_EXCAST) call = call->lhs;
    if (call->kind == ND_FUNCALL) {
        Node *f = call->func;
        while (f->kind == ND_IMCAST || f->kind == ND_LVTOR) f = f->lhs;
        if (f->kind == ND_VAR && f->var->is_nodiscard)
            warning(node->lhs->tok, "ignoring return value of function ‘%s’ declared with ‘nodiscard’ attribute",
                    str(f->var->id));
    }

    *rest = skip(tok, TK_SEMI);
    return node;
}

static int cont_depth;
static int brk_depth;

// SelHead ::= Exp | Decl Exp | SimDecl
// SimDecl ::= DeclSpecs Declr "=" Init
static Node *select_head(Token **rest, Token *tok) {
    Node *node;
    if (is_typename(tok, true) || is_attr_start(tok)) {
        SClass sclass = 0;
        int align = 0;
        int funcspec = 0;
        Attr *attrs = NULL;
        Type *basety = declspecs(&tok, tok, &sclass, &align, &funcspec, &attrs);
        node = new_node(ND_DECL, tok);
        if (tok->kind != TK_SEMI) node->body = init_decl_list(&tok, tok, basety, sclass, align, funcspec, attrs);
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
    if (is_typename(tok, true) || is_attr_start(tok)) {
        if (is_attr_start(tok) && attr_decl_then_semi(tok)) {
            // AttrDecl: a standalone attribute declaration.
            node->init = new_node(ND_EXPR_STMT, tok);
            tok = attr_decl(tok);
        } else {
            SClass sclass = 0;
            int align = 0;
            int funcspec = 0;
            Attr *attrs = NULL;
            Type *basety = declspecs(&tok, tok, &sclass, &align, &funcspec, &attrs);
            node->init = declaration(&tok, tok, basety, sclass, align, funcspec, attrs);
        }
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
    // Label ::= AttrSpec* (Ident | "case" ... | "default") ":". If no
    // label follows, leave the attributes for the statement.
    if (is_attr_start(tok)) {
        Token *t = skip_leading_attrs(tok);
        bool is_label =
            (t->kind == TK_IDENT && t->next->kind == TK_COLON) || t->kind == TK_CASE || t->kind == TK_DEFAULT;
        if (!is_label) return NULL;
        while (is_attr_start(tok)) {
            Attr *list = tok->kind == TK_ATTR ? attr_list_gnu(&tok, tok) : attr_list_c23(&tok, tok);
            for (Attr *a = list; a; a = a->next)
                if (a->info && !(a->info->targets & ATTR_LABEL))
                    error(a->tok, "‘%s’ attribute cannot be applied to a label", attr_disp_name(a));
        }
    }
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
            // 6.6.2: "The value of the first constant expression shall be
            // less than or equal to the value of the second." Both gcc
            // ("empty range specified") and clang ("empty case range
            // specified") diagnose this and carry on with a case that
            // matches nothing, so this warns rather than fails.
            if (val2 < val1) warning(tk_case, "empty case range specified");
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
    // UnLabelStmt ::= AttrSpec* (PrimBlk | JmpStmt) / ExpStmt ::= AttrSpec*
    // Exp ";". Only fallthrough (and the GNU statement attributes) apply
    // to statements.
    bool has_fallthrough = false;
    if (is_attr_start(tok)) {
        Token *start = tok;
        while (is_attr_start(tok)) {
            Attr *list = tok->kind == TK_ATTR ? attr_list_gnu(&tok, tok) : attr_list_c23(&tok, tok);
            for (Attr *a = list; a; a = a->next) {
                if (!a->info) continue;
                if (!strcmp(a->info->name, "fallthrough") && (a->info->targets & ATTR_STMT)) {
                    has_fallthrough = true;
                } else if (a->is_gnu && !strcmp(a->info->name, "unused")) {
                    // GNU statement attribute: accepted.
                } else {
                    error(a->tok, "‘%s’ attribute cannot be applied to a statement", attr_disp_name(a));
                }
            }
        }
        if (has_fallthrough) {
            if (tok->kind != TK_SEMI) error(start, "‘fallthrough’ attribute only applies to empty statements");
            if (!cur_sw) error(start, "fallthrough annotation is outside switch statement");
        }
    }
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
        if (is_typename(tok, true) || (is_attr_start(tok) && attr_then_typename(tok))) {
            SClass sclass = 0;
            int align = 0;
            int funcspec = 0;
            Attr *attrs = NULL;
            Type *basety = declspecs(&tok, tok, &sclass, &align, &funcspec, &attrs);

            if (sclass & SC_TYPEDEF) {
                Type *ty = declarator(&tok, tok, basety);
                apply_postdecl_attrs(ty);
                if (tok->kind == TK_AS)
                    error(tok,
                          "illegal initializer (only variables can be "
                          "initialized)");
                push_namespace(scope, get_ident(ty->name), SYM_TYNAME, ty, ty->name);
            } else {
                cur = cur->next = declaration(&tok, tok, basety, sclass, align, funcspec, attrs);
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

// EnumSpec ::= "enum" AttrSpec* Ident? EnumTypeSpec? "{" Enumr ("," Enumr)* ","? "}"
//            | "enum" Ident EnumTypeSpec?
// Enumr    ::= Ident AttrSpec* ("=" ConstExp)?
// EnumTypeSpec ::= ":" SpecQualList
//
// Where the type specifier is given, it is the type the enum is represented
// in and the enumerators have to fit it; where it is not, the type is the
// narrowest one that holds them (C23 6.7.2.2).
static bool enum_val_fits(Type *ty, Int128 v) {
    int bits = (ty->kind & TY_BITINT) ? bitint_width(ty) : ty->size * 8;
    return int128_fits(v, bits, ty->is_unsigned ? UNSIGNED : SIGNED);
}

static Type *enum_decl(Token **rest, Token *tok) {
    tok = tok->next;
    // EnumSpec ::= "enum" AttrSpec* Ident? ...
    Attr *enum_attrs = NULL;
    while (is_attr_start(tok)) {
        Attr *list = tok->kind == TK_ATTR ? attr_list_gnu(&tok, tok) : attr_list_c23(&tok, tok);
        Attr *tail = list;
        while (tail && tail->next) tail = tail->next;
        if (tail) tail->next = enum_attrs;
        enum_attrs = list;
    }
    // Read a enum tag.
    Token *tag = NULL;
    Type *ty = NULL;
    TagNameSpace *ns;
    if (tok->kind == TK_IDENT) {
        tag = tok;
        tok = tok->next;
    }

    // EnumTypeSpec ::= ":" SpecQualList
    // SpecQualList ::= TypeSpecQual+ AttrSpec*, and a TypeSpecQual is a type
    // specifier, a type qualifier or an alignment specifier. A storage class
    // and a function specifier are none of those, and declspecs reports each
    // of them when its out-parameter is NULL.
    Type *fixed = NULL;
    Attr *fixed_attrs = NULL;
    if (tok->kind == TK_COLON) {
        Token *colon = tok;
        int align = 0;
        fixed = declspecs(&tok, tok->next, NULL, &align, NULL, &fixed_attrs);
        if (!is_integer(fixed)) diag_exit("error", colon, "the type of an enum shall be an integer type");
        // The alignment does not reach the enum: an enum with a fixed type is
        // as wide and as aligned as that type, which is what clang makes of
        // `enum E : alignas(8) int` -- a four-byte, four-aligned enum.
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
            // A redeclaration may repeat the fixed type, and then it has to
            // name the one the enum already has.
            if (fixed && ty->size > 0 && (ty->size != fixed->size || ty->is_unsigned != fixed->is_unsigned))
                diag_exit("error", tag, "the underlying type of ‘enum %s’ does not match its previous declaration",
                          str(tag->id));
            ty_prepend_attrs(ty, enum_attrs);
            return ty;
        }

        ty = enum_type();
        ty->size = -1;
        push_tag_namespace(tag->id, ty, tag);
        ty->attrs = enum_attrs;
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
    // A fixed underlying type decides the representation whatever the
    // enumerators need; without one the range below decides.
    if (fixed) {
        ty->size = fixed->size;
        ty->align = fixed->align;
        ty->is_unsigned = fixed->is_unsigned;
    }
    ty_prepend_attrs(ty, enum_attrs);
    // Attributes from the type specifier belong to the enum, not to the type
    // it names, which is shared with every other use of that type.
    if (fixed_attrs) ty_prepend_attrs(ty, fixed_attrs);

    // Read an enum-list.
    EnumVal dummy = {};
    EnumVal *cur = &dummy;
    int i = 0;
    Int128 val = int128_set_i(0);
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

        // Enumr ::= Ident AttrSpec* ("=" ConstExp)?
        Attr *enm_attrs = NULL;
        while (is_attr_start(tok)) {
            Attr *list = tok->kind == TK_ATTR ? attr_list_gnu(&tok, tok) : attr_list_c23(&tok, tok);
            Attr *tail = list;
            while (tail && tail->next) tail = tail->next;
            if (tail) tail->next = enm_attrs;
            enm_attrs = list;
        }

        if (tok->kind == TK_AS) val = const_expr128(&tok, tok->next);

        if (fixed && !enum_val_fits(fixed, val))
            diag_exit("error", enm_name, "enumerator value %lld is not representable in the type of ‘enum %s’",
                      (long long)int128_to_i64(val), tag ? str(tag->id) : "(unnamed)");

        push_namespace(scope, name, SYM_ENUM, ty, enm_name)->enum_val = val;
        EnumVal *enm = emalloc(sizeof(EnumVal));
        enm->name = enm_name;
        enm->val = val;
        val = int128_add(val, int128_set_i(1));
        enm->attrs = enm_attrs;
        cur = cur->next = enm;
    }

    // Trailing attributes (after the closing brace) apply to the type.
    Attr *trail = NULL;
    while (is_attr_start(*rest)) {
        Attr *list = (*rest)->kind == TK_ATTR ? attr_list_gnu(rest, *rest) : attr_list_c23(rest, *rest);
        Attr *tail = list;
        while (tail && tail->next) tail = tail->next;
        if (tail) tail->next = trail;
        trail = list;
    }
    ty_prepend_attrs(ty, trail);

    if (!dummy.next) error(tok, "empty enum is invalid");
    ty->enumvals = dummy.next;
    if (!fixed) enum_set_underlying(ty, dummy.next);
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
        Type *basety = declspecs(&tok, tok, NULL, &align, NULL, NULL);
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
            // packed / aligned on the member (either spelling, as in
            // clang) adjust the member layout.
            for (Attr *a = mem->ty->attrs; a; a = a->next) {
                if (!a->info || a->info->ns != ATTR_NS_GNU) continue;
                if (!strcmp(a->info->name, "packed")) {
                    mem->is_packed = true;
                } else if (!strcmp(a->info->name, "aligned") && a->args) {
                    Token *t;
                    mem->align = MAX(mem->align, (int)const_expr(&t, a->args->next));
                    mem->is_align = true;
                }
            }
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
    // packed / aligned on the record type: packed lowers every member
    // alignment to 1; an explicit aligned(N) still raises the final
    // alignment above that (as in clang).
    int attr_align = 0;
    for (Attr *a = ty->attrs; a; a = a->next) {
        if (!a->info || a->info->ns != ATTR_NS_GNU) continue;
        if (!strcmp(a->info->name, "packed")) {
            ty->is_packed = true;
        } else if (!strcmp(a->info->name, "aligned") && a->args) {
            Token *t;
            attr_align = MAX(attr_align, (int)const_expr(&t, a->args->next));
        }
    }
    // The packed / aligned attributes are consumed by the layout; they
    // must not trigger post-declarator type diagnostics later.
    Attr **ap = &ty->attrs;
    while (*ap) {
        Attr *a = *ap;
        if (a->info && a->info->ns == ATTR_NS_GNU &&
            (!strcmp(a->info->name, "packed") || !strcmp(a->info->name, "aligned")))
            *ap = a->next;
        else
            ap = &a->next;
    }

    // Bit-field layout follows gcc/clang: each bit-field lives in a
    // storage unit of its declared type's size, anchored at multiples
    // of that size (in bits); fields of different types share a unit as
    // long as they fit. `bitpos` is the absolute bit cursor; it rebases
    // at every non-bit-field member. A packed record has no unit
    // boundaries (except zero-width bit-fields, as in clang).
    ty->align = 1;
    uint64_t bitpos = 0;
    int offset = 0;
    uint32_t idx = 0;

    for (Member *mem = ty->members; mem; mem = mem->next) {
        int mem_align = (ty->is_packed || mem->is_packed) ? 1 : mem->align;
        if (mem->is_align) mem_align = mem->align;  // explicit alignment overrides packed
        ty->align = MAX(ty->align, mem_align);
        mem->idx = idx++;

        if (is_union) {
            offset = MAX(offset, mem->ty->size);
            continue;
        }

        if (mem->is_bitfield) {
            int width = mem->bit_width;
            int unit = mem->ty->size * 8;
            uint64_t s = bitpos;
            if (width == 0) {
                s = ALIGN_UP(s, unit);
            } else if (!ty->is_packed) {
                // The field must not cross its unit's boundary.
                while (s + width > (s / unit + 1) * unit) s = (s / unit + 1) * unit;
            }
            mem->offset = s / 8;
            mem->bit_offset = s % 8;
            // The access unit is the smallest one covering the field's
            // bits, so that the load stays within the record (a packed
            // field may cross the boundary of its declared type).
            mem->unit_ty = get_unit_ty(min_bytes_for_bits(mem->bit_offset + width), mem->ty->is_unsigned);
            bitpos = s + width;
            offset = bitpos / 8;
        } else {
            offset = ALIGN_UP((bitpos + 7) / 8, mem_align);
            mem->offset = offset;
            mem->unit_ty = mem->ty;
            offset += mem->ty->size;
            bitpos = (uint64_t)offset * 8;
        }
    }

    // For unions offset holds the largest member (bitpos stays 0).
    offset = MAX(offset, (int)((bitpos + 7) / 8));
    if (attr_align) ty->align = MAX(ty->align, attr_align);
    ty->size = ALIGN_UP(offset, ty->align);
}

// RecordSpec ::= Record Ident ("{" MemDecl+ "}")? | Record "{" MemDecl+ "}"
static Type *record_decl(Token **rest, Token *tok) {
    bool is_union = tok->kind == TK_UNION;
    char *ty_kind = tok->kind == TK_UNION ? "union" : "struct";
    tok = tok->next;
    // RecordSpec ::= Record AttrSpec* Ident? ...
    Attr *rec_attrs = NULL;
    while (is_attr_start(tok)) {
        Attr *list = tok->kind == TK_ATTR ? attr_list_gnu(&tok, tok) : attr_list_c23(&tok, tok);
        Attr *tail = list;
        while (tail && tail->next) tail = tail->next;
        if (tail) tail->next = rec_attrs;
        rec_attrs = list;
    }
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
            ty_prepend_attrs(ty, rec_attrs);
            return ty;
        }

        ty = struct_type(is_union);
        ty->size = -1;
        push_tag_namespace(tag->id, ty, tag);
        ty->attrs = rec_attrs;
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
    ty_prepend_attrs(ty, rec_attrs);

    struct_members(rest, tok, ty);
    // Trailing attributes (after the closing brace) apply to the type.
    Attr *trail = NULL;
    while (is_attr_start(*rest)) {
        Attr *list = (*rest)->kind == TK_ATTR ? attr_list_gnu(rest, *rest) : attr_list_c23(rest, *rest);
        Attr *tail = list;
        while (tail && tail->next) tail = tail->next;
        if (tail) tail->next = trail;
        trail = list;
    }
    ty_prepend_attrs(ty, trail);
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

// Attribute lists. The token stream reaching the parser has no
// whitespace tokens, so "[ [" and "[[]]" are detected structurally.

static bool is_attr_start(Token *tok) {
    return tok->kind == TK_ATTR || (tok->kind == TK_LBRACKET && tok->next->kind == TK_LBRACKET);
}

// Attr ::= Ident ("::" Ident)? AttrArg?, one entry of an attribute
// list. AttrArg is a sequence of balanced ( ) [ ] { } tokens; an empty
// entry ("[[]]", "[[,]]") yields an Attr with info == NULL.
static Attr *attr_entry(Token **rest, Token *tok, bool is_gnu) {
    Token *start = tok;
    AttrInfo *info = NULL;
    // Attribute names may collide with keywords: glibc spells
    // __attribute__((__const__)), and `const` is a keyword here. Keyword
    // tokens keep their interned id (keywordize only rewrites kind), so
    // the name is str(tok->id) either way; only the acceptance test has
    // to allow non-identifiers.
    if (tok->kind == TK_IDENT || tk_is_keyword(tok)) {
        char *ns = NULL;
        char *name = str(tok->id);
        tok = tok->next;
        if (tok->kind == TK_COLONCOLON) {
            ns = name;
            tok = tok->next;
            if (tok->kind != TK_IDENT && !tk_is_keyword(tok)) error(tok, "expected attribute name");
            name = str(tok->id);
            tok = tok->next;
        }
        // The GNU spelling lives in the gnu namespace; the C23 spelling
        // defaults to the standard namespace.
        info = attr_lookup(is_gnu ? "gnu" : ns, name);
        if (!info) warning(start, "unknown attribute '%s' ignored", name);
    } else if (is_gnu) {
        error(tok, "expected attribute name");
    }

    Attr *attr = emalloc(sizeof(Attr));
    attr->info = info;
    attr->tok = start;
    attr->is_gnu = is_gnu;
    if (tok->kind == TK_LPAREN) {
        attr->args = tok;
        int depth = 0;
        for (;;) {
            if (tok->kind == TK_EOF || tok->is_sol) error(start, "expected ')'");
            if (tok->kind == TK_LPAREN || tok->kind == TK_LBRACKET || tok->kind == TK_LBRACE) depth++;
            if (tok->kind == TK_RPAREN || tok->kind == TK_RBRACKET || tok->kind == TK_RBRACE)
                if (--depth == 0) break;
            tok = tok->next;
        }
        tok = tok->next;
    }
    *rest = tok;
    return attr;
}

// AttrSpec ::= "[" "[" Attr? ("," Attr?)* "]" "]"
static Attr *attr_list_c23(Token **rest, Token *tok) {
    Token *start = tok;
    tok = tok->next->next;
    Attr dummy = {}, *cur = &dummy;
    while (tok->kind != TK_RBRACKET || tok->next->kind != TK_RBRACKET) {
        if (cur != &dummy) tok = skip(tok, TK_COMMA);
        cur = cur->next = attr_entry(&tok, tok, false);
        if (tok->kind == TK_EOF || tok->is_sol) error(start, "expected ']]'");
    }
    *rest = tok->next->next;
    return dummy.next;
}

// __attribute__ ( ( Attr ("," Attr)* )? ), the GNU spelling.
static Attr *attr_list_gnu(Token **rest, Token *tok) {
    Token *start = tok;
    tok = skip(tok->next, TK_LPAREN);
    tok = skip(tok, TK_LPAREN);
    Attr dummy = {}, *cur = &dummy;
    while (tok->kind != TK_RPAREN) {
        if (cur != &dummy) tok = skip(tok, TK_COMMA);
        cur = cur->next = attr_entry(&tok, tok, true);
        if (tok->kind == TK_EOF || tok->is_sol) error(start, "expected ')'");
    }
    tok = skip(tok, TK_RPAREN);
    *rest = tok->next;
    return dummy.next;
}

// Consume attribute lists after a declarator (or after '*') and attach
// them to the type.
static Type *decl_attrs(Token **rest, Token *tok, Type *ty) {
    if (!is_attr_start(tok)) {
        *rest = tok;
        return ty;
    }
    ty = copy_type(ty);
    Attr *head = ty->attrs;
    while (is_attr_start(tok)) {
        Attr *list = tok->kind == TK_ATTR ? attr_list_gnu(&tok, tok) : attr_list_c23(&tok, tok);
        Attr *tail = list;
        while (tail && tail->next) tail = tail->next;
        if (tail) tail->next = head;
        head = list;
    }
    ty->attrs = head;
    *rest = tok;
    return ty;
}

// Prepend attributes to a type (record_decl / enum_decl attach directly
// to the freshly created type).
static void ty_prepend_attrs(Type *ty, Attr *attrs) {
    if (!attrs) return;
    Attr *tail = attrs;
    while (tail->next) tail = tail->next;
    tail->next = ty->attrs;
    ty->attrs = attrs;
}

// Attributes recognized at the declspec position in C23 spelling (as in
// clang); the GNU spelling additionally accepts all declaration
// attributes there.
static bool declspec_pos_attr(AttrInfo *info) {
    return !strcmp(info->name, "deprecated") || !strcmp(info->name, "nodiscard") ||
           !strcmp(info->name, "maybe_unused") || !strcmp(info->name, "noreturn");
}

// Merge declaration attributes into *funcspec and *align. With
// gnu_only, only __attribute__-spelled attributes apply (post-declarator
// GNU attributes attach to the declaration; C23 spellings attach to the
// type).
static void attr_decl_apply(Attr *attrs, int *funcspec, int *align, bool gnu_only) {
    for (Attr *a = attrs; a; a = a->next) {
        if (!a->info) continue;
        if (gnu_only && !a->is_gnu) continue;
        if (!strcmp(a->info->name, "noreturn")) {
            *funcspec |= Q_NORETURN;
        } else if (!strcmp(a->info->name, "aligned") && a->args) {
            Token *t;
            *align = MAX(*align, (int)const_expr(&t, a->args->next));
        }
    }
}

// The name for diagnostics: gnu attributes print with their namespace.
static char *attr_disp_name(Attr *a) {
    if (a->info && a->info->ns == ATTR_NS_GNU) return format("gnu::%s", a->info->name);
    return str(a->tok->id);
}

// Apply C23 post-declarator (type) attributes: aligned adjusts the type
// alignment; attributes that cannot apply to a type are diagnosed (as
// in clang). Packed is type-valid only before a record's layout, so it
// is rejected here too.
static void apply_postdecl_attrs(Type *ty) {
    for (Attr *a = ty->attrs; a; a = a->next) {
        if (!a->info || a->is_gnu) continue;
        if (a->info->ns == ATTR_NS_CLANG) continue;  // recognized and ignored
        if (!strcmp(a->info->name, "aligned") && a->args) {
            Token *t;
            ty->align = MAX(ty->align, (int)const_expr(&t, a->args->next));
        } else if (!strcmp(a->info->name, "packed") || !(a->info->targets & ATTR_TYPE) || ty->kind == TY_FUNC) {
            warning(a->tok, "attribute '%s' ignored, because it cannot be applied to a type", attr_disp_name(a));
        }
    }
}

// Set the per-symbol flags for declaration attributes.
static void sym_attr_flags(Sym *var, Attr *attrs, bool gnu_only) {
    for (Attr *a = attrs; a; a = a->next) {
        if (!a->info) continue;
        if (gnu_only && !a->is_gnu) continue;
        if (!strcmp(a->info->name, "deprecated"))
            var->is_deprecated = true;
        else if (!strcmp(a->info->name, "nodiscard")) {
            if (!var->is_function) warning(a->tok, "‘nodiscard’ attribute only applies to functions");
            var->is_nodiscard = true;
        } else if (!strcmp(a->info->name, "maybe_unused"))
            var->is_maybe_unused = true;
        else if (!strcmp(a->info->name, "unused"))
            var->is_unused = true;
    }
}

// Skip leading attribute lists; returns the first token after them.
static Token *skip_leading_attrs(Token *tok) {
    while (is_attr_start(tok)) {
        if (tok->kind == TK_ATTR)
            attr_list_gnu(&tok, tok);
        else
            attr_list_c23(&tok, tok);
    }
    return tok;
}

// A standalone attribute declaration: AttrSpec+ ";" with no declspecs.
static bool attr_decl_then_semi(Token *tok) { return skip_leading_attrs(tok)->kind == TK_SEMI; }

// A declaration follows the leading attribute lists (a typename).
static bool attr_then_typename(Token *tok) { return is_typename(skip_leading_attrs(tok), true); }

// AttrDecl ::= AttrSpec+ ";": applies the declaration attribute checks
// (as in clang, statement attributes and noreturn are rejected), then
// returns the token past ';'.
static Token *attr_decl(Token *tok) {
    Token *t = tok;
    while (is_attr_start(t)) {
        Attr *list = t->kind == TK_ATTR ? attr_list_gnu(&t, t) : attr_list_c23(&t, t);
        for (Attr *a = list; a; a = a->next) {
            if (!a->info) continue;
            if (a->info->targets & ATTR_STMT)
                error(a->tok, "‘%s’ attribute cannot be applied to a declaration", attr_disp_name(a));
            if (!strcmp(a->info->name, "noreturn")) error(a->tok, "‘noreturn’ attribute only applies to functions");
        }
    }
    return t->next;
}

static Type *declspecs(Token **rest, Token *tok, SClass *sclass, int *align, int *funcspec, Attr **attrs) {
    // GNU `__extension__`: a no-op marker that suppresses pedantic
    // diagnostics. cxx has no such diagnostics yet, so it is simply
    // consumed -- glibc's <stdlib.h>/<wchar.h> write
    // `__extension__ typedef struct ...`.
    while (tok->kind == TK_EXTENSION) tok = tok->next;

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

    Attr dummy_a = {}, *attr_cur = &dummy_a;
    Attr *type_attrs = NULL;
    bool seen_declspec = false;

    while (is_typename(tok, true) || is_attr_start(tok)) {
        Token *ty_tok = tok;
        if (is_attr_start(tok)) {
            Attr *list = tok->kind == TK_ATTR ? attr_list_gnu(&tok, tok) : attr_list_c23(&tok, tok);
            for (Attr *a = list; a;) {
                Attr *next = a->next;
                if (!a->info) {
                    // empty entry
                } else if (seen_declspec && !a->is_gnu) {
                    // C23 spelling after the declspecs: type attributes.
                    a->next = type_attrs;
                    type_attrs = a;
                } else if (declspec_pos_attr(a->info) || (a->is_gnu && (a->info->targets & ATTR_DECL))) {
                    // Declaration attributes (the GNU spelling accepts
                    // the full declaration attribute set, as in clang).
                    a->next = NULL;
                    if (attrs) attr_cur = attr_cur->next = a;
                } else if (a->info->ns == ATTR_NS_CLANG) {
                    // clang:: attributes are recognized and ignored.
                } else if (!seen_declspec && !a->is_gnu && a->info->ns == ATTR_NS_GNU &&
                           (a->info->targets & ATTR_TYPE) && (tok->kind == TK_STRUCT || tok->kind == TK_UNION)) {
                    error(a->tok, "misplaced attributes; expected attributes here");
                } else {
                    warning(a->tok, "unknown attribute '%s' ignored", str(a->tok->id));
                }
                a = next;
            }
            continue;
        }
        seen_declspec = true;
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
                    for (Attr *a = orig->attrs; a; a = a->next)
                        if (a->info && !strcmp(a->info->name, "deprecated"))
                            warning(tok, "‘%s’ is deprecated", str(tok->id));
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
    ty = type_qual(ty, qual);
    if (type_attrs) {
        ty = copy_type(ty);
        ty_prepend_attrs(ty, type_attrs);
    }
    if (attrs) *attrs = dummy_a.next;
    return ty;
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

    // 6.7.7.1: parameter-type-list is either a parameter-list, that list
    // followed by ", ...", or a bare "...". The two shapes are disjoint:
    // the bare form has no parameter ahead of it, so it takes no comma and
    // nothing may follow it before the ')'.
    if (tok->kind == TK_ELLIPSIS) {
        is_variadic = true;
        tok = tok->next;
    } else {
        while (tok->kind != TK_RPAREN) {
            if (cur != &dummy) tok = skip(tok, TK_COMMA);
            if (tok->kind == TK_ELLIPSIS) {
                is_variadic = true;
                tok = tok->next;
                break;
            }

            Token *start = tok;
            Type *basety = declspecs(&tok, tok, NULL, NULL, NULL, NULL);
            Type *paramty = abstract_declarator(&tok, tok, basety, true);
            apply_postdecl_attrs(paramty);
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
                error(paramty->name, "parameter ‘%.*s’ has incomplete type", paramty->name->len,
                      tok_text(paramty->name));
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
    Token *l_bracket = tok;  // for the AST dumper; see array_bracket_note
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
        if (!scope->stack_top) scope->stack_top = new_lvar(id_anon, T.ty_voidptr);
        ty->vla_cnt = new_lvar(id_anon, T.ty_ulong);
        ty->vla_len = len;
        Node *expr = new_binary(ND_AS, new_var_node(ty->vla_cnt, tok), len, tok);
        scope->vla_expr = vgrow(scope->vla_expr, scope->vla_num + 1);
        scope->vla_expr[scope->vla_num++] = expr;
    } else {
        ty = array_of(ty, eval_ice(len));
    }

    array_bracket_note(ty, l_bracket);
    ty->qual = qual;
    // is_static / is_star share a union with vla_len / vla_cnt. vla_len is
    // a pointer, so writing these two bytes would land in its upper half
    // and truncate it to 32 bits. They only describe an array declarator,
    // so a VLA must not receive them.
    if (ty->kind != TY_VLA) {
        ty->is_static = is_static;
        ty->is_star = is_star;
    }
    return ty;
}

// DeclrSuf  ::= "(" ParamList? ")" | "[" ConstExp "]"
// ParamList ::= ParamDecl ("," ParamDecl)* ("," "...")? | "..."
// ParamDecl ::= DeclSpecs Declr
static Type *decl_suffix(Token **rest, Token *tok, Type *ty, bool is_param) {
    if (tok->kind == TK_LPAREN)
        ty = func_param(&tok, tok, ty);
    else if (tok->kind == TK_LBRACKET && !is_attr_start(tok))
        ty = array_dimensions(&tok, tok, ty, is_param);

    // int arr[]()
    if ((ty->kind == TY_ARRAY || ty->kind == TY_VLA) && ty->base->kind == TY_FUNC)
        error(tok, "declaration as array of functions");
    // void foo()[]
    if (tok->kind == TK_LBRACKET && !is_attr_start(tok)) error(tok, "function cannot return array type");
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
        ty = decl_attrs(rest, *rest, ty);
        return declarator(&tok, start->next, ty);
    }

    if (tok->kind != TK_IDENT) error(tok, "expected identifier or ‘(’");
    ty = decl_suffix(rest, tok->next, ty, false);
    ty->name = tok;
    ty = decl_attrs(rest, *rest, ty);
    return ty;
}

// Decl ::= DeclSpecs InitDecls? ";"
static Node *declaration(Token **rest, Token *tok, Type *basety, SClass sclass, int align, int funcspec, Attr *attrs) {
    Node *node = new_node(ND_DECL, tok);
    if (tok->kind == TK_SEMI) {
        if (sclass & SC_CONSTEXPR) error(tok, "‘constexpr’ requires an initialized data declaration");
        *rest = tok->next;
        return node;
    }
    node->body = init_decl_list(&tok, tok, basety, sclass, align, funcspec, attrs);
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

    if (is_attr_start(tok) && attr_decl_then_semi(tok)) {
        // AttrDecl: a standalone attribute declaration.
        return attr_decl(tok);
    }

    SClass sclass = 0;
    int align = 0;
    int funcspec = 0;
    Attr *attrs = NULL;
    Type *basety = declspecs(&tok, tok, &sclass, &align, &funcspec, &attrs);
    attr_decl_apply(attrs, &funcspec, &align, false);
    if (tok->kind == TK_SEMI) return tok->next;

    int cnt = -1;
    while (1) {
        cnt++;
        Type *ty = declarator(&tok, tok, basety);
        apply_postdecl_attrs(ty);
        Token *var_name = ty->name;
        NameSpace *ns = find_ident(var_name, false, false);
        Sym *var;
        bool is_fn = ty->kind == TY_FUNC;
        // GNU post-declarator attributes attach to the declaration.
        int fspec = funcspec;
        attr_decl_apply(ty->attrs, &fspec, &align, true);
        if (fspec && !is_fn) {
            if (fspec & Q_NORETURN) error(tok, "‘noreturn’ can only appear on functions");
            if (fspec & Q_INLINE) error(tok, "‘inline’ can only appear on functions");
        }

        // GNU asm label, e.g. `int f(void) __asm__("g");` or before a
        // function definition's body. Parsed here because it applies to
        // both branches and the Sym may not exist yet.
        char *asm_name = NULL;
        parse_asm_name(&tok, tok, &asm_name);

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
                    if (is_builtin_fn(var_name->id))
                        diag("error", var_name, "definition of builtin function ‘%s’", str(var_name->id));
                    else
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
                ns = push_namespace(scope, var->id, SYM_FUNC, ty, var_name);
                ns->var = var;
                ns->lnk = sclass == SC_STATIC ? LK_INTERN : LK_EXTERN;
                var->is_function = true;
                var->sclass = sclass;
            }

            // asm("name") may follow the declarator of a function
            // definition, optionally followed by attributes:
            //   int f(void) __asm__("g") __attribute__((noreturn)) { }
            ty = decl_attrs(&tok, tok, ty);
            set_asm_name(var, asm_name);

            var->is_defined = true;
            var->funcspec |= fspec;
            sym_attr_flags(var, attrs, false);
            sym_attr_flags(var, ty->attrs, true);
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
                Sym *pvar = new_lvar(id, param);
                sym_attr_flags(pvar, param->attrs, true);
                push_namespace(scope, id, SYM_VAR, ty, param->name)->var = pvar;
                param = param->next;
            }

            //  "__func__" is automatically defined as if
            // static const char __func__[] = "function-name";
            // [GNU] "__FUNCTION__" is yet another name of "__func__".
            Type *fn_name = array_of(T.ty_char, str_len(var->id) + 1);

            NameSpace *tmp = push_namespace(scope, id_func, SYM_VAR, fn_name, var_name);
            NameSpace *tmp2 = push_namespace(scope, id_function, SYM_VAR, fn_name, var_name);

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
                push_namespace(scope, get_ident(var_name), SYM_TYNAME, ty, var_name);
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
                ns = push_namespace(scope, var->id, symkind, ty, var_name);
                ns->var = var;
                ns->lnk = sclass & (SC_STATIC | SC_CONSTEXPR) ? LK_INTERN : LK_EXTERN;
            }

            // asm("name") for a file-scope object or function
            // declaration, optionally followed by attributes:
            //   extern int fscanf(...) __asm__("__isoc23_fscanf") __wur;
            //   int x __asm__("y") = 7;
            ty = decl_attrs(&tok, tok, ty);
            set_asm_name(var, asm_name);

            if (ty->kind == TY_VOID) error(var_name, "variable ‘%s’ declared void", str(var_name->id));

            if (tok->kind == TK_AS) {
                // Like clang: atomic aggregates cannot be brace-initialized.
                if ((ty->qual & Q_ATOMIC) && (ty->kind == TY_STRUCT || ty->kind == TY_UNION) &&
                    tok->next->kind == TK_LBRACE)
                    error(var_name, "illegal initializer type '_Atomic(%s)'", str(ty->uid));
                gvar_initializer(&tok, tok->next, var);
                var->is_defined = true;
                if (sclass & SC_CONSTEXPR) {
                    // A constexpr initializer must be a constant expression.
                    int64_t v;
                    uint32_t s = 0;
                    constexpr_fold(var, &v, &s);
                }
            }
            var->funcspec |= fspec;
            sym_attr_flags(var, attrs, false);
            sym_attr_flags(var, ty->attrs, true);
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

    // Publish the target's va_list under the name stdarg.h uses. The
    // layout itself stays in the target: the header only says
    // `typedef __builtin_va_list va_list;`, and the variadic builtins
    // check their operand against this type by ordinary compatibility.
    va_list_ty = T.va_list_type();
    publish_records(va_list_ty);
    // The records a variadic call may need to spell an aggregate argument
    // with. Their set is finite and fixed, so they are built here, next to
    // va_list, where insert_ty still feeds the list the module dumps.
    if (T.classify_publish) T.classify_publish();
    // The name has no source spelling, so the location carried by the entry
    // is used for diagnostics that mention it.
    push_namespace(file_scope, intern("__builtin_va_list", 17), SYM_TYNAME, va_list_ty, tok);

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
