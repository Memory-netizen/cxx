#include "cxx.h"

SrcFile *cur_file = NULL;

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

static char *expect[] = {
    [TK_LEFTAS] = "<<=", [TK_RIGHTAS] = ">>=",   [TK_ELLIPSIS] = "...", [TK_MODAS] = "%=",    [TK_ADDAS] = "+=",
    [TK_SUBAS] = "-=",   [TK_MULAS] = "*=",      [TK_DIVAS] = "/=",     [TK_ANDAS] = "&=",    [TK_XORAS] = "^=",
    [TK_ORAS] = "|=",    [TK_COLONCOLON] = "::", [TK_LEFT] = "<<",      [TK_RIGHT] = ">>",    [TK_EQ] = "==",
    [TK_NE] = "!=",      [TK_LE] = "<=",         [TK_GE] = ">=",        [TK_AND] = "&&",      [TK_OR] = "||",
    [TK_ARROW] = "->",   [TK_INC] = "++",        [TK_DEC] = "--",       [TK_HASHHASH] = "##", [TK_MOD] = "%",
    [TK_LBRACKET] = "[", [TK_RBRACKET] = "]",    [TK_LPAREN] = "(",     [TK_RPAREN] = ")",    [TK_LBRACE] = "{",
    [TK_RBRACE] = "}",   [TK_BAND] = "&",        [TK_STAR] = "*",       [TK_PLUS] = "+",      [TK_MINUS] = "-",
    [TK_INVERT] = "~",   [TK_NOT] = "!",         [TK_SLASH] = "/",      [TK_LT] = "<",        [TK_GT] = ">",
    [TK_XOR] = "^",      [TK_BOR] = "|",         [TK_QUESTION] = "?",   [TK_COLON] = ":",     [TK_SEMI] = ";",
    [TK_DOT] = ".",      [TK_AS] = "=",          [TK_COMMA] = ",",      [TK_HASH] = "#",      [TK_WHILE] = "while",
};

