#!/bin/bash
# -E round trip through cxx itself: compile and run the original with cxx,
# then preprocess it, compile and run *that* with cxx, and compare. This is
# what exercises the printer -- print_tokens(), needs_space() and the
# is_sol/origin bookkeeping that decides where the text breaks lines -- on
# real files rather than on hand-written probes.
C=/home/memory/cxx/cxx
SRC=${SRC:-/home/memory/compiler-test-suite/C}
T=${TMPDIR:-/tmp}/eround.$$
mkdir -p "$T"
trap 'rm -rf "$T"' EXIT
n=0; same=0; diffn=0; skip=0
for f in "$SRC"/0048/*.c "$SRC"/0125/*.c "$SRC"/0054/*.c "$SRC"/0081/*.c; do
    b=$(basename "$f" .c)
    if ! "$C" -w -o "$T/ref" "$f" -lm > "$T/ref.log" 2>&1; then
        skip=$((skip + 1)); continue
    fi
    "$T/ref" > "$T/ref.out" 2>&1; refrc=$?
    if ! "$C" -E "$f" > "$T/pp.c" 2> "$T/pp.log"; then skip=$((skip + 1)); continue; fi
    if ! "$C" -w -o "$T/rt" "$T/pp.c" -lm > "$T/rt.log" 2>&1; then
        echo "RT-COMPILE-FAIL $b"; head -3 "$T/rt.log" | sed 's/^/        /'; diffn=$((diffn + 1)); continue
    fi
    "$T/rt" > "$T/rt.out" 2>&1; rtrc=$?
    n=$((n + 1))
    if cmp -s "$T/ref.out" "$T/rt.out" && [ "$refrc" = "$rtrc" ]; then
        same=$((same + 1))
    else
        echo "RT-DIFF $b (rc $refrc vs $rtrc)"
        diff "$T/ref.out" "$T/rt.out" | head -4 | sed 's/^/        /'
        diffn=$((diffn + 1))
    fi
done
echo "round trip: $same of $n same, $diffn different, $skip skipped"
