#include "cxx.h"

static bool opt_E;
static bool opt_S;
static bool opt_ll;
static bool opt_c;
static bool opt_cc1;
static bool opt_hash_hash_hash;
static bool opt_ast_dump;
static bool opt_dump_tokens;
static bool opt_dump_raw_tokens;

static char *opt_o;

char *base_file;
static char *output_file;

static char **input_paths;
static int num_input;

static char **tmpfiles;
static int num_tmpfiles;

char **include_paths;
int num_include_paths;

static void usage(int status) {
    fprintf(stderr,
            "cxx [ -o <path> ] [ -S | -c | -E ] [ -ast-dump ] [ -dump-tokens ]"
            " [ -raw-dump-tokens ] <file>\n");
    exit(status);
}

static bool take_arg(char *arg) {
    char *x[] = {"-o", "-I"};
    for (size_t i = 0; i < sizeof(x) / sizeof(*x); i++)
        if (!strcmp(arg, x[i])) return true;
    return false;
}

static void add_default_include_paths(char *argv0) {
    include_paths[num_include_paths++] = format("%s/include", dirname(strdup(argv0)));

    // Add standard include paths.
    include_paths[num_include_paths++] = "/usr/local/include";
    include_paths[num_include_paths++] = "/usr/include/x86_64-linux-gnu";
    include_paths[num_include_paths++] = "/usr/include";
}

static void define(char *str) {
    char *eq = strchr(str, '=');
    if (eq)
        define_macro(strndup(str, eq - str), eq + 1);
    else
        define_macro(str, "1");
}

static void parse_args(int argc, char **argv) {
    // Make sure that all command line options that take an argument
    // have an argument.
    for (int i = 1; i < argc; i++)
        if (take_arg(argv[i]))
            if (!argv[++i]) usage(1);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-###")) {
            opt_hash_hash_hash = true;
            continue;
        }

        if (!strcmp(argv[i], "--help")) usage(0);

        if (!strcmp(argv[i], "-o")) {
            opt_o = argv[++i];
            continue;
        }

        if (!strncmp(argv[i], "-o", 2)) {
            opt_o = argv[i] + 2;
            continue;
        }

        if (!strcmp(argv[i], "-E")) {
            opt_E = true;
            continue;
        }

        if (!strcmp(argv[i], "-S")) {
            opt_S = true;
            continue;
        }

        if (!strcmp(argv[i], "-emit-llvm")) {
            opt_ll = true;
            continue;
        }

        if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--c")) {
            opt_c = true;
            continue;
        }

        if (!strncmp(argv[i], "-I", 2)) {
            include_paths[num_include_paths++] = argv[i] + 2;
            continue;
        }

        if (!strcmp(argv[i], "-D")) {
            define(argv[++i]);
            continue;
        }

        if (!strncmp(argv[i], "-D", 2)) {
            define(argv[i] + 2);
            continue;
        }

        if (!strcmp(argv[i], "-U")) {
            undef_macro(argv[++i]);
            continue;
        }

        if (!strncmp(argv[i], "-U", 2)) {
            undef_macro(argv[i] + 2);
            continue;
        }

        if (!strcmp(argv[i], "-cc1")) {
            opt_cc1 = true;
            continue;
        }
        if (!strcmp(argv[i], "-cc1-input")) {
            base_file = argv[++i];
            continue;
        }

        if (!strcmp(argv[i], "-cc1-output")) {
            output_file = argv[++i];
            continue;
        }

        if (!strcmp(argv[i], "-ast-dump")) {
            opt_ast_dump = true;
            continue;
        }

        if (!strcmp(argv[i], "-dump-tokens")) {
            opt_dump_tokens = true;
            continue;
        }

        if (!strcmp(argv[i], "-dump-raw-tokens")) {
            opt_dump_raw_tokens = true;
            continue;
        }

        if (argv[i][0] == '-' && argv[i][1] != '\0') fatal("unknown argument: %s", argv[i]);

        input_paths[num_input++] = argv[i];
    }
    if (!num_input && !base_file) fatal("no input files");
}

static FILE *open_outfile(char *path) {
    if (!path || strcmp(path, "-") == 0) return stdout;

    FILE *out = fopen(path, "w");
    if (!out) fatal("cannot create file: %s: %s", path, strerror(errno));
    return out;
}

static bool endswith(char *p, char *q) {
    int len1 = strlen(p);
    int len2 = strlen(q);
    return (len1 >= len2) && !strcmp(p + len1 - len2, q);
}

static void cleanup(void) {
    for (int i = 0; i < num_tmpfiles; i++) unlink(tmpfiles[i]);
}

// Replace file extension
static char *replace_extn(char *tmpl, char *extn) {
    char *tmp = strdup(tmpl);
    char *filename = basename(tmp);
    char *dot = strrchr(filename, '.');
    if (dot) *dot = '\0';
    char *result = format("%s%s", filename, extn);
    free(tmp);
    return result;
}

static char *create_tmpfile(void) {
    char *path = strdup("/tmp/cxx-XXXXXX");
    int fd = mkstemp(path);
    if (fd == -1) fatal("mkstemp failed: %s", strerror(errno));
    close(fd);
    tmpfiles[num_tmpfiles++] = path;
    return path;
}

static void run_subprocess(char **argv) {
    // If -### is given, just print the command line and return.
    if (opt_hash_hash_hash) {
        fprintf(stderr, "%s", argv[0]);
        for (int i = 1; argv[i]; i++) fprintf(stderr, " %s", argv[i]);
        fprintf(stderr, "\n");
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Child process. Run a new command.
        execvp(argv[0], argv);
        fprintf(stderr, "exec failed: %s: %s\n", argv[0], strerror(errno));
        _exit(1);
    }

    // Wait for the child process to finish.
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) fatal("waitpid failed: %s", strerror(errno));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) exit(1);
}

