#include "cxx.h"
#include <sys/mman.h>

// Input file
static SrcFile *cur_file;
// A list of all input files.
static SrcFile **input_files;

static bool is_sol;
static bool is_leadingws;

// Prototype: scanning a region that produces nothing. The same scanner runs,
// but no arena slot is taken and no identifier is interned -- the token that
// comes back is one reused scratch object, so `len` still says how far to
// advance. See lex_scan().
static bool lex_skip;
bool lex_lazy;
static Token lex_scratch;

// Create a new token.
static Token *new_token(uint32_t kind, char *start, char *end) {
    Token *tok;
    if (lex_skip) {
        tok = &lex_scratch;
        memset(tok, 0, sizeof(*tok));
    } else {
        tok = ALLOC_TOK(Token);
    }
    tok->kind = kind;
    tok->loc = (uint32_t)(start - cur_file->contents);
    tok->len = end - start;
    tok->file_uid = (cur_file)->uid;
    if (kind == TK_NL || kind == TK_WS || kind == TK_COMMENT) return tok;
    tok->is_sol = is_sol;
    tok->is_leadingws = is_leadingws;
    is_sol = is_leadingws = false;
    return tok;
}

// Attempt to match the given token type
// If matched, consume the token and return true;
// otherwise, leave the token unconsumed and return false.
// A speculative parse accepts by moving the caller's position and rejects by leaving
// it. Those two acts, named -- the call sites stop spelling out the mechanism, and when
// the reader becomes an index into the parallel arrays only these two change.
void parse_commit(Token **rest, Token *next) { *rest = next; }
void parse_rollback(void) {}
// The next token after this one. Today that is the chain link; when the reader is an
// index into the parallel arrays it will be the next index. Callers ask the question
// rather than reading the field, so only this function has to change.
// The next token after this one. Today that is the chain link; when the reader is an
// index into the parallel arrays it will be the next index. Callers ask the question
// rather than reading the field, so only this function has to change. (It is called
// tok_next because `next` is already a name here, and ->next belongs to 17 structs:
// Scope, Sym, Member and others -- so the rewrite names the receiver, tok.)
// The one way to the next token -- the parser looks ahead with it everywhere -- and
// therefore the one place a pulled stream has to be extended. Running off the end of
// what has been produced (a NULL link) means "there is more": the stream really ends
// at the EOF token the lexer left in the input. With no consumer armed this is the
// same load it always was.
Token *tok_next(Token *tok) {
    if (!tok) return NULL;
    if (!tok->next) pp_pull_more(tok);
    return tok->next;
}
bool match(Token **rest, Token *tok, uint32_t kind) {
    if (tok->kind == kind) {
        parse_commit(rest, tok_next(tok));
        return true;
    }
    *rest = tok;
    return false;
}

// The token kinds `skip` may be called with, for error messages.
static char *expect[TK_NKIND] = {
    [TK_ELLIPSIS] = "...",
    [TK_WHILE] = "while",
};

// Textual representation of a token kind, for error messages.
// A single-byte punctuator represents itself.
static char *kind_str(uint32_t kind) {
    static char buf[2];

    if (kind < 128) {
        buf[0] = kind;
        return buf;
    }
    return expect[kind];
}

// Ensure that the current token is `kind`.
Token *skip(Token *tok, uint32_t kind) {
    if (tok->kind != kind) error(tok, "expected ‘%s’ before ‘%.*s’", kind_str(kind), tok->len, tok_text(tok));
    return tok_next(tok);
}

// Read a punctuator at `p`, storing its kind in `*kind`.
// Returns the length of the punctuator, or 0 if `p` does not begin with one.
//
// Two levels of branch -- the first byte, then the second -- instead of the
// table that was scanned with start_with(), which cost a call per entry and a
// character loop inside it (perf.md 8oz).
int read_punct(char *p, uint32_t *kind) {
    unsigned char c0 = (unsigned char)p[0], c1 = (unsigned char)p[1];

    switch (c0) {
        case '<':
            if (c1 == '<') {
                if (p[2] == '=') { *kind = TK_LEFTAS; return 3; }
                *kind = TK_LEFT;
                return 2;
            }
            if (c1 == '=') { *kind = TK_LE; return 2; }
            if (c1 == ':') { *kind = TK_LBRACKET; return 2; }
            if (c1 == '%') { *kind = TK_LBRACE; return 2; }
            break;
        case '>':
            if (c1 == '>') {
                if (p[2] == '=') { *kind = TK_RIGHTAS; return 3; }
                *kind = TK_RIGHT;
                return 2;
            }
            if (c1 == '=') { *kind = TK_GE; return 2; }
            break;
        case '.':
            if (c1 == '.' && p[2] == '.') { *kind = TK_ELLIPSIS; return 3; }
            break;
        case '%':
            if (c1 == ':') {
                if (p[2] == '%' && p[3] == ':') { *kind = TK_HASHHASH; return 4; }
                *kind = TK_HASH;
                return 2;
            }
            if (c1 == '>') { *kind = TK_RBRACE; return 2; }
            if (c1 == '=') { *kind = TK_MODAS; return 2; }
            break;
        case '+':
            if (c1 == '+') { *kind = TK_INC; return 2; }
            if (c1 == '=') { *kind = TK_ADDAS; return 2; }
            break;
        case '-':
            if (c1 == '-') { *kind = TK_DEC; return 2; }
            if (c1 == '=') { *kind = TK_SUBAS; return 2; }
            if (c1 == '>') { *kind = TK_ARROW; return 2; }
            break;
        case '*':
            if (c1 == '=') { *kind = TK_MULAS; return 2; }
            break;
        case '/':
            if (c1 == '=') { *kind = TK_DIVAS; return 2; }
            break;
        case '&':
            if (c1 == '&') { *kind = TK_AND; return 2; }
            if (c1 == '=') { *kind = TK_ANDAS; return 2; }
            break;
        case '^':
            if (c1 == '=') { *kind = TK_XORAS; return 2; }
            break;
        case '|':
            if (c1 == '|') { *kind = TK_OR; return 2; }
            if (c1 == '=') { *kind = TK_ORAS; return 2; }
            break;
        case '=':
            if (c1 == '=') { *kind = TK_EQ; return 2; }
            break;
        case '!':
            if (c1 == '=') { *kind = TK_NE; return 2; }
            break;
        case ':':
            if (c1 == ':') { *kind = TK_COLONCOLON; return 2; }
            if (c1 == '>') { *kind = TK_RBRACKET; return 2; }
            break;
        case '#':
            if (c1 == '#') { *kind = TK_HASHHASH; return 2; }
            break;
        default:
            break;
    }

    if (c0 < 0x80 && ispunct(c0)) {
        *kind = c0;
        return 1;
    }

    return 0;
}

