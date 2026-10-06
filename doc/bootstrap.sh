#!/bin/bash
# The bootstrap chain: cxx -> cxx2 -> cxx3. Each stage compiles every source
# file, the host linker links the objects, and the next stage is built with
# the compiler the previous one produced. Stage 2 compiling cxx's own parser
# is the bar a C compiler has to clear to be self-hosting.
#
# Usage: bash doc/bootstrap.sh ./cxx [--suites]
#
# One line out: how far the chain got. A stage that stops earlier than it did
# before is what this probe is watching. With --suites the compiler stage 2
# produced is put through cxx's own test suites as well, which is the finer
# measure of how faithfully it reproduces stage 1.
set -u
C=${1:-./cxx}
suites=no
[ "${2:-}" = "--suites" ] && suites=yes
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1

out=$(mktemp -d /tmp/cxx-bootstrap-XXXXXX)
trap 'rm -rf $out' INT TERM HUP EXIT

build() { # build <compiler> <tag>
    local cc=$1 tag=$2
    local dir=$out/$tag
    mkdir -p "$dir"
    local n=0 fail=0
    for f in src/*.c src/*/*.c; do
        o=$dir/$(echo "$f" | tr '/' '_' | sed 's/\.c$/.o/')
        if $cc -w -I src -I include -c -o "$o" "$f" 2> "$dir/e"; then
            n=$((n + 1))
        else
            fail=$((fail + 1))
        fi
    done
    echo "$n $fail"
    [ $fail -eq 0 ] || return 1
    cc -o "$dir/cxx" $dir/*.o -lm 2> "$dir/link.err" || return 2
    return 0
}

# cxx looks for its headers next to its own executable (main.c's
# add_default_include_paths). A stage built in a temp directory would fall
# back to clang's headers, which use __has_feature -- a preprocessor operator
# cxx does not implement -- and the failures that follow are about where the
# binary sits, not about what it compiles. Give every stage a sibling
# include/ so the chain is measured on its own terms.
ln -s "$root/include" "$out/include" 2>/dev/null
printf 'int main(void) { return 1; }\n' > "$out/hello.c"
runs() { # runs <compiler>: compiles and runs hello, then compiles parser.c
    local cc=$1
    "$cc" -w -o "$out/hello" "$out/hello.c" > /dev/null 2>&1 || { echo "no"; return; }
    "$out/hello" > /dev/null 2>&1
    [ $? -eq 1 ] || { echo "no"; return; }
    "$cc" -w -I src -I include -S -emit-llvm -o "$out/parser.ll" src/parser.c > /dev/null 2>&1 &&
        clang -Wno-override-module -c -o /dev/null "$out/parser.ll" > /dev/null 2>&1 || { echo "half"; return; }
    echo "yes"
}

set -- $(build "$C" s2)
n2=$1; f2=$2
if [ "$f2" -ne 0 ]; then
    echo "bootstrap: cxx2 compiled $n2, failed $f2 -- no link"
    exit 1
fi
if [ ! -x "$out/s2/cxx" ]; then
    echo "bootstrap: cxx2 compiled $n2, link FAILED"
    exit 1
fi
v2=$(runs "$out/s2/cxx")
echo "bootstrap: cxx2 built, runs=$v2"
[ "$v2" = yes ] || exit 1

set -- $(build "$out/s2/cxx" s3)
n3=$1; f3=$2
if [ "$f3" -ne 0 ]; then
    echo "bootstrap: cxx3 compiled $n3, failed $f3 -- no link"
    exit 1
fi
if [ ! -x "$out/s3/cxx" ]; then
    echo "bootstrap: cxx3 compiled $n3, link FAILED"
    exit 1
fi
v3=$(runs "$out/s3/cxx")
echo "bootstrap: cxx3 built, runs=$v3"
[ "$v3" = yes ] || exit 1

if [ "$suites" = yes ]; then
    echo "bootstrap: stage 1 conformance: $(bash test/conformance.sh "$C" 2>&1 | tail -1)"
    echo "bootstrap: stage 2 conformance: $(bash test/conformance.sh "$out/s2/cxx" 2>&1 | tail -1)"
    echo "bootstrap: stage 2 c2y: $(bash test/c2y.sh "$out/s2/cxx" 2>&1 | tail -1)"
fi

# A compiler that can rebuild itself has reached a fixed point when the
# compiler it produces from its own source behaves the same way -- and a
# self-hosting compiler is reproducible, so the generations from the first
# cxx-built one on are the same bytes, built from the same objects.
set -- $(build "$out/s3/cxx" s4)
n4=$1; f4=$2
v4=$(runs "$out/s4/cxx" 2>/dev/null || echo no)
echo "bootstrap: cxx4 compiled $n4, failed $f4, runs=$v4"

if [ "$f4" -eq 0 ] && [ -x "$out/s4/cxx" ] && [ "$v4" = yes ]; then
    echo "bootstrap: fixed point reached (cxx2 -> cxx3 -> cxx4 all run)"
fi

# A self-hosting compiler is reproducible, so the generations from the first
# cxx-built one on are the same bytes, built from the same objects. This is
# the strongest line of the chain and the probe table shows the last one, so
# it goes last -- and a difference is a failure, not a note.
objects=0
for o in "$out/s3"/*.o; do
    cmp -s "$o" "$out/s4/$(basename "$o")" && objects=$((objects + 1))
done
if cmp -s "$out/s2/cxx" "$out/s3/cxx" && cmp -s "$out/s3/cxx" "$out/s4/cxx" && [ "$objects" -eq "$n4" ]; then
    echo "bootstrap: cxx2 = cxx3 = cxx4 byte for byte, $objects of $n4 objects too"
elif cmp -s "$out/s3/cxx" "$out/s4/cxx"; then
    echo "bootstrap: cxx3 = cxx4 byte for byte, $objects of $n4 objects too"
else
    echo "bootstrap: generations DIFFER in bytes ($objects of $n4 objects match)"
    exit 1
fi
