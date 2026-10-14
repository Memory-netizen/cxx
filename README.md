# cxx

A C compiler: it preprocesses, parses and folds C, emits LLVM IR, and lets
`clang` and the host linker finish the job.

    $ ./cxx -o prog prog.c

The three stages it runs, as it prints them itself when given `-###`:

    ./cxx -cc1 -cc1-input prog.c -cc1-output /tmp/cxx-XXXX    # this compiler
    clang -target x86_64-linux-gnu -x ir /tmp/cxx-XXXX -c -o /tmp/cxx-YYYY
    cc -o prog /tmp/cxx-YYYY

`-cc1` is the whole compiler — preprocess, parse, fold, IR. The driver is the
same binary, re-invoked with `-cc1` appended to its own argument list.

What it reads: one language level. `__STDC_VERSION__` reports `202311L` whatever
`-std=` says, and the C2y draft's features are covered by a suite of their own
(`test/c2y.sh`, ISO/IEC 9899:202y / N3685). What `-std=` does change is
`__STRICT_ANSI__`: an ISO spelling (`-std=c23`) defines it, a GNU one
(`-std=gnu23`) leaves it undefined — the switch headers use to ask whether GNU
extensions are in play.

## Build

    make -j8            # DEBUG=1 (the default): -g -O0, assertions on
    make -j8 DEBUG=0    # -O2 -DNDEBUG

Needs `clang` (the tree is compiled with it, and cxx runs it at run time: for
the built-in macro table, the resource directory, and the IR-to-object stage),
`cc` and the host linker, and `python3` for the generated headers
(`src/width_property.h`, `src/xid_property.h`, `src/config.h`). `make fmt` wants
`clang-format-21`.

`src/config.h` records the target this build defaults to, taken from the host
triple. When detection fails, it says so and asks for the file by hand:

    #define Deftgt T_amd64

## Use

    cxx [ -o <path> ] [ -S | -c | -E | -fsyntax-only ] [ -ast-dump ]
        [ -dump-tokens ] [ -raw-dump-tokens | -dump-raw-tokens ] <file>

`-` reads standard input; add `-x c` where the driver would otherwise take the
language from the file extension (`-E -` needs nothing, `-fsyntax-only -` does).
A `.s` file is assembled through clang, like every other input cxx accepts.
`-M` and `-MD` write dependencies, `-###` prints the stage commands instead of
running them, `-w` and the `-W…` groups control diagnostics,
`-target <triple>` and the `-m…` flags drive the clang stages.

`-cc1`, with `-cc1-input` and `-cc1-output`, is the internal single-process
form; it is also the entry point for tooling. With `-cc1-output out.ll` it stops
after the IR, which is how the measurements in `doc/perf.md` are taken.

## Tests

    make test           # 52 harness programs in test/*.c, then driver.sh,
                        # error.sh, ir.sh, conformance.sh and c2y.sh
    make test-all       # + test-amd64, test-arm64, test-rv64 (qemu user mode)
                        #   and test-rv32 (the bare-metal harness)

Two more suites check behaviour rather than whether something compiles:

    bash doc/tcctests.sh     # tinycc's tests/tests2, compiled and run
    bash doc/bootstrap.sh    # cxx builds cxx, twice more, objects compared

On this tree (2026-10-14, commit 279c815):

    conformance  326 passed, 0 known gap(s)     c2y  101 passed, 0 known gap(s)
    tcctests     106 ok, 0 failed               bootstrap  21 of 21 objects identical

`test/exhaust_int.c` is kept out of `make test` while the atomics work settles.

## Layout

    src/main.c         the driver, and cc1's entry point
    src/lexer.c        tokens; translation phases 2 and 3
    src/preprocess.c   directives, macro expansion, the streaming hand-out
    src/parser.c       declarations, statements, expressions
    src/type.c         types, conversions, layout
    src/literal.c      what type a literal has
    src/opt_ast.c      the constant folder
    src/attr.c         attributes
    src/unicode.c      identifier and character tables
    src/irgen.c        AST to LLVM IR; src/dumpir.c prints it
    src/dumpast.c      -ast-dump; src/dumptok.c -dump-tokens
    src/util.c         arena, string interning, positions, diagnostics
    src/amd64/ arm64/ rv64/ rv32/ rv/   target descriptions
    src/support/       int128 and fp128 helpers
    test/              the suites      tools/  the header generators
    doc/               the design and measurement records, below

## The front end, as it stands

Two things are worth knowing before reading it:

* **It streams.** `preprocess_file()` hands over the first segment; the rest is
  pulled, token by token, by the parser's own walk (`tok_next()` →
  `pp_pull_more()`), so a consumer that stops early stops the preprocessor.
  Design: `doc/streaming-frontend.md`. Landing and evidence: `doc/perf.md` 8oo.
* **It never rewrites the source.** Translation phase 2 is read, not written:
  the scanner steps over a backslash-newline itself, and the text is mapped
  read-only, so those pages stay file-backed and shareable. `doc/perf.md` 8or,
  and 8os for what a line splice does to a diagnostic.

## Known issues

* `-D` together with `-dump-tokens` fails — "macro name must be an identifier" —
  and the dump modes do not stop the driver, so a dump run exits non-zero
  (`doc/perf.md` 8op).
* A diagnostic quoting a token that a line splice cuts in half prints the raw
  bytes, splice and all, where gcc prints the joined spelling (8os).
* A file that does not end with a newline, or whose last page holds fewer than
  three bytes, takes the copying path instead of the read-only mapping (8or).

## License

MIT; see LICENSE.
