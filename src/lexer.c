#include "cxx.h"
#include "pow_table.h"

// Input file
static SrcFile *cur_file;
// A list of all input files.
static SrcFile **input_files;

static bool is_sol;
static bool is_leadingws;

// Create a new token.
static Token *new_token(uint32_t kind, char *start, char *end) {
    Token *tok = emalloc(sizeof(Token));
    tok->kind = kind;
    tok->loc = start;
    tok->len = end - start;
    tok->file = cur_file;
    tok->filename = cur_file->id;
    if (kind == TK_NL || kind == TK_WS || kind == TK_COMMENT) return tok;
    tok->is_sol = is_sol;
    tok->is_leadingws = is_leadingws;
    is_sol = is_leadingws = false;
    return tok;
}

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

static void check_escape_range(Token *tok, uint64_t val) {
    uint32_t prefix = tok->enc_prefix;
    switch (prefix) {
        case PREFIX_NONE:
        case PREFIX_u8:
            if (val > 0xFF) error(tok, "escape value out of range for unsigned char (0-255)");
            break;
        case PREFIX_u:
            if (val > 0xFFFF) error(tok, "escape value out of range for char16_t (0-65535)");
            break;
        case PREFIX_U:
            if (val > 0xFFFFFFFF) error(tok, "escape value out of range for char32_t (max 0xFFFFFFFF)");
            break;
        case PREFIX_L:
            if (val > 0xFFFFFFFF) error(tok, "escape value out of range for wchar_t");
            break;
    }
}

static void check_char_range(Token *tok, uint64_t code) {
    uint32_t prefix = tok->enc_prefix;
    switch (prefix) {
        case PREFIX_u8:
            if (code > 0x7F)
                error(tok, "UTF-8 character literal contains a character > 0x7F (requires multiple code units)");
            break;
        case PREFIX_u:
            if (code > 0xFFFF)
                error(tok, "UTF-16 character literal contains a character > 0xFFFF (requires surrogate pair)");
            break;
        case PREFIX_NONE:
        case PREFIX_U:
        case PREFIX_L:
            break;
    }
}

