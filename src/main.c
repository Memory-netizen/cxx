#include "config.h"
#include "cxx.h"

Target T;
extern Target T_amd64;
extern Target T_arm64;
extern Target T_rv64;
extern Target T_rv32;
extern Target T_rv32b;

// Choosing a target fixes more than the target table itself: a couple of
// types take a field from it, and they are process-wide.
static void select_target(Target *t) {
    T = *t;
    bitint_align_wide(T.bitint_align);
}

typedef enum {
    FILE_NONE,
    FILE_C,
    FILE_ASM,
    FILE_ASM_PP,  // .S: assembler the preprocessor runs first
    FILE_OBJ,
    FILE_AR,
    FILE_DSO,
} FileType;

static FileType opt_x;
static bool opt_E;
static bool opt_M;
static bool opt_MM;
static bool opt_MD;
static bool opt_MP;
static bool opt_S;
static bool opt_ll;
static bool opt_c;
static bool opt_fsyntax_only;
static bool opt_P;
static bool opt_cc1;
static bool opt_hash_hash_hash;
static bool opt_ast_dump;
static bool opt_dump_tokens;
static bool opt_dump_raw_tokens;

static char *opt_o;
static char *opt_MF;
// -Wa,<options> collected for the assembly stage; see the argument loop.
static char *asm_args[16];
static int num_asmarg;
static char *opt_MT;

char *base_file;
static char *output_file;

static char **input_paths;
static int num_input;

static char **tmpfiles;
static int num_tmpfiles;

static char *opt_sysroot;

char **include_paths;
int num_include_paths;

char **embed_dirs;
int num_embed_dirs;

char **std_include_paths;
int num_std_include_paths;

char **dirafter;
int num_dirafter;

char **ld_extra_args;
int num_ld_exarg;

bool opt_fpic;
bool opt_fcommon;
bool opt_nowarn;
// Every group on to start with; see the enum in cxx.h.
// Every group that is on by default; see the enum in cxx.h. WG_OFF_DEFAULT
// names the ones -Wall does not enable.
uint32_t opt_wgroups = WG_ALL & ~(uint32_t)WG_OFF_DEFAULT;
bool opt_werror;
bool opt_pedantic;
bool opt_pedantic_errors;

static void usage(int status) {
    fprintf(stderr,
            "cxx [ -o <path> ] [ -S | -c | -E | -fsyntax-only ] [ -ast-dump ] [ -dump-tokens ]"
            " [ -raw-dump-tokens | -dump-raw-tokens ] <file>\n");
    exit(status);
}

static bool take_arg(char *arg) {
    char *x[] = {"-o",  "-I",       "-include", "-x",       "-idirafter", "-MF",        "-MT",
                 "-MQ", "-Xlinker", "-target",  "-isystem", "--sysroot",  "-embed-dir", "--embed-dir"};
    for (size_t i = 0; i < sizeof(x) / sizeof(*x); i++)
        if (!strcmp(arg, x[i])) return true;
    return false;
}

static void machine_flag_macros(void);

// The -m... flags the user gave (-msse4.2, -mavx2, -march=..., -mno-...).
// clang is the backend, so they are the backend's to honour: they are
// collected in parse_args() and handed to both clang stages.
static char **machine_args;
static int num_machine;
// True when one of them named the architecture (-march=/-mcpu=): the target's
// own -march is only a default then, and the user's is the one that counts.
static bool machine_sets_arch;

static char *get_clang_resource_dir(void) {
    FILE *fp = popen("clang -print-resource-dir 2>/dev/null", "r");
    if (!fp) return NULL;
    char buf[4096];
    char *result = NULL;
    if (fgets(buf, sizeof(buf), fp)) {
        buf[strcspn(buf, "\n")] = '\0';
        result = strdup(buf);
    }
    pclose(fp);
    return result;
}

#include <glob.h>
static void add_gcc_include_paths(char *triple) {
    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "/usr/lib/gcc/%s/*/include", triple);

    glob_t g;
    if (glob(pattern, 0, NULL, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc; i++)
            std_include_paths[num_std_include_paths++] = include_paths[num_include_paths++] = strdup(g.gl_pathv[i]);
        globfree(&g);
    }
}

static bool is_host_triple(char *triple) {
    static char *host = NULL;
    if (!host) {
        FILE *fp = popen("cc -dumpmachine 2>/dev/null", "r");
        if (fp) {
            char buf[256];
            if (fgets(buf, sizeof(buf), fp)) {
                buf[strcspn(buf, "\n")] = '\0';
                host = strdup(buf);
            }
            pclose(fp);
        }
    }
    return host && !strcmp(host, triple);
}

static void add_default_include_paths(char *argv0) {
    for (int i = 0; i < num_std_include_paths; i++) include_paths[num_include_paths++] = std_include_paths[i];
    char *exe_inc = format("%s/include", dirname(strdup(argv0)));
    std_include_paths[num_std_include_paths++] = include_paths[num_include_paths++] = exe_inc;

    char *res_dir = get_clang_resource_dir();
    if (res_dir) {
        char *inc = format("%s/include", res_dir);
        std_include_paths[num_std_include_paths++] = include_paths[num_include_paths++] = inc;
    }

    char *sysroot = opt_sysroot ?: T.sysroot;
    if (sysroot) {
        std_include_paths[num_std_include_paths++] = include_paths[num_include_paths++] =
            format("%s/usr/include", sysroot);
        std_include_paths[num_std_include_paths++] = include_paths[num_include_paths++] =
            format("%s/usr/include/%s", sysroot, T.triple);
    } else {
        std_include_paths[num_std_include_paths++] = include_paths[num_include_paths++] =
            format("/usr/%s/include", T.triple);
        std_include_paths[num_std_include_paths++] = include_paths[num_include_paths++] =
            format("/usr/include/%s", T.triple);
    }

    if (is_host_triple(T.triple)) {
        std_include_paths[num_std_include_paths++] = include_paths[num_include_paths++] = "/usr/local/include";
        std_include_paths[num_std_include_paths++] = include_paths[num_include_paths++] = "/usr/include";
    }

    // GCC's include directory goes after the system's, which is where clang
    // puts the equivalent of it (nowhere: clang reaches glibc directly). It
    // has to, because clang's own <limits.h> -- the one in the resource
    // directory above -- does `#include_next <limits.h>` and sets
    // _GCC_LIMITS_H_ first, precisely so that gcc's copy steps aside. With
    // gcc's directory next in line that copy *is* what include_next finds, it
    // steps aside as asked, and the chain ends without glibc's limits.h ever
    // being read: SSIZE_MAX and the rest of <bits/posix1_lim.h> were missing,
    // which is what cpython's pyport.h reaches for.
    add_gcc_include_paths(T.triple);
}

