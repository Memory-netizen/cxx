#include "cxx.h"

// `#if` can be nested, so we use a stack to manage nested `#if`s.
typedef enum {
    BLOCK_DEAD,     // this block is dead
    BLOCK_PENDING,  // this block is pending for #elif/#else
    BLOCK_ACTIVE,   // this block is activing
} BlockState;

typedef struct CondIncl CondIncl;
struct CondIncl {
    struct CondIncl *next;
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
static Token *append(Token *tok1, Token *tok2) {
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

    for (; !tok->is_sol; tok = tok->next) cur = cur->next = copy_token(tok);

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
    int64_t val = const_expr(&rest2, expr);
    if (rest2->kind != TK_EOF) error(rest2, "extra token");
    return val;
}

// check #elif / #else valid
static void check_elif_else_valid(Token *dt) {
    if (!cond_incl->next) error(dt, "%s without #if", str(dt->next->id));
    if (cond_incl->else_seen) {
        diag("error", dt, "%s after #else", str(dt->next->id));
        error(cond_incl->if_tok, "the conditional began here");
    }
}

typedef enum {
    P_INCLUDE,
    P_IF,
    P_ELIF,
    P_ELSE,
    P_ENDIF,
    P_CNT,
} P_DIRECT;

static struct {
    char *directive;
    uint32_t id;
} dt[] = {
    [P_INCLUDE] = {"include", 0}, [P_IF] = {"if", 0},       [P_ELIF] = {"elif", 0},
    [P_ELSE] = {"else", 0},       [P_ENDIF] = {"endif", 0},
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

    while (tok->kind != TK_EOF) {
        BlockState cur_state = cond_incl->state;

        // Pass through if it is not a "#".
        if (!is_hash(tok)) {
            bool concat = (cur_state == BLOCK_ACTIVE);
            while (!is_hash(tok)) {
                if (concat) cur = cur->next = tok;
                tok = tok->next;
                if (tok->kind == TK_EOF) goto end;
            }
        }

        Token *tk_hash = tok;
        tok = tok->next;

        if (tok->id == dt[P_INCLUDE].id) {
            if (cur_state != BLOCK_ACTIVE) continue;
            tok = tok->next;

            if (tok->kind != TK_STRLIT) error(tok, "expected a filename");

            char *path = str(tok->id);
            if (path[0] != '/') path = format("%s/%s", dirname(strdup(tok->file->name)), path);

            Token *tok2 = tokenize_file(path);
            if (!tok2) error(tok, "%s", strerror(errno));
            tok = skip_line(tok->next);
            tok = append(tok2, tok);
            continue;
        }

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
            check_elif_else_valid(tk_hash);
            if (cur_state != BLOCK_PENDING) {
                cond_incl->state = BLOCK_DEAD;
            } else {
                int64_t val = eval_const_expr(&tok, tok);
                cond_incl->state = val ? BLOCK_ACTIVE : BLOCK_PENDING;
            }
            continue;
        }

        if (tok->id == dt[P_ELSE].id) {
            check_elif_else_valid(tk_hash);
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

        // `#`-only line is legal. It's called a null directive.
        if (tok->is_sol) continue;

        error(tok, "invalid preprocessor directive");
    }

end:

    cur->next = tok;
    return dummy.next;
}

// Entry point function of the preprocessor.
Token *preprocess(Token *tok) {
    tok = preprocess2(tok);
    convert_pptoken(tok);
    return tok;
}