// ---- Translation phase 2, read instead of written (perf.md 8or). ----

static bool has_splice(char *p, size_t len) {
    char *end = p + len;
    while ((p = memchr(p, '\\', (size_t)(end - p)))) {
        if (is_splice(p)) return true;
        p++;
    }
    return false;
}

char *splice_free(char *p, size_t len, size_t *out_len) {
    if (!has_splice(p, len)) {
        if (out_len) *out_len = len;
        return p;
    }
    char *end = p + len;
    size_t n = len;
    for (char *q = p; (q = memchr(q, '\\', (size_t)(end - q)));) {
        if (is_splice(q)) { n -= (size_t)(splice_end(q) - q); q = splice_end(q); }
        else q++;
    }

    char *buf = emalloc(n + 1);
    char *w = buf;
    while (p < end) {
        if (is_splice(p)) { p = splice_end(p); continue; }
        *w++ = *p++;
    }
    *w = '\0';
    if (out_len) *out_len = n;
    return buf;
}

char *tok_logical(Token *tok, size_t *out_len) {
    return splice_free(tok_text(tok), tok->len, out_len);
}

unsigned char last_logical_byte(Token *tok) {
    char *p = tok_text(tok), *end = p + tok->len;
    unsigned char c = 0;
    while (p < end) {
        if (is_splice(p)) { p = splice_end(p); continue; }
        c = (unsigned char)*p++;
    }
    return c;
}

void emit_logical(FILE *out, char *p, size_t len) {
    char *end = p + len;
    while (p < end) {
        if (is_splice(p)) { p = splice_end(p); continue; }
        char *q = p;
        while (q < end && !is_splice(q)) q++;
        fwrite(p, 1, (size_t)(q - p), out);
        p = q;
    }
}

// A punctuator a splice splits is still one punctuator, and read_punct() matches
// contiguous bytes: read it out of a window with the splices taken out. `*raw` is
// left after the same number of logical bytes in the file.
static int read_punct_logical(char *p, uint32_t *kind, char **raw) {
    char buf[16];
    size_t n = 0;
    char *q = p;
    while (n < sizeof(buf) - 1 && *q && !is_ascii_space((unsigned char)*q) && *q != '"' && *q != '\'') {
        if (is_splice(q)) { q = splice_end(q); continue; }
        buf[n++] = *q++;
    }
    buf[n] = '\0';

    int len = read_punct(buf, kind);
    if (!len) return 0;

    char *r = p;
    for (int i = 0; i < len;) {
        if (is_splice(r)) { r = splice_end(r); continue; }
        r++;
        i++;
    }
    *raw = r;
    return len;
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
        case PREFIX_L: {
            uint64_t wmax = (1ULL << (T.ty_wchar->size * 8)) - 1;
            if (val > wmax) error(tok, "escape value out of range for wchar_t");
            break;
        }
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
            break;
        case PREFIX_L:
            if (T.ty_wchar->size == 2 && code > 0xFFFF)
                error(tok, "wide character literal requires more than one wchar_t code unit");
            break;
    }
}

