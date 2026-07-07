#include "cxx.h"

int main(int argc, char **argv) {
    if (argc != 2) return 1;

    Token *tok = tokenize(argv[1]);
    Fn *prog = parse(tok);
    Fn *fn = irgen(prog);
    dump_fn(fn);

    freeall();
    return 0;
}
