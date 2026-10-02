#include "cxx.h"

struct tm *tm;

enum {
    P_INCLUDE,
    P_INCLUDE_NEXT,
    P_EMBED,
    P_IF,
    P_IFDEF,
    P_IFNDEF,
    P_ELIF,
    P_ELIFDEF,
    P_ELIFNDEF,
    P_ELSE,
    P_ENDIF,
    P_DEFINE,
    P_UNDEF,
    P_ERROR,
    P_WARNING,
    P_LINE,
    P_PRAGMA,
    P_CNT,
};

static struct {
    char *directive;
    uint32_t id;
} dt[] = {
    [P_INCLUDE] = {"include", 0},   [P_INCLUDE_NEXT] = {"include_next", 0},
    [P_EMBED] = {"embed", 0},       [P_IF] = {"if", 0},
    [P_IFDEF] = {"ifdef", 0},       [P_IFNDEF] = {"ifndef", 0},
    [P_ELIF] = {"elif", 0},         [P_ELIFDEF] = {"elifdef", 0},
    [P_ELIFNDEF] = {"elifndef", 0}, [P_ELSE] = {"else", 0},
    [P_ENDIF] = {"endif", 0},       [P_DEFINE] = {"define", 0},
    [P_UNDEF] = {"undef", 0},       [P_ERROR] = {"error", 0},
    [P_WARNING] = {"warning", 0},   [P_LINE] = {"line", 0},
    [P_PRAGMA] = {"pragma", 0},
};

enum {
    EMBED_LIMIT = 1 << 0,
    EMBED_PREFIX = 1 << 1,
    EMBED_SUFFIX = 1 << 2,
    EMBED_IF_EMPTY = 1 << 3,
};

// Embed parameter names. The plain and underscored spellings name the
// same parameter (6.10.3.1); ids are interned in init_preprocess.
static struct {
    char *name;
    uint32_t id;
    uint32_t bit;
} embed_params[] = {
    {"limit", 0, EMBED_LIMIT},       {"__limit__", 0, EMBED_LIMIT},       {"prefix", 0, EMBED_PREFIX},
    {"__prefix__", 0, EMBED_PREFIX}, {"suffix", 0, EMBED_SUFFIX},         {"__suffix__", 0, EMBED_SUFFIX},
    {"if_empty", 0, EMBED_IF_EMPTY}, {"__if_empty__", 0, EMBED_IF_EMPTY},
};

static uint32_t true_id;
static uint32_t defined_id;
static uint32_t vaarg_id;
static uint32_t vaopt_id;
static uint32_t once_id;
static uint32_t has_include_id;
static uint32_t has_include_next_id;
static uint32_t has_embed_id;
static uint32_t has_c_attribute_id;
static uint32_t pragma_op_id;

static Token *expand_macro(Token *dst, Token *list);
static char *join_tokens(Token *tok);
static char *search_include_paths(char *filename);
static char *read_filename(Token **rest, Token *tok, bool *is_dquote, bool to_eol);
static char *read_embed_filename(Token **rest, Token *tok, bool *is_dquote);
static Token *read_embed_param(Token **tok, Token *lp);
static char *resolve_embed_path(Token *tok, char *filename, bool is_dquote);
static int64_t eval_const_tokens(Token *expr);
static uint32_t check_embed_param(Token *tok, uint32_t seen, bool err_unknown);
typedef struct Macro Macro;
static Macro *find_macro(Token *tok);

static Token *filter_tokens(Token *tok) {
    Token dummy = {}, *cur = &dummy;
    for (; tok; tok = tok->next) {
        if (tok->kind == TK_WS) continue;
        if (tok->kind == TK_NL) continue;
        if (tok->kind == TK_COMMENT) continue;
        cur = cur->next = tok;
    }
    return dummy.next;
}

// `#if` can be nested, so we use a stack to manage nested `#if`s.
typedef enum {
    BLOCK_DEAD,     // this block is dead
    BLOCK_PENDING,  // this block is pending for #elif/#else
    BLOCK_ACTIVE,   // this block is activing
} BlockState;

typedef struct CondIncl CondIncl;
struct CondIncl {
    CondIncl *next;
    Token *if_tok;
    BlockState state;
    int else_seen;
};

static CondIncl *cond_incl;
static CondIncl *push_cond_incl(Token *tok, BlockState state) {
    CondIncl *ci = emalloc(sizeof(CondIncl));
    ci->state = state;
    ci->if_tok = tok;

    ci->next = cond_incl;
    cond_incl = ci;
    return ci;
}

Token *guard_macro;
// `#include` can be nested, so we use a stack to manage nested `#include`s.
typedef struct FileStack FileStack;
struct FileStack {
    int search_idx;
    int line_delta;
    uint32_t display_name;
    CondIncl *condframe;
    Token *guard_macro;
    Token *rest;
};

static int cur_path;
static int next_path;
static int line_delta;
static uint32_t display_name;
static FileStack *file_stack[256];
static int include_depth;
#define MAX_INCL_DEPTH 200

#define STR1(x) #x
#define STR(x) STR1(x)

static FileStack *push_file(Token *rest, SrcFile *file) {
    if (include_depth >= MAX_INCL_DEPTH) error(rest, "include nesting too deep, MAX_DEPTH = " STR(MAX_INCL_DEPTH));
#undef STR
#undef STR1

    FileStack *fs = emalloc(sizeof(FileStack));
    fs->search_idx = cur_path;
    fs->line_delta = line_delta;
    fs->display_name = display_name;
    fs->condframe = cond_incl;
    fs->guard_macro = guard_macro;
    fs->rest = rest;

    cur_path = next_path;
    line_delta = 0;
    display_name = file->id;
    cond_incl = NULL;
    guard_macro = NULL;
    push_cond_incl(NULL, BLOCK_ACTIVE);

    file_stack[include_depth++] = fs;

    return fs;
}

static Token *pop_file(void) {
    include_depth--;
    FileStack *file = file_stack[include_depth];

    cur_path = file->search_idx;
    cond_incl = file->condframe;
    guard_macro = file->guard_macro;
    line_delta = file->line_delta;
    display_name = file->display_name;

    return file->rest;
}

static bool is_hash(Token *tok) { return tok->is_sol && tok->kind == TK_HASH; }

// Some preprocessor directives such as #include allow extraneous
// tokens before newline. This function skips such tokens.
static Token *skip_line(Token *tok) {
    if (tok->is_sol) return tok;

    tok->line_delta = line_delta;
    tok->filename = display_name;
    warning(tok, "extra token");
    while (!tok->is_sol) tok = tok->next;
    return tok;
}

static Token *copy_token(Token *tok) {
    Token *t = emalloc(sizeof(Token));
    *t = *tok;
    t->next = NULL;
    return t;
}

static Token *new_eof(Token *tok) {
    Token *t = copy_token(tok);
    t->kind = TK_EOF;
    t->len = 0;
    t->is_sol = true;
    return t;
}

// Double-quote a given string and returns it.
static char *quote_string(char *str) {
    int bufsize = 3;
    for (int i = 0; str[i]; i++) {
        if (str[i] == '\\' || str[i] == '"') bufsize++;
        bufsize++;
    }

    char *buf = emalloc(bufsize);
    char *p = buf;
    *p++ = '"';
    for (int i = 0; str[i]; i++) {
        if (str[i] == '\\' || str[i] == '"') *p++ = '\\';
        *p++ = str[i];
    }
    *p++ = '"';
    *p++ = '\0';
    return buf;
}

static void write_scratch_space(Token *tok, char *str);
static Token *new_str_token(char *str, Token *tmpl) {
    Token *new = emalloc(sizeof(Token));
    int len = strlen(str);
    new->kind = TK_STRLIT;
    new->id = intern(str, len);
    char *q_str = quote_string(str);
    write_scratch_space(new, q_str);
    new->origin = tmpl;
    return new;
}

static Token *ident_to_num(Token *tok, int64_t val) {
    Token *new = emalloc(sizeof(Token));
    new->kind = TK_NUM;
    new->ival = int128_set_i(val);
    char *fmt = format("%ld", val);
    write_scratch_space(new, fmt);
    new->origin = tok;
    return new;
}

// Consume all tokens until a newline or EOF taking ownership of them.
// Terminate them with an EOF token and then returns them.
// Used for constructing macro bodies and #if, #error, and #warning directives.
static Token *read_line(Token **rest, Token *tok) {
    Token dummy = {};
    Token *cur = &dummy;

    for (; !tok->is_sol; tok = tok->next) {
        tok->line_delta = line_delta;
        tok->filename = display_name;
        cur = cur->next = tok;
    }

    tok->line_delta = line_delta;
    tok->filename = display_name;

    cur->next = new_eof(tok);
    *rest = tok;
    return dummy.next;
}

