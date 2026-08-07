#include "cxx.h"

static Token *expand_macro(Token *dst, Token *list);

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
    FileStack *next;
    SrcFile *srcfile;
    CondIncl *condframe;
    Token *rest;
};

static FileStack *file_stack;
static FileStack *push_file(Token *rest) {
    FileStack *file = emalloc(sizeof(FileStack));
    file->srcfile = cur_file;
    file->condframe = cond_incl;
    file->rest = rest;

    cond_incl = NULL;
    push_cond_incl(NULL, BLOCK_ACTIVE);

    file->next = file_stack;
    file_stack = file;
    return file;
}

static Token *pop_file(void) {
    cur_file = file_stack->srcfile;
    cond_incl = file_stack->condframe;
    Token *rest = file_stack->rest;
    file_stack = file_stack->next;
    return rest;
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
    return tokenize(new_file(tmpl->file->name, tmpl->file->file_no, buf));
}

// Copy all tokens until the next newline, terminate them with
// an EOF token and then returns them. This function is used to
// create a new list of tokens for `#if` arguments.
static Token *copy_line(Token **rest, Token *tok) {
    Token dummy = {};
    Token *cur = &dummy;

    for (; tok && !tok->is_sol; tok = tok->next) cur = cur->next = copy_token(tok);

    cur->next = new_eof(tok);
    *rest = tok;
    return dummy.next;
}

// Read and evaluate a constant expression.
static int64_t eval_const_expr(Token **rest, Token *tok) {
    Token *start = tok;
    Token *expr = copy_line(rest, tok->next);

    if (expr->kind == TK_EOF) error(start, "no expression");

    Token dummy = {}, *cur = &dummy;
    cur = expand_macro(cur, expr);
    cur->next = new_eof(cur);
    expr = dummy.next;

    convert_pptoken(expr);
    Token *rest2;
    int64_t val = const_expr(&rest2, expr);
    if (rest2->kind != TK_EOF) error(rest2, "extra token");
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

typedef struct Macro Macro;
struct Macro {
    Macro *next;
    uint32_t id;
    bool is_objlike;  // Object-like or function-like
    MacroParam *params;
    Token *body;
    bool deleted;
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
    if (tok->kind != TK_IDENT) return NULL;

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

static MacroParam *read_macro_params(Token **rest, Token *tok) {
    MacroParam dummy = {};
    MacroParam *cur = &dummy;

    while (tok->kind != TK_RPAREN) {
        if (cur != &dummy) tok = skip(tok, TK_COMMA);

        if (tok->kind != TK_IDENT) error(tok, "expected an identifier");
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
    Token *name = tok;
    tok = tok->next;

    if (tok->kind == TK_LPAREN && !tok->is_leadingws) {
        // Function-like macro
        MacroParam *params = read_macro_params(&tok, tok->next);
        Macro *m = add_macro(name->id, false, copy_line(rest, tok));
        m->params = params;
    } else {
        // Object-like macro
        add_macro(name->id, true, copy_line(rest, tok));
    }
}

static MacroArg *read_macro_arg_one(Token **rest, Token *tok) {
    Token dummy = {};
    Token *cur = &dummy;
    int level = 0;

    while ((level > 0) || (tok->kind != TK_COMMA && tok->kind != TK_RPAREN)) {
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

static MacroArg *read_macro_args(Token **rest, Token *tok, MacroParam *params) {
    tok = tok->next->next;

    MacroArg dummy = {};
    MacroArg *cur = &dummy;

    MacroParam *pp = params;
    for (; pp; pp = pp->next) {
        if (cur != &dummy) tok = skip(tok, TK_COMMA);
        cur = cur->next = read_macro_arg_one(&tok, tok);
        cur->id = pp->id;
    }

    if (tok->kind != TK_RPAREN) error(tok, "too many arguments");
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
    Token *tok = tokenize(new_file(lhs->file->name, lhs->file->file_no, buf));
    if (tok->next->kind != TK_EOF) error(lhs, "pasting forms '%s', an invalid token", buf);
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

        // Handle a macro token. Macro arguments are completely macro-expanded
        // before they are substituted into a macro body.
        if (arg) {
            Token dummy2 = {};
            expand_macro(&dummy2, arg->tok);
            for (Token *t = dummy2.next; t; t = t->next) cur = cur->next = copy_token(t);
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

        // Object-like macro application
        if (m->is_objlike) {
            push_disabled(cur->id);
            dst = expand_macro(dst, m->body);
            pop_disabled();
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
        uint32_t macro_id = cur->id;
        MacroArg *args = read_macro_args(&cur, cur, m->params);
        Token *sub = subst(m->body, args);
        push_disabled(macro_id);
        dst = expand_macro(dst, sub);
        pop_disabled();
        continue;
    }
    return dst;
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
    P_CNT,
} P_DIRECT;

static struct {
    char *directive;
    uint32_t id;
} dt[] = {
    [P_INCLUDE] = {"include", 0},   [P_IF] = {"if", 0},       [P_IFDEF] = {"ifdef", 0},
    [P_IFNDEF] = {"ifndef", 0},     [P_ELIF] = {"elif", 0},   [P_ELIFDEF] = {"elifdef", 0},
    [P_ELIFNDEF] = {"elifndef", 0}, [P_ELSE] = {"else", 0},   [P_ENDIF] = {"endif", 0},
    [P_DEFINE] = {"define", 0},     [P_UNDEF] = {"undef", 0},
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
            while (cond_incl->next) {
                error(cond_incl->if_tok, "unterminated conditional directive");
                cond_incl = cond_incl->next;
            }
            if (file_stack) {
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
            if (!cond_incl->next) error(tk_hash, "stray #endif");
            cond_incl = cond_incl->next;
            if (cond_incl->state == BLOCK_ACTIVE) tok = skip_line(tok->next);
            continue;
        }

        // these directives are only meaningful when the block is active
        if (cur_state != BLOCK_ACTIVE) continue;

        if (tok->id == dt[P_INCLUDE].id) {
            tok = tok->next;

            if (tok->kind != TK_STRLIT) error(tok, "expected a filename");

            char *path = str(tok->id);
            if (path[0] != '/') path = format("%s/%s", dirname(strdup(tok->file->name)), path);

            Token *tok2 = tokenize_file(path);
            if (!tok2) error(tok, "%s", strerror(errno));
            tok = skip_line(tok->next);
            push_file(tok);
            tok = tok2;
            continue;
        }

        if (tok->id == dt[P_DEFINE].id) {
            read_macro_definition(&tok, tok->next);
            continue;
        }

        if (tok->id == dt[P_UNDEF].id) {
            tok = tok->next;
            if (tok->kind != TK_IDENT) error(tok, "macro name must be an identifier");

            Macro *m = add_macro(tok->id, true, NULL);
            m->deleted = true;
            tok = skip_line(tok->next);
            continue;
        }

        // `#`-only line is legal. It's called a null directive.
        if (tok->is_sol) continue;
        error(tok, "invalid preprocessor directive");
    }

    cur->next = tok;
    return dummy.next;
}

// Entry point function of the preprocessor.
Token *preprocess(Token *tok) {
    tok = preprocess2(tok);
    convert_pptoken(tok);
    return tok;
}