static void add_dirafter(void) {
    for (int i = 0; i < num_dirafter; i++)
        std_include_paths[num_std_include_paths++] = include_paths[num_include_paths++] = dirafter[i];
}

static FileType parse_opt_x(char *s) {
    if (!strcmp(s, "c")) return FILE_C;
    if (!strcmp(s, "assembler")) return FILE_ASM;
    // The spelling the kernel's scripts use; gcc and clang accept both.
    if (!strcmp(s, "assembler-with-cpp")) return FILE_ASM_PP;
    if (!strcmp(s, "none")) return FILE_NONE;
    fatal("<command line>: unknown argument for -x: %s", s);
    return FILE_NONE;
}

char *quote_makefile(const char *s) {
    char *buf = emalloc(strlen(s) * 2 + 1);
    for (int i = 0, j = 0; s[i]; i++) {
        switch (s[i]) {
            case '$':
                buf[j++] = '$';
                buf[j++] = '$';
                break;
            case '#':
            case ' ':
            case '\t':
            case '\\':
            case ':':
            case '=':
            case '%':
                buf[j++] = '\\';
                buf[j++] = s[i];
                break;
            default:
                buf[j++] = s[i];
                break;
        }
    }
    return buf;
}

static struct {
    char *alias;
    Target *target;
} target_aliases[] = {
    {"amd64", &T_amd64},
    {"x86_64", &T_amd64},
    {"x86-64", &T_amd64},
    {"x86_64-unknown-linux-gnu", &T_amd64},
    {"x86_64-linux-gnu", &T_amd64},

    {"arm64", &T_arm64},
    {"aarch64", &T_arm64},
    {"aarch64-unknown-linux-gnu", &T_arm64},
    {"aarch64-linux-gnu", &T_arm64},

    {"rv64", &T_rv64},
    {"riscv64", &T_rv64},
    {"riscv64-unknown-linux-gnu", &T_rv64},
    {"riscv64-linux-gnu", &T_rv64},

    {"rv32", &T_rv32},
    {"riscv32", &T_rv32},
    {"rv32bare", &T_rv32b},
    {"riscv32-none-elf", &T_rv32b},
    {"riscv32-linux-gnu", &T_rv32},

    {NULL, NULL},
};

static Target *lookup_target(char *s) {
    for (int i = 0; target_aliases[i].alias; i++)
        if (!strcmp(s, target_aliases[i].alias)) return target_aliases[i].target;
    return NULL;
}

