#include "cxx.h"

static Type *declspecs(Token **rest, Token *tok, SClass *sclass);
static Type *decl_suffix(Token **rest, Token *tok, Type *ty);
static Type *declarator(Token **rest, Token *tok, Type *ty);
static Node *declaration(Token **rest, Token *tok, Type *ty, SClass sclass);
static Node *stmt(Token **rest, Token *tok);
static Node *compound_stmt(Token **rest, Token *tok);
static Node *expr(Token **rest, Token *tok);
static Node *assign(Token **rest, Token *tok);
static Node *cast(Token **rest, Token *tok);

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

static Node *new_var_node(Sym *var, Token *tok) {
    Node *node = new_node(ND_VAR, tok);
    node->var = var;
    return node;
}

static Node *new_binary(NodeKind kind, Node *lhs, Node *rhs, Token *tok) {
    Node *node = new_node(kind, tok);
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

Node *new_unary(NodeKind kind, Node *expr, Token *tok) {
    Node *node = new_node(kind, tok);
    node->lhs = expr;
    return node;
}

Node *new_imcast(Node *expr, Type *ty) {
    add_type(expr);
    Node *node = new_node(ND_IMCAST, expr->tok);
    node->lhs = expr;
    node->ty = ty;
    return node;
}

static Node *new_excast(Node *expr, Type *ty) {
    add_type(expr);
    Node *node = new_node(ND_EXCAST, expr->tok);
    node->lhs = expr;
    node->ty = ty;
    return node;
}

// Scope for local variables, global variables, typedefs
// or enum constants
typedef struct VarScope VarScope;
struct VarScope {
    VarScope *next;
    uint32_t id;
    Sym *var;
    Type *type_def;
    Type *enum_ty;
    int enum_val;
};

// Scope for struct, union or enum tags
typedef struct TagScope TagScope;
struct TagScope {
    TagScope *next;
    uint32_t id;
    Type *ty;
};

// Represents a block scope.
typedef struct Scope Scope;
struct Scope {
    Scope *next;

    // C has two block scopes; one is for variables/typedefs and
    // the other is for struct/union/enum tags.
    VarScope *vars;
    TagScope *tags;
};

// All local variable instances created during parsing are
// accumulated to this list.
static Sym *locals;
static Sym *globals;
static Type *types;

static Scope *scope = &(Scope){0};

// Points to the function object the parser is currently parsing.
static Sym *current_fn;

static void enter_scope(void) {
    Scope *sc = emalloc(sizeof(Scope));
    sc->next = scope;
    sc->vars = NULL;
    scope = sc;
}

static void leave_scope(void) { scope = scope->next; }

// Find a variable by name.
static VarScope *find_var(Token *tok, bool search_par) {
    Scope *sc = scope;
    while (sc) {
        for (VarScope *sc2 = sc->vars; sc2; sc2 = sc2->next)
            if (tok->id == sc2->id) return sc2;
        if (search_par)
            sc = sc->next;
        else
            return NULL;
    }
    return NULL;
}

static Type *find_tag(Token *tok, bool search_par) {
    Scope *sc = scope;
    while (sc) {
        for (TagScope *sc2 = sc->tags; sc2; sc2 = sc2->next)
            if (tok->id == sc2->id) return sc2->ty;
        if (search_par)
            sc = sc->next;
        else
            return NULL;
    }
    return NULL;
}

static VarScope *push_scope(uint32_t id) {
    VarScope *sc = emalloc(sizeof(VarScope));
    sc->id = id;
    sc->next = scope->vars;
    scope->vars = sc;
    return sc;
}

static void push_tag_scope(uint32_t id, Type *ty) {
    TagScope *sc = emalloc(sizeof(TagScope));
    sc->id = id;
    sc->ty = ty;
    sc->next = scope->tags;
    scope->tags = sc;
    ty->id = id;
}

static Sym *new_var(uint32_t id, Type *ty) {
    Sym *var = emalloc(sizeof(Sym));
    memset(var, 0, sizeof(Sym));
    var->id = id;
    var->ty = ty;
    push_scope(id)->var = var;
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

static uint32_t new_unique_strname(void) {
    static uint32_t id = 0;
    char buf[64];
    snprintf(buf, sizeof(buf), ".str.%u", id++);
    return intern(buf, strlen(buf));
}

static Sym *new_anon_gvar(Type *ty) { return new_gvar(new_unique_strname(), ty); }

static Sym *new_string_literal(uint32_t id) {
    Type *ty = array_of(ty_char, str_len(id) + 1);
    Sym *var = new_anon_gvar(ty);
    var->is_str = true;
    var->init_data = id;
    return var;
}

static Type *find_typedef(Token *tok, bool search_par) {
    if (tok->kind == TK_IDENT) {
        VarScope *sc = find_var(tok, search_par);
        if (sc) return sc->type_def;
    }
    return NULL;
}

static long get_number(Token *tok) {
    if (tok->kind != TK_NUM) error(tok->loc, "expected a number");
    return tok->val;
}

static uint32_t get_ident(Token *tok) {
    if (tok->kind != TK_IDENT) error(tok->loc, "expected identifier");
    return tok->id;
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
    if (is_integer(lhs->ty) && is_integer(rhs->ty)) return new_binary(ND_ADD, lhs, rhs, tok);

    if (lhs->ty->base && rhs->ty->base) error(tok->loc, "invalid operands to binary expression");

    // Canonicalize `num + ptr` to `ptr + num`.
    if (!lhs->ty->base && rhs->ty->base) swap(&lhs, &rhs);

    // ptr + num
    return new_binary(ND_PTRADD, lhs, rhs, tok);
}

static Node *new_sub(Node *lhs, Node *rhs, Token *tok) {
    add_type(lhs);
    add_type(rhs);

    // num - num
    if (is_integer(lhs->ty) && is_integer(rhs->ty)) return new_binary(ND_SUB, lhs, rhs, tok);

    // ptr - num
    if (lhs->ty->base && is_integer(rhs->ty)) return new_binary(ND_PTRADD, lhs, new_unary(ND_NEG, rhs, rhs->tok), tok);

    // ptr - ptr, which returns how many elements are between the two.
    if (lhs->ty->base && rhs->ty->base) {
        size_t size = lhs->ty->base->size;
        lhs = new_imcast(lhs, ty_long);
        rhs = new_imcast(rhs, ty_long);
        Node *node = new_binary(ND_SUB, lhs, rhs, tok);
        return new_binary(ND_DIV, node, new_long(size, tok), tok);
    }

    error(tok->loc, "invalid operands to binary expression");
    return NULL;
}

// Returns true if a given token represents a type.
static bool is_typename(Token *tok, bool search_par) {
    if (TK_INLINE <= tok->kind && tok->kind <= TK_ALIGNAS) return true;
    return find_typedef(tok, search_par);
}

// AbsDeclr ::= Ptr DirAbsDeclr? | DirAbsDeclr;
// DirAbsDeclr ::= ("(" AbsDeclr ")")? DeclrSuf*
static Type *abstract_declarator(Token **rest, Token *tok, Type *ty) {
    while (match(&tok, tok, TK_STAR)) ty = pointer_to(ty);

    if (tok->kind == TK_LPAREN) {
        Token *start = tok;
        Type dummy = {};
        abstract_declarator(&tok, start->next, &dummy);
        tok = skip(tok, TK_RPAREN);
        ty = decl_suffix(rest, tok, ty);
        return abstract_declarator(&tok, start->next, ty);
    }

    return decl_suffix(rest, tok, ty);
}

// TypeName ::= DeclSpecs AbsDeclr?
static Type *typename(Token **rest, Token *tok) {
    Type *ty = declspecs(&tok, tok, NULL);
    return abstract_declarator(rest, tok, ty);
}

// Init ::= AsExp
static Node *initializer(Token **rest, Token *tok) { return assign(rest, tok); }

static Node *fncall(Token **rest, Token *tok) {
    VarScope *sc = find_var(tok, 1);
    if (!sc) error(tok->loc, "implicit declaration of function ‘%.*s’", tok->len, tok->loc);
    if (!sc->var || sc->var->ty->kind != TY_FUNC)
        error(tok->loc, "called object ‘%.*s’ is not a function or function pointer", tok->len, tok->loc);

    Node *node = new_node(ND_FUNCALL, tok);
    node->func = tok->id;

    Type *ty = sc->var->ty;
    Type *param_ty = ty->params;
    node->func_ty = ty;
    node->ty = ty->ret;

    tok = tok->next->next;

    if (tok->kind == TK_RPAREN) {
        *rest = tok->next;
        return node;
    }

    Node dummy, *cur = &dummy;
    uint32_t i = 0;

    do {
        Node *arg = assign(&tok, tok);
        if (param_ty) {
            if (param_ty->kind == TY_STRUCT || param_ty->kind == TY_UNION)
                error(arg->tok->loc, "passing struct or union is not supported yet");
            arg = new_imcast(arg, param_ty);
            param_ty = param_ty->next;
        }
        ++i;
        cur = cur->next = arg;
    } while (match(&tok, tok, TK_COMMA));

    *rest = skip(tok, TK_RPAREN);

    node->args = dummy.next;
    node->narg = i;
    return node;
}

// PrimExp ::= Num | Str | Ident | "(" Exp ")" | "(" CompStmt ")"
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
    } else if (tok->kind == TK_NUM) {
        node = new_num(tok->val, tok);
    } else if (tok->kind == TK_STRLIT) {
        Sym *var = new_string_literal(tok->id);
        *rest = tok->next;
        return new_var_node(var, tok);
    } else if (tok->kind == TK_IDENT) {
        // Function call
        if (tok->next->kind == TK_LPAREN) return fncall(rest, tok);
        // Variable or enum constant
        VarScope *sc = find_var(tok, 1);
        if (!sc || (!sc->var && !sc->enum_ty))
            error(tok->loc, "use of undeclared identifier ‘%.*s’", tok->len, tok->loc);
        if (sc->var)
            node = new_var_node(sc->var, tok);
        else
            node = new_num(sc->enum_val, tok);
    } else {
        error(tok->loc, "expected expression");
        node = NULL;
    }
    *rest = tok->next;
    return node;
}

