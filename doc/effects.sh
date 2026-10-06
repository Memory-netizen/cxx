#!/bin/bash
# Side-effect evaluation counts.
#
# Folding and AST rewriting are exactly where an operand can quietly stop
# being evaluated once -- or start being evaluated twice. This probe counts
# the calls each construct makes and diffs cxx against clang, so a change in
# either direction shows up as a difference rather than as a wrong answer.
#
# The two comparison macros that are not one C operator (islessgreater,
# isunordered) are in here because that is why they were implemented in
# irgen rather than as a rewrite: 7.12.18.6 defines islessgreater as
# (x) < (y) || (x) > (y), which would put each operand in two positions.
#
# Usage: bash doc/effects.sh
C=${CXX:-/home/memory/cxx/cxx}
CC=${CLANG:-clang}
t=`mktemp -d /tmp/cxx-eff-XXXXXX`
trap 'rm -rf $t' EXIT

cat > "$t/a.c" <<'EOF'
#include <stdio.h>
#include <math.h>

static int n;
static int    fi(void) { n++; return 2; }
static double fd(void) { n++; return 2.0; }
static float  ff(void) { n++; return 2.0f; }

/* Evaluates e after resetting the counter, then reports the count. */
#define CNT(name, e) do { n = 0; (void)(e); printf("%-34s %d\n", name, n); } while (0)

int main(void) {
    /* --- 7.12.18: every operand exactly once ------------------------ */
    CNT("isgreater(fd(),1)",    isgreater(fd(), 1.0));
    CNT("isgreaterequal(fd(),1)", isgreaterequal(fd(), 1.0));
    CNT("isless(fd(),1)",       isless(fd(), 1.0));
    CNT("islessequal(fd(),1)",  islessequal(fd(), 1.0));
    CNT("islessgreater(fd(),1)", islessgreater(fd(), 1.0));
    CNT("isunordered(fd(),1)",  isunordered(fd(), 1.0));
    CNT("isgreater(fd(),fd())", isgreater(fd(), fd()));
    CNT("islessgreater(fd(),fd())", islessgreater(fd(), fd()));
    CNT("isunordered(fd(),fd())", isunordered(fd(), fd()));
    CNT("isgreater(ff(),1.f)",  isgreater(ff(), 1.0f));
    CNT("islessgreater(ff(),1.f)", islessgreater(ff(), 1.0f));
    CNT("isgreater(ff(),1.0)",  isgreater(ff(), 1.0));
    CNT("isunordered(ff(),ff())", isunordered(ff(), ff()));

    /* --- && / || : the folded forms and the live ones --------------- */
    CNT("1 && fi()",            1 && fi());
    CNT("0 && fi()",            0 && fi());
    CNT("0 || fi()",            0 || fi());
    CNT("1 || fi()",            1 || fi());
    CNT("fi() && 1",            fi() && 1);
    CNT("fi() || 0",            fi() || 0);
    CNT("1 && fi() && fi()",    1 && fi() && fi());
    CNT("0 && fi() && fi()",    0 && fi() && fi());
    CNT("0 || fi() || fi()",    0 || fi() || fi());
    CNT("1 || fi() || fi()",    1 || fi() || fi());

    /* --- short circuit with a right operand that opens blocks ------- */
    CNT("fi() && (fi()?1:0)",   fi() && (fi() ? 1 : 0));
    CNT("0 && (fi()?1:0)",      0 && (fi() ? 1 : 0));
    CNT("1 || (fi()?1:0)",      1 || (fi() ? 1 : 0));
    CNT("0 || (fi()?1:0)",      0 || (fi() ? 1 : 0));
    CNT("fi() && (fi() && fi())", fi() && (fi() && fi()));

    /* --- ?: --------------------------------------------------------- */
    CNT("1 ? fi() : fi()",      1 ? fi() : fi());
    CNT("0 ? fi() : fi()",      0 ? fi() : fi());
    CNT("fi() ? fi() : fi()",   fi() ? fi() : fi());

    /* --- unevaluated operands --------------------------------------- */
    CNT("sizeof(fi())",         sizeof(fi()));
    CNT("_Alignof(typeof(fi()))", _Alignof(typeof(fi())));
    {
        int a[4];
        n = 0; int c = _Countof(a); printf("%-34s %d\n", "_Countof(a)", n);
        (void)c;
    }
    CNT("_Generic(fi(),int:1)", _Generic(fi(), int: 1, default: 0));
    {
        n = 0; typeof(fi()) x = 0; printf("%-34s %d\n", "typeof(fi()) x = 0", n); (void)x;
    }
    CNT("__builtin_constant_p(fi())", __builtin_constant_p(fi()));
    return 0;
}
EOF

$C -w -o "$t/cxx" "$t/a.c" > "$t/err" 2>&1
if [ ! -x "$t/cxx" ]; then echo "cxx failed to build the probe:"; head -5 "$t/err"; exit 2; fi
$CC -std=c2y -w -o "$t/cl" "$t/a.c" > "$t/err2" 2>&1
if [ ! -x "$t/cl" ]; then echo "clang failed to build the probe:"; head -5 "$t/err2"; exit 2; fi

"$t/cxx" > "$t/o1"; "$t/cl" > "$t/o2"

if diff -u "$t/o2" "$t/o1" > "$t/d"; then
    echo "evaluation counts IDENTICAL to clang ($(wc -l < "$t/o1") cases)"
    cat "$t/o1"
else
    echo "DIFFERENCES (clang expected, cxx actual):"
    cat "$t/d"
    exit 1
fi