// The warning groups a -W switch can name. Kept beside the parser rather
// than in cxx.h because it is the driver's interface, not the compiler's.
static const struct {
    const char *name;
    int bit;
} wgroup_names[] = {
    {"deprecated-declarations", WG_DEPRECATED},
    // Aliases: gcc and clang name several of these differently, so both
    // spellings map to one group.
    {"invalid-memory-model", WG_MEMORY_ORDER},  // gcc's name
    {"unknown-attributes", WG_ATTRIBUTES},      // clang's name
    {"ignored-attributes", WG_ATTRIBUTES},      // clang's name
    {"invalid-noreturn", WG_INVALID_NORETURN},
    {"builtin-macro-redefined", WG_BUILTIN_MACRO_REDEFINED},
    {"extra-tokens", WG_EXTRA_TOKENS},  // clang's name
    {"endif-labels", WG_EXTRA_TOKENS},  // gcc's name for the same diagnostic
    {"implicit-fallthrough", WG_IMPLICIT_FALLTHROUGH},
    {"shift-count-negative", WG_SHIFT_COUNT_NEGATIVE},
    {"shift-count-overflow", WG_SHIFT_COUNT_OVERFLOW},
    {"unused-result", WG_UNUSED_RESULT},
    {"attributes", WG_ATTRIBUTES},
    {"return-type", WG_RETURN_TYPE},
    {"cpp", WG_CPP},
    {"atomic-memory-ordering", WG_MEMORY_ORDER},
    {"unused-variable", WG_UNUSED_VARIABLE},
    {"unused-function", WG_UNUSED_FUNCTION},
    {"unused-const-variable", WG_UNUSED_CONST_VARIABLE},
    {"static-local-in-inline", WG_STATIC_LOCAL_IN_INLINE},
    {"tentative-definition-array", WG_TENTATIVE_DEFINITION_ARRAY},
    {"constant-conversion", WG_CONSTANT_CONVERSION},
    {"literal-conversion", WG_LITERAL_CONVERSION},
    {"float-conversion", WG_FLOAT_CONVERSION},
    {"literal-range", WG_LITERAL_RANGE},
    {"sign-compare", WG_SIGN_COMPARE},
    {"discarded-qualifiers", WG_DISCARDED_QUALIFIERS},
    {"pointer-sign", WG_POINTER_SIGN},  // gcc's and clang's name
    {"implicit-const-int-float-conversion", WG_CONST_INT_FLOAT_CONVERSION},
    {"implicit-function-declaration", WG_IMPLICIT_FUNCTION_DECLARATION},
};

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

        if (!strcmp(argv[i], "-M")) {
            opt_M = true;
            continue;
        }

        if (!strcmp(argv[i], "-MM")) {
            opt_M = opt_MM = true;
            continue;
        }

        if (!strcmp(argv[i], "-MD")) {
            opt_MD = true;
            continue;
        }

        if (!strcmp(argv[i], "-MMD")) {
            opt_MM = opt_MD = true;
            continue;
        }

        if (!strcmp(argv[i], "-S")) {
            opt_S = true;
            continue;
        }

        // Check the translation unit and write nothing: no .ll, no .s, no
        // .o, no link, and -o is ignored, which is what gcc and clang do with
        // this flag (a build system passes its usual -o along). -E and -M
        // are handled before it in the driver, so preprocessing still wins,
        // as it does in both of them.
        if (!strcmp(argv[i], "-fsyntax-only")) {
            opt_fsyntax_only = true;
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

        if (!strcmp(argv[i], "-P")) {
            opt_P = true;
            continue;
        }

        if (!strncmp(argv[i], "--sysroot=", 10)) {
            opt_sysroot = argv[i] + 10;
            ld_extra_args[num_ld_exarg++] = argv[i];
            continue;
        }

        if (!strcmp(argv[i], "--sysroot")) {
            opt_sysroot = argv[++i];
            ld_extra_args[num_ld_exarg++] = format("--sysroot=%s", opt_sysroot);
            continue;
        }

        if (!strcmp(argv[i], "-I")) {
            include_paths[num_include_paths++] = argv[++i];
            continue;
        }

        if (!strncmp(argv[i], "-I", 2)) {
            include_paths[num_include_paths++] = argv[i] + 2;
            continue;
        }

        if (!strcmp(argv[i], "-embed-dir") || !strcmp(argv[i], "--embed-dir")) {
            embed_dirs[num_embed_dirs++] = argv[++i];
            continue;
        }

        if (!strncmp(argv[i], "-embed-dir=", 11)) {
            embed_dirs[num_embed_dirs++] = argv[i] + 11;
            continue;
        }

        if (!strncmp(argv[i], "--embed-dir=", 12)) {
            embed_dirs[num_embed_dirs++] = argv[i] + 12;
            continue;
        }

        if (!strcmp(argv[i], "-isystem")) {
            std_include_paths[num_std_include_paths++] = argv[++i];
            continue;
        }

        if (!strcmp(argv[i], "-idirafter")) {
            dirafter[num_dirafter++] = argv[++i];
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

        if (!strncmp(argv[i], "-l", 2) || !strncmp(argv[i], "-Wl,", 4)) {
            input_paths[num_input++] = argv[i];
            continue;
        }

        if (!strcmp(argv[i], "-s")) {
            ld_extra_args[num_ld_exarg++] = "-s";
            continue;
        }

        if (!strcmp(argv[i], "-static")) {
            ld_extra_args[num_ld_exarg++] = "-static";
            continue;
        }

        if (!strcmp(argv[i], "-shared")) {
            ld_extra_args[num_ld_exarg++] = "-shared";
            continue;
        }

        // -pthread is one switch on two levels: the linker has to see the
        // thread library, and the preprocessor has to see _REENTRANT, which
        // POSIX reserves for a program that uses threads. A program using
        // <threads.h> or <pthread.h> otherwise needs three separate flags.
        if (!strcmp(argv[i], "-pthread")) {
            ld_extra_args[num_ld_exarg++] = "-pthread";
            cmd_define_macro("_REENTRANT");
            continue;
        }

        // Whether plain char is signed is implementation-defined (6.2.5) and
        // the ABI settles it -- signed on x86-64, unsigned on the ARM and
        // RISC-V ABIs. These override the target's default. char is one
        // shared Type, so the change reaches every use; signed char and
        // unsigned char are separate types and are untouched.
        if (!strcmp(argv[i], "-funsigned-char")) {
            T.ty_char->is_unsigned = true;
            continue;
        }

        // -fno-signed-char is the same request written the other way round,
        // which is how both references read it.
        if (!strcmp(argv[i], "-fsigned-char") || !strcmp(argv[i], "-fno-unsigned-char")) {
            T.ty_char->is_unsigned = false;
            continue;
        }

        if (!strcmp(argv[i], "-fno-signed-char")) {
            T.ty_char->is_unsigned = true;
            continue;
        }

        if (!strcmp(argv[i], "-L")) {
            ld_extra_args[num_ld_exarg++] = "-L";
            ld_extra_args[num_ld_exarg++] = argv[++i];
            continue;
        }

        if (!strncmp(argv[i], "-L", 2)) {
            ld_extra_args[num_ld_exarg++] = "-L";
            ld_extra_args[num_ld_exarg++] = argv[i] + 2;
            continue;
        }

        if (!strcmp(argv[i], "-Xlinker")) {
            ld_extra_args[num_ld_exarg++] = argv[++i];
            continue;
        }

        if (!strcmp(argv[i], "-MF")) {
            opt_MF = argv[++i];
            continue;
        }

        if (!strcmp(argv[i], "-MT")) {
            if (opt_MT == NULL)
                opt_MT = argv[++i];
            else
                opt_MT = format("%s %s", opt_MT, argv[++i]);
            continue;
        }

        if (!strcmp(argv[i], "-MQ")) {
            if (opt_MT == NULL)
                opt_MT = quote_makefile(argv[++i]);
            else
                opt_MT = format("%s %s", opt_MT, quote_makefile(argv[++i]));
            continue;
        }

        if (!strcmp(argv[i], "-MP")) {
            opt_MP = true;
            continue;
        }

        if (!strcmp(argv[i], "-fpic") || !strcmp(argv[i], "-fPIC")) {
            opt_fpic = true;
            continue;
        }

        if (!strcmp(argv[i], "-fno-pic") || !strcmp(argv[i], "-fno-PIC")) {
            opt_fpic = false;
            continue;
        }

        if (!strcmp(argv[i], "-fcommon")) {
            opt_fcommon = true;
            continue;
        }

        if (!strcmp(argv[i], "-fno-common")) {
            opt_fcommon = false;
            continue;
        }

        if (!strcmp(argv[i], "-target")) {
            char *t_name = argv[++i];
            Target *t = lookup_target(t_name);
            if (!t) {
                fprintf(stderr, "unknown target '%s'\n", t_name);
                exit(1);
            }
            select_target(t);
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

        if (!strcmp(argv[i], "-raw-dump-tokens") || !strcmp(argv[i], "-dump-raw-tokens")) {
            opt_dump_raw_tokens = true;
            continue;
        }

        if (!strcmp(argv[i], "-w")) {
            opt_nowarn = true;
            continue;
        }

        // -W<group> / -Wno-<group> / -Wall. An unrecognized group is an
        // error: accepting every -W* and discarding it is how a typo like
        // -Wno-bogus-option used to pass unnoticed, and gcc and clang both
        // reject it.
        // -Wp,<options> hands options to the preprocessor and -Wa,<options>
        // to the assembler. Neither is a warning switch, though both start
        // with -W: `-Wa,--version` used to be read as -W + "a,--version" and
        // rejected as an unknown warning group. The kernel runs exactly that
        // (scripts/as-version.sh) and passes -Wp,-MMD,$(depfile) on every
        // compile, which failed the same way.
        if (!strncmp(argv[i], "-Wp,", 4)) {
            // The inner options carry their own dash: -Wp,-MMD,file.
            char *p = argv[i] + 4;
            if (*p == '-') p++;
            // The dependency writers are the ones cxx has: -Wp,-MD,file and
            // -Wp,-MMD,file are what -MD/-MMD with -MF file spell.
            if (!strncmp(p, "MD", 2) || !strncmp(p, "MMD", 3)) {
                if (p[1] == 'M') opt_MM = true;
                opt_MD = true;
                char *file = strchr(p, ',');
                if (file && file[1]) opt_MF = file + 1;
                continue;
            }
            fatal("-Wp,%s: unsupported preprocessor option", p);
        }

        if (!strncmp(argv[i], "-Wa,", 4)) {
            char *a = argv[i] + 4;
            if (num_asmarg < (int)(sizeof(asm_args) / sizeof(asm_args[0]))) asm_args[num_asmarg++] = a;
            continue;
        }

        if (!strncmp(argv[i], "-W", 2)) {
            const char *name = argv[i] + 2;
            bool off = !strncmp(name, "no-", 3);
            if (off) name += 3;
            // -Werror and -Wpedantic are handled outside the table;
            // everything else here is a group.
            // `off` has already been read off the name, so -Wno-pedantic
            // clears the mode rather than setting it.
            if (!strcmp(name, "pedantic")) {
                opt_pedantic = !off;
                continue;
            }
            if (strcmp(name, "error")) {
                if (!strcmp(name, "all") || !strcmp(name, "extra")) {
                    // -Wall turns on what is on by default; it does not reach the
                    // WG_OFF_DEFAULT groups, which need their own flag.
                    opt_wgroups |= WG_ALL & ~(uint32_t)WG_OFF_DEFAULT;
                    continue;
                }
                int bit = 0;
                for (size_t k = 0; k < sizeof(wgroup_names) / sizeof(wgroup_names[0]); k++)
                    if (!strcmp(name, wgroup_names[k].name)) {
                        bit = wgroup_names[k].bit;
                        break;
                    }
                if (!bit) fatal("unknown warning group: -W%s%s", off ? "no-" : "", name);
                if (off)
                    opt_wgroups &= ~(uint32_t)bit;
                else
                    opt_wgroups |= (uint32_t)bit;
                continue;
            }
        }

        if (!strcmp(argv[i], "-pedantic") || !strcmp(argv[i], "-Wpedantic")) {
            opt_pedantic = true;
            continue;
        }

        if (!strcmp(argv[i], "-pedantic-errors")) {
            opt_pedantic = opt_pedantic_errors = true;
            continue;
        }

        if (!strcmp(argv[i], "-Werror")) {
            opt_werror = true;
            continue;
        }

        // Machine flags are the backend's (see machine_args). -m32 and -mx32
        // change the *target*, which cxx's own type model does not have, so
        // taking them would mean 64-bit IR assembled as 32-bit code -- they
        // are refused rather than silently mis-compiled.
        if (!strncmp(argv[i], "-m", 2) && argv[i][2]) {
            if (!strcmp(argv[i], "-m32") || !strcmp(argv[i], "-mx32"))
                fatal("%s: cxx has no 32-bit x86 target (use -target i386-unknown-linux-gnu)", argv[i]);
            if (!strncmp(argv[i], "-march=", 7) || !strncmp(argv[i], "-mcpu=", 6)) machine_sets_arch = true;
            if (!machine_args)
                machine_args = vnew(8, sizeof(char *));
            else
                machine_args = vgrow(machine_args, (num_machine + 8) * sizeof(char *));
            machine_args[num_machine++] = argv[i];
            continue;
        }

        // An ISO mode defines __STRICT_ANSI__, a GNU one leaves it
        // undefined: that is how headers and projects ask whether GNU
        // extensions are in play. cpython's Py_ARRAY_LENGTH() puts
        // Py_BUILD_ASSERT_EXPR() -- a comma expression -- into the array
        // bound only when it is *not* defined, and a comma expression is
        // not an integer constant expression, so the constant bound of
        // `slotdefs_dups` came out variably modified (Objects/typeobject.c).
        if (!strncmp(argv[i], "-std=", 5)) {
            if (strncmp(argv[i] + 5, "gnu", 3)) cmd_define_macro("__STRICT_ANSI__=1");
            continue;
        }

        // These options are ignored for now.
        if (!strncmp(argv[i], "-O", 2) || !strncmp(argv[i], "-g", 2) || !strcmp(argv[i], "-ffreestanding") ||
            !strcmp(argv[i], "-fno-builtin") || !strcmp(argv[i], "-fno-omit-frame-pointer") ||
            !strcmp(argv[i], "-fno-stack-protector") || !strcmp(argv[i], "-fno-strict-aliasing") ||
            !strcmp(argv[i], "-m64") || !strcmp(argv[i], "-mno-red-zone"))
            continue;

        if (argv[i][0] == '-' && argv[i][1] != '\0') fatal("unknown argument: %s", argv[i]);

        input_paths[num_input++] = argv[i];
    }
    if (!num_input && !base_file) fatal("no input files");

    // -E implies that the input is the C macro language.
    if (opt_E) opt_x = FILE_C;
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
    // A child killed by a signal died without a diagnostic of its own: a
    // crash in cc1, or in the tools a later stage runs. Saying so is the
    // difference between a bug report and a compiler that silently refuses
    // to compile the file -- which is how the three crashes this round fixes
    // went unnoticed.
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "cxx: internal compiler error: %s killed by signal %d\n", argv[0], WTERMSIG(status));
        exit(1);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) exit(1);
}

