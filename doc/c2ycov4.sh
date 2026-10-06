#!/bin/bash
# Wave 4: math builtins with real calls, plus the two attribute questions.
C=/home/memory/cxx/cxx
CC=clang
t=`mktemp -d /tmp/cxx-c2y4-XXXXXX`
trap 'rm -rf $t' EXIT

pair() {  # name  -- reads a translation unit; compiles with both, no run
    n=$1; cat > "$t/$n.c"
    printf '%-40s cxx:' "$n"
    if $C -w -S -emit-llvm -o /dev/null "$t/$n.c" > "$t/e" 2>&1; then printf ' OK  '; else printf ' REJ '; fi
    printf ' clang:'
    if $CC -std=c2y -w -S -emit-llvm -o /dev/null "$t/$n.c" > "$t/e2" 2>&1; then printf ' OK\n'; else printf ' REJ\n'; fi
    if [ -s "$t/e" ]; then sed 's/^/      cxx:   /' "$t/e" | head -2; fi
    if [ -s "$t/e2" ]; then sed 's/^/      clang: /' "$t/e2" | head -2; fi
}

echo "### L. math builtins glibc's <math.h> expands to (real calls)"
pair nanf        <<'EOF'
double f(void) { return __builtin_nanf(""); }
EOF
pair nan         <<'EOF'
double f(void) { return __builtin_nan(""); }
EOF
pair nans        <<'EOF'
double f(void) { return __builtin_nans(""); }
EOF
pair huge_val    <<'EOF'
double f(void) { return __builtin_huge_val(); }
EOF
pair huge_valf   <<'EOF'
float f(void) { return __builtin_huge_valf(); }
EOF
pair huge_vall   <<'EOF'
long double f(void) { return __builtin_huge_vall(); }
EOF
pair inf         <<'EOF'
double f(void) { return __builtin_inf(); }
EOF
pair fpclassify  <<'EOF'
int f(double x) { return __builtin_fpclassify(0, 1, 2, 3, 4, x); }
EOF
pair isgreater   <<'EOF'
int f(double a, double b) { return __builtin_isgreater(a, b); }
EOF
pair isunordered <<'EOF'
int f(double a, double b) { return __builtin_isunordered(a, b); }
EOF
pair signbit     <<'EOF'
int f(double x) { return __builtin_signbit(x); }
EOF
pair fabs        <<'EOF'
double f(double x) { return __builtin_fabs(x); }
EOF
pair sqrt        <<'EOF'
double f(double x) { return __builtin_sqrt(x); }
EOF
pair strtod_ok   <<'EOF'
int f(void) { return 1; }
EOF

echo
echo "### M. other builtins real code and glibc headers rely on"
pair expect      <<'EOF'
int f(int x) { if (__builtin_expect(x, 1)) return 1; return 0; }
EOF
pair unreachable <<'EOF'
int f(int x) { if (x) return 1; __builtin_unreachable(); }
EOF
pair trap        <<'EOF'
int f(int x) { if (x) __builtin_trap(); return 0; }
EOF
pair offsetof_b  <<'EOF'
struct S { int a; char b; };
unsigned long f(void) { return __builtin_offsetof(struct S, b); }
EOF
pair object_size <<'EOF'
unsigned long f(const char *p) { return __builtin_object_size(p, 0); }
EOF
pair memcpy_b    <<'EOF'
void f(void *d, const void *s) { __builtin_memcpy(d, s, 4); }
EOF
pair strlen_b    <<'EOF'
unsigned long f(const char *s) { return __builtin_strlen(s); }
EOF
pair abs_b       <<'EOF'
int f(int x) { return __builtin_abs(x); }
EOF
pair launder     <<'EOF'
int *f(int *p) { return __builtin_launder(p); }
EOF
pair assume      <<'EOF'
void f(int x) { __builtin_assume(x > 0); }
EOF

echo
echo "### N. attribute applicability questions"
pair attr_expr   <<'EOF'
int main(void) { [[maybe_unused]] 1 + 1; return 0; }
EOF
pair attr_expr2  <<'EOF'
int main(void) { [[gnu::unused]] 1 + 1; return 0; }
EOF
pair alignas_parm <<'EOF'
int f(alignas(8) int x) { return x; }
int main(void) { return f(1) - 1; }
EOF

echo
echo "### O. driver: -lm and friends"
cat > "$t/lnk.c" <<'EOF'
#include <math.h>
int main(void) { return sqrt(4.0) == 2.0 ? 0 : 1; }
EOF
for f in "-lm" "-L/usr/lib/x86_64-linux-gnu -lm" "-pthread" "-O2"; do
    printf 'cxx %-34s : ' "$f"
    if $C -w $f -o "$t/lnk" "$t/lnk.c" > "$t/e" 2>&1 && "$t/lnk"; then echo "links and runs"; else
        echo "fails: $(head -1 $t/e)"; fi
done
printf 'clang -lm                              : '
if $CC -std=c2y -w -lm -o "$t/lnkc" "$t/lnk.c" >/dev/null 2>&1 && "$t/lnkc"; then echo "links and runs"; else echo fails; fi

echo
echo "### P. <math.h> macro inventory (each used on its own)"
for m in "NAN" "INFINITY" "HUGE_VAL" "HUGE_VALF" "HUGE_VALL" "NANF" "NANL" \
         "FP_INFINITE" "FP_NAN" "FP_NORMAL" "FP_SUBNORMAL" "FP_ZERO" \
         "MATH_ERRNO" "MATH_ERREXCEPT" "math_errhandling" "signgam"; do
    cat > "$t/m.c" <<EOF
#include <math.h>
double v = (double)($m);
int main(void) { return 0; }
EOF
    printf '%-16s cxx:' "$m"
    if $C -w -S -o /dev/null "$t/m.c" > "$t/e" 2>&1; then printf ' OK '; else printf ' REJ'; fi
    printf ' clang:'
    if $CC -std=c2y -w -S -o /dev/null "$t/m.c" >/dev/null 2>&1; then printf ' OK\n'; else printf ' REJ\n'; fi
done
