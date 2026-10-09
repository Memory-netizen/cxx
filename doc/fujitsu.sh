#!/bin/bash
# Run the Fujitsu Compiler Test Suite's C tests with cxx.
#
#   bash doc/fujitsu.sh [compiler]
#
#   FJ_SRC=~/compiler-test-suite   the suite (fujitsu/compiler-test-suite)
#   FJ_LIMIT=204                   how many tests; 0 means all 37190
#   FJ_PER_DIR=1                   files taken from each directory
#   FJ_DIRS="0000 0185"            run only these numbered directories
#   FJ_JOBS=8                      tests compiled and run in parallel
#   FJ_TIMEOUT=20                  seconds per test
#   FJ_REF=clang                   the reference compiler (empty to skip it)
#   FJ_FILTER_CC=clang             who decides what is out of scope (empty: no filter)
#   FJ_GAPS=1                      filter tests that need a missing feature (0 to keep them)
#   FJ_VERBOSE=1                   print every failing test
#
# Two forms are out of scope for cxx and are filtered out rather than counted
# as failures (plan section 0, measurements in R56 and R57):
#
#   * calling a function declared `f()`, with no prototype. It was deprecated
#     in every version of C, and C23 gave `()` the meaning of `(void)` -- so
#     `f(1)` is an error in the references too under -std=c23.
#   * an old-style (K&R) definition, `int f(a) int a; { }`. C23 did not deprecate
#     it, it removed it: the draft's function-definition production has no
#     declaration-list.
#
# The filter does not guess with a regex: clang names both with one diagnostic
# (-Wdeprecated-non-prototype), so a test is filtered exactly when the reference
# compiler objects to it for that reason. A `(void)` prototype is untouched.
#
# A test that needs a feature cxx does not have is a *gap*, not a failure: it
# is filtered out and the summary names the feature. These are the entries of
# section 0's gap table -- code that says `#include <mmintrin.h>` or calls a
# __sync_* form that is not implemented cannot tell us anything about the code
# that is. FJ_GAPS=0 turns the filter off, to see the list rather than the
# count.
#
# Complex arithmetic is out of scope as well (section 0: _Complex, _Imaginary
# and the two headers), so a test that mentions those keywords, or whose
# compile fails only because of an imaginary-literal suffix (`1.0i`, `2.0fi`,
# `2*i*I`), is counted there too. The keywords are unambiguous, so a textual
# test is safe; the suffixes are read off cxx's own diagnostic.
#
# The suite's own runner is the LLVM test-suite (CMake + Ninja + lit), and what
# lit compares is the program's output against a stored reference. This probe
# does that without the framework: each test is compiled and run with cxx and
# with the reference compiler, and the two have to agree on stdout and on the
# exit status. `config.single_source` in lit.local.cfg means each .c is a
# program of its own, which is what makes the comparison meaningful.
#
# (An earlier version asked for empty output, reading config.traditional_output
# the wrong way. These tests do print on success -- C/0000/0000_0000.c prints
# "OK" -- so a reference compiler is the only honest oracle.)
#
# The tests live in C/0000 .. C/0202. The default is one test per directory: a
# sample that still touches every theme, in a couple of minutes. FJ_LIMIT=0
# runs the lot. A test that asks for OpenMP is skipped, since cxx has none and
# the suite's own CMake turns those off for such a compiler.
set -u
C=${1:-./cxx}
SRC=${FJ_SRC:-$HOME/compiler-test-suite}
LIMIT=${FJ_LIMIT:-204}
PER_DIR=${FJ_PER_DIR:-1}
JOBS=${FJ_JOBS:-8}
TMO=${FJ_TIMEOUT:-20}
LIBS=${FJ_LIB--lm}
DIRS=${FJ_DIRS:-}
GAPS=${FJ_GAPS:-1}
REF=${FJ_REF-clang}
FILTER_CC=${FJ_FILTER_CC-clang}
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1

