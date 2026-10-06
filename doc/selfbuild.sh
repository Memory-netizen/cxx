#!/bin/bash
# The two-stage build: cxx compiles cxx, the host linker links the objects,
# and the compiler that comes out is asked to compile a program and then
# cxx's own parser. This is the ledge the self-hosting work is standing on.
#
# Usage: bash doc/selfbuild.sh ./cxx
#
# Reported as one line: how far the second stage got. cxx2 not working yet is
# not a failure of this probe -- the line is the measurement -- but a stage
# that stops earlier than it did before is worth noticing.
set -u
C=${1:-./cxx}
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1

out=`mktemp -d /tmp/cxx-selfbuild-XXXXXX`
trap 'rm -rf $out' INT TERM HUP EXIT

compiled=0
failed=0
for f in src/*.c src/*/*.c; do
    o=$out/$(echo "$f" | tr '/' '_' | sed 's/\.c$/.o/')
    if $C -w -I src -I include -c -o "$o" "$f" 2> "$out/e"; then
        compiled=$((compiled + 1))
    else
        failed=$((failed + 1))
    fi
done

if [ $failed -ne 0 ]; then
    echo "selfbuild: compiled $compiled, failed $failed, no link"
    exit 1
fi

if ! cc -o "$out/cxx2" $out/*.o -lm 2> "$out/link.err"; then
    echo "selfbuild: compiled $compiled, link FAILED: $(head -1 "$out/link.err" | cut -c1-60)"
    exit 1
fi

printf 'int main(void) { return 1; }\n' > "$out/hello.c"
if "$out/cxx2" -w -o "$out/hello" "$out/hello.c" > /dev/null 2>&1; then
    "$out/hello" > /dev/null 2>&1
    hello="hello=$?"
else
    hello=hello=rejected
fi

if "$out/cxx2" -w -I src -I include -S -emit-llvm -o "$out/parser.ll" src/parser.c > /dev/null 2>&1 &&
   clang -Wno-override-module -c -o /dev/null "$out/parser.ll" > /dev/null 2>&1; then
    itself=itself=ok
else
    itself=itself=no
fi

echo "selfbuild: compiled $compiled, linked, cxx2 $hello $itself"
