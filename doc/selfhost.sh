#!/bin/bash
# Can cxx compile its own sources? The scoreboard of a self-hosting attempt.
#
# Usage: bash doc/selfhost.sh ./cxx
#
# Every file of src/ is compiled by cc1 (cxx's own front end and code
# generator, without the assembler), and the module it prints is handed to
# clang. Three verdicts, and the point of the probe is the first column: a
# compiler that cannot read its own source is missing something a real
# program uses, and each missing piece here showed up first as a crash or an
# invalid module rather than as a diagnostic.
#
#   ok      cc1 survived and LLVM accepted the module
#   IR bad  cc1 survived, but the module is not valid LLVM IR
#   crash   cc1 died (a signal, or a diagnostic), or refused the file
#
# EXPECT_OK is the floor: fewer than that many ok files is a regression, and
# the run fails. Raise it as the compiler grows.
set -u
C=${1:-./cxx}
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1

EXPECT_OK=${EXPECT_OK:-21}
tmp=`mktemp -d /tmp/cxx-selfhost-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

ok=0
irbad=0
bad=0

for f in src/*.c src/*/*.c; do
    $C -cc1 -w -S -emit-llvm -I src -I include -cc1-input "$f" -cc1-output "$tmp/o.ll" > /dev/null 2> "$tmp/e"
    st=$?
    if [ $st -ge 128 ]; then
        printf '  crash   %-24s (signal %s)\n' "$f" "$((st - 128))"
        bad=$((bad + 1))
        continue
    fi
    if [ $st -ne 0 ]; then
        printf '  crash   %-24s %s\n' "$f" "$(head -1 "$tmp/e" | cut -c1-64)"
        bad=$((bad + 1))
        continue
    fi
    if clang -Wno-override-module -c -o /dev/null "$tmp/o.ll" 2> "$tmp/ce"; then
        printf '  ok      %-24s\n' "$f"
        ok=$((ok + 1))
    else
        printf '  IR bad  %-24s %s\n' "$f" "$(grep -m1 error "$tmp/ce" | cut -c1-64)"
        irbad=$((irbad + 1))
    fi
done

echo
if [ "$ok" -lt "$EXPECT_OK" ]; then
    echo "selfhost: $ok ok, $irbad with invalid IR, $bad crashed (expected at least $EXPECT_OK ok)"
    exit 1
fi
if [ "$ok" -gt "$EXPECT_OK" ]; then
    echo "selfhost: $ok ok, $irbad with invalid IR, $bad crashed (expected $EXPECT_OK ok: raise EXPECT_OK)"
    exit 1
fi
echo "selfhost: $ok ok, $irbad with invalid IR, $bad crashed"