static Token *read_const_expr(Token **rest, Token *tok) {
    tok = read_line(rest, tok);

    Token dummy = {};
    Token *cur = &dummy;

    while (tok) {
        // "defined(foo)" or "defined foo" becomes "1" if macro "foo"
        // is defined. Otherwise "0".
        if (tok->kind == TK_IDENT && tok->id == defined_id) {
            Token *start = tok;
            bool has_paren = match(&tok, tok->next, TK_LPAREN);

            if (tok->kind != TK_IDENT) error(start, "operator \"defined\" requires an identifier");
            Macro *m = find_macro(tok);
            tok = tok->next;

            if (has_paren) tok = skip(tok, TK_RPAREN);

            cur = cur->next = ident_to_num(start, m ? 1 : 0);
            continue;
        }

        cur = cur->next = tok;
        tok = tok->next;
    }

    return dummy.next;
}

static bool exist_include(Token *tok, char *filename, bool is_dquote) {
    Token *orig = tok;
    while (orig->origin) orig = orig->origin;

    if (filename[0] != '/' && is_dquote) {
        char *path = format("%s/%s", dirname(strdup(orig->file->name)), filename);
        if (file_exists(path)) return true;
    }

    char *path = search_include_paths(filename);
    return file_exists(path ? path : filename);
}

static bool exist_include_next(char *filename);

static Token *eval_has_include(Token *tok) {
    Token dummy = {};
    Token *cur = &dummy;
    while (tok) {
        // __has_include[_next]("file") or __has_include[_next](<file>)
        // becomes "1" if the file can be included, otherwise "0".
        // The _next variant searches like #include_next: it skips the
        // directory of the current file.
        if (tok->kind == TK_IDENT && (tok->id == has_include_id || tok->id == has_include_next_id)) {
            Token *start = tok;
            bool is_next = tok->id == has_include_next_id;
            tok = skip(tok->next, TK_LPAREN);

            bool is_dquote = false;
            char *path = read_filename(&tok, tok, &is_dquote, false);
            bool exists = is_next ? exist_include_next(path) : exist_include(start, path, is_dquote);
            tok = skip(tok, TK_RPAREN);
            cur = cur->next = ident_to_num(start, exists ? 1 : 0);
            continue;
        }
        cur = cur->next = tok;
        tok = tok->next;
    }

    return dummy.next;
}

// __has_embed ( resource, params... ): the resource availability as an
// integer constant: __STDC_EMBED_NOT_FOUND__ (0) if the search fails
// or a parameter is unsupported, __STDC_EMBED_EMPTY__ (2) if the
// resource is empty, __STDC_EMBED_FOUND__ (1) otherwise (C23 6.10.1).
// `tok` must point at the opening '('.
static int64_t has_embed_result(Token **rest, Token *tok) {
    Token *lp = tok;
    tok = tok->next;
    bool is_dquote = false;
    char *filename = read_embed_filename(&tok, tok, &is_dquote);

    int64_t limit = -1;
    uint32_t seen = 0;
    bool ok = true;
    while (ok) {
        if (tok->kind == TK_RPAREN) break;
        if (tok->kind == TK_COMMA) {
            tok = tok->next;
            continue;
        }
        if (tok->kind != TK_IDENT) {
            ok = false;
            break;
        }
        Token *ptok = tok;
        uint32_t bit = check_embed_param(tok, seen, false);
        if (!bit) {
            ok = false;  // Unsupported parameter: not found.
            break;
        }
        seen |= bit;
        Token *plp = tok->next;
        if (!plp || plp->kind != TK_LPAREN) {
            ok = false;
            break;
        }
        Token *content = read_embed_param(&tok, plp);
        tok = tok->next;
        if (bit == EMBED_LIMIT) {
            limit = eval_const_tokens(content);
            if (limit < 0) error(ptok, "invalid value '%ld'; must be positive", limit);
        }
    }
    if (!ok) {
        // Skip the rest of the call, honoring nested parentheses.
        int depth = 1;
        for (; tok; tok = tok->next) {
            if (tok->kind == TK_LPAREN) depth++;
            if (tok->kind == TK_RPAREN && --depth == 0) break;
            if (tok->kind == TK_EOF || tok->is_sol) break;
        }
        if (tok && tok->kind == TK_RPAREN) tok = tok->next;
        *rest = tok;
        return 0;
    }
    tok = tok->next;  // the closing ')'
    *rest = tok;

    char *path = resolve_embed_path(lp, filename, is_dquote);
    if (!path) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return st.st_size == 0 ? 2 : 1;
}

static Token *eval_has_embed(Token *tok) {
    Token dummy = {};
    Token *cur = &dummy;
    while (tok) {
        if (tok->kind == TK_IDENT && tok->id == has_embed_id) {
            Token *start = tok;
            if (!tok->next || tok->next->kind != TK_LPAREN) error(tok, "missing '(' after '__has_embed'");
            int64_t result = has_embed_result(&tok, tok->next);
            cur = cur->next = ident_to_num(start, result);
            continue;
        }
        cur = cur->next = tok;
        tok = tok->next;
    }

    return dummy.next;
}

// __has_c_attribute ( tokens ): attribute support is not implemented
// yet, so this stub always yields 0 (C23 6.10.1). The parenthesized
// token sequence is consumed without evaluation.
static Token *eval_has_c_attribute(Token *tok) {
    Token dummy = {};
    Token *cur = &dummy;
    while (tok) {
        if (tok->kind == TK_IDENT && tok->id == has_c_attribute_id) {
            Token *start = tok;
            if (!tok->next || tok->next->kind != TK_LPAREN) error(tok, "missing '(' after '__has_c_attribute'");
            int depth = 1;
            for (tok = tok->next->next; tok; tok = tok->next) {
                if (tok->kind == TK_LPAREN) depth++;
                if (tok->kind == TK_RPAREN && --depth == 0) {
                    tok = tok->next;
                    break;
                }
                if (tok->kind == TK_EOF || tok->is_sol) error(start, "missing ')' after '__has_c_attribute'");
            }
            cur = cur->next = ident_to_num(start, 0);
            continue;
        }
        cur = cur->next = tok;
        tok = tok->next;
    }

    return dummy.next;
}

// Evaluate a constant expression from a NULL-terminated token list
// (used by #if and by the #embed limit parameter).
static int64_t eval_const_tokens(Token *expr) {
    Token dummy = {}, *cur = &dummy;
    cur = expand_macro(cur, expr);
    cur->next = new_eof(cur);
    expr = dummy.next;
    expr = eval_has_include(expr);
    expr = eval_has_embed(expr);
    expr = eval_has_c_attribute(expr);

    // we replace remaining non-macro identifiers with "0"
    Token dummy2 = {};
    cur = &dummy2;
    for (Token *t = expr; t; t = t->next) {
        if (t->kind == TK_IDENT)
            cur = cur->next = ident_to_num(t, t->id == true_id ? 1 : 0);
        else
            cur = cur->next = t;
    }
    expr = dummy2.next;

    convert_ppnumber(expr);
    // 6.10.1: in #if, all integer types act as intmax_t/uintmax_t
    // (64 bits here); truncate _BitInt values as-if converted to
    // uintmax_t before the parser's checked const_expr sees them.
    for (Token *t = expr; t->kind != TK_EOF; t = t->next)
        if (t->kind == TK_NUM && (t->lit_suffix & SUF_BITINT)) t->ival = int128_normalize(t->ival, 64, UNSIGNED);
    // No floating constant of any kind is allowed in #if (gcc/clang
    // reject them all: float/double/long double and the interchange
    // _Float16/32/64/128).
    for (Token *t = expr; t->kind != TK_EOF; t = t->next)
        if (t->kind == TK_NUM &&
            t->lit_suffix & (SUF_FLOAT | SUF_DOUBLE | SUF_LDOUBLE | SUF_F16 | SUF_F32 | SUF_F64 | SUF_F128))
            error(t, "floating point literal in preprocessor expression");

    Token *rest2;
    int64_t val = const_expr(&rest2, expr);
    if (rest2->kind != TK_EOF)
        error(rest2, "missing binary operator before token \"%.*s\"", rest2->len, tok_text(rest2));
    return val;
}

static int64_t eval_const_expr(Token **rest, Token *tok) {
    Token *start = tok;
    Token *expr = read_const_expr(rest, tok->next);
    if (expr->kind == TK_EOF) error(start, "no expression in #%s", str(start->id));
    return eval_const_tokens(expr);
}

