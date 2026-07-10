#include "cxx.h"

int main(int argc, char **argv) {
    if (argc != 2) return 1;

    Token *tok = tokenize_file(argv[1]);
    Obj *prog = parse(tok);
    Module *module = irgen(prog);
    dump_module(module);

    freeall();
    return 0;
}
