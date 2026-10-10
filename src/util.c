#include "cxx.h"

// Chunks are handed out on demand rather than one 128 MB pool being calloc'd
// up front: the tail of a big pool is memory that cannot be used for
// anything else, and virtual size that a leak or a stray write can hide in.
#define BIG_THRESHOLD (128 * 1024)     // 128KB
#define ALIGNMENT _Alignof(max_align_t)
#define POOL_CHUNK_MIN (64 * 1024)
#define POOL_CHUNK_MAX (8 * 1024 * 1024)
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

#define COLOR_RESET "\033[0m"
#define COLOR_BOLD "\033[1m"
#define COLOR_RED "\033[0;1;31m"
#define COLOR_GREEN "\033[0;1;32m"
#define COLOR_MAGENTA "\033[0;1;35m"
#define COLOR_CYAN "\033[0;1;36m"

// Reports an error and exit.
void fatal(char *fmt, ...) {
    bool use_color = isatty(fileno(stderr));
    if (use_color)
        fprintf(stderr, COLOR_BOLD "cxx:" COLOR_RESET COLOR_RED " fatal error: " COLOR_RESET);
    else
        fprintf(stderr, "cxx: fatal error: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

static void emit_diag(char *level, uint32_t filename, int line_delta, SrcFile *diagfile, uint32_t loc, const char *msg,
                      va_list ap) {
    // Find the line containing `loc`.
    int line, col;
    get_location(diagfile, loc, &line, &col);

    char *p = diagfile->contents;
    char *start = p + diagfile->line_offsets[line - 1];
    char *end = p + diagfile->line_offsets[line] - 1;
    char *at = p + loc;

    bool use_color = isatty(fileno(stderr));
    if (use_color)
        fprintf(stderr, COLOR_BOLD "%s:%d:%d: " COLOR_RESET, str(filename), line + line_delta, col);
    else
        fprintf(stderr, "%s:%d:%d: ", str(filename), line + line_delta, col);
    if (use_color) {
        char *color;
        if (!strcmp(level, "error"))
            color = COLOR_RED;
        else if (!strcmp(level, "warning"))
            color = COLOR_MAGENTA;
        else
            color = COLOR_CYAN;
        fprintf(stderr, "%s%s: " COLOR_RESET, color, level);
    } else {
        fprintf(stderr, "%s: ", level);
    }
    vfprintf(stderr, msg, ap);
    fputc('\n', stderr);

    int indent = fprintf(stderr, " %4d | ", line);
    fprintf(stderr, "%.*s\n", (int)(end - start), start);

    fprintf(stderr, "%*s", indent, "| ");  // print pos spaces.

    int width = display_width(start, at - start);
    while (start < at)
        if (*start++ == '\t') fprintf(stderr, "\t");
    fprintf(stderr, "%*s", width, "");

    if (use_color)
        fprintf(stderr, COLOR_GREEN "^\n" COLOR_RESET);
    else
        fprintf(stderr, "^\n");
}

// The fields emit_diag already takes, gathered into the one type a caller
// that no longer has tokens can carry around.
static void emit_diag_loc(char *level, Loc at, const char *msg, va_list ap) {
    emit_diag(level, at.filename, at.line_delta, file_of(at.file_uid), at.loc, msg, ap);
}

// The location a diagnostic should name: an expansion reports where it was
// written, which is what walking the origin chain finds -- the same walk diag()
// below makes.
Loc loc_of(Token *tok) {
    Token *orig = tok;
    while (orig->origin) orig = orig->origin;
    Loc at = {orig->filename, file_of(orig->file_uid)->uid, orig->loc, orig->len, orig->line_delta};
    return at;
}

// diag()'s sibling for the stages that keep locations but no tokens.
void diag_loc(char *level, Loc at, const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    emit_diag_loc(level, at, msg, ap);
    va_end(ap);
}

void diag(char *level, Token *tok, const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    // Through Loc: the same four fields emit_diag wants, taken from the token
    // here. Every message the compiler prints goes this way, so the path is
    // exercised by the whole test suite rather than by the few callers that
    // keep a location past the tokens.
    emit_diag_loc(level, loc_of(tok), msg, ap);
    va_end(ap);
}

void diag_exit(char *level, Token *tok, const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    emit_diag_loc(level, loc_of(tok), msg, ap);
    va_end(ap);
    exit(1);
}

void error(Token *tok, const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    Token *orig = tok;
    while (orig->origin) orig = orig->origin;
    emit_diag("error", orig->filename, orig->line_delta, file_of(orig->file_uid), orig->loc, msg, ap);
    va_end(ap);
    exit(1);
}

extern bool opt_nowarn;
extern bool opt_werror;
extern uint32_t opt_wgroups;
// -w silences everything; otherwise a diagnostic is emitted when its group
// is on. WG_DEFAULT has no bit of its own and is on whenever -w is absent.
bool wg_enabled(int group) {
    if (opt_nowarn) return false;
    if (group == WG_DEFAULT) return true;
    return (opt_wgroups & (uint32_t)group) != 0;
}

// A construct ISO C forbids that cxx accepts as a GNU extension. Silent
// unless -pedantic, and fatal under -pedantic-errors. Not part of the
// warning-group machinery: -pedantic is a mode, not a group, which is how
// gcc and clang treat it too.
void pedantic(Token *tok, const char *msg, ...) {
    // -w inhibits the mode as a whole, diagnostics and all: gcc and clang
    // stay silent for `-w -pedantic` and for `-w -pedantic-errors`.
    if (!opt_pedantic || opt_nowarn) return;
    va_list ap;
    va_start(ap, msg);
    Token *orig = tok;
    while (orig->origin) orig = orig->origin;
    emit_diag(opt_pedantic_errors ? "error" : "warning", orig->filename, orig->line_delta, file_of(orig->file_uid), orig->loc, msg,
              ap);
    va_end(ap);
    if (opt_pedantic_errors) exit(1);
}

// The scalar type names gcc and clang use in the conversion diagnostics.
// Only arithmetic types reach them.
const char *diag_ty_name(Type *ty) {
    switch (ty->kind) {
        case TY_BOOL:
            return "_Bool";
        case TY_CHAR:
            return "char";
        case TY_UCHAR:
            return "unsigned char";
        case TY_SCHAR:
            return "signed char";
        case TY_SHORT:
            return ty->is_unsigned ? "unsigned short" : "short";
        case TY_INT:
            return ty->is_unsigned ? "unsigned int" : "int";
        case TY_LONG:
            return ty->is_unsigned ? "unsigned long" : "long";
        case TY_LLONG:
            return ty->is_unsigned ? "unsigned long long" : "long long";
        case TY_FLOAT:
            return "float";
        case TY_DOUBLE:
            return "double";
        case TY_LDOUBLE:
            return "long double";
        default:
            return "?";
    }
}

void warning(int group, Token *tok, const char *msg, ...) {
    if (!wg_enabled(group)) return;
    va_list ap;
    va_start(ap, msg);
    Token *orig = tok;
    while (orig->origin) orig = orig->origin;
    emit_diag(opt_werror ? "error" : "warning", orig->filename, orig->line_delta, file_of(orig->file_uid), orig->loc, msg, ap);
    va_end(ap);
    if (opt_werror) exit(1);
}

void error_at(SrcFile *file, uint32_t loc, const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    emit_diag("error", file->id, 0, file, loc, msg, ap);
    va_end(ap);
    exit(1);
}


static char *pool;
static size_t free_len;
static size_t next_chunk = POOL_CHUNK_MIN;  // grows, so a small compile stays small

// One function, not an inline: measured, inlining this into its sixty call sites
// was 0.9% slower -- the body is the cost, not the call.
void *emalloc(size_t n) {
    if (n == 0) return NULL;
    n = ALIGN_UP(n, ALIGNMENT);

    if (n >= BIG_THRESHOLD) {
        void *p = calloc(1, n);
        if (!p) fatal("emalloc, out of memory");
        return p;
    }

    if (free_len < n) {
        // Chunks on demand rather than one 128 MB pool: the tail of a big pool is
        // memory that can serve nothing else, and virtual space a stray write can
        // hide in. Doubling keeps the number of calls down for a large job.
        size_t chunk = next_chunk;
        while (chunk < n) chunk *= 2;
        if (next_chunk < POOL_CHUNK_MAX) next_chunk *= 2;
        char *p = calloc(1, chunk);
        if (!p) fatal("emalloc, out of memory");
        pool = p;
        free_len = chunk;
    }

    void *p = pool;
    pool += n;
    free_len -= n;
    return p;
}

typedef struct Vec Vec;
struct Vec {
    size_t esz;
    size_t cap;
    union {
        long long ll;
        long double ld;
        void *ptr;
        void (*fp)(void);
    } data[];
};

void *vnew(size_t len, size_t esz) {
    size_t cap = 2;
    while (cap < len) cap *= 2;
    Vec *v = emalloc(sizeof(Vec) + esz * cap);
    v->cap = cap;
    v->esz = esz;
    return v->data;
}

void *vgrow(void *data, size_t len) {
    if (!data) return NULL;
    Vec *v = container_of(data, Vec, data);
    if (v->cap >= len) return data;

    void *new_data = vnew(len, v->esz);
    memcpy(new_data, data, v->esz * v->cap);
    return new_data;
}

char *format(char *s, ...) {
    va_list ap;
    int n;
    char *p;

    va_start(ap, s);
    n = vsnprintf(NULL, 0, s, ap);
    va_end(ap);
    p = emalloc(n + 1);
    va_start(ap, s);
    vsnprintf(p, n + 1, s, ap);
    va_end(ap);
    return p;
}

#define IBits 12
#define IMask ((1 << IBits) - 1)

typedef struct Bucket Bucket;
struct Bucket {
    uint32_t nstr;
    uint32_t *len;
    char **str;
};
static Bucket itbl[IMask + 1];

static uint32_t fnv_hash_32(const unsigned char *s, int len) {
    uint32_t hash = 0x811c9dc5;
    for (int i = 0; i < len; i++) {
        hash ^= (uint32_t)s[i];
        hash *= 0x01000193;
    }
    return hash;
}

uint32_t intern(char *s, uint32_t len) {
    Bucket *b;
    uint32_t h;
    uint32_t i, n;

    h = fnv_hash_32((const unsigned char *)s, len) & IMask;
    b = &itbl[h];
    n = b->nstr;

    for (i = 0; i < n; i++)
        if (b->len[i] == len && memcmp(s, b->str[i], len) == 0) return h | (i << IBits);

    if (n == 1 << (32 - IBits)) fatal("interning table overflow");
    if (n == 0) {
        b->str = vnew(1, sizeof b->str[0]);
        b->len = vnew(1, sizeof b->len[0]);
    } else if ((n & (n - 1)) == 0) {
        b->str = vgrow(b->str, n + n);
        b->len = vgrow(b->len, n + n);
    }
    b->len[n] = len;
    b->str[n] = emalloc(len + 1);
    b->nstr = n + 1;
    memcpy(b->str[n], s, len);
    b->str[n][len] = '\0';
    return h | (n << IBits);
}

char *str(uint32_t id) {
    assert(id >> IBits < itbl[id & IMask].nstr);
    return itbl[id & IMask].str[id >> IBits];
}

uint32_t str_len(uint32_t id) {
    assert(id >> IBits < itbl[id & IMask].nstr);
    return itbl[id & IMask].len[id >> IBits];
}

char *escape_char_to_string(char c) {
    static char buffer[8];
    if (c == '\\') {
        return "\\\\";
    }
    if (c == '"') {
        return "\\22";
    }
    if (isprint(c)) {
        buffer[0] = c;
        buffer[1] = '\0';
    } else {
        sprintf(buffer, "\\%02X", (unsigned char)c);
    }
    return buffer;
}

static uint32_t con_hash(Con *c0) {
    if (c0->type == CBits128)
        return c0->bits.i128.limb[0] ^ c0->bits.i128.limb[1] ^ c0->bits.i128.limb[2] ^ c0->bits.i128.limb[3];
    return (uint32_t)(c0->type * 0x9E3779B9u) ^ c0->sym ^ (uint32_t)c0->bits.i ^ (uint32_t)(c0->bits.i >> 32);
}

static void con_rehash(Module *md, int cap) {
    int *ht = vnew(cap, sizeof(int));
    for (int i = 0; i < cap; i++) ht[i] = -1;
    for (int i = 0; i < md->ncon; i++) {
        Con *c = &md->con[i];
        int h = con_hash(c) & (cap - 1);
        c->hnext = ht[h];
        ht[h] = i;
    }
    md->con_ht = ht;
    md->con_cap = cap;
}

static bool con_eq(Con *a, Con *b) {
    if (a->type != b->type || a->sym != b->sym) return false;
    if (a->type == CBits128) return memcmp(&a->bits.i128, &b->bits.i128, sizeof(Int128)) == 0;
    return a->bits.i == b->bits.i;
}

Ref newcon(Con *c0, Module *md) {
    if (!md->con_ht)
        con_rehash(md, 64);
    else if (md->con_n >= md->con_cap * 2)
        con_rehash(md, md->con_cap * 2);

    int h = con_hash(c0) & (md->con_cap - 1);
    for (int ci = md->con_ht[h]; ci >= 0; ci = md->con[ci].hnext)
        if (con_eq(c0, &md->con[ci])) return CON(ci, NULL);

    md->con = vgrow(md->con, ++md->ncon);
    int i = md->ncon - 1;
    md->con[i] = *c0;
    md->con[i].hnext = md->con_ht[h];
    md->con_ht[h] = i;
    md->con_n++;
    return CON(i, NULL);
}

Ref getcon(int64_t val, Module *md) {
    Con c0 = {.type = CBits, .bits.i = val};
    if (!md->con_ht)
        con_rehash(md, 64);
    else if (md->con_n >= md->con_cap * 2)
        con_rehash(md, md->con_cap * 2);

    int h = con_hash(&c0) & (md->con_cap - 1);
    for (int ci = md->con_ht[h]; ci >= 0; ci = md->con[ci].hnext)
        if (md->con[ci].type == CBits && md->con[ci].bits.i == val) return CON(ci, NULL);

    md->con = vgrow(md->con, ++md->ncon);
    int i = md->ncon - 1;
    md->con[i] = c0;
    md->con[i].hnext = md->con_ht[h];
    md->con_ht[h] = i;
    md->con_n++;
    return CON(i, NULL);
}
