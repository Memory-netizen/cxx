#include "cxx.h"

static const char *token_kind_name(uint32_t kind) {
    if (kind == TK_EOF) return "eof";
    if (kind >= TK_KEYWORD) return "keyword";

    static const char *table[] = {
        [TK_PUNCT] = "punctuator",
        [TK_COMMA] = "comma",
        [TK_AS] = "equal",
        [TK_ADDAS] = "plus_equal",
        [TK_SUBAS] = "minus_equal",
        [TK_MULAS] = "star_equal",
        [TK_DIVAS] = "slash_equal",
        [TK_MODAS] = "percent_equal",
        [TK_ANDAS] = "and_equal",
        [TK_ORAS] = "or_equal",
        [TK_XORAS] = "xor_equal",
        [TK_LEFTAS] = "left_shift_equal",
        [TK_RIGHTAS] = "right_shift_equal",
        [TK_OR] = "or_or",
        [TK_AND] = "and_and",
        [TK_BOR] = "pipe",
        [TK_XOR] = "caret",
        [TK_BAND] = "ampersand",
        [TK_EQ] = "equal_equal",
        [TK_NE] = "not_equal",
        [TK_LT] = "less",
        [TK_GT] = "greater",
        [TK_LE] = "less_equal",
        [TK_GE] = "greater_equal",
        [TK_LEFT] = "left_shift",
        [TK_RIGHT] = "right_shift",
        [TK_PLUS] = "plus",
        [TK_MINUS] = "minus",
        [TK_STAR] = "star",
        [TK_SLASH] = "slash",
        [TK_MOD] = "percent",
        [TK_INC] = "inc",
        [TK_DEC] = "dec",
        [TK_INVERT] = "tilde",
        [TK_NOT] = "not",
        [TK_DOT] = "dot",
        [TK_ARROW] = "arrow",
        [TK_LPAREN] = "l_paren",
        [TK_RPAREN] = "r_paren",
        [TK_LBRACKET] = "l_bracket",
        [TK_RBRACKET] = "r_bracket",
        [TK_LBRACE] = "l_brace",
        [TK_RBRACE] = "r_brace",
        [TK_SEMI] = "semicolon",
        [TK_COLON] = "colon",
        [TK_COLONCOLON] = "colon_colon",
        [TK_QUESTION] = "question",
        [TK_ELLIPSIS] = "ellipsis",
        [TK_HASH] = "hash",
        [TK_HASHHASH] = "hash_hash",
        [TK_IDENT] = "identifier",
        [TK_NUM] = "numeric_constant",
        [TK_PPNUM] = "pp_number",
        [TK_CHARLIT] = "char_literal",
        [TK_STRLIT] = "string_literal",
    };

    const char *name = table[kind];
    return name ? name : "unknown";
}

void dump_tokens(Token *tok) {
    while (tok && tok->kind != TK_EOF) {
        fprintf(stdout, "%-20s ‘%-.*s’", token_kind_name(tok->kind), (int)tok->len, tok->loc);

        if (tok->is_sol) fprintf(stdout, " [StartOfLine]");
        if (tok->is_leadingws) fprintf(stdout, " [LeadingSpace]");

        fprintf(stdout, "   Loc=<%s:%d:%d>\n", tok->file->name, tok->line, tok->col);

        tok = tok->next;
    }
    fprintf(stdout, "%-20s ‘’   Loc=<%s:%d:%d>\n", "eof", tok->file->name, tok->line, tok->col);
}