static int from_hex(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

uint32_t read_universal_char(char **new_pos, char *p, int ch) {
    int len = ch == 'u' ? 4 : 8;
    if (*p == '{') {
        char *q = ++p;
        while (isxdigit(*q)) q++;
        if (*q != '}') fatal("‘\\%c{’ not terminated with ‘}’", ch);
        len = q - p;
        if (len == 0) fatal("empty delimited universal character name");
    }
    uint32_t c = 0;
    for (int i = 0; i < len; i++) {
        if (!isxdigit(p[i])) fatal("invalid digit ‘%c’ in universal character name", p[i]);
        c = (c << 4) | from_hex(p[i]);
    }
    if (0xD800 <= c && c <= 0xDFFF) fatal("invalid universal character");
    if (c > 0x10FFFF) fatal("invalid universal character");
    if (*(p - 1) == '{') len++;
    *new_pos = p + len;
    return c;
}

// Replace \u or \U escape sequences with corresponding UTF-8 bytes.
static char *convert_universal_chars(char *p, int len) {
    char *end = p + len;
    char *buf = emalloc(len + 1);
    char *cur = buf;
    char *result = buf;

    while (p < end) {
        if (start_with(p, "\\u")) {
            uint32_t c = read_universal_char(&p, p + 2, 'u');
            cur += encode_utf8(cur, c);
        } else if (start_with(p, "\\U")) {
            uint32_t c = read_universal_char(&p, p + 2, 'U');
            cur += encode_utf8(cur, c);
        } else {
            *cur++ = *p++;
        }
    }

    *cur = '\0';
    return result;
}

// Read an identifier and returns the length of it.
// If p does not point to a valid identifier, 0 is returned.
static int read_ident(char *start) {
    char *p = start;
    uint32_t c;
    if (start_with(p, "\\u")) {
        c = read_universal_char(&p, p + 2, 'u');
        if (c <= 0x9F) fatal("universal character %.*s is not valid in an identifier", p - start, start);
    } else if (start_with(p, "\\U")) {
        c = read_universal_char(&p, p + 2, 'U');
        if (c <= 0x9F) fatal("universal character %.*s is not valid in an identifier", p - start, start);
    } else {
        bool success = false;
        c = decode_utf8(&p, p, &success);
        if (!success) fatal("invalid UTF-8 in identifier");
    }
    if (!is_ident1(c)) {
        if (c > 0x7F) fatal("invalid character %.*s in identifier", p - start, start);
        return 0;
    }

    while (1) {
        char *uc_start = p;
        if (start_with(p, "\\u")) {
            c = read_universal_char(&p, p + 2, 'u');
            if (c <= 0x9F) fatal("universal character %.*s is not valid in an identifier", p - uc_start, uc_start);
        } else if (start_with(p, "\\U")) {
            c = read_universal_char(&p, p + 2, 'U');
            if (c <= 0x9F) fatal("universal character %.*s is not valid in an identifier", p - uc_start, uc_start);
        } else {
            bool success = false;
            c = decode_utf8(&p, p, &success);
            if (!success) fatal("invalid UTF-8 in identifier");
        }
        if (!is_ident2(c)) {
            if (c > 0x7F) fatal("invalid character %.*s in identifier", p - uc_start, uc_start);
            return p - start - 1;
        }
    }
}

static uint64_t read_escaped_char(char **new_pos, char *p, char *end) {
    static struct {
        char ch;
        uint8_t val;
    } simple[] = {
        {'a', '\a'}, {'b', '\b'}, {'e', 27},    {'f', '\f'}, {'n', '\n'}, {'r', '\r'},
        {'t', '\t'}, {'v', '\v'}, {'\'', '\''}, {'"', '"'},  {'?', '?'},  {'\\', '\\'},
    };
    char c = *p;
    for (uint32_t i = 0; i < sizeof(simple) / sizeof(simple[0]); i++) {
        if (c == simple[i].ch) {
            *new_pos = p + 1;
            return simple[i].val;
        }
    }

    if ('0' <= c && c <= '7') {
        // Read an octal number.
        int c = *p++ - '0';
        if ('0' <= *p && *p <= '7') {
            c = (c << 3) + (*p++ - '0');
            if ('0' <= *p && *p <= '7') c = (c << 3) + (*p++ - '0');
        }
        *new_pos = p;
        return c;
    }

    if (c == 'o') {
        // Read an octal number.
        if (p[1] != '{') error_at(cur_file, p, "'\\o' not followed by '{'");
        p += 2;
        if (*p == '}') goto empty_error;
        uint64_t val = 0;
        while (p < end) {
            int c = *p;
            if (c == '}') break;
            if (c < '0' || c > '7') goto digit_error;
            val = (val << 3) + c - '0';
            p++;
        }
        if (*p != '}') goto miss_error;
        *new_pos = p + 1;
        return val;
    }

    if (c == 'x') {
        // Read a hexadecimal number.
        bool has_brace = false;
        if (p[1] == '{') {
            has_brace = true;
            p += 2;
        } else {
            p++;
        }
        if (!isxdigit(*p)) error_at(cur_file, p, "invalid hex escape sequence");

        uint64_t c = 0;
        for (; p < end && isxdigit(*p); p++) c = (c << 4) + from_hex(*p);
        if (has_brace && *p++ != '}') goto miss_error;
        *new_pos = p;
        return c;
    }

    if (c == 'u' || c == 'U') return read_universal_char(new_pos, p + 1, c);

    *new_pos = p + 1;
    return (unsigned char)*p;

empty_error:
    error_at(cur_file, p, "empty delimited escape sequence");
miss_error:
    error_at(cur_file, p, "‘\\%c{’ not terminated with ‘}’", c);
digit_error:
    error_at(cur_file, p, "invalid digit ‘%c’ in escape sequence", *p);
    return 0;
}

static void convert_utf8_str_literal(Token *tok, char *str, Type *ty) {
    char *p = str;
    while (*p++ != '"');
    char *end = str + strlen(str) - 1;
    char *buf = emalloc(end - p);
    int len = 0;

    while (p < end) {
        if (*p == '\\') {
            buf[len++] = read_escaped_char(&p, p + 1, end);
        } else {
            buf[len++] = *p++;
        }
    }

    tok->ty = array_of(ty, len + 1);
    tok->id = intern(buf, len);
}

// Read a UTF-8-encoded string literal and transcode it in UTF-16.
//
// UTF-16 is yet another variable-width encoding for Unicode. Code
// points smaller than U+10000 are encoded in 2 bytes. Code points
// equal to or larger than that are encoded in 4 bytes. Each 2 bytes
// in the 4 byte sequence is called "surrogate", and a 4 byte sequence
// is called a "surrogate pair".
static void convert_utf16_str_literal(Token *tok, char *str, Type *ty) {
    char *p = str;
    while (*p++ != '"');
    char *end = str + strlen(str) - 1;
    uint16_t *buf = emalloc(4 * (end - p));
    int len = 0;

    while (p < end) {
        if (*p == '\\') {
            buf[len++] = read_escaped_char(&p, p + 1, end);
            continue;
        }

        bool ignored = false;
        uint32_t c = decode_utf8(&p, p, &ignored);

        if (c < 0x10000) {
            // Encode a code point in 2 bytes.
            buf[len++] = c;
        } else {
            // Encode a code point in 4 bytes.
            c -= 0x10000;
            buf[len++] = 0xD800 + ((c >> 10) & 0x3FF);
            buf[len++] = 0xDC00 + (c & 0x3FF);
        }
    }

    tok->ty = array_of(ty, len + 1);
    tok->id = intern((char *)buf, len * 2);
}

// Read a UTF-8-encoded string literal and transcode it in UTF-32.
//
// UTF-32 is a fixed-width encoding for Unicode. Each code point is
// encoded in 4 bytes.
static void convert_utf32_str_literal(Token *tok, char *str, Type *ty) {
    char *p = str;
    while (*p++ != '"');
    char *end = str + strlen(str) - 1;
    uint32_t *buf = emalloc(4 * (end - p));
    int len = 0;
    bool ignored = false;

    while (p < end) {
        if (*p == '\\')
            buf[len++] = read_escaped_char(&p, p + 1, end);
        else
            buf[len++] = decode_utf8(&p, p, &ignored);
    }

    tok->ty = array_of(ty, len + 1);
    tok->id = intern((char *)buf, len * 4);
}

static void convert_str_literal(Token *tok) {
    char *str = convert_universal_chars(tok->loc, tok->len);
    uint32_t prefix = tok->enc_prefix;
    switch (prefix) {
        case PREFIX_NONE:
            convert_utf8_str_literal(tok, str, ty_char);
            break;
        case PREFIX_u8:
            convert_utf8_str_literal(tok, str, ty_uchar);
            break;
        case PREFIX_u:
            convert_utf16_str_literal(tok, str, ty_ushort);
            break;
        case PREFIX_U:
            convert_utf32_str_literal(tok, str, ty_uint);
            break;
        case PREFIX_L:
            convert_utf32_str_literal(tok, str, ty_int);
            break;
        default:
            break;
    }
}

static Token *read_string_literal(char *start, char *quote, uint32_t prefix) {
    Token *tok;
    char *p = quote + 1;
    if (*p == '\0') goto error;

    while (1) {
        if (*p == '\\')
            p += 2;
        else if (*p == '\0' || *p == '\n')
            goto error;
        else if (*p == '"')
            break;
        else
            p++;
    }

    tok = new_token(TK_STRLIT, start, p + 1);
    tok->enc_prefix = prefix;
    convert_str_literal(tok);
    return tok;
error:
    tok = new_token(TK_ERR, start, p);
    tok->msg = "missing terminating \" character";
    return tok;
}

static void convert_char_literal(Token *tok) {
    char *p = tok->loc;
    while (*p++ != '\'');
    char *end = tok->loc + tok->len - 1;

    uint32_t val = 0;
    while (p < end) {
        if (*p == '\\') {
            p++;
            uint64_t ch = read_escaped_char(&p, p, end);
            check_escape_range(tok, ch);
            val = (val << 8) + ch;
        } else {
            bool success = false;
            uint64_t ch = decode_utf8(&p, p, &success);
            if (!success) error(tok, "invalid UTF-8 in char literal");
            check_char_range(tok, ch);
            val = (val << 8) + ch;
        }
    }
    tok->kind = TK_NUM;
    tok->val = val;

    uint32_t prefix = tok->enc_prefix;
    tok->ty = prefix == PREFIX_NONE ? ty_int
              : prefix == PREFIX_L  ? ty_int
              : prefix == PREFIX_U  ? ty_uint
              : prefix == PREFIX_u  ? ty_ushort
                                    : ty_uchar;
}

static Token *read_char_literal(char *start, char *quote, uint32_t prefix) {
    Token *tok;
    char *p = quote + 1;
    if (*p == '\0') goto error;

    while (1) {
        if (*p == '\\')
            p += 2;
        else if (*p == '\0' || *p == '\n')
            goto error;
        else if (*p == '\'')
            break;
        else
            p++;
    }

    if (p == quote + 1) {
        tok = new_token(TK_ERR, start, p + 1);
        tok->msg = "empty character constant";
        return tok;
    }

    tok = new_token(TK_CHARLIT, start, p + 1);
    tok->enc_prefix = prefix;
    return tok;
error:
    tok = new_token(TK_ERR, start, quote + 1);
    tok->msg = "unclosed char literal";
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

static void convert_pp_num(Token *t) {
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
            if (!is_valid_digit(*text, base)) error(t, "invalid suffix ‘%.*s’ on integer constant", end - text, text);
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

void convert_ppnumber(Token *tok) {
    while (tok->kind != TK_EOF) {
        if (tok->kind == TK_ERR) error(tok, tok->msg);
        if (tok->kind == TK_PPNUM) convert_pp_num(tok);
        if (tok->kind == TK_CHARLIT) convert_char_literal(tok);
        tok = tok->next;
    }
}

void convert_keywords(Token *tok) {
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
        tok = tok->next;
    }
}

// Tokenize a given string and returns new tokens.
Token *tokenize(SrcFile *file) {
    cur_file = file;
    char *p = file->contents;

    is_sol = true;
    is_leadingws = false;

    Token dummy = {}, *cur = &dummy;
    Token *tok;

    while (*p) {
        // Read line comments.
        if (start_with(p, "//")) {
            char *q = p + 2;
            while (*q != '\n') q++;
            tok = new_token(TK_COMMENT, p, q);
            is_leadingws = true;
            cur = cur->next = tok;
            p = q;
            continue;
        }

        // Read block comments.
        if (start_with(p, "/*")) {
            char *q = strstr(p + 2, "*/");
            if (!q) {
                tok = new_token(TK_ERR, p, p + 1);
                tok->msg = "unterminated /* comment";
                cur = cur->next = tok;
                goto end;
            }
            tok = new_token(TK_COMMENT, p, q + 2);
            is_leadingws = true;
            cur = cur->next = tok;
            p = q + 2;
            continue;
        }

        // Read white space
        if (isspace(*p)) {
            char *q = p;
            int nl = 0;
            while (isspace(*q)) {
                if (*q++ == '\n') {
                    nl++;
                    continue;
                }
            }
            tok = new_token(TK_WS, p, q);
            if (nl) {
                tok->kind = TK_NL;
                is_sol = true;
            }
            if (*(q - 1) != '\n') is_leadingws = true;
            cur = cur->next = tok;
            p = q;
            continue;
        }

        // Numeric literal
        if (isdigit(*p) || (*p == '.' && isdigit(p[1]))) {
            tok = read_int_literal(p);
            cur = cur->next = tok;
            p += tok->len;
            continue;
        }

        // String literal
        if (*p == '"') {
            tok = read_string_literal(p, p, PREFIX_NONE);
            cur = cur->next = tok;
            p += tok->len;
            continue;
        }

        // Character literal
        if (start_with(p, "u8\"")) {
            tok = read_string_literal(p, p + 2, PREFIX_u8);
            cur = cur->next = tok;
            p += tok->len;
            continue;
        }

        // UTF-16 string literal
        if (start_with(p, "u\"")) {
            tok = read_string_literal(p, p + 1, PREFIX_u);
            cur = cur->next = tok;
            p += tok->len;
            continue;
        }

        // UTF-32 string literal
        if (start_with(p, "U\"")) {
            tok = read_string_literal(p, p + 1, PREFIX_U);
            cur = cur->next = tok;
            p += tok->len;
            continue;
        }

        // Wide string literal
        if (start_with(p, "L\"")) {
            tok = read_string_literal(p, p + 1, PREFIX_L);
            cur = cur->next = tok;
            p += tok->len;
            continue;
        }

        // Character literal
        if (*p == '\'') {
            tok = read_char_literal(p, p, PREFIX_NONE);
            cur = cur->next = tok;
            p += tok->len;
            continue;
        }

        // UTF-8 character literal
        if (start_with(p, "u8'")) {
            tok = read_char_literal(p, p + 2, PREFIX_u8);
            cur = cur->next = tok;
            p += tok->len;
            continue;
        }

        // UTF-16 character literal
        if (start_with(p, "u'")) {
            tok = read_char_literal(p, p + 1, PREFIX_u);
            cur = cur->next = tok;
            p += tok->len;
            continue;
        }

        // UTF-32 character literal
        if (start_with(p, "U'")) {
            tok = read_char_literal(p, p + 1, PREFIX_U);
            cur = cur->next = tok;
            p += tok->len;
            continue;
        }

        // Wide character literal
        if (start_with(p, "L'")) {
            tok = read_char_literal(p, p + 1, PREFIX_L);
            cur = cur->next = tok;
            p += tok->len;
            continue;
        }

        // Identifier or keyword
        int ident_len = read_ident(p);
        if (ident_len) {
            tok = new_token(TK_IDENT, p, p + ident_len);
            char *buf = convert_universal_chars(p, ident_len);
            tok->id = intern(buf, strlen(buf));
            cur = cur->next = tok;
            p += cur->len;
            continue;
        }

        // Punctuator
        if (*p <= 0x7F && ispunct(*p) && *p != '`' && *p != '@') {
            tok = new_token(TK_PUNCT, p, p);
            tok->len = read_punct(p, tok);
            cur = cur->next = tok;
            p += tok->len;
            continue;
        }

        // Other char
        tok = new_token(TK_OTHER, p, p + 1);
        cur = cur->next = tok;
        p += tok->len;
        continue;
    }
end:
    Token *eof = new_token(TK_EOF, p, p);
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
    fflush(out);

    // Make sure that the last line is properly terminated with '\n'.
    if (buflen > 0 && buf[buflen - 1] == '\\') fatal("stray ‘\\’ at end of file");
    if (buflen == 0 || buf[buflen - 1] != '\n') fputc('\n', out);
    fputc('\0', out);
    fclose(out);

    return buf;
}

SrcFile **get_input_files(void) { return input_files; }

SrcFile *new_file(char *name, int file_no, char *contents) {
    SrcFile *file = emalloc(sizeof(SrcFile));
    file->name = name;
    file->id = intern(name, strlen(name));
    file->file_no = file_no;
    file->contents = contents;
    return file;
}

// Translation phases 2.
// Removes backslashes followed by a newline.
static void remove_backslash_newline(char *p) {
    int i = 0, j = 0;
    int n = 0;

    // We want to keep the number of newline characters so that
    // the logical line number matches the physical one.
    // This counter maintain the number of newlines we have removed.
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

static void build_line_offsets(SrcFile *f) {
    if (!f || f->num_lines > 0) return;

    int count = 1;
    char *p = f->contents;
    while (*p)
        if (*p++ == '\n') count++;

    f->size = p - f->contents;
    f->num_lines = count;

    f->line_offsets = emalloc(sizeof(uint32_t) * count);

    int idx = 1;
    f->line_offsets[0] = 0;
    p = f->contents;
    while (*p)
        if (*p++ == '\n') f->line_offsets[idx++] = p - f->contents;
}

void get_location(SrcFile *f, char *loc, int *out_line, int *out_col) {
    if (!f || !loc) {
        *out_line = 1;
        *out_col = 1;
        return;
    }

    build_line_offsets(f);

    uint32_t offset = (uint32_t)(loc - f->contents);

    if (offset > f->size) {
        offset = f->size;
        loc = f->contents + f->size;
    }

    int lo = 0, hi = f->num_lines - 1;
    int idx = 0;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (f->line_offsets[mid] <= offset) {
            idx = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    *out_line = idx + 1;

    char *line_start = f->contents + f->line_offsets[idx];
    *out_col = (int)(loc - line_start) + 1;
}

Token *tokenize_file(char *path) {
    char *p = read_file(path);
    if (!p) return NULL;

    // UTF-8 texts may start with a 3-byte "BOM" marker sequence.
    // If exists, just skip them because they are useless bytes.
    if (!memcmp(p, "\xef\xbb\xbf", 3)) p += 3;

    canonicalize_newline(p);
    remove_backslash_newline(p);

    static int file_no = 0;
    SrcFile *file = new_file(path, file_no + 1, p);

    if (!input_files)
        input_files = vnew(2, sizeof(SrcFile *));
    else
        input_files = vgrow(input_files, file_no + 1);

    input_files[file_no++] = file;

    return tokenize(file);
}
