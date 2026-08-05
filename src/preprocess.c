#include "cxx.h"

// Entry point function of the preprocessor.
Token *preprocess(Token *tok) {
    convert_pptoken(tok);
    return tok;
}