static void run_cc1(int argc, char **argv, char *input, char *output) {
    char **args = emalloc((argc + 10) * sizeof(char *));
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

// Whether a space has to be printed between two tokens of -E output. What is
// printed has to re-tokenize to the same tokens (6.10.3.3), and `is_leadingws`
// cannot say that on its own: after substitution an argument's first token
// carries the whitespace it had where it was written, which says nothing about
// the token it now follows. `#define __SOCKADDR_COMMON(prefix) sa_family_t
// prefix;` called as `__SOCKADDR_COMMON (sa_family)` printed
// `sa_family_tsa_family;` that way.
static bool needs_space(Token *prev, Token *cur) {
    if (!prev) return false;
    unsigned char c1 = (unsigned char)tok_text(prev)[prev->len - 1];
    unsigned char c2 = (unsigned char)tok_text(cur)[0];

    // An identifier or a number runs into what follows it.
    if ((is_ident2(c1) || c1 >= 0x80) && (is_ident2(c2) || c2 >= 0x80)) return true;

    // A `.` that would join a number, and the third one of an ellipsis.
    if (prev->kind == TK_NUM && c2 == '.') return true;
    if (c1 == '.' && cur->kind == TK_NUM) return true;
    if (c1 == '.' && c2 == '.' && cur->next && cur->next->kind == TK_ELLIPSIS) return true;

    // Two punctuators that would lex as one longer one; the lexer that reads
    // them is the authority on which those are.
    char buf[8];
    int n = 0;
    buf[n++] = c1;
    for (uint32_t i = 0; i < cur->len && n < 7; i++) buf[n++] = tok_text(cur)[i];
    buf[n] = 0;
    uint32_t kind;
    return read_punct(buf, &kind) > 1;
}

// Print tokens to stdout. Used for -E.
static void print_tokens(Token *tok) {
    FILE *out = open_outfile(opt_o ? opt_o : "-");

    int cur_line = 0;
    Token *prev = NULL;
    for (; tok->kind != TK_EOF; tok = tok->next) {
        if (cur_line > 0 && tok->is_sol) {
            int line, col;
            Token *orig = tok;
            while (orig->origin) orig = orig->origin;
            get_location(orig->file, orig->loc, &line, &col);
            if (!opt_P)
                while (cur_line < line + orig->line_delta) {
                    fprintf(out, "\n");
                    cur_line++;
                }
            fprintf(out, "\n");
            if (col > 2) fprintf(out, "%*s", col - 2, "");
            cur_line++;
        }
        if (tok->kind == TK_LINE) {
            cur_line = tok->id;
            if (opt_P) continue;
            fprintf(out, "# %lu \"%s\"", (unsigned long)tok->id, str(tok->filename));
            continue;
        }
        if (tok->is_leadingws || needs_space(prev, tok)) fprintf(out, " ");
        fprintf(out, "%.*s", (int)tok->len, tok_text(tok));
        prev = tok;
    }
    fprintf(out, "\n");
}

static Token *filter_tokens(Token *tok) {
    Token dummy = {}, *cur = &dummy;
    for (; tok; tok = tok->next) {
        if (tok->kind == TK_LINE) continue;
        cur = cur->next = tok;
    }
    return dummy.next;
}

static bool in_std_include_path(char *path) {
    for (int i = 0; i < num_std_include_paths; i++) {
        char *dir = std_include_paths[i];
        int len = strlen(dir);
        while (len > 1 && dir[len - 1] == '/') len--;
        if (strncmp(dir, path, len) == 0 && (path[len] == '/' || path[len] == '\0')) return true;
    }
    return false;
}

// If -M options is given, the compiler write a list of input files to stdout
static void print_dependencies(void) {
    char *path;
    if (opt_MF)
        path = opt_MF;
    else if (opt_MD)
        path = replace_extn(opt_o ? opt_o : base_file, ".d");
    else if (opt_o)
        path = opt_o;
    else
        path = "-";
    FILE *out = open_outfile(path);

    if (opt_MT)
        fprintf(out, "%s:", opt_MT);
    else
        fprintf(out, "%s:", quote_makefile(replace_extn(base_file, ".o")));

    SrcFile **files = get_input_files();
    for (int i = 0; files[i] && files[i]->id; i++) {
        if (opt_MM && in_std_include_path(files[i]->name)) continue;
        fprintf(out, " \\\n  %s", files[i]->name);
    }
    fprintf(out, "\n\n");

    if (opt_MP)
        for (int i = 1; files[i] && files[i]->id; i++) {
            if (opt_MM && in_std_include_path(files[i]->name)) continue;
            fprintf(out, "%s:\n\n", quote_makefile(files[i]->name));
        }
}

// Plain char's signedness is visible to a header through this macro: glibc's
// <limits.h> derives CHAR_MIN and CHAR_MAX from __CHAR_UNSIGNED__, so a
// compiler that flips the language and leaves the macro alone has the two
// disagreeing -- `-funsigned-char` still reported CHAR_MIN as -128. The ARM
// and RISC-V targets predefine it, since their ABIs make plain char unsigned;
// a flag that overrides the target has to add or drop it, which is what this
// does, after the machine flags have had their say and before the preprocessor
// runs. gcc and clang keep the language, the macro and <limits.h> in step.
static void char_sign_macro(void) {
    bool want = T.ty_char->is_unsigned;
    bool have = strstr(T.predef, "#define __CHAR_UNSIGNED__") != NULL;
    if (want == have) return;

    char *out = vnew(strlen(T.predef) + 32, 1);
    size_t n = 0;
    for (char *line = T.predef; line && *line;) {
        char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line + 1) : strlen(line);
        if (strncmp(line, "#define __CHAR_UNSIGNED__", 24)) {
            memcpy(out + n, line, len);
            n += len;
        }
        if (!eol) break;
        line = eol + 1;
    }
    if (want) n += (size_t)sprintf(out + n, "#define __CHAR_UNSIGNED__ 1\n");
    out[n] = '\0';
    T.predef = out;
}

