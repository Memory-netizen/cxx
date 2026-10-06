#!/bin/bash
# Wave 3: math macros/builtins, noreturn, and the remaining Annex A productions.
C=/home/memory/cxx/cxx
CC=clang
t=`mktemp -d /tmp/cxx-c2y3-XXXXXX`
trap 'rm -rf $t' EXIT
n_pass=0; n_fail=0

run() {
    cat > "$t/t.c"
    if $C -w -o "$t/t" "$t/t.c" > "$t/log" 2>&1 && "$t/t"; then
        echo "PASS  $1"; n_pass=$((n_pass+1))
    else
        echo "FAIL  $1"; sed 's/^/          /' "$t/log" | head -2; n_fail=$((n_fail+1))
    fi
}
ok() {
    cat > "$t/t.c"
    if $C -w -S -emit-llvm -o /dev/null "$t/t.c" > "$t/log" 2>&1; then
        echo "PASS  $1"; n_pass=$((n_pass+1))
    else
        echo "FAIL  $1"; sed 's/^/          /' "$t/log" | head -2; n_fail=$((n_fail+1))
    fi
}
rej() {  # must be diagnosed
    cat > "$t/t.c"
    if $C -w -S -o /dev/null "$t/t.c" > "$t/log" 2>&1; then
        echo "FAIL  $1  (accepted; expected a diagnostic)"; n_fail=$((n_fail+1))
    else
        echo "PASS  $1"; n_pass=$((n_pass+1))
    fi
}
cmp_() {  # show cxx vs clang for a one-liner, no pass/fail judgement
    n=$1; shift
    printf '%s\n' "$1" > "$t/$n.c"
    printf '%-34s cxx: ' "$n"
    if $C -w -S -emit-llvm -o /dev/null "$t/$n.c" > "$t/e" 2>&1; then printf 'OK   '; else printf 'REJ  '; fi
    printf 'clang: '
    if $CC -std=c2y -w -S -emit-llvm -o /dev/null "$t/$n.c" > "$t/e2" 2>&1; then printf 'OK\n'; else printf 'REJ\n'; fi
}

echo "### H. <stdnoreturn.h> and the noreturn spelling"
run "noreturn after <stdnoreturn.h>" <<'EOF'
#include <stdnoreturn.h>
noreturn void g(void);
void g(void) { for (;;) { } }
int main(void) { return 0; }
EOF
ok "_Noreturn keyword without a header" <<'EOF'
_Noreturn void g(void);
int main(void) { return 0; }
EOF

echo
echo "### I. <math.h> macros and the builtins behind them"
run "NAN" <<'EOF'
#include <math.h>
int main(void) { double d = NAN; return d != d ? 0 : 1; }
EOF
run "INFINITY" <<'EOF'
#include <math.h>
int main(void) { double d = INFINITY; return d > 1e308 ? 0 : 1; }
EOF
run "HUGE_VAL / HUGE_VALF / HUGE_VALL" <<'EOF'
#include <math.h>
int main(void) { return (HUGE_VAL > 1e308 && HUGE_VALF > 1e38f && HUGE_VALL > 1e308L) ? 0 : 1; }
EOF
run "isnan / isinf / isfinite / signbit" <<'EOF'
#include <math.h>
int main(void) {
    double n = NAN, i = INFINITY;
    return (isnan(n) && !isnan(i) && isinf(i) && !isfinite(i) && signbit(-1.0)) ? 0 : 1;
}
EOF
run "fpclassify / isnormal" <<'EOF'
#include <math.h>
int main(void) { return (fpclassify(1.0) == FP_NORMAL && isnormal(1.0) && !isnormal(0.0)) ? 0 : 1; }
EOF
run "fp comparison macros isgreater/isless" <<'EOF'
#include <math.h>
int main(void) { return (isgreater(2.0, 1.0) && isless(1.0, 2.0)) ? 0 : 1; }
EOF
# The host does not fold libm into libc: the same program fails to link
# with clang unless -lm is given, so the entry passes it too.
cat > "$t/t.c" <<'EOF'
#include <math.h>
int main(void) { return (sqrt(4.0) == 2.0 && fabs(-3.0) == 3.0 && ldexp(1.0, 3) == 8.0) ? 0 : 1; }
EOF
if $C -w -lm -o "$t/t" "$t/t.c" > "$t/log" 2>&1 && "$t/t"; then
    echo "PASS  math functions link (sqrt/fabs/ldexp)"; n_pass=$((n_pass+1))
else
    echo "FAIL  math functions link (sqrt/fabs/ldexp)"; sed 's/^/          /' "$t/log" | head -2; n_fail=$((n_fail+1))
fi

echo
echo "### J. individual math builtins glibc's <math.h> expands to"
for b in __builtin_nanf __builtin_nan __builtin_nans __builtin_nansf \
         __builtin_huge_val __builtin_huge_valf __builtin_huge_vall \
         __builtin_inf __builtin_inff __builtin_fabs __builtin_sqrt \
         __builtin_copysign __builtin_fmin __builtin_fmax __builtin_isfinite; do
    cmp_ "$b" "double f(void) { return $b; }" 
done

echo
echo "### K. remaining Annex A productions (attribute placement)"
ok "attribute before a struct member declaration" <<'EOF'
struct S { [[maybe_unused]] int a; };
int main(void) { struct S s = {1}; return s.a - 1; }
EOF
ok "attribute on a case label" <<'EOF'
int f(int c) { switch (c) { [[maybe_unused]] case 1: return 1; default: return 0; } }
int main(void) { return f(1) - 1; }
EOF
# clang rejects this ("'maybe_unused' attribute cannot be applied to a
# statement"), gcc only warns; cxx matches clang, message included.
rej "attribute on an expression statement" <<'EOF'
int main(void) { [[maybe_unused]] 1 + 1; return 0; }
EOF
ok "attribute on a function definition" <<'EOF'
[[maybe_unused]] int f(void) { return 0; }
int main(void) { return f(); }
EOF
ok "attribute on a pointer declarator" <<'EOF'
int * [[maybe_unused]] p;
int main(void) { return p == 0 ? 0 : 1; }
EOF
ok "attribute on an enumerator" <<'EOF'
enum E { A [[maybe_unused]] = 1 };
int main(void) { return A - 1; }
EOF
ok "attribute on a struct tag" <<'EOF'
struct [[maybe_unused]] S { int a; };
int main(void) { return 0; }
EOF
ok "attribute on a direct declarator identifier" <<'EOF'
int x [[maybe_unused]];
int main(void) { return x; }
EOF
# gcc: "alignment specified for parameter 'x'"; clang: "'alignas'
# attribute cannot be applied to a function parameter"; cxx has its own
# wording. All three reject it.
rej "alignas in a parameter declaration" <<'EOF'
int f(alignas(8) int x) { return x; }
int main(void) { return f(1) - 1; }
EOF
ok "static and type qualifiers in an array parameter" <<'EOF'
int f(int a[static 4]) { return a[3]; }
int f2(int a[const 4]) { return a[3]; }
int f3(int a[static const 4]) { return a[3]; }
int main(void) { int v[4] = {1,2,3,4}; return (f(v) == 4 && f2(v) == 4 && f3(v) == 4) ? 0 : 1; }
EOF

echo
echo "c2ycov3: $n_pass passed, $n_fail failed"
