#include "cxx.h"

static Node *new_node(NodeKind kind) {
    Node *node = emalloc(sizeof(Node));
    node->kind = kind;
    return node;
}

static Node *new_num(int val) {
    Node *node = new_node(ND_NUM);
    node->val = val;
    return node;
}

static Node *new_var_node(Obj *var) {
    Node *node = new_node(ND_VAR);
    node->var = var;
    return node;
}

Obj *locals;
// Find a local variable by name.
static Obj *find_var(Token *tok) {
    for (Obj *var = locals; var; var = var->next)
        if (strlen(var->name) == tok->len && !strncmp(tok->loc, var->name, tok->len)) return var;
    return NULL;
}

static Obj *new_lvar(char *name) {
    Obj *var = emalloc(sizeof(Obj));
    var->name = name;
    var->next = locals;
    locals = var;
    return var;
}

static Node *new_binary(NodeKind kind, Node *lhs, Node *rhs) {
    Node *node = new_node(kind);
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

static Node *new_unary(NodeKind kind, Node *expr) {
    Node *node = new_node(kind);
    node->lhs = expr;
    node->rhs = NULL;
    return node;
}

static Node *expr(Token **rest, Token *tok);

// PrimaryExp ::= Number | ident | "(" Exp ")";
static Node *primary(Token **rest, Token *tok) {
    Node *node = NULL;
    if (tok->kind == TK_LPAREN) {
        node = expr(&tok, tok->next);
        assert(tok->kind == TK_RPAREN);
    } else if (tok->kind == TK_NUM) {
        node = new_num(tok->val);
    } else if (tok->kind == TK_IDENT) {
        Obj *var = find_var(tok);
        if (!var) var = new_lvar(strndup(tok->loc, tok->len));
        node = new_var_node(var);
    }
    *rest = tok->next;
    return node;
}

// UnaryExp   ::= PrimaryExp | UnaryOp UnaryExp;
// UnaryOp    ::= "+" | "-" | "~" | "!" ;
static Node *unary(Token **rest, Token *tok) {
    switch (tok->kind) {
        case TK_PLUS:
            return new_unary(ND_PLUS, unary(rest, tok->next));
            break;
        case TK_MINUS:
            return new_unary(ND_NEG, unary(rest, tok->next));
            break;
        case TK_INVERT:
            return new_unary(ND_INVERT, unary(rest, tok->next));
            break;
        case TK_NOT:
            return new_unary(ND_NOT, unary(rest, tok->next));
            break;
        default:
            break;
    }
    return primary(rest, tok);
}

// OrExp      ::= XorExp     { "|" XorExp};
// XorExp     ::= AndExp     { "^" AndExp};
// AndExp     ::= EqExp      { "&" EqExp};
// EqExp      ::= RelExp     { ("==" | "!=") RelExp};
// RelExp     ::= ShiftExp   { ("<" | ">" | "<=" | ">=") ShiftExp};
// ShiftExp   ::= AddExp     { ("<<" | ">>") AddExp};
// AddExp     ::= MulExp     { ("+" | "-") MulExp};
// MulExp     ::= UnaryExp   { ("*" | "/" | "%") UnaryExp};
static Node *binexpr(Token **rest, Token *tok, int min_prec) {
    static int op_table[][2] = {
        [TK_BOR] = {40, ND_BOR},    [TK_XOR] = {50, ND_XOR},   [TK_BAND] = {60, ND_BAND},   [TK_EQ] = {70, ND_EQ},
        [TK_NE] = {70, ND_NE},      [TK_LT] = {80, ND_LT},     [TK_GT] = {80, ND_GT},       [TK_LE] = {80, ND_LE},
        [TK_GE] = {80, ND_GE},      [TK_LEFT] = {90, ND_LEFT}, [TK_RIGHT] = {90, ND_RIGHT}, [TK_PLUS] = {100, ND_ADD},
        [TK_MINUS] = {100, ND_SUB}, [TK_STAR] = {110, ND_MUL}, [TK_SLASH] = {110, ND_DIV},  [TK_MOD] = {110, ND_MOD},
    };

    Node *lhs = unary(&tok, tok);

    while (TK_BOR <= tok->kind && tok->kind <= TK_MOD) {
        NodeKind expr_op = op_table[tok->kind][1];
        int cur_prec = op_table[tok->kind][0];
        if (cur_prec <= min_prec) break;
        Node *rhs = binexpr(&tok, tok->next, cur_prec);
        lhs = new_binary(expr_op, lhs, rhs);
    }
    *rest = tok;
    return lhs;
}

// AssignExp ::= OrExp { "=" AssignExp };
static Node *assign(Token **rest, Token *tok) {
    Node *node = binexpr(&tok, tok, 0);
    while (tok->kind == TK_AS) node = new_binary(ND_AS, node, assign(&tok, tok->next));
    *rest = tok;
    return node;
}

// Exp ::= AssignExp { "," AssignExp };
static Node *expr(Token **rest, Token *tok) {
    Node *node = assign(&tok, tok);
    while (tok->kind == TK_COMMA) node = new_binary(ND_COMMA, node, assign(&tok, tok->next));
    *rest = tok;
    return node;
}

static Node *stmt(Token **rest, Token *tok);
static Node *compound_stmt(Token **rest, Token *tok);

// ExpStmt = [Exp] ";";
static Node *expr_stmt(Token **rest, Token *tok) {
    Node *node = NULL;

    if (tok->kind != TK_SEMI) node = expr(&tok, tok);
    assert(tok->kind == TK_SEMI);
    *rest = tok->next;

    return new_unary(ND_EXPR_STMT, node);
}

// RetStmt ::= "return" [Exp] ";";
static Node *return_stmt(Token **rest, Token *tok) {
    Node *node = NULL;

    if (tok->next->kind != TK_SEMI) node = expr(&tok, tok->next);
    assert(tok->kind == TK_SEMI);
    *rest = tok->next;

    return new_unary(ND_RETURN, node);
}

// Stmt ::= ExpStmt | CompStmt | RetStmt;
static Node *stmt(Token **rest, Token *tok) {
    Node *node;
    switch (tok->kind) {
        case TK_LBRACE:
            node = compound_stmt(&tok, tok);
            break;
        case TK_RETURN:
            node = return_stmt(&tok, tok);
            break;

        default:
            node = expr_stmt(&tok, tok);
            break;
    }
    *rest = tok;
    return node;
}

// CompStmt ::= "{" {Stmt} "}";
static Node *compound_stmt(Token **rest, Token *tok) {
    assert(tok->kind == TK_LBRACE);
    Node dummy, *cur = &dummy;
    cur->next = NULL;

    tok = tok->next;
    while (tok->kind != TK_RBRACE) cur = cur->next = stmt(&tok, tok);
    *rest = tok->next;

    Node *node = new_node(ND_COMP_STMT);
    node->items = dummy.next;
    return node;
}

Function *parse(Token *tok) {
    Function *prog = emalloc(sizeof(Function));
    prog->body = compound_stmt(&tok, tok);
    prog->locals = locals;
    return prog;
}