// Stage 1: .c → .ll  (cc1: tokenize + preprocess + parse + irgen)
static void cc1(void) {
    // The machine flags' macro effects come from clang, and the preprocessor
    // is about to run with whatever definitions are already queued.
    machine_flag_macros();
    char_sign_macro();

    Token *tok = tokenize_file(base_file);
    if (!tok) fatal("%s: %s", base_file, strerror(errno));

    if (opt_dump_raw_tokens) dump_raw_tokens(tok);

    tok = preprocess(tok);

    if (opt_dump_tokens) dump_tokens(tok);

    // If -M or -MD is given, print file dependencies.
    if (opt_M || opt_MD) {
        print_dependencies();
        if (opt_M) return;
    }

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

    // -fsyntax-only: the translation unit has been read and checked, and
    // that is all. The return sits after the folding pass -- where the
    // conversions and shift counts a program is warned about are seen -- and
    // before irgen, which only lowers: every demand a program has to meet is
    // the front end's to make, so nothing user-facing is skipped.
    if (opt_fsyntax_only) return;

    Module *module = irgen(prog);

    FILE *out = open_outfile(output_file);
    dump_module(module, out);

    fclose(out);
}

// One line of clang's macro table, as `-dM -E` prints it.
typedef struct {
    char *name;
    char *value;
} ClangMacro;

