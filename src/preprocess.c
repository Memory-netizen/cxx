#include "cxx.h"

struct tm *tm;

enum {
    P_INCLUDE,
    P_INCLUDE_NEXT,
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
    [P_INCLUDE] = {"include", 0}, [P_INCLUDE_NEXT] = {"include_next", 0},
    [P_IF] = {"if", 0},           [P_IFDEF] = {"ifdef", 0},
    [P_IFNDEF] = {"ifndef", 0},   [P_ELIF] = {"elif", 0},
    [P_ELIFDEF] = {"elifdef", 0}, [P_ELIFNDEF] = {"elifndef", 0},
    [P_ELSE] = {"else", 0},       [P_ENDIF] = {"endif", 0},
    [P_DEFINE] = {"define", 0},   [P_UNDEF] = {"undef", 0},
    [P_ERROR] = {"error", 0},     [P_WARNING] = {"warning", 0},
    [P_LINE] = {"line", 0},       [P_PRAGMA] = {"pragma", 0},
};

static uint32_t true_id;
static uint32_t defined_id;
static uint32_t vaarg_id;
static uint32_t vaopt_id;
static uint32_t once_id;
static uint32_t has_include_id;

static Token *expand_macro(Token *dst, Token *list);
static char *join_tokens(Token *tok);
static char *search_include_paths(char *filename);
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
    new->ty = array_of(ty_char, len + 1);
    char *q_str = quote_string(str);
    write_scratch_space(new, q_str);
    new->origin = tmpl;
    return new;
}

