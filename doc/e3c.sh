#!/bin/bash
# E3: every local that IS used must stay silent. The earlier version marked
# the flag on the wrong node, so nothing counted as used and every local --
# including ones referenced as a call argument, through a member, or as a
# subscript -- was reported.
C=/home/memory/cxx/cxx
CC=clang
t=`mktemp -d /tmp/cxx-e3c-XXXXXX`
trap 'rm -rf $t' EXIT

cat > "$t/used.c" <<'EOF'
#include <stdio.h>
#include <string.h>
struct S { char c; int i; };
static int f(int x) { return x; }
int main(void) {
    char buf[8];
    struct S s;
    int a = 1, b = a, c[3];
    int *p = &a;
    memset(buf, 0, sizeof buf);
    s.i = 2;
    c[0] = 3;
    *p = 4;
    printf("%d %d %d %d %d %d\n", buf[0], s.i, a, b, c[0], f(a));
    return 0;
}
EOF

cat > "$t/unused.c" <<'EOF'
int main(void) {
    int used = 1;
    int dead = 2;
    return used;
}
EOF

echo "=== a file where every local is used: cxx must report nothing"
$C -S -o /dev/null "$t/used.c" > "$t/o" 2>&1
echo "cxx   warnings: $(grep -c 'unused variable' "$t/o")  $(grep -oE "unused variable .[a-z0-9_]+." "$t/o" | tr '\n' ' ')"
echo "clang warnings: $($CC -std=c2y -Wunused-variable -S -o /dev/null "$t/used.c" 2>&1 | grep -c 'unused variable')"

echo
echo "=== exactly one unused local"
$C -S -o /dev/null "$t/unused.c" > "$t/o" 2>&1
echo "cxx   warnings: $(grep -c 'unused variable' "$t/o")  $(grep -oE "unused variable .[a-z0-9_]+." "$t/o" | tr '\n' ' ')"
echo "clang warnings: $($CC -std=c2y -Wunused-variable -S -o /dev/null "$t/unused.c" 2>&1 | grep -c 'unused variable')"

echo
echo "=== -w and -Wno-unused-variable still silence it"
for f in "-w" "-Wno-unused-variable"; do
    printf '  %-24s %s\n' "[$f]" "$($C $f -S -o /dev/null "$t/unused.c" 2>&1 | grep -c 'unused variable')"
done