// check #elif / #else valid
static void check_elif_else_valid(Token *dt) {
    if (!cond_incl->next) error(dt, "%s without #if", str(dt->id));
    if (cond_incl->else_seen) {
        diag("error", dt, "%s after #else", str(dt->id));
        error(cond_incl->if_tok, "the conditional began here");
    }
}

typedef struct MacroParam MacroParam;
struct MacroParam {
    MacroParam *next;
    uint32_t id;
};

typedef struct MacroArg MacroArg;
struct MacroArg {
    MacroArg *next;
    uint32_t id;
    bool is_va_args;
    Token *tok;
};

typedef Token *macro_handler_fn(Token **, Token *);
struct Macro {
    Macro *next;
    Macro *hnext;  // hash chain link
    uint32_t id;
    bool deleted;
    bool is_builtin;
    bool is_objlike;  // Object-like or function-like
    bool is_variadic;
    uint32_t va_args_id;
    MacroParam *params;
    Token *body;
    macro_handler_fn *handler;
};

static Macro *macros;

typedef struct Hideset Hideset;
struct Hideset {
    Hideset *next;
    uint32_t id;
};

static Hideset *hideset;

static bool is_disabled(uint32_t id) {
    for (Hideset *hs = hideset; hs; hs = hs->next)
        if (hs->id == id) return true;
    return false;
}

static void push_disabled(uint32_t id) {
    Hideset *hs = emalloc(sizeof(Hideset));
    hs->id = id;
    hs->next = hideset;
    hideset = hs;
}

static void pop_disabled() { hideset = hideset->next; }

// Hash over macros (chain addressing): lookup stays O(1) even for
// headers that define thousands of macros.
static Macro **macro_ht;
static int macro_cap;
static int macro_n;

static Macro *find_macro(Token *tok) {
    if (tok->kind != TK_IDENT) {
        tok->line_delta = line_delta;
        tok->filename = display_name;
        error(tok, "macro name must be an identifier");
    }
    if (macro_ht)
        for (Macro *m = macro_ht[tok->id & (macro_cap - 1)]; m; m = m->hnext)
            if (m->id == tok->id) return m->deleted ? NULL : m;
    return NULL;
}

static Macro *add_macro(uint32_t id, bool is_objlike, Token *body) {
    Macro *m = emalloc(sizeof(Macro));
    m->id = id;
    m->is_objlike = is_objlike;
    m->body = body;

    m->next = macros;
    macros = m;

    if (!macro_ht) {
        macro_cap = 256;
        macro_ht = vnew(macro_cap, sizeof(Macro *));
    } else if (macro_n >= macro_cap * 2) {
        int cap = macro_cap * 2;
        Macro **ht = vnew(cap, sizeof(Macro *));
        Macro **tail = vnew(cap, sizeof(Macro *));
        for (int i = 0; i < cap; i++) ht[i] = tail[i] = NULL;
        for (int i = 0; i < macro_cap; i++)
            for (Macro *x = macro_ht[i]; x;) {
                Macro *next = x->hnext;  // saved before the link is rewired
                int h = x->id & (cap - 1);
                // tail-insert keeps the chain order: redefinitions must
                // keep winning over their predecessors
                x->hnext = NULL;
                if (tail[h])
                    tail[h]->hnext = x;
                else
                    ht[h] = x;
                tail[h] = x;
                x = next;
            }
        macro_ht = ht;
        macro_cap = cap;
    }
    int h = id & (macro_cap - 1);
    m->hnext = macro_ht[h];
    macro_ht[h] = m;
    macro_n++;
    return m;
}

static MacroParam *read_macro_params(Token **rest, Token *tok, bool *is_variadic, uint32_t *va_args_id) {
    MacroParam dummy = {};
    MacroParam *cur = &dummy;

    while (tok->kind != TK_RPAREN) {
        if (cur != &dummy) tok = skip(tok, TK_COMMA);
        if (tok->kind == TK_ELLIPSIS) {
            *is_variadic = true;
            *va_args_id = vaarg_id;
            *rest = skip(tok->next, TK_RPAREN);
            return dummy.next;
        }
        if (tok->kind != TK_IDENT) error(tok, " expected parameter name");
        for (MacroParam *p = dummy.next; p; p = p->next)
            if (p->id == tok->id) error(tok, "duplicate macro parameter \"%s\"", str(tok->id));

        if (tok->next->kind == TK_ELLIPSIS) {
            *is_variadic = true;
            *va_args_id = tok->id;
            *rest = skip(tok->next->next, TK_RPAREN);
            return dummy.next;
        }

        MacroParam *m = emalloc(sizeof(MacroParam));
        m->id = tok->id;
        cur = cur->next = m;
        tok = tok->next;
    }
    *rest = tok->next;
    return dummy.next;
}

static void read_macro_definition(Token **rest, Token *tok) {
    tok = read_line(rest, tok);

    if (tok->kind != TK_IDENT) error(tok, "macro name must be an identifier");
    if (tok->id == defined_id) error(tok, "'defined' cannot be used as a macro name");
    Macro *exist = find_macro(tok);
    if (exist && exist->is_builtin) warning(tok, "redefining builtin macro");

    Token *name = tok;
    tok = tok->next;

    if (tok->kind == TK_LPAREN && !tok->is_leadingws) {
        // Function-like macro
        bool is_variadic = false;
        uint32_t va_args_id = 0;
        MacroParam *params = read_macro_params(&tok, tok->next, &is_variadic, &va_args_id);
        if (!tok->is_sol && !tok->is_leadingws) warning(tok, "ISO C99 requires whitespace after the macro name");
        Macro *m = add_macro(name->id, false, tok);
        m->params = params;
        m->is_variadic = is_variadic;
        m->va_args_id = va_args_id;
    } else {
        // Object-like macro
        if (!tok->is_sol && !tok->is_leadingws) warning(tok, "ISO C99 requires whitespace after the macro name");
        add_macro(name->id, true, tok);
    }
}

static MacroArg *read_macro_arg_one(Token **rest, Token *tok, bool read_rest) {
    Token dummy = {};
    Token *cur = &dummy;
    int level = 0;

    while (1) {
        if (level == 0 && tok->kind == TK_RPAREN) break;
        if (level == 0 && tok->kind == TK_COMMA && !read_rest) break;

        if (tok->kind == TK_EOF) error(tok, "premature end of input");

        if (tok->kind == TK_LPAREN)
            level++;
        else if (tok->kind == TK_RPAREN)
            level--;
        cur = cur->next = copy_token(tok);
        tok = tok->next;
    }

    cur->next = new_eof(tok);

    MacroArg *arg = emalloc(sizeof(MacroArg));
    arg->tok = dummy.next;
    *rest = tok;
    return arg;
}

static MacroArg *read_macro_args(Token **rest, Token *tok, MacroParam *params, bool is_variadic, uint32_t va_args_id) {
    tok = tok->next->next;

    MacroArg dummy = {};
    MacroArg *cur = &dummy;

    MacroParam *pp = params;
    for (; pp; pp = pp->next) {
        if (cur != &dummy) tok = skip(tok, TK_COMMA);
        cur = cur->next = read_macro_arg_one(&tok, tok, false);
        cur->id = pp->id;
    }

    if (is_variadic) {
        MacroArg *arg;
        if (tok->kind == TK_RPAREN) {
            arg = emalloc(sizeof(MacroArg));
            arg->tok = new_eof(tok);
        } else {
            if (pp != params) tok = skip(tok, TK_COMMA);
            arg = read_macro_arg_one(&tok, tok, true);
        }

        arg->id = va_args_id;
        arg->is_va_args = true;
        cur = cur->next = arg;
    } else if (tok->kind != TK_RPAREN) {
        error(tok, "too many arguments");
    }
    *rest = skip(tok, TK_RPAREN);
    return dummy.next;
}

static MacroArg *find_arg(MacroArg *args, Token *tok) {
    for (MacroArg *ap = args; ap; ap = ap->next)
        if (ap->id == tok->id) return ap;
    return NULL;
}

// Concatenates all tokens in `tok` and returns a new string.
static char *join_tokens(Token *tok) {
    // Compute the length of the resulting token.
    int len = 1;
    for (Token *t = tok; t && t->kind != TK_EOF; t = t->next) {
        if (t != tok && t->is_leadingws) len++;
        len += t->len;
    }

    char *buf = emalloc(len);

    // Copy token texts.
    int pos = 0;
    for (Token *t = tok; t && t->kind != TK_EOF; t = t->next) {
        if (t != tok && t->is_leadingws) buf[pos++] = ' ';
        strncpy(buf + pos, tok_text(t), t->len);
        pos += t->len;
    }
    buf[pos] = '\0';
    return buf;
}

