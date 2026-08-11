#include "cxx.h"

typedef enum { FILE_NONE, FILE_C, FILE_ASM, FILE_OBJ } FileType;

static FileType opt_x;
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
    char *x[] = {"-o", "-I", "-include", "-x"};
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

static FileType parse_opt_x(char *s) {
    if (!strcmp(s, "c")) return FILE_C;
    if (!strcmp(s, "assembler")) return FILE_ASM;
    if (!strcmp(s, "none")) return FILE_NONE;
    fatal("<command line>: unknown argument for -x: %s", s);
    return FILE_NONE;
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

        if (!strcmp(argv[i], "-include")) {
            cmd_include_file(argv[++i]);
            continue;
        }

        if (!strcmp(argv[i], "-D")) {
            cmd_define_macro(argv[++i]);
            continue;
        }

        if (!strncmp(argv[i], "-D", 2)) {
            cmd_define_macro(argv[i] + 2);
            continue;
        }

        if (!strcmp(argv[i], "-U")) {
            cmd_undef_macro(argv[++i]);
            continue;
        }

        if (!strncmp(argv[i], "-U", 2)) {
            cmd_undef_macro(argv[i] + 2);
            continue;
        }

        if (!strcmp(argv[i], "-x")) {
            opt_x = parse_opt_x(argv[++i]);
            continue;
        }

        if (!strncmp(argv[i], "-x", 2)) {
            opt_x = parse_opt_x(argv[i] + 2);
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

        // These options are ignored for now.
        if (!strncmp(argv[i], "-O", 2) || !strncmp(argv[i], "-W", 2) || !strncmp(argv[i], "-g", 2) ||
            !strncmp(argv[i], "-std=", 5) || !strcmp(argv[i], "-ffreestanding") || !strcmp(argv[i], "-fno-builtin") ||
            !strcmp(argv[i], "-fno-omit-frame-pointer") || !strcmp(argv[i], "-fno-stack-protector") ||
            !strcmp(argv[i], "-fno-strict-aliasing") || !strcmp(argv[i], "-m64") || !strcmp(argv[i], "-mno-red-zone") ||
            !strcmp(argv[i], "-w"))
            continue;

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

    int cur_line = 0;
    for (; tok->kind != TK_EOF; tok = tok->next) {
        if (cur_line > 0 && tok->is_sol) {
            int line, col;
            Token *orig = tok;
            while (orig->origin) orig = orig->origin;
            get_location(orig->file, orig->loc, &line, &col);

            while (cur_line < line + orig->line_delta) {
                fprintf(out, "\n");
                cur_line++;
            }
            fprintf(out, "\n");
            if (col > 2) fprintf(out, "%*s", col - 2, "");
            cur_line++;
        }
        if (tok->kind == TK_LINE) {
            fprintf(out, "# %lu \"%s\"", tok->val, str(tok->filename));
            cur_line = tok->val;
            continue;
        }
        if (tok->is_leadingws) fprintf(out, " ");
        fprintf(out, "%.*s", (int)tok->len, tok->loc);
    }
    fprintf(out, "\n");
}

static Token *filter_tokens(Token *tok) {
    Token dummy = {}, *cur = &dummy;
    for (; tok; tok = tok->next) {
        if (tok->kind == TK_LINE) continue;
        if (tok->kind == TK_WS) continue;
        if (tok->kind == TK_NL) continue;
        if (tok->kind == TK_COMMENT) continue;
        cur = cur->next = tok;
    }
    return dummy.next;
}

// Stage 1: .c → .ll  (cc1: tokenize + preprocess + parse + irgen)
static void cc1(void) {
    Token *tok = tokenize_file(base_file);
    if (!tok) fatal("%s: %s", base_file, strerror(errno));

    if (opt_dump_raw_tokens) dump_raw_tokens(tok);

    tok = preprocess(tok);

    if (opt_dump_tokens) dump_tokens(tok);

    // If -E is given, print out preprocessed C code as a result.
    if (opt_E) {
        print_tokens(tok);
        return;
    }

    tok = filter_tokens(tok);

    join_adjacent_string_literals(tok);

    Module *prog = parse(tok);

    if (opt_ast_dump) dump_ast(prog);

    fold_ast(prog);

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

static FileType get_file_type(char *filename) {
    if (endswith(filename, ".o")) return FILE_OBJ;

    if (opt_x != FILE_NONE) return opt_x;

    if (endswith(filename, ".c")) return FILE_C;
    if (endswith(filename, ".s")) return FILE_ASM;

    fatal("<command line>: unknown file extension: %s", filename);
    return FILE_NONE;
}

int main(int argc, char **argv) {
    atexit(cleanup);
    input_paths = vnew(argc, sizeof(char *));
    tmpfiles = vnew(argc * 4, sizeof(char *));
    include_paths = vnew(argc + 4, sizeof(char *));

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

        FileType type = get_file_type(input);

        // Handle .o — pass straight to linker.
        if (type == FILE_OBJ) {
            ld_args[num_ldarg++] = input;
            continue;
        }

        // Handle .s — assemble, unless -S stops here.

        if (type == FILE_ASM) {
            if (opt_S) continue;
            assemble(input, output);
            if (!opt_c) ld_args[num_ldarg++] = output;
            continue;
        }

        // Handle .c (or stdin "-").
        assert(type == FILE_C);

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
