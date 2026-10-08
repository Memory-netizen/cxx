#!/bin/bash
# Run the Fujitsu Compiler Test Suite's C tests with cxx.
#
#   bash doc/fujitsu.sh [compiler]
#
#   FJ_SRC=~/compiler-test-suite   the suite (fujitsu/compiler-test-suite)
#   FJ_LIMIT=204                   how many tests; 0 means all 37190
#   FJ_DIRS="0000 0185"            run only these numbered directories
#   FJ_JOBS=8                      tests compiled and run in parallel
#   FJ_TIMEOUT=20                  seconds per test
#   FJ_REF=clang                   the reference compiler (empty to skip it)
#   FJ_VERBOSE=1                   print every failing test
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
JOBS=${FJ_JOBS:-8}
TMO=${FJ_TIMEOUT:-20}
LIBS=${FJ_LIB--lm}
DIRS=${FJ_DIRS:-}
REF=${FJ_REF-clang}
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
        f=$(ls "$SRC/C/$d"/*.c 2>/dev/null | head -1)
        [ -n "$f" ] && echo "$f"
    done > "$work/list"
fi
total=$(wc -l < "$work/list")
[ "$total" -gt 0 ] || { echo "no tests found under $SRC/C" >&2; exit 2; }
printf '== Fujitsu Compiler Test Suite: %s C test(s) from %s\n' "$total" "$SRC"
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

one() {
    local src=$1 dir=$2
    local b
    b=$(basename "$src" .c)
    grep -q '#pragma omp' "$src" 2>/dev/null && {
        echo "skip $src"
        return
    }

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
            echo "compile $src"
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
export -f one try
export C REF LIBS TMO

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
n_bad=$((n_compile + n_exit + n_out + n_tmo))
ran=$((total - n_skip - n_reffail))

printf '   ok %s of %s, compile-fail %s, wrong exit %s, output differs %s, timeout %s\n' \
    "$n_ok" "$ran" "$n_compile" "$n_exit" "$n_out" "$n_tmo"
printf '   skipped %s (OpenMP), reference could not build %s\n' "$n_skip" "$n_reffail"

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
if [ "$n_skip" -gt 0 ] || [ "$n_reffail" -gt 0 ]; then
    printf ' (skipped %s, unbuildable by the reference %s)' "$n_skip" "$n_reffail"
fi
printf '\n'
[ "$n_bad" -eq 0 ]