// Concatenates all tokens in `arg` and returns a new string token.
// This function is used for the stringizing operator (#).
static Token *stringize(Token *arg) {
    // Create a new string token. We need to set some value to its
    // source location for error reporting function, so we use a macro
    // name token as a template.
    char *s = join_tokens(arg);
    return new_str_token(s, arg);
}

// Concatenate two tokens to create a new token.
static Token *paste(Token *lhs, Token *rhs) {
    // Paste the two tokens.
    char *buf = format("%.*s%.*s", lhs->len, tok_text(lhs), rhs->len, tok_text(rhs));

    // Tokenize the resulting string.
    SrcFile *file = new_file(lhs->file->name, lhs->file->file_no, buf);
    Token *tok = tokenize(file);
    if (tok->next->kind != TK_EOF) error(lhs, "pasting forms '%s', an invalid token", buf);

    // Inherit source location and whitespace flag from lhs.
    tok->is_sol = false;
    tok->is_leadingws = lhs->is_leadingws;
    return tok;
}

static bool has_varargs(MacroArg *args) {
    for (MacroArg *ap = args; ap; ap = ap->next)
        if (ap->id == vaarg_id) return ap->tok->kind != TK_EOF;
    return false;
}

// Replace func-like macro parameters with given arguments.
static Token *subst(Token *tok, MacroArg *args) {
    Token dummy = {};
    Token *cur = &dummy;

    while (tok->kind != TK_EOF) {
        // "#" followed by a parameter is replaced with stringized actuals.
        if (tok->kind == TK_HASH) {
            MacroArg *arg = find_arg(args, tok->next);
            if (!arg) error(tok->next, "'#' is not followed by a macro parameter");
            cur = cur->next = stringize(arg->tok);
            tok = tok->next->next;
            continue;
        }

        // [GNU] If __VA_ARG__ is empty, `,##__VA_ARGS__` is expanded
        // to the empty token list. Otherwise, its expaned to `,` and
        // __VA_ARGS__.
        if (tok->kind == TK_COMMA && tok->next->kind == TK_HASHHASH) {
            MacroArg *arg = find_arg(args, tok->next->next);
            if (arg && arg->is_va_args) {
                if (arg->tok->kind == TK_EOF) {
                    tok = tok->next->next->next;
                } else {
                    cur = cur->next = copy_token(tok);
                    tok = tok->next->next;
                }
                continue;
            }
        }

        if (tok->kind == TK_HASHHASH) {
            if (cur == &dummy) error(tok, "'##' cannot appear at start of macro expansion");

            if (tok->next->kind == TK_EOF) error(tok, "'##' cannot appear at end of macro expansion");

            MacroArg *arg = find_arg(args, tok->next);
            if (arg) {
                if (arg->tok->kind != TK_EOF) {
                    *cur = *paste(cur, arg->tok);
                    for (Token *t = arg->tok->next; t->kind != TK_EOF; t = t->next) cur = cur->next = copy_token(t);
                }
                tok = tok->next->next;
                continue;
            }

            *cur = *paste(cur, tok->next);
            tok = tok->next->next;
            continue;
        }

        MacroArg *arg = find_arg(args, tok);

        if (arg && tok->next->kind == TK_HASHHASH) {
            Token *rhs = tok->next->next;

            if (arg->tok->kind == TK_EOF) {
                MacroArg *arg2 = find_arg(args, rhs);
                if (arg2) {
                    for (Token *t = arg2->tok; t->kind != TK_EOF; t = t->next) cur = cur->next = copy_token(t);
                } else {
                    cur = cur->next = copy_token(rhs);
                }
                tok = rhs->next;
                continue;
            }

            for (Token *t = arg->tok; t->kind != TK_EOF; t = t->next) cur = cur->next = copy_token(t);
            tok = tok->next;
            continue;
        }

        // If __VA_ARG__ is empty, __VA_OPT__(x) is expanded to the
        // empty token list. Otherwise, __VA_OPT__(x) is expanded to x.
        if (tok->id == vaopt_id && tok->next->kind == TK_LPAREN) {
            MacroArg *arg = read_macro_arg_one(&tok, tok->next->next, true);
            if (has_varargs(args))
                for (Token *t = arg->tok; t->kind != TK_EOF; t = t->next) cur = cur->next = t;
            tok = skip(tok, TK_RPAREN);
            continue;
        }

        // Expand the argument and mark the resulting tokens so they
        // are not expanded again during the body re-scan.
        // The first token inherits is_leadingws from the parameter token
        // in the macro body (since it conceptually "replaces" it there).
        if (arg) {
            Token dummy2 = {};
            expand_macro(&dummy2, arg->tok);
            bool first = true;
            for (Token *t = dummy2.next; t; t = t->next) {
                cur = cur->next = copy_token(t);
                cur->noexpand = true;
                if (first) {
                    cur->is_leadingws = tok->is_leadingws;
                    first = false;
                }
            }
            tok = tok->next;
            continue;
        }

        // Handle a non-macro token.
        cur = cur->next = copy_token(tok);
        tok = tok->next;
        continue;
    }

    cur->next = tok;
    return dummy.next;
}

// Recursively expand the input linked‑list macro,
// link it to the pointer specified by the argument,
// and terminate the returned linked list with a NULL tail.
static Token *expand_macro(Token *dst, Token *list) {
    Token *cur = list;
    while (cur && cur->kind != TK_EOF) {
        if (cur->kind != TK_IDENT) {
            dst = dst->next = copy_token(cur);
            cur = cur->next;
            continue;
        }
        if (is_disabled(cur->id) || cur->noexpand) {
            dst = dst->next = copy_token(cur);
            cur = cur->next;
            continue;
        }

        Macro *m = find_macro(cur);
        if (!m) {
            dst = dst->next = copy_token(cur);
            cur = cur->next;
            continue;
        }

        Token *macro_name = cur;
        // Built-in dynamic macro application such as __LINE__
        if (m->handler) {
            Token *t = m->handler(&cur, macro_name);
            t->is_leadingws = macro_name->is_leadingws;
            t->is_sol = macro_name->is_sol;
            dst = dst->next = t;
            continue;
        }

        // Object-like macro application
        if (m->is_objlike) {
            Token *prev = dst;
            push_disabled(cur->id);
            dst = expand_macro(dst, m->body);
            pop_disabled();
            if (prev->next) {
                prev->next->is_leadingws = macro_name->is_leadingws;
                prev->next->is_sol = macro_name->is_sol;
            }
            for (Token *t = prev->next; t && t->kind != TK_EOF; t = t->next) t->origin = macro_name;
            cur = cur->next;
            continue;
        }

        // If a funclike macro token is not followed by an argument list,
        // treat it as a normal identifier.
        if (!cur->next || cur->next->kind != TK_LPAREN) {
            dst = dst->next = copy_token(cur);
            cur = cur->next;
            continue;
        }

        // Function-like macro application
        MacroArg *args = read_macro_args(&cur, cur, m->params, m->is_variadic, m->va_args_id);
        Token *sub = subst(m->body, args);
        for (Token *t = sub; t && t->kind != TK_EOF; t = t->next) t->origin = macro_name;

        Token *prev = dst;
        push_disabled(m->id);
        dst = expand_macro(dst, sub);
        pop_disabled();
        if (prev->next) {
            prev->next->is_leadingws = macro_name->is_leadingws;
            prev->next->is_sol = macro_name->is_sol;
        }

        continue;
    }
    dst->next = NULL;
    return dst;
}

static char *search_include_paths(char *filename) {
    if (filename[0] == '/') {
        next_path = 0;
        return filename;
    }

    uint32_t file_id = intern(filename, strlen(filename));

    static struct {
        int idx;
        uint32_t file_id;
        char *path;
    } *cache;
    static int num_cache;

    for (int i = 0; i < num_cache; i++)
        if (cache[i].file_id == file_id) {
            next_path = cache[i].idx;
            return cache[i].path;
        }

    if (!cache)
        cache = vnew(16, sizeof(cache[0]));
    else
        cache = vgrow(cache, num_cache + 1);

    // Search a file from the include paths.
    for (int i = 0; i < num_include_paths; i++) {
        char *path = format("%s/%s", include_paths[i], filename);
        if (!file_exists(path)) continue;
        cache[num_cache].file_id = file_id;
        cache[num_cache].path = path;
        cache[num_cache++].idx = i + 1;
        next_path = i + 1;
        return path;
    }

    return NULL;
}

