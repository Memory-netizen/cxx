#!/bin/bash
# Compile and RUN a project's own self-checking test programs with cxx.
#
#   bash doc/tcctests.sh [compiler]
#
# The test programs are tinycc's tests/tests2: each .c prints something and
# has an .expect file to compare against. They are the one part of these
# trees that checks *behaviour* rather than whether the file compiles, which
# is what the other probes cannot see.
#
# A test is only counted when the host compiler (gcc) also reproduces its
# .expect: the rest are written for tcc itself -- `-run`, inline asm,
# backtraces, tcc's -dt dump mode -- and a failure there would say nothing
# about cxx. The probe reports, of the tests a reference compiler passes,
# how many cxx passes, and the diagnostic classes behind the ones it does
# not.
#
#   RW=/path        where the tinycc tree is (default ~/rw)
#   TT_CAP=0        at most this many tests (0 = all)
#   TT_CLUSTER=10   how many failure classes to show
set -u
C=${1:-./cxx}
RW=${RW:-$HOME/rw}
CLUSTER=${TT_CLUSTER:-10}
CAP=${TT_CAP:-0}
SRC=${TT_SRC:-$RW/tinycc/tests/tests2}

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1
case $C in
    /*) ;;
    *) [ -x "$C" ] && C=$root/${C#./} ;;
esac
[ -d "$SRC" ] || { echo "no test directory at $SRC" >&2; exit 2; }

tmp=$(mktemp -d /tmp/cxx-tcc-XXXXXX)
trap 'rm -rf $tmp' INT TERM HUP EXIT

# Flags and arguments the tests2 Makefile gives these tests.
flags_for() {
    case $1 in
        22_floating_point | 24_math_library) echo "-lm" ;;
        106_versym) echo "-pthread" ;;
        *) echo "" ;;
    esac
}
args_for() {
    case $1 in
        31_args) echo "arg1 arg2 arg3 arg4 arg5" ;;
        *) echo "" ;;
    esac
}

# run_one <compiler> <name> <outfile> -- compile and run, exit status in $?
run_one() {
    local cc=$1 name=$2 out=$3
    rm -f "$tmp/prog"
    ( cd "$SRC" && timeout 60 "$cc" -w $(flags_for "$name") -o "$tmp/prog" "$name.c" ) \
        > "$tmp/cc.log" 2>&1 || return 1
    ( cd "$SRC" && timeout 20 "$tmp/prog" $(args_for "$name") ) > "$out" 2>&1 || return 1
    return 0
}

ref_ok=0
ref_bad=0
cxx_ok=0
cxx_bad=0
skip=0
failed=""

for f in "$SRC"/??_*.c "$SRC"/???_*.c; do
    [ -f "$f" ] || continue
    name=$(basename "$f" .c)
    exp="$SRC/$name.expect"
    [ -f "$exp" ] || { skip=$((skip + 1)); continue; }
    if [ "$CAP" -gt 0 ] && [ "$((ref_ok + cxx_bad))" -ge "$CAP" ]; then break; fi

    # The reference decides whether this test is about C or about tcc.
    if ! run_one cc "$name" "$tmp/ref.out" || ! cmp -s "$tmp/ref.out" "$exp"; then
        ref_bad=$((ref_bad + 1))
        continue
    fi
    ref_ok=$((ref_ok + 1))

    if run_one "$C" "$name" "$tmp/cxx.out" && cmp -s "$tmp/cxx.out" "$exp"; then
        cxx_ok=$((cxx_ok + 1))
    else
        cxx_bad=$((cxx_bad + 1))
        failed="$failed $name"
        cp "$tmp/cc.log" "$tmp/$name.log" 2>/dev/null || true
        cp "$tmp/cxx.out" "$tmp/$name.out" 2>/dev/null || true
    fi
done

echo "== tinycc tests/tests2 under $SRC =="
echo "  reference (cc) passes $ref_ok, does not reproduce $ref_bad, no .expect $skip"
echo "  cxx               $cxx_ok ok, $cxx_bad failed"

if [ "$cxx_bad" -gt 0 ]; then
    echo
    echo "== failures (up to 12) =="
    n=0
    for t in $failed; do
        printf '  %-28s %s\n' "$t" "$(head -2 "$tmp/$t.log" 2>/dev/null | tail -1 | cut -c1-70)"
        n=$((n + 1))
        [ "$n" -ge 12 ] && break
    done

    echo
    echo "== classes (top $CLUSTER) =="
    for t in $failed; do
        if [ -s "$tmp/$t.log" ]; then
            sed -n 's/.*error: //p;s/.*warning: //p' "$tmp/$t.log" | head -1
        else
            echo "<ran, output differs>"
        fi
    done | sed 's/[0-9]\+/N/g' | sort | uniq -c | sort -rn | head -"$CLUSTER"
fi
