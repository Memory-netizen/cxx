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
static void apply_alias_attr(Sym *var, Attr *attrs);
static uint32_t new_unique_varname(uint32_t id);
static Node *declaration(Token **rest, Token *tok, Type *ty, SClass sclass, int align, int funcspec, Attr *attrs);

static void attr_decl_apply(Attr *attrs, int *funcspec, int *align, bool gnu_only);
static Node *parse_mem_builtin(Token **rest, Token *tok, int kind);
static Attr *find_noreturn_attr(Attr *attrs);
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
static void strip_cleanup_attr(Type *ty);
static void warn_cleanup_attrs(Attr *attrs);
static char *attr_disp_name(Attr *a);
static Attr *attr_list_gnu(Token **rest, Token *tok);
static Attr *attr_list_c23(Token **rest, Token *tok);
static void ty_prepend_attrs(Type *ty, Attr *attrs);
static Node *stmt(Token **rest, Token *tok);
static Node *compound_stmt(Token **rest, Token *tok);
static Node *expr(Token **rest, Token *tok);
static Node *assign(Token **rest, Token *tok);
static Node *cast(Token **rest, Token *tok);
static Node *new_excast(Node *expr, Type *ty, Token *tok);
static int64_t sizeof_value(Type *ty);
static Node *init_rvalue(Node *expr);
static void designation(Token **rest, Token *tok, Initializer *init);
static void designation_range(Token **rest, Token *tok, Initializer *init, int begin, int end);
// True while a static initializer is being parsed: its expressions have to be
// constants, so a range designator has no temporary to evaluate into -- and
// needs none, since every element gets the same constant.
static bool static_init_ctx;

// Statements a function runs before its body. A variable length array's
// declaration needs a slot of its own (see the VLA case in the declarator
// loop) and that slot starts NULL; the prologue is the one place outside any
// loop where that can be stored. Filled while the body is parsed, prepended
// to it when the definition is complete.
static Node *fn_prologue_first, *fn_prologue_last;

// The reuse guards of the VLA declarations in the function being parsed, and
// how many there were. With a single one the guard stands; with more the
// objects of the other declarations are alive behind it, so the guards are
// turned off once the count is known (see the end of a function definition).
static Node **fn_vla_guards;
static int fn_vla_guard_num;
static int fn_vla_decls;
static int64_t eval(Node *node);
static int64_t eval2(Node *node, uint32_t *sym);
static int64_t eval_rval(Node *node, uint32_t *sym);
Fp128 eval_fp128(Node *node);
static Int128 eval_int128(Node *node);
static void array_initializer2(Token **rest, Token *tok, Initializer *init, int i, bool comma);
static void struct_initializer2(Token **rest, Token *tok, Initializer *init, Member *mem, bool comma);
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
        // 6.4.4.2: the constant is rounded to its own type, and one that
        // overflows it becomes an infinity. The lexer parsed the text with
        // fp128_set_str(), which cannot report that, so the check is here.
        // An infinity can only come from an overflowing literal: the
        // infinities of <math.h> are identifiers, not constants.
        if (fp128_is_inf(tok->fpval))
            warning(WG_LITERAL_RANGE, tok, "magnitude of floating-point constant too large for type \u2018%s\u2019",
                    diag_ty_name(node->ty));
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

// __attribute__((cleanup(f))): an automatic object and the handler that is
// called with its address when the scope holding it is left.
typedef struct Cleanup Cleanup;
struct Cleanup {
    Sym *var;
    Sym *fn;
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

    // The objects of this scope that carry __attribute__((cleanup(f))), in
    // declaration order. They are destroyed in the reverse, which is the
    // order `cleanup_scope_chain()` builds.
    Cleanup *cleanups;
    int cleanup_num;
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

// The stack pointer a scope saved, put back: the storage of the variable
// length arrays it declared is released by this. It runs where a cleanup
// handler of the same scope runs, and after it -- a handler may still name
// the array.
static Node *scope_sp_release(Scope *sc, Token *tok) {
    if (!sc->sp_saved) return NULL;
    Node *node = new_var_node(sc->stack_top, tok);
    add_type(node);
    lvalue_convert(&node);
    node = new_unary(ND_SP_RESTORE, node, tok);
    node->ty = T.ty_void;
    return node;
}

static Node *leave_scope(Token *tok) {
    Node *node = scope_sp_release(scope, tok);
    scope = scope->next;
    return node;
}

// [GNU] __label__ declares labels whose scope is the block they appear in, so
// that two blocks -- or two expansions of a macro carrying one in a statement
// expression -- may each define the same name. Labels are resolved through one
// function-wide list of interned ids, so a declared name is given an id of its
// own here, mangled once per declaration: the label statement, the goto and
// the labels-as-values form all ask this function for the id, and everything
// downstream (the duplicate check, goto resolution, block assignment) keeps
// working on ids that are already distinct.
//
// An entry is live while the scope that declared it is on the chain the parser
// is inside; a lookup from outside the block finds nothing, which is what the
// scope rule means. Entries whose scope has ended are dropped as they are met.
static struct {
    Scope *scp;
    uint32_t name;
    uint32_t id;
} *local_labels;
static uint32_t local_label_num;
static int local_label_seq;

static uint32_t label_id_of(uint32_t name) {
    for (uint32_t i = local_label_num; i-- > 0;) {
        if (local_labels[i].name != name) continue;
        for (Scope *s = scope; s; s = s->next)
            if (s == local_labels[i].scp) return local_labels[i].id;
        local_labels[i] = local_labels[--local_label_num];
    }
    return name;
}

// __label__ Ident ("," Ident)* ";"
static void label_decl(Token **rest, Token *tok) {
    tok = tok->next;
    for (;;) {
        if (tok->kind != TK_IDENT) error(tok, "expected an identifier in ‘__label__’");
        if (!local_labels)
            local_labels = vnew(4, sizeof(local_labels[0]));
        else
            local_labels = vgrow(local_labels, local_label_num + 1);
        local_labels[local_label_num].scp = scope;
        local_labels[local_label_num].name = tok->id;
        // An id of its own per declaration. new_unique_varname() is no use
        // here: it looks the id up among the global symbols and hands it back
        // unchanged when it finds none, which is what a label always is.
        char *mangled = format("%s.%d", str(tok->id), local_label_seq++);
        local_labels[local_label_num].id = intern(mangled, strlen(mangled));
        local_label_num++;
        tok = tok->next;
        if (tok->kind != TK_COMMA) break;
        tok = tok->next;
    }
    *rest = skip(tok, TK_SEMI);
}

static bool is_file_scope(void) { return scope == file_scope; }

// How a record is named in a diagnostic. `uid` is the name the *IR* prints,
// and it is 0 for a record the compiler built itself -- the ABI's va_list, an
// aggregate shape -- where str(0) is not a string at all but whatever the
// interning table's first slot happens to hold: that is how a member lookup
// came to report "no member named 'gp_offset' in '__INT_FAST8_TYPE__'". The
// tag is the name the program wrote, so it is asked for first.
static char *record_diag_name(Type *ty) {
    char *kind = ty->kind == TY_UNION ? "union" : "struct";
    // gcc's spelling for a record with no tag.
    if (ty->is_anon) return format("%s <anonymous>", kind);
    if (ty->id) return format("%s %s", kind, str(ty->id));
    if (ty->uid) return format("%s %s", kind, str(ty->uid));
    return format("%s <anonymous>", kind);
}

// The name of a type in a diagnostic: the tag for a record, the usual
// spelling for anything else. An incomplete type has no name token of its own,
// so every diagnostic about one has to go through here rather than dereference
// the type's `name`.
static char *diag_type_name(Type *ty) {
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) return record_diag_name(ty);
    return format("%s", diag_ty_name(ty));
}

// The spelling the cleanup diagnostic uses for a type. It is a pointer most
// of the time -- the address of the object is what the handler receives --
// and `diag_ty_name` only names scalar types, so pointers, arrays and records
// are spelled out here.
static char *cleanup_ty_str(Type *ty) {
    switch (ty->kind) {
        case TY_PTR:
            return format("%s *", cleanup_ty_str(ty->base));
        case TY_ARRAY:
            return format("%s[%u]", cleanup_ty_str(ty->base), ty->len);
        case TY_VLA:
            return format("%s[*]", cleanup_ty_str(ty->base));
        case TY_STRUCT:
        case TY_UNION:
            return record_diag_name(ty);
        default:
            return format("%s", diag_ty_name(ty));
    }
}

// Read the handler named by __attribute__((cleanup(f))). Both references want
// a plain function name -- `&h` and a function pointer are rejected -- whose
// parameter the object's address can be passed to as it stands: gcc and clang
// accept `const int *` and `void *` for an `int`, and reject `char *`, which
// is the ordinary argument compatibility rule and not a special one.
static Sym *cleanup_handler(Sym *var) {
    Attr *a = var->cleanup_attr;
    Token *tok = skip(a->args, TK_LPAREN);
    Token *arg = tok;
    Node *node = assign(&tok, tok);
    if (tok->kind != TK_RPAREN) error(a->tok, "‘cleanup’ attribute takes one argument");
    // A function designator is converted to a pointer on the way out of the
    // expression parser; that wrapper is not what was written, so it comes
    // off. An explicit `&h` is a different node and stays, which is what both
    // references reject.
    while (node->kind == ND_IMCAST || node->kind == ND_LVTOR) node = node->lhs;
    if (node->kind != ND_VAR || !node->var->is_function) {
        if (arg->kind == TK_IDENT)
            error(a->tok, "‘cleanup’ argument ‘%.*s’ is not a function", arg->len, tok_text(arg));
        error(a->tok, "‘cleanup’ argument is not a function");
    }
    Sym *fn = node->var;
    if (!fn->ty->params || fn->ty->params->next)
        error(a->tok, "‘cleanup’ function ‘%s’ must take 1 parameter", str(fn->id));
    Type *parm = fn->ty->params;
    // A variable length object has no fixed type to compare against; both
    // references accept a handler for one.
    if (!parm || var->ty->kind == TY_VLA) return fn;
    if (!is_pointer(parm) ||
        !(parm->base->kind == TY_VOID || is_compatible(type_unqual(parm->base), type_unqual(var->ty))))
        error(a->tok,
              "‘cleanup’ function ‘%s’ parameter has type ‘%s’ which is incompatible "
              "with type ‘%s’",
              str(fn->id), cleanup_ty_str(parm), cleanup_ty_str(pointer_to(var->ty, 0)));
    return fn;
}

// f(&var), typed like any other call so the ordinary argument conversion
// applies. The callee's edge in the reference graph was recorded where the
// attribute's argument was parsed, so the handler cannot be optimised away.
static Node *cleanup_call(Sym *var, Sym *fn, Token *tok) {
    Node *arg = new_unary(ND_ADDR, new_var_node(var, tok), tok);
    add_type(arg);
    Type *parm = fn->ty->params;
    if (parm) {
        check_asop(parm, arg, CTX_CALL);
        lvalue_convert(&arg);
        new_imcast(&arg, parm);
    }
    Node *callee = new_var_node(fn, tok);
    add_type(callee);
    new_imcast(&callee, pointer_to(callee->ty, 0));
    Node *call = new_node(ND_FUNCALL, tok);
    call->func = callee;
    call->args = arg;
    call->narg = 1;
    call->ty = fn->ty->ret;
    return call;
}

// Append one call, keeping the chain in evaluation order.
static Node *cleanup_add(Node *chain, Node *call, Token *tok) {
    return chain ? new_binary(ND_COMMA, chain, call, tok) : call;
}

// The handlers of one scope, most recently declared first.
static Node *cleanup_scope_chain(Scope *scp, Token *tok) {
    Node *chain = NULL;
    for (int i = scp->cleanup_num; i-- > 0;)
        chain = cleanup_add(chain, cleanup_call(scp->cleanups[i].var, scp->cleanups[i].fn, tok), tok);
    return chain;
}

// An identifier with a variably modified type. Its size expression is
// evaluated where its declaration is reached, so a jump that lands inside its
// scope without passing that point leaves the object's type without a value:
// 6.8.6.1p1 forbids that for goto, and 6.8.5.3p2 for the labels a switch
// dispatches to.
typedef struct VmDecl VmDecl;
struct VmDecl {
    VmDecl *next;
    Scope *scp;    // the scope the identifier belongs to
    uint32_t seq;  // its position among such declarations in this function
    Token *tok;    // the declared name, for the note
    bool is_typedef;
};
static VmDecl *vm_decls;
static uint32_t vm_seq;

// 6.7.6.2p? : a type derived from a variably modified type is itself variably
// modified, so a pointer to a variable length array counts as much as the
// array does.
static bool is_vm_type(Type *ty) {
    for (; ty; ty = ty->base) {
        if (ty->kind == TY_VLA) return true;
        if (ty->kind != TY_PTR && ty->kind != TY_ARRAY) return false;
    }
    return false;
}

static void note_vm_decl(Token *tok, bool is_typedef) {
    VmDecl *v = emalloc(sizeof(VmDecl));
    v->scp = scope;
    v->seq = vm_seq++;
    v->tok = tok;
    v->is_typedef = is_typedef;
    v->next = vm_decls;
    vm_decls = v;
}

// Is `outer` one of the scopes that enclose `inner` (or `inner` itself)?
static bool scope_encloses(Scope *outer, Scope *inner) {
    for (Scope *sc = inner; sc; sc = sc->next)
        if (sc == outer) return true;
    return false;
}

// 6.8.6.1p1 for a goto, 6.8.5.3p2 for a label a switch dispatches to: the jump
// may not land inside the scope of an identifier with a variably modified type
// from a point that has not initialized it. A jump that starts outside that
// scope has not; one that starts inside it has not when it starts before the
// declaration. Jumping back over such a declaration is fine -- the size
// expression ran on the way in.
static void check_vm_jump(Token *tok, char *what, Scope *from, uint32_t from_seq, Scope *to, uint32_t to_seq) {
    for (VmDecl *v = vm_decls; v; v = v->next) {
        // The label is inside the identifier's scope ...
        if (!scope_encloses(v->scp, to)) continue;
        // ... and the jump has not passed its declaration: either it starts
        // outside that scope, or it starts before the declaration inside it.
        if (scope_encloses(v->scp, from) && from_seq > v->seq) continue;
        // A label the jump reaches before the declaration skips nothing.
        if (to_seq <= v->seq) continue;
        diag("error", tok, "%s", what);
        if (v->is_typedef)
            diag_exit("note", v->tok, "jump bypasses initialization of VLA typedef ‘%s’", str(v->tok->id));
        diag_exit("note", v->tok, "jump bypasses initialization of variable length array ‘%s’", str(v->tok->id));
    }
}

// Every handler a jump leaves behind: the scopes from `from` outwards, up to
// but not including the first one that is still live where the jump lands.
// That scope's own handlers run where it ends, whichever way it was left, so
// running them here as well would run them twice. `to` of NULL leaves every
// scope, which is what a return does.
static Node *cleanup_leaving(Scope *from, Scope *to, Token *tok) {
    Node *chain = NULL;
    for (Scope *sc = from; sc && sc != file_scope; sc = sc->next) {
        if (scope_encloses(sc, to)) break;
        for (int i = sc->cleanup_num; i-- > 0;)
            chain = cleanup_add(chain, cleanup_call(sc->cleanups[i].var, sc->cleanups[i].fn, tok), tok);
        // ... and the variable length arrays that scope declared go with it:
        // their storage lives until the scope ends, and a jump out of it is
        // one of the ways it ends. Leaving this to the statement at the end
        // of the block lost the stack pointer whenever the jump went past
        // that statement -- goto, break and continue all do.
        Node *release = scope_sp_release(sc, tok);
        if (release) chain = cleanup_add(chain, release, tok);
    }
    return chain;
}

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
// GNU's other way of giving a symbol a name of its own:
//
//     static void foo_impl(void) __attribute__((alias("foo")));
//
// The declared name is another name for an object or function defined in this
// translation unit, and cxx has exactly that notion already -- a symbol whose C
// identifier differs from the name it is emitted under -- so the alias is
// attached as the emitted name, the slot `__asm__("foo")` fills. A call to the
// alias then refers to the target's definition, and the printer's rule of one
// entry per object-file name leaves a single definition in the module.
//
// The string is read the way the asm name is (see parse_asm_name): adjacent
// literals are already one TK_STRLIT, and str(tok->id) is the decoded content.
static void apply_alias_attr(Sym *var, Attr *attrs) {
    for (Attr *a = attrs; a; a = a->next) {
        if (!a->info || a->info->ns != ATTR_NS_GNU || strcmp(a->info->name, "alias")) continue;
        Token *arg = a->args ? a->args->next : NULL;
        if (!arg || arg->kind != TK_STRLIT) error(a->tok, "‘alias’ attribute requires a string literal");
        if (arg->enc_prefix != PREFIX_NONE) error(arg, "expected a plain string literal in ‘alias’");
        if (!str(arg->id)[0]) error(arg, "expected non-empty string in ‘alias’");
        set_asm_name(var, str(arg->id));
        return;
    }
}

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

// The file-scope object whose initializer is being parsed, or NULL. A name
// resolved while it is set belongs to that object: an initializer runs
// before main, so it keeps alive exactly what the object itself keeps alive.
static Sym *cur_init;

// True while the initializer being parsed belongs to an object that reaches
// the output whatever the reference graph says: a block-scope static, or a
// compound literal with static storage duration. Those initializers run at
// load time, so a name they mention is live even when nothing reaches the
// object itself -- attributing the name to a function that is dead, which is
// what happened before, left the emitted initializer pointing at a definition
// that had been dropped, and the module was refused.
static bool live_init;

// Add one edge to the reference graph. The list holds distinct symbols, so
// the scan is over the names a function mentions, not over its references.
static void add_ref(Sym *from, Sym *to) {
    for (uint32_t i = 0; i < from->num_refs; i++)
        if (from->refs[i] == to) return;
    if (!from->refs)
        from->refs = vnew(16, sizeof(Sym *));
    else
        from->refs = vgrow(from->refs, from->num_refs + 16);
    from->refs[from->num_refs++] = to;
}

// 6.5.3.4p2: the operand of sizeof is not evaluated -- unless its type is a
// variable length array, whose size expression does run. So a name that only
// a skipped operand mentions does not keep its definition alive: clang leaves
// such a definition out of the output too (and under -Wall it says so, as
// -Wunneeded-internal-declaration).
//
// While such an operand is parsed the edges the graph would gain are parked
// here instead of being added, and the decision waits for the type: an
// evaluated operand replays them, a skipped one drops them. A NULL `from` is
// the "mentioned in no body and no initializer" case, which would otherwise
// have made the name a root.
typedef struct ParkedRef ParkedRef;
struct ParkedRef {
    Sym *from;
    Sym *to;
};
static bool uneval_operand;
static ParkedRef *parked;
static uint32_t num_parked;

static void park_ref(Sym *from, Sym *to) {
    if (!parked)
        parked = vnew(8, sizeof(ParkedRef));
    else
        parked = vgrow(parked, num_parked + 8);
    parked[num_parked].from = from;
    parked[num_parked].to = to;
    num_parked++;
}

// The operand turned out to be evaluated (a variable length array): the
// edges it mentioned are real after all.
static void unpark_refs(uint32_t mark) {
    for (uint32_t i = mark; i < num_parked; i++) {
        if (parked[i].from)
            add_ref(parked[i].from, parked[i].to);
        else
            parked[i].to->is_reachable = true;
    }
    num_parked = mark;
}

// 6.7.5p8: every file scope declaration of a function updates whether its
// definition can still be an inline definition. All of them have to carry
// inline, and none may carry extern; a later declaration can take the answer
// away again, so parse() reads it once the unit is complete.
static void note_inline_decl(Sym *fn, int fspec, SClass sclass) {
    if (fn->all_decls_inline && (!(fspec & Q_INLINE) || sclass == SC_EXTERN)) fn->all_decls_inline = false;
}

// A symbol the source declared, as opposed to the ones the parser makes up
// for itself -- string literals and compound literals. Only the first kind
// has a token, and only the first kind can be diagnosed or left out.
static bool is_user_global(Sym *sym) { return sym->tok && !sym->is_str; }