// Ensure that the current token is `kind`.
Token *skip(Token *tok, TokenKind kind) {
    if (tok->kind != kind) error(tok, "expected ‘%s’ before ‘%.*s’", expect[kind], tok->len, tok->loc);
    return tok->next;
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

// Fill position metadata into a token.
static void fill_tok(Token *tok, char *filename, int line, int col, bool is_sol, bool is_leadingws) {
    tok->filename = filename;
    tok->line = line;
    tok->col = col;
    tok->is_sol = is_sol;
    tok->is_leadingws = is_leadingws;
}

static int from_hex(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

static int read_escaped_char(char **new_pos, char *p) {
    if ('0' <= *p && *p <= '7') {
        // Read an octal number.
        int c = *p++ - '0';
        if ('0' <= *p && *p <= '7') {
            c = (c << 3) + (*p++ - '0');
            if ('0' <= *p && *p <= '7') c = (c << 3) + (*p++ - '0');
        }
        *new_pos = p;
        return c;
    }

    if (*p == 'x') {
        // Read a hexadecimal number.
        p++;
        if (!isxdigit(*p)) error_at(p, "invalid hex escape sequence");

        int c = 0;
        for (; isxdigit(*p); p++) c = (c << 4) + from_hex(*p);
        *new_pos = p;
        return c;
    }

    *new_pos = p + 1;

    switch (*p) {
        case 'a':
            return '\a';
        case 'b':
            return '\b';
        case 't':
            return '\t';
        case 'n':
            return '\n';
        case 'v':
            return '\v';
        case 'f':
            return '\f';
        case 'r':
            return '\r';
        // [GNU] \e for the ASCII escape character is a GNU C extension.
        case 'e':
            return 27;
        default:
            return (unsigned char)*p;
    }
}

// Find a closing double-quote.
static char *string_literal_end(char *p) {
    for (char *start = p++; *p != '"'; p++) {
        if (*p == '\n' || *p == '\0') error_at(start, "missing terminating \" character");
        if (*p == '\\') p++;
    }
    return p;
}

static Token *read_string_literal(char *start) {
    char *end = string_literal_end(start);
    char buf[end - start];
    int len = 0;

    for (char *p = start + 1; p < end;) {
        if (*p == '\\')
            buf[len++] = read_escaped_char(&p, p + 1);
        else
            buf[len++] = *p++;
    }

    Token *tok = new_token(TK_STRLIT, start, end + 1);
    tok->ty = array_of(ty_char, len + 1);
    tok->id = intern(buf, len);
    return tok;
}

static Token *read_char_literal(char *start) {
    char *p = start + 1;
    if (*p == '\0') error_at(start, "unclosed char literal");
    int c;
    if (*p == '\\')
        c = read_escaped_char(&p, p + 1);
    else
        c = (unsigned char)*p++;

    char *end = strchr(p, '\'');
    if (!end) error_at(p, "unclosed char literal");

    Token *tok = new_token(TK_NUM, start, end + 1);
    tok->val = c;
    return tok;
}

static int is_valid_digit(int c, int base) {
    if (base == 2)
        return c == '0' || c == '1';
    else if (base == 8)
        return '0' <= c && c <= '7';
    else if (base == 10)
        return '0' <= c && c <= '9';
    else
        return ('0' <= c && c <= '9') || ('a' <= c && c <= 'f') || ('A' <= c && c <= 'F');
}

enum {
    SUF_INT = 0x001,
    SUF_UNSIGNED = 0x002,
    SUF_LONG = 0x004,
    SUF_LLONG = 0x008,
    SUF_FLOAT = 0x010,
    SUF_DOUBLE = 0x020,
    SUF_LDOUBLE = 0x040,
    SUF_BITINT = 0x080,
};

static Type *infer_type(uint64_t val, int flags, int base) {
    Type *ty;
    if (base == 10) {
        switch (flags) {
            case SUF_UNSIGNED | SUF_LLONG:
                ty = ty_ullong;
                break;
            case SUF_LLONG:
                ty = ty_llong;
                break;
            case SUF_UNSIGNED | SUF_LONG:
                ty = val <= ULONG_MAX ? ty_ulong : ty_ullong;
                break;
            case SUF_LONG:
                ty = val <= LONG_MAX ? ty_long : val <= LLONG_MAX ? ty_llong : ty_ullong;
                break;
            case SUF_UNSIGNED:
                ty = val <= UINT_MAX ? ty_uint : val <= ULONG_MAX ? ty_ulong : ty_ullong;
                break;
            default:
                ty = val <= INT_MAX ? ty_int : val <= LONG_MAX ? ty_long : val <= LLONG_MAX ? ty_llong : ty_ullong;
                break;
        }
    } else {
        switch (flags) {
            case SUF_UNSIGNED | SUF_LLONG:
                ty = ty_ullong;
                break;
            case SUF_LLONG:
                ty = val <= LLONG_MAX ? ty_llong : ty_ullong;
                break;
            case SUF_UNSIGNED | SUF_LONG:
                ty = val <= ULONG_MAX ? ty_ulong : ty_ullong;
                break;
            case SUF_LONG:
                ty = val <= LONG_MAX ? ty_long : val <= ULONG_MAX ? ty_ulong : val <= LLONG_MAX ? ty_llong : ty_ullong;
                break;
            case SUF_UNSIGNED:
                ty = val <= UINT_MAX ? ty_uint : val <= ULONG_MAX ? ty_ulong : ty_ullong;
                break;
            default:
                ty = val <= INT_MAX     ? ty_int
                     : val <= UINT_MAX  ? ty_uint
                     : val <= LONG_MAX  ? ty_long
                     : val <= ULONG_MAX ? ty_ulong
                     : val <= LLONG_MAX ? ty_llong
                                        : ty_ullong;
                break;
        }
    }
    return ty;
}

void convert_pp_number(Token *t) {
    char *text = t->loc;
    char *end = t->loc + t->len;
    char first_ch = *text;

    int base = 10;
    char clean[t->len + 1];
    uint32_t ci = 0;

    // Stage 1: Process literal prefixes
    if (first_ch == '0') {
        int pre = text[1];
        if (pre == 'b' || pre == 'B' || pre == 'o' || pre == 'O' || pre == 'x' || pre == 'X') {
            switch (pre) {
                case 'b':
                case 'B':
                    base = 2;
                    break;
                case 'o':
                case 'O':
                    base = 8;
                    break;
                case 'x':
                case 'X':
                    base = 16;
                    break;
            }
            text += 2;
            if (!is_valid_digit(*text, base))
                error_at(text, "invalid suffix ‘%.*s’ on integer constant", end - text, text);
        }
    } else if (first_ch == '.') {
        clean[ci++] = '0';  // canonicalize
    }

    // Stage 2: Skip numeric separators ', filter out invalid characters
    // Record positions of e, E and .
    char c = *text++;
    int pos_e = 0;
    int pos_p = 0;
    int pos_dot = 0;
    int sign = 1;
    int is_float = 0;
    int prev_is_digit = 0;
    int flags = 0;

    // c = *text
    while (text <= end) {
        switch (c) {
            case '.':
                if (pos_dot || pos_e || pos_p || base == 2 || base == 8) goto error;
                pos_dot = ci;
                is_float = 1;
                prev_is_digit = 0;
                c = *text++;
                continue;
            case 'e':
            case 'E':
                if (base == 10) {
                    if (pos_e || pos_p) goto error;
                    pos_e = ci;
                } else {
                    if (base == 16)
                        break;  // e is an ordinary number
                    else
                        goto error;
                }
                c = *text++;
                if (c == '+' || c == '-') {
                    sign = c == '+' ? 1 : -1;
                    c = *text++;
                }
                if (!is_valid_digit(c, base)) error(t, "exponent has no digits");
                is_float = 1;
                break;
            case 'p':
            case 'P':
                if (base != 16 || pos_p) goto error;
                pos_p = ci;
                base = 10;
                c = *text++;
                if (c == '+' || c == '-') {
                    sign = c == '+' ? 1 : -1;
                    c = *text++;
                }
                if (!is_valid_digit(c, base)) error(t, "exponent has no digits");
                is_float = 1;
                break;
            case 'f':
            case 'F':
                if (base == 16) {
                    break;  // f is an ordinary number
                } else if (!is_float) {
                    goto error;
                } else {
                    if (text < end) goto error;
                    flags = SUF_FLOAT;
                    goto extract_end;
                }
            case 'u':
            case 'U':
            case 'l':
            case 'L':
            case 'w':
            case 'W':
                if (is_float) {
                    if (text < end) {
                        goto error;
                    } else if (c == 'l' || c == 'L') {
                        flags = SUF_LDOUBLE;
                        goto extract_end;
                    } else {
                        goto error;
                    }
                }
                while (text <= end) {
                    if (c == 'u' || c == 'U') {
                        if (flags & SUF_UNSIGNED) goto error;
                        flags |= SUF_UNSIGNED;
                    } else if (c == 'l' || c == 'L') {
                        if (flags & SUF_LLONG || flags & SUF_LONG || flags & SUF_BITINT) goto error;
                        if (c == *text) {
                            flags |= SUF_LLONG;
                            text++;
                        } else {
                            flags |= SUF_LONG;
                        }
                    } else if (c == 'w' || c == 'W') {
                        if (flags & SUF_LLONG || flags & SUF_LONG || flags & SUF_BITINT) goto error;
                        if ((c == 'w' && *text == 'b') || (c == 'W' && *text == 'B')) {
                            flags |= SUF_BITINT;
                            text++;
                        } else {
                            goto error;
                        }
                    } else {
                        goto error;
                    }
                    c = *text++;
                }
                goto extract_end;
            case '\'':
                if (prev_is_digit) {
                    c = *text++;
                    if (!is_valid_digit(c, base)) error(t, "digit separator ' not between digits");
                } else {
                    error(t, "digit separator ' not between digits");
                }
                break;
            default:
                if (!is_valid_digit(c, base)) goto error;
        }
        prev_is_digit = is_valid_digit(c, base);
        clean[ci++] = c;
        c = *text++;
    }
extract_end:
    clean[ci] = '\0';

    // Delayed octal check to support floating-point parsing
    if (base == 10 && !is_float && first_ch == '0') {
        base = 8;
        for (int i = 0; clean[i]; i++)
            if (clean[i] == '8' || clean[i] == '9') error(t, "invalid digit %c in octal constant", clean[i]);
    }
    if (base == 16 && is_float) error(t, "hexadecimal floating constant requires an exponent");
    base = pos_p ? 16 : base;  // Restore base to hexadecimal

    // Stage 3: Evaluate literals
    uint32_t pos = 0;
    uint64_t int_part = 0;
    double frac_part = 0.0;
    int exp = 0;
    int pos_exp = base == 10 ? pos_e : pos_p;

    uint32_t limit = is_float ? pos_dot ? pos_dot : pos_exp : ci;

    while (pos < limit) int_part = int_part * base + from_hex(clean[pos++]);

    if (!is_float) {
        t->val = int_part;
        t->ty = infer_type(int_part, flags, base);
        return;
    }

    // t->ty = ty_float;
    limit = pos_exp ? pos_exp : ci;

    double divisor = base;
    while (pos < limit) {
        frac_part += from_hex(clean[pos++]) / divisor;
        divisor *= base;
    }

    frac_part += int_part;
    if (!pos_exp) {
        t->val = frac_part;
        t->ty = infer_type(t->val, flags, base);
        return;
    }

    while (pos < ci) exp = exp * 10 + clean[pos++] - '0';

    exp *= sign;
    // t->val.f =
    //     base == 16 ? fast_ldexp(frac_part, exp) : frac_part * fast_pow10(exp);

    return;
error:
    text--;
    error_at(text, "invalid suffix ‘%.*s’ on constant", end - text, text);
}

static Token *read_int_literal(char *start) {
    char *p = start + 1;
    while (1) {
        int c = *p;
        if (c == 'e' || c == 'E' || c == 'p' || c == 'P') {
            c = *++p;
            if (c == '+' || c == '-')
                p++;
            else
                continue;
        } else {
            if (isalnum(c) || c == '.' || c == '_' || c == '\'')
                p++;
            else
                break;
        }
    }

    Token *tok = new_token(TK_NUM, start, p);
    convert_pp_number(tok);
    return tok;
}

// Advance col, tracking newlines.
static inline void advance_col(int *line, int *col, int n) {
    *col += n;
    (void)line;
}

// Tokenize a given string and returns new tokens.
static Token *tokenize(char *filename, char *p) {
    int line = 1;
    int col = 1;
    Token dummy, *cur = &dummy;

    while (*p) {
        bool is_leadingws = false;
        bool is_sol = cur == &dummy;
        // Skip whitespace and comments.
        for (;;) {
            // Skip line comments.
            if (start_with(p, "//")) {
                is_leadingws = true;
                p += 2;
                col += 2;
                while (*p != '\n' && *p != '\0') {
                    p++;
                    col++;
                }
                continue;
            }

            // Skip block comments.
            if (start_with(p, "/*")) {
                is_leadingws = true;
                char *q = strstr(p + 2, "*/");
                if (!q) error_at(p, "unterminated /* comment");
                for (char *r = p; r < q + 2; r++) {
                    if (*r == '\n') {
                        line++;
                        col = 1;
                    } else {
                        col++;
                    }
                }
                p = q + 2;
                continue;
            }

            if (*p == '\n') {
                is_leadingws = false;
                is_sol = true;
                line++;
                col = 1;
                p++;
                continue;
            }

            if (isspace(*p)) {
                is_leadingws = true;
                col++;
                p++;
                continue;
            }

            break;
        }

        if (*p == '\0') break;

        Token *tok;

        // Numeric literal
        if (isdigit(*p)) {
            tok = read_int_literal(p);
            fill_tok(tok, filename, line, col, is_sol, is_leadingws);
            cur = cur->next = tok;
            p += tok->len;
            advance_col(&line, &col, tok->len);
            continue;
        }

        // String literal
        if (*p == '"') {
            tok = read_string_literal(p);
            fill_tok(tok, filename, line, col, is_sol, is_leadingws);
            cur = cur->next = tok;
            p += tok->len;
            advance_col(&line, &col, tok->len);
            continue;
        }

        // Character literal
        if (*p == '\'') {
            tok = read_char_literal(p);
            fill_tok(tok, filename, line, col, is_sol, is_leadingws);
            cur = cur->next = tok;
            p += tok->len;
            advance_col(&line, &col, tok->len);
            continue;
        }

        // Identifier
        if (is_ident0(*p)) {
            char *start = p;
            do {
                p++;
            } while (is_ident1(*p));
            tok = new_token(TK_IDENT, start, p);
            tok->id = intern(tok->loc, tok->len);
            fill_tok(tok, filename, line, col, is_sol, is_leadingws);
            cur = cur->next = tok;
            advance_col(&line, &col, tok->len);
            continue;
        }

        // other char
        if (*p == '`' || *p == '@' || *p == '$') error_at(p, "stray ‘%s’ in program", *p);

        // Punctuator
        if (ispunct(*p)) {
            tok = new_token(TK_PUNCT, p, p);
            tok->len = read_punct(p, &tok->kind);
            fill_tok(tok, filename, line, col, is_sol, is_leadingws);
            cur = cur->next = tok;
            p += tok->len;
            advance_col(&line, &col, tok->len);
            continue;
        }

        error_at(p, "invalid token");
    }

    Token *eof = new_token(TK_EOF, p, p);
    fill_tok(eof, filename, line, col, false, false);
    eof->next = NULL;
    cur->next = eof;
    convert_keywords(dummy.next);
    return dummy.next;
}

// Returns the contents of a given file.
static char *read_file(char *path) {
    FILE *fp;

    if (strcmp(path, "-") == 0) {
        // By convention, read from stdin if a given filename is "-".
        fp = stdin;
    } else {
        fp = fopen(path, "r");
        if (!fp) fatal("%s: %s", path, strerror(errno));
    }

    char *buf;
    size_t buflen;
    FILE *out = open_memstream(&buf, &buflen);

    // Read the entire file.
    while (1) {
        char buf2[4096];
        int n = fread(buf2, 1, sizeof(buf2), fp);
        if (n == 0) break;
        fwrite(buf2, 1, n, out);
    }

    if (fp != stdin) fclose(fp);

    // Make sure that the last line is properly terminated with '\n'.
    fflush(out);
    if (buflen == 0 || buf[buflen - 1] != '\n') fputc('\n', out);
    fputc('\0', out);
    fclose(out);

    cur_file = emalloc(sizeof(SrcFile));
    cur_file->filename = path;
    cur_file->content = buf;

    return buf;
}

Token *tokenize_file(char *path) { return tokenize(path, read_file(path)); }
