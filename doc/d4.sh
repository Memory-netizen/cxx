#!/bin/bash
# D4: plain char's signedness -- the language, the macro, and <limits.h>.
#
#   bash doc/d4.sh [compiler]
#
# The switch is implementation-defined (C23 6.2.5) and has three visible
# effects. They have to move together:
#
#   - what plain char means (`(char)-1 < 0`)
#   - whether __CHAR_UNSIGNED__ is defined
#   - <limits.h>'s CHAR_MIN and CHAR_MAX, which glibc derives from that macro
#
# `signed char` and `unsigned char` are separate types and must not move, and
# the ARM and RISC-V ABIs make plain char unsigned to begin with, so a flag
# that overrides them has to override the target's own predefine as well.
# -fno-signed-char is the same request written the other way round; both
# references read it that way.
#
# The macro is read with #ifdef rather than with `-dM -E`: cxx's -dM prints a
# macro list of its own that does not carry the target's char predefine (a
# known difference, recorded in the plan).
#
# The expected values are what gcc and clang print for the same file.
# The summary line at the end is what doc/probes.sh reports.
set -u
C=${1:-/home/memory/cxx/cxx}
[ -x "$C" ] || { echo "no compiler at $C" >&2; exit 2; }
t=$(mktemp -d /tmp/cxx-d4-XXXXXX)
trap 'rm -rf $t' EXIT

cat > "$t/m.c" <<'EOF'
#include <stdio.h>
#include <limits.h>
int main(void) {
    char c = -1;
    signed char sc = -1;
    unsigned char uc = 255;
#ifdef __CHAR_UNSIGNED__
    int macro = 1;
#else
    int macro = 0;
#endif
    printf("%s %d %d %d %d %d\n", c < 0 ? "s" : "u", macro, (int)CHAR_MIN, (int)CHAR_MAX, (int)sc,
           (int)uc);
    return 0;
}
EOF

n_rows=0
n_bad=0

# A row that runs: language, macro, CHAR_MIN, CHAR_MAX, signed char, unsigned char.
run_row() { # run_row <label> <expected> [flags...]
    local label=$1 want=$2; shift 2
    n_rows=$((n_rows + 1))
    printf '%-36s ' "$label"
    rm -f "$t/a.out"
    if ! timeout 120 $C "$@" -w -o "$t/a.out" "$t/m.c" > "$t/log" 2>&1; then
        printf 'COMPILE FAILED: %s\n' "$(head -1 "$t/log")"
        n_bad=$((n_bad + 1))
        return
    fi
    got=$(timeout 10 "$t/a.out")
    if [ "$got" = "$want" ]; then
        printf '%s  ok\n' "$got"
    else
        printf '%s  MISMATCH (want %s)\n' "$got" "$want"
        n_bad=$((n_bad + 1))
    fi
}

# A row that only compiles (a cross target cannot run here): the macro is read
# out of the preprocessed text, where `int macro = N;` has the answer.
macro_row() { # macro_row <label> <expected 0|1> [flags...]
    local label=$1 want=$2; shift 2
    n_rows=$((n_rows + 1))
    printf '%-36s ' "$label"
    got=$(timeout 120 $C "$@" -w -E "$t/m.c" 2>/dev/null | grep -oE 'int macro = [01]' | head -1 |
              grep -oE '[01]$')
    if [ "$got" = "$want" ]; then
        printf '__CHAR_UNSIGNED__=%s  ok\n' "$got"
    else
        printf '__CHAR_UNSIGNED__=%s  MISMATCH (want %s)\n' "${got:-<none>}" "$want"
        n_bad=$((n_bad + 1))
    fi
}

echo "== ${C##*/}: plain char on this target"
run_row "<default>"             "s 0 -128 127 -1 255"
run_row "-fsigned-char"         "s 0 -128 127 -1 255" -fsigned-char
run_row "-funsigned-char"       "u 1 0 255 -1 255"    -funsigned-char
run_row "-fno-signed-char"      "u 1 0 255 -1 255"    -fno-signed-char
run_row "-funsigned-char -fsigned-char" "s 0 -128 127 -1 255" -funsigned-char -fsigned-char

# gcc has no -target; asking it about one would report four rows that say
# nothing about it, so those are skipped rather than failed.
printf '#ifdef __aarch64__\nint arch = 1;\n#else\nint arch = 0;\n#endif\n' > "$t/arch.c"
if [ "$(timeout 60 $C -target aarch64-linux-gnu -E "$t/arch.c" 2>/dev/null |
        grep -oE 'int arch = [01]' | head -1)" = "int arch = 1" ]; then
    echo "== ${C##*/}: a target whose ABI makes plain char unsigned"
    macro_row "-target aarch64-linux-gnu"               1 -target aarch64-linux-gnu
    macro_row "-target aarch64-linux-gnu -fsigned-char" 0 -target aarch64-linux-gnu -fsigned-char
    macro_row "-target riscv64-linux-gnu"               1 -target riscv64-linux-gnu
    macro_row "-target riscv64-linux-gnu -fsigned-char" 0 -target riscv64-linux-gnu -fsigned-char
else
    echo "== ${C##*/}: no -target, cross rows skipped"
fi

echo "d4: $n_rows row(s), $n_bad mismatch(es)"
[ "$n_bad" -eq 0 ]
