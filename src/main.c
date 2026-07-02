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
    TK_EOF,
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
    TK_EQ,
    TK_NE,
    TK_LT,
    TK_GT,
    TK_LE,
    TK_GE,

    TK_BOR,
    TK_XOR,
    TK_BAND,
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
    TK_LOGNOT,
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
} TokenKind;

typedef struct Token {
    TokenKind kind;
    struct Token *next;
    char *loc, *end;
    int val;
} Token;

static bool start_with(char *p, char *q) { return strncmp(p, q, strlen(q)) == 0; }

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
        {"-", TK_MINUS},       {"~", TK_INVERT},   {"!", TK_LOGNOT},    {"/", TK_SLASH},      {"<", TK_LT},
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
            cur = cur->next = new_token(TK_EOF, p, p);
            p += read_punct(p, &cur->kind);
            cur->end = p;
            continue;
        }
    }

    cur->next = new_token(TK_EOF, p, p);
    return dummy.next;
}

int main(int argc, char **argv) {
    if (argc != 2) return 1;

    Token *tok = tokenize(argv[1]);
    int cur_reg = 1;

    printf("define i32 @main() {\n");
    printf("entry:\n");

    // The first token must be a number
    printf("  %%%d = add nsw i32 0, %d\n", cur_reg, tok->val);
    tok = tok->next;

    // <number> ([+-] <number>)*
    while (tok->kind != TK_EOF) {
        if (tok->kind == TK_PLUS) {
            int old_reg = cur_reg++;
            printf("  %%%d = add nsw i32 %%%d, %d\n", cur_reg, old_reg, tok->next->val);
            tok = tok->next->next;
        } else if (tok->kind == TK_MINUS) {
            int old_reg = cur_reg++;
            printf("  %%%d = sub nsw i32 %%%d, %d\n", cur_reg, old_reg, tok->next->val);
            tok = tok->next->next;
        }
    }

    printf("  ret i32 %%%d\n", cur_reg);
    printf("}\n");

    freeall();
    return 0;
}
