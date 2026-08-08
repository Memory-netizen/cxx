#include "cxx.h"
#include "pow_table.h"

// Input file
SrcFile *cur_file = NULL;
// A list of all input files.
static SrcFile **input_files;

static int line;
static int col;

// Attempt to match the given token type
// If matched, consume the token and return true;
// otherwise, leave the token unconsumed and return false.
bool match(Token **rest, Token *tok, uint32_t kind) {
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
Token *skip(Token *tok, uint32_t kind) {
    if (tok->kind != kind) error(tok, "expected ‘%s’ before ‘%.*s’", expect[kind], tok->len, tok->loc);
    return tok->next;
}

// Compare if the pending matching string matches the target string
static inline bool start_with(char *p, char *q) { return strncmp(p, q, strlen(q)) == 0; }

// Returns true if c is ident_start.
static inline bool is_ident0(char c) { return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_'; }

// Returns true if c is ident_continue.
static inline bool is_ident1(char c) { return is_ident0(c) || ('0' <= c && c <= '9'); }

static int read_punct(char *p, Token *tok) {
    static struct {
        char *punct;
        uint32_t type;
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
            tok->kind = punct[i].type;
            return strlen(punct[i].punct);
        }

    return 0;
}

// Create a new token.
static Token *new_token(uint32_t kind, char *start, char *end) {
    Token *tok = emalloc(sizeof(Token));
    tok->kind = kind;
    tok->file = cur_file;
    tok->loc = start;
    tok->len = end - start;
    return tok;
}

// Fill position metadata into a token.
static void fill_tok(Token *tok, int line, int col, bool is_sol, bool is_leadingws) {
    tok->file = cur_file;
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
        if (!isxdigit(*p)) error_at(line, p, "invalid hex escape sequence");

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
        if (*p == '\n' || *p == '\0') error_at(line, start, "missing terminating \" character");
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

static Token *read_char_literal(char *start, char *quote) {
    char *p = quote + 1;
    if (*p == '\0') error_at(line, start, "unclosed char literal");
    int c;
    if (*p == '\\')
        c = read_escaped_char(&p, p + 1);
    else
        c = (unsigned char)*p++;

    char *end = strchr(p, '\'');
    if (!end) error_at(line, p, "unclosed char literal");

    Token *tok = new_token(TK_NUM, start, end + 1);
    tok->val = c;
    return tok;
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

    Token *tok = new_token(TK_PPNUM, start, p);
    return tok;
}

static double fast_pow10(int exp) {
    if (exp < -323) return 0.0;
    if (exp > 308) return HUGE_VAL;
    return pow10_table[exp + 323];
}

static double fast_ldexp(double x, int exp) {
    if (exp < -1074) return 0.0;
    if (exp > 1023) return HUGE_VAL * x;
    return x * pow2_table[exp + 1074];
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
    SUF_UNSIGNED = 0x001,
    SUF_LONG = 0x002,
    SUF_LLONG = 0x004,
    SUF_FLOAT = 0x08,
    SUF_LDOUBLE = 0x010,
    SUF_BITINT = 0x020,
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
    t->kind = TK_NUM;
    char *text = t->loc;
    char *end = t->loc + t->len;
    char first_ch = *text;

    int base = 10;
    char *clean = emalloc((t->len + 1) * sizeof(char));
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
                error_at(line, text, "invalid suffix ‘%.*s’ on integer constant", end - text, text);
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

    t->ty = flags == SUF_LDOUBLE ? ty_ldouble : ty_double;
    t->ty = flags == SUF_FLOAT ? ty_float : ty_double;

    limit = pos_exp ? pos_exp : ci;

    double divisor = base;
    while (pos < limit) {
        frac_part += from_hex(clean[pos++]) / divisor;
        divisor *= base;
    }

    t->fval = int_part + frac_part;

    if (pos_exp) {
        while (pos < ci) exp = exp * 10 + clean[pos++] - '0';

        exp *= sign;
        t->fval = base == 16 ? fast_ldexp(t->fval, exp) : t->fval * fast_pow10(exp);
    }

    // Demote literal value from double to float
    if (t->ty->kind == TY_FLOAT) t->fval = (float)t->fval;

    return;
error:
    text--;
    error(t, "invalid suffix ‘%.*s’ on constant", end - text, text);
}

// Advance col, tracking newlines.
static inline void advance_col(int *line, int *col, int n) {
    *col += n;
    (void)line;
}

void convert_pptoken(Token *tok) {
    static struct {
        char *keyword;
        uint32_t id;
        uint32_t type;
    } kw[] = {
        {"_Alignas", 0, TK_ALIGNAS},
        {"_Alignof", 0, TK_ALIGNOF},
        {"_Atomic", 0, TK_ATOMIC},
        {"_BitInt", 0, TK_BITINT},
        {"_Bool", 0, TK_BOOL},
        {"_Countof", 0, TK_COUNTOF},
        {"_Generic", 0, TK_GENERIC},
        {"_Noreturn", 0, TK_NORETURN},
        {"_Static_assert", 0, TK_STATIC_ASSERT},
        {"_Thread_local", 0, TK_THREAD},
        {"__asm", 0, TK_ASM},
        {"__asm__", 0, TK_ASM},
        {"__attribute__", 0, TK_ATTR},
        {"__restrict", 0, TK_RESTRICT},
        {"__restrict__", 0, TK_RESTRICT},
        {"__thread", 0, TK_THREAD},
        {"alignas", 0, TK_ALIGNAS},
        {"alignof", 0, TK_ALIGNOF},
        {"asm", 0, TK_ASM},
        {"auto", 0, TK_AUTO},
        {"bool", 0, TK_BOOL},
        {"break", 0, TK_BREAK},
        {"case", 0, TK_CASE},
        {"char", 0, TK_CHAR},
        {"const", 0, TK_CONST},
        {"constexpr", 0, TK_CONSTEXPR},
        {"continue", 0, TK_CONTINUE},
        {"default", 0, TK_DEFAULT},
        {"do", 0, TK_DO},
        {"double", 0, TK_DOUBLE},
        {"else", 0, TK_ELSE},
        {"enum", 0, TK_ENUM},
        {"extern", 0, TK_EXTERN},
        {"false", 0, TK_FALSE},
        {"float", 0, TK_FLOAT},
        {"for", 0, TK_FOR},
        {"goto", 0, TK_GOTO},
        {"if", 0, TK_IF},
        {"inline", 0, TK_INLINE},
        {"int", 0, TK_INT},
        {"long", 0, TK_LONG},
        {"nullptr", 0, TK_NULLPTR},
        {"register", 0, TK_REGISTER},
        {"restrict", 0, TK_RESTRICT},
        {"return", 0, TK_RETURN},
        {"short", 0, TK_SHORT},
        {"signed", 0, TK_SIGNED},
        {"sizeof", 0, TK_SIZEOF},
        {"static", 0, TK_STATIC},
        {"static_assert", 0, TK_STATIC_ASSERT},
        {"struct", 0, TK_STRUCT},
        {"switch", 0, TK_SWITCH},
        {"thread_local", 0, TK_THREAD},
        {"true", 0, TK_TRUE},
        {"typedef", 0, TK_TYPEDEF},
        {"typeof", 0, TK_TYPEOF},
        {"typeof_unqual", 0, TK_TYPEOF_U},
        {"union", 0, TK_UNION},
        {"unsigned", 0, TK_UNSIGNED},
        {"void", 0, TK_VOID},
        {"volatile", 0, TK_VOLATILE},
        {"while", 0, TK_WHILE},
    };
    if (!kw[0].id) {
        for (size_t i = 0; i < sizeof(kw) / sizeof(kw[0]); ++i) kw[i].id = intern(kw[i].keyword, strlen(kw[i].keyword));
    }
    while (tok->kind != TK_EOF) {
        if (tok->kind == TK_IDENT) {
            for (size_t i = 0; i < sizeof(kw) / sizeof(kw[0]); ++i)
                if (tok->id == kw[i].id) {
                    tok->kind = kw[i].type;
                    break;
                }
        }
        if (tok->kind == TK_PPNUM) convert_pp_number(tok);
        tok = tok->next;
    }
}

// Tokenize a given string and returns new tokens.
Token *tokenize(SrcFile *file, int line_no, int col_no) {
    cur_file = file;
    char *p = file->contents;

    line = line_no;
    col = col_no;

    Token dummy = {}, *cur = &dummy;

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
                if (!q) error_at(line, p, "unterminated /* comment");
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
        if (isdigit(*p) || (*p == '.' && isdigit(p[1]))) {
            tok = read_int_literal(p);
            fill_tok(tok, line, col, is_sol, is_leadingws);
            cur = cur->next = tok;
            p += tok->len;
            advance_col(&line, &col, tok->len);
            continue;
        }

        // String literal
        if (*p == '"') {
            tok = read_string_literal(p);
            fill_tok(tok, line, col, is_sol, is_leadingws);
            cur = cur->next = tok;
            p += tok->len;
            advance_col(&line, &col, tok->len);
            continue;
        }

        // Character literal
        if (*p == '\'') {
            tok = read_char_literal(p, p);
            fill_tok(tok, line, col, is_sol, is_leadingws);
            cur = cur->next = tok;
            p += tok->len;
            advance_col(&line, &col, tok->len);
            continue;
        }

        // Wide character literal
        if (start_with(p, "L'")) {
            tok = read_char_literal(p, p + 1);
            fill_tok(tok, line, col, is_sol, is_leadingws);
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
            fill_tok(tok, line, col, is_sol, is_leadingws);
            cur = cur->next = tok;
            advance_col(&line, &col, tok->len);
            continue;
        }

        // Punctuator
        if (ispunct(*p) && ((*p != '`' && *p != '@' && *p != '$'))) {
            tok = new_token(TK_PUNCT, p, p);
            tok->len = read_punct(p, tok);
            fill_tok(tok, line, col, is_sol, is_leadingws);
            cur = cur->next = tok;
            p += tok->len;
            advance_col(&line, &col, tok->len);
            continue;
        }

        // Other char
        tok = new_token(TK_OTHER, p, p + 1);
        fill_tok(tok, line, col, is_sol, is_leadingws);
        cur = cur->next = tok;
        p += tok->len;
        advance_col(&line, &col, tok->len);
        continue;
    }

    Token *eof = new_token(TK_EOF, p, p);
    fill_tok(eof, line, col, false, false);
    cur->next = eof;
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
        if (!fp) return NULL;
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

    return buf;
}

SrcFile **get_input_files(void) { return input_files; }

SrcFile *new_file(char *name, int file_no, char *contents) {
    SrcFile *file = emalloc(sizeof(SrcFile));
    file->name = name;
    file->file_no = file_no;
    file->contents = contents;
    return file;
}

// Translation phases 2.
// Removes backslashes followed by a newline.
static void remove_backslash_newline(char *p) {
    int i = 0, j = 0;
    int n = 0;

    while (p[i]) {
        if (p[i] == '\\' && p[i + 1] == '\n') {
            i += 2;
            n++;
        } else if (p[i] == '\n') {
            p[j++] = p[i++];
            for (; n > 0; n--) p[j++] = '\n';
        } else {
            p[j++] = p[i++];
        }
    }

    for (; n > 0; n--) p[j++] = '\n';
    p[j] = '\0';
}

// Replaces \r or \r\n with \n.
static void canonicalize_newline(char *p) {
    int i = 0, j = 0;

    while (p[i]) {
        if (p[i] == '\r' && p[i + 1] == '\n') {
            i += 2;
            p[j++] = '\n';
        } else if (p[i] == '\r') {
            i++;
            p[j++] = '\n';
        } else {
            p[j++] = p[i++];
        }
    }

    p[j] = '\0';
}

Token *tokenize_file(char *path) {
    char *p = read_file(path);
    if (!p) return NULL;

    canonicalize_newline(p);
    remove_backslash_newline(p);

    static int file_no = 0;
    SrcFile *file = new_file(path, file_no + 1, p);

    if (!input_files)
        input_files = vnew(2, sizeof(SrcFile *));
    else
        input_files = vgrow(input_files, file_no + 1);

    input_files[file_no++] = file;

    return tokenize(file, 1, 1);
}
