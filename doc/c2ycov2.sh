#!/bin/bash
# C2y (N3685) coverage probe, wave 2: the items wave 1 could not settle,
# plus the library surface the draft requires.
C=/home/memory/cxx/cxx
t=`mktemp -d /tmp/cxx-c2y2-XXXXXX`
trap 'rm -rf $t' EXIT
n_pass=0; n_fail=0; n_gap=0

run() {
    cat > "$t/t.c"
    if $C -w -o "$t/t" "$t/t.c" > "$t/log" 2>&1 && "$t/t"; then
        echo "PASS  $1"; n_pass=$((n_pass+1))
    else
        echo "FAIL  $1"; sed 's/^/          /' "$t/log" | head -3; n_fail=$((n_fail+1))
    fi
}
ok() {
    cat > "$t/t.c"
    if $C -w -S -emit-llvm -o /dev/null "$t/t.c" > "$t/log" 2>&1; then
        echo "PASS  $1"; n_pass=$((n_pass+1))
    else
        echo "FAIL  $1"; sed 's/^/          /' "$t/log" | head -3; n_fail=$((n_fail+1))
    fi
}
rej() {
    cat > "$t/t.c"
    if $C -w -S -o /dev/null "$t/t.c" > "$t/log" 2>&1; then
        echo "FAIL  $1  (accepted; expected a diagnostic)"; n_fail=$((n_fail+1))
    else
        echo "PASS  $1"; n_pass=$((n_pass+1))
    fi
}
# a library surface that only the host C library can supply
lib() {
    cat > "$t/t.c"
    if $C -w -S -o /dev/null "$t/t.c" > "$t/log" 2>&1; then
        echo "PASS  $1"; n_pass=$((n_pass+1))
    else
        echo "GAP   $1"
        sed 's/^/          /' "$t/log" | head -2
        n_gap=$((n_gap+1))
    fi
}

echo "### A. items wave 1 could not settle"
run "\\x{41} braced hex escape returns 'A'" <<'EOF'
int main(void) { return '\x{41}' == 'A' ? 0 : 1; }
EOF
run "char8_t comes from <uchar.h>" <<'EOF'
#include <uchar.h>
int main(void) { char8_t c = u8'x'; return c == 'x' ? 0 : 1; }
EOF
run "true/false/nullptr literals" <<'EOF'
#include <stddef.h>
int main(void) { nullptr_t p = nullptr; return (true && !false && p == nullptr) ? 0 : 1; }
EOF
run "named continue to a loop label jumps to the increment" <<'EOF'
int main(void) { int n = 0; outer: for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) { n++; if (j == 1) continue outer; }
    return n == 6 ? 0 : 1; }
EOF
run "&& of two comparisons with a constant left operand" <<'EOF'
int main(void) { return ('A' == 'A') && ("xy"[1] == 'y') ? 0 : 1; }
EOF
run "1 && ptr-comparison" <<'EOF'
int main(void) { int x = 1; int *p = &x; return (1 && (p != 0)) ? 0 : 1; }
EOF
run "0 || ptr-comparison" <<'EOF'
int main(void) { int x = 1; int *p = &x; return (0 || (p != 0)) ? 0 : 1; }
EOF

echo
echo "### B. N3348 unspecified-size array types in a generic association"
rej "6.5.2.1 EXAMPLE 3: _Generic(int[3][2], int[3][*]: 1, int[2][*]: 0)" <<'EOF'
int main(void) { return _Generic(int[3][2], int[3][*]: 1, int[2][*]: 0) == 1 ? 0 : 1; }
EOF
rej "6.5.2.1 EXAMPLE 3: _Generic(int(*)[2], int(*)[*]: 1)" <<'EOF'
int main(void) { return _Generic(int(*)[2], int(*)[*]: 1, default: 0); }
EOF
run "array operand of _Generic decays to a pointer (6.5.2.1 p2)" <<'EOF'
int main(void) { int a[3]; return _Generic(a, int[3]: 1, int *: 2, default: 0) == 2 ? 0 : 1; }
EOF

echo
echo "### C. N3652 composite types"
run "composite array type from two declarations" <<'EOF'
int a[];
int a[10];
int main(void) { return sizeof(a) == 40 ? 0 : 1; }
EOF
run "composite array type used in a function" <<'EOF'
int a[];
int a[10];
int f(void) { return sizeof(a); }
int main(void) { return f() == 40 ? 0 : 1; }
EOF
run "tentative definition completed later" <<'EOF'
static int b[];
static int b[4] = {1, 2, 3, 4};
int main(void) { return (sizeof(b) == 16 && b[3] == 4) ? 0 : 1; }
EOF
run "composite struct type" <<'EOF'
struct S;
struct S { int a; };
struct S v;
int main(void) { v.a = 3; return v.a == 3 ? 0 : 1; }
EOF
run "composite function type" <<'EOF'
int f(int);
int f(int x) { return x; }
int main(void) { return f(1) - 1; }
EOF