static char *search_include_next(char *filename) {
    for (int i = cur_path; i < num_include_paths; i++) {
        char *path = format("%s/%s", include_paths[i], filename);
        if (file_exists(path)) return path;
    }
    return NULL;
}

static bool exist_include_next(char *filename) {
    char *path = search_include_next(filename);
    return file_exists(path ? path : filename);
}

// Read an #include argument.
// Read a filename/resource: a string literal or a <...> header-name
// sequence. If to_eol, the directive ends at the newline and *rest is
// that newline token (extra tokens are diagnosed); otherwise *rest is
// the token after the filename (embed parameters or a closing ')').
static char *read_filename(Token **rest, Token *tok, bool *is_dquote, bool to_eol) {
    if (tok->kind == TK_STRLIT && tok->enc_prefix == PREFIX_NONE) {
        *is_dquote = true;
        *rest = to_eol ? skip_line(tok->next) : tok->next;
        return strndup(tok_text(tok) + 1, tok->len - 2);
    }

    if (tok->kind == TK_LT) {
        // Reconstruct a filename from the tokens between "<" and ">".
        Token *start = tok;
        for (; tok->kind != TK_GT; tok = tok->next)
            if (tok->is_sol || tok->kind == TK_EOF) {
                start->line_delta = line_delta;
                start->filename = display_name;
                error(start, "expected '>' to match this '<'");
            }
        *is_dquote = false;
        // In place, so that the token list stays intact for the caller
        // to continue parsing after the filename.
        tok->kind = TK_EOF;
        *rest = to_eol ? skip_line(tok->next) : tok->next;
        return join_tokens(start->next);
    }

    tok->line_delta = line_delta;
    tok->filename = display_name;
    error(tok, "expected \"FILENAME\" or <FILENAME>");
    return NULL;
}

static char *read_include_filename(Token **rest, Token *tok, bool *is_dquote) {
    // #include FOO: FOO must macro-expand to either a single string
    // token or a sequence of "<" ... ">".
    if (tok->kind == TK_IDENT) {
        Token dummy = {}, *cur = &dummy;
        cur = expand_macro(cur, read_line(rest, tok));
        cur->next = new_eof(cur);
        return read_include_filename(&cur, dummy.next, is_dquote);
    }
    return read_filename(rest, tok, is_dquote, true);
}

// #embed resource: "q-char-sequence" or <h-char-sequence>, without
// consuming the following embed parameters.
static char *read_embed_filename(Token **rest, Token *tok, bool *is_dquote) {
    return read_filename(rest, tok, is_dquote, false);
}

// Collect the tokens inside a parenthesized #embed parameter. *tok must
// point at the opening '('; on return *tok is the matching ')'.
static Token *read_embed_param(Token **tok, Token *lp) {
    int depth = 1;
    Token dummy = {};
    Token *cur = &dummy;
    Token *t = lp->next;
    for (;;) {
        if (!t || t->kind == TK_EOF || t->is_sol) error(lp, "expected ')'");
        if (t->kind == TK_LPAREN) depth++;
        if (t->kind == TK_RPAREN && --depth == 0) break;
        cur = cur->next = t;
        t = t->next;
    }
    cur->next = NULL;
    *tok = t;
    return dummy.next;
}

// #embed resource: a string literal or header-name, or the pp-tokens
// form macro-expanded and retried against the other two. Returns the
// filename; *rest is the first token after the resource and *eol the
// directive's newline token (NULL for the direct forms).
static char *read_embed_resource(Token **rest, Token *tok, bool *is_dquote, Token **eol) {
    if (tok->kind != TK_STRLIT && tok->kind != TK_LT) {
        Token *line = read_line(&tok, tok);
        *eol = tok;
        Token dummy0 = {}, *c0 = &dummy0;
        c0 = expand_macro(c0, line);
        tok = dummy0.next;
    }
    return read_embed_filename(rest, tok, is_dquote);
}

// Map an embed parameter name to its bit, 0 for unknown names.
static uint32_t embed_param_bit(uint32_t pid) {
    for (size_t i = 0; i < sizeof(embed_params) / sizeof(embed_params[0]); i++) {
        if (pid == embed_params[i].id) return embed_params[i].bit;
    }
    return 0;
}

// Validate an embed parameter name: unknown names are diagnosed only if
// err_unknown (__has_embed treats them as "not found" instead), and
// duplicates are always diagnosed. Returns the parameter bit.
static uint32_t check_embed_param(Token *tok, uint32_t seen, bool err_unknown) {
    uint32_t bit = embed_param_bit(tok->id);
    if (!bit) {
        if (err_unknown) error(tok, "unknown embed preprocessor parameter '%s'", str(tok->id));
        return 0;
    }
    if (seen & bit) error(tok, "cannot specify parameter '%s' twice in the same '#embed' directive", str(tok->id));
    return bit;
}

// #embed parameters: limit / prefix / suffix / if_empty, each with a
// parenthesized token list, at most once. *rest is the directive's
// newline token.
static void read_embed_params(Token **rest, Token *arg, Token *eol, int64_t *limit, Token **prefix, Token **suffix,
                              Token **if_empty) {
    *limit = -1;
    *prefix = *suffix = *if_empty = NULL;
    uint32_t seen = 0;
    while (arg && arg->kind != TK_EOF && !arg->is_sol) {
        if (arg->kind != TK_IDENT) error(arg, "unknown embed preprocessor parameter");
        uint32_t bit = check_embed_param(arg, seen, true);
        seen |= bit;
        Token *lp = arg->next;
        if (!lp || lp->kind != TK_LPAREN)
            error(arg, "embed parameter '%s' requires a parenthesized list", str(arg->id));
        Token *content = read_embed_param(&arg, lp);
        arg = arg->next;
        if (bit == EMBED_LIMIT) {
            *limit = eval_const_tokens(content);
            if (*limit < 0) error(lp, "invalid value '%ld'; must be positive", *limit);
        } else if (bit == EMBED_PREFIX) {
            *prefix = content;
        } else if (bit == EMBED_SUFFIX) {
            *suffix = content;
        } else {
            *if_empty = content;
        }
    }
    *rest = eol ? eol : arg;
}

// Resolve an embed resource path (as in clang): absolute paths are used
// as-is; the quoted form checks the current file's directory, then the
// plain filename, then the -embed-dir directories; the angle form only
// the -embed-dir directories. Include paths are not searched. Returns
// NULL if the resource cannot be found.
static char *resolve_embed_path(Token *tok, char *filename, bool is_dquote) {
    if (filename[0] == '/') return filename;
    if (is_dquote) {
        char *path = format("%s/%s", dirname(strdup(tok->file->name)), filename);
        if (file_exists(path)) return path;
        if (file_exists(filename)) return filename;
    }
    for (int i = 0; i < num_embed_dirs; i++) {
        char *path = format("%s/%s", embed_dirs[i], filename);
        if (file_exists(path)) return path;
    }
    return NULL;
}

// Resolve and read the #embed resource bytes.
static unsigned char *read_embed_bytes(Token *tok, char *filename, bool is_dquote, long *fsize) {
    char *path = resolve_embed_path(tok, filename, is_dquote);
    if (!path) error(tok, "'%s' file not found", filename);

    FILE *fp = fopen(path, "rb");
    if (!fp) error(tok, "'%s' file not found", filename);
    fseek(fp, 0, SEEK_END);
    *fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char *data = emalloc(*fsize > 0 ? *fsize : 1);
    if (fread(data, 1, *fsize, fp) != (size_t)*fsize) error(tok, "cannot read embed file: %s", filename);
    fclose(fp);
    return data;
}

// Emit the #embed replacement into the output stream: if_empty replaces
// an empty resource entirely; otherwise prefix, the comma-separated
// byte values, and suffix (the parameter contents are macro-expanded).
static void emit_embed(Token **cur, Token *embed_tok, unsigned char *data, long fsize, int64_t limit, Token *prefix,
                       Token *suffix, Token *if_empty) {
    Token *start = *cur;
    Token *out = *cur;
    if (fsize == 0) {
        // An empty resource yields only if_empty (clang/gcc emit
        // nothing at all without it; prefix/suffix do not apply).
        if (if_empty) out = expand_macro(out, if_empty);
    } else {
        if (prefix) out = expand_macro(out, prefix);
        int64_t n = limit < 0 || limit > fsize ? fsize : limit;
        for (int64_t i = 0; i < n; i++) {
            // The replacement is a comma-separated list.
            if (i > 0) {
                Token *comma = emalloc(sizeof(Token));
                comma->kind = TK_COMMA;
                char *buf = format(",");
                write_scratch_space(comma, buf);
                comma->origin = embed_tok;
                out = out->next = comma;
            }
            out = out->next = ident_to_num(embed_tok, data[i]);
        }
        if (suffix) out = expand_macro(out, suffix);
    }
    // Mark the first emitted token as start-of-line so the -E printer
    // separates it from the preceding line marker.
    if (start->next) start->next->is_sol = true;
    *cur = out;
}

