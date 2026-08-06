#include "cxx.h"

typedef struct Macro Macro;
struct Macro {
    Macro *next;
    uint32_t id;
    Token *body;
};
static Macro *macros;

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

static Token *new_eof(void) {
    Token *t = emalloc(sizeof(Token));
    t->kind = TK_EOF;
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

// Copy all tokens until the next newline, terminate them with
// an EOF token and then returns them. This function is used to
// create a new list of tokens for `#if` arguments.
static Token *copy_line(Token **rest, Token *tok) {
    Token dummy = {};
    Token *cur = &dummy;

    for (; tok && !tok->is_sol; tok = tok->next) cur = cur->next = copy_token(tok);

    cur->next = new_eof();
    *rest = tok;
    return dummy.next;
}

// Read and evaluate a constant expression.
static int64_t eval_const_expr(Token **rest, Token *tok) {
    Token *start = tok;
    Token *expr = copy_line(rest, tok->next);

    if (expr->kind == TK_EOF) error(start, "no expression");

    Token *rest2;
    convert_pptoken(expr);
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

static Macro *find_macro(Token *tok) {
    if (tok->kind != TK_IDENT) return NULL;

    for (Macro *m = macros; m; m = m->next)
        if (m->id == tok->id) return m;
    return NULL;
}

static Macro *add_macro(uint32_t id, Token *body) {
    Macro *m = emalloc(sizeof(Macro));
    m->id = id;
    m->body = body;

    m->next = macros;
    macros = m;
    return m;
}

// If tok is a macro, expand it and return true.
// Otherwise, do nothing and return false.
static bool expand_macro(Token **rest, Token *tok) {
    Macro *m = find_macro(tok);
    if (!m) return false;
    *rest = append(m->body, tok->next);
    return true;
}

typedef enum {
    P_INCLUDE,
    P_IF,
    P_ELIF,
    P_ELSE,
    P_ENDIF,
    P_DEFINE,
    P_CNT,
} P_DIRECT;

static struct {
    char *directive;
    uint32_t id;
} dt[] = {
    [P_INCLUDE] = {"include", 0}, [P_IF] = {"if", 0},       [P_ELIF] = {"elif", 0},
    [P_ELSE] = {"else", 0},       [P_ENDIF] = {"endif", 0}, [P_DEFINE] = {"define", 0},
};

// Visit all tokens in `tok` while evaluating preprocessing
// macros and directives.
static Token *preprocess2(Token *tok) {
    if (!dt[0].id) {
        for (size_t i = 0; i < sizeof(dt) / sizeof(dt[0]); ++i)
            dt[i].id = intern(dt[i].directive, strlen(dt[i].directive));
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

        // Pass through if it is not a "#".
        if (!is_hash(tok)) {
            bool concat = (cur_state == BLOCK_ACTIVE);
            while (!is_hash(tok)) {
                if (concat) {
                    expand_macro(&tok, tok);
                    cur = cur->next = tok;
                }
                tok = tok->next;
                if (tok->kind == TK_EOF) goto loop_start;
            }
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
            tok = tok->next;
            if (tok->kind != TK_IDENT) error(tok, "macro name must be an identifier");
            add_macro(tok->id, copy_line(&tok, tok->next));
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