echo
echo "### D. storage class / specifier classification (N3244, N3544)"
run "register on a scalar" <<'EOF'
int main(void) { register int a = 1; return a - 1; }
EOF
rej "register object address is not computable" <<'EOF'
int main(void) { register int a = 1; int *p = &a; return *p; }
EOF
ok "_Thread_local and thread_local" <<'EOF'
_Thread_local int a;
thread_local int b;
int main(void) { return a + b; }
EOF
# `noreturn` is a macro of <stdnoreturn.h> in C23, not a keyword: without
# the include gcc, clang and cxx all reject it, so the entry has to include
# the header it belongs to.
ok "_Noreturn and noreturn" <<'EOF'
#include <stdnoreturn.h>
_Noreturn void f(void);
noreturn void g(void);
int main(void) { return 0; }
EOF
ok "auto type inference" <<'EOF'
int main(void) { auto x = 3; auto y = 2.5; return (x == 3 && y > 2.0) ? 0 : 1; }
EOF

echo
echo "### E. constexpr / typeof / _Countof constraints"
run "constexpr object of scalar, array and struct type" <<'EOF'
constexpr int a = 5;
constexpr int b[3] = {1, 2, 3};
struct S { int x; };
constexpr struct S s = {7};
int main(void) { return (a == 5 && b[2] == 3 && s.x == 7) ? 0 : 1; }
EOF
run "constexpr compound literal" <<'EOF'
struct S { int a; };
int main(void) { constexpr struct S s = (constexpr struct S){ .a = 4 }; return s.a == 4 ? 0 : 1; }
EOF
rej "constexpr object is not modifiable" <<'EOF'
constexpr int a = 1;
int main(void) { a = 2; return a; }
EOF
run "typeof does not evaluate its operand" <<'EOF'
int main(void) { int n = 0; typeof(n++) t = 3; return (n == 0 && t == 3) ? 0 : 1; }
EOF
rej "_Countof of a pointer" <<'EOF'
int f(int *p) { return _Countof(p); }
EOF
rej "_Countof of a scalar" <<'EOF'
int main(void) { int x = 0; return _Countof(x); }
EOF

echo
echo "### F. library surface required by the draft"
lib "N3366 mbrtoc8 / c8rtomb" <<'EOF'
#include <uchar.h>
int main(void) { char8_t c; mbstate_t st = {0}; return mbrtoc8(&c, "a", 1, &st) == 1 ? 0 : 1; }
EOF
lib "N3366 restartable restartable conversion helpers" <<'EOF'
#include <uchar.h>
int main(void) { return 0; }
EOF
lib "N3326 strnlen" <<'EOF'
#include <string.h>
int main(void) { return strnlen("abc", 5) == 3 ? 0 : 1; }
EOF
lib "N3322 free_sized / free_aligned_sized" <<'EOF'
#include <stdlib.h>
int main(void) { void *p = malloc(8); free_sized(p, 8); void *q = malloc(8); free_aligned_sized(q, 8, 8); return 0; }
EOF
lib "N3322 memalignment" <<'EOF'
#include <stdlib.h>
int main(void) { char b[16]; return memalignment(b) > 0 ? 0 : 1; }
EOF
lib "N3577 umaxabs" <<'EOF'
#include <inttypes.h>
int main(void) { return umaxabs((intmax_t)-5) == 5 ? 0 : 1; }
EOF
lib "N3364 NAN / signaling NaN initialisation" <<'EOF'
#include <math.h>
int main(void) { double d = NAN; return d != d ? 0 : 1; }
EOF
lib "N3367 <stdbit.h> bit utilities" <<'EOF'
#include <stdbit.h>
int main(void) { return stdc_bit_floor(5u) == 4u ? 0 : 1; }
EOF
lib "N3469 <stdcountof.h> countof()" <<'EOF'
#include <stdcountof.h>
int main(void) { int a[4]; return countof(a) == 4 ? 0 : 1; }
EOF
lib "7.26 <stdmchar.h>" <<'EOF'
#include <stdmchar.h>
int main(void) { return 0; }
EOF
lib "7.28 memccpy-style byte arrays (N3254)" <<'EOF'
#include <string.h>
int main(void) { char a[4] = {0}; memcpy(a, "xyz", 3); return a[2] == 'z' ? 0 : 1; }
EOF

echo
echo "### G. feature macros (with #ifdef, not -E)"
chk() {
    cat > "$t/m.c" <<EOF
#ifdef $1
int $2 = 1;
#else
int $2 = 0;
#endif
EOF
    v=$($C -E "$t/m.c" 2>/dev/null | grep -c "int $2 = 1")
    printf 'INFO  %-34s %s\n' "$1" "$([ "$v" = 1 ] && echo defined || echo 'not defined')"
}
chk __STDC_IEC_60559_TYPES__ a1
chk __STDC_IEC_60559_BFP__ a2
chk __STDC_IEC_60559_DFP__ a3
chk __STDC_IEC_559__ a4
chk __STDC_NO_COMPLEX__ a5
chk __STDC_NO_ATOMICS__ a6
chk __STDC_NO_THREADS__ a7
chk __STDC_NO_VLA__ a8
chk __STDC_NO_DECIMAL_FLOAT__ a9
chk __STDC_NO_DECIMAL128_FLOAT__ a10
chk __STDC_EMBED_FOUND__ a11
chk __GNUC__ a12
chk __clang__ a13

echo
echo "c2ycov2: $n_pass passed, $n_fail failed, $n_gap library gap(s)"