struct {
    uint32_t path;
    Token *macro;
} *guard;
int num_guard;

static void detect_include_guard1(Token *tok) {
    if (!is_hash(tok)) return;
    tok = tok->next;

    if (tok->kind != TK_IDENT || tok->id != dt[P_IFNDEF].id) return;
    tok = tok->next;

    if (tok->kind != TK_IDENT) return;
    Token *macro = tok;
    while (!tok->is_sol) tok = tok->next;
    if (!is_hash(tok)) return;
    tok = tok->next;

    if (tok->kind != TK_IDENT || tok->id != dt[P_DEFINE].id) return;
    tok = tok->next;
    if (tok->kind != TK_IDENT) return;
    if (tok->id != macro->id) return;

    guard_macro = macro;
}

static void detect_include_guard2(void) {
    if (!guard_macro || !find_macro(guard_macro)) return;
    if (!guard)
        guard = vnew(16, sizeof(guard[0]));
    else
        guard = vgrow(guard, num_guard + 1);
    guard[num_guard].path = guard_macro->file->id;
    guard[num_guard++].macro = guard_macro;
}

static uint32_t *pragma_path;
static int num_pragma;

static void add_pragma(Token *tok) {
    if (!pragma_path)
        pragma_path = vnew(16, sizeof(pragma_path[0]));
    else
        pragma_path = vgrow(pragma_path, num_pragma + 1);
    pragma_path[num_pragma++] = tok->file->id;
}

static bool find_pragma(uint32_t file_id) {
    for (int i = 0; i < num_pragma; i++)
        if (pragma_path[i] == file_id) return true;
    return false;
}

// _Pragma ( string-literal ): destringize the literal and process it as
// a #pragma directive (C99 6.10.9). The only pragma with an effect is
// "once"; anything else is ignored, like the #pragma directive.
static Token *process_pragma_op(Token *tok) {
    Token *lp = tok->next;
    if (!lp || lp->kind != TK_LPAREN) error(tok, "_Pragma takes a parenthesized string literal");
    Token *str = lp->next;
    if (!str || str->kind != TK_STRLIT || str->enc_prefix != PREFIX_NONE)
        error(tok, "_Pragma takes a parenthesized string literal");
    Token *rp = str->next;
    if (!rp || rp->kind != TK_RPAREN) error(tok, "_Pragma takes a parenthesized string literal");

    // Destringize: the content between the quotes, unescaping \\ and \".
    char *p = tok_text(str) + 1;
    char *buf = emalloc(str->len - 1);
    int n = 0;
    for (uint32_t i = 0; i < str->len - 2; i++) {
        char c = p[i];
        if (c == '\\' && i + 1 < str->len - 2 && (p[i + 1] == '\\' || p[i + 1] == '"')) c = p[++i];
        buf[n++] = c;
    }
    buf[n] = '\0';

    if (!strcmp(buf, "once")) add_pragma(str);
    return rp->next;
}

// Process and remove _Pragma operators from a NULL-terminated token
// list produced by macro expansion.
static Token *scan_pragma_op(Token *tok) {
    Token dummy = {};
    Token *cur = &dummy;
    while (tok) {
        if (tok->kind == TK_IDENT && tok->id == pragma_op_id) {
            tok = process_pragma_op(tok);
            continue;
        }
        cur = cur->next = tok;
        tok = tok->next;
    }
    return dummy.next;
}

static Token *new_linemarker(Token *tmpl, int line, uint32_t filename) {
    Token *linemarker = copy_token(tmpl);
    linemarker->kind = TK_LINE;
    linemarker->id = line;
    linemarker->filename = filename;
    linemarker->is_sol = true;
    return linemarker;
}

static Token *include_file(Token **rest, Token *tok, char *path, Token *filename_tok) {
    uint32_t file_id = intern(path, strlen(path));
    for (int i = 0; i < num_guard; i++)
        if (guard[i].path == file_id && find_macro(guard[i].macro)) return NULL;

    if (find_pragma(file_id)) return NULL;

    Token *tok2 = tokenize_file(path);
    tok2 = filter_tokens(tok2);
    if (!tok2) {
        filename_tok->line_delta = line_delta;
        filename_tok->filename = display_name;
        error(filename_tok, "%s: cannot open file: %s", path, strerror(errno));
    }
    push_file(tok, tok2->file);
    detect_include_guard1(tok2);
    *rest = tok2;

    int line, col;
    get_location(tok2->file, tok2->loc, &line, &col);
    return new_linemarker(tok2, line, display_name);
}

// Read #line arguments
static Token *read_line_marker(Token **rest, Token *tok) {
    Token *start = tok;
    tok = read_line(rest, tok->next);
    if (tok->kind != TK_PPNUM) error(start, "#line directive requires a positive integer argument");

    int line_no = 0;
    for (uint32_t i = 0; i < tok->len; i++) {
        char c = tok_text(tok)[i];
        if (isdigit(c))
            line_no = line_no * 10 + c - '0';
        else
            error(start, "line marker directive requires a simple digit sequence");
    }
    if (line_no == 0) error(start, "#line directive requires a positive integer argument");
    if (*tok_text(tok) == '0') error(start, "line marker directive interprets number as decimal, not octal");

    int line, col;
    get_location(start->file, start->loc, &line, &col);
    line_delta = line_no - line - 1;

    tok = tok->next;

    if (tok->kind != TK_EOF && (tok->kind != TK_STRLIT || tok->enc_prefix != PREFIX_NONE))
        error(tok, "filename expected");

    if (tok->kind == TK_STRLIT) {
        convert_str_literal(tok);
        display_name = tok->id;
    }

    return new_linemarker(start, line_no, display_name);
}

static void check_invalid_ident(Token *tok) {
    if (tok->id == has_include_id || tok->id == has_include_next_id || tok->id == has_embed_id ||
        tok->id == has_c_attribute_id)
        error(tok, "'%s' must be used within a preprocessing directive", str(tok->id));
}

