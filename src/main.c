#include "cxx.h"

static char *opt_o;

static char *input_path;

static void usage(int status) {
    fprintf(stderr, "cxx [ -o <path> ] <file>\n");
    exit(status);
}

static void parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) usage(0);

        if (!strcmp(argv[i], "-o")) {
            if (!argv[++i]) usage(1);
            opt_o = argv[i];
            continue;
        }

        if (!strncmp(argv[i], "-o", 2)) {
            opt_o = argv[i] + 2;
            continue;
        }

        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "unknown argument: %s", argv[i]);
            exit(1);
        }

        input_path = argv[i];
    }

    if (!input_path) {
        fprintf(stderr, "no input files");
        exit(1);
    }
}

static FILE *open_file(char *path) {
    if (!path || strcmp(path, "-") == 0) return stdout;

    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "cannot open output file: %s: %s", path, strerror(errno));
        exit(1);
    }
    return out;
}

int main(int argc, char **argv) {
    parse_args(argc, argv);

    // Tokenize and parse.
    Token *tok = tokenize_file(input_path);
    Obj *prog = parse(tok);

    // Traverse the AST to emit assembly.
    Module *module = irgen(prog);
    FILE *out = open_file(opt_o);
    dump_module(module, out);

    freeall();
    return 0;
}
