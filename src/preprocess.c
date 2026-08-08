#include "cxx.h"

static Token *expand_macro(Token *dst, Token *list);
typedef struct Macro Macro;
static Macro *find_macro(Token *tok);

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

// `#include` can be nested, so we use a stack to manage nested `#include`s.
typedef struct FileStack FileStack;
struct FileStack {
    SrcFile *srcfile;
    CondIncl *condframe;
    Token *rest;
};

static FileStack *file_stack[256];
static int include_depth;
#define MAX_INCL_DEPTH 200

#define STR1(x) #x
#define STR(x) STR1(x)

static FileStack *push_file(Token *rest) {
    if (include_depth >= MAX_INCL_DEPTH) error(rest, "include nesting too deep, MAX_DEPTH = " STR(MAX_INCL_DEPTH));
#undef STR
#undef STR1

    FileStack *file = emalloc(sizeof(FileStack));
    file->srcfile = cur_file;
    file->condframe = cond_incl;
    file->rest = rest;

    cond_incl = NULL;
    push_cond_incl(NULL, BLOCK_ACTIVE);

    file_stack[include_depth++] = file;

    return file;
}

static Token *pop_file(void) {
    include_depth--;
    FileStack *file = file_stack[include_depth];

    cur_file = file->srcfile;
    cond_incl = file->condframe;
    return file->rest;
}

static bool is_hash(Token *tok) { return tok && tok->is_sol && tok->kind == TK_HASH; }

// Some preprocessor directives such as #include allow extraneous
// tokens before newline. This function skips such tokens.
static Token *skip_line(Token *tok) {
    if (tok->is_sol || tok->kind == TK_EOF) return tok;
    diag("warning", tok, "extra token");
    while (!tok->is_sol && tok->kind != TK_EOF) tok = tok->next;
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
    return t;
}