// Ask clang for its own macro table with `extra` flags appended, and collect
// the definitions. NULL when clang could not be run at all.
static ClangMacro *clang_macros(char *extra, int *nout) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "clang -target %s %s-dM -E -x c /dev/null 2>/dev/null", T.triple, extra);
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;

    ClangMacro *out = vnew(64, sizeof(ClangMacro));
    int n = 0;
    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "#define ", 8)) continue;
        char *name = line + 8;
        char *end = name;
        while (*end && !isspace((unsigned char)*end)) end++;
        char *value = end;
        while (*value && isspace((unsigned char)*value)) value++;
        size_t vlen = strlen(value);
        while (vlen && isspace((unsigned char)value[vlen - 1])) value[--vlen] = '\0';
        *end = '\0';
        if (!*name) continue;
        out = vgrow(out, (n + 1) * sizeof(ClangMacro));
        out[n].name = strdup(name);
        out[n].value = strdup(value);
        n++;
    }
    pclose(fp);
    *nout = n;
    return out;
}

// The macros cxx leaves undefined on purpose even though clang's own
// baseline has them: the x86 feature set, which pulls in the intrinsic
// headers (see the comment in the amd64 target). A -m flag that asks for the
// instruction set must define them -- that is what the flag means.
static const char *absent_feature_macros[] = {
    "__SSE__", "__SSE2__", "__SSE_MATH__", "__SSE2_MATH__", "__MMX__", "__FXSR__",
};

static bool is_absent_feature(char *name) {
    for (size_t i = 0; i < sizeof(absent_feature_macros) / sizeof(*absent_feature_macros); i++)
        if (!strcmp(absent_feature_macros[i], name)) return true;
    return false;
}

static ClangMacro *find_clang_macro(ClangMacro *m, int n, char *name) {
    for (int i = 0; i < n; i++)
        if (!strcmp(m[i].name, name)) return &m[i];
    return NULL;
}

