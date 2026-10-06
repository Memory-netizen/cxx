#!/bin/bash
# Minimise the two invalid-IR findings.
C=/home/memory/cxx/cxx
t=`mktemp -d /tmp/cxx-min-XXXXXX`
trap 'rm -rf $t' EXIT
try() {
    n=$1; cat > "$t/$n.c"
    printf '%-46s ' "$n"
    if $C -w -S -o /dev/null "$t/$n.c" > "$t/e" 2>&1; then echo OK; else
        grep -m1 -E 'error|PHI|expected' "$t/e" | sed 's/^/BAD: /'; fi
}

echo "=== A. nullptr_t / bool contexts"
try a1 <<'EOF'
#include <stddef.h>
int f(void) { nullptr_t a = nullptr; return !a; }
EOF
try a2 <<'EOF'
#include <stddef.h>
int f(void) { nullptr_t a = nullptr; return a ? 0 : 1; }
EOF
try a3 <<'EOF'
#include <stddef.h>
int f(void) { nullptr_t a = nullptr; void *p = a; return p == 0; }
EOF
try a4 <<'EOF'
#include <stddef.h>
int f(void) { nullptr_t a = nullptr, b = nullptr; return a == b; }
EOF
try a5 <<'EOF'
#include <stddef.h>
int f(void) { nullptr_t a = nullptr; return a == nullptr; }
EOF
try a6 <<'EOF'
#include <stddef.h>
int f(int x) { nullptr_t a = nullptr; if (a && x) return 1; return 0; }
EOF
try a7 <<'EOF'
#include <stddef.h>
int f(int x) { nullptr_t a = nullptr; return a && x; }
EOF
try a8 <<'EOF'
#include <stddef.h>
int f(int x) { nullptr_t a = nullptr; return x ? 0 : !a; }
EOF
try a9 <<'EOF'
#include <stddef.h>
int f(int x) { if (x) { return 1; } return 0; }
EOF
try a10 <<'EOF'
#include <stddef.h>
int f(int x, int y) { int r = 0; if (x) r = 1; if (y) r = 2; return r ? 0 : 1; }
EOF
try a11 <<'EOF'
#include <stddef.h>
int f(int x, int y, int z) { return (x && y && z) ? 0 : 1; }
EOF
try a12 <<'EOF'
#include <stddef.h>
int f(void) { nullptr_t a = nullptr; int b = !a; int c = a ? 0 : 1; return b + c; }
EOF

echo
echo "=== B. _Atomic aggregate access"
try b1 <<'EOF'
#include <stdatomic.h>
struct S { int a; };
int f(void) { _Atomic struct S v; atomic_store(&v, (struct S){7}); return atomic_load(&v).a; }
EOF
try b2 <<'EOF'
#include <stdatomic.h>
struct S { int a; };
int f(void) { _Atomic struct S v; atomic_store(&v, (struct S){7}); struct S w = atomic_load(&v); return w.a; }
EOF
try b3 <<'EOF'
#include <stdatomic.h>
struct S { int a; };
int f(void) { _Atomic struct S v; struct S w = {1}; atomic_store(&v, w); return 0; }
EOF
try b4 <<'EOF'
#include <stdatomic.h>
struct S { int a; };
int f(void) { _Atomic struct S v; atomic_init(&v, (struct S){7}); return 0; }
EOF
try b5 <<'EOF'
#include <stdatomic.h>
int f(void) { _Atomic int v; atomic_store(&v, 7); return atomic_load(&v); }
EOF
try b6 <<'EOF'
#include <stdatomic.h>
struct S { int a; };
_Atomic struct S g;
int f(void) { return g.a; }
EOF
try b7 <<'EOF'
#include <stdatomic.h>
struct S { int a; };
int f(_Atomic struct S *p) { return p->a; }
EOF

echo
echo "=== C. probe-defect confirmations"
try c1 <<'EOF'
#include <uchar.h>
int f(void) { return _Generic(u8'x', char8_t: 1, default: 0); }
EOF
try c2 <<'EOF'
int f(void) { 1 = 2; return 0; }
EOF
try c3 <<'EOF'
int f(void) { static int g(void); return 0; }
EOF
try c4 <<'EOF'
#include <stddef.h>
#include <stdio.h>
int main(void) {
    nullptr_t a = nullptr, b = nullptr;
    void *p = a;
    printf("%d %d %d %d\n", (int)(a == b), (int)(p == 0), (int)!a, (int)(a ? 0 : 1));
    return 0;
}
EOF