// Append tok2 to the end of tok1.
Token *append(Token *tok1, Token *tok2) {
    if (!tok1 || tok1->kind == TK_EOF) return tok2;

    Token dummy = {};
    Token *cur = &dummy;

    for (; tok1 && tok1->kind != TK_EOF; tok1 = tok1->next) cur = cur->next = copy_token(tok1);
    cur->next = tok2;
    return dummy.next;
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

static Token *new_str_token(char *str, Token *tmpl) {
    char *buf = quote_string(str);
    SrcFile *file = new_file(tmpl->file->name, tmpl->file->file_no, buf);
    Token *tok = tokenize(file, tmpl->line, tmpl->col);
    tok->is_sol = false;
    tok->is_leadingws = false;
    return tok;
}

// Copy all tokens until the next newline, terminate them with
// an EOF token and then returns them. This function is used to
// create a new list of tokens for `#if` arguments.
static Token *copy_line(Token **rest, Token *tok) {
    Token dummy = {};
    Token *cur = &dummy;

    for (; tok && !tok->is_sol && tok->kind != TK_EOF; tok = tok->next) cur = cur->next = copy_token(tok);

    cur->next = new_eof(tok);
    *rest = tok;
    return dummy.next;
}

static Token *new_num_token(int val, Token *tmpl) {
    char *buf = format("%d\n", val);
    SrcFile *file = new_file(tmpl->file->name, tmpl->file->file_no, buf);
    return tokenize(file, tmpl->line, tmpl->col);
}

static uint32_t defined_id;
static Token *read_const_expr(Token **rest, Token *tok) {
    tok = copy_line(rest, tok);

    Token dummy = {};
    Token *cur = &dummy;

    if (!defined_id) defined_id = intern("defined", 7);

    while (tok->kind != TK_EOF) {
        // "defined(foo)" or "defined foo" becomes "1" if macro "foo"
        // is defined. Otherwise "0".
        if (tok->kind == TK_IDENT && tok->id == defined_id) {
            Token *start = tok;
            bool has_paren = match(&tok, tok->next, TK_LPAREN);

            if (tok->kind != TK_IDENT) error(start, "operator \"defined\" requires an identifier");
            Macro *m = find_macro(tok);
            tok = tok->next;

            if (has_paren) tok = skip(tok, TK_RPAREN);

            cur = cur->next = new_num_token(m ? 1 : 0, start);
            continue;
        }

        cur = cur->next = tok;
        tok = tok->next;
    }

    cur->next = tok;
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

    // we replace remaining non-macro identifiers with "0"
    for (Token *t = expr; t->kind != TK_EOF; t = t->next) {
        if (t->kind == TK_IDENT) {
            Token *next = t->next;
            *t = *new_num_token(0, t);
            t->next = next;
        }
    }

    convert_pptoken(expr);
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
    if (tok->kind != TK_IDENT) error(tok, "macro name must be an identifier");

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

static MacroParam *read_macro_params(Token **rest, Token *tok, bool *is_variadic) {
    MacroParam dummy = {};
    MacroParam *cur = &dummy;

    while (tok->kind != TK_RPAREN) {
        if (cur != &dummy) tok = skip(tok, TK_COMMA);
        if (tok->kind == TK_ELLIPSIS) {
            *is_variadic = true;
            *rest = skip(tok->next, TK_RPAREN);
            return dummy.next;
        }
        if (tok->kind != TK_IDENT) error(tok, " expected parameter name");
        for (MacroParam *p = dummy.next; p; p = p->next)
            if (p->id == tok->id) error(tok, "duplicate macro parameter \"%s\"", str(tok->id));

        MacroParam *m = emalloc(sizeof(MacroParam));
        m->id = tok->id;
        cur = cur->next = m;
        tok = tok->next;
    }
    *rest = tok->next;
    return dummy.next;
}

static void read_macro_definition(Token **rest, Token *tok) {
    if (tok->kind != TK_IDENT) error(tok, "macro name must be an identifier");
    if (!defined_id) defined_id = intern("defined", 7);
    if (tok->id == defined_id) error(tok, "'defined' cannot be used as a macro name");
    Macro *exist = find_macro(tok);
    if (exist && exist->is_builtin) diag("warning", tok, "redefining builtin macro");

    Token *name = tok;
    tok = tok->next;

    if (tok->kind == TK_LPAREN && !tok->is_leadingws) {
        // Function-like macro
        bool is_variadic = false;
        MacroParam *params = read_macro_params(&tok, tok->next, &is_variadic);
        if (!tok->is_leadingws) diag("warning", tok, "ISO C99 requires whitespace after the macro name");
        Macro *m = add_macro(name->id, false, copy_line(rest, tok));
        m->params = params;
        m->is_variadic = is_variadic;
    } else {
        // Object-like macro
        if (!tok->is_leadingws) diag("warning", tok, "ISO C99 requires whitespace after the macro name");
        add_macro(name->id, true, copy_line(rest, tok));
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

static MacroArg *read_macro_args(Token **rest, Token *tok, MacroParam *params, bool is_variadic) {
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
        arg->id = intern("__VA_ARGS__", 11);
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
static Token *stringize(Token *hash, Token *arg) {
    // Create a new string token. We need to set some value to its
    // source location for error reporting function, so we use a macro
    // name token as a template.
    char *s = join_tokens(arg);
    return new_str_token(s, hash);
}

// Concatenate two tokens to create a new token.
static Token *paste(Token *lhs, Token *rhs) {
    // Paste the two tokens.
    char *buf = format("%.*s%.*s", lhs->len, lhs->loc, rhs->len, rhs->loc);

    // Tokenize the resulting string.
    SrcFile *file = new_file(lhs->file->name, lhs->file->file_no, buf);
    Token *tok = tokenize(file, lhs->line, lhs->col);
    if (tok->next->kind != TK_EOF) error(lhs, "pasting forms '%s', an invalid token", buf);

    // Inherit source location and whitespace flag from lhs.
    tok->is_sol = false;
    tok->is_leadingws = lhs->is_leadingws;
    return tok;
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
            cur = cur->next = stringize(tok, arg->tok);
            tok = tok->next->next;
            continue;
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
        if (cur->noexpand || cur->kind != TK_IDENT) {
            dst = dst->next = copy_token(cur);
            cur = cur->next;
            continue;
        }
        if (is_disabled(cur->id)) {
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
            dst = dst->next = m->handler(macro_name);
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
            for (Token *t = prev; t && t->kind != TK_EOF; t = t->next) t->origin = macro_name;
            cur = cur->next;
            continue;
        }

        // If a funclike macro token is not followed by an argument list,
        // treat it as a normal identifier.
        if (!cur->next || cur->next->kind != TK_LPAREN || cur->next->is_leadingws) {
            dst = dst->next = copy_token(cur);
            cur = cur->next;
            continue;
        }

        // Function-like macro application
        MacroArg *args = read_macro_args(&cur, cur, m->params, m->is_variadic);
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
    return dst;
}

static char *search_include_paths(char *filename) {
    if (filename[0] == '/') return filename;

    // Search a file from the include paths.
    for (int i = 0; i < num_include_paths; i++) {
        char *path = format("%s/%s", include_paths[i], filename);
        if (file_exists(path)) return path;
    }
    return NULL;
}

// Read an #include argument.
static char *read_include_filename(Token **rest, Token *tok, bool *is_dquote) {
    // Pattern 1: #include "foo.h"
    if (tok->kind == TK_STRLIT) {
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
            if (tok->is_sol || tok->kind == TK_EOF) error(start, "expected '>' to match this '<'");

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
        cur = expand_macro(cur, copy_line(rest, tok));
        cur->next = new_eof(cur);
        return read_include_filename(&cur, dummy.next, is_dquote);
    }

    error(tok, "expected \"FILENAME\" or <FILENAME>");
    return NULL;
}

static Token *include_file(Token *tok, char *path, Token *filename_tok) {
    Token *tok2 = tokenize_file(path);
    if (!tok2) error(filename_tok, "%s: cannot open file: %s", path, strerror(errno));
    push_file(tok);
    return tok2;
}

typedef enum {
    P_INCLUDE,
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
    P_CNT,
} P_DIRECT;

static struct {
    char *directive;
    uint32_t id;
} dt[] = {
    [P_INCLUDE] = {"include", 0},   [P_IF] = {"if", 0},       [P_IFDEF] = {"ifdef", 0},
    [P_IFNDEF] = {"ifndef", 0},     [P_ELIF] = {"elif", 0},   [P_ELIFDEF] = {"elifdef", 0},
    [P_ELIFNDEF] = {"elifndef", 0}, [P_ELSE] = {"else", 0},   [P_ENDIF] = {"endif", 0},
    [P_DEFINE] = {"define", 0},     [P_UNDEF] = {"undef", 0}, [P_ERROR] = {"error", 0},
    [P_WARNING] = {"warning", 0},
};

// Visit all tokens in `tok` while evaluating preprocessing
// macros and directives.
static Token *preprocess2(Token *tok) {
    if (!dt[0].id) {
        for (size_t i = 0; i < P_CNT; ++i) dt[i].id = intern(dt[i].directive, strlen(dt[i].directive));
    }
    Token dummy = {};
    Token *cur = &dummy;
    push_cond_incl(tok, BLOCK_ACTIVE);

    while (1) {
    loop_start:
        if (tok->kind == TK_EOF) {
            if (cond_incl->next) error(cond_incl->if_tok, "unterminated conditional directive");
            if (include_depth > 0) {
                tok = pop_file();
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
                if (concat) buf = buf->next = tok;
                tok = tok->next;
            }
            buf->next = new_eof(tok);
            cur = expand_macro(cur, dummy2.next);
            if (tok->kind == TK_EOF) goto loop_start;
        }

        Token *tk_hash = tok;
        tok = tok->next;

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
                    tok = include_file(tok, path, tk_hash->next->next);
                    continue;
                }
            }
            char *path = search_include_paths(filename);
            tok = include_file(tok, path ? path : filename, tk_hash->next->next);
            continue;
        }

        if (tok->id == dt[P_DEFINE].id) {
            read_macro_definition(&tok, tok->next);
            continue;
        }

        if (tok->id == dt[P_UNDEF].id) {
            tok = tok->next;
            Macro *m = find_macro(tok);
            if (m && m->is_builtin) diag("warning", tok, "undefining builtin macro");

            m = add_macro(tok->id, true, NULL);
            m->deleted = true;

            tok = skip_line(tok->next);
            continue;
        }

        if (tok->id == dt[P_ERROR].id) {
            Token *err = tok;
            Token *msg = copy_line(&tok, tok);
            error(err, "#%s", join_tokens(msg));
        }

        if (tok->id == dt[P_WARNING].id) {
            Token *warn = tok;
            Token *msg = copy_line(&tok, tok);
            diag("warning", warn, "#%s", join_tokens(msg));
            continue;
        }

        // `#`-only line is legal. It's called a null directive.
        if (tok->is_sol) continue;
        error(tok, "invalid preprocessor directive");
    }

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

void define_macro(char *str) {
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

void undef_macro(char *name) {
    size_t len = strlen(name);
    if (!cmd_buf)
        cmd_buf = vnew(4096, sizeof(char));
    else
        cmd_buf = vgrow(cmd_buf, cmd_len + len + 12);

    sprintf(cmd_buf + cmd_len, "#undef %s\n", name);
    cmd_len += len + 8;
}

static void prep_cmdline(void) {
    if (!cmd_buf) return;
    SrcFile *cmd_line = new_file("<command line>", 1, cmd_buf);
    Token *tok = tokenize(cmd_line, 1, 1);
    preprocess2(tok);
}

static Macro *add_builtin(char *name, char *buf, macro_handler_fn *fn) {
    Token *tok = NULL;
    if (buf) tok = tokenize(new_file("<built-in>", 1, buf), 1, 1);
    Macro *m = add_macro(intern(name, strlen(name)), true, tok);
    m->handler = fn;
    m->is_builtin = true;
    return m;
}

static Token *file_macro(Token *tmpl) {
    while (tmpl->origin) tmpl = tmpl->origin;
    return new_str_token(tmpl->file->name, tmpl);
}

static Token *line_macro(Token *tmpl) {
    while (tmpl->origin) tmpl = tmpl->origin;
    return new_num_token(tmpl->line, tmpl);
}

void init_macros(void) {
    // Define predefined macros
    add_builtin("_LP64", "1", NULL);
    add_builtin("__C99_MACRO_WITH_VA_ARGS", "1", NULL);
    add_builtin("__ELF__", "1", NULL);
    add_builtin("__LP64__", "1", NULL);
    add_builtin("__SIZEOF_DOUBLE__", "8", NULL);
    add_builtin("__SIZEOF_FLOAT__", "4", NULL);
    add_builtin("__SIZEOF_INT__", "4", NULL);
    add_builtin("__SIZEOF_LONG_DOUBLE__", "8", NULL);
    add_builtin("__SIZEOF_LONG_LONG__", "8", NULL);
    add_builtin("__SIZEOF_LONG__", "8", NULL);
    add_builtin("__SIZEOF_POINTER__", "8", NULL);
    add_builtin("__SIZEOF_PTRDIFF_T__", "8", NULL);
    add_builtin("__SIZEOF_SHORT__", "2", NULL);
    add_builtin("__SIZEOF_SIZE_T__", "8", NULL);
    add_builtin("__SIZE_TYPE__", "unsigned long", NULL);
    add_builtin("__STDC_HOSTED__", "1", NULL);
    add_builtin("__STDC_NO_ATOMICS__", "1", NULL);
    add_builtin("__STDC_NO_COMPLEX__", "1", NULL);
    add_builtin("__STDC_NO_THREADS__", "1", NULL);
    add_builtin("__STDC_NO_VLA__", "1", NULL);
    add_builtin("__STDC_VERSION__", "201112L", NULL);
    add_builtin("__STDC__", "1", NULL);
    add_builtin("__USER_LABEL_PREFIX__", "", NULL);
    add_builtin("__alignof__", "_Alignof", NULL);
    // add_builtin("__amd64", "1",NULL);
    // add_builtin("__amd64__", "1",NULL);
    add_builtin("__cxx__", "1", NULL);
    add_builtin("__const__", "const", NULL);
    add_builtin("__gnu_linux__", "1", NULL);
    add_builtin("__inline__", "inline", NULL);
    add_builtin("__linux", "1", NULL);
    add_builtin("__linux__", "1", NULL);
    add_builtin("__signed__", "signed", NULL);
    add_builtin("__typeof__", "typeof", NULL);
    add_builtin("__unix", "1", NULL);
    add_builtin("__unix__", "1", NULL);
    add_builtin("__volatile__", "volatile", NULL);
    // add_builtin("__x86_64", "1",NULL);
    // add_builtin("__x86_64__", "1",NULL);
    add_builtin("linux", "1", NULL);
    add_builtin("unix", "1", NULL);

    add_builtin("__FILE__", NULL, file_macro);
    add_builtin("__LINE__", NULL, line_macro);
}

// Translation phases 6.
// Concatenate adjacent string literals into a single string literal
// as per the C spec.
void join_adjacent_string_literals(Token *tok1) {
    while (tok1->kind != TK_EOF) {
        if (tok1->kind != TK_STRLIT || tok1->next->kind != TK_STRLIT) {
            tok1 = tok1->next;
            continue;
        }

        Token *tok2 = tok1->next;
        while (tok2->kind == TK_STRLIT) tok2 = tok2->next;

        int len = tok1->ty->len;
        for (Token *t = tok1->next; t != tok2; t = t->next) len = len + t->ty->len - 1;

        char *buf = emalloc(tok1->ty->base->size * len);

        int i = 0;
        for (Token *t = tok1; t != tok2; t = t->next) {
            memcpy(buf + i, str(t->id), t->ty->size);
            i = i + t->ty->size - t->ty->base->size;
        }

        *tok1 = *copy_token(tok1);
        tok1->ty = array_of(tok1->ty->base, len);
        tok1->id = intern(buf, len);
        tok1->next = tok2;
        tok1 = tok2;
    }
}

// Entry point function of the preprocessor.
Token *preprocess(Token *tok) {
    init_macros();
    prep_cmdline();
    tok = preprocess2(tok);
    convert_pptoken(tok);
    return tok;
}
