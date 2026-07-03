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

// Exp        ::= OrExp;
// OrExp      ::= XorExp     { "|" XorExp};
// XorExp     ::= AndExp     { "^" AndExp};
// AndExp     ::= EqExp      { "&" EqExp};
// EqExp      ::= RelExp     { ("==" | "!=") RelExp};
// RelExp     ::= ShiftExp   { ("<" | ">" | "<=" | ">=") ShiftExp};
// ShiftExp   ::= AddExp     { ("<<" | ">>") AddExp};
// AddExp     ::= MulExp     { ("+" | "-") MulExp};
// MulExp     ::= UnaryExp   { ("*" | "/" | "%") UnaryExp};
// UnaryExp   ::= PrimaryExp | UnaryOp UnaryExp;
// UnaryOp    ::= "+" | "-" | "~" | "!" ;
// PrimaryExp ::= Number | "(" Exp ")";

static Node *expr(Token **rest, Token *tok);

static Node *primary(Token **rest, Token *tok) {
    Node *node = NULL;
    if (tok->kind == TK_LPAREN) {
        node = expr(&tok, tok->next);
        assert(tok->kind == TK_RPAREN);
    } else if (tok->kind == TK_NUM) {
        node = new_num(tok->val);
    }
    *rest = tok->next;
    return node;
}

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

static Node *expr(Token **rest, Token *tok) {
    Node *node = binexpr(&tok, tok, 0);
    *rest = tok;
    return node;
}

Node *parse(Token *tok) { return expr(&tok, tok); }
