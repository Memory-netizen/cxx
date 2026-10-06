#!/bin/bash
# T1: pointer-vs-0 comparisons without an inttoptr in front.
C=/home/memory/cxx/cxx
CC=clang
t=`mktemp -d /tmp/cxx-t1-XXXXXX`
trap 'rm -rf $t' EXIT

cat > "$t/a.c" <<'EOF'
#include <stddef.h>
int f1(void *p) { return p != 0; }
int f2(void *p) { return p == 0; }
int f3(void *p) { return p == NULL; }
int f4(void *p) { return p != nullptr; }
void *f5(void) { return 0; }
void *f6(void) { void *p = 0; return p; }
int f7(int *p) { return p > 0; }
int f8(void *p, long n) { return p == n; }
EOF

echo "=== cxx"
$C -w -S -emit-llvm -o "$t/a.ll" "$t/a.c" 2>&1 | head -3
grep -E 'define|icmp|inttoptr|ret ' "$t/a.ll" | grep -vE '^\s*ret void'
echo
echo "=== clang"
$CC -std=c2y -w -S -emit-llvm -O0 -o "$t/b.ll" "$t/a.c" 2>/dev/null
grep -E 'define|icmp|inttoptr|ret ' "$t/b.ll" | grep -vE '^\s*ret void'

echo
echo "=== inttoptr must still appear where it belongs"
printf 'void *g(long n) { return (void *)n; }\nvoid *h(int n) { return (void *)(n + 1); }\n' > "$t/c.c"
$C -w -S -emit-llvm -o "$t/c.ll" "$t/c.c" 2>&1 | head -3
grep -cE 'inttoptr' "$t/c.ll"

echo
echo "=== runtime behaviour unchanged"
cat > "$t/d.c" <<'EOF'
#include <stdio.h>
#include <stddef.h>
static void *z(void) { return 0; }
int main(void) {
    int x = 1; void *p = &x, *q = 0;
    printf("%d %d %d %d %d\n", p != 0, p == 0, q == 0, q == NULL, z() == nullptr);
    return 0;
}
EOF
$C -w -o "$t/d" "$t/d.c" 2>&1 | head -3 && "$t/d"
$CC -std=c2y -w -o "$t/dc" "$t/d.c" 2>/dev/null && "$t/dc"
