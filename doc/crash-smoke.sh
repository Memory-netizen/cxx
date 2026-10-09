#!/bin/bash
# Does cxx ever die on a signal?
#
#   bash doc/crash-smoke.sh [results] [stride]
#
# The rule this checks is absolute: whatever the input, the compiler must not
# crash -- it may refuse the file, with a diagnostic and a non-zero exit, but
# it must not die. The probe's own results only tell us about the files it was
# counting, so this walks the whole corpus instead: every `stride`-th line of a
# results file, whatever verdict it carries, checked with `-fsyntax-only` and a
# short timeout. A signal exit (>= 128) is a crash; `internal compiler error`
# in the output is the driver reporting one.
#
# cxx's -fsyntax-only runs the whole front end and writes nothing (see the
# comment in cc1()), so this covers the parser, the folder and irgen -- which
# is where a crash would be. Before that option existed this script passed
# `-fsyntax-only` anyway and cxx answered "unknown argument" for every file,
# so its readings measured nothing; `-S -o /dev/null` was the stand-in.
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
# Every stride-th line, and with stride 1 every line: `NR % s == 1` silently
# selected nothing for stride 1, which is exactly the sweep this is for.
grep -oE '/home/memory/compiler-test-suite/C/[^ ]+\.c' "$LOG" 2>/dev/null | awk -v s="$STRIDE" 'NR % s == 0' > "$LIST"
n=$(wc -l < "$LIST")
if [ "$n" -eq 0 ]; then
    echo "crash-smoke: no tests found in $LOG" >&2
    exit 1
fi

cat > "$WORK/one.sh" <<'SH'
#!/bin/bash
cxx=$1
f=$2
out=$(timeout 25 "$cxx" -w -fsyntax-only "$f" 2>&1)
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

echo "crash-smoke: $n of the corpus's tests checked with -fsyntax-only, $crashes crashed"
if [ "$crashes" -gt 0 ]; then
    head -20 "$WORK/crashes"
fi
rm -rf "$WORK"
exit "$crashes"