case $C in
    /*) ;;
    *) [ -x "$C" ] && C=$root/${C#./} ;;
esac
[ -x "$C" ] || { echo "no compiler at $C" >&2; exit 2; }
[ -d "$SRC/C" ] || { echo "no Fujitsu suite at $SRC (set FJ_SRC)" >&2; exit 2; }
if [ -n "$REF" ]; then
    command -v "$REF" >/dev/null 2>&1 || { echo "no reference compiler $REF (set FJ_REF=)" >&2; exit 2; }
fi

work=$(mktemp -d /tmp/cxx-fujitsu-XXXXXX)
trap 'rm -rf $work' EXIT

if [ -n "$DIRS" ]; then
    for d in $DIRS; do find "$SRC/C/$d" -name '*.c' 2>/dev/null; done | sort > "$work/list"
elif [ "$LIMIT" -eq 0 ]; then
    find "$SRC/C" -name '*.c' | sort > "$work/list"
else
    for d in $(ls "$SRC/C" | grep -E '^[0-9]{4}$'); do
        ls "$SRC/C/$d"/*.c 2>/dev/null | head -n "$PER_DIR"
    done > "$work/list"
fi
total=$(wc -l < "$work/list")
[ "$total" -gt 0 ] || { echo "no tests found under $SRC/C" >&2; exit 2; }
printf '== Fujitsu Compiler Test Suite: %s C test(s) from %s\n' "$total" "$SRC"
[ "$PER_DIR" != 1 ] && printf '   %s file(s) per directory\n' "$PER_DIR"
printf '   compiler %s, reference %s, -j%s, %ss per test\n' "$C" "${REF:-none}" "$JOBS" "$TMO"

# Build and run one test with one compiler: "<status>", with stdout in
# <tag>.out and the exit status in <tag>.rc.
try() { # try <compiler> <src> <dir> <tag>
    local cc=$1 src=$2 dir=$3 tag=$4
    if ! timeout 120 "$cc" -w -o "$dir/$tag.bin" "$src" $LIBS > "$dir/$tag.log" 2>&1; then
        echo compile
        return
    fi
    timeout "$TMO" "$dir/$tag.bin" > "$dir/$tag.out" 2> "$dir/$tag.err"
    local rc=$?
    if [ $rc -eq 124 ]; then
        echo timeout
        return
    fi
    echo "$rc" > "$dir/$tag.rc"
    echo run
}

# True when the test uses one of the two forms cxx does not implement, decided
# by the reference compiler rather than by a regex: clang calls both of them
# -Wdeprecated-non-prototype.
# The feature a test needs and cxx does not have, or nothing. The patterns name
# things that cannot be mistaken for ordinary code.
gap_feature() { # gap_feature <src>
    local src=$1
    if grep -qE '__builtin_ia32_|__HPC_ACE__|mmintrin\.h|xmmintrin\.h|emmintrin\.h|immintrin\.h' "$src" 2>/dev/null; then
        echo "SSE/MMX intrinsics"
        return 0
    fi
    # The __sync_* forms that are not implemented; the fetch_and_* family and
    # the two lock_* ones are, so they are named one by one.
    if grep -qE '__sync_(add|sub|or|and|xor|nand)_and_fetch|__sync_fetch_and_nand|__sync_(bool|val)_compare_and_swap' \
        "$src" 2>/dev/null; then
        echo "__sync_* (unimplemented forms)"
        return 0
    fi
    if grep -qE '_Decimal32|_Decimal64|_Decimal128' "$src" 2>/dev/null; then
        echo "decimal floating point"
        return 0
    fi
    return 0
}

out_of_scope() { # out_of_scope <src> <dir>
    local src=$1 dir=$2
    # The keywords are unambiguous, so a textual test cannot misfire.
    if grep -qE '_Complex|_Imaginary|<complex\.h>|<tgmath\.h>' "$src" 2>/dev/null; then
        return 0
    fi
    # `f()` with no prototype, and old-style definitions, are what clang calls
    # -Wdeprecated-non-prototype. Asking it beats guessing with a regex.
    [ -z "$FILTER_CC" ] && return 1
    command -v "$FILTER_CC" >/dev/null 2>&1 || return 1
    local b
    b=$(basename "$src" .c)
    if ! timeout 60 "$FILTER_CC" -std=c17 -Werror=deprecated-non-prototype -c -o "$dir/f_$b.o" "$src" \
            > "$dir/f_$b.log" 2>&1 &&
        grep -q 'deprecated-non-prototype\|without a prototype' "$dir/f_$b.log"; then
        return 0
    fi
    return 1
}

one() {
    local src=$1 dir=$2
    local b
    b=$(basename "$src" .c)
    grep -q '#pragma omp' "$src" 2>/dev/null && {
        echo "skip $src"
        return
    }
    if [ "$GAPS" = 1 ]; then
        local feat
        feat=$(gap_feature "$src")
        if [ -n "$feat" ]; then
            echo "gap $feat | $src"
            return
        fi
    fi

    # The keyword and prototype tests can run before anything is compiled; the
    # imaginary-suffix test needs cxx's diagnostic, so it is repeated below.
    if out_of_scope "$src" "$dir"; then
        echo "noproto $src"
        return
    fi

    local refstat=none
    if [ -n "$REF" ]; then
        refstat=$(try "$REF" "$src" "$dir" "r_$b")
        # A test the reference cannot build says nothing about cxx.
        [ "$refstat" = compile ] && {
            echo "reffail $src"
            return
        }
        [ "$refstat" = timeout ] && {
            echo "reftimeout $src"
            return
        }
    fi

    local st
    st=$(try "$C" "$src" "$dir" "c_$b")
    case $st in
        compile)
            # An imaginary-literal suffix: cxx refuses it, both references take
            # it, and complex arithmetic is out of scope.
            if grep -q "invalid suffix .*[iI]" "$dir/c_$b.log" 2>/dev/null; then
                echo "noproto $src"
            else
                echo "compile $src"
            fi
            return
            ;;
        timeout)
            echo "timeout $src"
            return
            ;;
    esac
    local rc
    rc=$(cat "$dir/c_$b.rc")

    if [ "$refstat" = run ]; then
        local rrc
        rrc=$(cat "$dir/r_$b.rc")
        # The reference dying is not cxx's doing; a difference in status is.
        if [ "$rrc" -ge 128 ]; then
            echo "refcrash $src"
            return
        fi
        if [ "$rc" -ne "$rrc" ]; then
            echo "exit $src"
            return
        fi
        if ! cmp -s "$dir/c_$b.out" "$dir/r_$b.out"; then
            echo "output $src"
            return
        fi
    else
        if [ "$rc" -ne 0 ]; then
            echo "exit $src"
            return
        fi
        if [ -s "$dir/c_$b.out" ]; then
            echo "output $src"
            return
        fi
    fi
    echo "ok $src"
}
export -f one try out_of_scope gap_feature
export C REF LIBS TMO FILTER_CC GAPS

xargs -a "$work/list" -d '\n' -P "$JOBS" -I{} bash -c 'one "$@"' _ {} "$work" > "$work/results" 2>"$work/xargs.err"
[ -s "$work/xargs.err" ] && cat "$work/xargs.err"

count() { grep -c "^$1 " "$work/results" || true; }
n_ok=$(count ok)
n_compile=$(count compile)
n_exit=$(count exit)
n_out=$(count output)
n_tmo=$(count timeout)
n_skip=$(count skip)
n_reffail=$(count reffail)
n_noproto=$(count noproto)
n_gap=$(grep -c '^gap ' "$work/results" || true)
n_bad=$((n_compile + n_exit + n_out + n_tmo))
ran=$((total - n_skip - n_reffail - n_noproto - n_gap))

printf '   ok %s of %s, compile-fail %s, wrong exit %s, output differs %s, timeout %s\n' \
    "$n_ok" "$ran" "$n_compile" "$n_exit" "$n_out" "$n_tmo"
printf '   out of scope: %s (OpenMP), %s (no prototype or K&R), unbuildable by the reference: %s\n' \
    "$n_skip" "$n_noproto" "$n_reffail"
if [ "$n_gap" -gt 0 ]; then
    printf '   gaps (a feature cxx does not have; section 0 of the plan):\n'
    grep '^gap ' "$work/results" | sed 's/^gap //' | awk -F' \\| ' '{n[$1]++} END {for (f in n) printf "      %5d  %s\n", n[f], f}' | sort -rn
fi

if [ "$n_compile" -gt 0 ]; then
    printf '   what the compile failures say:\n'
    for f in $(grep '^compile ' "$work/results" | awk '{print $2}' | head -400); do
        b=$(basename "$f" .c)
        # For a link failure the useful line names the missing symbol.
        grep -h 'undefined reference\|error:' "$work/c_$b.log" 2>/dev/null | head -1 | sed 's/.*error: //'
    done | sed "s/‘[^’]*’/‘X’/g" | sort | uniq -c | sort -rn |
        awk '{printf "      %5s  %s\n", $1, substr($0, index($0, $2))}' | head -"${FJ_CLUSTER:-10}"
fi

if [ "$n_bad" -gt 0 ]; then
    if [ "${FJ_VERBOSE:-0}" = 1 ]; then
        printf '   failing tests:\n'
        grep -v '^ok \|^skip \|^reffail \|^refcrash ' "$work/results" | head -60 | sed 's/^/      /'
    else
        printf '   first failing tests (FJ_VERBOSE=1 for all):\n'
        grep -v '^ok \|^skip \|^reffail \|^refcrash ' "$work/results" | head -n "${FJ_SHOW:-8}" | sed 's/^/      /'
    fi
fi

printf 'fujitsu: %s of %s passed, %s failed' "$n_ok" "$ran" "$n_bad"
if [ "$n_skip" -gt 0 ] || [ "$n_reffail" -gt 0 ] || [ "$n_noproto" -gt 0 ] || [ "$n_gap" -gt 0 ]; then
    printf ' (out of scope: %s OpenMP, %s no-prototype/K&R; gaps: %s; unbuildable by the reference: %s)' \
        "$n_skip" "$n_noproto" "$n_gap" "$n_reffail"
fi
printf '\n'
[ "$n_bad" -eq 0 ]