static int from_hex(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

uint32_t read_universal_char(char **new_pos, char *p, int ch, SrcFile *file) {
    int len = ch == 'u' ? 4 : 8;
    if (*p == '{') {
        char *q = ++p;
        while (is_ascii_xdigit((unsigned char)*q)) q++;
        if (*q != '}') error_at(file, (uint32_t)(p - 1 - file->contents), "‘\\%c{’ not terminated with ‘}’", ch);
        len = q - p;
        if (len == 0) error_at(file, (uint32_t)(p - 1 - file->contents), "empty delimited universal character name");
    }
    uint32_t c = 0;
    for (int i = 0; i < len; i++) {
        if (!is_ascii_xdigit((unsigned char)p[i]))
            error_at(file, (uint32_t)(p + i - file->contents), "invalid digit ‘%c’ in universal character name", p[i]);
        c = (c << 4) | from_hex(p[i]);
    }
    if (0xD800 <= c && c <= 0xDFFF) error_at(file, (uint32_t)(p - 1 - file->contents), "invalid universal character");
    if (c > 0x10FFFF) error_at(file, (uint32_t)(p - 1 - file->contents), "invalid universal character");
    if (*(p - 1) == '{') len++;
    *new_pos = p + len;
    return c;
}

// Whether this span actually holds a universal character name. The conversion
// allocates a buffer and copies the whole token, and it only ever *changes* a \u or
// \U -- so a span with none is itself the answer, and interning can take it
// straight (intern copies len bytes and terminates its own copy, so it never needed
// a C string). The walk has to make the same decisions as the conversion: a
// backslash followed by anything else is two characters, which is what keeps an
// escaped backslash from being read as the start of a name.
static bool has_ucn(char *p, int len) {
    char *end = p + len;
    while ((p = memchr(p, '\\', (size_t)(end - p)))) {
        if (p + 1 >= end) return false;
        if (p[1] == 'u' || p[1] == 'U') return true;
        p += 2;
    }
    return false;
}

// Replace \u or \U escape sequences with corresponding UTF-8 bytes.
static char *convert_universal_chars(char *p, int len, SrcFile *file) {
    char *end = p + len;
    char *buf = emalloc(len + 1);
    char *cur = buf;

    while (p < end) {
        if (*p == '\\' && (p[1] == 'u' || p[1] == 'U')) {
            uint32_t c = read_universal_char(&p, p + 2, p[1], file);
            cur += encode_utf8(cur, c);
        } else if (*p == '\\' && p + 1 < end) {
            // Any other escape is two characters, and the one after the
            // backslash is part of it: `"\\u00"` is a backslash followed by
            // u00, not a universal character name. Reading the second
            // backslash as one is what rejected sqlite's
            // jsonAppendRawNZ(pOut, "\\u00", 4).
            *cur++ = *p++;
            *cur++ = *p++;
        } else {
            // A run with no backslash and no high bit is a straight copy, and
            // copying it a byte at a time (six million instructions on tccgen)
            // bought nothing: ASCII needs no decoding, it is its own UTF-8.
            // Everything up to the next backslash is copied unchanged -- ASCII
            // is its own UTF-8, so nothing here needs decoding -- and one memchr
            // finds that backslash instead of a test per byte. The copy is bulk
            // for a long run and byte-wise for a short one: at -O0 memcpy is a
            // libc call, which costs more than the copy when the run is a few
            // bytes (which is what a string full of escapes produces).
            char *bs = memchr(p, '\\', (size_t)(end - p));
            char *stop = bs ? bs : end;
            size_t n = (size_t)(stop - p);
            if (n == 0) {
                // A backslash with nothing after it: no other branch can take
                // it, and it still has to be consumed or this loop never ends.
                *cur++ = *p++;
            } else if (n >= 16) {
                memcpy(cur, p, n);
                cur += n;
                p = stop;
            } else {
                while (p < stop) *cur++ = *p++;
            }
        }
    }

    *cur = '\0';
    return buf;
}

// Read an identifier and returns the length of it.
// If p does not point to a valid identifier, 0 is returned.
static int read_ident(char *start) {
    char *p = start;
    uint32_t c;
    bool first_ascii = false;
    if (*p == '\\' && (p[1] == 'u' || p[1] == 'U')) {
        c = read_universal_char(&p, p + 2, p[1], cur_file);
        if (c <= 0x9F)
            error_at(cur_file, (uint32_t)(start - cur_file->contents),
                     "universal character %.*s is not valid in an identifier", (int)(p - start), start);
    } else if ((unsigned char)*p < 0x80) {
        // ASCII: the byte is the code point, so there is nothing to decode and
        // no table to consult -- and no error to report, since an ASCII byte
        // that is not an identifier start is simply not an identifier.
        if (!is_ident1_ascii((unsigned char)*p)) return 0;
        p++;
        first_ascii = true;
    } else {
        bool success = false;
        c = decode_utf8(&p, p, &success);
        if (!success) error_at(cur_file, (uint32_t)(start - cur_file->contents), "invalid UTF-8 in identifier");
    }
    if (!first_ascii && !is_ident1(c)) {
        if (c > 0x7F)
            error_at(cur_file, (uint32_t)(start - cur_file->contents), "invalid character %.*s in identifier",
                     (int)(p - start), start);
        return 0;
    }

    while (1) {
        char *uc_start = p;
        // The two lines a splice joins are one line, so the pieces either side of
        // it are one name. The span keeps the splice; the caller asks for the
        // logical spelling.
        if (is_splice(p)) {
            p = splice_end(p);
            continue;
        }
        if (*p == '\\' && (p[1] == 'u' || p[1] == 'U')) {
            c = read_universal_char(&p, p + 2, p[1], cur_file);
            if (c <= 0x9F)
                error_at(cur_file, (uint32_t)(uc_start - cur_file->contents),
                         "universal character %.*s is not valid in an identifier", (int)(p - uc_start), uc_start);
        } else if ((unsigned char)*p < 0x80) {
            // The same shortcut per character. Note p does not move before the
            // test: the non-ASCII path has already stepped past the code point,
            // which is why it returns p - start - 1 and this returns p - start.
            if (!is_ident2_ascii((unsigned char)*p)) return p - start;
            p++;
            continue;
        } else {
            bool success = false;
            c = decode_utf8(&p, p, &success);
            if (!success) error_at(cur_file, (uint32_t)(uc_start - cur_file->contents), "invalid UTF-8 in identifier");
        }
        if (!is_ident2(c)) {
            if (c > 0x7F)
                error_at(cur_file, (uint32_t)(uc_start - cur_file->contents), "invalid character %.*s in identifier",
                         (int)(p - uc_start), uc_start);
            return p - start - 1;
        }
    }
}

static uint64_t read_escaped_char(char **new_pos, char *p, char *end) {
    uint8_t c = *p;
    static const uint32_t sim[256] = {
        ['a'] = '\a', ['b'] = '\b', ['e'] = 27,    ['f'] = '\f', ['n'] = '\n', ['r'] = '\r',
        ['t'] = '\t', ['v'] = '\v', ['\''] = '\'', ['"'] = '"',  ['?'] = '?',  ['\\'] = '\\',
    };
    if (sim[c]) {
        *new_pos = p + 1;
        return sim[c];
    }

    if ('0' <= c && c <= '7') {
        // Read an octal number.
        uint32_t val = *p++ - '0';
        if ('0' <= *p && *p <= '7') {
            val = (val << 3) + (*p++ - '0');
            if ('0' <= *p && *p <= '7') val = (val << 3) + (*p++ - '0');
        }
        *new_pos = p;
        return val;
    }

    if (c == 'o') {
        // Read an octal number.
        if (p[1] != '{') error_at(cur_file, (uint32_t)((p)-cur_file->contents), "'\\o' not followed by '{'");
        p += 2;
        if (*p == '}') goto empty_error;
        uint64_t val = 0;
        while (p < end) {
            if (*p == '}') break;
            if (*p < '0' || *p > '7') goto digit_error;
            val = (val << 3) + *p++ - '0';
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
        if (!is_ascii_xdigit((unsigned char)*p)) error_at(cur_file, (uint32_t)((p)-cur_file->contents), "invalid hex escape sequence");

        uint64_t val = 0;
        for (; p < end && is_ascii_xdigit((unsigned char)*p); p++) val = (val << 4) + from_hex(*p);
        if (has_brace && *p++ != '}') goto miss_error;
        *new_pos = p;
        return val;
    }

    if (c == 'u' || c == 'U') return read_universal_char(new_pos, p + 1, c, cur_file);

    *new_pos = p + 1;
    return c;

empty_error:
    error_at(cur_file, (uint32_t)((p)-cur_file->contents), "empty delimited escape sequence");
miss_error:
    error_at(cur_file, (uint32_t)((p)-cur_file->contents), "‘\\%c{’ not terminated with ‘}’", c);
digit_error:
    error_at(cur_file, (uint32_t)((p)-cur_file->contents), "invalid digit ‘%c’ in escape sequence", *p);
    return 0;
}

static void convert_utf8_str_literal(Token *tok, char *str) {
    while (*str++ != '"');
    char *p = str;
    while (*str++);
    char *end = str - 2;
    char *buf = emalloc(end - p);
    int len = 0;

    while (p < end) {
        if (*p == '\\') {
            buf[len++] = read_escaped_char(&p, p + 1, end);
        } else {
            buf[len++] = *p++;
        }
    }

    tok->id = intern(buf, len);
}

// Read a UTF-8-encoded string literal and transcode it in UTF-16.
//
// UTF-16 is yet another variable-width encoding for Unicode. Code
// points smaller than U+10000 are encoded in 2 bytes. Code points
// equal to or larger than that are encoded in 4 bytes. Each 2 bytes
// in the 4 byte sequence is called "surrogate", and a 4 byte sequence
// is called a "surrogate pair".
static void convert_utf16_str_literal(Token *tok, char *str) {
    while (*str++ != '"');
    char *p = str;
    while (*str++);
    char *end = str - 2;
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

    tok->id = intern((char *)buf, len * 2);
}

// Read a UTF-8-encoded string literal and transcode it in UTF-32.
//
// UTF-32 is a fixed-width encoding for Unicode. Each code point is
// encoded in 4 bytes.
static void convert_utf32_str_literal(Token *tok, char *str) {
    while (*str++ != '"');
    char *p = str;
    while (*str++);
    char *end = str - 2;
    uint32_t *buf = emalloc(4 * (end - p));
    int len = 0;
    bool ignored = false;

    while (p < end) {
        if (*p == '\\')
            buf[len++] = read_escaped_char(&p, p + 1, end);
        else
            buf[len++] = decode_utf8(&p, p, &ignored);
    }

    tok->id = intern((char *)buf, len * 4);
}

void convert_str_literal(Token *tok) {
    size_t len;
    char *str = convert_universal_chars(tok_logical(tok, &len), (int)len, file_of(tok->file_uid));
    switch (tok->enc_prefix) {
        case PREFIX_NONE:
        case PREFIX_u8:
            convert_utf8_str_literal(tok, str);
            break;
        case PREFIX_u:
            convert_utf16_str_literal(tok, str);
            break;
        case PREFIX_U:
            convert_utf32_str_literal(tok, str);
            break;
        case PREFIX_L:
            if (T.ty_wchar->size == 4)
                convert_utf32_str_literal(tok, str);
            else if (T.ty_wchar->size == 2)
                convert_utf16_str_literal(tok, str);
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
        if (is_splice(p))  // the literal goes on at the top of the next line
            p = splice_end(p);
        else if (*p == '\\')
            p += 2;
        else if (*p == '\0' || *p == '\n' || *p == '\r')
            goto error;
        else if (*p == '"')
            break;
        else
            p++;
    }

    tok = new_token(TK_STRLIT, start, p + 1);
    tok->enc_prefix = prefix;
    return tok;
error:
    tok = new_token(TK_ERR, start, p);
    tok->msg = "missing terminating \" character";
    return tok;
}

static void convert_char_literal(Token *tok) {
    size_t len;
    char *str = tok_logical(tok, &len);
    char *p = str;
    while (*p++ != '\'');
    char *end = str + len - 1;

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
    tok->ival = int128_set_ui(val);
}

static Token *read_char_literal(char *start, char *quote, uint32_t prefix) {
    Token *tok;
    char *p = quote + 1;
    if (*p == '\0') goto error;

    while (1) {
        if (is_splice(p))  // the literal goes on at the top of the next line
            p = splice_end(p);
        else if (*p == '\\')
            p += 2;
        else if (*p == '\0' || *p == '\n' || *p == '\r')
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
        if (is_splice(p)) {  // one pp-number, two lines
            p = splice_end(p);
            continue;
        }
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

static bool is_valid_digit(int c, int base) {
    if (base == 10)
        return '0' <= c && c <= '9';
    else if (base == 16)
        return ('0' <= c && c <= '9') || ('a' <= c && c <= 'f') || ('A' <= c && c <= 'F');
    else if (base == 8)
        return '0' <= c && c <= '7';
    else if (base == 2)
        return c == '0' || c == '1';
    else
        return false;
}

static void convert_pp_num(Token *t) {
    t->kind = TK_NUM;
    // The spelling to read is the logical one: a splice may sit inside the number.
    size_t tlen;
    char *text = tok_logical(t, &tlen);
    char *end = text + tlen;
    char first_ch = *text;

    int base = 10;
    char *clean = emalloc((t->len + 1) * sizeof(char));
    uint32_t ci = 0;
    bool has_pre = false;

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
            has_pre = true;
            clean[ci++] = '0';
            clean[ci++] = pre;
            text += 2;
            if (base == 16 && *text == '.' && is_valid_digit(text[1], base)) {
                // 6.4.4.2 lets the fraction stand alone -- `0x.8p0` -- and the
                // reader wants the leading zero, so it goes back in the way a
                // `.5` is canonicalised to `0.5` below. The dot itself is left
                // for the loop, which records it as the fraction. A dot with
                // digits on neither side is not a constant (`0x.p0`), and is
                // refused by the branch below.
                clean[ci++] = '0';
            } else if (!is_valid_digit(*text, base)) {
                error(t, "invalid suffix ‘%.*s’ on integer constant", (int)(end - text), text);
            }
        }
    } else if (first_ch == '.') {
        clean[ci++] = '0';  // canonicalize
    }

    // Stage 2: Skip numeric separators ', filter out invalid characters.
    // The cleaned text keeps the e/p exponent markers (with their sign)
    // so it can be handed to the library parsers as-is.
    char c = *text++;
    int had_dot = 0;
    int had_e = 0;
    int had_p = 0;
    int is_float = 0;
    int prev_is_digit = 0;
    int flags = 0;

    // c = *text
    while (text <= end) {
        switch (c) {
            case '.':
                if (had_dot || had_e || had_p || base == 2 || base == 8) goto error;
                had_dot = 1;
                clean[ci++] = '.';
                is_float = 1;
                prev_is_digit = 0;
                c = *text++;
                continue;
            case 'e':
            case 'E':
                if (base == 10) {
                    if (had_e || had_p) goto error;
                    had_e = 1;
                    clean[ci++] = 'e';
                } else {
                    if (base == 16) break;  // e is an ordinary hex digit
                    goto error;
                }
                c = *text++;
                if (c == '+' || c == '-') {
                    clean[ci++] = c;  // keep the exponent sign for the parser
                    c = *text++;
                }
                if (!is_valid_digit(c, base)) error(t, "exponent has no digits");
                is_float = 1;
                break;
            case 'p':
            case 'P':
                if (base != 16 || had_p) goto error;
                had_p = 1;
                clean[ci++] = 'p';
                base = 10;
                c = *text++;
                if (c == '+' || c == '-') {
                    clean[ci++] = c;
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
                } else if (text == end) {
                    flags = SUF_FLOAT;
                    goto extract_end;
                } else {
                    int suf = 0;
                    if (*text == '0') goto error;
                    while (text < end) {
                        if (is_ascii_digit((unsigned char)*text))
                            suf = suf * 10 + *text++ - '0';
                        else
                            goto error;
                    }
                    switch (suf) {
                        case 16:
                            flags = SUF_F16;
                            goto extract_end;
                        case 32:
                            flags = SUF_F32;
                            goto extract_end;
                        case 64:
                            flags = SUF_F64;
                            goto extract_end;
                        case 128:
                            flags = SUF_F128;
                            goto extract_end;
                        default:
                            goto error;
                    }
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
    base = had_p ? 16 : base;  // Restore base to hexadecimal

    t->lit_suffix = flags;
    if (base != 10) t->lit_suffix |= SUF_NONDEC;

    // Stage 3: Evaluate with the arbitrary-precision library parsers so
    // that large literals cannot overflow 64-bit intermediates.
    if (!is_float) {
        char *num = has_pre ? clean + 2 : clean;
        bool success = int128_set_str(&t->ival, num, base);
        if (!success) error(t, "integer literal is too large to be represented in any integer type");

        if (flags & SUF_BITINT) return;
        if (int128_bit_width(t->ival, UNSIGNED) > 64)
            error(t, "integer literal is too large to be represented in any integer type");
        return;
    }

    if (flags == 0) t->lit_suffix |= SUF_DOUBLE;

    FpFormat target = FP64;
    if (flags & SUF_FLOAT)
        target = FP32;
    else if (flags & SUF_F16)
        target = FP16;
    else if (flags & SUF_F32)
        target = FP32;
    else if (flags & SUF_F64)
        target = FP64;
    else if (flags & SUF_F128)
        target = FP128;
    else if (flags & SUF_LDOUBLE)
        target = T.ldouble_is_fp80 ? FP80 : FP128;

    bool ok = had_p ? fp128_set_hex_str(&t->fpval, clean, target) : fp128_set_str(&t->fpval, clean, target);
    if (!ok) error(t, "invalid floating constant");
    return;
error:
    text--;
    error(t, "invalid suffix ‘%.*s’ on constant", (int)(end - text), text);
}

void convert_ppnumber(Token *tok) {
    // NULL as well as TK_EOF: in a pulled stream the pass is run on the part that
    // has just become final, and that part ends at NULL.
    while (tok && tok->kind != TK_EOF) {
        if (tok->kind == TK_ERR) error(tok, "%s", tok->msg);
        if (tok->kind == TK_PPNUM) convert_pp_num(tok);
        if (tok->kind == TK_CHARLIT) convert_char_literal(tok);
        tok = tok_next(tok);
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
        {"_Float16", 0, TK_F16},
        {"_Float32", 0, TK_F32},
        {"_Float64", 0, TK_F64},
        {"_Float128", 0, TK_F128},
        {"_Float32x", 0, TK_F32X},
        {"_Float64x", 0, TK_F64X},
        {"_Float128x", 0, TK_F128X},
        {"_Generic", 0, TK_GENERIC},
        {"_Noreturn", 0, TK_NORETURN},
        {"_Static_assert", 0, TK_STATIC_ASSERT},
        {"_Thread_local", 0, TK_THREAD},
        {"__asm", 0, TK_ASM},
        {"__asm__", 0, TK_ASM},
        {"__attribute", 0, TK_ATTR},
        {"__attribute__", 0, TK_ATTR},
        // GNU's older spelling of _Alignof; __alignof__ already resolves through
        // the double-underscore stripping, but this one does not.
        {"__alignof", 0, TK_ALIGNOF},
        {"__extension__", 0, TK_EXTENSION},
        // [GNU] __label__: declares names whose scope is the enclosing block.
        {"__label__", 0, TK_LABEL_DECL},
        // The ACLE name for the half type. clang makes it a type of its own --
        // _Generic and __builtin_types_compatible_p tell it from _Float16, and
        // on targets without half-precision parameters it may only sit behind a
        // pointer -- where cxx takes it as another spelling of _Float16, which
        // matches on representation, arithmetic and ABI. gcc has no __fp16.
        {"__fp16", 0, TK_F16},
        {"__inline", 0, TK_INLINE},
        {"__inline__", 0, TK_INLINE},
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
    // 76 keywords, so an open-addressed table at a load factor well under half:
    // what this replaces is a walk over every row for every identifier token,
    // and identifiers are most of the tokens in real source. The id carries the
    // interning table's own hash in its low bits, which indexes this one.
    //
    // `used` is not the id: an interned id is `bucket | (index << 12)`, so the
    // first string in bucket zero has the id zero -- a valid id that an
    // "empty means zero" test would both hide and let a probe chain stop at.
    enum { KW_SLOTS = 256 };
    static struct {
        uint32_t id;
        uint32_t kind;
        bool used;
    } kw_tab[KW_SLOTS];

    if (!kw[0].id) {
        for (size_t i = 0; i < sizeof(kw) / sizeof(kw[0]); ++i) {
            kw[i].id = intern(kw[i].keyword, strlen(kw[i].keyword));
            uint32_t h = kw[i].id & (KW_SLOTS - 1);
            // First row wins, as the linear scan did, if a spelling were listed twice.
            while (kw_tab[h].used && kw_tab[h].id != kw[i].id) h = (h + 1) & (KW_SLOTS - 1);
            kw_tab[h].id = kw[i].id;
            kw_tab[h].kind = kw[i].type;
            kw_tab[h].used = true;
        }
    }
    while (tok && tok->kind != TK_EOF) {
        if (tok->kind == TK_IDENT) {
            for (uint32_t h = tok->id & (KW_SLOTS - 1); kw_tab[h].used; h = (h + 1) & (KW_SLOTS - 1))
                if (kw_tab[h].id == tok->id) {
                    tok->kind = kw_tab[h].kind;
                    break;
                }
        }
        tok = tok_next(tok);
    }
}

// One chunk of scanning. The scanner is the same one that always ran here;
// what is new is that it can stop and be resumed.
//
// A chunk ends after the directive line that follows a segment, plus one more
// token: skip_line()/read_line() walk to the first token of the next line and
// cond_directive() returns it as `after`, so both have to exist. One token is
// also what a dead region costs in the worst case.
typedef struct LexChunk LexChunk;
struct LexChunk {
    SrcFile *file;
    char *p;
    bool stop_pending;  // a directive's '#' has been produced in this chunk
    bool after;         // one token has been produced since
    bool done;
    bool eof_done;
    // An included file gets a frame of its own, chained to the one it
    // interrupts; the two flags belong to a scan position, so the parent's are
    // kept here while the child runs.
    LexChunk *parent;
    bool f_sol, f_ws;
    Token dummy;
    Token *cur;
};

static LexChunk *LC;

#define LINK(t)                        \
    do {                               \
        LC->cur = LC->cur->next = (t); \
        /* The chunk ends one token into the line after the directive -- but a
           '#' cannot be that token: the preprocessor reads its next token (the
           directive's name) straight away, so the whole line has to be here. */ \
        if (LC->stop_pending && (t)->is_sol && (t)->kind != TK_HASH) LC->after = true; \
    } while (0)

static void lex_scan(void) {
    char *p = LC->p;
    Token *tok;

    LC->stop_pending = false;
    LC->after = false;

    for (;;) {
        if (!*p) {
            LC->done = true;
            break;
        }
        if (LC->stop_pending && LC->after) break;
        // Read line comments.
        if (*p == '/' && p[1] == '/') {
            char *q = p + 2;
            // A splice continues the comment on the next line; a lone CR ends it,
            // and so does the end of the file.
            while (*q && *q != '\n' && *q != '\r') {
                if (is_splice(q)) { q = splice_end(q); continue; }
                q++;
            }
            // A comment is leading white space for the token that follows it,
            // so that flag is all this has to leave behind when the dump
            // options are off.
            is_leadingws = true;
            if (opt_keep_ws_comments && !lex_lazy) {
                tok = new_token(TK_COMMENT, p, q);
                LINK(tok);
            }
            p = q;
            continue;
        }

        // Read block comments.
        if (*p == '/' && p[1] == '*') {
            char *q = strstr(p + 2, "*/");
            if (!q) error_at(cur_file, (uint32_t)((p)-cur_file->contents), "unterminated /* comment");
            is_leadingws = true;
            if (opt_keep_ws_comments && !lex_lazy) {
                tok = new_token(TK_COMMENT, p, q + 2);
                LINK(tok);
            }
            p = q + 2;
            continue;
        }

        // Read white space. A run of it is one token, so the run has to be
        // measured either way; what the dump options decide is whether it also
        // becomes one. The two bits the parser reads off the *next* token --
        // start of line, leading white space -- are set here as they always
        // were, so the stream the parser sees does not change.
        if (is_ascii_space((unsigned char)*p) || is_splice(p)) {
            char *q = p;
            int nl = 0;
            // Whether the run's last logical byte is space rather than a line
            // break. A splice is space: the two lines it joins are one line.
            bool ends_ws = false;
            while (is_ascii_space((unsigned char)*q) || is_splice(q)) {
                if (is_splice(q)) {
                    // A splice contributes nothing: it is not a line break, so it
                    // does not start a line, and it is not white space either, so
                    // the run's last logical byte is still whatever came before.
                    q = splice_end(q);
                    continue;
                }
                if (*q == '\r' && q[1] == '\n') { q += 2; nl++; ends_ws = false; continue; }
                if (*q == '\n' || *q == '\r') { q++; nl++; ends_ws = false; continue; }
                q++;
                ends_ws = true;
            }
            if (nl) is_sol = true;
            // Blank space before a newline is leading white space; the newline
            // itself is not. "  \n  x" is both.
            if (ends_ws) is_leadingws = true;
            if (opt_keep_ws_comments && !lex_lazy) {
                tok = new_token(TK_WS, p, q);
                if (nl) tok->kind = TK_NL;
                LINK(tok);
            }
            p = q;
            continue;
        }

        // Numeric literal
        if (is_ascii_digit((unsigned char)*p) || (*p == '.' && is_ascii_digit((unsigned char)p[1]))) {
            tok = read_int_literal(p);
            LINK(tok);
            p += tok->len;
            continue;
        }

        // String literal
        if (*p == '"') {
            tok = read_string_literal(p, p, PREFIX_NONE);
            LINK(tok);
            p += tok->len;
            continue;
        }

        // Character literal
        if (*p == 'u' && p[1] == '8' && p[2] == '"') {
            tok = read_string_literal(p, p + 2, PREFIX_u8);
            LINK(tok);
            p += tok->len;
            continue;
        }

        // UTF-16 string literal
        if (*p == 'u' && p[1] == '"') {
            tok = read_string_literal(p, p + 1, PREFIX_u);
            LINK(tok);
            p += tok->len;
            continue;
        }

        // UTF-32 string literal
        if (*p == 'U' && p[1] == '"') {
            tok = read_string_literal(p, p + 1, PREFIX_U);
            LINK(tok);
            p += tok->len;
            continue;
        }

        // Wide string literal
        if (*p == 'L' && p[1] == '"') {
            tok = read_string_literal(p, p + 1, PREFIX_L);
            LINK(tok);
            p += tok->len;
            continue;
        }

        // Character literal
        if (*p == '\'') {
            tok = read_char_literal(p, p, PREFIX_NONE);
            LINK(tok);
            p += tok->len;
            continue;
        }

        // UTF-8 character literal
        if (*p == 'u' && p[1] == '8' && p[2] == '\'') {
            tok = read_char_literal(p, p + 2, PREFIX_u8);
            LINK(tok);
            p += tok->len;
            continue;
        }

        // UTF-16 character literal
        if (*p == 'u' && p[1] == '\'') {
            tok = read_char_literal(p, p + 1, PREFIX_u);
            LINK(tok);
            p += tok->len;
            continue;
        }

        // UTF-32 character literal
        if (*p == 'U' && p[1] == '\'') {
            tok = read_char_literal(p, p + 1, PREFIX_U);
            LINK(tok);
            p += tok->len;
            continue;
        }

        // Wide character literal
        if (*p == 'L' && p[1] == '\'') {
            tok = read_char_literal(p, p + 1, PREFIX_L);
            LINK(tok);
            p += tok->len;
            continue;
        }

        // Identifier or keyword
        int ident_len = read_ident(p);
        if (ident_len) {
            tok = new_token(TK_IDENT, p, p + ident_len);
            // Only when there is a name to replace: otherwise the token's text is already
            // what interning wants, and that buffer and its copy are pure overhead --
            // intern copies len bytes and terminates its own copy, so it never needed a
            // C string to begin with.
            // The name as it reads, which is the file's bytes unless a splice
            // is inside it (perf.md 8or).
            size_t name_len;
            char *name = splice_free(p, (size_t)ident_len, &name_len);
            bool ucn = has_ucn(name, (int)name_len);
            char *buf = ucn ? convert_universal_chars(name, (int)name_len, cur_file) : name;
            // Interning copies the text and hashes it: the one piece of work a
            // skipped region must not pay for.
            if (!lex_skip) tok->id = intern(buf, ucn ? (uint32_t)strlen(buf) : (uint32_t)name_len);
            LINK(tok);
            p += tok->len;
            continue;
        }

        // Punctuator
        if (*p <= 0x7F) {
            uint32_t kind;
            int len;
            char *raw;
            // A splice can split a punctuator (`+` \ newline `+` is one `++`), so
            // when one is in reach the match is done on a logical window. Real
            // sources never take that path: perf.md 8or counted none.
            if (is_splice(p + 1) || is_splice(p + 2) || is_splice(p + 3)) {
                len = read_punct_logical(p, &kind, &raw);
            } else {
                len = read_punct(p, &kind);
                raw = p + len;
            }
            if (len) {
                tok = new_token(kind, p, raw);
                LINK(tok);
                // The line this '#' introduces belongs to the chunk as well:
                // read_line()/skip_line() walk it, and cond_directive() returns
                // its first token as `after`.
                if (kind == TK_HASH && tok->is_sol) LC->stop_pending = true;
                p = raw;
                continue;
            }
        }

        // Other char
        tok = new_token(TK_OTHER, p, p + 1);
        LINK(tok);
        p += tok->len;
        continue;
    }

    LC->p = p;
}

// The EOF token, once, when the file has run out. The guard is a flag and not
// `cur->kind == TK_EOF`: TK_EOF is 0, which is exactly what an empty chunk's
// dummy head holds, so an all-whitespace file would look finished already.
static void lex_scan_eof(void) {
    if (!LC->done || LC->eof_done) return;
    LC->eof_done = true;
    LC->cur = LC->cur->next = new_token(TK_EOF, LC->p, LC->p);
}

// The whole file in one go: what includes, paste() and -dump-raw-tokens use.
// The scanner's own state is saved and put back, so a nested file cannot
// disturb the lazily lexed one.
Token *tokenize(SrcFile *file) {
    LexChunk local = {.file = file, .p = file->contents};
    local.cur = &local.dummy;
    LexChunk *saved = LC;
    SrcFile *sfile = cur_file;
    bool s1 = is_sol, s2 = is_leadingws, s3 = lex_skip, s4 = lex_lazy;

    cur_file = file;
    is_sol = true;
    is_leadingws = false;
    lex_skip = false;
    lex_lazy = false;
    LC = &local;

    while (!local.done) lex_scan();
    lex_scan_eof();

    LC = saved;
    cur_file = sfile;
    is_sol = s1;
    is_leadingws = s2;
    lex_skip = s3;
    lex_lazy = s4;
    return local.dummy.next;
}

// ---- the lazy path: files are produced as the preprocessor asks ----

// A file (the main one, or an included one) gets a frame of its own, chained to
// the frame it interrupts.
void lex_push_file(SrcFile *file) {
    LexChunk *st = emalloc(sizeof(LexChunk));
    *st = (LexChunk){.file = file, .p = file->contents, .parent = LC};
    st->cur = &st->dummy;
    if (LC) {
        LC->f_sol = is_sol;
        LC->f_ws = is_leadingws;
    }
    LC = st;
    cur_file = file;
    is_sol = true;
    is_leadingws = false;
    lex_skip = false;
    lex_lazy = true;
}

void lex_start(SrcFile *file) { lex_push_file(file); }

// Back to the file that was being read when this one was included. Its scan
// position, its chain and its two flags are where they were left.
void lex_pop_file(void) {
    if (!LC || !LC->parent) return;
    LC = LC->parent;
    cur_file = LC->file;
    is_sol = LC->f_sol;
    is_leadingws = LC->f_ws;
}

// Walk to the '#' that begins the next directive without making a single
// token. White space and comments are scanned the way the tokenizer scans them
// -- a '#' inside either is not a directive -- and the two flags are left
// exactly as the tokenizer would have left them, so the '#' that ends the walk
// gets the right ones. Literals go through the tokenizer's own readers (in the
// no-allocation mode new_token() has): where a literal ends is not something to
// re-derive, cxx's reader is the definition.
static void lex_skip_to_directive(void) {
    char *p = LC->p;
    Token *tok = NULL;
    lex_skip = true;

    for (;;) {
        if (!*p) {
            LC->done = true;
            break;
        }
        if (is_sol && *p == '#') break;

        // Line comment.
        if (*p == '/' && p[1] == '/') {
            char *q = p + 2;
            // A splice continues the comment on the next line; a lone CR ends it,
            // and so does the end of the file.
            while (*q && *q != '\n' && *q != '\r') {
                if (is_splice(q)) { q = splice_end(q); continue; }
                q++;
            }
            is_leadingws = true;
            p = q;
            continue;
        }
        // Block comment.
        if (*p == '/' && p[1] == '*') {
            char *q = strstr(p + 2, "*/");
            if (!q) error_at(cur_file, (uint32_t)(p - cur_file->contents), "unterminated /* comment");
            is_leadingws = true;
            p = q + 2;
            continue;
        }
        // White space, and the two bits the tokenizer sets here.
        if (is_ascii_space((unsigned char)*p) || is_splice(p)) {
            char *q = p;
            int nl = 0;
            bool ends_ws = false;
            while (is_ascii_space((unsigned char)*q) || is_splice(q)) {
                if (is_splice(q)) { q = splice_end(q); continue; }
                if (*q == '\r' && q[1] == '\n') { q += 2; nl++; ends_ws = false; continue; }
                if (*q == '\n' || *q == '\r') { q++; nl++; ends_ws = false; continue; }
                q++;
                ends_ws = true;
            }
            if (nl) is_sol = true;
            if (ends_ws) is_leadingws = true;
            p = q;
            continue;
        }
        // A literal, with or without an encoding prefix.
        if (*p == '"' || *p == '\'') {
            tok = *p == '"' ? read_string_literal(p, p, PREFIX_NONE)
                            : read_char_literal(p, p, PREFIX_NONE);
        } else if (*p == 'u' && p[1] == '8' && (p[2] == '"' || p[2] == '\'')) {
            tok = p[2] == '"' ? read_string_literal(p, p + 2, PREFIX_u8)
                              : read_char_literal(p, p + 2, PREFIX_u8);
        } else if ((*p == 'u' || *p == 'U' || *p == 'L') && (p[1] == '"' || p[1] == '\'')) {
            uint32_t pre = *p == 'u' ? PREFIX_u : *p == 'U' ? PREFIX_U : PREFIX_L;
            tok = p[1] == '"' ? read_string_literal(p, p + 1, pre)
                              : read_char_literal(p, p + 1, pre);
        } else {
            // One byte of something that cannot start a directive.
            is_sol = is_leadingws = false;
            p++;
            continue;
        }
        is_sol = is_leadingws = false;
        p += tok->len;
    }

    lex_skip = false;
    LC->p = p;
}

Token *lex_chunk(bool live) {
    // The tail as it stands: whatever this call produces hangs off it.
    Token *before = LC->cur;

    if (LC->done) {
        lex_scan_eof();
        return tok_next(before);
    }
    if (!live) {
        lex_skip_to_directive();
        if (LC->done) {
            lex_scan_eof();
            return tok_next(before);
        }
    }
    lex_scan();
    lex_scan_eof();
    return tok_next(before);
}

// Returns the contents of a given file.
static char *read_file(char *path) {
    FILE *fp;

    if (*path == '-' && path[1] == '\0') {
        // By convention, read from stdin if a given filename is "-".
        fp = stdin;
    } else {
        fp = fopen(path, "r");
        if (!fp) return NULL;
    }

    // A regular file is mapped, not copied: its size is known, nothing writes
    // it, and mapping keeps the pages file-backed -- the kernel can drop them
    // under pressure and a second compile of the same file shares them. Anything
    // else (a pipe, or one of the two shapes noted below) is read in.
    char *buf = NULL;
    size_t buflen = 0, cap = 0;
    bool mapped = false;

    struct stat st;
    if (fp != stdin && fstat(fileno(fp), &st) == 0 && S_ISREG(st.st_mode)) {
        cap = (size_t)st.st_size + 2;
        // The scanner reads up to two bytes past the text (the terminator, and a
        // splice's CR LF), so the last page has to hold them: a size whose tail
        // lands in a page that is wholly past the end of the file cannot be
        // mapped. Three sizes in 4096 are like that, and a file that does not end
        // with a newline takes the fixup below, which is a write.
        if (st.st_size > 0 && st.st_size % 4096 <= 4092) {
            char *m = mmap(NULL, cap, PROT_READ, MAP_PRIVATE, fileno(fp), 0);
            if (m != MAP_FAILED) {
                if (m[st.st_size - 1] == '\n') {
                    buf = m;
                    buflen = (size_t)st.st_size;
                    mapped = true;
                } else {
                    munmap(m, cap);
                }
            }
        }
        if (!buf) {
            buf = cap >= BIG_THRESHOLD ? emalloc_big(cap) : emalloc(cap);
            buflen = fread(buf, 1, (size_t)st.st_size, fp);
        }
    } else {
        // A pipe or something else without a size: read in steps. The arena hands
        // out contiguous memory -- it is a bump allocator and nothing else
        // allocates while this loop runs -- so each step is just the address
        // after the last, with no vgrow and no copy. vgrow also left every
        // intermediate buffer behind in the arena; this leaves none. The one
        // thing that breaks the run is running out of the current pool chunk,
        // which is what the check below is for.
        cap = 4096;
        buf = emalloc(cap);
        while (1) {
            if (buflen + 4096 + 2 > cap) {
                char *more = emalloc(4096);
                if (more == buf + cap) {
                    cap += 4096;  // the chunk continued: still one piece
                } else {
                    // A chunk boundary, so the two pieces are not adjacent: take
                    // one buffer that holds what we have plus room to go on, and
                    // copy once. This happens at most once per pool chunk.
                    size_t newcap = cap * 2 + 4096 + 2;
                    char *big = emalloc(newcap);
                    memcpy(big, buf, buflen);
                    buf = big;
                    cap = newcap;
                }
            }
            int n = fread(buf + buflen, 1, cap - buflen - 2, fp);
            if (n == 0) break;
            buflen += n;
        }
    }

    // Make sure that the last line is properly terminated with '\n'.
    if (buflen > 0 && buf[buflen - 1] == '\\') fatal("stray ‘\\’ at end of file");
    // A mapped file already ends with a newline (it had to, to be mapped), and
    // writing to it would fault: the mapping is read-only on purpose.
    if (!mapped && (buflen == 0 || buf[buflen - 1] != '\n')) buf[buflen] = '\n';

    return buf;
}

SrcFile **get_input_files(void) { return input_files; }


// Every file the compiler opens, by its own handle. The display-name id cannot
// serve here: two files can carry the same name, and #line moves the name away
// from the file. The compiler's own sources (scratch space, <built-in>, the
// command line) go through new_file() as well, so they are covered too.
SrcFile **file_tab;
uint32_t file_tab_cap;
static uint32_t file_uid_seq;

// emalloc() callocs on both of its paths -- the pool it hands out of and the
// large blocks -- so what vnew() returns is already zeroed; a junk value in one
// of these buffers would mean something wrote out of bounds, not that the
// allocator left it unset.
static void register_file(SrcFile *f) {
    f->uid = ++file_uid_seq;
    if (f->uid >= file_tab_cap) {
        uint32_t cap = file_tab_cap ? file_tab_cap : 64;
        while (cap <= f->uid) cap *= 2;
        SrcFile **tab = vnew(cap, sizeof(SrcFile *));
        for (uint32_t i = 0; i < file_tab_cap; i++) tab[i] = file_tab[i];
        file_tab = tab;
        file_tab_cap = cap;
    }
    file_tab[f->uid] = f;
}

SrcFile *new_file(char *name, int file_no, char *contents) {
    SrcFile *file = emalloc(sizeof(SrcFile));
    file->name = name;
    file->id = intern(name, strlen(name));
    file->file_no = file_no;
    file->contents = contents;
    register_file(file);
    return file;
}

static void build_line_offsets(SrcFile *f) {
    if (!f || f->num_lines > 0) return;

    // The length and the number of lines, by searching rather than by walking a
    // byte at a time: memchr is vectorised, and the two loops that used to scan
    // the source character by character were 4% of the front end on tccgen.
    size_t size = strlen(f->contents);
    int count = 1;
    for (char *p = f->contents; (p = memchr(p, '\n', f->contents + size - p)); p++) count++;

    f->size = size;
    f->num_lines = count;
    f->line_offsets = emalloc(sizeof(uint32_t) * count);

    int idx = 1;
    f->line_offsets[0] = 0;
    for (char *p = f->contents; (p = memchr(p, '\n', f->contents + size - p));)
        f->line_offsets[idx++] = (uint32_t)(++p - f->contents);
}

void get_location(SrcFile *f, uint32_t offset, int *out_line, int *out_col) {
    if (!f) {
        *out_line = 1;
        *out_col = 1;
        return;
    }

    build_line_offsets(f);

    if (offset > f->size) offset = f->size;

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

    *out_col = (int)(offset - f->line_offsets[idx]) + 1;
}

// Read a file and register it, without tokenizing it.
SrcFile *open_source_file(char *path) {
    char *p = read_file(path);
    if (!p) return NULL;

    // UTF-8 texts may start with a 3-byte "BOM" marker sequence.
    // If exists, just skip them because they are useless bytes.
    if (!memcmp(p, "\xef\xbb\xbf", 3)) p += 3;

    // Nothing is rewritten: the scanner reads phase 2 itself (perf.md 8or), so
    // the bytes stay as they are on disk and a mapping of them stays clean.

    static int file_no = 0;
    SrcFile *file = new_file(path, file_no + 1, p);

    // One slot past the last file is the terminator print_dependencies()
    // stops at; it used to be whatever the arena happened to have there.
    if (!input_files)
        input_files = vnew(4, sizeof(SrcFile *));
    else
        input_files = vgrow(input_files, file_no + 2);

    input_files[file_no++] = file;
    input_files[file_no] = NULL;
    return file;
}

Token *tokenize_file(char *path) {
    SrcFile *file = open_source_file(path);
    return file ? tokenize(file) : NULL;
}
