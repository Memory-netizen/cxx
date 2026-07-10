#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cxx.h"

// Attempt to match the given token type
// If matched, consume the token and return true;
// otherwise, leave the token unconsumed and return false.
bool match(Token **rest, Token *tok, TokenKind kind) {
    if (tok->kind == kind) {
        *rest = tok->next;
        return true;
    }
    *rest = tok;
    return false;
}

// Compare if the pending matching string matches the target string
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

static void convert_keywords(Token *tok) {
    static struct {
        char *keyword;
        TokenKind type;
    } kw[] = {
        {"_Alignas", TK_ALIGNAS},
        {"_Alignof", TK_ALIGNOF},
        {"_Atomic", TK_ATOMIC},
        {"_BitInt", TK_BITINT},
        {"_Bool", TK_BOOL},
        {"_Countof", TK_COUNTOF},
        {"_Generic", TK_GENERIC},
        {"_Noreturn", TK_NORETURN},
        {"_Static_assert", TK_STATIC_ASSERT},
        {"_Thread_local", TK_THREAD},
        {"__asm", TK_ASM},
        {"__asm__", TK_ASM},
        {"__attribute__", TK_ATTR},
        {"__restrict", TK_RESTRICT},
        {"__restrict__", TK_RESTRICT},
        {"__thread", TK_THREAD},
        {"alignas", TK_ALIGNAS},
        {"alignof", TK_ALIGNOF},
        {"asm", TK_ASM},
        {"auto", TK_AUTO},
        {"bool", TK_BOOL},
        {"break", TK_BREAK},
        {"case", TK_CASE},
        {"char", TK_CHAR},
        {"const", TK_CONST},
        {"constexpr", TK_CONSTEXPR},
        {"continue", TK_CONTINUE},
        {"default", TK_DEFAULT},
        {"do", TK_DO},
        {"double", TK_DOUBLE},
        {"else", TK_ELSE},
        {"enum", TK_ENUM},
        {"extern", TK_EXTERN},
        {"false", TK_FALSE},
        {"float", TK_FLOAT},
        {"for", TK_FOR},
        {"goto", TK_GOTO},
        {"if", TK_IF},
        {"inline", TK_INLINE},
        {"int", TK_INT},
        {"long", TK_LONG},
        {"nullptr", TK_NULLPTR},
        {"register", TK_REGISTER},
        {"restrict", TK_RESTRICT},
        {"return", TK_RETURN},
        {"short", TK_SHORT},
        {"signed", TK_SIGNED},
        {"sizeof", TK_SIZEOF},
        {"static", TK_STATIC},
        {"static_assert", TK_STATIC_ASSERT},
        {"struct", TK_STRUCT},
        {"switch", TK_SWITCH},
        {"thread_local", TK_THREAD},
        {"true", TK_TRUE},
        {"typedef", TK_TYPEDEF},
        {"typeof", TK_TYPEOF},
        {"typeof_unqual", TK_TYPEOF_U},
        {"union", TK_UNION},
        {"unsigned", TK_UNSIGNED},
        {"void", TK_VOID},
        {"volatile", TK_VOLATILE},
        {"while", TK_WHILE},
    };
    while (tok->kind != TK_EOF) {
        if (tok->kind != TK_IDENT) {
            tok = tok->next;
            continue;
        }
        for (size_t i = 0; i < sizeof(kw) / sizeof(kw[0]); ++i)
            if (tok->len == strlen(kw[i].keyword) && start_with(tok->loc, kw[i].keyword)) {
                tok->kind = kw[i].type;
                break;
            }
        tok = tok->next;
    }
}

// Create a new token.
static Token *new_token(TokenKind kind, char *start, char *end) {
    Token *tok = emalloc(sizeof(Token));
    tok->kind = kind;
    tok->loc = start;
    tok->len = end - start;
    return tok;
}

static Token *read_string_literal(char *start) {
    char *p = start + 1;
    for (; *p != '"'; p++)
        if (*p == '\n' || *p == '\0') {
            fprintf(stderr, "unclosed string literal");
            exit(1);
        }

    Token *tok = new_token(TK_STRLIT, start, p + 1);
    tok->id = intern(start + 1, p - start - 1);
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

        // String literal
        if (*p == '"') {
            cur = cur->next = read_string_literal(p);
            p += cur->len;
            continue;
        }

        // Identifier
        if (is_ident0(*p)) {
            char *start = p;
            do {
                p++;
            } while (is_ident1(*p));
            Token *ident = new_token(TK_IDENT, start, p);
            ident->id = intern(ident->loc, ident->len);
            cur = cur->next = ident;
            continue;
        }

        // other char
        if (*p == '`' || *p == '@' || *p == '$') {
            // error
            exit(1);
            p++;
            continue;
        }

        // Punctuator
        if (ispunct(*p)) {
            cur = cur->next = new_token(TK_PUNCT, p, p);
            p += cur->len = read_punct(p, &cur->kind);
            continue;
        }
    }

    cur->next = new_token(TK_EOF, p, p);
    convert_keywords(dummy.next);
    return dummy.next;
}