// The macros that go with the machine flags, taken from clang rather than
// tabulated here: cxx's own predefines describe the target as cxx models it,
// and a header that takes an SSE2 path while clang was told -mno-sse2 would
// generate the instructions the flag forbids. Asking clang for its macro
// table twice -- with and without the flags -- is exactly the difference the
// flags make, whatever they are (`-march=native` and every CPU name
// included).
static void machine_flag_macros(void) {
    if (!num_machine) return;

    char extra[4096] = "";
    for (int i = 0; i < num_machine; i++) {
        if (strlen(extra) + strlen(machine_args[i]) + 2 >= sizeof(extra)) break;
        strcat(extra, machine_args[i]);
        strcat(extra, " ");
    }

    int nbase = 0, nwith = 0;
    ClangMacro *base = clang_macros("", &nbase);
    if (!base) return;
    ClangMacro *with = clang_macros(extra, &nwith);
    if (!with) return;

    // Did the flags add anything? A flag that only takes things away
    // (-mno-sse) or changes nothing at all (-m64, -mtune=...) says nothing
    // about the feature set, while one that adds a feature carries the macros
    // that feature implies -- the x86 ones cxx normally leaves out included.
    bool adds = false;
    for (int i = 0; i < nwith; i++) {
        ClangMacro *b = find_clang_macro(base, nbase, with[i].name);
        if (!b || strcmp(b->value, with[i].value)) adds = true;
    }

    char *out = vnew(64 * 1024, 1);
    size_t n = 0;
    char **kept = vnew(16, sizeof(char *));
    int nkept = 0;

    // Only what the flags themselves changed is ours to apply. A macro clang
    // had before the flags and still has, spelled the same way, stays exactly
    // as the target predefines it: cxx's __GNUC__ and __STDC_VERSION__ and
    // the rest are its own, and respelling them as clang's would tell a
    // header that this is clang (`__GNUC__ 4` makes glibc's
    // __GNUC_PREREQ (7, 0) false, and <bits/floatn-common.h> then typedefs
    // _Float32 over the keyword).
    for (char *line = T.predef; line && *line;) {
        char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line + 1) : strlen(line);
        char name[256] = "";
        bool is_define = sscanf(line, "#define %255s", name) == 1;
        ClangMacro *w = is_define ? find_clang_macro(with, nwith, name) : NULL;
        ClangMacro *b = is_define ? find_clang_macro(base, nbase, name) : NULL;
        if (is_define && b && !w) {
            // A macro these flags do not have: not predefined at all, rather
            // than predefined and then undefined (which is what gcc and clang
            // do, and why neither warns here).
        } else if (b && w && strcmp(b->value, w->value)) {
            out = vgrow(out, n + strlen(w->name) + strlen(w->value) + 16);
            n += (size_t)sprintf(out + n, "#define %s %s\n", w->name, w->value);
            kept = vgrow(kept, (nkept + 1) * sizeof(char *));
            kept[nkept++] = w->name;
        } else {
            out = vgrow(out, n + len + 1);
            n += (size_t)sprintf(out + n, "%.*s", (int)len, line);
            if (is_define) {
                kept = vgrow(kept, (nkept + 1) * sizeof(char *));
                kept[nkept++] = name;
            }
        }
        if (!eol) break;
        line = eol + 1;
    }

    // And the ones the flags turned on, which the target does not predefine.
    for (int i = 0; i < nwith; i++) {
        ClangMacro *b = find_clang_macro(base, nbase, with[i].name);
        if (b && !(adds && is_absent_feature(with[i].name))) continue;
        bool have = false;
        for (int j = 0; j < nkept && !have; j++) have = !strcmp(kept[j], with[i].name);
        if (have) continue;
        out = vgrow(out, n + strlen(with[i].name) + strlen(with[i].value) + 16);
        n += (size_t)sprintf(out + n, "#define %s %s\n", with[i].name, with[i].value);
    }

    out[n] = '\0';
    T.predef = out;
}

// Stage 2: .ll -> .s  (via clang)
static void compile(char *input, char *output) {
    // Bare-metal triples default to a soft-float ABI, which clashes with
    // the "target-abi" module flag; pass the matching driver flags.
    char mabi[64] = "-mabi=";
    char march[64] = "-march=";
    char *cmd[24];
    int n = 0;
    cmd[n++] = "clang";
    cmd[n++] = "-target";
    cmd[n++] = T.triple;
    cmd[n++] = "-S";
    cmd[n++] = "-fno-addrsig";
    cmd[n++] = "-Wno-override-module";
    cmd[n++] = "-x";
    cmd[n++] = "ir";
    cmd[n++] = input;
    cmd[n++] = "-o";
    cmd[n++] = output;
    if (T.clang_mabi) {
        strncat(mabi, T.clang_mabi, 56);
        cmd[n++] = mabi;
    }
    // The target's -march is a default; a -march=/-mcpu= the user gave
    // replaces it (two -march options in one command line conflict).
    if (T.clang_march && !machine_sets_arch) {
        strncat(march, T.clang_march, 56);
        cmd[n++] = march;
    }
    for (int i = 0; i < num_machine && n < 23; i++) cmd[n++] = machine_args[i];
    cmd[n] = NULL;
    run_subprocess(cmd);
}

// Stage 3: .s → .o  (via the clang integrated assembler: it tracks the
// directives the compiler emits, e.g. .prefalign, which older binutils
// reject)
static void assemble(char *input, char *output) {
    char *cmd[16];
    int n = 0;
    cmd[n++] = "clang";
    cmd[n++] = "-target";
    cmd[n++] = T.triple;
    for (int i = 0; i < num_machine && n < 11; i++) cmd[n++] = machine_args[i];
    // One -Wa, argument holding everything the user wrote, which is how clang
    // spells the same thing.
    char wa[1024] = "-Wa";
    for (int i = 0; i < num_asmarg; i++) {
        if (strlen(wa) + strlen(asm_args[i]) + 2 >= sizeof(wa)) break;
        strcat(wa, ",");
        strcat(wa, asm_args[i]);
    }
    if (num_asmarg) cmd[n++] = wa;
    cmd[n++] = "-x";
    cmd[n++] = "assembler";
    cmd[n++] = "-c";
    cmd[n++] = input;
    cmd[n++] = "-o";
    cmd[n++] = output;
    cmd[n] = NULL;
    run_subprocess(cmd);
}

