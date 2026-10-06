#!/bin/bash
# __attribute__((cleanup(f))): gcc, clang and cxx side by side.
#
# Usage: bash doc/cleanup.sh ./cxx
#
# The behaviour table is the strongest check available: every shape is a whole
# program, and the three compilers' stdout and exit status have to agree to the
# byte. The diagnostic table compares the *kind* of answer -- refused, warned
# about, or ignored in silence -- for the ways the attribute is misused.
set -u
C=${1:-./cxx}
tmp=`mktemp -d /tmp/cxx-cleanup-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

pass=0
fail=0

shape() { # shape <name> <expected "exit:stdout">
    name=$1
    cat > "$tmp/$name.c"
    ok=1
    ref=""
    line=""
    for cc in gcc clang "$C"; do
        bin="$tmp/$name.bin"
        if ! $cc -std=c23 -w -o "$bin" "$tmp/$name.c" > "$tmp/$name.err" 2>&1; then
            got="refused"
        else
            out=$("$bin" 2>&1)
            got="$?:$out"
        fi
        [ -z "$ref" ] && ref="$got"
        [ "$got" != "$ref" ] && ok=0
        line="$line $cc=[$got]"
    done
    if [ $ok -eq 1 ] && [ "$ref" != "$2" ]; then
        ok=0
        line="$line (expected [$2])"
    fi
    if [ $ok -eq 1 ]; then
        printf '  ok        %-18s %s\n' "$name" "$ref"
        pass=$((pass + 1))
    else
        printf '  MISMATCH  %-18s%s\n' "$name" "$line"
        fail=$((fail + 1))
    fi
}

echo "== behaviour (all three, stdout and exit status byte for byte)"
shape order "0:body b(2) a(1) | body2 c(3) " <<'EOF'
#include <stdio.h>
static void ha(int *p) { printf("a(%d) ", *p); }
static void hb(int *p) { printf("b(%d) ", *p); }
static void hc(int *p) { printf("c(%d) ", *p); }
int main(void) {
    { __attribute__((cleanup(ha))) int a = 1; __attribute__((cleanup(hb))) int b = 2; printf("body "); }
    printf("| ");
    { [[gnu::cleanup(hc)]] int c = 3; printf("body2 "); }
    return 0;
}
EOF

shape nested "0:b(2) c(3) a(1) " <<'EOF'
#include <stdio.h>
static void ha(int *p) { printf("a(%d) ", *p); }
static void hb(int *p) { printf("b(%d) ", *p); }
static void hc(int *p) { printf("c(%d) ", *p); }
int main(void) {
    __attribute__((cleanup(ha))) int a = 1;
    { __attribute__((cleanup(hb))) int b = 2; }
    __attribute__((cleanup(hc))) int c = 3;
    return 0;
}
EOF

shape jumps "0:a(0) a(1) b(5) c(6) done " <<'EOF'
#include <stdio.h>
static void ha(int *p) { printf("a(%d) ", *p); }
static void hb(int *p) { printf("b(%d) ", *p); }
static void hc(int *p) { printf("c(%d) ", *p); }
int main(void) {
    for (int i = 0; i < 2; i++) {
        __attribute__((cleanup(ha))) int a = i;
        if (i == 0) continue;
        break;
    }
    switch (1) { case 1: { __attribute__((cleanup(hb))) int b = 5; break; } }
    { __attribute__((cleanup(hc))) int c = 6; goto out; }
    printf("skipped ");
out:
    printf("done ");
    return 0;
}
EOF

shape for_init "0:body(0) body(1) i(1) after i(9) end " <<'EOF'
#include <stdio.h>
static void hi(int *p) { printf("i(%d) ", *p); }
static void hb(int *p) { printf("body(%d) ", *p); }
int main(void) {
    for (__attribute__((cleanup(hi))) int i = 0; i < 3; i++) {
        __attribute__((cleanup(hb))) int b = i;
        if (i == 1) break;
    }
    printf("after ");
    for (__attribute__((cleanup(hi))) int i = 9; i < 1; i++) printf("never ");
    printf("end ");
    return 0;
}
EOF

shape ret "0:h(7) f=7 g-body h(8) h(0) h(1) h(2) loop=42 " <<'EOF'
#include <stdio.h>
static void ha(int *p) { printf("h(%d) ", *p); *p = 100; }
static int f(void) { __attribute__((cleanup(ha))) int x = 7; return x; }
static void g(void) { __attribute__((cleanup(ha))) int y = 8; printf("g-body "); return; }
static int loop(void) {
    for (int i = 0; i < 10; i++) { __attribute__((cleanup(ha))) int z = i; if (i == 2) return 42; }
    return 0;
}
int main(void) { printf("f=%d ", f()); g(); printf("loop=%d ", loop()); return 0; }
EOF

shape fall_off "0:f-body end(3) back " <<'EOF'
#include <stdio.h>
static void ha(int *p) { printf("end(%d) ", *p); }
static void f(void) { __attribute__((cleanup(ha))) int x = 3; printf("f-body "); }
int main(void) { f(); printf("back "); return 0; }
EOF

shape record "0:main(7) " <<'EOF'
#include <stdio.h>
struct S { int a; int b; };
static void hs(struct S *p) { printf("main(%d) ", p->a + p->b); }
int main(void) { __attribute__((cleanup(hs))) struct S s = {3, 4}; return 0; }
EOF

shape vla "0:size(12) " <<'EOF'
#include <stdio.h>
static void hv(int (*p)[3]) { printf("size(%d) ", (int)sizeof *p); }
int main(void) { int n = 3; __attribute__((cleanup(hv))) int a[n]; a[0] = 1; return 0; }
EOF

shape parameter_ignored "0:f(5) back " <<'EOF'
#include <stdio.h>
static void hp(int *p) { printf("param(%d) ", *p); }
static void f(__attribute__((cleanup(hp))) int x) { printf("f(%d) ", x); }
int main(void) { f(5); printf("back "); return 0; }
EOF

kind() { # kind <compiler> <file>
    out=$($1 -std=c23 -S -o /dev/null "$2" 2>&1)
    st=$?
    if [ $st -ne 0 ]; then echo refused
    elif [ -n "$out" ]; then echo warned
    else echo silent
    fi
}

diag() { # diag <name>
    name=$1
    cat > "$tmp/$name.c"
    g=$(kind gcc "$tmp/$name.c")
    c=$(kind clang "$tmp/$name.c")
    x=$(kind "$C" "$tmp/$name.c")
    if [ "$g" = "$c" ] && [ "$c" = "$x" ]; then
        printf '  ok        %-18s %s\n' "$name" "$x"
        pass=$((pass + 1))
    else
        printf '  MISMATCH  %-18s gcc=%s clang=%s cxx=%s\n' "$name" "$g" "$c" "$x"
        fail=$((fail + 1))
    fi
}

echo
echo "== diagnostics (refused, warned about, or ignored in silence)"
diag file_scope <<'EOF'
static void h(int *p) { (void)p; }
__attribute__((cleanup(h))) static int g = 1;
int main(void) { return 0; }
EOF
diag block_static <<'EOF'
static void h(int *p) { (void)p; }
int main(void) { __attribute__((cleanup(h))) static int s = 1; return 0; }
EOF
diag parameter <<'EOF'
static void h(int *p) { (void)p; }
static void f(__attribute__((cleanup(h))) int x) { (void)x; }
int main(void) { f(5); return 0; }
EOF
diag typedef_attr <<'EOF'
static void h(int *p) { (void)p; }
typedef int T __attribute__((cleanup(h)));
int main(void) { T x = 1; (void)x; return 0; }
EOF
diag on_function <<'EOF'
static void h(int *p) { (void)p; }
static void f(void) __attribute__((cleanup(h)));
int main(void) { return 0; }
EOF
diag not_a_function <<'EOF'
int main(void) { int n = 0; __attribute__((cleanup(n))) int x = 1; return 0; }
EOF
diag address_of <<'EOF'
static void h(int *p) { (void)p; }
int main(void) { __attribute__((cleanup(&h))) int x = 1; return 0; }
EOF
diag wrong_type <<'EOF'
static void h(char *p) { (void)p; }
int main(void) { __attribute__((cleanup(h))) int x = 1; return 0; }
EOF
diag two_parameters <<'EOF'
static void h(int *p, int q) { (void)p; (void)q; }
int main(void) { __attribute__((cleanup(h))) int x = 1; return 0; }
EOF
diag no_argument <<'EOF'
int main(void) { __attribute__((cleanup)) int x = 1; return 0; }
EOF

echo
echo "cleanup: $pass passed, $fail mismatch(es)"
[ $fail -eq 0 ]
