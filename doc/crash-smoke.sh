#!/bin/bash
# Does cxx ever die on a signal?
#
#   bash doc/crash-smoke.sh [results] [stride]
#
# The rule this checks is absolute: whatever the input, the compiler must not
# crash -- it may refuse the file, with a diagnostic and a non-zero exit, but
# it must not die. The probe's own results only tell us about the files it was
# counting, so this walks the whole corpus instead: every `stride`-th line of a
# results file, whatever verdict it carries, compiled with `-S` and a short
# timeout. A signal exit (>= 128) is a crash; `internal compiler error` in the
# output is the driver reporting one.
#
# `-fsyntax-only` is what a smoke test wants, but cxx has no such option: it
# answered "unknown argument" and exited 1 for every file, which this script
# counted as a clean run. `-S -o /dev/null` is the closest thing that exists,
# and it still runs the whole front end.
#
# The exit status is the number of crashes, so it can gate a commit.
set -u
cd "$(dirname "$0")/.."
CXX=${CXX:-$HOME/cxx/cxx}
LOG=${1:-$HOME/cxxwork/logs/fj-full4.results}
STRIDE=${2:-40}
JOBS=${SMOKE_JOBS:-8}
WORK=${SMOKE_WORK:-$(mktemp -d)}

LIST=$WORK/sample.list
grep -oE '/home/memory/compiler-test-suite/C/[^ ]+\.c' "$LOG" 2>/dev/null | awk -v s="$STRIDE" 'NR % s == 1' > "$LIST"
n=$(wc -l < "$LIST")
if [ "$n" -eq 0 ]; then
    echo "crash-smoke: no tests found in $LOG" >&2
    exit 1
fi

cat > "$WORK/one.sh" <<'SH'
#!/bin/bash
cxx=$1
f=$2
out=$(timeout 25 "$cxx" -w -S -o /dev/null "$f" 2>&1)
rc=$?
if [ $rc -ge 128 ]; then
    printf 'signal %s\t%s\n' "$rc" "$f"
elif printf '%s' "$out" | grep -q 'internal compiler error'; then
    printf 'ice\t%s\n' "$f"
fi
SH
chmod +x "$WORK/one.sh"

: > "$WORK/crashes"
xargs -P "$JOBS" -I{} "$WORK/one.sh" "$CXX" {} < "$LIST" >> "$WORK/crashes"
crashes=$(wc -l < "$WORK/crashes")

echo "crash-smoke: $n of the corpus's tests compiled with -S, $crashes crashed"
if [ "$crashes" -gt 0 ]; then
    head -20 "$WORK/crashes"
fi
rm -rf "$WORK"
exit "$crashes"