static Token *ident_to_num(Token *tok, int64_t val) {
    Token *new = emalloc(sizeof(Token));
    new->kind = TK_NUM;
    new->val = val;
    new->ty = ty_long;
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

static Token *eval_has_include(Token *tok) {
    Token dummy = {};
    Token *cur = &dummy;
    while (tok) {
        // __has_include("file") or __has_include(<file>)
        // becomes "1" if the file can beincluded,
        // otherwise "0".
        if (tok->kind == TK_IDENT && tok->id == has_include_id) {
            Token *start = tok;
            tok = skip(tok->next, TK_LPAREN);

            bool exists = false;
            if (tok->kind == TK_STRLIT && tok->enc_prefix == PREFIX_NONE) {
                char *path = strndup(tok->loc + 1, tok->len - 2);
                exists = exist_include(start, path, true);
            } else if (tok->kind == TK_LT) {
                Token *lt = tok;
                for (; tok->kind != TK_GT; tok = tok->next)
                    if (tok->is_sol || tok->kind == TK_EOF) error(lt, "expected '>' to match this '<'");
                Token *gt = tok;
                gt->kind = TK_EOF;
                char *path = join_tokens(lt->next);
                exists = exist_include(start, path, false);
            } else {
                error(tok, "__has_include expects \"FILENAME\" or <FILENAME>");
            }

            tok = skip(tok->next, TK_RPAREN);
            cur = cur->next = ident_to_num(start, exists ? 1 : 0);
            continue;
        }
        cur = cur->next = tok;
        tok = tok->next;
    }

    return dummy.next;
}

// Read and evaluate a constant expression.
static int64_t eval_const_expr(Token **rest, Token *tok) {
    Token *start = tok;
    Token *expr = read_const_expr(rest, tok->next);

    if (expr->kind == TK_EOF) error(start, "no expression in #%s", str(start->id));

    Token dummy = {}, *cur = &dummy;
    cur = expand_macro(cur, expr);
    cur->next = new_eof(cur);
    expr = dummy.next;
    expr = eval_has_include(expr);

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
    for (Token *t = expr; t->kind != TK_EOF; t = t->next)
        if (t->kind == TK_NUM && is_flonum(t->ty)) error(t, "floating point literal in preprocessor expression");

    Token *rest2;
    int64_t val = const_expr(&rest2, expr);
    if (rest2->kind != TK_EOF) error(rest2, "missing binary operator before token \"%.*s\"", rest2->len, rest2->loc);
    return val;
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

typedef Token *macro_handler_fn(Token *);
struct Macro {
    Macro *next;
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

static Macro *find_macro(Token *tok) {
    if (tok->kind != TK_IDENT) {
        tok->line_delta = line_delta;
        tok->filename = display_name;
        error(tok, "macro name must be an identifier");
    }
    for (Macro *m = macros; m; m = m->next)
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
        strncpy(buf + pos, t->loc, t->len);
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
    char *buf = format("%.*s%.*s", lhs->len, lhs->loc, rhs->len, rhs->loc);

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
            Token *t = m->handler(macro_name);
            t->is_leadingws = cur->is_leadingws;
            t->is_sol = cur->is_sol;
            dst = dst->next = t;
            cur = cur->next;
            continue;
        }

        // Object-like macro application
        if (m->is_objlike) {
            Token *prev = dst;
            push_disabled(cur->id);
            dst = expand_macro(dst, m->body);
            pop_disabled();
            if (prev->next) {
                prev->next->is_leadingws = cur->is_leadingws;
                prev->next->is_sol = cur->is_sol;
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

// Read an #include argument.
static char *read_include_filename(Token **rest, Token *tok, bool *is_dquote) {
    // Pattern 1: #include "foo.h"
    if (tok->kind == TK_STRLIT && tok->enc_prefix == PREFIX_NONE) {
        *is_dquote = true;
        *rest = skip_line(tok->next);
        return strndup(tok->loc + 1, tok->len - 2);
    }

    // Pattern 2: #include <foo.h>
    if (tok->kind == TK_LT) {
        // Reconstruct a filename from a sequence of tokens between
        // "<" and ">".
        Token *start = tok;

        // Find closing ">".
        for (; tok->kind != TK_GT; tok = tok->next)
            if (tok->is_sol || tok->kind == TK_EOF) {
                start->line_delta = line_delta;
                start->filename = display_name;
                error(start, "expected '>' to match this '<'");
            }

        *is_dquote = false;
        *rest = skip_line(tok->next);
        *tok = *new_eof(tok);
        return join_tokens(start->next);
    }

    // Pattern 3: #include FOO
    // In this case FOO must be macro-expanded to either
    // a single string token or a sequence of "<" ... ">".
    if (tok->kind == TK_IDENT) {
        Token dummy = {}, *cur = &dummy;
        cur = expand_macro(cur, read_line(rest, tok));
        cur->next = new_eof(cur);
        return read_include_filename(&cur, dummy.next, is_dquote);
    }

    tok->line_delta = line_delta;
    tok->filename = display_name;
    error(tok, "expected \"FILENAME\" or <FILENAME>");
    return NULL;
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

static Token *new_linemarker(Token *tmpl, int line, uint32_t filename) {
    Token *linemarker = copy_token(tmpl);
    linemarker->kind = TK_LINE;
    linemarker->val = line;
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
        char c = tok->loc[i];
        if (isdigit(c))
            line_no = line_no * 10 + c - '0';
        else
            error(start, "line marker directive requires a simple digit sequence");
    }
    if (line_no == 0) error(start, "#line directive requires a positive integer argument");
    if (*tok->loc == '0') error(start, "line marker directive interprets number as decimal, not octal");

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
                    if (tok->kind == TK_ERR) error(tok, tok->msg);
                    if (tok->kind == TK_WARN) warning(tok, tok->msg);
                    if (tok->id == has_include_id)
                        error(tok, "'__has_include' must be used within a preprocessing directive");
                    buf = buf->next = tok;
                }
                tok = tok->next;
            }
            buf->next = new_eof(tok);
            cur = expand_macro(cur, dummy2.next);
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

static Token *file_macro(Token *tmpl) {
    Token *orig = tmpl;
    while (orig->origin) orig = orig->origin;
    return new_str_token(str(orig->filename), tmpl);
}

static Token *line_macro(Token *tmpl) {
    Token *orig = tmpl;
    while (orig->origin) orig = orig->origin;
    int line, col;
    get_location(orig->file, orig->loc, &line, &col);
    return ident_to_num(tmpl, line + orig->line_delta);
}

// __COUNTER__ is expanded to serial values starting from 0.
static Token *counter_macro(Token *tmpl) {
    static int i = 0;
    return ident_to_num(tmpl, i++);
}

// __TIMESTAMP__ is expanded to a string describing the last
// modification time of the current file. E.g.
// "Fri Jul 24 01:32:50 2020"
static Token *timestamp_macro(Token *tmpl) {
    struct stat st;
    if (stat(tmpl->file->name, &st) != 0) return new_str_token("??? ??? ?? ??:??:?? ????", tmpl);

    char buf[30];
    ctime_r(&st.st_mtime, buf);
    buf[24] = '\0';
    return new_str_token(buf, tmpl);
}

static Token *base_file_macro(Token *tmpl) {
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
static Token *date_macro(Token *tmpl) {
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
static Token *time_macro(Token *tmpl) {
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

SrcFile *scratch;
static void init_scratch_space(void) {
    char *scratch_space = vnew(128 * 1024, sizeof(char));
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
    tok->loc = scratch->contents + space_pos;
    tok->len = len;
    space_pos += len + 1;
}

static char built_in[] = {
    "#define _LP64 1\n"
    "#define __C99_MACRO_WITH_VA_ARGS 1\n"
    "#define __ELF__ 1\n"
    "#define __LP64__ 1\n"
    "#define __SIZEOF_DOUBLE__ 8\n"
    "#define __SIZEOF_FLOAT__ 4\n"
    "#define __SIZEOF_INT__ 4\n"
    "#define __SIZEOF_LONG_DOUBLE__ 8\n"
    "#define __SIZEOF_LONG_LONG__ 8\n"
    "#define __SIZEOF_POINTER__ 8\n"
    "#define __SIZEOF_PTRDIFF_T__ 8\n"
    "#define __SIZEOF_SHORT__ 2\n"
    "#define __SIZEOF_SIZE_T__ 8\n"
    "#define __SIZE_TYPE__ unsigned long\n"
    "#define __STDC_HOSTED__ 1\n"
    "#define __STDC_NO_ATOMICS__ 1\n"
    "#define __STDC_NO_COMPLEX__ 1\n"
    "#define __STDC_NO_THREADS__ 1\n"
    "#define __STDC_NO_VLA__ 1\n"
    "#define __STDC_UTF_16__ 1\n"
    "#define __STDC_UTF_32__ 1\n"
    "#define __STDC_VERSION__ 201112L\n"
    "#define __STDC__ 1\n"
    "#define __USER_LABEL_PREFIX__\n"
    "#define __alignof__ _Alignof\n"
    "#define __cxx__ 1\n"
    "#define __const__ const\n"
    "#define __gnu_linux__ 1\n"
    "#define __has_include __has_include\n"
    "#define __inline__ inline\n"
    "#define __linux 1\n"
    "#define __linux__ 1\n"
    "#define __signed__ signed\n"
    "#define __typeof__ typeof\n"
    "#define __unix 1\n"
    "#define __unix__ 1\n"
    "#define __volatile__ volatile\n"
    "#define linux 1\n"
    "#define unix 1\n",
};

static void prep_builtin(void) {
    SrcFile *pred_marcos = new_file("<bulit-in>", 1, built_in);
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

    prep_builtin();

    add_builtin("__FILE__", file_macro);
    add_builtin("__LINE__", line_macro);
    add_builtin("__COUNTER__", counter_macro);
    add_builtin("__TIMESTAMP__", timestamp_macro);
    add_builtin("__BASE_FILE__", base_file_macro);

    add_builtin("__DATE__", date_macro);
    add_builtin("__TIME__", time_macro);

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

        int len = tok1->ty->len;
        for (Token *t = tok1->next; t != after; t = t->next) len += t->ty->len - 1;

        int base_size = tok1->ty->base->size;
        char *buf = emalloc(base_size * len);

        int i = 0;
        for (Token *t = tok1; t != after; t = t->next) {
            int valid_size = t->ty->size - base_size;
            memcpy(buf + i, str(t->id), valid_size);
            i += valid_size;
        }

        tok1->ty = array_of(tok1->ty->base, len);
        tok1->id = intern(buf, base_size * (len - 1));
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