static Member *get_struct_member(Type *ty, Token *tok) {
    for (Member *mem = ty->members; mem; mem = mem->next)
        if (mem->name->id == tok->id) return mem;
    error(tok->loc, "no member named ‘%.*s’ in ‘%s’", tok->len, tok->loc, str(ty->uid));
    return NULL;
}

static Member *copy_mem(Member *mem) {
    Member *new = emalloc(sizeof(Member));
    *new = *mem;
    return new;
}

// PostExp  ::= PrimExp PostFix*
// PostFix  ::= "(" ArgList? ")" | "[" Exp "]" | "." Ident | "++" | "--"
// ArgList  ::= AsExp ("," AsExp)*
static Node *postfix(Token **rest, Token *tok) {
    Node *node = primary(&tok, tok);
    while (1) {
        add_type(node);
        if (node->ty->kind == TY_ARRAY) node = new_imcast(node, pointer_to(node->ty->base));
        switch (tok->kind) {
                // x[y] is short for *(x+y)
            case TK_LBRACKET: {
                Token *start = tok;
                Node *idx = expr(&tok, tok->next);
                tok = skip(tok, TK_RBRACKET);
                node = new_unary(ND_DEREF, new_add(node, idx, start), start);
                continue;
            }
            case TK_ARROW:
                // x->y is short for (*x).y
                node = new_unary(ND_DEREF, node, tok);
                add_type(node);
                // fall through
            case TK_DOT: {
                Type *ty = node->ty;
                Token *dot = tok;
                tok = tok->next;
                if (ty->kind != TY_STRUCT && ty->kind != TY_UNION) {
                    if (tok->kind == TK_IDENT)
                        error(dot->loc, "request for member ‘%.*s’ in something not a structure or union", tok->len,
                              tok->loc);
                    else
                        error(dot->loc, "expected ‘;’ after expression");
                }
                Member *mem = copy_mem(get_struct_member(ty, tok));
                tok = tok->next;
                mem->next = NULL;
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
            if (node->ty->kind == TY_ARRAY) node = new_imcast(node, pointer_to(node->ty->base));
            return node;
        }
        case TK_INC:
            return new_unary(ND_PREINC, unary(rest, tok->next), tok);
        case TK_DEC:
            return new_unary(ND_PREDEC, unary(rest, tok->next), tok);
        case TK_SIZEOF: {
            if (tok->next->kind == TK_LPAREN && is_typename(tok->next->next, 1)) {
                Token *start = tok;
                Type *ty = typename(&tok, tok->next->next);
                *rest = skip(tok, TK_RPAREN);
                return new_num(ty->size, start);
            }
            Node *node = unary(rest, tok->next);
            add_type(node);
            if (node->kind == ND_IMCAST && node->lhs->ty->kind == TY_ARRAY) node = node->lhs;
            return new_long(node->ty->size, tok);
        }
        default:
            break;
    }
    return postfix(rest, tok);
}

// CastExp ::= UnaryExp | "(" TypeName ")" CastExp
static Node *cast(Token **rest, Token *tok) {
    if (tok->kind == TK_LPAREN && is_typename(tok->next, 1)) {
        Token *start = tok;
        Type *ty = typename(&tok, tok->next);
        tok = skip(tok, TK_RPAREN);
        Node *node = new_excast(cast(rest, tok), ty);
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
static Node *binexpr(Token **rest, Token *tok, int min_prec) {
    static int op_table[][2] = {
        [TK_BOR] = {40, ND_BOR},    [TK_XOR] = {50, ND_XOR},   [TK_BAND] = {60, ND_BAND},   [TK_EQ] = {70, ND_EQ},
        [TK_NE] = {70, ND_NE},      [TK_LT] = {80, ND_LT},     [TK_GT] = {80, ND_LT},       [TK_LE] = {80, ND_LE},
        [TK_GE] = {80, ND_LE},      [TK_LEFT] = {90, ND_LEFT}, [TK_RIGHT] = {90, ND_RIGHT}, [TK_PLUS] = {100, ND_ADD},
        [TK_MINUS] = {100, ND_SUB}, [TK_STAR] = {110, ND_MUL}, [TK_SLASH] = {110, ND_DIV},  [TK_MOD] = {110, ND_MOD},
    };

    Node *lhs = cast(&tok, tok);
    add_type(lhs);

    while (TK_BOR <= tok->kind && tok->kind <= TK_MOD) {
        Token *op_tok = tok;
        NodeKind expr_op = op_table[op_tok->kind][1];
        int cur_prec = op_table[op_tok->kind][0];
        if (cur_prec <= min_prec) break;

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

// AsOP  ::= "=" | "*=" | "/=" | "%=" | "+=" | "-="
//         | "<<=" | ">>=" | "&=" | "^=" | "|="
static inline bool is_assignop(Token *tok) { return TK_AS <= tok->kind && tok->kind <= TK_RIGHTAS; }

// AsExp ::= BOrExp (AsOP AsExp)?
static Node *assign(Token **rest, Token *tok) {
    Node *node = binexpr(&tok, tok, 0);
    static int as_op[] = {
        [TK_AS] = ND_AS,       [TK_ADDAS] = ND_ADDAS,   [TK_SUBAS] = ND_SUBAS,     [TK_MULAS] = ND_MULAS,
        [TK_DIVAS] = ND_DIVAS, [TK_MODAS] = ND_MODAS,   [TK_ANDAS] = ND_ANDAS,     [TK_ORAS] = ND_ORAS,
        [TK_XORAS] = ND_XORAS, [TK_LEFTAS] = ND_LEFTAS, [TK_RIGHTAS] = ND_RIGHTAS,
    };
    while (is_assignop(tok)) node = new_binary(as_op[tok->kind], node, assign(&tok, tok->next), tok);
    *rest = tok;
    add_type(node);
    return node;
}

// Exp ::= AsExp ("," AsExp)*
static Node *expr(Token **rest, Token *tok) {
    Node *node = assign(&tok, tok);
    while (tok->kind == TK_COMMA) node = new_binary(ND_COMMA, node, assign(&tok, tok->next), tok);
    *rest = tok;
    add_type(node);
    return node;
}

// ExpStmt ::= ";" | Exp ";"
static Node *expr_stmt(Token **rest, Token *tok) {
    if (tok->kind == TK_SEMI) {
        *rest = tok->next;
        return new_node(ND_EXPR_STMT, tok);
    }

    Node *node = new_node(ND_EXPR_STMT, tok);
    node->lhs = expr(&tok, tok);

    *rest = skip(tok, TK_SEMI);
    return node;
}

// RetStmt ::= "return" Exp? ";"
static Node *return_stmt(Token **rest, Token *tok) {
    Node *node = new_node(ND_RETURN, tok);
    if (tok->next->kind == TK_SEMI) {
        *rest = tok->next->next;
        return node;
    }

    Node *exp = expr(&tok, tok->next);
    *rest = skip(tok, TK_SEMI);

    add_type(exp);
    node->lhs = new_imcast(exp, current_fn->ty->ret);

    return node;
}

// InitDecls ::= InitDeclr ("," InitDeclr)*
// InitDeclr ::= Declr ("=" Init)?
static Node *init_decl_list(Token **rest, Token *tok, Type *basety, SClass sclass) {
    Node dummy, *cur = &dummy;
    do {
        Token *start = tok;
        Type *ty = declarator(&tok, tok, basety);
        if (ty->kind == TY_VOID) error(start->loc, "variable ‘%.*s’ declared void", start->len, start->loc);
        Sym *var = new_lvar(get_ident(ty->name), ty);
        var->sclass = sclass;
        if (tok->kind == TK_AS) {
            Node *lhs = new_var_node(var, ty->name);
            Node *rhs = initializer(&tok, tok->next);
            Node *as = new_binary(ND_AS, lhs, rhs, tok);
            add_type(as);
            cur = cur->next = new_unary(ND_EXPR_STMT, as, tok);
        }
    } while (match(&tok, tok, TK_COMMA));

    *rest = tok;
    cur->next = NULL;
    return dummy.next;
}

// SelHead ::= Exp | Decl Exp | SimDecl
// SimDecl ::= DeclSpecs Declr "=" Init
static Node *select_head(Token **rest, Token *tok) {
    Node *node;
    if (is_typename(tok, 1)) {
        Type *basety = declspecs(&tok, tok, NULL);
        node = new_node(ND_DECL, tok);
        if (tok->kind != TK_SEMI) node->body = init_decl_list(&tok, tok, basety, 0);
        if (tok->kind == TK_SEMI) {
            Node *stmt = node->body;
            while (stmt->next) stmt = stmt->next;
            stmt->next = expr(&tok, tok->next);
        }
    } else {
        node = expr(&tok, tok);
    }
    *rest = tok;
    return node;
}

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

// ForStmt ::= "for" "(" (Decl | Exp? ";") Exp? ";" Exp? ")" Stmt
static Node *for_stmt(Token **rest, Token *tok) {
    enter_scope();
    Node *node = new_node(ND_FOR, tok);
    tok = skip(tok->next, TK_LPAREN);

    // Init
    if (is_typename(tok, 1)) {
        Type *basety = declspecs(&tok, tok, NULL);
        node->init = declaration(&tok, tok, basety, 0);
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
    leave_scope();
    return node;
}

// WhileStmt ::= "while" "(" Exp ")" Stmt
static Node *while_stmt(Token **rest, Token *tok) {
    enter_scope();
    Node *node = new_node(ND_WHILE, tok);
    tok = skip(tok->next, TK_LPAREN);
    // Cond
    node->cond = expr(&tok, tok);
    tok = skip(tok, TK_RPAREN);
    // Body
    node->then = stmt(rest, tok);
    leave_scope();
    return node;
}

// DoStmt ::= "do" Stmt "while" "(" Exp ")" ";"
static Node *do_stmt(Token **rest, Token *tok) {
    enter_scope();
    Node *node = new_node(ND_DO, tok);
    // Body
    node->body = stmt(&tok, tok->next);
    // Cond
    tok = skip(tok, TK_WHILE);
    tok = skip(tok, TK_LPAREN);
    node->cond = expr(&tok, tok);
    tok = skip(tok, TK_RPAREN);
    *rest = skip(tok, TK_SEMI);
    leave_scope();
    return node;
}

// Stmt ::= ExpStmt | CompStmt | RetStmt | IfStmt | WhileStmt | DoStmt | ForStmt
static Node *stmt(Token **rest, Token *tok) {
    switch (tok->kind) {
        case TK_LBRACE:
            return compound_stmt(rest, tok);
        case TK_RETURN:
            return return_stmt(rest, tok);
        case TK_IF:
            return if_stmt(rest, tok);
        case TK_FOR:
            return for_stmt(rest, tok);
        case TK_WHILE:
            return while_stmt(rest, tok);
        case TK_DO:
            return do_stmt(rest, tok);
        default:
            return expr_stmt(rest, tok);
    }
}

// CompStmt  ::= "{" BlockItem* "}"
// BlockItem ::= Stmt | Decl
static Node *compound_stmt(Token **rest, Token *tok) {
    enter_scope();
    Node *node = new_node(ND_COMP_STMT, tok);
    Node dummy, *cur = &dummy;

    tok = tok->next;
    while (tok->kind != TK_RBRACE) {
        if (is_typename(tok, 1)) {
            SClass sclass = 0;
            Type *basety = declspecs(&tok, tok, &sclass);
            if (sclass == SC_TYPEDEF) {
                Type *ty = declarator(&tok, tok, basety);
                push_scope(get_ident(ty->name))->type_def = ty;
            } else {
                cur = cur->next = declaration(&tok, tok, basety, sclass);
            }
        } else {
            cur = cur->next = stmt(&tok, tok);
        }
        add_type(cur);
    }
    cur->next = NULL;
    *rest = skip(tok, TK_RBRACE);

    node->body = dummy.next;
    leave_scope();
    return node;
}

// EnumSpec ::= "enum" Ident? "{" Enumr ("," Enumr)* ","? "}"
//            | "enum" Ident
// Enumr    ::= Ident ("=" Num)?
static Type *enum_decl(Token **rest, Token *tok) {
    Type *ty = enum_type();
    tok = tok->next;
    // Read a enum tag.
    Token *tag = NULL;
    if (tok->kind == TK_IDENT) {
        tag = tok;
        tok = tok->next;
    }

    if (tag && tok->kind != TK_LBRACE) {
        Type *ty = find_tag(tag, 1);
        if (!ty) error(tag->loc, "unknown enum type");
        if (ty->kind != TY_ENUM) error(tag->loc, "not an enum tag");
        *rest = tok;
        return ty;
    }

    tok = skip(tok, TK_LBRACE);

    // Read an enum-list.
    int i = 0;
    int val = 0;
    while (tok->kind != TK_RBRACE) {
        if (i++ > 0) tok = skip(tok, TK_COMMA);

        uint32_t name = get_ident(tok);
        tok = tok->next;

        if (tok->kind == TK_AS) {
            val = get_number(tok->next);
            tok = tok->next->next;
        }

        VarScope *sc = push_scope(name);
        sc->enum_ty = ty;
        sc->enum_val = val++;
    }

    *rest = tok->next;

    if (tag) push_tag_scope(tag->id, ty);

    return ty;
}

// MemDecl  ::= TypeSpec+ (MemDeclr ("," MemDeclr)*)? ";"
// MemDeclr ::= Declr
static void struct_members(Token **rest, Token *tok, Type *ty) {
    Member head = {};
    Member *cur = &head;

    while (tok->kind != TK_RBRACE) {
        Type *basety = declspecs(&tok, tok, NULL);
        int i = 0;

        while (!match(&tok, tok, TK_SEMI)) {
            if (i++) tok = skip(tok, TK_COMMA);
            Token *start = tok;
            Member *mem = emalloc(sizeof(Member));
            mem->ty = declarator(&tok, tok, basety);
            if (mem->ty->kind == TY_VOID) error(start->loc, "field ‘%.*s’ declared void", start->len, start->loc);
            mem->name = mem->ty->name;
            cur = cur->next = mem;
        }
    }

    *rest = tok->next;
    ty->members = head.next;
}

// RecordSpec ::= Record Ident ("{" MemDecl+ "}")? | Record "{" MemDecl+ "}"
static Type *record_decl(Token **rest, Token *tok) {
    bool is_union = tok->kind == TK_UNION;
    tok = tok->next;
    // Read a tag.
    Token *tag = NULL;
    if (tok->kind == TK_IDENT) {
        tag = tok;
        tok = tok->next;
    }

    if (tag && tok->kind != TK_LBRACE) {
        Type *ty = find_tag(tag, 1);
        assert(ty);
        *rest = tok;
        return ty;
    }

    // Construct a struct object.
    Type *ty = emalloc(sizeof(Type));
    ty->kind = is_union ? TY_UNION : TY_STRUCT;
    struct_members(rest, tok->next, ty);
    ty->align = 1;

    // Assign offsets within the struct to members.
    int offset = 0;
    uint32_t idx = 0;
    for (Member *mem = ty->members; mem; mem = mem->next) {
        ty->align = MAX(ty->align, mem->ty->align);
        mem->idx = idx++;
        if (is_union) {
            offset = MAX(offset, mem->ty->size);
            continue;
        }
        offset = ALIGN_UP(offset, mem->ty->align);
        mem->offset = offset;
        offset += mem->ty->size;
    }
    ty->size = ALIGN_UP(offset, ty->align);
    // Register the struct type if a name was given.
    if (tag)
        push_tag_scope(tag->id, ty);
    else
        ty->id = intern("anon", 4);

    int i = -1;
    Type *t = types;
    while (t) {
        if (t->id == ty->id) i++;
        t = t->next;
    }
    char *name;
    char *kind = is_union ? "union" : "struct";
    if (i >= 0) {
        name = format("%s.%s.%d", kind, str(ty->id), i);
    } else {
        name = format("%s.%s", kind, str(ty->id));
    }
    ty->uid = intern(name, strlen(name));
    ty->next = types;
    types = ty;
    return ty;
}

// DeclSpecs ::= DeclSpec+
// DeclSpec  ::= SCSpec | TypeSpec
// SCSpec    ::= "typedef" | "static"
// TypeSpec  ::= "void" | "_Bool" | "char" | "short" | "int" | "long"
//            | RecordSpec
//            | EnumSpec
//            | TypedefName
static Type *declspecs(Token **rest, Token *tok, SClass *sclass) {
    Type *ty = ty_int;
    int typespec_cnt = 0;
    enum {
        NONE,
        VOID = 1 << 0,
        BOOL = 1 << 2,
        CHAR = 1 << 4,
        SHORT = 1 << 6,
        INT = 1 << 8,
        LONG = 1 << 10,
        OTHER = 1 << 12,
    };

    static const SClass sc_table[] = {
        [TK_EXTERN] = SC_EXTERN, [TK_REGISTER] = SC_REG,    [TK_STATIC] = SC_STATIC,
        [TK_THREAD] = SC_THREAD, [TK_TYPEDEF] = SC_TYPEDEF,
    };

    while (is_typename(tok, 1)) {
        Token *ty_tok = tok;
        switch (tok->kind) {
            case TK_TYPEDEF:
            case TK_STATIC:
            case TK_EXTERN:
            case TK_THREAD:
            case TK_REGISTER:
            case TK_CONSTEXPR: {
                SClass sc = sc_table[tok->kind];
                if (!sclass) error(tok->loc, "storage class specifier is not allowed in this context");
                if (*sclass) {
                    if (*sclass == sc)
                        error(tok->loc, "duplicate ‘%.*s’", tok->len, tok->loc);
                    else
                        error(tok->loc, "multiple storage classes in declaration specifiers");
                };
                *sclass = sc;
                break;
            }
            case TK_IDENT: {
                Type *orig = find_typedef(tok, *sclass != SC_TYPEDEF);
                if (orig) {
                    if (typespec_cnt)
                        error(tok->loc, "‘%.*s’ redeclared as different kind of symbol", tok->len, tok->loc);
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
            case SHORT:
            case SHORT + INT:
                ty = ty_short;
                break;
            case INT:
                ty = ty_int;
                break;
            case LONG:
            case LONG + INT:
                ty = ty_long;
                break;
            case LONG + LONG:
            case LONG + LONG + INT:
                ty = ty_llong;
                break;
            case NONE:
            case OTHER:
                break;
            default:
                error(ty_tok->loc,
                      "cannot combine with previous"
                      " declaration specifier");
        }
    }
loop_end:
    if (!typespec_cnt) error(tok->loc, "a type specifier is required for all declarations");
    *rest = tok;
    return ty;
}

// DeclrSuf  ::= "(" ParamList? ")" | "[" Num "]"
// ParamList ::= ParamDecl ("," ParamDecl)*
// ParamDecl ::= DeclSpecs Declr
static Type *decl_suffix(Token **rest, Token *tok, Type *ty) {
    if (tok->kind == TK_LPAREN) {
        tok = tok->next;
        Type dummy, *cur = &dummy;

        while (tok->kind != TK_RPAREN) {
            if (cur != &dummy) tok = skip(tok, TK_COMMA);
            Token *start = tok;
            Type *basety = declspecs(&tok, tok, NULL);
            Type *paramty = declarator(&tok, tok, basety);
            if (paramty->kind == TY_VOID)
                error(start->loc, "argument may not have ‘void’ type", start->len, start->loc);
            cur = cur->next = copy_type(paramty);
        }
        *rest = tok->next;
        cur->next = NULL;

        ty = func_type(ty);
        ty->params = dummy.next;
        return ty;
    }
    if (tok->kind == TK_LBRACKET) {
        int sz = get_number(tok->next);
        tok = skip(tok->next->next, TK_RBRACKET);
        ty = decl_suffix(rest, tok, ty);
        return array_of(ty, sz);
    }

    *rest = tok;
    return ty;
}

// Declr    ::= Ptr? DirDeclr
// Ptr      ::= "*"+
// DirDeclr ::= (Ident | "(" Declr ")") DeclrSuf*
static Type *declarator(Token **rest, Token *tok, Type *ty) {
    while (match(&tok, tok, TK_STAR)) ty = pointer_to(ty);

    if (tok->kind == TK_LPAREN) {
        Token *start = tok;
        Type dummy = {};
        declarator(&tok, start->next, &dummy);
        tok = skip(tok, TK_RPAREN);
        ty = decl_suffix(rest, tok, ty);
        return declarator(&tok, start->next, ty);
    }

    if (tok->kind != TK_IDENT) error(tok->loc, "expected identifier or ‘(’");
    ty = decl_suffix(rest, tok->next, ty);
    ty->name = tok;
    return ty;
}

// Decl ::= DeclSpecs InitDecls? ";"
static Node *declaration(Token **rest, Token *tok, Type *basety, SClass sclass) {
    Node *node = new_node(ND_DECL, tok);
    if (tok->kind == TK_SEMI) {
        *rest = tok->next;
        return node;
    }
    node->body = init_decl_list(&tok, tok, basety, sclass);
    *rest = skip(tok, TK_SEMI);
    return node;
}

static void create_param_lvars(Type *param) {
    if (param) {
        create_param_lvars(param->next);
        new_lvar(get_ident(param->name), param);
    }
}

// ExDecl    ::= FuncDef | Decl
// FuncDef   ::= DeclSpecs Declr CompStmt
static Token *external_declaration(Token *tok) {
    while (match(&tok, tok, TK_SEMI));
    if (tok->kind == TK_EOF) return tok;

    SClass sclass = 0;
    Type *basety = declspecs(&tok, tok, &sclass);
    if (tok->kind == TK_SEMI) return tok->next;

    int cnt = -1;
    while (1) {
        cnt++;
        Node *init = NULL;
        Token *start = tok;
        Type *ty = declarator(&tok, tok, basety);

        // function-definition
        if (tok->kind == TK_LBRACE) {
            if (cnt || ty->kind != TY_FUNC || sclass == SC_TYPEDEF)
                error(tok->loc, "expected ‘;’ after top level declarator");
            Sym *fn = new_gvar(get_ident(ty->name), ty);
            fn->is_function = true;
            fn->is_definition = true;
            fn->sclass = sclass;

            locals = NULL;
            current_fn = fn;
            enter_scope();
            create_param_lvars(ty->params);
            fn->params = locals;
            uint32_t i = 0;
            Sym *cur = locals;
            while (cur) {
                ++i;
                cur = cur->next;
            }
            fn->params = locals;
            fn->nparam = i;

            fn->body = compound_stmt(&tok, tok);
            fn->locals = locals;
            leave_scope();
            return tok;
        }

        // declaration
        if (tok->kind == TK_AS) {
            if (ty->kind == TY_FUNC || sclass == SC_TYPEDEF)
                error(ty->name->loc,
                      "illegal initializer (only variables can be "
                      "initialized)");
            init = initializer(&tok, tok);
        }
        if (sclass == SC_TYPEDEF) {
            push_scope(get_ident(ty->name))->type_def = ty;
        } else {
            if (ty->kind == TY_VOID) error(start->loc, "variable ‘%.*s’ declared void", start->len, start->loc);
            Sym *var = new_gvar(get_ident(ty->name), ty);
            var->is_function = var->ty->kind == TY_FUNC;
            var->sclass = sclass;
            var->init = init;
        }
        if (match(&tok, tok, TK_COMMA))
            continue;
        else if (tok->kind == TK_SEMI)
            return tok->next;
        else
            error(tok->loc, "expected ‘;’ after top level declarator");
    }
}

// TransUnit ::= ExDecl+
Module *parse(Token *tok) {
    Module *md = emalloc(sizeof(Module));
    memset(md, 0, sizeof(Module));

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
    md->tys = types;
    return md;
}
