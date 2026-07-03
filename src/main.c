#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// Simple memory pool
//

#define POOL_SIZE (32 * 1024 * 1024)  // 32MB
#define ALIGNMENT 8
#define HEAD_SIZE (((sizeof(void *) + ALIGNMENT - 1) / ALIGNMENT) * ALIGNMENT)

static void **pool;
static size_t len;

typedef struct big_block {
    void *ptr;
    struct big_block *next;
} big_block;
static big_block *big_blocks;

void *emalloc(size_t n) {
    if (n == 0) return NULL;
    n = (n + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    if (n > POOL_SIZE - HEAD_SIZE) {
        void *p = malloc(n);
        big_block *b = malloc(sizeof(big_block));
        if (!p || !b) {
            fprintf(stderr, "emalloc: out of memory\n");
            exit(1);
        }
        b->ptr = p;
        b->next = big_blocks;
        big_blocks = b;
        return p;
    }

    if (len < n) {
        void **new_pool = malloc(POOL_SIZE);
        if (!new_pool) {
            fprintf(stderr, "emalloc: out of memory\n");
            exit(1);
        }
        new_pool[0] = pool;
        pool = new_pool;
        len = POOL_SIZE - HEAD_SIZE;
    }

    void *p = (char *)pool + HEAD_SIZE + (len - n);
    len -= n;
    return p;
}

void freeall(void) {
    while (pool) {
        void **pp = pool[0];
        free(pool);
        pool = pp;
    }
    while (big_blocks) {
        big_block *b = big_blocks;
        big_blocks = b->next;
        free(b->ptr);
        free(b);
    }
    len = 0;
}

//
// Tokenizer
//

// token ::= keyword | ident | constant | strlit | punctuator;
// pp-token ::= header_name | ident | pp_num | charlit | strlit | punctuator | other;
typedef enum {
    TK_NOP,
    TK_AS,
    TK_ADDAS,
    TK_SUBAS,
    TK_MULAS,
    TK_DIVAS,
    TK_MODAS,

    TK_ANDAS,
    TK_ORAS,
    TK_XORAS,
    TK_LEFTAS,
    TK_RIGHTAS,

    TK_OR,
    TK_AND,
    TK_BOR,
    TK_XOR,
    TK_BAND,

    TK_EQ,
    TK_NE,
    TK_LT,
    TK_GT,
    TK_LE,
    TK_GE,

    TK_LEFT,
    TK_RIGHT,

    TK_PLUS,
    TK_MINUS,
    TK_STAR,
    TK_SLASH,
    TK_MOD,

    TK_INC,
    TK_DEC,
    TK_INVERT,
    TK_NOT,
    TK_DOT,
    TK_ARROW,

    TK_LPAREN,
    TK_RPAREN,
    TK_LBRACKET,
    TK_RBRACKET,
    TK_LBRACE,
    TK_RBRACE,
    TK_COMMA,
    TK_SEMI,
    TK_COLON,
    TK_COLONCOLON,
    TK_QUESTION,
    TK_ELLIPSIS,
    TK_HASH,
    TK_HASHHASH,

    TK_IDENT,
    TK_KEYWORD,
    TK_NUM,
    TK_PPNUM,
    TK_CHAR,
    TK_STR,
    TK_EOF,
} TokenKind;

typedef struct Token Token;
struct Token {
    TokenKind kind;
    struct Token *next;
    char *loc, *end;
    int val;
};

static inline bool start_with(char *p, char *q) { return strncmp(p, q, strlen(q)) == 0; }

static int read_punct(char *p, TokenKind *type) {
    static struct {
        char *punct;
        TokenKind type;
    } punct[] = {
        {"%:%:", TK_HASHHASH}, {"<<=", TK_LEFTAS}, {">>=", TK_RIGHTAS}, {"...", TK_ELLIPSIS}, {"<%", TK_LBRACE},
        {"%>", TK_RBRACE},     {"%:", TK_HASH},    {"%=", TK_MODAS},    {"+=", TK_ADDAS},     {"-=", TK_SUBAS},
        {"*=", TK_MULAS},      {"/=", TK_DIVAS},   {"&=", TK_ANDAS},    {"^=", TK_XORAS},     {"|=", TK_ORAS},
        {"::", TK_COLONCOLON}, {"<<", TK_LEFT},    {">>", TK_RIGHT},    {"==", TK_EQ},        {"!=", TK_NE},
        {"<=", TK_LE},         {">=", TK_GE},      {"&&", TK_AND},      {"||", TK_OR},        {"->", TK_ARROW},
        {"++", TK_INC},        {"--", TK_DEC},     {"##", TK_HASHHASH}, {"<:", TK_LBRACKET},  {":>", TK_RBRACKET},
        {"%", TK_MOD},         {"[", TK_LBRACKET}, {"]", TK_RBRACKET},  {"(", TK_LPAREN},     {")", TK_RPAREN},
        {"{", TK_LBRACE},      {"}", TK_RBRACE},   {"&", TK_BAND},      {"*", TK_STAR},       {"+", TK_PLUS},
        {"-", TK_MINUS},       {"~", TK_INVERT},   {"!", TK_NOT},       {"/", TK_SLASH},      {"<", TK_LT},
        {">", TK_GT},          {"^", TK_XOR},      {"|", TK_BOR},       {"?", TK_QUESTION},   {":", TK_COLON},
        {";", TK_SEMI},        {".", TK_DOT},      {"=", TK_AS},        {",", TK_COMMA},      {"#", TK_HASH},
    };

    for (size_t i = 0; i < sizeof(punct) / sizeof(punct[0]); ++i)
        if (start_with(p, punct[i].punct)) {
            *type = punct[i].type;
            return strlen(punct[i].punct);
        }

    return 0;
}

// Create a new token.
static Token *new_token(TokenKind kind, char *start, char *end) {
    Token *tok = emalloc(sizeof(Token));
    tok->kind = kind;
    tok->loc = start;
    tok->end = end;
    return tok;
}

// Tokenize 'input' and returns token list.
static Token *tokenize(char *input) {
    char *p = input;
    Token dummy, *cur = &dummy;

    while (*p) {
        // Skip whitespace characters.
        if (isspace(*p)) {
            p++;
            continue;
        }

        // Numeric literal
        if (isdigit(*p)) {
            cur = cur->next = new_token(TK_NUM, p, p);
            cur->val = strtoul(p, &p, 10);
            cur->end = p;
            continue;
        }

        // Punctuator
        if (ispunct(*p)) {
            cur = cur->next = new_token(TK_NOP, p, p);
            p += read_punct(p, &cur->kind);
            cur->end = p;
            continue;
        }
    }

    cur->next = new_token(TK_EOF, p, p);
    return dummy.next;
}

//
// Parser
//

typedef enum {
    ND_BOR,     // |
    ND_XOR,     // ^
    ND_BAND,    // &
    ND_EQ,      // ==
    ND_NE,      // !=
    ND_LT,      // <
    ND_GT,      // >
    ND_LE,      // <=
    ND_GE,      // >=
    ND_LEFT,    // <<
    ND_RIGHT,   // >>
    ND_ADD,     // +
    ND_SUB,     // -
    ND_MUL,     // *
    ND_DIV,     // /
    ND_MOD,     // %
    ND_PLUS,    // unary +
    ND_NEG,     // unary -
    ND_NOT,     // !
    ND_INVERT,  // ~
    ND_NUM,     // Int
} NodeKind;

// AST node type
typedef struct Node Node;
struct Node {
    NodeKind kind;  // Node kind
    Node *lhs;      // Left-hand side
    Node *rhs;      // Right-hand side
    int val;
};

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

int cur_reg = 1;

// decode operand to str
static void fmt_operand(char *buf, int encoded) {
    if (encoded > 0)
        sprintf(buf, "%%%d", encoded);
    else
        sprintf(buf, "%d", -encoded);
}

static int gen_expr(Node *node) {
    if (!node) return 0;
    // Imm node: returns the negative number without alloca reg
    if (node->kind == ND_NUM) {
        return -node->val;
    }

    int lr = gen_expr(node->lhs);
    int rr = gen_expr(node->rhs);
    int reg = cur_reg++;

    char lhs_str[32], rhs_str[32];
    fmt_operand(lhs_str, lr);
    fmt_operand(rhs_str, rr);

    switch (node->kind) {
        // unary arithmetic operation
        case ND_PLUS:
            printf("  %%%d = add nsw i32 0, %s\n", reg, lhs_str);
            break;
        case ND_NEG:
            printf("  %%%d = sub nsw i32 0, %s\n", reg, lhs_str);
            break;
        case ND_INVERT:
            printf("  %%%d = xor i32 -1, %s\n", reg, lhs_str);
            break;
        case ND_NOT:
            printf("  %%%d = icmp eq i32 %s, 0\n", reg, lhs_str);
            int zext_reg = cur_reg++;
            printf("  %%%d = zext i1 %%%d to i32\n", zext_reg, reg);
            return zext_reg;
        // binary arithmetic operation
        case ND_ADD:
            printf("  %%%d = add nsw i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;
        case ND_SUB:
            printf("  %%%d = sub nsw i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;
        case ND_MUL:
            printf("  %%%d = mul nsw i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;
        case ND_DIV:
            printf("  %%%d = sdiv i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;
        case ND_MOD:
            printf("  %%%d = srem i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;

        // shift operation
        case ND_LEFT:
            printf("  %%%d = shl nsw i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;
        case ND_RIGHT:
            printf("  %%%d = ashr i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;

        // bit operation
        case ND_BAND:
            printf("  %%%d = and i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;
        case ND_BOR:
            printf("  %%%d = or i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;
        case ND_XOR:
            printf("  %%%d = xor i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;

        // Comparison operations：icmp return i1，zext to i32
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_GT:
        case ND_LE:
        case ND_GE: {
            const char *pred;
            switch (node->kind) {
                case ND_EQ:
                    pred = "eq";
                    break;
                case ND_NE:
                    pred = "ne";
                    break;
                case ND_LT:
                    pred = "slt";
                    break;
                case ND_GT:
                    pred = "sgt";
                    break;
                case ND_LE:
                    pred = "sle";
                    break;
                case ND_GE:
                    pred = "sge";
                    break;
                default:
                    pred = "";
            }
            printf("  %%%d = icmp %s i32 %s, %s\n", reg, pred, lhs_str, rhs_str);
            int zext_reg = cur_reg++;
            printf("  %%%d = zext i1 %%%d to i32\n", zext_reg, reg);
            return zext_reg;
        }

        default:
            fprintf(stderr, "gen_expr: unknown node kind %d\n", node->kind);
            exit(1);
    }

    return reg;
}

int main(int argc, char **argv) {
    if (argc != 2) return 1;

    Token *tok = tokenize(argv[1]);
    Node *node = expr(&tok, tok);

    printf("define i32 @main() {\n");
    printf("entry:\n");

    int ret_val = gen_expr(node);
    if (ret_val > 0)
        printf("  ret i32 %%%d\n", ret_val);
    else
        printf("  ret i32 %d\n", -ret_val);

    printf("}\n");

    freeall();
    return 0;
}
