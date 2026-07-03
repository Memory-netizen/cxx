#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cxx.h"

static inline bool start_with(char *p, char *q) { return strncmp(p, q, strlen(q)) == 0; }

// Returns true if c is ident_start.
static inline bool is_ident0(char c) { return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_'; }

// Returns true if c is ident_continue.
static inline bool is_ident1(char c) { return is_ident0(c) || ('0' <= c && c <= '9'); }

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
    tok->len = end - start;
    return tok;
}

// Tokenize 'input' and returns token list.
Token *tokenize(char *input) {
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
            char *q = p;
            cur = cur->next = new_token(TK_NUM, p, p);
            cur->val = strtoul(p, &p, 10);
            cur->len = p - q;
            continue;
        }

        // Identifier
        if (is_ident0(*p)) {
            char *start = p;
            do {
                p++;
            } while (is_ident1(*p));
            cur = cur->next = new_token(TK_IDENT, start, p);
            continue;
        }

        // Punctuator
        if (ispunct(*p)) {
            cur = cur->next = new_token(TK_NOP, p, p);
            p += cur->len = read_punct(p, &cur->kind);
            continue;
        }
    }

    cur->next = new_token(TK_EOF, p, p);
    return dummy.next;
}
