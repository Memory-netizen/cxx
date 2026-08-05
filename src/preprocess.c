#include "cxx.h"

static bool is_hash(Token *tok) { return tok->is_sol && tok->kind == TK_HASH; }

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