// Stage 4: .o → executable  (via cc)
static void run_linker(char **ld_args, int num_ldarg, char *output) {
    ld_args[0] = "cc";
    ld_args[1] = "-o";
    ld_args[2] = output;
    memcpy(ld_args + num_ldarg, ld_extra_args, num_ld_exarg * sizeof(char *));
    run_subprocess(ld_args);
}

static FileType get_file_type(char *filename) {
    if (opt_x != FILE_NONE) return opt_x;

    if (endswith(filename, ".a")) return FILE_AR;
    if (endswith(filename, ".so")) return FILE_DSO;
    if (endswith(filename, ".o")) return FILE_OBJ;
    if (endswith(filename, ".c")) return FILE_C;
    if (endswith(filename, ".s")) return FILE_ASM;
    if (endswith(filename, ".S")) return FILE_ASM_PP;

    fatal("<command line>: unknown file extension: %s", filename);
    return FILE_NONE;
}

int main(int argc, char **argv) {
    atexit(cleanup);
    select_target(&Deftgt);

    input_paths = emalloc(argc * sizeof(char *));
    tmpfiles = emalloc(argc * 4 * sizeof(char *));
    include_paths = emalloc((argc + 16) * sizeof(char *));
    embed_dirs = emalloc((argc + 16) * sizeof(char *));
    std_include_paths = emalloc((argc + 16) * sizeof(char *));
    dirafter = emalloc(argc * sizeof(char *));
    // ld_extra_args holds every argument that must follow the object files:
    // --sysroot/-L/-s/-static/-shared/-Xlinker, and -l/-Wl, below. A -Wl,
    // argument expands to one entry per comma-separated token, so allow more
    // room than one entry per command-line argument.
    ld_extra_args = emalloc(argc * 4 * sizeof(char *));

    parse_args(argc, argv);

    if (opt_cc1) {
        add_default_include_paths(argv[0]);
        add_dirafter();
        cc1();
        return 0;
    }

    if (num_input > 1 && opt_o && (opt_c || opt_S || opt_E))
        fatal("cannot specify '-o' with '-c' ,'-S' or '-E' with multiple files");

    char **ld_args = vnew(argc + 4, sizeof(char *));
    int num_ldarg = 3;

    for (int i = 0; i < num_input; i++) {
        char *input = input_paths[i];

        // Libraries belong after the objects that reference them: the
        // linker only pulls a member out of an archive to satisfy a symbol
        // that is already undefined, so `-lm` ahead of the object file is a
        // no-op. run_linker appends ld_extra_args last.
        if (!strncmp(input, "-l", 2)) {
            ld_extra_args[num_ld_exarg++] = input;
            continue;
        }

        if (!strncmp(input, "-Wl,", 4)) {
            char *s = strdup(input + 4);
            char *arg = strtok(s, ",");
            while (arg) {
                ld_extra_args[num_ld_exarg++] = arg;
                arg = strtok(NULL, ",");
            }
            continue;
        }

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

        // -fsyntax-only has nothing to do with an object file, an archive
        // or an assembly source, and it must not put them in the linker's
        // argument list either: the link does not happen.
        if (opt_fsyntax_only && type != FILE_C) continue;

        // Handle .o, .a or .so — pass straight to linker.
        if (type == FILE_OBJ || type == FILE_AR || type == FILE_DSO) {
            ld_args[num_ldarg++] = input;
            continue;
        }

        // Handle .S — assembler that the C preprocessor runs first: cpython
        // ships Python/asm_trampoline_x86_64.S, which picks its symbols with
        // `#ifdef __x86_64__`. clang does the two steps in one invocation,
        // and it is the assembler cxx's own stage 3 uses anyway. Only the
        // machine flags and the include paths are handed over: -D/-U reach
        // cxx's own preprocessor as a text buffer.
        if (type == FILE_ASM_PP) {
            char *cmd[32];
            int m = 0;
            cmd[m++] = "clang";
            cmd[m++] = "-target";
            cmd[m++] = T.triple;
            for (int j = 0; j < num_machine && m < 24; j++) cmd[m++] = machine_args[j];
            for (int j = 0; j < num_include_paths && m < 24; j++) cmd[m++] = format("-I%s", include_paths[j]);
            // When this ends in a link, the object goes to a temporary
            // file: `-o` names the executable the linker is about to write.
            char *obj = opt_E || opt_S || opt_c ? output : create_tmpfile();
            cmd[m++] = opt_E ? "-E" : (opt_S ? "-S" : "-c");
            cmd[m++] = input;
            cmd[m++] = "-o";
            cmd[m++] = obj;
            cmd[m] = NULL;
            run_subprocess(cmd);
            if (!opt_E && !opt_S && !opt_c) ld_args[num_ldarg++] = obj;
            continue;
        }

        // Handle .s — assemble, unless -S stops here.

        if (type == FILE_ASM) {
            if (opt_S) continue;
            char *obj = opt_c ? output : create_tmpfile();
            assemble(input, obj);
            if (!opt_c) ld_args[num_ldarg++] = obj;
            continue;
        }

        // Handle .c (or stdin "-").
        assert(type == FILE_C);

        // -E: .c → stdout
        if (opt_E || opt_M) {
            run_cc1(argc, argv, input, NULL);
            continue;
        }

        // -fsyntax-only: .c → nothing at all. The front end runs in full
        // (see cc1()), and no output path is handed to it, so no file of any
        // kind is written.
        if (opt_fsyntax_only) {
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

    if (num_ldarg > 3) run_linker(ld_args, num_ldarg, opt_o ? opt_o : "a.out");

    return 0;
}