// Visit all tokens in `tok` while evaluating preprocessing
// macros and directives.
static Token *preprocess2(Token *tok) {
    int line, col;
    Token dummy = {};
    Token *cur = &dummy;
    push_cond_incl(tok, BLOCK_ACTIVE);
    cur_path = 0;
    line_delta = 0;
    display_name = tok->file->id;

    cur = cur->next = new_linemarker(tok, 1, display_name);

    while (1) {
        if (tok->kind == TK_EOF) {
            if (cond_incl->next) error(cond_incl->if_tok, "unterminated conditional directive");
            if (include_depth > 0) {
                tok = pop_file();
                get_location(tok->file, tok->loc, &line, &col);
                cur = cur->next = new_linemarker(tok, line + line_delta, display_name);
                continue;
            } else {
                break;
            }
        }

        BlockState cur_state = cond_incl->state;

        // Concat token to buff until meet "#".
        if (!is_hash(tok)) {
            bool concat = (cur_state == BLOCK_ACTIVE);
            Token dummy2 = {}, *buf = &dummy2;
            while (!is_hash(tok) && tok->kind != TK_EOF) {
                if (concat) {
                    tok->line_delta = line_delta;
                    tok->filename = display_name;
                    if (tok->kind == TK_ERR) error(tok, "%s", tok->msg);
                    if (tok->kind == TK_WARN) warning(tok, "%s", tok->msg);
                    check_invalid_ident(tok);
                    buf = buf->next = tok;
                }
                tok = tok->next;
            }
            buf->next = new_eof(tok);
            Token *seg = cur;
            cur = expand_macro(cur, dummy2.next);
            if (seg->next) {
                seg->next = scan_pragma_op(seg->next);
                cur = seg;
                while (cur->next) cur = cur->next;
            }
            if (tok->kind == TK_EOF) continue;
        }

        tok->line_delta = line_delta;
        tok->filename = display_name;

        Token *tk_hash = tok;
        tok = tok->next;

        tok->line_delta = line_delta;
        tok->filename = display_name;

        // Preprocessing directives that may alter conditional‑frame state
        if (tok->id == dt[P_IF].id) {
            BlockState state = BLOCK_DEAD;
            if (cur_state == BLOCK_ACTIVE) {
                int64_t val = eval_const_expr(&tok, tok);
                state = val ? BLOCK_ACTIVE : BLOCK_PENDING;
            }
            push_cond_incl(tk_hash, state);
            continue;
        }
        if (tok->id == dt[P_IFDEF].id) {
            BlockState state = BLOCK_DEAD;
            if (cur_state == BLOCK_ACTIVE) {
                bool defined = find_macro(tok->next);
                state = defined ? BLOCK_ACTIVE : BLOCK_PENDING;
            }
            push_cond_incl(tk_hash, state);
            tok = skip_line(tok->next->next);
            continue;
        }

        if (tok->id == dt[P_IFNDEF].id) {
            BlockState state = BLOCK_DEAD;
            if (cur_state == BLOCK_ACTIVE) {
                bool defined = find_macro(tok->next);
                state = defined ? BLOCK_PENDING : BLOCK_ACTIVE;
            }
            push_cond_incl(tk_hash, state);
            tok = skip_line(tok->next->next);
            continue;
        }

        if (tok->id == dt[P_ELIF].id) {
            check_elif_else_valid(tok);
            if (cur_state != BLOCK_PENDING) {
                cond_incl->state = BLOCK_DEAD;
            } else {
                int64_t val = eval_const_expr(&tok, tok);
                cond_incl->state = val ? BLOCK_ACTIVE : BLOCK_PENDING;
            }
            continue;
        }

        if (tok->id == dt[P_ELIFDEF].id) {
            check_elif_else_valid(tok);
            if (cur_state != BLOCK_PENDING) {
                cond_incl->state = BLOCK_DEAD;
            } else {
                bool defined = find_macro(tok->next);
                cond_incl->state = defined ? BLOCK_ACTIVE : BLOCK_PENDING;
            }
            tok = skip_line(tok->next->next);
            continue;
        }

        if (tok->id == dt[P_ELIFNDEF].id) {
            check_elif_else_valid(tok);
            if (cur_state != BLOCK_PENDING) {
                cond_incl->state = BLOCK_DEAD;
            } else {
                bool defined = find_macro(tok->next);
                cond_incl->state = defined ? BLOCK_PENDING : BLOCK_ACTIVE;
            }
            tok = skip_line(tok->next->next);
            continue;
        }

        if (tok->id == dt[P_ELSE].id) {
            check_elif_else_valid(tok);
            cond_incl->else_seen = 1;
            cond_incl->state = (cur_state == BLOCK_PENDING) ? BLOCK_ACTIVE : BLOCK_DEAD;
            if (cond_incl->next->state == BLOCK_ACTIVE) tok = skip_line(tok->next);
            continue;
        }

        if (tok->id == dt[P_ENDIF].id) {
            if (!cond_incl->next) error(tk_hash, "#endif without #if");
            cond_incl = cond_incl->next;
            if (cond_incl->state == BLOCK_ACTIVE) tok = skip_line(tok->next);
            if (tok->kind == TK_EOF) detect_include_guard2();
            continue;
        }

        // these directives are only meaningful when the block is active
        if (cur_state != BLOCK_ACTIVE) continue;

        if (tok->id == dt[P_INCLUDE].id) {
            bool is_dquote;
            char *filename = read_include_filename(&tok, tok->next, &is_dquote);

            if (filename[0] != '/' && is_dquote) {
                char *path = format("%s/%s", dirname(strdup(tk_hash->file->name)), filename);
                if (file_exists(path)) {
                    next_path = 0;
                    Token *tmp = include_file(&tok, tok, path, tk_hash->next->next);
                    if (tmp) cur = cur->next = tmp;
                    continue;
                }
            }

            char *path = search_include_paths(filename);
            Token *tmp = include_file(&tok, tok, path ? path : filename, tk_hash->next->next);
            if (tmp) cur = cur->next = tmp;
            continue;
        }

        if (tok->id == dt[P_INCLUDE_NEXT].id) {
            bool ignore;
            char *filename = read_include_filename(&tok, tok->next, &ignore);
            char *path = search_include_next(filename);
            Token *tmp = include_file(&tok, tok, path ? path : filename, tk_hash->next->next);
            if (tmp) cur = cur->next = tmp;
            continue;
        }

        if (tok->id == dt[P_EMBED].id) {
            // #embed resource params-opt: the directive is replaced by a
            // comma-separated list of the resource bytes (C23 6.10.4).
            Token *embed_tok = tok;
            tok = tok->next;
            Token *eol = NULL;
            bool is_dquote = false;
            char *filename = read_embed_resource(&tok, tok, &is_dquote, &eol);
            int64_t limit;
            Token *prefix, *suffix, *if_empty;
            read_embed_params(&tok, tok, eol, &limit, &prefix, &suffix, &if_empty);
            long fsize;
            unsigned char *data = read_embed_bytes(tk_hash, filename, is_dquote, &fsize);
            emit_embed(&cur, embed_tok, data, fsize, limit, prefix, suffix, if_empty);
            continue;
        }

        if (tok->id == dt[P_DEFINE].id) {
            read_macro_definition(&tok, tok->next);
            continue;
        }

        if (tok->id == dt[P_UNDEF].id) {
            tok = tok->next;
            Macro *m = find_macro(tok);
            if (m && m->is_builtin) {
                tok->line_delta = line_delta;
                tok->filename = display_name;
                warning(tok, "undefining builtin macro");
            }
            m = add_macro(tok->id, true, NULL);
            m->deleted = true;

            tok = skip_line(tok->next);
            continue;
        }

        if (tok->id == dt[P_ERROR].id) {
            Token *err = tok;
            Token *msg = read_line(&tok, tok);
            error(err, "#%s", join_tokens(msg));
        }

        if (tok->id == dt[P_WARNING].id) {
            Token *warn = tok;
            Token *msg = read_line(&tok, tok);
            warning(warn, "#%s", join_tokens(msg));
            continue;
        }

        if (tok->id == dt[P_LINE].id) {
            cur = cur->next = read_line_marker(&tok, tok);
            continue;
        }

        if (tok->kind == TK_PPNUM) {
            cur = cur->next = read_line_marker(&tok, tk_hash);
            continue;
        }

        if (tok->id == dt[P_PRAGMA].id && tok->next->id == once_id) {
            add_pragma(tok);
            tok = skip_line(tok->next->next);
            continue;
        }

        if (tok->id == dt[P_PRAGMA].id) {
            do {
                tok = tok->next;
            } while (!tok->is_sol);
            continue;
        }

        // `#`-only line is legal. It's called a null directive.
        if (tok->is_sol) continue;
        error(tok, "invalid preprocessor directive");
    }

    tok->line_delta = line_delta;
    tok->filename = display_name;

    cur->next = tok;
    cond_incl = NULL;
    return dummy.next;
}

static char *cmd_buf;
static int cmd_len;

static void remove_quote(char *buf, const char *str) {
    while (isspace((unsigned char)*str)) str++;

    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) len--;

    if (len >= 2 && (str[0] == '\'' || str[0] == '\"') && str[0] == str[len - 1]) {
        str++;
        len -= 2;
    }

    memcpy(buf, str, len);
    buf[len] = '\0';
}

void cmd_include_file(char *str) {
    static char buf[4096];
    remove_quote(buf, str);
    size_t len = strlen(buf);

    if (!cmd_buf)
        cmd_buf = vnew(4096, sizeof(char));
    else
        cmd_buf = vgrow(cmd_buf, cmd_len + len + 16);

    sprintf(cmd_buf + cmd_len, "#include \"%s\"\n", buf);
    cmd_len += len + 12;
}

void cmd_define_macro(char *str) {
    static char buf[4096];
    remove_quote(buf, str);

    size_t len = strlen(buf);
    char *eq = strchr(buf, '=');
    if (eq) {
        *eq = ' ';
    } else {
        buf[len++] = ' ';
        buf[len++] = '1';
        buf[len++] = '\0';
    }

    if (!cmd_buf)
        cmd_buf = vnew(4096, sizeof(char));
    else
        cmd_buf = vgrow(cmd_buf, cmd_len + len + 12);

    sprintf(cmd_buf + cmd_len, "#define %s\n", buf);
    cmd_len += len + 9;
}

void cmd_undef_macro(char *name) {
    size_t len = strlen(name);
    if (!cmd_buf)
        cmd_buf = vnew(4096, sizeof(char));
    else
        cmd_buf = vgrow(cmd_buf, cmd_len + len + 12);

    sprintf(cmd_buf + cmd_len, "#undef %s\n", name);
    cmd_len += len + 8;
}

static Token *prep_cmdline(void) {
    if (!cmd_buf) return NULL;
    SrcFile *cmd_line = new_file("<command line>", 1, cmd_buf);
    Token *tok = tokenize(cmd_line);
    return tok;
}

