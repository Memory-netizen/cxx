#include "cxx.h"

static Type *declarator(Token **rest, Token *tok, Type *ty);
static Node *declaration(Token **rest, Token *tok);
static Node *stmt(Token **rest, Token *tok);
static Node *compound_stmt(Token **rest, Token *tok);
static Node *expr(Token **rest, Token *tok);
static Node *assign(Token **rest, Token *tok);
static Node *unary(Token **rest, Token *tok);
static Node *binexpr(Token **rest, Token *tok, int min_prec);

static Node *new_node(NodeKind kind, Token *tok) {
    Node *node = emalloc(sizeof(Node));
    memset(node, 0, sizeof(*node));
    node->kind = kind;
    node->tok = tok;
    return node;
}

static Node *new_num(int val, Token *tok) {
    Node *node = new_node(ND_NUM, tok);
    node->val = val;
    return node;
}

static Node *new_var_node(Obj *var, Token *tok) {
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

static Node *new_unary(NodeKind kind, Node *expr, Token *tok) {
    Node *node = new_node(kind, tok);
    node->lhs = expr;
    return node;
}

// Scope for local or global variables.
typedef struct VarScope VarScope;
struct VarScope {
    VarScope *next;
    uint32_t id;
    Obj *var;
};

// Represents a block scope.
typedef struct Scope Scope;
struct Scope {
    Scope *next;
    VarScope *vars;
};

// All local variable instances created during parsing are
// accumulated to this list.
static Obj *locals;
static Obj *globals;

static Scope *scope = &(Scope){0};

static void enter_scope(void) {
    Scope *sc = emalloc(sizeof(Scope));
    sc->next = scope;
    scope = sc;
}

static void leave_scope(void) { scope = scope->next; }

// Find a variable by name.
static Obj *find_var(Token *tok) {
    for (Scope *sc = scope; sc; sc = sc->next)
        for (VarScope *sc2 = sc->vars; sc2; sc2 = sc2->next)
            if (tok->id == sc2->id) return sc2->var;
    return NULL;
}

static VarScope *push_scope(uint32_t id, Obj *var) {
    VarScope *sc = emalloc(sizeof(VarScope));
    sc->id = id;
    sc->var = var;
    sc->next = scope->vars;
    scope->vars = sc;
    return sc;
}

static Obj *new_var(uint32_t id, Type *ty) {
    Obj *var = emalloc(sizeof(Obj));
    var->id = id;
    var->ty = ty;
    push_scope(id, var);
    return var;
}

static Obj *new_lvar(uint32_t id, Type *ty) {
    Obj *var = new_var(id, ty);
    var->is_local = true;
    var->next = locals;
    locals = var;
    return var;
}

static Obj *new_gvar(uint32_t id, Type *ty) {
    Obj *var = new_var(id, ty);
    var->next = globals;
    globals = var;
    return var;
}

static uint32_t new_unique_name(void) {
    static uint32_t id = 0;
    char buf[64];
    snprintf(buf, sizeof(buf), ".str.%u", id++);
    return intern(buf, strlen(buf));
}

static Obj *new_anon_gvar(Type *ty) { return new_gvar(new_unique_name(), ty); }
static Obj *new_string_literal(uint32_t id) {
    Type *ty = array_of(ty_char, str_len(id) + 1);
    Obj *var = new_anon_gvar(ty);
    var->is_str = true;
    var->init_data = id;
    return var;
}

static int get_number(Token *tok) {
    assert(tok->kind == TK_NUM);
    return tok->val;
}

static uint32_t get_ident(Token *tok) {
    assert(tok->kind == TK_IDENT);
    return tok->id;
}

static Node *new_add(Node *lhs, Node *rhs, Token *tok) {
    add_type(lhs);
    add_type(rhs);

    // num + num
    if (is_integer(lhs->ty) && is_integer(rhs->ty)) return new_binary(ND_ADD, lhs, rhs, tok);

    if (lhs->ty->base && rhs->ty->base) exit(1);

    // Canonicalize `num + ptr` to `ptr + num`.
    if (!lhs->ty->base && rhs->ty->base) {
        Node *tmp = lhs;
        lhs = rhs;
        rhs = tmp;
    }

    // ptr + num
    return new_binary(ND_PTRADD, lhs, rhs, tok);
}

static Node *new_sub(Node *lhs, Node *rhs, Token *tok) {
    add_type(lhs);
    add_type(rhs);

    // num - num
    if (is_integer(lhs->ty) && is_integer(rhs->ty)) return new_binary(ND_SUB, lhs, rhs, tok);

    // ptr - num
    if (lhs->ty->base && is_integer(rhs->ty)) {
        Node *node = new_binary(ND_PTRSUB, lhs, rhs, tok);
        node->ty = lhs->ty;
        return node;
    }

    // ptr - ptr, which returns how many elements are between the two.
    if (lhs->ty->base && rhs->ty->base) {
        Node *node = new_binary(ND_SUB, lhs, rhs, tok);
        node->ty = ty_int;
        return new_binary(ND_DIV, node, new_num(lhs->ty->base->size, tok), tok);
    }
    return NULL;
}

static Node *fncall(Token **rest, Token *tok) {
    Node *node = new_node(ND_FUNCALL, tok);
    node->func = tok->id;
    tok = tok->next->next;

    if (tok->kind == TK_RPAREN) {
        *rest = tok->next;
        return node;
    }

    Node dummy, *cur = &dummy;
    cur = cur->next = assign(&tok, tok);
    uint32_t i = 1;
    for (; tok->kind == TK_COMMA; ++i) cur = cur->next = assign(&tok, tok->next);

    assert(tok->kind == TK_RPAREN);
    *rest = tok->next;

    node->args = dummy.next;
    node->narg = i;
    return node;
}

// PrimExp ::= Num | Str | Ident | "(" Exp ")" | "(" CompStmt ")"
static Node *primary(Token **rest, Token *tok) {
    Node *node;
    if (tok->kind == TK_LPAREN && tok->next->kind == TK_LBRACE) {
        // This is a GNU statement expresssion.
        Node *node = new_node(ND_STMT_EXPR, tok);
        node->body = compound_stmt(&tok, tok->next)->body;
        *rest = tok->next;
        return node;
    }

    if (tok->kind == TK_LPAREN) {
        node = expr(&tok, tok->next);
        assert(tok->kind == TK_RPAREN);
    } else if (tok->kind == TK_NUM) {
        node = new_num(tok->val, tok);
    } else if (tok->kind == TK_STRLIT) {
        Obj *var = new_string_literal(tok->id);
        *rest = tok->next;
        return new_var_node(var, tok);
    } else if (tok->kind == TK_IDENT) {
        if (tok->next->kind == TK_LPAREN) return fncall(rest, tok);
        Obj *var = find_var(tok);
        if (!var) exit(1);
        node = new_var_node(var, tok);
    } else {
        exit(1);
    }
    *rest = tok->next;
    return node;
}

// PostExp  ::= PrimExp PostFix*
// PostFix  ::= "(" ArgList? ")" | "[" Exp "]"
// ArgList  ::= AsExp ("," AsExp)*
static Node *postfix(Token **rest, Token *tok) {
    Node *node = primary(&tok, tok);
    while (1) {
        switch (tok->kind) {
            case TK_LBRACKET:
                // x[y] is short for *(x+y)
                Token *start = tok;
                Node *idx = expr(&tok, tok->next);
                assert(tok->kind == TK_RBRACKET);
                tok = tok->next;
                node = new_unary(ND_DEREF, new_add(node, idx, start), start);
                continue;
            default:
                break;
        }
        break;
    }
    *rest = tok;
    return node;
}

// UnaryExp ::= PostExp | UnaryOp UnaryExp
// UnaryOp  ::= "+" | "-" | "~" | "!" | "&" | "*" | "sizeof"
static Node *unary(Token **rest, Token *tok) {
    switch (tok->kind) {
        case TK_PLUS:
            return new_unary(ND_PLUS, unary(rest, tok->next), tok);
        case TK_MINUS:
            return new_unary(ND_NEG, unary(rest, tok->next), tok);
        case TK_INVERT:
            return new_unary(ND_INVERT, unary(rest, tok->next), tok);
        case TK_NOT:
            return new_unary(ND_NOT, unary(rest, tok->next), tok);
        case TK_BAND:
            return new_unary(ND_ADDR, unary(rest, tok->next), tok);
        case TK_STAR:
            return new_unary(ND_DEREF, unary(rest, tok->next), tok);
        case TK_SIZEOF: {
            Node *node = unary(rest, tok->next);
            add_type(node);
            return new_num(node->ty->size, tok);
        }
        default:
            break;
    }
    return postfix(rest, tok);
}

// MulExp   ::= UnaryExp (("*" | "/" | "%") UnaryExp)*
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
        [TK_NE] = {70, ND_NE},      [TK_LT] = {80, ND_LT},     [TK_GT] = {80, ND_GT},       [TK_LE] = {80, ND_LE},
        [TK_GE] = {80, ND_GE},      [TK_LEFT] = {90, ND_LEFT}, [TK_RIGHT] = {90, ND_RIGHT}, [TK_PLUS] = {100, ND_ADD},
        [TK_MINUS] = {100, ND_SUB}, [TK_STAR] = {110, ND_MUL}, [TK_SLASH] = {110, ND_DIV},  [TK_MOD] = {110, ND_MOD},
    };

    Node *lhs = unary(&tok, tok);
    add_type(lhs);

    while (TK_BOR <= tok->kind && tok->kind <= TK_MOD) {
        Token *op_tok = tok;
        NodeKind expr_op = op_table[op_tok->kind][1];
        int cur_prec = op_table[op_tok->kind][0];
        if (cur_prec <= min_prec) break;

        Node *rhs = binexpr(&tok, tok->next, cur_prec);
        add_type(rhs);

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

// AsExp ::= BOrExp ("=" AsExp)?
static Node *assign(Token **rest, Token *tok) {
    Node *node = binexpr(&tok, tok, 0);
    while (tok->kind == TK_AS) node = new_binary(ND_AS, node, assign(&tok, tok->next), tok);
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

// ExpStmt ::= ";" | Exp ";";
static Node *expr_stmt(Token **rest, Token *tok) {
    if (tok->kind == TK_SEMI) {
        *rest = tok->next;
        return new_node(ND_EXPR_STMT, tok);
    }

    Node *node = new_node(ND_EXPR_STMT, tok);
    node->lhs = expr(&tok, tok);

    assert(tok->kind == TK_SEMI);
    *rest = tok->next;
    return node;
}

// RetStmt ::= "return" Exp? ";";
static Node *return_stmt(Token **rest, Token *tok) {
    Node *node = new_node(ND_RETURN, tok);
    if (tok->next->kind == TK_SEMI) {
        *rest = tok->next->next;
        return node;
    }

    node->lhs = expr(&tok, tok->next);
    assert(tok->kind == TK_SEMI);
    *rest = tok->next;

    return node;
}

// IfStmt ::= "if" "(" SelHead ")" Stmt ("else" Stmt)?
// SelHead ::= Exp | Decl Exp | SimDecl
static Node *if_stmt(Token **rest, Token *tok) {
    enter_scope();
    Node *node = new_node(ND_IF, tok);
    assert(tok->next->kind == TK_LPAREN);
    // Cond
    node->cond = expr(&tok, tok->next->next);
    assert(tok->kind == TK_RPAREN);
    // Then
    node->then = stmt(&tok, tok->next);
    // Else
    if (tok->kind == TK_ELSE) node->els = stmt(&tok, tok->next);
    *rest = tok;
    leave_scope();
    return node;
}

// ForStmt ::= "for" "(" (Decl | Exp? ";") Exp? ";" Exp? ")" Stmt;
static Node *for_stmt(Token **rest, Token *tok) {
    enter_scope();
    Node *node = new_node(ND_FOR, tok);
    assert(tok->next->kind == TK_LPAREN);
    tok = tok->next->next;

    // Init
    if (tok->kind != TK_SEMI) node->init = expr(&tok, tok);
    assert(tok->kind == TK_SEMI);
    tok = tok->next;

    // Cond
    if (tok->kind != TK_SEMI) node->cond = expr(&tok, tok);
    assert(tok->kind == TK_SEMI);
    tok = tok->next;

    // Inc
    if (tok->kind != TK_RPAREN) node->inc = expr(&tok, tok);
    assert(tok->kind == TK_RPAREN);

    // Body
    node->body = stmt(rest, tok->next);
    leave_scope();
    return node;
}

// WhileStmt ::= "while" "(" Exp ")" Stmt
static Node *while_stmt(Token **rest, Token *tok) {
    enter_scope();
    Node *node = new_node(ND_WHILE, tok);
    assert(tok->next->kind == TK_LPAREN);
    // Cond
    node->cond = expr(&tok, tok->next->next);
    assert(tok->kind == TK_RPAREN);
    // Body
    node->then = stmt(rest, tok->next);
    leave_scope();
    return node;
}

// DoStmt ::= "do" Stmt "while" "(" Exp ")" ";";
static Node *do_stmt(Token **rest, Token *tok) {
    enter_scope();
    Node *node = new_node(ND_DO, tok);
    // Body
    node->body = stmt(&tok, tok->next);
    // Cond
    assert(tok->kind == TK_WHILE);
    assert(tok->next->kind == TK_LPAREN);
    node->cond = expr(&tok, tok->next->next);
    assert(tok->kind == TK_RPAREN);
    assert(tok->next->kind == TK_SEMI);
    *rest = tok->next->next;
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

// Returns true if a given token represents a type.
static bool is_typename(Token *tok) { return tok->kind == TK_INT || tok->kind == TK_CHAR; }

// CompStmt ::= "{" BlockItem* "}"
// BlockItem ::= Stmt | Decl
static Node *compound_stmt(Token **rest, Token *tok) {
    enter_scope();
    Node *node = new_node(ND_COMP_STMT, tok);
    Node dummy, *cur = &dummy;

    tok = tok->next;
    while (tok->kind != TK_RBRACE) {
        if (is_typename(tok))
            cur = cur->next = declaration(&tok, tok);
        else
            cur = cur->next = stmt(&tok, tok);
        add_type(cur);
    }
    cur->next = NULL;
    *rest = tok->next;

    node->body = dummy.next;
    leave_scope();
    return node;
}

// DeclSpec ::= "int" | "char"
static Type *declspec(Token **rest, Token *tok) {
    if (tok->kind == TK_CHAR) {
        *rest = tok->next;
        return ty_char;
    }
    *rest = tok->next;
    return ty_int;
}

// DeclrSuf  ::= "(" ParamList? ")" | "[" Num "]"
// ParamList ::= ParamDecl ("," ParamDecl)*
// ParamDecl ::= DeclSpec Declr
static Type *decl_suffix(Token **rest, Token *tok, Type *ty) {
    if (tok->kind == TK_LPAREN) {
        tok = tok->next;

        Type dummy = {};
        Type *cur = &dummy;

        while (tok->kind != TK_RPAREN) {
            if (cur != &dummy && tok->kind == TK_COMMA) tok = tok->next;
            Type *basety = declspec(&tok, tok);
            Type *paramty = declarator(&tok, tok, basety);
            cur = cur->next = copy_type(paramty);
        }
        *rest = tok->next;

        cur->next = NULL;
        ty = func_type(ty);
        ty->func.params = dummy.next;
        return ty;
    }
    if (tok->kind == TK_LBRACKET) {
        int sz = get_number(tok->next);
        tok = tok->next->next->next;
        ty = decl_suffix(rest, tok, ty);
        return array_of(ty, sz);
    }

    *rest = tok;
    return ty;
}

// Declr ::= "*"* Ident DeclrSuf*
static Type *declarator(Token **rest, Token *tok, Type *ty) {
    while (match(&tok, tok, TK_STAR)) ty = pointer_to(ty);

    assert(tok->kind == TK_IDENT);
    ty = decl_suffix(rest, tok->next, ty);
    ty->name = tok;
    return ty;
}

// Decl ::= DeclSpec (Declr ("=" AsExp)? ("," Declr ("=" AsExp)?)*)? ";";
static Node *declaration(Token **rest, Token *tok) {
    Node *node = new_node(ND_DECL, tok);
    Type *basety = declspec(&tok, tok);
    Node dummy, *cur = &dummy;
    int i = 0;

    while (tok->kind != TK_SEMI) {
        if (i++ && tok->kind == TK_COMMA) tok = tok->next;
        Type *ty = declarator(&tok, tok, basety);
        Obj *var = new_lvar(get_ident(ty->name), ty);

        switch (tok->kind) {
            case TK_AS: {
                Node *lhs = new_var_node(var, ty->name);
                Node *rhs = assign(&tok, tok->next);
                Node *node = new_binary(ND_AS, lhs, rhs, tok);
                add_type(node);
                cur = cur->next = new_unary(ND_EXPR_STMT, node, tok);
            }
            default:
                break;
        }
    }
    *rest = tok->next;
    cur->next = NULL;

    node->body = dummy.next;
    return node;
}

static void create_param_lvars(Type *param) {
    if (param) {
        create_param_lvars(param->next);
        new_lvar(get_ident(param->name), param);
    }
}

// FuncDef ::= DeclSpec Declr CompStmt
static Token *function(Token *tok, Type *basety) {
    Type *ty = declarator(&tok, tok, basety);

    Obj *fn = new_gvar(get_ident(ty->name), ty);
    fn->is_function = true;

    locals = NULL;
    enter_scope();
    create_param_lvars(ty->func.params);
    fn->params = locals;
    uint32_t i = 0;
    Obj *cur = locals;
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

static Token *global_variable(Token *tok, Type *basety) {
    bool first = true;

    while (!match(&tok, tok, TK_SEMI)) {
        if (!first && tok->kind == TK_COMMA) tok = tok->next;
        first = false;

        Type *ty = declarator(&tok, tok, basety);
        new_gvar(get_ident(ty->name), ty);
    }
    return tok;
}

// Lookahead tokens and returns true if a given token is a start
// of a function definition or declaration.
static bool is_function(Token *tok) {
    if (tok->kind == TK_SEMI) return false;

    Type dummy = {};
    Type *ty = declarator(&tok, tok, &dummy);
    return ty->kind == TY_FUNC;
}

// TransUnit ::= ExDecl+;
// ExDecl ::= FuncDef | Decl;
Obj *parse(Token *tok) {
    globals = NULL;

    while (tok->kind != TK_EOF) {
        Type *basety = declspec(&tok, tok);
        // Function
        if (is_function(tok)) {
            tok = function(tok, basety);
            continue;
        }

        // Global variable
        tok = global_variable(tok, basety);
    }
    return globals;
}