// gcc and clang report an unused const object under a group of its own; an
// array of const is a const object too.
static bool is_const_object(Type *ty) {
    return (ty->qual & Q_CONST) || (ty->kind == TY_ARRAY && (ty->base->qual & Q_CONST));
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

// 6.2.7: the composite of two compatible types. Only an array can gain
// information from a second declaration -- combining an unknown length with
// a known one gives the known length, while a type that is already complete
// is unchanged. This is what lets `int a[]; int a[10];` name one object of
// type int[10] rather than staying incomplete.
static Type *composite_type(Type *old, Type *new) {
    if (old->kind != TY_ARRAY || new->kind != TY_ARRAY) return old;
    if (old->size >= 0 || new->size < 0) return old;
    return new;
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

static Node *new_num(int64_t val, Token *tok);

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
    if (is_pointer(lhs->ty) && is_integer(rhs->ty)) {
        // The index of a pointer subtraction is a ptrdiff_t. Negating in the
        // operand's own type and widening afterwards loses the sign for an
        // unsigned operand: `unsigned char *p; unsigned int n = 28;` gave
        // `p + 4294967268` because the negated i32 was zero-extended to the
        // index width. Converted first, the negation happens there.
        lvalue_convert(&rhs);
        new_imcast(&rhs, T.ty_long);
        return new_add(lhs, new_unary(ND_NEG, rhs, rhs->tok), tok);
    }

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
    // Attributes say nothing about whether a type name follows, so they are
    // skipped: a cast may spell them in front of the type it names.
    tok = skip_leading_attrs(tok);
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
        else if (tok->kind == TK_ATOMIC)
            // 6.7.3p1 lists _Atomic with the other three, and as a qualifier
            // it designates an atomic type: `int * _Atomic p` is an atomic
            // pointer. The specifier form `_Atomic(T)` sets the same bit, so
            // everything downstream reads it the same way.
            qual |= Q_ATOMIC;
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
    // An attribute may sit anywhere a declarator may: `int(ATTR *)(void)` is
    // a pointer to a function, with the attribute between the two.
    tok = skip_leading_attrs(tok);
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
    tok = skip_leading_attrs(tok);
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

// The innermost element type of an array, which is what a string literal
// fills.
static Type *array_leaf_type(Type *ty) {
    while (ty->kind == TY_ARRAY) ty = ty->base;
    return ty;
}

// Lay a string's characters into an array's innermost elements, in memory
// order, stopping after n of them. `unit` is how many bytes one of those
// elements takes, so a wide string fills them the same way.
static void fill_char_leaves(Initializer *init, int unit, char *str, int *i, int n, Token *tok) {
    if (init->ty->kind == TY_ARRAY) {
        for (int k = 0; k < init->ty->len; k++) fill_char_leaves(init->child[k], unit, str, i, n, tok);
        return;
    }
    if (*i >= n) return;
    uint32_t v = 0;
    memcpy(&v, str + (size_t)*i * unit, unit);
    init->expr = new_num(v, tok);
    (*i)++;
}

static void string_initializer(Token **rest, Token *tok, Initializer *init) {
    Type *ty = infer_strtype(tok);
    int arrlen = ty->len;
    if (init->is_flexible) *init = *new_initializer(array_of(init->ty->base, arrlen), false);

    char *string = str(tok->id);
    int len = MIN(init->ty->len, arrlen);

    // A character array of more than one dimension: the string fills the
    // whole object, innermost elements first.
    if (init->ty->base->kind == TY_ARRAY) {
        Type *leaf = array_leaf_type(init->ty);
        // One innermost array is what a string fills: `char c[2][2][2] =
        // {"ab","cd"}` puts "ab" in c[0][0] and "cd" in c[0][1], which is
        // what both references produce.
        Type *deep = init->ty;
        while (deep->base->kind == TY_ARRAY) deep = deep->base;
        int n = MIN(deep->len, arrlen);
        int i = 0;
        fill_char_leaves(init, leaf->size, string, &i, n, tok);
        *rest = tok->next;
        return;
    }

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
    // `a: 1` is the obsolete GNU spelling of `.a = 1`; both references still
    // take it, each with a warning of its own.
    bool old_style = tok->kind == TK_IDENT;
    if (old_style) {
        warning(WG_DEFAULT, tok, "obsolete field designator ‘%.*s:’", tok->len, tok_text(tok));
    } else {
        tok = skip(tok, TK_DOT);
    }
    if (tok->kind != TK_IDENT) error(tok, "expected a field designator");

    for (Member *mem = ty->members; mem; mem = mem->next) {
        // An anonymous struct or union member: its members are members of
        // this record too (6.7.2.1p13), so the designator may name one of
        // them. The token is left where it is -- the caller parses it again
        // against that member's type, which is what walks a nest of them.
        if (!mem->name) {
            // Only the member's own members: get_struct_member() walks on
            // past the end of the list it is given, so handing it the member
            // rather than that member's list made a name of a later sibling
            // look like one this member declares.
            if (is_record(mem->ty) && get_struct_member(mem->ty->members, tok)) {
                *rest = start;
                return mem;
            }
            // An unnamed bit-field declares no name a designator could use.
            continue;
        }

        // Regular struct member
        if (mem->name->id == tok->id) {
            // The colon is left in place: designation() tells the obsolete
            // spelling from the modern one by it, and the modern one has `=`.
            *rest = tok->next;
            return mem;
        }
    }

    error(tok, "struct has no such member");
    return NULL;
}

// The elements an array range designator covers. The initializer is read
// once and every element gets the value it produced: gcc gives
// `{[0 ... 1] = ++c}` the pair 1 1 rather than 1 2, which is what tinycc's
// tests2/90_struct-init.c measures. The first element carries the assignment
// and the others read the temporary it stored into.
static void designation_range(Token **rest, Token *tok, Initializer *init, int begin, int end) {
    Token *tok2 = tok;
    if (begin < end && !static_init_ctx) {
        Initializer *first = init->child[begin];
        designation(&tok2, tok, first);
        if (first->expr) {
            Node *val = init_rvalue(first->expr);
            Sym *tmp = new_lvar(intern("", 0), val->ty);
            Node *dst = new_var_node(tmp, val->tok);
            add_type(dst);
            Node *store = new_binary(ND_AS, dst, val, val->tok);
            store->ty = val->ty;
            first->pre = store;
            for (int i = begin; i <= end; i++) {
                Node *use = new_var_node(tmp, first->tok);
                add_type(use);
                lvalue_convert(&use);
                init->child[i]->expr = use;
                init->child[i]->is_inited = true;
            }
            *rest = tok2;
            return;
        }
    }
    for (int i = begin; i <= end; i++) designation(&tok2, tok, init->child[i]);
    *rest = tok2;
}

// A scalar initializer expression as a value. cxx keeps the lvalue underneath
// the conversion the target type asked for, so the load belongs here rather
// than at the assignment site -- which is where a hand-built assignment would
// otherwise store an address.
static Node *init_rvalue(Node *expr) {
    add_type(expr);
    Node *lval = expr;
    if (lval->kind == ND_IMCAST && !lval->is_lvalue) lval = lval->lhs;
    if (lval->is_lvalue && is_scalar(lval->ty)) {
        lvalue_convert(&lval);
        if (expr->kind == ND_IMCAST)
            expr->lhs = lval;
        else
            expr = lval;
    }
    return expr;
}

// Desig ::= "[" (ConstExp | ConstRangeExp) "]" | "." Ident
// The obsolete GNU field designator `a: 1`, which the initializer parsers
// have to recognize before calling designation().
static bool is_old_designator(Token *tok) { return tok->kind == TK_IDENT && tok->next->kind == TK_COLON; }

static void designation(Token **rest, Token *tok, Initializer *init) {
    if (tok->kind == TK_LBRACKET) {
        if (init->ty->kind != TY_ARRAY) error(tok, "array index in non-array initializer");
        int begin, end;
        array_designator(&tok, tok, init->ty, &begin, &end);
        Token *tok2;
        designation_range(&tok2, tok, init, begin, end);

        array_initializer2(rest, tok2, init, end + 1, true);
        return;
    }

    bool old_desig = is_old_designator(tok);
    if ((tok->kind == TK_DOT || old_desig) && init->ty->kind == TY_STRUCT) {
        Member *mem = struct_designator(&tok, tok, init->ty);
        designation(&tok, tok, init->child[mem->idx]);
        init->expr = NULL;
        struct_initializer2(rest, tok, init, mem->next, true);
        return;
    }

    if ((tok->kind == TK_DOT || old_desig) && init->ty->kind == TY_UNION) {
        Member *mem = struct_designator(&tok, tok, init->ty);
        init->mem = mem;
        designation(rest, tok, init->child[mem->idx]);
        return;
    }

    if (tok->kind == TK_DOT || old_desig) error(tok, "field name not in struct or union initializer");

    // `.a = 1` has the `=`, the obsolete `a: 1` has only the colon.
    if (tok->kind == TK_COLON)
        tok = skip(tok, TK_COLON);
    else
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
            designation_range(&tok2, tok, init, begin, end);
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

// Fill the rest of a brace the caller is already inside: the elements that
// follow an initializer which did not open one itself. `comma` says whether
// the token stands in front of a comma the caller has not consumed -- it does
// for the continuation after a designator, because the comma between two
// elements belongs to the list -- while a list opened here starts at its
// first element. When the list stops at another designator, the token handed
// back is the one before it, comma included: that is what the caller, which
// owns the list, still has to consume.
static void array_initializer2(Token **rest, Token *tok, Initializer *init, int i, bool comma) {
    if (init->is_flexible) {
        int len = count_array_init_elements(tok, init->ty);
        *init = *new_initializer(array_of(init->ty->base, len), false);
    }

    for (; i < init->ty->len && !is_end(tok); i++) {
        Token *start = tok;
        if (comma) tok = skip(tok, TK_COMMA);
        comma = true;
        if (tok->kind == TK_LBRACKET || tok->kind == TK_DOT || is_old_designator(tok)) {
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
        if (tok->kind == TK_DOT || is_old_designator(tok)) {
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

static void struct_initializer2(Token **rest, Token *tok, Initializer *init, Member *mem, bool comma) {
    for (; mem && !is_end(tok); mem = mem->next) {
        Token *start = tok;
        if (comma) tok = skip(tok, TK_COMMA);
        comma = true;
        if (tok->kind == TK_LBRACKET || tok->kind == TK_DOT || is_old_designator(tok)) {
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
        if (tok->kind == TK_DOT || is_old_designator(tok)) {
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
        // A string literal may be parenthesized: `static const char name[] =
        // (PREFIX "name")`, which is how cpython's Modules/_testsinglephase.c
        // writes it, and gcc and clang read the parentheses as if they were
        // not there. They are counted here and consumed with the literal;
        // anything else inside the parentheses leaves `paren` at zero and
        // falls through to the initializer-list paths below as before.
        int paren = 0;
        Token *str = tok;
        while (str->kind == TK_LPAREN) {
            str = str->next;
            paren++;
        }
        if (paren) {
            Token *t = str->kind == TK_STRLIT ? str->next : NULL;
            for (int i = 0; t && i < paren; i++) t = t->kind == TK_RPAREN ? t->next : NULL;
            if (!t) paren = 0;
        }
        Token *lit = paren ? str : tok;
        bool braced_str = lit->kind == TK_LBRACE && lit->next->kind == TK_STRLIT && lit->next->next->kind == TK_RBRACE;
        if (braced_str && !is_compatible(type_unqual(infer_strtype(lit->next)->base), type_unqual(init->ty->base)))
            braced_str = false;
        if (lit->kind == TK_STRLIT || braced_str) {
            tok = lit;
            bool has_brace = match(&tok, tok, TK_LBRACE);
            Type *ty = infer_strtype(tok);
            // The character type a string literal fits is the array's
            // innermost element, not the outermost array's: gcc and clang
            // take `char m[2][3] = {"abc"}` for that reason. They refuse the
            // bare `char x[2][3] = "abc"`, which cxx accepts -- a deliberate
            // divergence, recorded in the plan.
            if (!(is_char(array_leaf_type(init->ty)) && is_char(ty->base)) &&
                !is_compatible(type_unqual(ty->base), type_unqual(init->ty->base)))
                error(tok, "array of inappropriate type initialized from string constant");
            string_initializer(&tok, tok, init);
            if (has_brace) tok = skip(tok, TK_RBRACE);
            for (int i = 0; i < paren; i++) tok = skip(tok, TK_RPAREN);
            *rest = tok;
            return;
        }
    }

    if (init->ty->kind == TY_ARRAY) {
        if (tok->kind == TK_LBRACE)
            array_initializer1(rest, tok, init);
        else if (!need_brace)
            array_initializer2(rest, tok, init, 0, false);
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
            struct_initializer2(rest, tok, init, init->ty->members, false);
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
    if (!ty->id) {
        // A record the compiler built itself -- the target's va_list, an
        // argument aggregate -- was never given a name token. str(0) is
        // whatever string the preprocessor interned first, which can be a
        // file path, and a path is not an identifier the IR can carry:
        // number those instead. i counts the unnamed records already in the
        // list, so the first one becomes `struct.anon.1`.
        name = format("%s.anon.%d", kind, i + 2);
    } else if (i >= 0) {
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
    // An element or member whose initializer is empty writes nothing -- the
    // caller's ND_MEMZERO covers it -- so it adds nothing to the chain. The
    // chain is walked from its left end, one gen_expr frame per node, so
    // leaving the empty ones in makes the stack cost of an object
    // proportional to its size rather than to the number of elements
    // actually initialized: a `char path[PATH_MAX + 1]` inside a partly
    // initialized record is 4097 frames of a function that is otherwise
    // four statements long.
    if (ty->kind == TY_ARRAY) {
        Node *node = new_node(ND_NOP, tok);
        for (int i = 0; i < ty->len; i++) {
            InitDesg desg2 = {desg, i, NULL, NULL};
            Node *rhs = create_lvar_init(init->child[i], ty->base, &desg2, tok);
            Node *pre = init->child[i]->pre;
            if (pre) rhs = rhs->kind == ND_NOP ? pre : new_binary(ND_COMMA, pre, rhs, tok);
            if (rhs->kind != ND_NOP) node = new_binary(ND_COMMA, node, rhs, tok);
        }
        return node;
    }
    if (ty->kind == TY_STRUCT && !init->expr) {
        Node *node = new_node(ND_NOP, tok);

        for (Member *mem = ty->members; mem; mem = mem->next) {
            InitDesg desg2 = {desg, 0, mem, NULL};
            Node *rhs = create_lvar_init(init->child[mem->idx], mem->ty, &desg2, tok);
            add_type(rhs);
            if (rhs->kind != ND_NOP) node = new_binary(ND_COMMA, node, rhs, tok);
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
    if (is_scalar(init->ty)) rhs = init_rvalue(rhs);
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

// Set while a static initializer's expression is folded. 6.3.1.4 leaves a
// floating-to-integer conversion whose value does not fit undefined, so
// there is no value to initialise the object with and the initializer is
// refused outright -- clang does the same. Inside a function the same
// conversion is a warning, which is why the decision lives in fold_cast().
bool in_static_init;

static void eval_gvar_data(Initializer *init, Type *ty) {
    // A whole-aggregate copy from a constexpr source -- or from the object a
    // compound literal names, which is a static object of its own -- mounts
    // the source's children at the matching positions (the element-wise
    // evaluation below then folds their expressions).
    if (init->expr && (ty->kind == TY_ARRAY || ty->kind == TY_STRUCT || ty->kind == TY_UNION)) {
        Node *root = elem_root(init->expr);
        if (root->kind == ND_VAR && ((root->var->sclass & SC_CONSTEXPR) || root->var->is_compliteral) &&
            root->var->init) {
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

    // Diagnostics for constant conversions come out of folding, and a
    // global initialiser is evaluated here without ever passing through
    // fold_ast(), which walks function bodies only -- the parser already
    // notes that where it folds builtin arguments. Folding the scalar
    // expression is what makes `char c = 300;` at file scope as loud as the
    // same conversion inside a function; the bytes the data section prints
    // come from the Con built below either way.
    if (init->expr && ty->kind != TY_ARRAY && ty->kind != TY_STRUCT && ty->kind != TY_UNION) {
        in_static_init = true;
        Node *folded = fold_node(init->expr);
        in_static_init = false;
        if (folded) init->expr = folded;
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
    bool outer_static = static_init_ctx;
    static_init_ctx = true;
    Initializer *init = initializer(rest, tok, var->ty, &var->ty);
    static_init_ctx = outer_static;

    eval_gvar_data(init, var->ty);
    var->init = init;
}

static uint32_t get_ident(Token *tok) {
    if (tok->kind != TK_IDENT) error(tok, "expected identifier");
    return tok->id;
}

// Interned once at the top of parse(): the anonymous name for
// compiler-generated temporaries and __func__/__FUNCTION__/__PRETTY_FUNCTION__.
static uint32_t id_anon;
static uint32_t id_func;
static uint32_t id_function;
static uint32_t id_pretty;

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
    // Both take an i32 level and answer with a pointer. The level is an
    // immarg, so a constant is what LLVM wants; gcc asks for one too, and
    // the callers that exist (tinycc's backtrace stubs) pass literals.
    [BUILTIN_FRAME_ADDRESS] = {"__builtin_frame_address", BCLASS_DECL, "llvm.frameaddress.p0", BT_VOIDPTR, BT_UINT,
                               true, -1, 1, 1, NULL, 0},
    [BUILTIN_RETURN_ADDRESS] = {"__builtin_return_address", BCLASS_DECL, "llvm.returnaddress.p0", BT_VOIDPTR, BT_UINT,
                                true, -1, 1, 1, NULL, 0},
    [ATOMIC_STORE] = {"__c11_atomic_store", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [ATOMIC_LOAD] = {"__c11_atomic_load", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [ATOMIC_STORE_GENERIC] = {"__atomic_store", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [ATOMIC_LOAD_GENERIC] = {"__atomic_load", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [ATOMIC_COMPARE_EXCHANGE_GENERIC] = {"__atomic_compare_exchange", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1,
                                         0, 0, NULL, 0},
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
    [ATOMIC_COMPARE_EXCHANGE_N] = {"__atomic_compare_exchange_n", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0,
                                   0, NULL, 0},
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

    // llvm.expect returns its first operand and tells the backend what that
    // operand usually is. Its second operand comes from the call site, unlike
    // clz/ctz's immediate, which is why extra_arg is -1 and intrinsic_args
    // says the operand is there.
    [BUILTIN_EXPECT] = {"__builtin_expect", BCLASS_DECL, "llvm.expect.i%d", BT_LONG, BT_LONG, true, -1, 2, 2, NULL, 0},
    // The member designator is not an expression, so the shape comes from
    // parser code; the value is a constant, so irgen never sees it.
    [BUILTIN_OFFSETOF] = {"__builtin_offsetof", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_SYNC_LOCK_RELEASE] = {"__sync_lock_release", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL,
                                   0},
    [BUILTIN_SYNC_SYNCHRONIZE] = {"__sync_synchronize", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL,
                                  0},
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
    [BUILTIN_ASSUME_ALIGNED] = {"__builtin_assume_aligned", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0,
                                NULL, 0},
    [BUILTIN_UNREACHABLE] = {"__builtin_unreachable", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    // A BCLASS_SPECIAL row may carry a name that is no intrinsic: for these
    // it is the library function the builtin is, which parse_mem_builtin()
    // declares and calls.
    [BUILTIN_MEMCPY] = {"__builtin_memcpy", BCLASS_SPECIAL, "memcpy", BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_MEMMOVE] = {"__builtin_memmove", BCLASS_SPECIAL, "memmove", BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_MEMSET] = {"__builtin_memset", BCLASS_SPECIAL, "memset", BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_MEMCMP] = {"__builtin_memcmp", BCLASS_SPECIAL, "memcmp", BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
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
    [BUILTIN_NANSF] = {"__builtin_nansf", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_NANS] = {"__builtin_nans", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_NANSL] = {"__builtin_nansl", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},

    // The comparison macros (7.12.18). Their operands keep the types written
    // at the call site, which no prototype expresses: converting a float
    // operand to double first would still compare correctly but emits an
    // fpext that a direct fcmp does not need.
    [BUILTIN_ISGREATER] = {"__builtin_isgreater", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_ISGREATEREQUAL] = {"__builtin_isgreaterequal", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0,
                                NULL, 0},
    [BUILTIN_ISLESS] = {"__builtin_isless", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_ISLESSEQUAL] = {"__builtin_islessequal", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_ISLESSGREATER] = {"__builtin_islessgreater", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL,
                               0},
    [BUILTIN_ISUNORDERED] = {"__builtin_isunordered", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},

    // The classification family. One operand for most of them, six for
    // fpclassify (glibc passes the five results to choose between followed
    // by the value), so irgen lowers each by hand.
    [BUILTIN_ISNAN] = {"__builtin_isnan", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_ISINF] = {"__builtin_isinf", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_ISINF_SIGN] = {"__builtin_isinf_sign", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_ISFINITE] = {"__builtin_isfinite", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_ISNORMAL] = {"__builtin_isnormal", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_SIGNBIT] = {"__builtin_signbit", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
    [BUILTIN_FPCLASSIFY] = {"__builtin_fpclassify", BCLASS_SPECIAL, NULL, BT_NONE, BT_NONE, false, -1, 0, 0, NULL, 0},
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
// Clang accepts GCC's spelling of the atomics for the C11 builtins, and the
// arguments line up one for one: __atomic_store_n(ptr, val, order) is
// __c11_atomic_store(ptr, val, order). Real code uses the GCC spelling --
// sqlite's amalgamation calls __atomic_store_n in its mutex layer -- so these
// are aliases rather than a second set of rows; one row still describes one
// operation.
static struct {
    char *name;
    int kind;
} builtin_aliases[] = {
    {"__atomic_store_n", ATOMIC_STORE},
    {"__atomic_load_n", ATOMIC_LOAD},
    {"__atomic_exchange_n", ATOMIC_EXCHANGE},
    {"__atomic_fetch_add", ATOMIC_FETCH_ADD},
    {"__atomic_fetch_sub", ATOMIC_FETCH_SUB},
    {"__atomic_fetch_and", ATOMIC_FETCH_AND},
    {"__atomic_fetch_or", ATOMIC_FETCH_OR},
    {"__atomic_fetch_xor", ATOMIC_FETCH_XOR},
    {"__atomic_thread_fence", ATOMIC_THREAD_FENCE},
    {"__atomic_signal_fence", ATOMIC_SIGNAL_FENCE},
    // GCC spells the overflow builtins once per operation and once per type,
    // and all of them are the same three-operand operation: the operands keep
    // their own types, so the width comes from them either way. cpython's
    // bundled mimalloc calls __builtin_umull_overflow, and those calls were
    // the whole of its remaining "implicit declaration" class.
    {"__builtin_uadd_overflow", BUILTIN_ADD_OVERFLOW},
    {"__builtin_uaddl_overflow", BUILTIN_ADD_OVERFLOW},
    {"__builtin_uaddll_overflow", BUILTIN_ADD_OVERFLOW},
    {"__builtin_sadd_overflow", BUILTIN_ADD_OVERFLOW},
    {"__builtin_saddl_overflow", BUILTIN_ADD_OVERFLOW},
    {"__builtin_saddll_overflow", BUILTIN_ADD_OVERFLOW},
    {"__builtin_usub_overflow", BUILTIN_SUB_OVERFLOW},
    {"__builtin_usubl_overflow", BUILTIN_SUB_OVERFLOW},
    {"__builtin_usubll_overflow", BUILTIN_SUB_OVERFLOW},
    {"__builtin_ssub_overflow", BUILTIN_SUB_OVERFLOW},
    {"__builtin_ssubl_overflow", BUILTIN_SUB_OVERFLOW},
    {"__builtin_ssubll_overflow", BUILTIN_SUB_OVERFLOW},
    {"__builtin_umul_overflow", BUILTIN_MUL_OVERFLOW},
    {"__builtin_umull_overflow", BUILTIN_MUL_OVERFLOW},
    {"__builtin_umulll_overflow", BUILTIN_MUL_OVERFLOW},
    {"__builtin_smul_overflow", BUILTIN_MUL_OVERFLOW},
    {"__builtin_smull_overflow", BUILTIN_MUL_OVERFLOW},
    {"__builtin_smulll_overflow", BUILTIN_MUL_OVERFLOW},
};

// GCC's older atomics. Each is one of the operations above with the memory
// order written into the name instead of passed as an argument: the
// lock_test_and_set acquires, the lock_release releases, and the fetch_and_*
// family is a full barrier. The parser reads the order from here and skips the
// argument it would otherwise parse.
//
// The *_and_fetch forms are not here (they yield the new value, where the
// atomicrmw node yields the old, and the operand has to be added back --
// scaled when the object is a pointer), nor are the two compare-and-swap
// forms (they take the old value by value), nor nand (no A_* opcode).
static struct {
    char *name;
    int kind;
    int order;
} sync_aliases[] = {
    {"__sync_lock_test_and_set", ATOMIC_EXCHANGE, MEM_ORDER_ACQUIRE},
    {"__sync_fetch_and_add", ATOMIC_FETCH_ADD, MEM_ORDER_SEQ_CST},
    {"__sync_fetch_and_sub", ATOMIC_FETCH_SUB, MEM_ORDER_SEQ_CST},
    {"__sync_fetch_and_and", ATOMIC_FETCH_AND, MEM_ORDER_SEQ_CST},
    {"__sync_fetch_and_or", ATOMIC_FETCH_OR, MEM_ORDER_SEQ_CST},
    {"__sync_fetch_and_xor", ATOMIC_FETCH_XOR, MEM_ORDER_SEQ_CST},
};

// The implied memory order when `id` names one of them, -1 otherwise.
static int sync_alias_order(uint32_t id) {
    for (size_t i = 0; i < sizeof(sync_aliases) / sizeof(sync_aliases[0]); ++i)
        if (intern(sync_aliases[i].name, strlen(sync_aliases[i].name)) == id) return sync_aliases[i].order;
    return -1;
}

static size_t builtin_find(uint32_t id) {
    intern_builtin_ids();
    for (size_t i = 0; i < builtin_row_count; ++i)
        if (builtin_defs[i].name && builtin_defs[i].id == id) return i;
    static uint32_t alias_ids[sizeof(builtin_aliases) / sizeof(builtin_aliases[0])];
    for (size_t i = 0; i < sizeof(builtin_aliases) / sizeof(builtin_aliases[0]); ++i) {
        if (!alias_ids[i]) alias_ids[i] = intern(builtin_aliases[i].name, strlen(builtin_aliases[i].name));
        if (alias_ids[i] == id) return (size_t)builtin_aliases[i].kind;
    }
    static uint32_t sync_ids[sizeof(sync_aliases) / sizeof(sync_aliases[0])];
    for (size_t i = 0; i < sizeof(sync_aliases) / sizeof(sync_aliases[0]); ++i) {
        if (!sync_ids[i]) sync_ids[i] = intern(sync_aliases[i].name, strlen(sync_aliases[i].name));
        if (sync_ids[i] == id) return (size_t)sync_aliases[i].kind;
    }
    return builtin_row_count;  // not a builtin
}

// Whether this call is one of the GCC-spelled atomics, which take the address
// of an ordinary object: a name that is an alias in the table, or one of the
// generic forms that needed a row of its own because their arguments differ
// from the C11 operation's. The C11 spelling (__c11_atomic_*) is the one that
// requires an _Atomic object, and it is the only one that does.
static bool is_gcc_atomic_spelling(uint32_t id, int kind) {
    if (kind == ATOMIC_STORE_GENERIC || kind == ATOMIC_LOAD_GENERIC || kind == ATOMIC_COMPARE_EXCHANGE_GENERIC)
        return true;
    for (size_t i = 0; i < sizeof(builtin_aliases) / sizeof(builtin_aliases[0]); ++i)
        if (intern(builtin_aliases[i].name, strlen(builtin_aliases[i].name)) == id) return true;
    if (sync_alias_order(id) >= 0) return true;
    return id == intern("__atomic_compare_exchange_n", 27);
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
        warning(WG_MEMORY_ORDER, tok, "success memory order argument to atomic operation is invalid");
    else if (mode == MO_CAS_FAIL)
        warning(WG_MEMORY_ORDER, tok, "failure memory order argument to atomic operation is invalid");
    else
        warning(WG_MEMORY_ORDER, tok, "memory order argument to atomic operation is invalid");
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
// True while parsing the arguments of a builtin spelled the GCC way:
// __atomic_store_n and its relatives address a plain object, while the C11
// builtins they map to require _Atomic. The flag is read by atomic_object(),
// which every one of these builtins calls before it parses anything that
// could nest another call.
static bool gcc_atomic_args;
// The implied memory order of a __sync_* form, or -1 for everything else. Set
// once per builtin call, like the flag above.
static int sync_order;

static Node *atomic_object(Token **tok, Token *start) {
    Node *object = assign(tok, *tok);
    bool atomic = is_pointer(object->ty) && (object->ty->base->qual & Q_ATOMIC);
    if (!atomic && !(gcc_atomic_args && is_pointer(object->ty)))
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
    if (want->kind == TY_ARRAY || want->kind == TY_PTR) {
        // An operand that has already decayed -- an array va_list reaching
        // here as a pointer, which is what a forwarded one looks like --
        // has to be read: `va_list ap` as a *parameter* is exactly that,
        // because 6.7.6.3p7 adjusts the array to a pointer, so the object
        // lives in the caller and only its address is here. Without the
        // conversion gen_expr() handed irgen the address of the pointer
        // variable, and a va_list forwarded to a helper -- vprintf's shape --
        // read the wrong bytes.
        //
        // A va_list that is itself a pointer (rv32, rv64) is the other case,
        // and it must *not* be read here. There the object is the pointer
        // variable, and the linear cursor strategy loads it, takes the value
        // out of the argument area and stores the advanced pointer back into
        // it; reading it first left irgen loading through the cursor as if
        // the area held the va_list (`load void`), which is not IR.
        if (want->kind == TY_ARRAY && got->kind == TY_PTR) lvalue_convert(&ap);
        return ap;
    }
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
    bool is_snan = false;
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
        case BUILTIN_NANSF:
            ty = T.ty_float, is_nan = true, is_snan = true;
            break;
        case BUILTIN_NANS:
            ty = T.ty_double, is_nan = true, is_snan = true;
            break;
        case BUILTIN_NANSL:
            ty = T.ty_ldouble, is_nan = true, is_snan = true;
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

    /* FP128_SNAN carries its payload in the top payload bit, which is the
     * shape the converters carry into each narrower format (and the same
     * pattern gcc and clang produce). */
    Fp128 v = is_snan ? FP128_SNAN : is_nan ? FP128_NAN : FP128_INF;

    Node *node = new_node(ND_NUM, start);
    node->ty = ty;
    node->fpval = v;
    return node;
}

// The comparison macros of 7.12.18.
//
// Four of them are exactly one operator over the operands, so the call is
// rewritten to that operator and nothing downstream changes; each operand
// is evaluated once. The NaNs make the correspondence exact rather than
// approximate: the C operators already compile to the ordered predicates
// the macros specify, so `isgreater(x, y)` and `x > y` are the same
// computation, invalid-exception behaviour aside (cxx does not model
// exceptions).
//
// islessgreater and isunordered are the exceptions. islessgreater is
// defined as (x) < (y) || (x) > (y) and isunordered asks whether either
// operand is a NaN, so both need each operand in two positions. Sharing one
// node between them is not an option: gen_expr would generate it twice
// while cnt_blk() counted its blocks once at parse time, and the block
// totals have to agree. They therefore reach irgen as a call, where the
// `one` and `uno` predicates do the job with one evaluation each.
static Node *parse_math_cmp(Token **rest, Token *tok, int kind) {
    Token *start = tok;
    tok = skip(tok->next, TK_LPAREN);
    Node *x = assign(&tok, tok);
    tok = skip(tok, TK_COMMA);
    Node *y = assign(&tok, tok);
    *rest = skip(tok, TK_RPAREN);

    add_type(x);
    lvalue_convert(&x);
    add_type(y);
    lvalue_convert(&y);

    NodeKind op = ND_GT;
    bool one_operator = true;
    switch (kind) {
        case BUILTIN_ISGREATER:
            op = ND_GT;
            break;
        case BUILTIN_ISGREATEREQUAL:
            op = ND_GE;
            break;
        case BUILTIN_ISLESS:
            op = ND_LT;
            break;
        case BUILTIN_ISLESSEQUAL:
            op = ND_LE;
            break;
        default:
            one_operator = false;
            break;
    }
    if (one_operator) {
        Node *node = new_binary(op, x, y, start);
        add_type(node);
        return node;
    }

    // The two that need a predicate of their own. The conversions are the
    // ones add_type would have applied to a comparison, so a float/double
    // pair is still compared at the wider type.
    usual_arith_conv(&x, &y);
    Type *fty = func_type(T.ty_int);
    fty->is_builtin = true;
    fty->id = kind;
    fty->name = start;

    Sym *sym = new_var(start->id, fty);
    sym->is_function = true;
    sym->is_builtin = true;

    y->next = NULL;
    x->next = y;
    Node *node = new_node(ND_FUNCALL, start);
    node->func = new_var_node(sym, start);
    node->args = x;
    node->narg = 2;
    node->ty = T.ty_int;
    return node;
}

// The classification family: __builtin_isnan, __builtin_isinf,
// __builtin_isinf_sign, __builtin_isfinite, __builtin_isnormal,
// __builtin_signbit and __builtin_fpclassify (7.12.4, 7.12.3).
//
// Each of these needs its operand value in more than one comparison, so
// none of them can be an AST rewrite: sharing one node between two
// positions makes gen_expr generate it twice while cnt_blk() counted its
// blocks once, and the block totals have to agree. They travel to irgen as
// a call instead, and the operand is evaluated there once into a Ref.
//
// The first five of fpclassify are the results to choose between, so they
// are converted to int here; the value itself keeps its own type.
static Node *parse_classify(Token **rest, Token *tok, int kind) {
    Token *start = tok;
    bool is_fpclassify = kind == BUILTIN_FPCLASSIFY;

    tok = skip(tok->next, TK_LPAREN);
    Node dummy = {0};
    Node *cur = &dummy;
    int narg = 0;
    for (;;) {
        Node *arg = assign(&tok, tok);
        add_type(arg);
        lvalue_convert(&arg);
        if (is_fpclassify && narg < 5) {
            Node *c = new_unary(ND_IMCAST, arg, arg->tok);
            c->ty = T.ty_int;
            arg = c;
        }
        cur = cur->next = arg;
        narg++;
        if (tok->kind != TK_COMMA) break;
        tok = tok->next;
    }
    *rest = skip(tok, TK_RPAREN);

    if (is_fpclassify && narg != 6) error(start, "\u2018%s\u2019 requires 6 arguments", str(start->id));
    if (!is_fpclassify && narg != 1) error(start, "\u2018%s\u2019 requires one argument", str(start->id));

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
    node->narg = narg;
    node->ty = T.ty_int;
    return node;
}

static Node *parse_builtin_fn(Token **rest, Token *tok, int kind) {
    Token *start = tok;
    bool is_weak = false;
    bool is_signal = false;
    gcc_atomic_args = is_gcc_atomic_spelling(tok->id, kind);
    sync_order = sync_alias_order(tok->id);
    // Read once, before the arguments are parsed: an argument may hold another
    // builtin call, and that one sets this flag for itself.
    int this_sync_order = sync_order;
    // The __sync_* forms address a plain object, exactly as the __atomic_*
    // spellings do. __sync_lock_release is a row of its own rather than an
    // alias -- its argument list is not one of the operations above -- so it
    // is named here too.
    if (sync_order >= 0 || kind == BUILTIN_SYNC_LOCK_RELEASE) gcc_atomic_args = true;
    switch (kind) {
        case BUILTIN_TYPES_COMPATIBLE_P: {
            tok = skip(tok->next, TK_LPAREN);
            Type *type1 = typename(&tok, tok);
            tok = skip(tok, TK_COMMA);
            Type *type2 = typename(&tok, tok);
            *rest = skip(tok, TK_RPAREN);
            return new_num(is_compatible(type_unqual(type1), type_unqual(type2)), start);
        }
        case BUILTIN_SYNC_SYNCHRONIZE: {
            // A full barrier, and the only argument is the empty list. The
            // node is the one the C11 fence builtins build; a
            // sequentially consistent fence is exactly what
            // __sync_synchronize() means.
            Node *fence = new_node(ND_FENCE, tok);
            tok = skip(tok->next, TK_LPAREN);
            *rest = skip(tok, TK_RPAREN);
            fence->mem_order = MEM_ORDER_SEQ_CST + 1;
            fence->ty = T.ty_void;
            return fence;
        }
        case BUILTIN_OFFSETOF: {
            // __builtin_offsetof(type, member-designator), where the
            // designator is a chain of `.member` and `[constant]`. 7.19p3
            // makes the result an integer constant expression, and real code
            // sizes arrays with it -- so the offset is computed here rather
            // than left as the address constant cxx's own <stddef.h> used to
            // spell it as.
            tok = skip(tok->next, TK_LPAREN);
            Type *ty = typename(&tok, tok);
            tok = skip(tok, TK_COMMA);
            int64_t off = 0;
            bool first = true;
            for (;;) {
                if (first || tok->kind == TK_DOT) {
                    Token *name = first ? tok : tok->next;
                    first = false;
                    if (ty->kind != TY_STRUCT && ty->kind != TY_UNION)
                        error(name, "request for member ‘%s’ in something not a structure or union", str(name->id));
                    Member *mem = get_struct_member(ty->members, name);
                    if (!mem) error(name, "no member named ‘%s’ in ‘%s’", str(name->id), record_diag_name(ty));
                    off += mem->offset;
                    ty = mem->ty;
                    tok = name->next;
                    continue;
                }
                if (tok->kind == TK_LBRACKET) {
                    Token *br = tok;
                    if (ty->kind != TY_ARRAY) error(br, "subscripted value is neither array nor pointer");
                    int64_t idx = const_expr(&tok, tok->next);
                    off += idx * ty->base->size;
                    ty = ty->base;
                    tok = skip(tok, TK_RBRACKET);
                    continue;
                }
                break;
            }
            *rest = skip(tok, TK_RPAREN);
            return new_num(off, start);
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
        case BUILTIN_NANSF:
        case BUILTIN_NANS:
        case BUILTIN_NANSL:
            return parse_math_const(rest, tok, kind);

        // The comparison macros of 7.12.18.
        case BUILTIN_ISGREATER:
        case BUILTIN_ISGREATEREQUAL:
        case BUILTIN_ISLESS:
        case BUILTIN_ISLESSEQUAL:
        case BUILTIN_ISLESSGREATER:
        case BUILTIN_ISUNORDERED:
            return parse_math_cmp(rest, tok, kind);

        // The classification family of 7.12.4.
        case BUILTIN_ISNAN:
        case BUILTIN_ISINF:
        case BUILTIN_ISINF_SIGN:
        case BUILTIN_ISFINITE:
        case BUILTIN_ISNORMAL:
        case BUILTIN_SIGNBIT:
        case BUILTIN_FPCLASSIFY:
            return parse_classify(rest, tok, kind);
        case BUILTIN_VA_ARG: {
            // __builtin_va_arg(ap, type): the second operand is a type
            // name, not an expression.
            //
            // No variadic-function check: 7.16.1.1 asks only for a va_list
            // initialised by va_start or va_copy, and the function that
            // forwards one -- vprintf's whole shape -- has no parameter list
            // of its own to be variadic. gcc and clang accept it there.
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
            // this, so the copy itself is left to the backend. Like va_arg,
            // this needs two va_lists, not a variadic function.
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
            //
            // Only va_start has to be in a variadic function (7.16.1.1p1).
            // va_end closes a va_list that may have been handed to a
            // function that is not itself variadic -- the vprintf shape,
            // which cpython's object_vacall(), git's helpers and tinycc's
            // all use, and which gcc and clang both accept.
            if (kind == BUILTIN_VA_START && (!cur_fn || !cur_fn->ty->is_variadic))
                error(tok, "‘__builtin_va_start’ used in a function that is not variadic");
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
        case BUILTIN_SYNC_LOCK_RELEASE: {
            // __sync_lock_release(object): gcc defines it as storing zero with
            // release semantics -- the other half of the lock that
            // __sync_lock_test_and_set takes. One argument, no order.
            tok = skip(tok->next, TK_LPAREN);
            Node *object = atomic_object(&tok, start);
            // gcc and clang both tolerate further arguments here as well as on
            // __sync_lock_test_and_set (the Fujitsu suite passes one to each).
            while (tok->kind == TK_COMMA) assign(&tok, tok->next);
            *rest = skip(tok, TK_RPAREN);

            Sym *zero_sym = new_lvar(id_anon, type_unqual(object->ty->base));
            Node *zero_init = new_binary(ND_INIT, new_var_node(zero_sym, start), new_num(0, start), tok);
            Node *target = new_unary(ND_DEREF, object, tok);
            Node *store = new_binary(ND_AS, target, new_var_node(zero_sym, start), tok);
            store->mem_order = MEM_ORDER_RELEASE + 1;
            return new_binary(ND_COMMA, zero_init, store, tok);
        }
        case ATOMIC_STORE:
        case ATOMIC_STORE_GENERIC: {
            // temp = desired; *object = temp with the given order.
            tok = skip(tok->next, TK_LPAREN);
            Node *object = atomic_object(&tok, start);
            tok = skip(tok, TK_COMMA);
            Node *desired = assign(&tok, tok);
            // __atomic_store addresses the value: `*val`, not `val`.
            if (kind == ATOMIC_STORE_GENERIC) {
                add_type(desired);
                if (!is_pointer(desired->ty))
                    error(desired->tok, "argument 2 of ‘__atomic_store’ must be a pointer to the value");
                desired = new_unary(ND_DEREF, desired, desired->tok);
            }
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
        case ATOMIC_LOAD:
        case ATOMIC_LOAD_GENERIC: {
            // temp = *object with the given order; yield temp.
            tok = skip(tok->next, TK_LPAREN);
            Node *object = atomic_object(&tok, start);
            tok = skip(tok, TK_COMMA);
            // __atomic_load addresses the result: `*ret`, not `ret`. gcc's
            // form has no value of its own; cxx leaves the loaded value as
            // the expression's, which accepts everything gcc does and one
            // thing more.
            Node *ret = NULL;
            if (kind == ATOMIC_LOAD_GENERIC) {
                ret = assign(&tok, tok);
                add_type(ret);
                if (!is_pointer(ret->ty))
                    error(ret->tok, "argument 2 of ‘__atomic_load’ must be a pointer to the result");
                ret = new_unary(ND_DEREF, ret, ret->tok);
                tok = skip(tok, TK_COMMA);
            }
            int order = atomic_order(&tok, MO_LOAD);
            *rest = skip(tok, TK_RPAREN);

            Sym *result_sym = new_lvar(id_anon, type_unqual(object->ty->base));
            Node *src = new_unary(ND_DEREF, object, tok);
            src->mem_order = order + 1;
            Node *load = new_binary(ND_AS, new_var_node(result_sym, start), src, tok);
            Node *result = new_binary(ND_COMMA, load, new_var_node(result_sym, start), tok);
            if (!ret) return result;
            Node *store = new_binary(ND_AS, ret, result, tok);
            return new_binary(ND_COMMA, store, new_var_node(result_sym, start), tok);
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
            int order;
            if (this_sync_order >= 0) {
                // __sync_fetch_and_*(object, operand): the order is in the name.
                order = this_sync_order;
                // gcc and clang both tolerate further arguments on
                // __sync_lock_test_and_set -- the Fujitsu suite's C/0044_0001
                // passes a third -- and both refuse them on the fetch family,
                // which is the rule here.
                if (kind == ATOMIC_EXCHANGE)
                    while (tok->kind == TK_COMMA) assign(&tok, tok->next);
                *rest = skip(tok, TK_RPAREN);
            } else {
                tok = skip(tok, TK_COMMA);
                order = atomic_order(&tok, MO_ATOMICRMW);
                *rest = skip(tok, TK_RPAREN);
            }

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
        case ATOMIC_COMPARE_EXCHANGE_N:
        case ATOMIC_COMPARE_EXCHANGE_GENERIC:
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
            // The generic spelling addresses the desired value: `*desired`,
            // not `desired`. Everything else about it is the _n form's.
            if (kind == ATOMIC_COMPARE_EXCHANGE_GENERIC) {
                add_type(desired);
                if (!is_pointer(desired->ty))
                    error(desired->tok, "third argument of ‘__atomic_compare_exchange’ must be a pointer to the value");
                desired = new_unary(ND_DEREF, desired, desired->tok);
                // The new node has no type yet, and both calls below read it.
                add_type(desired);
            }
            lvalue_convert(&desired);
            new_imcast(&desired, value_ty);
            tok = skip(tok, TK_COMMA);
            if (kind == ATOMIC_COMPARE_EXCHANGE_N || kind == ATOMIC_COMPARE_EXCHANGE_GENERIC) {
                // Both GCC spellings put the weak flag where the C11 builtins
                // put the success order. A strong compare-exchange satisfies
                // everything a weak one does, so the flag is read and left
                // unused rather than turning the call into a different
                // operation the caller did not ask for.
                assign(&tok, tok);
                tok = skip(tok, TK_COMMA);
            }
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
        case BUILTIN_MEMCPY:
        case BUILTIN_MEMMOVE:
        case BUILTIN_MEMSET:
        case BUILTIN_MEMCMP:
            return parse_mem_builtin(rest, tok, kind);
        case BUILTIN_UNREACHABLE: {
            // GNU __builtin_unreachable(): the statement after which control
            // never arrives, and reaching it is undefined. cxx has no
            // unreachable terminator to emit -- the IR opcode for one is
            // declared and unused -- so what it builds is the no-op that a
            // void expression already is. The program's results are the same
            // either way, because a path that reaches this point has none;
            // what is lost is the optimiser's knowledge that it cannot.
            // cpython's Py_UNREACHABLE() expands to it.
            tok = skip(tok->next, TK_LPAREN);
            *rest = skip(tok, TK_RPAREN);
            return new_excast(new_num(0, start), T.ty_void, start);
        }
        case BUILTIN_ASSUME_ALIGNED: {
            // GNU __builtin_assume_aligned(ptr, align[, offset]) is the
            // pointer itself plus a promise to the optimiser that it is
            // aligned. The promise changes no result, so what is kept is the
            // pointer -- evaluated, and converted the way any expression
            // value is -- and the other two arguments are read and checked
            // the way gcc reads them: both must be constants, and the
            // alignment a power of two. cpython's bundled mimalloc calls it.
            tok = skip(tok->next, TK_LPAREN);
            Node *ptr = assign(&tok, tok);
            add_type(ptr);
            lvalue_convert(&ptr);
            tok = skip(tok, TK_COMMA);
            int64_t align = const_expr(&tok, tok);
            if (align <= 0 || (align & (align - 1)))
                error(tok, "requested alignment ‘%ld’ is not a positive power of 2", align);
            if (match(&tok, tok, TK_COMMA)) const_expr(&tok, tok);
            *rest = skip(tok, TK_RPAREN);
            return ptr;
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

    // 6.5.2.1p3: neither the controlling operand of a generic selection nor
    // the expression of an association it does not select is evaluated, so a
    // name that only one of those mentions does not keep its definition
    // alive; clang leaves exactly those definitions out of the output too.
    // Both are parsed with the parking flag up, and what the selection does
    // evaluate -- the selected association's expression -- is replayed below.
    bool outer_uneval = uneval_operand;
    uint32_t ctrl_mark = num_parked;
    // p3 also keeps the size expressions in a type name from running, where
    // sizeof would have taken them over: nothing of the controlling operand
    // is evaluated, so whatever it registered as a variable length bound is
    // dropped again.
    int ctrl_vla_num = scope->vla_num;
    bool ctrl_vla = false;
    bool tyname = is_typename(tok, true);
    uneval_operand = true;
    Type *t1;
    if (tyname) {
        t1 = typename(&tok, tok);
        ctrl_vla = t1->kind == TY_VLA;
    } else {
        Node *expr = assign(&tok, tok);
        ctrl_vla = expr->ty->kind == TY_VLA;
        lvalue_convert(&expr);
        t1 = expr->ty;
    }
    uneval_operand = outer_uneval;
    scope->vla_num = ctrl_vla_num;
    // p2: the lvalue, array to pointer and function to pointer conversions
    // apply to an assignment expression operand. A type name designates the
    // type it writes, so it is used as it stands -- gcc and clang both
    // answer 9 for `_Generic(int[3], int *: 1, default: 9)` and for the
    // function type beside it. A name is still kept when the type it names
    // is a variable length one: nothing is emitted for it here either, but
    // clang does not report it, and cxx follows that.
    if (ctrl_vla)
        unpark_refs(ctrl_mark);
    else
        num_parked = ctrl_mark;

    if (!tyname) {
        if (t1->kind == TY_FUNC)
            t1 = pointer_to(t1, 0);
        else if (t1->kind == TY_ARRAY || t1->kind == TY_VLA)
            t1 = pointer_to(t1->base, 0);
    }

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
    // Where the default association's parked names start; the decision waits
    // for the end of the list, because either a type association or the
    // default is selected.
    uint32_t def_mark = 0;

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
            def_mark = num_parked;
            uneval_operand = true;
            default_expr = assign(&tok, tok);
            uneval_operand = outer_uneval;
            continue;
        }

        // p3 covers the size expressions in these type names too: a bound
        // they register is not a bound anything evaluates.
        int t2_vla_num = scope->vla_num;
        Type *t2 = typename(&tok, tok);
        scope->vla_num = t2_vla_num;
        tok = skip(tok, TK_COLON);

        // Unselected associations are discarded after parsing: their
        // block/label accounting must not leak into the function. The type
        // name is parsed with the flag as it was -- a variable length bound
        // in it belongs to the enclosing scope, which still emits it.
        int saved_blk = cur_fn ? cur_fn->num_blk : 0;
        int saved_lbl = cur_fn ? cur_fn->num_lbl : 0;
        Node *saved_labels = labels;
        Node *saved_gotos = gotos;

        uint32_t as_mark = num_parked;
        uneval_operand = true;
        Node *node = assign(&tok, tok);
        uneval_operand = outer_uneval;
        push_generic(t2, as_tok, gen);
        if (is_compatible(t1, t2)) {
            if (ret_tok) {
                diag("error", as_tok, "‘_Generic’ selector matches multiple associations");
                diag_exit("note", ret_tok, "other match is here");
            }
            ret_tok = as_tok;
            ret_expr = node;
            // This is the expression the selection evaluates.
            unpark_refs(as_mark);
        } else {
            // Nor is this one ever evaluated.
            num_parked = as_mark;
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
            // the default association is discarded, names included
            num_parked = def_mark;
            if (cur_fn) {
                cur_fn->num_blk = def_blk;
                cur_fn->num_lbl = def_lbl;
            }
            labels = def_labels;
            gotos = def_gotos;
        }
        return ret_expr;
    }
    if (default_tok) {
        unpark_refs(def_mark);
        return default_expr;
    }
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
        // [GNU] A braced group used as an expression.
        pedantic(tok, "ISO C forbids braced-groups within expressions");
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
            // [GNU] "__FUNCTION__" and "__PRETTY_FUNCTION__" are further
            // names of the standard "__func__"; the name space entry is the
            // one that says which spelling this use came from. gcc's wording
            // for both is the same, and so is this.
            if (sc->id == id_function) pedantic(tok, "ISO C does not support ‘__FUNCTION__’ predefined identifier");
            if (sc->id == id_pretty)
                pedantic(tok, "ISO C does not support ‘__PRETTY_FUNCTION__’ predefined identifier");
            if (sc->var->is_deprecated) warning(WG_DEPRECATED, tok, "‘%s’ is deprecated", str(sc->var->id));
            // -Wunused-variable is the absence of this: an identifier that
            // never resolves to its variable leaves the flag clear.
            sc->var->is_referenced = true;
            // The same point is the only one that knows who is referring,
            // so the reference graph is built here: the function being
            // parsed, or the object whose initializer is being parsed. A
            // name resolved outside both -- in the array bound of a global,
            // say -- is code that always runs, which makes it a root.
            // The innermost context decides, so the parking test comes
            // first: an unevaluated operand parks the edge even inside an
            // emitted initializer, because a nested sizeof is still folded.
            // A block-scope static is never dropped -- gcc and clang both
            // keep it, and clang stays quiet about a `sizeof q` mention --
            // so parking its edge would make cxx report an object it goes on
            // to emit.
            if (uneval_operand && !sc->var->is_block_static) park_ref(cur_fn ? cur_fn : cur_init, sc->var);
            // An emitted initializer roots the name even inside a dead
            // function: that is what live_init marks.
            else if (live_init)
                sc->var->is_reachable = true;
            else if (cur_fn)
                add_ref(cur_fn, sc->var);
            else if (cur_init)
                add_ref(cur_init, sc->var);
            else
                sc->var->is_reachable = true;
            node = new_var_node(sc->var, tok);
        }
        *rest = tok->next;
        return node;
    }
    error(tok, "expected expression before ‘%.*s’", tok->len, tok_text(tok));
    return NULL;
}

// The type a call passes an argument as. A parameter of a transparent union
// -- `union { struct sockaddr *sa; ... } __attribute__((transparent_union))`,
// which is how glibc declares the address parameter of connect(), bind() and
// accept() under _GNU_SOURCE -- takes the types of its members directly, and
// the call passes one of those, never the union. The member the argument is
// assignable to is the one it goes as; with none of them it stays the union
// and check_asop() reports it as before.
//
// gcc also wants the members to be passed alike and warns `union cannot be
// made transparent` when they are not; cxx takes the attribute at its word
// and lets the member the argument matches decide, which is what the shape
// that exists in the headers needs.
static Type *transparent_union_member(Type *param, Node *arg) {
    if (param->kind != TY_UNION) return param;

    bool transparent = false;
    for (Attr *a = param->attrs; a; a = a->next)
        if (a->info && !strcmp(a->info->name, "transparent_union")) {
            transparent = true;
            break;
        }
    if (!transparent) return param;

    for (Member *m = param->members; m; m = m->next)
        if (is_assignable(m->ty, arg, CTX_CALL)) return m->ty;
    return param;
}

// The argument-count diagnostics of a call. A call through a pointer has no
// function name to give -- the function type it points to is anonymous -- so
// the name is used only when the type has one, and clang's wording ("too many
// arguments to function call, expected 0, have 1") is used otherwise. Reading
// ty->name unconditionally is what killed the compiler on
// `int (*p)(); p(0);`.
static void error_call_args(Token *tok, Type *ty, bool too_many) {
    const char *what = too_many ? "too many" : "too few";
    if (ty->name)
        error(tok, "%s arguments to function ‘%.*s’; expected %d", what, ty->name->len, tok_text(ty->name), ty->nparam);
    error(tok, "%s arguments to function call; expected %d", what, ty->nparam);
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
        if (param_ty) error_call_args(tok, ty, false);
        *rest = tok->next;
        return node;
    }

    Node dummy, *cur = &dummy;
    uint32_t i = 0;

    do {
        Node *arg = assign(&tok, tok);
        if (param_ty) {
            // A transparent union parameter passes the member, not the union.
            Type *pass_ty = transparent_union_member(param_ty, arg);
            check_asop(pass_ty, arg, CTX_CALL);
            // lvalue conversion must come before the cast: it wraps the
            // operand in ND_LVTOR, and integer_promotion() would otherwise
            // hide the lvalue and the load would never happen. For a record
            // it produces no load -- the caller's value *is* its address --
            // which irgen turns into a by-value argument.
            lvalue_convert(&arg);
            new_imcast(&arg, pass_ty);
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
            error_call_args(tok, ty, true);
        }
        ++i;
        cur = cur->next = arg;
    } while (match(&tok, tok, TK_COMMA));

    if (param_ty) error_call_args(tok, ty, false);

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
// The memory builtins. Each is the library function named in its table row,
// and the call it builds is an ordinary one to it: the declaration the
// program has in scope gives the call its prototype -- glibc's <string.h>
// always declares these -- and one is declared here when it has not, since a
// builtin is callable without including the header that declares it. The
// symbol is not marked as a builtin, so irgen emits the call rather than
// looking for an intrinsic.
static Node *parse_mem_builtin(Token **rest, Token *tok, int kind) {
    BuiltinDef *d = builtin_def(kind);
    char *lib = d->intrinsic;
    uint32_t id = intern(lib, strlen(lib));

    // The library function's own prototype, used only when nothing in scope
    // declares it: void *f(void *, const void *|int, size_t), or int for
    // memcmp. The size parameter is the target's unsigned long, which is the
    // width size_t has everywhere cxx targets.
    Type *fty = func_type(kind == BUILTIN_MEMCMP ? T.ty_int : pointer_to(T.ty_void, 0));
    Type *p = copy_type(pointer_to(T.ty_void, 0));
    fty->params = p;
    p = p->next = copy_type(kind == BUILTIN_MEMSET ? T.ty_int : pointer_to(T.ty_void, 0));
    p->next = copy_type(T.ty_ulong);
    fty->nparam = 3;
    fty->name = tok;

    // Look the name up as it is written, so a declaration from a header is
    // the one that types the call. The identifier is only borrowed for the
    // lookup: what the source says is still the builtin's spelling.
    uint32_t saved = tok->id;
    tok->id = id;
    NameSpace *ns = find_ident(tok, true, false);
    tok->id = saved;

    Sym *sym;
    if (ns) {
        while (ns->prev) ns = ns->prev;
        sym = ns->var;
    } else {
        sym = new_gvar(id, fty);
        sym->is_function = true;
        push_namespace(file_scope, id, SYM_FUNC, fty, tok)->var = sym;
    }

    Node *fn = new_var_node(sym, tok);
    add_type(fn);
    // The function designator decays to a pointer, which is the shape
    // fncall() expects from postfix() and the one irgen reads the address
    // out of: without it the conversion inside fncall() would load the
    // function itself.
    new_imcast(&fn, pointer_to(fn->ty, 0));
    return fncall(rest, tok->next, fn);
}

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
            var->is_compliteral = true;
            // The object a compound literal names has no linkage, so at file
            // scope it is emitted as a local symbol: without that, two
            // translation units that each have one both define
            // `.compoundliteral` and the link fails -- which is how cxx could
            // not link against itself (src/type.c and src/irgen.c each have
            // one).
            sclass = (sclass & ~SC_EXTERN) | SC_STATIC;
            // This literal is emitted with its initializer, and the
            // initializer runs at load time -- even when the expression that
            // mentions the literal is an unevaluated operand (a sizeof, an
            // unselected _Generic association), which is why both flags are
            // set for it.
            bool outer_live = live_init;
            bool outer_uneval = uneval_operand;
            live_init = true;
            uneval_operand = false;
            gvar_initializer(&tok, tok, var);
            live_init = outer_live;
            uneval_operand = outer_uneval;
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
                if (!mem) error(tok, "no member named ‘%s’ in ‘%s’", str(tok->id), record_diag_name(ty));

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
    // `__extension__ ({ ... })` for statement expressions). What follows is a
    // cast expression, so the parse resumes at `cast`, not at `unary`: with
    // the narrower entry the `(voidf)(p)` of lua's
    // `#define cast_func(p) (__extension__ (voidf)(p))` was no longer seen as
    // a cast at all, and the initialisation it feeds was rejected as
    // incompatible.
    if (tok->kind == TK_EXTENSION) {
        *rest = tok->next;
        return cast(rest, tok->next);
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
            // The operand of the expression form, kept for __alignof__: an
            // object's alignment is the object's, not its type's.
            Node *operand = NULL;
            // See uneval_operand: the names this operand mentions are parked
            // until its type says whether the operand is evaluated at all.
            uint32_t park_mark = num_parked;
            bool outer_uneval = uneval_operand;
            uneval_operand = true;
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
                operand = node;
            }
            uneval_operand = outer_uneval;
            if (ty->kind == TY_VLA)
                unpark_refs(park_mark);
            else
                num_parked = park_mark;
            if (ty->size < 0 && (ty->kind != TY_ARRAY && ty->kind != TY_VLA)) {
                error(start, "invalid application of ‘%*.s’ to incomplete type", start->len, tok_text(start));
            }
            if (start->kind == TK_ALIGNOF) {
                // `__alignof__(object)` is the alignment of the object, which
                // an `aligned` attribute or `_Alignas` raises above the
                // type's -- cpython asserts that its Py_ALIGNED(64) buffer is
                // 64-aligned. The type form keeps the type's own alignment.
                if (operand && operand->kind == ND_VAR) return new_ulong(MAX(operand->var->align, ty->align), start);
                return new_ulong(ty->align, start);
            }
            // The bounds a type name registered are evaluated before the
            // length is read. An *object* has registered none -- its bounds
            // ran where it was declared -- so there is no chain here, and
            // the length is the whole expression: a comma with a null left
            // operand is not a node the rest of the compiler can walk.
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
                    Node *cnt = new_var_node(ty->vla_cnt, start);
                    return size ? new_binary(ND_COMMA, size, cnt, start) : cnt;
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
                Node *len = new_binary(ND_MUL, vla_len, base_sz, start);
                return size ? new_binary(ND_COMMA, size, len, start) : len;
            }
            if (ty->size < 0) error(start, "invalid application of ‘sizeof’ to incomplete type");
            return new_ulong(sizeof_value(ty), start);
        }
        // [GNU] labels-as-values
        case TK_AND: {
            pedantic(tok, "ISO C forbids taking the address of a label");
            Node *node = new_node(ND_LABEL_VAL, tok);
            node->label = label_id_of(get_ident(tok->next));

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

// 6.7.2.1p18: the size of a record with a flexible array member is as if the
// member were omitted. cxx completes that member on the record an initializer
// produced, so the completed size is what the type carries; the declared one
// is that less the member. Before an initializer completes anything the
// member is a zero-length array, and nothing is taken off.
static int64_t sizeof_value(Type *ty) {
    if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->is_flexible) {
        Member *mem = ty->members;
        while (mem && mem->next) mem = mem->next;
        if (mem && mem->ty->size > 0) return ty->size - mem->ty->size;
    }
    return ty->size;
}

static Node *new_excast(Node *expr, Type *ty, Token *tok) {
    add_type(expr);
    lvalue_convert(&expr);

    if (!is_void(ty) && !is_scalar(ty)) {
        // A cast to the operand's own type converts nothing, and C allows it
        // for an aggregate too: `(struct S)s` where s already is one. There
        // is nothing for the back end to do, so the operand stands.
        if (is_compatible(expr->ty, ty)) return expr;
        error(tok, "scalar or void type is required in here");
    }
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
    // The result is a value, not an lvalue, so its top-level qualifiers are
    // gone: `(float const)x` has type `float`. A `_Generic` selector written
    // with a qualified cast therefore matches the unqualified association,
    // which is what both references do.
    node->ty = type_unqual(ty);
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
        add_type(lhs);

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
        pedantic(tok->next, "ISO C forbids omitting the middle term of a ‘?:’ expression");
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
            // A member's array begins at the member's offset; one a
            // subscript produced begins at the element's stride. Only the
            // first has a member -- reading one off an ND_SUBACCESS is what
            // used to crash on `int m[2][3]; int *p = m[1];`.
            int64_t base = eval_rval(node->lhs, sym);
            if (node->kind == ND_MEMBER) return base + node->member->offset;
            return base + eval(node->rhs) * node->ty->size;
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
        // The bounds registered here are the ones the declarator adds plus
        // whatever a `typeof(int[n])` in the specifier part registered
        // before it; the statement below evaluates each of them once, and
        // the object's size then reads the counters they wrote.
        Type *ty = declarator(&tok, tok, basety);
        Token *var_name = ty->name;
        apply_postdecl_attrs(ty);

        // GNU post-declarator attributes attach to the declaration.
        int fspec = funcspec;
        attr_decl_apply(attrs, &fspec, &align, false);
        // A function declarator's post-declarator position is a type attribute
        // one (only __attribute__ applies there); an object declarator's is
        // the declaration position, where the spellings are equivalent.
        attr_decl_apply(ty->attrs, &fspec, &align, ty->kind == TY_FUNC);

        if (ty->kind == TY_VOID) error(start, "variable ‘%s’ declared void", str(var_name->id));

        bool is_fn = ty->kind == TY_FUNC;
        if (fspec && !is_fn) {
            // `noreturn` on a function *pointer* is a type both references
            // have, and git's usage.c declares several:
            // `static __attribute__((noreturn)) report_fn usage_routine = ...`
            // with `report_fn` a pointer typedef. cxx carries the flag on the
            // declaration rather than in the type, so it takes it there and
            // moves on; on anything else the attribute is ignored, which is
            // what gcc (-Wattributes) and clang (-Wignored-attributes) say.
            if (fspec & Q_NORETURN) {
                Attr *nr = find_noreturn_attr(attrs);
                if (!nr) nr = find_noreturn_attr(ty->attrs);
                if (nr && nr->info->ns == ATTR_NS_STD) {
                    // 6.7.13.3p2: the standard attribute "shall be applied
                    // only to the declaration of a function", and clang
                    // treats a breach as an error wherever the attribute
                    // lands -- on an object, on a typedef, or on a function
                    // pointer object.
                    error(nr->tok, "‘noreturn’ can only appear on functions");
                } else if (is_funcptr(ty)) {
                    // A type of the pointer's: taken, and kept.
                } else {
                    warning(WG_ATTRIBUTES, tok, "‘noreturn’ attribute ignored");
                    // Ignored means ignored: leaving the flag on would mark
                    // the object noreturn, and a call through it would then
                    // be taken as one that does not return.
                    fspec &= ~Q_NORETURN;
                }
            }
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
        bool took_over = false;
        NameSpace *ns = find_ident(var_name, false, is_extern);
        uint32_t id = get_ident(var_name);
        if (ns) {
            if (!is_extern) {
                diag("error", var_name, "redefinition of ‘%s’", str(var_name->id));
                diag_exit("note", ns->loc, "previous definition is here");
            }
            check_decl_compatile(ns, symkind, ty);
            // A block-scope declaration of a function declares the function,
            // which the outer declaration has already named -- there is no
            // object here to give a slot to. The symbol it took over is the
            // one every reference already resolves to; the file-scope path
            // takes the same one over.
            if (is_fn && ns->var) {
                var = ns->var;
                took_over = true;
            } else {
                var = new_lvar(id, ty);
                var->tok = var_name;
            }
        } else if (is_extern) {
            var = new_gvar(id, ty);
        } else if (is_static) {
            char *name = format("%s.%s", str(cur_fn->id), str(id));
            uint32_t uid = new_unique_varname(intern(name, strlen(name)));
            var = new_gvar(uid, ty);
            // The object is a global -- hence the mangled name -- but it
            // belongs to this function, which is what the unused-object
            // walk and the emitter both need to know.
            var->is_block_static = true;
            var->tok = var_name;
            // 6.7.5p3: an inline definition may not define a modifiable
            // object with static storage duration. Whether this function's
            // definition is an inline definition is only known once the unit
            // is complete, so remember the token for now.
            if (!is_const_object(ty)) cur_fn->static_local_tok = var_name;
        } else {
            var = new_lvar(id, ty);
            var->tok = var_name;
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
        // The symbol keeps the storage class of its first declaration (see
        // the file-scope path): a block-scope `long g(void);` after
        // `static long g(void);` must leave the function internal.
        if (!took_over) var->sclass = sclass;
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
            // An explicit __asm__("name") wins over an alias attribute when one
            // declaration carries both.
            if (a) {
                set_asm_name(var, a);
            } else {
                // The attribute may sit before or after the declarator: the
                // one written after it lands on the type.
                apply_alias_attr(var, attrs);
                apply_alias_attr(var, ty->attrs);
            }
        }
        sym_attr_flags(var, attrs, false);
        sym_attr_flags(var, ty->attrs, true);
        // 6.8.6.1p1 / 6.8.5.3p2: a jump may not pass this declaration.
        if (is_vm_type(var->ty)) note_vm_decl(var_name, false);
        // __attribute__((cleanup(f))): an automatic object has a scope to be
        // left; a static, an extern or a function declared here has not, and
        // both references ignore the attribute on those.
        if (var->cleanup_attr) {
            if (is_static || is_extern || is_fn)
                warning(WG_ATTRIBUTES, var_name, "‘cleanup’ attribute only applies to local variables");
            else {
                Sym *fn = cleanup_handler(var);
                // The call the handler runs is this object's use: an object
                // whose only mention is the attribute is not unused, and
                // neither reference reports it as one.
                var->is_referenced = true;
                if (!scope->cleanups)
                    scope->cleanups = vnew(4, sizeof(Cleanup));
                else
                    scope->cleanups = vgrow(scope->cleanups, scope->cleanup_num + 4);
                scope->cleanups[scope->cleanup_num].var = var;
                scope->cleanups[scope->cleanup_num].fn = fn;
                scope->cleanup_num++;
            }
            var->cleanup_attr = NULL;
        }
        if (tok->kind == TK_AS) {
            if (is_extern)
                error(var_name, "declaration of block scope identifier ‘%s’ with linkage cannot have an initializer",
                      str(var_name->id));
            // Like clang: atomic aggregates cannot be brace-initialized
            // (copy-initialization from another object stays legal).
            if ((ty->qual & Q_ATOMIC) && (ty->kind == TY_STRUCT || ty->kind == TY_UNION) &&
                tok->next->kind == TK_LBRACE)
                error(var_name, "illegal initializer type '_Atomic(%s)'", record_diag_name(ty));
            if (is_static) {
                // A block-scope static is emitted, and its initializer runs,
                // whether or not anything reaches the function around it.
                bool outer_live = live_init;
                live_init = true;
                gvar_initializer(&tok, tok->next, var);
                live_init = outer_live;
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
            // Re-entering the declaration without leaving the scope -- a
            // jump back into the block, which is tinycc's 122_vla_reuse --
            // would otherwise walk the stack down one array per pass. The
            // first execution records where this object starts, and later
            // ones put the stack pointer back there before allocating, so
            // the address depends on the size alone: the same shape gcc and
            // clang get by hoisting the array into the frame.
            Sym *vla_base = new_lvar(intern("", 0), T.ty_voidptr);
            {
                Node *zero = new_node(ND_NULLPTR, tok);
                zero->ty = T.ty_voidptr;
                Node *dst = new_var_node(vla_base, tok);
                add_type(dst);
                Node *init = new_binary(ND_AS, dst, zero, tok);
                init->ty = T.ty_voidptr;
                Node *st = new_unary(ND_EXPR_STMT, init, tok);
                if (fn_prologue_last)
                    fn_prologue_last = fn_prologue_last->next = st;
                else
                    fn_prologue_first = fn_prologue_last = st;
            }
            {
                Node *test = new_var_node(vla_base, tok);
                add_type(test);
                lvalue_convert(&test);
                Node *slot = new_var_node(vla_base, tok);
                add_type(slot);
                lvalue_convert(&slot);
                Node *back = new_unary(ND_SP_RESTORE, slot, tok);
                back->ty = T.ty_void;
                Node *iff = new_node(ND_IF, tok);
                iff->cond = test;
                iff->then = new_unary(ND_EXPR_STMT, back, tok);
                cnt_blk(2);  // gen_if: then / merge
                if (!fn_vla_guards)
                    fn_vla_guards = vnew(4, sizeof(Node *));
                else
                    fn_vla_guards = vgrow(fn_vla_guards, fn_vla_guard_num + 4);
                fn_vla_guards[fn_vla_guard_num++] = iff;
                fn_vla_decls++;
                cur = cur->next = iff;
            }
            {
                Node *save = new_node(ND_SP_SAVE, tok);
                save->ty = T.ty_voidptr;
                Node *dst = new_var_node(vla_base, tok);
                add_type(dst);
                Node *keep = new_binary(ND_AS, dst, save, tok);
                keep->ty = T.ty_voidptr;
                cur = cur->next = keep;
            }
            // The size comes from the counters the bound statements above
            // wrote, not from the bounds themselves: those statements have
            // already evaluated each bound, and reaching for the bound node
            // again here would run its side effects a second time -- and ask
            // for blocks the parse-time count did not reserve.
            Node *size = NULL;
            for (Type *t = var->ty; t->kind == TY_VLA; t = t->base) {
                Node *cnt = new_var_node(t->vla_cnt, tok);
                add_type(cnt);
                size = size ? new_binary(ND_MUL, size, cnt, tok) : cnt;
            }
            Type *base_ty = var->ty;
            while (base_ty->kind == TY_VLA) base_ty = base_ty->base;
            add_type(size);
            Node *alloc = new_node(ND_ALLOCA, tok);
            alloc->lhs = size;
            // The length may have come from the initializer, after the
            // alignment was settled, so it is taken again where the storage is
            // handed out. An access may keep the lower alignment; this is the
            // one that has to be right.
            alloc->rhs = new_ulong(object_align(var->ty, var->align), tok);
            alloc->base_ty = base_ty;
            add_type(alloc);
            Node *vla_var = new_var_node(var, tok);
            add_type(vla_var);
            Node *expr = new_binary(ND_AS, vla_var, alloc, tok);
            expr->ty = var->ty;
            cur = cur->next = expr;
        }
        scope->vla_num = 0;
        // 6.7.6.2p1: the element type of an array shall be complete.
        if (var->ty->kind == TY_ARRAY && var->ty->base->size < 0) error(var_name, "array has incomplete element type");
        // A definition of an array of unknown size needs an initializer to
        // give it one (6.7.9); 6.9.2p2's one-element assumption is for
        // tentative definitions, which only file scope has. Both references
        // diagnose the bare form here.
        if (!is_extern && var->ty->kind == TY_ARRAY && var->ty->len < 0)
            error(var_name, "definition of variable with array type needs an explicit size or an initializer");
        // 6.9.2p2: a declaration with `extern` and no initializer is not a
        // definition, and only a definition needs a complete type -- both
        // references accept `extern struct S x;` for a type that is never
        // completed. cpython declares every object in PyAPI_DATA that way,
        // and git's headers declare records the same way.
        if (var->ty->size < 0 && !is_extern && (var->ty->kind != TY_ARRAY && var->ty->kind != TY_VLA))
            error(var_name, "variable ‘%s’ has incomplete type", str(var_name->id));
    } while (match(&tok, tok, TK_COMMA));

    *rest = tok;
    cur->next = NULL;
    return dummy.next;
}

// -Wimplicit-fallthrough. A case or default label is entered either by the
// switch's own dispatch or by falling out of the statement before it. The
// parser reads a body in order, so one bit is enough: every statement leaves
// in it whether control can reach what follows, which is also what a
// compound statement has to tell its parent. Only a case label reads it, and
// it clears the bit on the way in -- that is what keeps the first label of a
// switch, and the label after a break, quiet.
static bool falls_through;

// A break statement leaves the innermost enclosing loop or switch, so each
// one leaves a frame here while its body is parsed. An endless loop only
// falls through when a break of its own can leave it: `while (1) {}` does
// not, `while (1) { if (x) break; }` does.
typedef struct BrkFrame BrkFrame;
struct BrkFrame {
    BrkFrame *next;
    bool seen;
};
static BrkFrame *brk_frame;

// True for `while (1)` and `for (;;)`. fold_ast runs after parsing, so only
// a literal condition is recognised here; `while (1 + 0)` is not.
static bool cond_never_false(Node *cond) {
    if (!cond) return true;  // for (;;)
    if (cond->kind != ND_NUM || !cond->ty || !is_integer(cond->ty)) return false;
    return !int128_is_zero(cond->ival);
}

// The label consumes the bit: a run of labels one after another is a single
// entry point, not a fall-through from each one to the next.
static void check_fallthrough(Token *tok) {
    if (falls_through) warning(WG_IMPLICIT_FALLTHROUGH, tok, "unannotated fall-through between switch labels");
    falls_through = false;
}

// ExpStmt ::= ";" | Exp ";"
static Node *expr_stmt(Token **rest, Token *tok) {
    Node *node = new_node(ND_EXPR_STMT, tok);

    if (tok->kind == TK_SEMI) {
        *rest = tok->next;
        falls_through = true;
        return node;
    }

    node->lhs = expr(&tok, tok);

    // A discarded call to a nodiscard function warns (as in clang). The same
    // unwrapping finds the callee for the fall-through bit below: a function
    // declared noreturn never reaches the statement after the call.
    Node *call = node->lhs;
    while (call->kind == ND_LVTOR || call->kind == ND_IMCAST || call->kind == ND_EXCAST) call = call->lhs;
    falls_through = true;
    if (call->kind == ND_FUNCALL) {
        Node *f = call->func;
        while (f->kind == ND_IMCAST || f->kind == ND_LVTOR) f = f->lhs;
        if (f->kind == ND_VAR && f->var->is_nodiscard)
            warning(WG_UNUSED_RESULT, node->lhs->tok,
                    "ignoring return value of function ‘%s’ declared with ‘nodiscard’ attribute", str(f->var->id));
        if (f->kind == ND_VAR && f->var->is_function && (f->var->funcspec & Q_NORETURN)) falls_through = false;
    }

    *rest = skip(tok, TK_SEMI);
    return node;
}

static int cont_depth;
static int brk_depth;

// The scopes the enclosing loops and switches opened, innermost first. A
// break or continue runs the handlers of every scope it leaves, up to but not
// including the one it lands in: that scope's own handlers run where it ends,
// which is where a break lands as well.
typedef struct LoopScope LoopScope;
struct LoopScope {
    LoopScope *next;
    Scope *scp;    // the scope the loop or switch opened
    Node *labels;  // the label run in front of it, for `break name;`
    bool is_loop;  // false for a switch, which only a break can leave
};
static LoopScope *loop_scopes;

// The label run of the statement stmt() is about to parse, so that the loop
// or switch it turns out to be can record it (see LoopScope.labels).
static Node *stmt_label;

// Where a break or continue lands: the innermost loop (or, for a break, the
// innermost loop or switch) for the plain form, or the one a name selects.
static LoopScope *loop_scope_of(Node *name, bool is_break) {
    for (LoopScope *fr = loop_scopes; fr; fr = fr->next) {
        if (!name) {
            if (is_break || fr->is_loop) return fr;
            continue;
        }
        if (!fr->labels) continue;
        Node *t = fr->labels;
        do {
            if (t->label == name->label) return fr;
            t = t->label_ring;
        } while (t && t != fr->labels);
    }
    return NULL;
}

// The scope each goto and each label was parsed in, so that the handlers a
// jump leaves can be worked out once the label it names is known.
typedef struct JumpScope JumpScope;
struct JumpScope {
    Node *node;
    Scope *scp;
    uint32_t seq;  // variably modified declarations seen before this point
};
static JumpScope *jump_scopes;
static uint32_t num_jump_scopes;

static void note_jump_scope(Node *node, Scope *scp) {
    if (!jump_scopes)
        jump_scopes = vnew(8, sizeof(JumpScope));
    else
        jump_scopes = vgrow(jump_scopes, num_jump_scopes + 8);
    jump_scopes[num_jump_scopes].node = node;
    jump_scopes[num_jump_scopes].scp = scp;
    jump_scopes[num_jump_scopes].seq = vm_seq;
    num_jump_scopes++;
}

static uint32_t jump_seq_of(Node *node) {
    for (uint32_t i = 0; i < num_jump_scopes; i++)
        if (jump_scopes[i].node == node) return jump_scopes[i].seq;
    return 0;
}

static Scope *jump_scope_of(Node *node) {
    for (uint32_t i = 0; i < num_jump_scopes; i++)
        if (jump_scopes[i].node == node) return jump_scopes[i].scp;
    return NULL;
}

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
    bool then_falls = falls_through;
    // Else
    if (tok->kind == TK_ELSE) node->els = stmt(&tok, tok->next);
    // Without an else, the condition being false is itself a way past the
    // if; with one, either branch may carry control to what follows.
    falls_through = node->els ? (then_falls || falls_through) : true;
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
    LoopScope loop = {loop_scopes, scope, stmt_label, false};
    loop_scopes = &loop;
    Node *node = new_node(ND_SWITCH, tok);
    Node *sw = cur_sw;
    cur_sw = node;

    // cond
    tok = skip(tok->next, TK_LPAREN);
    node->cond = select_head(&tok, tok);
    add_type(node);
    tok = skip(tok, TK_RPAREN);

    // The dispatch is a jump to every case and default label in the body, so
    // 6.8.5.3p2 asks the same question of it that 6.8.6.1p1 asks of a goto.
    Scope *sw_scope = scope;
    uint32_t sw_seq = vm_seq;

    // body. The first label of a switch is entered by the dispatch, not by
    // falling out of anything, so the bit starts clear and the body's own
    // statements fill it in from there.
    falls_through = false;
    BrkFrame fr = {brk_frame, false};
    brk_frame = &fr;
    node->body = stmt(rest, tok);
    brk_frame = fr.next;
    // A switch reaches what follows it when no label can match -- there is no
    // default to catch the value -- when a break leaves it, or when the last
    // statement of the body runs off its end. Only a switch with a default
    // whose every label ends in a jump never falls through.
    falls_through = !node->default_case || fr.seen || falls_through;
    cnt_blk(1);  // gen_switch: merge (case labels count in label())

    loop_scopes = loop.next;
    brk_depth--;
    cur_sw = sw;
    node->case_next = reverse_list(Node, node->case_next, case_next);
    for (Node *c = node->case_next; c; c = c->case_next)
        check_vm_jump(c->tok, "cannot jump from switch statement to this case label", sw_scope, sw_seq,
                      jump_scope_of(c), jump_seq_of(c));

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
    LoopScope loop = {loop_scopes, scope, stmt_label, true};
    loop_scopes = &loop;
    Node *node = new_node(ND_WHILE, tok);

    tok = skip(tok->next, TK_LPAREN);
    // Cond
    node->cond = expr(&tok, tok);
    tok = skip(tok, TK_RPAREN);
    // Body
    BrkFrame fr = {brk_frame, false};
    brk_frame = &fr;
    node->then = stmt(rest, tok);
    brk_frame = fr.next;
    falls_through = !(cond_never_false(node->cond) && !fr.seen);
    cnt_blk(3);  // gen_while: cond / body / merge

    loop_scopes = loop.next;
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
    LoopScope loop = {loop_scopes, scope, stmt_label, true};
    loop_scopes = &loop;
    Node *node = new_node(ND_DO, tok);

    // Body
    BrkFrame fr = {brk_frame, false};
    brk_frame = &fr;
    node->body = stmt(&tok, tok->next);
    brk_frame = fr.next;
    // Cond
    tok = skip(tok, TK_WHILE);
    tok = skip(tok, TK_LPAREN);
    node->cond = expr(&tok, tok);
    tok = skip(tok, TK_RPAREN);
    *rest = skip(tok, TK_SEMI);
    falls_through = !(cond_never_false(node->cond) && !fr.seen);
    cnt_blk(3);  // gen_do: body / cond / merge

    loop_scopes = loop.next;
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
    LoopScope loop = {loop_scopes, scope, stmt_label, true};
    loop_scopes = &loop;
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
    BrkFrame fr = {brk_frame, false};
    brk_frame = &fr;
    node->body = stmt(rest, tok);
    brk_frame = fr.next;
    falls_through = !(cond_never_false(node->cond) && !fr.seen);
    cnt_blk(4);  // gen_for: cond / body / incr / merge

    cont_depth--;
    brk_depth--;
    // This is the one of these statements whose own scope can hold a
    // declaration -- the for-init -- so it is the one whose handlers have to
    // run where the loop ends. A break lands there too, which is why the
    // break does not run them itself.
    Node *fini = cleanup_scope_chain(scope, tok);
    loop_scopes = loop.next;
    Node *restore = leave_scope(tok);
    if (fini) node = new_binary(ND_COMMA, node, fini, tok);
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
        pedantic(tok, "ISO C forbids ‘goto *expr;’");
        // [GNU] `goto *ptr` jumps to the address specified by `ptr`.
        Node *node = new_node(ND_GOTO_EXPR, tok);
        node->lhs = expr(&tok, tok->next->next);
        lvalue_convert(&node->lhs);
        if (!is_pointer(node->lhs->ty)) error(node->lhs->tok, "computed goto must be pointer type");
        *rest = skip(tok, TK_SEMI);
        falls_through = false;
        return node;
    }
    Node *node = new_node(ND_GOTO, tok);
    node->label = label_id_of(get_ident(tok->next));
    // Which handlers this jump runs is only known once the label is, so the
    // scope it starts from waits next to it (see resolve_goto_labels).
    note_jump_scope(node, scope);

    node->goto_next = gotos;
    gotos = node;
    falls_through = false;

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

    // The handlers of every scope this jump leaves run before it.
    LoopScope *fr = loop_scope_of(node->target, false);
    if (fr) node->unwind = cleanup_leaving(scope, fr->scp, node->tok);

    falls_through = false;
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

    // The frame on top is the loop or switch this break leaves -- right for
    // the plain form; the GNU named form is attributed to the innermost one.
    if (brk_frame) brk_frame->seen = true;
    LoopScope *fr = loop_scope_of(node->target, true);
    if (fr) node->unwind = cleanup_leaving(scope, fr->scp, node->tok);
    falls_through = false;
    *rest = skip(tok, TK_SEMI);
    return node;
}

// RetStmt ::= "return" Exp? ";"
static Node *return_stmt(Token **rest, Token *tok) {
    if (cur_fn->funcspec & Q_NORETURN)
        warning(WG_INVALID_NORETURN, tok, "function ‘%s’ declared 'noreturn' should not return", str(cur_fn->id));
    Node *node = new_node(ND_RETURN, tok);
    falls_through = false;
    Type *ret = cur_fn->ty->ret;
    if (tok->next->kind == TK_SEMI) {
        if (ret->kind != TY_VOID) error(tok, "non-void function ‘%s’ should return a value", str(cur_fn->id));
        *rest = tok->next->next;
        // A return leaves every scope of the function; the result is read
        // first, which is gen_ret's business.
        node->unwind = cleanup_leaving(scope, NULL, tok);
        return node;
    }

    node->lhs = expr(&tok, tok->next);
    if (ret->kind == TY_VOID) error(node->tok, "void function ‘%s’ should not return a value", str(cur_fn->id));
    *rest = skip(tok, TK_SEMI);

    add_type(node);
    check_asop(ret, node->lhs, CTX_RET);
    new_imcast(&node->lhs, ret);
    node->unwind = cleanup_leaving(scope, NULL, tok);

    return node;
}

static void check_label(uint32_t label, Token *tok) {
    Node *cur = labels;
    while (cur) {
        if (cur->label == label) {
            // The token carries the name that was written; the id may be the
            // mangled one a __label__ declaration gave it.
            diag("error", tok, "redefinition of label ‘%s’", str(tok->id));
            diag_exit("note", cur->tok, "previous definition is here");
        }
        cur = cur->goto_next;
    }
}

// A case label is a value or a closed range, and no two of them may overlap
// (6.8.4.2p2 rules out a duplicate; gcc and clang also report an overlap).
// Comparing ranges as ranges is what keeps a wide one cheap: walking the
// values of `case -9223372036854775807LL-1LL ... -1LL` never ended.
static void check_case(int64_t lo, int64_t hi, bool is_range, Token *tok) {
    for (Node *cur = cur_sw->case_next; cur; cur = cur->case_next) {
        int64_t clo = int128_to_i64(cur->ival);
        int64_t chi = cur->is_range ? int128_to_i64(cur->ival_end) : clo;
        if (hi < clo || chi < lo) continue;
        if (chi == clo && hi == lo) {
            diag("error", tok, "duplicate case value ‘%ld’", lo);
        } else {
            diag("error", tok, "case label range ‘%ld ... %ld’ overlaps ‘%ld ... %ld’", lo, hi, clo, chi);
        }
        diag_exit("note", cur->tok, "previous case defined here");
    }
    (void)is_range;
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
            // A label is an entry point of its own, and clang's check treats
            // a case label that a normal label leads into as deliberate.
            falls_through = false;
            Node *node = new_node(ND_LABEL, tok);
            node->label = label_id_of(tok->id);
            note_jump_scope(node, scope);
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
            check_fallthrough(tok);
            Node *node = new_node(ND_CASE, tok);
            note_jump_scope(node, scope);
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
            check_fallthrough(tk_case);
            if (tok->kind == TK_COLON) {
                check_case(val1, val1, false, tk_case);
                Node *node = new_node(ND_CASE, tk_case);
                note_jump_scope(node, scope);
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
            if (val2 < val1) warning(WG_DEFAULT, tk_case, "empty case range specified");
            check_case(val1, val2, true, tk_case);
            // One label for the whole range: the back end compares against
            // both ends instead of enumerating them.
            Node *node = new_node(ND_CASE, tk_case);
            note_jump_scope(node, scope);
            node->ival = int128_set_i(val1);
            node->ival_end = int128_set_i(val2);
            node->is_range = true;
            if (idx < 0 && cur_fn) {
                idx = cur_fn->num_lbl++;
                cnt_blk(1);
                // The back end puts a comparison ahead of the switch for this
                // label, and that comparison needs a block of its own.
                cnt_blk(1);
            }
            node->blk_idx = idx;
            node->case_next = cur_sw->case_next;
            cur_sw->case_next = node;
            cur = cur->label_ring = node;
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
    falls_through = true;
    return new_node(ND_NOP, start);
}

//
// GNU asm statements.
//
// ISO C has no asm in any form, so the whole construct is the GNU one: a
// basic statement (a template and nothing else), an extended one (with
// output, input and clobber lists) and asm goto (whose template may jump to
// labels of the enclosing function). Both references implement all three the
// same way and differ only in the wording of the diagnostics.
//
// Most of what happens here is translation. A template is written in GCC's
// operand language -- `%0`, `%[name]`, `%l1`, `%%` -- and has to be rewritten
// in LLVM's, where an operand is `$0`, a label reference `${0:l}`, a modifier
// `${0:b}`, and a literal dollar sign `$$`. The rewrite needs the operand
// numbering settled first, so the operands are numbered before the template
// is converted, and the constraint string -- which is what the IR call is
// built from -- is assembled in the same pass.
//
// `asm` itself is a keyword here as in gcc's GNU mode; `__asm__` is the
// spelling that survives strict ISO mode, and the lexer folds both into one
// token.

// asm-qualifiers ::= ("volatile" | "inline" | "goto")*, in any order.
// `volatile` says the statement has effects and may not be moved or dropped,
// `inline` is a hint LLVM's IR has nowhere to put, and `goto` says the
// template jumps to labels. All three are also spelled with underscores on
// both sides, which the preprocessor has already folded into the keywords.
static Token *asm_qualifiers(Token *tok, uint32_t *flags) {
    for (;; tok = tok->next) {
        if (tok->kind == TK_VOLATILE) {
            *flags |= ASM_VOLATILE;
        } else if (tok->kind == TK_INLINE) {
            *flags |= ASM_INLINE;
        } else if (tok->kind == TK_GOTO) {
            *flags |= ASM_GOTO;
        } else {
            return tok;
        }
    }
}

// Where a constraint's letters begin. In front of them stand GCC's modifiers:
// `=` writes the operand, `+` reads and writes it, `&` makes it an early
// clobber, and `%` says the instruction and the operand may be swapped. LLVM
// spells `+` as an output plus an input of its own, which the caller does --
// it is what makes the constraint string longer than the operand list -- and
// keeps the rest. A digit is not a modifier but the whole constraint: it is
// the matching constraint, which shares an earlier operand's register.
static int asm_cons_off(char *cons) {
    int i = 0;
    while (cons[i] == '=' || cons[i] == '+' || cons[i] == '&' || cons[i] == '%' || cons[i] == '*') i++;
    return i;
}

// A constraint's letters, in LLVM's spelling. They are looked up in the
// target's table: a few name one fixed register ('a' is {ax} on x86), and the
// memory letters have to be marked indirect ('m' is *m), which is also what
// tells the printer to hand LLVM an address. A letter no table names is left
// as written, which is what clang does with it too.
static char *asm_cons_conv(char *cons, bool is_output) {
    for (int k = 0; k < T.num_asm_cons; k++) {
        AsmConsConv *c = &T.asm_cons[k];
        if (cons[0] != c->letter) continue;
        char *rep = c->reg ? format("{%s}", c->reg) : (is_output ? c->out : c->in);
        // The letter is in the table but this direction is not rewritten --
        // an input-only constraint -- so it is spelled as written.
        if (rep) return format("%s%s", rep, cons + 1);
        break;
    }
    return format("%s", cons);
}

// Whether a name written in the template is this operand's.
static bool asm_name_is(char *name, char *s, int len) {
    return name && (int)strlen(name) == len && !memcmp(name, s, len);
}

// The template, rewritten in LLVM's spelling.
//
// GCC writes an operand as `%0`, `%[name]`, `%l1` or `%l[name]`, with an
// optional modifier letter in front of the operand (`%b0`, `%w1`, `%c2`), and
// `%%` is one literal per cent. LLVM marks an operand with `$`, spells a
// label reference `${N:l}` and a modified operand `${N:b}`, and takes a
// literal dollar sign as `$$` -- so one written in the template is doubled.
// Whatever else a `%` is followed by is left alone: both references hand
// `%eax` to the assembler, where the per cent is a register prefix and no
// operand at all.
//
// `label_base` is where the first goto label lands in LLVM's numbering.
// GCC's is the operand count -- which is what makes `%l0` with one input name
// the input -- and the labels come after the input half of every `+` operand
// in the constraint string, which is the distance between the two.
static char *asm_tmpl_conv(Token *tok, char *src, AsmOperand *ops, Node **labels, int nlabels, uint32_t label_base,
                           bool dialects, bool module_asm) {
    int nops = 0;
    for (AsmOperand *x = ops; x; x = x->next) nops++;

    int len = strlen(src);
    char *buf = emalloc(len * 4 + 1);  // every byte, worst case: "$$" for one
    int o = 0;

    for (int i = 0; i < len;) {
        char c = src[i];
        if (c == '$') {
            // LLVM's own operand marker: one written in the template is two.
            // A file-scope statement is not an inline one -- it becomes
            // `module asm`, whose text reaches the assembler as written, and
            // the assembler reads `$` as the immediate prefix.
            buf[o++] = '$';
            if (!module_asm) buf[o++] = '$';
            i++;
            continue;
        }
        if (c != '%') {
            // GCC's dialect alternatives: `{att|intel|...}` picks one of its
            // arms for the assembler in use, and LLVM spells the same thing
            // `$(att$|intel$|...)`. Only an extended statement on a target
            // whose assembler has dialects is rewritten -- clang leaves the
            // braces of a basic statement, and of every statement on a target
            // without dialects, exactly as written, and cpython's
            // pycore_pystate.h writes `{movq %%rsp, %0|mov %0, rsp}` for
            // x86-64.
            if (dialects && (c == '{' || c == '|' || c == '}')) {
                buf[o++] = '$';
                buf[o++] = c == '{' ? '(' : c == '|' ? '|' : ')';
                i++;
                continue;
            }
            buf[o++] = c;
            i++;
            continue;
        }
        i++;
        if (src[i] == '%') {
            buf[o++] = '%';
            i++;
            continue;
        }
        // `%{`, `%|` and `%}` are the literal characters the syntax above
        // would otherwise take. This one is not conditional on the target:
        // clang honours it wherever the syntax exists at all.
        if (src[i] == '{' || src[i] == '|' || src[i] == '}') {
            buf[o++] = src[i];
            i++;
            continue;
        }

        bool is_label = false;
        char mod = 0;
        if (isalpha((unsigned char)src[i])) {
            if (src[i] == 'l')
                is_label = true;
            else
                mod = src[i];
            i++;
        }

        // The operand: a number, or a name in brackets.
        long num = -1;
        char *name = NULL;
        int nlen = 0;
        if (isdigit((unsigned char)src[i])) {
            for (num = 0; isdigit((unsigned char)src[i]); i++) num = num * 10 + (src[i] - '0');
        } else if (src[i] == '[') {
            name = src + ++i;
            while (src[i] && src[i] != ']') i++;
            nlen = src + i - name;
            if (src[i] != ']') error(tok, "expected ‘]’ in asm template");
            i++;
        } else {
            // Not an operand reference after all: `%eax`, or a modifier with
            // no operand for it to modify. Both references leave it to the
            // assembler rather than diagnosing it here.
            buf[o++] = '%';
            if (mod) buf[o++] = mod;
            if (is_label) buf[o++] = 'l';
            continue;
        }

        uint32_t pos;
        if (is_label) {
            int k = -1;
            if (name) {
                for (int j = 0; j < nlabels; j++)
                    if (labels[j]->label == intern(name, nlen)) k = j;
                if (k < 0) error(tok, "unknown symbolic operand name in inline assembly string");
            } else {
                if (num < nops || num >= nops + (long)nlabels) error(tok, "invalid ‘asm’: ‘%%l’ operand isn’t a label");
                k = num - nops;
            }
            pos = label_base + k;
        } else {
            AsmOperand *op = NULL;
            for (AsmOperand *x = ops; x; x = x->next) {
                if (name) {
                    if (asm_name_is(x->name, name, nlen)) op = x;
                } else if ((long)x->index == num) {
                    op = x;
                }
            }
            // A number LLVM does not know is not a diagnostic there: its
            // backend aborts on one, so it has to be caught here.
            if (!op) {
                if (name) error(tok, "unknown symbolic operand name in inline assembly string");
                error(tok, "invalid operand number in inline asm string");
            }
            pos = op->pos;
        }

        if (is_label) {
            o += sprintf(buf + o, "${%u:l}", pos);
        } else if (mod) {
            o += sprintf(buf + o, "${%u:%c}", pos, mod);
        } else {
            o += sprintf(buf + o, "$%u", pos);
        }
    }
    buf[o] = '\0';
    return buf;
}

// One more entry in the constraint string being built. It is built with
// format() rather than in a buffer of its own: a statement has a handful of
// operands, and the pieces arrive one at a time from three different places.
static char *asm_cons_add(char *cons, char *piece) {
    if (!cons) return format("%s", piece);
    return format("%s,%s", cons, piece);
}

// asm-operand ::= "[" identifier "]" string-literal "(" assignment-expr ")"
// An output is an lvalue the template writes; an input is a value it reads.
// A `[name]` lets the template refer to the operand by name instead of by
// number.
static Token *asm_operand(Token **rest, Token *tok, bool is_output, AsmOperand ***tail) {
    AsmOperand *op = emalloc(sizeof(AsmOperand));
    op->is_output = is_output;
    op->arg_pos = -1;
    op->plus_arg_pos = -1;

    if (tok->kind == TK_LBRACKET) {
        tok = tok->next;
        if (tok->kind != TK_IDENT) error(tok, "expected identifier in ‘asm’ operand name");
        op->name = str(tok->id);
        tok = skip(tok->next, TK_RBRACKET);
    }

    if (tok->kind != TK_STRLIT) error(tok, "expected string literal in ‘asm’");
    if (tok->enc_prefix != PREFIX_NONE) error(tok, "expected a plain string literal in ‘asm’");
    op->tok = tok;
    op->cons = str(tok->id);
    tok = skip(tok->next, TK_LPAREN);
    op->expr = assign(&tok, tok);
    tok = skip(tok, TK_RPAREN);

    // GCC's two rules about the constraint itself: an output has to say it
    // writes ('='), or reads and writes ('+'), and an input may say neither.
    bool writes = op->cons[0] == '=' || op->cons[0] == '+';
    if (is_output && !writes) error(op->tok, "output operand constraint lacks ‘=’");
    if (!is_output && writes) error(op->tok, "input operand constraint contains ‘=’");
    op->is_plus = op->cons[0] == '+';
    // The output half: the modifiers with `+` turned into `=`, and the letters
    // converted. The input half of a `+` is added later, as a constraint of
    // its own, out of the same letters.
    int off = asm_cons_off(op->cons);
    char *pre = emalloc(off + 1);
    for (int i = 0; i < off; i++) pre[i] = op->cons[i] == '+' ? '=' : op->cons[i];
    pre[off] = '\0';
    op->conv = format("%s%s", is_output ? pre : "", asm_cons_conv(op->cons + off, is_output));
    op->is_indirect = strchr(op->conv, '*') != NULL;

    **tail = op;
    *tail = &op->next;
    *rest = tok;
    return tok;
}

// asm-operands ::= asm-operand ("," asm-operand)*
static Token *asm_operands(Token **rest, Token *tok, bool is_output, AsmOperand ***tail, int *count) {
    while (tok->kind != TK_COLON && tok->kind != TK_COLONCOLON && tok->kind != TK_RPAREN) {
        if (*count) tok = skip(tok, TK_COMMA);
        tok = asm_operand(&tok, tok, is_output, tail);
        (*count)++;
    }
    *rest = tok;
    return tok;
}

// asm-clobbers ::= string-literal ("," string-literal)*
// Each names a register the template writes without saying so, or "memory"
// for the memory it touches. They are constraints to LLVM ("~{rax}"), so
// they are appended to the constraint list as they are read.
static Token *asm_clobbers(Token **rest, Token *tok, char **cons) {
    while (tok->kind == TK_STRLIT) {
        if (tok->enc_prefix != PREFIX_NONE) error(tok, "expected a plain string literal in ‘asm’");
        char *name = str(tok->id);
        if (!name[0]) error(tok, "expected non-empty string in ‘asm’ clobber list");
        *cons = asm_cons_add(*cons, format("~{%s}", name));
        tok = tok->next;
        if (tok->kind != TK_COMMA) break;
        tok = tok->next;
    }
    *rest = tok;
    return tok;
}

// asm-goto-labels ::= identifier ("," identifier)*
// The labels are resolved with the ordinary gotos, at the end of the
// function: an asm goto may land anywhere a goto may, and the same checks
// apply to it. Each gets a node of the shape a goto uses, so the one pass
// that knows how a label is found does not have to know about asm -- a node
// of kind ND_NOP is one of these rather than a jump.
static Token *asm_goto_labels(Token **rest, Token *tok, Node *node) {
    while (tok->kind == TK_IDENT) {
        if (!node->asm_labels)
            node->asm_labels = vnew(4, sizeof(Node *));
        else
            node->asm_labels = vgrow(node->asm_labels, node->asm_nlabels + 1);

        Token *name = tok;
        Node *ref = new_node(ND_NOP, name);
        ref->label = get_ident(name);
        note_jump_scope(ref, scope);
        ref->goto_next = gotos;
        gotos = ref;
        node->asm_labels[node->asm_nlabels++] = ref;

        tok = tok->next;
        if (tok->kind != TK_COMMA) break;
        tok = tok->next;
        if (tok->kind != TK_IDENT) error(tok, "expected label name in ‘asm’ goto label list");
    }
    *rest = tok;
    return tok;
}

// AsmStmt ::= "asm" AsmQual* "(" string-literal
//                ( ":" AsmOperands? ( ":" AsmOperands? ( ":" Clobbers?
//                  ( ":" IdentList? )? )? )? )? ")" ";"
//
// A section that is not written is empty, and `:::` is three of them: the
// first colon opens the output list, so a run of colons is a run of empty
// sections. Only four may be written.
static Node *asm_stmt(Token **rest, Token *tok) {
    Token *start = tok;
    Node *node = new_node(ND_ASM, start);
    tok = asm_qualifiers(tok->next, &node->asm_flags);
    tok = skip(tok, TK_LPAREN);

    if (tok->kind != TK_STRLIT) error(tok, "expected string literal in ‘asm’");
    if (tok->enc_prefix != PREFIX_NONE) error(tok, "expected a plain string literal in ‘asm’");
    char *tmpl = str(tok->id);
    tok = tok->next;

    AsmOperand *ops = NULL, **tail = &ops;
    int nouts = 0, nins = 0;
    // The clobbers are collected apart from the operands: they come last in
    // the constraint string, however early in the statement they are written.
    char *clob = NULL;
    char *cons = NULL;
    // The four sections, each opened by a colon: outputs, inputs, clobbers,
    // labels. A section that is not written is empty, so a colon followed by
    // another opens one that holds nothing. `::` is a single token here -- it
    // is the scope qualifier of an attribute like [[gnu::const]] -- and
    // between two sections it is the two colons it is spelled with: reading
    // it leaves one of them owed, and while one is owed the section is at its
    // end already. A fifth colon has nowhere to go, which is where both
    // references stop too.
    int spare = 0;
    int sect = 0;
    for (; spare || tok->kind == TK_COLON || tok->kind == TK_COLONCOLON;) {
        if (sect == 4) error(tok, "expected ‘)’ before ‘:’ token");
        if (spare)
            spare--;
        else if (tok->kind == TK_COLON)
            tok = tok->next;
        else {
            spare = 1;
            tok = tok->next;
        }
        if (spare) {
            // The colon just read was the first half of a `::`, and its
            // second half opens the next section: this one is empty.
            sect++;
            continue;
        }
        if (sect == 0)
            tok = asm_operands(&tok, tok, true, &tail, &nouts);
        else if (sect == 1)
            tok = asm_operands(&tok, tok, false, &tail, &nins);
        else if (sect == 2)
            tok = asm_clobbers(&tok, tok, &clob);
        else
            tok = asm_goto_labels(&tok, tok, node);
        sect++;
    }
    if (node->asm_nlabels && !(node->asm_flags & ASM_GOTO)) error(start, "expected ‘goto’ before asm goto label list");

    // Two operands may not share a name: the template refers to them by it.
    for (AsmOperand *op = ops; op; op = op->next)
        for (AsmOperand *x = op->next; x; x = x->next)
            if (op->name && x->name && !strcmp(op->name, x->name))
                error(op->tok, "duplicate ‘asm’ operand name ‘%s’", op->name);

    node->asm_ops = ops;
    node->asm_nops = nouts + nins;
    // The block control falls through to when the template does not jump to
    // one of its labels: an asm goto is a terminator, so what follows it is
    // not the rest of this one.
    if (node->asm_nlabels) cnt_blk(1);
    // A statement with no output is one both references treat as volatile:
    // there is no value whose use could justify keeping it, so the template
    // must not be dropped or moved.
    if (!nouts) node->asm_flags |= ASM_VOLATILE;

    tok = skip(tok, TK_RPAREN);
    *rest = skip(tok, TK_SEMI);

    // Numbering first: GCC numbers the outputs, then the inputs, then the
    // labels, and the template's own references are to that numbering. The
    // constraint string lists the operands in the same order, so an operand's
    // place in it is its number -- and the input half of a `+` operand, which
    // GCC does not number, is put after every numbered input.
    int arg = 0, out_ord = 0, in_ord = 0, plus_ord = 0, nplus = 0, nret = 0;
    for (AsmOperand *op = ops; op; op = op->next)
        if (op->is_plus) nplus++;
    for (AsmOperand *op = ops; op; op = op->next) {
        op->index = op->is_output ? out_ord++ : nouts + in_ord++;
        op->pos = op->index;
        // A register output is one of the call's return values; an indirect
        // one travels as an address argument, in its place among the others.
        if (op->is_output && !op->is_indirect) {
            nret++;
            continue;
        }
        op->arg_pos = arg++;
    }
    for (AsmOperand *op = ops; op; op = op->next) {
        if (!op->is_plus) continue;
        op->plus_pos = nouts + nins + plus_ord;
        op->plus_arg_pos = arg++;
        // A `+` operand shares the output's register ("0"), or its address
        // ("*m"): the first is the matching constraint, the second is the
        // same memory operand named a second time.
        op->conv_in =
            op->is_indirect ? asm_cons_conv(op->cons + asm_cons_off(op->cons), false) : format("%u", op->index);
        plus_ord++;
    }

    for (AsmOperand *op = ops; op; op = op->next) cons = asm_cons_add(cons, op->conv);
    for (AsmOperand *op = ops; op; op = op->next)
        if (op->is_plus) cons = asm_cons_add(cons, op->conv_in);
    for (int i = 0; i < node->asm_nlabels; i++) cons = asm_cons_add(cons, "!i");
    if (clob) cons = asm_cons_add(cons, clob);
    // The clobbers every asm of this target implicitly has -- the x86 flags;
    // see the target's asm_clobbers.
    if (T.asm_clobbers) cons = asm_cons_add(cons, T.asm_clobbers);

    node->asm_cons = cons ? cons : "";
    node->asm_narg = arg;
    node->asm_nret = nret;
    // A statement with no colon is *basic* asm, and a basic template is not
    // rewritten at all: GCC's dialect alternatives, like its `%` escapes,
    // belong to the extended form. clang keeps `{a|b}` in a basic statement
    // and rewrites it in an extended one, operands or not.
    node->asm_tmpl = asm_tmpl_conv(start, tmpl, ops, node->asm_labels, node->asm_nlabels, nouts + nins + nplus,
                                   sect > 0 && T.asm_dialect_alt, false);
    return node;
}

// AsmDecl ::= "asm" AsmQual* "(" string-literal ")" ";"
// A file-scope asm statement has no operands: outside a function there is
// nothing for them to be evaluated in and no registers to bind them to. It
// becomes LLVM's `module asm`, one directive per statement, in source order.
static Token *asm_decl(Token *tok) {
    Token *start = tok;
    uint32_t flags = 0;
    Token *qual = tok->next;
    tok = asm_qualifiers(tok->next, &flags);
    // There is no statement for `volatile` to say anything about, and no
    // function for `goto` to jump inside: clang reports the first and gcc
    // does not even parse it.
    if (flags) error(qual, "meaningless ‘%.*s’ on asm outside function", qual->len, tok_text(qual));
    tok = skip(tok, TK_LPAREN);
    if (tok->kind != TK_STRLIT) error(tok, "expected string literal in ‘asm’");
    if (tok->enc_prefix != PREFIX_NONE) error(tok, "expected a plain string literal in ‘asm’");
    char *tmpl = str(tok->id);
    tok = tok->next;
    // The colon of an extended statement. gcc reports the register
    // constraint that has nowhere to go; there is no statement to be an
    // lvalue in, which is what its other half of the message is about.
    if (tok->kind == TK_COLON) error(tok, "constraint allows registers outside of a function");
    tok = skip(tok, TK_RPAREN);
    tok = skip(tok, TK_SEMI);

    // The template is still rewritten -- `%%` is one per cent -- but it goes
    // to the assembler as it stands, so a dollar sign stays one. There are no
    // operands for `%0` to name, and a number LLVM cannot resolve aborts its
    // backend rather than diagnosing it, so a reference is caught here.
    char *t = asm_tmpl_conv(start, tmpl, NULL, NULL, 0, 0, false, true);
    if (!curm->masm)
        curm->masm = vnew(4, sizeof(char *));
    else
        curm->masm = vgrow(curm->masm, curm->num_masm + 1);
    curm->masm[curm->num_masm++] = t;
    return tok;
}

// Stmt        ::= LabelStmt | UnLabelStmt
// LabelStmt   ::= Label Stmt
// UnLabelStmt ::= ExpStmt | PrimBlk | JmpStmt
// PrimBlk     ::= CompStmt | SelStmt | IterStmt
static Node *stmt(Token **rest, Token *tok) {
    Node *lb = label(&tok, tok);
    uint32_t i = push_named_loop(lb, tok);
    // A loop or switch records this so that `break name;` can find it.
    Node *outer_stmt_label = stmt_label;
    stmt_label = lb;
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
        case TK_ASM:
            stmt = asm_stmt(rest, tok);
            // The template of an asm goto may jump to a label, but control
            // also reaches the statement after it, and that is what the
            // fall-through bookkeeping asks about.
            falls_through = true;
            break;
        default:
            stmt = expr_stmt(rest, tok);
            break;
    }
    stmt_label = outer_stmt_label;
    while (i--) named_loop = named_loop->loop_next;

    // `[[fallthrough]];` marks the fall into the next label as deliberate,
    // which is the whole point of the attribute.
    //
    // 6.7.13.2p2 asks for an empty statement, and that much is checked above.
    // It does *not* ask for the label to be the next token: gcc and clang
    // both accept the annotation with a statement after it, with a user label
    // between, and at the end of the switch -- sqlite writes the last of those
    // -- so cxx does too, and only records that the fall is deliberate.
    if (has_fallthrough) falls_through = false;

    if (lb) {
        lb->label_body = stmt;
        return lb;
    }
    return stmt;
}

// CompStmt ::= "{" BlkItem* "}"
// BlkItem  ::= Decl | UnLabelStmt | Label
// The alignment cap `#pragma pack` last asked for: zero means the target's
// default. It is read where a record is laid out, so a record keeps the
// layout it was declared under after a later `pack(pop)`.
static int cur_pack;

// Consume a marker the preprocessor left behind, if this is one. The caller
// loops, since several `#pragma pack` lines may sit in a row.
static bool consume_pragma(Token **rest, Token *tok) {
    if (tok->kind != TK_PRAGMA) return false;
    cur_pack = (int)int128_to_i64(tok->ival);
    *rest = tok->next;
    return true;
}

static Node *compound_stmt2(Token **rest, Token *tok, bool is_func_body) {
    Node dummy, *cur = &dummy;
    Node *node = new_node(ND_COMP_STMT, tok);

    if (!is_func_body) {
        enter_scope();
    } else {
        // The bounds of a variably modified parameter: once per call, at the
        // top of the body. The whole assignment is emitted, not just the
        // expression -- the counter the parameter's type reads is what it
        // writes.
        for (int i = 0; i < scope->vla_num; i++) {
            cur = cur->next = new_unary(ND_EXPR_STMT, scope->vla_expr[i], scope->vla_expr[i]->tok);
            add_type(cur);
        }
        scope->vla_num = 0;
    }

    tok = tok->next;
    while (tok->kind != TK_RBRACE) {
        if (consume_pragma(&tok, tok)) continue;
        Token *start = tok;

        // Label
        Node *lb = label(&tok, tok);
        if (lb) {
            uint32_t i = push_named_loop(lb, tok);

            bool no_body = tok->kind == TK_RBRACE || is_typename(tok, true);
            if (no_body)
                lb->label_body = new_node(ND_EXPR_STMT, start);
            else
                lb->label_body = stmt(&tok, tok);
            // A label with no statement of its own falls straight out of the
            // block, into the next label if there is one.
            if (no_body) falls_through = true;

            cur = cur->next = lb;
            add_type(cur);

            while (i--) named_loop = named_loop->loop_next;
            continue;
        }

        // [GNU] __label__ Ident, ... ;
        if (tok->kind == TK_LABEL_DECL) {
            label_decl(&tok, tok);
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
                warn_cleanup_attrs(attrs);
                strip_cleanup_attr(ty);
                if (is_vm_type(ty)) note_vm_decl(ty->name, true);
                if (tok->kind == TK_AS)
                    error(tok,
                          "illegal initializer (only variables can be "
                          "initialized)");
                push_namespace(scope, get_ident(ty->name), SYM_TYNAME, ty, ty->name);
                // A variably modified typedef is where its bounds are
                // evaluated: gcc and clang both capture the size there, and
                // every later use of the type -- `sizeof(T)`, an object
                // declaration's size -- reads the counter they wrote rather
                // than running the bound again.
                for (int i = 0; i < scope->vla_num; i++) {
                    cur = cur->next = new_unary(ND_EXPR_STMT, scope->vla_expr[i], scope->vla_expr[i]->tok);
                    add_type(cur);
                }
                scope->vla_num = 0;
            } else {
                cur = cur->next = declaration(&tok, tok, basety, sclass, align, funcspec, attrs);
            }

            // A declaration is not a jump: control reaches the statement that
            // follows it, whatever its initializer does.
            falls_through = true;
            add_type(cur);
            continue;
        }

        // UnLabelStmt
        cur = cur->next = stmt(&tok, tok);
        add_type(cur);
    }

    // __attribute__((cleanup(f))): leaving the block destroys its objects,
    // most recently declared first, and before the stack pointer of a
    // variable length one is put back. The function body is the same thing,
    // reached by falling off its end; a return out of the block runs the same
    // handlers by carrying them itself (Node.unwind).
    for (int i = scope->cleanup_num; i-- > 0;) {
        Node *call = cleanup_call(scope->cleanups[i].var, scope->cleanups[i].fn, tok);
        cur = cur->next = new_unary(ND_EXPR_STMT, call, tok);
        add_type(cur);
    }
    if (!is_func_body) {
        Node *restore = leave_scope(tok);
        if (restore) cur = cur->next = restore;
    }
    cur->next = NULL;
    *rest = skip(tok, TK_RBRACE);

    // An empty block falls through. A block with items in it has already
    // left the answer in falls_through.
    if (!dummy.next) falls_through = true;
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

// 6.7.13.5 / -Wdeprecated-declarations: a type declared [[deprecated]] is
// reported at every use that names it, which is what gcc and clang do. The
// definition itself is not a use, and neither is a bare redeclaration
// (`struct S;`), so both callers check the token that follows.
static void check_deprecated_ty(Type *ty, Token *tok) {
    for (Attr *a = ty->attrs; a; a = a->next)
        if (a->info && !strcmp(a->info->name, "deprecated")) {
            warning(WG_DEPRECATED, tok, "‘%s’ is deprecated", str(tok->id));
            return;
        }
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
            if (tok->kind != TK_SEMI) check_deprecated_ty(ty, tag);
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
    complete_copies(ty);
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
        // An anonymous struct or union member contributes its own members
        // to the enclosing record, so they are what has to be looked for.
        // The member itself has no name, and asking for one -- which is what
        // this used to do, having no `continue` here -- passed a null token
        // to get_struct_member(), which dereferenced it.
        if ((mem2->ty->kind == TY_STRUCT || mem2->ty->kind == TY_UNION) && !mem2->name) {
            check_anon_mem(mem1, mem2->ty->members);
            continue;
        }

        // An unnamed bit-field is the only other member without a name, and
        // it declares nothing that could be declared twice.
        if (!mem2->name) continue;

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
        int mem_funcspec = 0;
        Attr *mem_attrs = NULL;
        Type *basety = declspecs(&tok, tok, NULL, &align, &mem_funcspec, &mem_attrs);
        // An attribute in the specifier position belongs to the member:
        // `__attribute__((aligned(16))) int b;` and
        // `__attribute__((aligned(16))) char a : 4;` both raise the member's
        // alignment, and with it the record's. Both out-parameters used to be
        // NULL here, so the attribute was parsed and dropped -- only the
        // post-declarator spelling reached mem->ty->attrs.
        // Only `_Alignas` is refused on a bit-field; the GNU attribute is
        // accepted, so remember which value came from where.
        int alignas_align = align;
        attr_decl_apply(mem_attrs, &mem_funcspec, &align, true);
        int i = 0;
        Token *start = tok;

        // Anonymous struct member
        if (match(&tok, tok, TK_SEMI)) {
            if (!is_record(basety)) {
                warning(WG_DEFAULT, start, "declaration does not declare anything");
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
                if (alignas_align) error(start, "'_Alignas' cannot be applied to a bit-field");
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
                if (alignas_align) error(start, "'_Alignas' cannot be applied to a bit-field");
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
    if (bytes == 8) return is_unsigned ? T.ty_ullong : T.ty_llong;
    // Wider than anything the target has: a packed field can start in one
    // eight-byte unit and end in the next (a 63-bit field at bit offset 2
    // reaches bit 65), and _BitInt is what reaches past eight bytes.
    // bit_offset is at most 7 and a field is at most 64 bits, so 9 bytes is
    // as far as this goes.
    return bitint[bytes * 8][is_unsigned];
}

static int min_bytes_for_bits(int bits) {
    if (bits <= 8) return 1;
    if (bits <= 16) return 2;
    if (bits <= 32) return 4;
    if (bits <= 64) return 8;
    // The field crosses an eight-byte boundary: the unit has to cover all of
    // it. Stopping at 8 here made the unit too small for the field, and the
    // load shifted by a negative amount.
    return (bits + 7) / 8;
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

    // `#pragma pack(n)` caps the alignment of everything the record holds,
    // and with n == 1 the layout is the packed one -- gcc packs twelve bits
    // and then seven with no storage-unit boundary between them, which is
    // what tinycc's tests2/95_bitfields.c measures as a seven-byte record.
    bool packed_layout = ty->is_packed || cur_pack == 1;

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
    // What the members would ask for with no pragma in the way, and whether
    // the pragma lowered anything. The two together say whether LLVM's own
    // layout of the element types is the C layout (see `layout_packed`).
    int natural_align = 1;
    bool lowered = false;

    for (Member *mem = ty->members; mem; mem = mem->next) {
        natural_align = MAX(natural_align, mem->align);
        int mem_align = (ty->is_packed || mem->is_packed) ? 1 : mem->align;
        if (mem->is_align) mem_align = mem->align;  // explicit alignment overrides packed
        if (cur_pack > 0 && mem_align > cur_pack) {
            mem_align = cur_pack;
            lowered = true;
        }
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
            } else if (!packed_layout) {
                // The field must not cross its unit's boundary.
                while (s + width > (s / unit + 1) * unit) s = (s / unit + 1) * unit;
            }
            // An explicitly aligned bit-field starts where its alignment
            // says: gcc puts `__attribute__((aligned(16))) char a : 4;` at
            // byte 16 of the record, not directly after the field before it.
            // The floor counts too -- under `#pragma pack(push,1)` the
            // alignment is capped to one byte and the field still moves to
            // the next byte boundary.
            if (mem->is_align) s = ALIGN_UP(s, (uint64_t)mem_align * 8);
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

    // `#pragma pack(4)` on `struct { double d; char c; }` lowers the record's
    // own alignment as well, and with it the size -- 12, where LLVM's type
    // would round 9 up to its own alignment of 8 and give 16. An explicit
    // aligned(N) raises the alignment and is not a lowering.
    if (cur_pack > 0 && natural_align > cur_pack && !attr_align) lowered = true;
    ty->layout_packed = ty->is_packed || lowered;
    for (Member *m = ty->members; !ty->layout_packed && m; m = m->next)
        if (m->is_packed) ty->layout_packed = true;
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
            if (tok->kind != TK_SEMI) check_deprecated_ty(ty, tag);
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
    complete_copies(ty);

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
    // Only the type is wanted, so only a variable length operand is
    // evaluated -- the same rule, and the same parking, as sizeof's.
    uint32_t park_mark = num_parked;
    bool outer_uneval = uneval_operand;
    uneval_operand = true;
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
    uneval_operand = outer_uneval;
    if (ty->kind == TY_VLA)
        unpark_refs(park_mark);
    else
        num_parked = park_mark;
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
        // vector_size and ext_vector_type land here too, and staying a
        // warning is deliberate: glibc's own <link.h> has
        // `typedef float La_x86_64_xmm __attribute__ ((__vector_size__ (16)))`,
        // which <execinfo.h> pulls in, and refusing it stops programs that
        // merely include <link.h> from compiling (cpython's Python/traceback.c
        // and Modules/_ctypes/callproc.c do). gcc and clang warn about
        // attributes they do not know as well; that the vector is then a
        // scalar is the documented cost of not having vector types (3.5.3).
        if (!info) warning(WG_ATTRIBUTES, start, "unknown attribute '%s' ignored", name);
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
            if (tok->kind == TK_EOF) error(start, "expected ')'");
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
        if (tok->kind == TK_EOF) error(start, "expected ']]'");
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
        if (tok->kind == TK_EOF) error(start, "expected ')'");
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
        // With gnu_only this is the post-declarator position of a function
        // declarator, which both references treat as a type attribute
        // position: `void g(void) [[gnu::noreturn]];` is warned about and
        // ignored rather than applied.
        if (gnu_only && !a->is_gnu) continue;
        if (!strcmp(a->info->name, "noreturn")) {
            *funcspec |= Q_NORETURN;
        } else if (!strcmp(a->info->name, "aligned") && a->args) {
            Token *t;
            *align = MAX(*align, (int)const_expr(&t, a->args->next));
        }
    }
}

// The `noreturn` attribute in a declaration, whichever spelling carried
// it. The standard one is a constraint and clang reports it as an error;
// the GNU one is ignored with a warning.
static Attr *find_noreturn_attr(Attr *attrs) {
    for (Attr *a = attrs; a; a = a->next)
        if (a->info && !strcmp(a->info->name, "noreturn")) return a;
    return NULL;
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
// The declaration attributes a typedef collected in front of it: the type is
// the last thing a typedef declares, so `cleanup` never reaches an object of
// its own, and both references warn about it there.
static void warn_cleanup_attrs(Attr *attrs) {
    for (Attr *a = attrs; a; a = a->next)
        if (a->info && a->info->ns == ATTR_NS_GNU && !strcmp(a->info->name, "cleanup"))
            warning(WG_ATTRIBUTES, a->tok, "‘cleanup’ attribute only applies to local variables");
}

// `cleanup` is a variable attribute, and both references ignore it on a
// typedef -- with a warning -- leaving the objects the type later declares
// alone. Dropping the entry is what keeps them alone: the copy `decl_attrs()`
// made belongs to this declaration only.
static void strip_cleanup_attr(Type *ty) {
    for (Attr **link = &ty->attrs; *link;) {
        Attr *a = *link;
        if (a->info && a->info->ns == ATTR_NS_GNU && !strcmp(a->info->name, "cleanup")) {
            warning(WG_ATTRIBUTES, a->tok, "‘cleanup’ attribute only applies to local variables");
            *link = a->next;
            continue;
        }
        link = &a->next;
    }
}

static void apply_postdecl_attrs(Type *ty) {
    for (Attr *a = ty->attrs; a; a = a->next) {
        if (!a->info || a->is_gnu) continue;
        if (a->info->ns == ATTR_NS_CLANG) continue;  // recognized and ignored
        if (!strcmp(a->info->name, "aligned") && a->args) {
            Token *t;
            ty->align = MAX(ty->align, (int)const_expr(&t, a->args->next));
        } else if (a->info->ns == ATTR_NS_GNU && (a->info->targets & ATTR_DECL) && ty->kind != TY_FUNC) {
            // Written after the declarator, a GNU declaration attribute is
            // still a declaration attribute -- sym_attr_flags() reads it --
            // and not a type attribute that cannot apply. The `__attribute__`
            // spelling never reached here either (the loop skips it).
        } else if (!strcmp(a->info->name, "packed") || !(a->info->targets & ATTR_TYPE) || ty->kind == TY_FUNC) {
            warning(WG_ATTRIBUTES, a->tok, "attribute '%s' ignored, because it cannot be applied to a type",
                    attr_disp_name(a));
        }
    }
}

// Set the per-symbol flags for declaration attributes.
// The priority an initializer attribute carries: its argument, or the
// default both gcc and clang use.
static int attr_prio(Attr *a) {
    if (!a->args || a->args->next->kind == TK_RPAREN) return 65535;
    Token *t;
    int prio = (int)const_expr(&t, a->args->next);
    if (prio < 0 || prio > 65535) error(a->tok, "priority for ‘%s’ must be between 0 and 65535", a->info->name);
    return prio;
}

static void sym_attr_flags(Sym *var, Attr *attrs, bool gnu_only) {
    for (Attr *a = attrs; a; a = a->next) {
        if (!a->info) continue;
        // The flag means the GNU namespace, not the __attribute__ spelling:
        // `int x [[gnu::cleanup(h)]]` carries the same attribute as
        // `int x __attribute__((cleanup(h)))`, and both have to be read here
        // (see apply_postdecl_attrs for where that holds).
        if (gnu_only && a->info->ns != ATTR_NS_GNU) continue;
        if (!strcmp(a->info->name, "deprecated"))
            var->is_deprecated = true;
        else if (!strcmp(a->info->name, "nodiscard")) {
            if (!var->is_function) warning(WG_ATTRIBUTES, a->tok, "‘nodiscard’ attribute only applies to functions");
            var->is_nodiscard = true;
        } else if (!strcmp(a->info->name, "maybe_unused"))
            var->is_maybe_unused = true;
        else if (!strcmp(a->info->name, "unused"))
            var->is_unused = true;
        else if (!strcmp(a->info->name, "constructor")) {
            if (var->is_function) var->ctor_prio = attr_prio(a);
        } else if (!strcmp(a->info->name, "destructor")) {
            if (var->is_function) var->dtor_prio = attr_prio(a);
        } else if (!strcmp(a->info->name, "cleanup")) {
            // The handler is looked up where one can run -- an automatic
            // object at block scope -- and the attribute is diagnosed as
            // ignored everywhere else, so all this keeps is the argument.
            if (!a->args || a->args->next->kind == TK_RPAREN) error(a->tok, "‘cleanup’ attribute takes one argument");
            var->cleanup_attr = a;
        }
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
                } else if (declspec_pos_attr(a->info) ||
                           (a->info->ns == ATTR_NS_GNU && (a->info->targets & ATTR_DECL))) {
                    // Declaration attributes. Both spellings of a GNU
                    // attribute accept the full declaration set, as in
                    // clang: `__attribute__((unused))` and `[[gnu::unused]]`
                    // are the same attribute.
                    a->next = NULL;
                    if (attrs) attr_cur = attr_cur->next = a;
                } else if (a->info->ns == ATTR_NS_CLANG) {
                    // clang:: attributes are recognized and ignored.
                } else if (!seen_declspec && !a->is_gnu && a->info->ns == ATTR_NS_GNU &&
                           (a->info->targets & ATTR_TYPE) && (tok->kind == TK_STRUCT || tok->kind == TK_UNION)) {
                    error(a->tok, "misplaced attributes; expected attributes here");
                } else {
                    warning(WG_ATTRIBUTES, a->tok, "unknown attribute '%s' ignored", str(a->tok->id));
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
                // Whether it is allowed is decided where the qualifier meets
                // the type: on an array it belongs to the element, which is
                // where a pointer may be found.
                qual |= Q_RESTRICT;
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
                    check_deprecated_ty(orig, tok);
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
            case TK_F32X:
                // Same representation as double.
                typespec_cnt += DOUBLE;
                break;
            case TK_F64X:
                // Same representation as long double.
                typespec_cnt += LONG + DOUBLE;
                break;
            case TK_F128X:
                // Same representation as _Float128.
                typespec_cnt += F128;
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
    // A qualifier written on an array type applies to the element type, not
    // to the array type (C11 6.7.3p9). Without this, `const A` and
    // `const int[3]` are different types and a redeclaration is rejected.
    if (is_array(ty) || ty->kind == TY_VLA)
        ty = array_elem_qual(ty, qual);
    else
        ty = type_qual(ty, qual);
    // `restrict` may qualify only a pointer to an object (6.7.3p2), and the
    // rule above may have moved it to an array's element.
    if (qual & Q_RESTRICT) {
        Type *elem = ty;
        while (is_array(elem) || elem->kind == TY_VLA) elem = elem->base;
        if (!is_pointer(elem)) error(tok, "restrict requires a pointer");
    }
    if (type_attrs) {
        ty = copy_type(ty);
        ty_prepend_attrs(ty, type_attrs);
    }
    if (attrs) *attrs = dummy_a.next;
    return ty;
}

// The symbol a function declarator made for a named parameter. Its prototype
// scope is gone by the time the definition is parsed, so the definition path
// finds it here and adopts it -- which is what keeps a bound expression that
// named an earlier parameter pointing at the parameter the body sees.
typedef struct ParamSym ParamSym;
struct ParamSym {
    Type *ty;
    Sym *var;
};
static ParamSym *param_syms;
static uint32_t num_param_syms;

static void note_param_sym(Type *ty, Sym *var) {
    if (!param_syms)
        param_syms = vnew(8, sizeof(ParamSym));
    else
        param_syms = vgrow(param_syms, num_param_syms + 8);
    param_syms[num_param_syms].ty = ty;
    param_syms[num_param_syms].var = var;
    num_param_syms++;
}

static Sym *param_sym_of(Type *ty) {
    for (uint32_t i = 0; i < num_param_syms; i++)
        if (param_syms[i].ty == ty) return param_syms[i].var;
    return NULL;
}

// The prototype scope the last function declarator opened, or NULL. A
// definition takes the bounds it registered over into the function's own
// scope, where they are evaluated once per call, at the top of the body, with
// every parameter they may name already in scope. A declaration has no body
// to run them in: 6.7.6.2p5 treats a non-constant size in prototype scope as
// `[*]`, so there is nothing to evaluate and the entries go away with the
// scope.
static Scope *proto_scope;

// The symbols the last function declarator created (a parameter type's
// counters, the hidden stack pointer). They are unlinked from `locals` while
// the declarator is parsed -- a prototype keeps none of them, and a definition
// relinks them *after* the parameters, which irgen reads as the leading
// entries of that list.
static Sym *proto_locals;

static void adopt_proto_locals(void) {
    for (Sym *v = proto_locals; v;) {
        Sym *next = v->next;
        v->next = locals;
        locals = v;
        v = next;
    }
    proto_locals = NULL;
}

static void adopt_proto_vla_exprs(void) {
    if (!proto_scope || !proto_scope->vla_num) return;
    for (int i = 0; i < proto_scope->vla_num; i++) {
        scope->vla_expr = vgrow(scope->vla_expr, scope->vla_num + 1);
        scope->vla_expr[scope->vla_num++] = proto_scope->vla_expr[i];
    }
    proto_scope->vla_num = 0;
}

static Type *func_param(Token **rest, Token *tok, Type *ty) {
    tok = skip(tok, TK_LPAREN);
    if (tok->kind == TK_VOID && tok->next->kind == TK_RPAREN) {
        // No parameter list, so nothing for a definition to take over either.
        proto_scope = NULL;
        proto_locals = NULL;
        *rest = tok->next->next;
        return func_type(ty);
    }

    // 6.2.1p7: a parameter name is in scope from its declaration to the end
    // of the function declarator. That is this scope, and it ends here.
    enter_scope();
    Scope *proto = scope;
    Sym *saved_locals = locals;
    proto_scope = NULL;
    proto_locals = NULL;

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
            Attr *param_attrs = NULL;
            SClass psclass = 0;
            Type *basety = declspecs(&tok, tok, &psclass, NULL, NULL, &param_attrs);
            // 6.7.6.3p2: `register` is the only storage-class specifier a
            // parameter may carry. A NULL sclass here refused every one of
            // them, which is what stopped git's kwset.c at
            // `register struct tree const *tree`.
            if (psclass & ~SC_REG)
                error(start, "storage class ‘%s’ is not allowed on a parameter", sclass_name[psclass & ~SC_REG]);
            Type *paramty = abstract_declarator(&tok, tok, basety, true);
            apply_postdecl_attrs(paramty);
            // A declaration attribute in front of the type belongs to this
            // parameter as well, and it has to sit on this parameter's own
            // copy of the type.
            if (param_attrs) {
                paramty = copy_type(paramty);
                ty_prepend_attrs(paramty, param_attrs);
            }
            // A parameter has no scope of its own to be left, so a cleanup
            // attribute here is ignored -- with a warning in every
            // declaration, prototype included, which is where the references
            // warn as well.
            strip_cleanup_attr(paramty);
            if (paramty->kind == TY_VOID) error(start, "argument may not have ‘void’ type");
            // "array of T" is converted to "pointer to T" in the parameter
            // context. For example, *argv[] is converted to **argv by this.

            if (paramty->kind == TY_ARRAY || paramty->kind == TY_VLA) {
                Type *arr = paramty;
                paramty = pointer_to(paramty->base, paramty->qual);
                paramty->name = arr->name;
                // is_static / is_star share their storage with a VLA's
                // vla_len / vla_cnt, so reading them off one compares the low
                // half of a pointer; they only describe a fixed array
                // declarator anyway.
                if (arr->kind != TY_VLA) {
                    paramty->is_star = arr->is_star;
                    paramty->is_static = arr->is_static;
                }
            }
            if (paramty->kind == TY_FUNC) {
                Type *fn = paramty;
                paramty = pointer_to(paramty, 0);
                paramty->name = fn->name;
            }

            if (paramty->name) {
                uint32_t id = get_ident(paramty->name);
                for (Type *p = dummy.next; p && p->name; p = p->next) {
                    if (id == p->name->id) {
                        diag("error", paramty->name, "redefinition of parameter ‘%s’", str(id));
                        diag_exit("note", p->name, "previous definition is here");
                    }
                }
            }

            Type *copy = copy_type(paramty);
            // The name is pushed as it is parsed, so that the bounds of the
            // parameters after it can use it. The symbol is the one a
            // definition will use for this parameter (the definition path
            // adopts it), and a declaration simply never adopts it: it is not
            // linked into any function's locals, so no prototype leaves a
            // slot behind either.
            if (copy->name) {
                uint32_t id = get_ident(copy->name);
                Sym *pvar = new_var(id, copy);
                pvar->is_local = true;
                note_param_sym(copy, pvar);
                push_namespace(scope, id, SYM_VAR, copy, copy->name)->var = pvar;
            }
            cur = cur->next = copy;
            nparam++;
        }
    }

    *rest = skip(tok, TK_RPAREN);
    leave_scope(tok);
    // What the parameter types made belongs to no function yet; the
    // definition that follows is the one that can take it over. `locals` goes
    // back to what it was: a prototype keeps no symbols of its own.
    if (locals != saved_locals) {
        proto_locals = locals;
        locals = saved_locals;
    }
    // The outermost declarator finishes last, so this is the parameter list a
    // definition that follows belongs to.
    proto_scope = proto;

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
                // 6.8.6.1p1: the jump may not land inside the scope of an
                // identifier with a variably modified type. An asm goto's
                // labels are jumps of the same kind, which clang words
                // differently.
                char *what = x->kind == ND_NOP
                                 ? "cannot jump from this asm goto statement to one of its possible targets"
                                 : "cannot jump from this goto statement to its label";
                Scope *from = jump_scope_of(x);
                Scope *to = jump_scope_of(y);
                check_vm_jump(x->tok, what, from, jump_seq_of(x), to, jump_seq_of(y));
                // The handlers of the scopes this jump leaves run before it:
                // the label's own scope is still live where it lands. An asm
                // goto cannot run them -- the jump is inside the template,
                // and the path that falls out of it still needs them.
                if (from && x->kind != ND_NOP) x->unwind = cleanup_leaving(from, to, x->tok);
                break;
            }

        // The name is the whole reference for an asm goto, which has no
        // token after it to point at.
        if (!x->target) error(x->kind == ND_NOP ? x->tok : x->tok->next, "use of undeclared label");
    }

    gotos = labels = NULL;
    num_jump_scopes = 0;
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

    // A file-scope asm statement: `asm(".globl f");`. Only the basic form is
    // one -- a constraint outside a function has no register to be bound to,
    // which is what asm_decl() reports.
    if (tok->kind == TK_ASM) return asm_decl(tok);

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
        // 6.7.6.2p2: a variably modified type needs a block scope, the bound
        // being something that is evaluated rather than a constant. Taking
        // one as a constant here emitted the object with no type at all
        // (`global (null)`), and only the backend noticed.
        if (is_vm_type(ty)) error(var_name, "variably modified ‘%s’ at file scope", str(var_name->id));
        NameSpace *ns = find_ident(var_name, false, false);
        Sym *var;
        bool is_fn = ty->kind == TY_FUNC;
        // GNU post-declarator attributes attach to the declaration.
        int fspec = funcspec;
        attr_decl_apply(ty->attrs, &fspec, &align, ty->kind == TY_FUNC);
        if (fspec && !is_fn) {
            // See the same check in the block-scope declaration path: the
            // attribute is a type of function pointer's, or it is ignored.
            if (fspec & Q_NORETURN) {
                Attr *nr = find_noreturn_attr(attrs);
                if (!nr) nr = find_noreturn_attr(ty->attrs);
                if (nr && nr->info->ns == ATTR_NS_STD)
                    error(nr->tok, "‘noreturn’ can only appear on functions");
                else if (is_funcptr(ty)) {
                    // See the block-scope path above.
                } else {
                    warning(WG_ATTRIBUTES, tok, "‘noreturn’ attribute ignored");
                    fspec &= ~Q_NORETURN;
                }
            }
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

            // A definition's parameters have to be complete, a declaration's
            // do not: `void show_reflog_message(..., struct date_mode, ...)`
            // in git's reflog-walk.h names one that the header never
            // completes, and both references take it. The check used to sit
            // in func_param(), where it fired on the declaration -- and
            // dereferenced a name token an unnamed parameter does not have,
            // taking cxx down instead of reporting.
            for (Type *p = ty->params; p; p = p->next)
                if (p->size < 0)
                    error(p->name ? p->name : var_name, "parameter has incomplete type ‘%s’", diag_type_name(p));

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
                var->all_decls_inline = true;
                var->sclass = sclass;
            }

            // asm("name") may follow the declarator of a function
            // definition, optionally followed by attributes:
            //   int f(void) __asm__("g") __attribute__((noreturn)) { }
            ty = decl_attrs(&tok, tok, ty);
            set_asm_name(var, asm_name);

            // Where a diagnostic about the symbol points: the definition
            // when there is one, else the declaration that created it.
            var->tok = var_name;
            var->is_defined = true;
            var->funcspec |= fspec;
            note_inline_decl(var, fspec, sclass);
            sym_attr_flags(var, attrs, false);
            sym_attr_flags(var, ty->attrs, true);
            if (var->cleanup_attr) {
                warning(WG_ATTRIBUTES, var_name, "‘cleanup’ attribute only applies to local variables");
                var->cleanup_attr = NULL;
            }
            cur_fn = var;
            cur_fn->num_blk = 2;  // fn->start + fn->end
            cur_fn->num_lbl = 0;
            locals = NULL;
            vm_decls = NULL;
            vm_seq = 0;
            enter_scope();
            // The parameter bounds belong to this function now; the body puts
            // them at its top, where the parameters they name are in scope.
            adopt_proto_vla_exprs();

            Type *param = ty->params;
            while (param) {
                if (is_pointer(param) && param->is_star)
                    error(var_name, "‘[*]’ not allowed in other than function prototype scope");
                uint32_t id = id_anon;
                if (param->name) id = get_ident(param->name);
                // No token: the unused-variable walk keys on one, and a
                // parameter is -Wunused-parameter's business, not
                // -Wunused-variable's.
                // The declarator's own symbol is taken over when it made one,
                // so a bound that named this parameter refers to the object
                // the body sees; an unnamed parameter gets a fresh one.
                Sym *pvar = param_sym_of(param);
                if (pvar) {
                    pvar->next = locals;
                    locals = pvar;
                } else {
                    pvar = new_lvar(id, param);
                }
                sym_attr_flags(pvar, param->attrs, true);
                push_namespace(scope, id, SYM_VAR, ty, param->name)->var = pvar;
                param = param->next;
            }
            // The declarator's own symbols (a parameter type's counters, the
            // hidden stack pointer) go in *under* the parameters: `locals` is
            // reversed once the body is parsed, so what is prepended last
            // comes out first, and irgen reads the leading entries as the
            // parameters.
            adopt_proto_locals();

            //  "__func__" is automatically defined as if
            // static const char __func__[] = "function-name";
            // [GNU] "__FUNCTION__" and "__PRETTY_FUNCTION__" are further
            // names of "__func__". In C gcc makes the last one the same
            // string; clang spells the function's type out ("int f(int)"),
            // which would need a C declarator printer of its own, so cxx
            // follows gcc here.
            Type *fn_name = array_of(T.ty_char, str_len(var->id) + 1);

            NameSpace *tmp = push_namespace(scope, id_func, SYM_VAR, fn_name, var_name);
            NameSpace *tmp2 = push_namespace(scope, id_function, SYM_VAR, fn_name, var_name);
            NameSpace *tmp3 = push_namespace(scope, id_pretty, SYM_VAR, fn_name, var_name);

            tmp3->var = tmp2->var = tmp->var = new_string_literal(var->id, fn_name);

            fn_prologue_first = fn_prologue_last = NULL;
            fn_vla_guard_num = 0;
            fn_vla_decls = 0;
            var->body = compound_stmt2(&tok, tok, true);
            // More than one variable length array in the function: the reuse
            // guard of each would free the objects of the others, which are
            // still alive. Only a lone one keeps its guard.
            if (fn_vla_decls > 1) {
                for (int i = 0; i < fn_vla_guard_num; i++) {
                    fn_vla_guards[i]->cond = new_num(0, tok);
                    add_type(fn_vla_guards[i]->cond);
                }
            }
            if (fn_prologue_first) {
                fn_prologue_last->next = var->body->body;
                var->body->body = fn_prologue_first;
                fn_prologue_first = fn_prologue_last = NULL;
            }

            var->locals = reverse_list(Sym, locals, next);

            // -Wunused-variable: a local that nothing ever resolved to an
            // ND_VAR. Nameless symbols are the compiler's own temporaries
            // (they share this list), [[maybe_unused]] and
            // __attribute__((unused)) opt out, and a parameter has no token
            // so it is skipped -- that warning is gcc's -Wunused-parameter,
            // which is not in -Wall.
            for (Sym *v = var->locals; v; v = v->next) {
                if (!v->tok || v->is_referenced || v->is_unused || v->is_maybe_unused) continue;
                if (v->sclass & (SC_STATIC | SC_EXTERN)) continue;
                warning(WG_UNUSED_VARIABLE, v->tok, "unused variable \u2018%s\u2019", str(v->id));
            }
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
        // 6.9.2p2: a file-scope declaration of an object with no initializer
        // and no `extern` is a *tentative definition*, and the unit defines
        // the object unless something else in it does. Whether this
        // declaration is one is settled here, where the token still says so.
        bool is_tentative = !is_fn && !(sclass & (SC_EXTERN | SC_TYPEDEF)) && tok->kind != TK_AS;
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
            warn_cleanup_attrs(attrs);
            strip_cleanup_attr(ty);
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
                var->all_decls_inline = is_fn;
                var->sclass = sclass;
                var->align = MAX(align, ty->align);
                ns = push_namespace(scope, var->id, symkind, ty, var_name);
                ns->var = var;
                ns->lnk = sclass & (SC_STATIC | SC_CONSTEXPR) ? LK_INTERN : LK_EXTERN;
            }

            var->tok = var_name;

            // The symbol's storage class is the first declaration's, so a
            // name first seen as `extern Target T;` carried SC_EXTERN even
            // after the tentative definition -- LLVM got `@T = external
            // global` for a unit that defines T, and the link failed with
            // `undefined reference to T`.
            if (is_tentative) var->sclass &= ~SC_EXTERN;

            // asm("name") for a file-scope object or function
            // declaration, optionally followed by attributes:
            //   extern int fscanf(...) __asm__("__isoc23_fscanf") __wur;
            //   int x __asm__("y") = 7;
            ty = decl_attrs(&tok, tok, ty);
            if (asm_name) {
                set_asm_name(var, asm_name);
            } else {
                apply_alias_attr(var, attrs);
                apply_alias_attr(var, ty->attrs);
            }

            if (ty->kind == TY_VOID) error(var_name, "variable ‘%s’ declared void", str(var_name->id));

            // 6.2.7: two compatible declarations of one object give it their
            // composite type. A later declaration can supply the length an
            // earlier one omitted -- `int a[]; int a[10];` -- and because
            // every use of the name refers to the one object, the object has
            // to take the complete type; keeping the first declaration's
            // int[] is what made `sizeof(a)` an incomplete-type error.
            if (ns && var == ns->var) {
                Type *comp = composite_type(var->ty, ty);
                if (comp != var->ty) var->ty = ns->ty = comp;
            }

            if (tok->kind == TK_AS) {
                // Like clang: atomic aggregates cannot be brace-initialized.
                if ((ty->qual & Q_ATOMIC) && (ty->kind == TY_STRUCT || ty->kind == TY_UNION) &&
                    tok->next->kind == TK_LBRACE)
                    error(var_name, "illegal initializer type '_Atomic(%s)'", record_diag_name(ty));
                Sym *outer = cur_init;
                cur_init = var;
                gvar_initializer(&tok, tok->next, var);
                cur_init = outer;
                var->is_defined = true;
                // A declaration with an initializer is a definition, whatever
                // an earlier `extern` declaration said. SC_EXTERN is this
                // compiler's "no definition here" mark, and it is what the
                // printer reads to choose between the initializer and a bare
                // type -- so `const Fp128 FP128_ONE = ...` after the header's
                // `extern const Fp128 FP128_ONE;` was emitted as a
                // declaration, and the link failed with `undefined reference
                // to FP128_ONE`.
                var->sclass &= ~SC_EXTERN;
                if (sclass & SC_CONSTEXPR) {
                    // A constexpr initializer must be a constant expression.
                    int64_t v;
                    uint32_t s = 0;
                    constexpr_fold(var, &v, &s);
                }
            }
            var->funcspec |= fspec;
            if (is_fn) note_inline_decl(var, fspec, sclass);
            sym_attr_flags(var, attrs, false);
            sym_attr_flags(var, ty->attrs, true);
            if (var->cleanup_attr) {
                warning(WG_ATTRIBUTES, var_name, "‘cleanup’ attribute only applies to local variables");
                var->cleanup_attr = NULL;
            }
            if (var->ty->kind == TY_ARRAY && var->ty->base->size < 0)
                error(var_name, "array has incomplete element type");
            if (var->ty->size < 0 && !(var->sclass & SC_EXTERN) &&
                (var->ty->kind != TY_ARRAY && var->ty->kind != TY_VLA))
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

// 6.9.2p2: the type of a tentative definition is the composite type of
// its declarations as of the end of the translation unit. When that is
// still an array of unknown size, gcc and clang assume one element and say
// so -- and the object has to be emitted with some length, because a global
// of unknown size cannot be written down in LLVM IR at all.
static void complete_tentative_arrays(void) {
    for (Sym *sym = globals; sym; sym = sym->next) {
        if (sym->is_function || !is_user_global(sym)) continue;
        // An extern declaration is not a definition, so there is nothing to
        // complete; it is emitted as an array of length zero instead.
        if (sym->sclass & SC_EXTERN) continue;
        if (sym->ty->kind != TY_ARRAY || sym->ty->len >= 0) continue;

        sym->ty = array_of(sym->ty->base, 1);
        warning(WG_TENTATIVE_DEFINITION_ARRAY, sym->tok, "tentative array definition assumed to have one element");
    }
}

// 6.7.5p8: a definition with external linkage whose every file scope
// declaration is `inline` without `extern` is an inline definition. It is
// not an external definition, so this translation unit does not define the
// symbol: calls to it become undefined references unless another unit
// defines it, which is what gcc and clang do at -O0 as well.
//
// p5 is the other half, and a constraint: an inline declaration has to be
// defined in the same unit. gcc diagnoses a violation, clang does not.
static void check_inline_definitions(void) {
    for (Sym *sym = globals; sym; sym = sym->next) {
        if (!sym->is_function || !is_user_global(sym)) continue;
        // Internal linkage always defines the function here.
        if (sym->sclass & SC_STATIC) continue;

        if (!sym->body) {
            // p5: declared inline, never defined in this unit.
            if (sym->funcspec & Q_INLINE)
                warning(WG_DEFAULT, sym->tok, "inline function \u2018%s\u2019 declared but never defined",
                        str(sym->tok->id));
            continue;
        }
        if (!sym->all_decls_inline) continue;
        sym->is_inline_def = true;
        // p3: an inline definition may not define a modifiable static.
        if (sym->static_local_tok)
            warning(WG_STATIC_LOCAL_IN_INLINE, sym->static_local_tok,
                    "non-constant static local variable in inline function may be different in different files");
    }
}

// -Wunused-function / -Wunused-const-variable, and the dead code
// elimination that goes with them.
//
// The graph is the one add_ref() builds while parsing: every file-scope name
// a body or an initializer resolves is an edge from the symbol being parsed.
// A definition another translation unit can see -- external linkage, and for
// an object a definition rather than a declaration -- is a root, and
// everything a root reaches can run. What is left with internal linkage is
// dead: it is diagnosed and left out of the output, which is what clang does
// at -O0 as well.
//
// The reason to ask the graph rather than "was this name ever mentioned",
// which is what gcc and clang ask, is the pair of static functions that only
// call each other: both names are mentioned and both are dead. Being a
// rooted walk, it reaches objects too, so a static table of function
// pointers that nothing uses keeps nothing alive.
static void check_unused_statics(void) {
    // globals is built by prepending, so reading it into an array and
    // walking that array backwards gives source order -- the order the
    // diagnostics have to come out in.
    uint32_t nsym = 0;
    for (Sym *sym = globals; sym; sym = sym->next) nsym++;
    Sym **syms = vnew(nsym + 1, sizeof(Sym *));
    uint32_t ns = 0;
    for (Sym *sym = globals; sym; sym = sym->next) syms[ns++] = sym;

    // Roots and the names parsing already rooted are both seeded, so a
    // symbol can be pushed twice; the walk below still marks each one once.
    Sym **work = vnew(2 * nsym + 1, sizeof(Sym *));
    uint32_t n = 0;

    for (uint32_t i = ns; i-- > 0;) {
        Sym *sym = syms[i];
        // A function that runs at startup or shutdown is called by the
        // platform rather than by the program: nothing references it, and a
        // static one must still be emitted.
        if (sym->is_function && (sym->ctor_prio || sym->dtor_prio)) {
            sym->is_reachable = true;
            work[n++] = sym;
            continue;
        }
        if (!is_user_global(sym) || (sym->sclass & (SC_STATIC | SC_CONSTEXPR))) continue;
        // A block-scope static is reached through the function that owns it,
        // not from outside, so it is not a root.
        if (sym->is_block_static) continue;
        // A function is a definition when it has a body, an object when it
        // is more than a declaration.
        if (sym->is_function ? !sym->body : (sym->sclass & SC_EXTERN)) continue;
        sym->is_reachable = true;
        work[n++] = sym;
    }

    // A name parsing already marked reachable -- one an emitted initializer
    // roots (see live_init), or one mentioned where there is no function and
    // no initializer to attribute it to -- carries references of its own.
    // Leaving it out of the worklist stopped the walk there: sqlite's
    // aSyscall is named by such functions, so the table was reported unused
    // and dropped while the uses stayed in the module.
    for (uint32_t i = 0; i < ns; i++)
        if (syms[i]->is_reachable) work[n++] = syms[i];

    while (n) {
        Sym *sym = work[--n];
        for (uint32_t i = 0; i < sym->num_refs; i++) {
            Sym *ref = sym->refs[i];
            if (ref->is_reachable) continue;
            ref->is_reachable = true;
            work[n++] = ref;
        }
    }

    for (uint32_t i = ns; i-- > 0;) {
        Sym *sym = syms[i];
        if (!is_user_global(sym) || !(sym->sclass & (SC_STATIC | SC_CONSTEXPR))) continue;
        if (sym->is_reachable) continue;

        if (sym->is_function) {
            sym->is_dead = true;
            // A static inline function is the header idiom for "here if you
            // want it": gcc stays quiet about an unused one and clang does
            // not. cxx follows gcc, whose answer survives the header case.
            if (sym->funcspec & Q_INLINE) continue;
            if (sym->is_unused || sym->is_maybe_unused) continue;
            warning(WG_UNUSED_FUNCTION, sym->tok, "unused function ‘%s’", str(sym->tok->id));
            continue;
        }

        // A declared object is promised by somewhere else; only a
        // definition can be an object this file fails to use.
        if (sym->sclass & SC_EXTERN) continue;
        if (!sym->is_block_static) sym->is_dead = true;
        if (sym->is_unused || sym->is_maybe_unused) continue;
        // str(tok->id) rather than str(sym->id): a block-scope static is
        // emitted as `f.q`, and gcc and clang name it `q`.
        warning(is_const_object(sym->ty) ? WG_UNUSED_CONST_VARIABLE : WG_UNUSED_VARIABLE, sym->tok,
                "unused variable ‘%s’", str(sym->tok->id));
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
    id_pretty = intern("__PRETTY_FUNCTION__", 19);

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

    while (tok->kind != TK_EOF) {
        if (consume_pragma(&tok, tok)) continue;
        tok = external_declaration(tok);
    }
    leave_scope(tok);

    // Every symbol now has its edges, so the reachable set can be computed
    // once, before the module is split into functions and objects.
    complete_tentative_arrays();
    check_unused_statics();
    check_inline_definitions();

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

    // A name may be carried by two symbols. A block-scope declaration of an
    // entity with external linkage and the file-scope definition of that
    // entity are one thing to 6.2.2p2 -- linkage ties them together, not the
    // visibility of the name, and the block's name is not visible here -- but
    // the definition's lookup does not find the declaration, so it makes a
    // second symbol. The printer keeps one entry per object-file name and the
    // first one wins, so a declaration that came first left the module with
    //     declare i32 @f(i32)   /   @x = external global i32
    // and no definition, and the link failed with "undefined reference".
    // Definitions go first; within each group the source order stands.
    for (int pass = 0; pass < 2; pass++) {
        Sym **head = pass ? &md->data : &md->fns;
        Sym *defined = NULL, **defined_tail = &defined;
        Sym *rest = NULL, **rest_tail = &rest;
        for (Sym *sym = *head; sym;) {
            Sym *next = sym->next;
            if (sym->is_defined && !sym->is_inline_def) {
                *defined_tail = sym;
                defined_tail = &sym->next;
            } else {
                *rest_tail = sym;
                rest_tail = &sym->next;
            }
            sym = next;
        }
        *defined_tail = rest;
        *rest_tail = NULL;
        *head = defined;
    }
    md->tys = reverse_list(Type, types, next);
    return md;
}