static Macro *add_builtin(char *name, macro_handler_fn *fn) {
    Macro *m = add_macro(intern(name, strlen(name)), true, NULL);
    m->handler = fn;
    return m;
}

static Token *file_macro(Token **rest, Token *tmpl) {
    *rest = tmpl->next;
    Token *orig = tmpl;
    while (orig->origin) orig = orig->origin;
    return new_str_token(str(orig->filename), tmpl);
}

static Token *line_macro(Token **rest, Token *tmpl) {
    *rest = tmpl->next;
    Token *orig = tmpl;
    while (orig->origin) orig = orig->origin;
    int line, col;
    get_location(orig->file, orig->loc, &line, &col);
    return ident_to_num(tmpl, line + orig->line_delta);
}

// __COUNTER__ is expanded to serial values starting from 0.
static Token *counter_macro(Token **rest, Token *tmpl) {
    *rest = tmpl->next;
    static int i = 0;
    return ident_to_num(tmpl, i++);
}

// __INCLUDE_LEVEL__ is expanded to serial values include depth.
static Token *include_depth_macro(Token **rest, Token *tmpl) {
    *rest = tmpl->next;
    return ident_to_num(tmpl, include_depth);
}

// __TIMESTAMP__ is expanded to a string describing the last
// modification time of the current file. E.g.
// "Fri Jul 24 01:32:50 2020"
static Token *timestamp_macro(Token **rest, Token *tmpl) {
    *rest = tmpl->next;
    struct stat st;
    if (stat(tmpl->file->name, &st) != 0) return new_str_token("??? ??? ?? ??:??:?? ????", tmpl);

    char buf[30];
    ctime_r(&st.st_mtime, buf);
    buf[24] = '\0';
    return new_str_token(buf, tmpl);
}

static Token *base_file_macro(Token **rest, Token *tmpl) {
    *rest = tmpl->next;
    static Token *exist;
    if (exist) {
        Token *new = copy_token(exist);
        new->origin = tmpl;
        return new;
    }
    exist = new_str_token(base_file, tmpl);
    return exist;
}

// __DATE__ is expanded to the current date, e.g. "May 17 2020".
static Token *date_macro(Token **rest, Token *tmpl) {
    *rest = tmpl->next;
    static char mon[][4] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    static Token *exist;
    if (exist) {
        Token *new = copy_token(exist);
        new->origin = tmpl;
        return new;
    }
    char buf[32];
    sprintf(buf, "%s %2d %d", mon[tm->tm_mon], tm->tm_mday, tm->tm_year + 1900);
    exist = new_str_token(buf, tmpl);
    return exist;
}

// __TIME__ is expanded to the current time, e.g. "13:34:03".
static Token *time_macro(Token **rest, Token *tmpl) {
    *rest = tmpl->next;
    static Token *exist;
    if (exist) {
        Token *new = copy_token(exist);
        new->origin = tmpl;
        return new;
    }
    char buf[32];
    sprintf(buf, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    exist = new_str_token(buf, tmpl);
    return exist;
}

static Token *builtin_fn_macro(Token **rest, Token *tmpl) {
    Token *tok = skip(tmpl->next, TK_LPAREN);
    if (tok->kind != TK_IDENT) {
        tok->line_delta = line_delta;
        tok->filename = display_name;
        error(tok, "macro __has_builtin requires an identifier");
    }
    *rest = skip(tok->next, TK_RPAREN);
    return ident_to_num(tmpl, is_builtin_fn(tok->id));
}

SrcFile *scratch;
static void init_scratch_space(void) {
    char *scratch_space = vnew(128 * 1024, 1);
    scratch_space[0] = '\n';
    scratch = new_file("<scratch space>", 1, scratch_space);
}

static void write_scratch_space(Token *tok, char *str) {
    static int space_pos = 1;
    int len = strlen(str);
    scratch->contents = vgrow(scratch->contents, space_pos + len + 1);
    scratch->num_lines = 0;  // Rebuild_line_offsets table
    sprintf(scratch->contents + space_pos, "%s\n", str);
    tok->file = scratch;
    tok->filename = scratch->id;
    tok->loc = space_pos;
    tok->len = len;
    space_pos += len + 1;
}

static void prep_builtin(void) {
    SrcFile *pred_marcos = new_file("<bulit-in>", 1, T.predef);
    Token *tok = tokenize(pred_marcos);
    tok = filter_tokens(tok);
    preprocess2(tok);
}

void init_macros(void) {
    // Define predefined macros
    time_t now = time(NULL);
    tm = localtime(&now);

    for (size_t i = 0; i < P_CNT; ++i) dt[i].id = intern(dt[i].directive, strlen(dt[i].directive));

    true_id = intern("true", 4);
    defined_id = intern("defined", 7);
    vaarg_id = intern("__VA_ARGS__", 11);
    vaopt_id = intern("__VA_OPT__", 10);
    once_id = intern("once", 4);
    has_include_id = intern("__has_include", 13);
    has_include_next_id = intern("__has_include_next", 18);
    has_embed_id = intern("__has_embed", 11);
    has_c_attribute_id = intern("__has_c_attribute", 17);
    pragma_op_id = intern("_Pragma", 7);
    for (size_t i = 0; i < sizeof(embed_params) / sizeof(embed_params[0]); i++)
        embed_params[i].id = intern(embed_params[i].name, strlen(embed_params[i].name));

    prep_builtin();

    add_builtin("__FILE__", file_macro);
    add_builtin("__LINE__", line_macro);
    add_builtin("__COUNTER__", counter_macro);
    add_builtin("__TIMESTAMP__", timestamp_macro);
    add_builtin("__BASE_FILE__", base_file_macro);
    add_builtin("__INCLUDE_LEVEL__", include_depth_macro);

    add_builtin("__DATE__", date_macro);
    add_builtin("__TIME__", time_macro);
    add_builtin("__has_builtin", builtin_fn_macro);

    for (Macro *m = macros; m; m = m->next) m->is_builtin = true;

    init_scratch_space();
}

// Translation phases 6.
// Concatenate adjacent string literals into a single string literal
// as per the C spec.
void join_adjacent_string_literals(Token *tok) {
    // First pass: If regular string literals are adjacent to wide
    // string literals, regular string literals are converted to a wide
    // type before concatenation. In this pass, we do the conversion
    for (Token *tok1 = tok; tok1->kind != TK_EOF;) {
        if (tok1->kind != TK_STRLIT) {
            tok1 = tok1->next;
            continue;
        }

        if (tok1->next->kind != TK_STRLIT) {
            convert_str_literal(tok1);
            tok1 = tok1->next;
            continue;
        }

        uint32_t kind = tok1->enc_prefix;

        for (Token *t = tok1->next; t->kind == TK_STRLIT; t = t->next) {
            uint32_t k = t->enc_prefix;
            if (k == PREFIX_NONE)
                continue;
            else if (kind == PREFIX_NONE)
                kind = k;
            else if (kind != k)
                error(t, "unsupported non-standard concatenation of string literals");
        }

        for (Token *t = tok1; t->kind == TK_STRLIT; t = t->next) {
            t->enc_prefix = kind;
            convert_str_literal(t);
        }

        while (tok1->kind == TK_STRLIT) tok1 = tok1->next;
    }

    // Second pass: concatenate adjacent string literals.
    for (Token *tok1 = tok; tok1->kind != TK_EOF;) {
        if (tok1->kind != TK_STRLIT || tok1->next->kind != TK_STRLIT) {
            tok1 = tok1->next;
            continue;
        }

        Token *after = tok1->next;
        while (after->kind == TK_STRLIT) after = after->next;

        int len = str_len(tok1->id);
        for (Token *t = tok1->next; t != after; t = t->next) len += str_len(t->id);

        char *buf = emalloc(len);
        int i = 0;
        for (Token *t = tok1; t != after; t = t->next) {
            int valid_size = str_len(t->id);
            memcpy(buf + i, str(t->id), valid_size);
            i += valid_size;
        }

        tok1->id = intern(buf, len);
        tok1->next = after;
        tok1 = after;
    }
}

// Entry point function of the preprocessor.
Token *preprocess(Token *tok) {
    init_macros();

    Token *main;
    Token *tok_cmd = prep_cmdline();
    if (!tok_cmd) {
        main = tok;
    } else {
        Token *end = tok_cmd;
        while (end->next->kind != TK_EOF) end = end->next;
        end->next = tok;
        main = tok_cmd;
    }

    tok = filter_tokens(main);
    tok = preprocess2(tok);

    convert_keywords(tok);
    convert_ppnumber(tok);
    return tok;
}
