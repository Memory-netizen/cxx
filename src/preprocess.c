#include "cxx.h"

static bool is_hash(Token *tok) { return tok->is_sol && tok->kind == TK_HASH; }

// Some preprocessor directives such as #include allow extraneous
// tokens before newline. This function skips such tokens.
static Token *skip_line(Token *tok) {
    if (tok->is_sol) return tok;
    diag("warning", tok, "extra token");
    while (tok->is_sol) tok = tok->next;
    return tok;
}

static Token *copy_token(Token *tok) {
    Token *t = emalloc(sizeof(Token));
    *t = *tok;
    t->next = NULL;
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

// Visit all tokens in `tok` while evaluating preprocessing
// macros and directives.
static Token *preprocess2(Token *tok) {
    Token dummy = {};
    Token *cur = &dummy;

    while (tok->kind != TK_EOF) {
        // Pass through if it is not a "#".
        if (!is_hash(tok)) {
            cur = cur->next = tok;
            tok = tok->next;
            continue;
        }

        tok = tok->next;

        if (tok->id == intern("include", 7)) {
            tok = tok->next;

            if (tok->kind != TK_STRLIT) error(tok, "expected a filename");

            char *path = format("%s/%s", dirname(strdup(tok->file->name)), str(tok->id));
            Token *tok2 = tokenize_file(path);
            if (!tok2) error(tok, "%s", strerror(errno));
            tok = skip_line(tok->next);
            tok = append(tok2, tok);
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