static void run_cc1(int argc, char **argv, char *input, char *output) {
    char **args = calloc(argc + 10, sizeof(char *));
    memcpy(args, argv, argc * sizeof(char *));
    args[argc++] = "-cc1";

    if (input) {
        args[argc++] = "-cc1-input";
        args[argc++] = input;
    }

    if (output) {
        args[argc++] = "-cc1-output";
        args[argc++] = output;
    }

    run_subprocess(args);
}

// Returns true if a given file exists.
bool file_exists(char *path) {
    struct stat st;
    return !stat(path, &st);
}

// --- Compilation stages ---

// Print tokens to stdout. Used for -E.
static void print_tokens(Token *tok) {
    FILE *out = open_outfile(opt_o ? opt_o : "-");

    int line = 1;
    for (; tok->kind != TK_EOF; tok = tok->next) {
        if (line > 1 && tok->is_sol) fprintf(out, "\n");
        if (tok->is_leadingws) fprintf(out, " ");
        fprintf(out, "%.*s", (int)tok->len, tok->loc);
        line++;
    }
    fprintf(out, "\n");
}

// Stage 1: .c → .ll  (cc1: tokenize + preprocess + parse + irgen)
static void cc1(void) {
    Token *tok = tokenize_file(base_file);
    if (!tok) fatal("%s: %s", base_file, strerror(errno));

    if (opt_dump_raw_tokens) dump_tokens(tok);

    tok = preprocess(tok);

    if (opt_dump_tokens) dump_tokens(tok);

    // If -E is given, print out preprocessed C code as a result.
    if (opt_E) {
        print_tokens(tok);
        return;
    }
    join_adjacent_string_literals(tok);

    Module *prog = parse(tok);

    if (opt_ast_dump) dump_ast(prog);

    Module *module = irgen(prog);

    FILE *out = open_outfile(output_file);
    dump_module(module, out);

    fclose(out);
}

// Stage 2: .ll → .s  (via clang)
static void compile(char *input, char *output) {
    char *cmd[] = {"clang", "-S", "-fno-addrsig", "-Wno-override-module", "-x", "ir", input, "-o", output, NULL};
    run_subprocess(cmd);
}

// Stage 3: .s → .o  (via GNU assembler)
static void assemble(char *input, char *output) {
    char *cmd[] = {"as", "-c", input, "-o", output, NULL};
    run_subprocess(cmd);
}

// Stage 4: .o → executable  (via cc)
static void run_linker(char **inputs, int num_ldarg, char *output) {
    char **cmd = vnew(num_ldarg + 16, sizeof(char *));
    cmd[0] = "cc";
    cmd[1] = "-o";
    cmd[2] = output;
    for (int i = 0; i < num_ldarg; i++) cmd[i + 3] = inputs[i];
    run_subprocess(cmd);
}

int main(int argc, char **argv) {
    atexit(cleanup);
    input_paths = vnew(argc, sizeof(char *));
    tmpfiles = vnew(argc * 4, sizeof(char *));
    include_paths = vnew(argc + 4, sizeof(char *));

    init_macros();
    parse_args(argc, argv);

    if (opt_cc1) {
        add_default_include_paths(argv[0]);
        cc1();
        return 0;
    }

    if (num_input > 1 && opt_o && (opt_c || opt_S || opt_E))
        fatal("cannot specify '-o' with '-c' ,'-S' or '-E' with multiple files");

    char **ld_args = vnew(argc, sizeof(char *));
    int num_ldarg = 0;

    for (int i = 0; i < num_input; i++) {
        char *input = input_paths[i];

        char *output;
        if (opt_o)
            output = opt_o;
        else if (opt_S && opt_ll)
            output = replace_extn(input, ".ll");
        else if (opt_S)
            output = replace_extn(input, ".s");
        else
            output = replace_extn(input, ".o");

        // Handle .o — pass straight to linker.
        if (endswith(input, ".o")) {
            ld_args[num_ldarg++] = input;
            continue;
        }

        // Handle .s — assemble, unless -S stops here.
        if (endswith(input, ".s")) {
            if (opt_S) continue;
            assemble(input, output);
            ld_args[num_ldarg++] = output;
            continue;
        }

        // Handle .c (or stdin "-").
        if (!endswith(input, ".c") && strcmp(input, "-")) fatal("unknown file extension: %s", input);

        // -E: .c → stdout
        if (opt_E) {
            run_cc1(argc, argv, input, NULL);
            continue;
        }

        // -S: .c → .ll → .s
        if (opt_S) {
            char *tmp_ll = opt_ll ? output : create_tmpfile();
            run_cc1(argc, argv, input, tmp_ll);
            if (!opt_ll) compile(tmp_ll, output);
            continue;
        }

        // -c: .c → .ll → .s → .o
        if (opt_c) {
            char *tmp_ll = create_tmpfile();
            char *tmp_s = create_tmpfile();
            run_cc1(argc, argv, input, tmp_ll);
            compile(tmp_ll, tmp_s);
            assemble(tmp_s, output);
            continue;
        }

        // Default: .c → .ll → .s → .o → executable
        char *tmp_ll = create_tmpfile();
        char *tmp_s = create_tmpfile();
        char *tmp_o = create_tmpfile();
        run_cc1(argc, argv, input, tmp_ll);
        compile(tmp_ll, tmp_s);
        assemble(tmp_s, tmp_o);
        ld_args[num_ldarg++] = tmp_o;
    }

    if (num_ldarg > 0) run_linker(ld_args, num_ldarg, opt_o ? opt_o : "a.out");

    return 0;
}
