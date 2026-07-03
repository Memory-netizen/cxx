#include "cxx.h"

int main(int argc, char **argv) {
    if (argc != 2) return 1;

    Token *tok = tokenize(argv[1]);
    Function *prog = parse(tok);
    irgen(prog);

    freeall();
    return 0;
}
