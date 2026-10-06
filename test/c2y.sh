#!/bin/bash
# C2y (ISO/IEC 9899:202y, N3685) regression net.
#
# Why this file exists: the C2y-driven round that produced
# doc/n3685-conformance.md found three real defects -- a && / || fold that
# answered the opposite of the standard, a short-circuit right operand that
# emitted a module LLVM refused, and a -lm that reached the linker ahead of
# the object files -- and every one of them passed `make test` untouched.
# The probes that caught them lived in doc/ and were run by hand. This net
# is the part of that work that must run on every build.
#
# Scope: features of N3685 that no other suite exercises. The exhaustive
# versions of the logical-operator cases live in test/fold.c (values) and
# test/control.c (nested right operands); header and driver behaviour lives
# in test/conformance.sh. What is checked here is the C2y surface itself,
# plus one end-to-end assertion per defect class so the class cannot come
# back through a different expression.
#
# `gap` marks a feature that is known to be missing. It is reported but
# never fatal, and it fails loudly the moment the feature lands, so the
# entry gets promoted to ok/run then. Those are the open items in
# doc/cxx-c2y-plan.md.
#
# Usage: bash test/c2y.sh ./cxx
compiler=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
if [ ! -x "$compiler" ]; then
    echo "c2y: no compiler at '$1'" >&2
    exit 2
fi

tmp=`mktemp -d /tmp/cxx-c2y-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

# Counter names avoid `pass`: bash keeps $? in a variable called `pass`, so
# `pass=$((pass+1))` is overwritten by the next command. The source is read
# from a file rather than through a pipeline, for the same reason
# test/conformance.sh does: a pipeline would run the checker in a subshell
# and discard every counter increment.
n_pass=0
n_fail=0
n_gaps=0

compile() {
    cat > "$tmp/t.c"
    "$compiler" -w -S -emit-llvm -o /dev/null "$tmp/t.c" > "$tmp/log" 2>&1
}

# run <name> -- must compile and exit 0
runit() {
    cat > "$tmp/t.c"
    if "$compiler" -w -o "$tmp/t" "$tmp/t.c" > "$tmp/log" 2>&1 && "$tmp/t"; then
        echo "testing $1 ... passed"
        n_pass=$((n_pass + 1))
    else
        echo "testing $1 ... FAILED"
        sed 's/^/    /' "$tmp/log" | head -4
        n_fail=$((n_fail + 1))
    fi
}

# ok <name> -- must compile
ok() {
    if compile; then
        echo "testing $1 ... passed"
        n_pass=$((n_pass + 1))
    else
        echo "testing $1 ... FAILED"
        sed 's/^/    /' "$tmp/log" | head -4
        n_fail=$((n_fail + 1))
    fi
}

# bad <name> -- must be rejected
bad() {
    if compile; then
        echo "testing $1 ... FAILED (expected a diagnostic)"
        n_fail=$((n_fail + 1))
    else
        echo "testing $1 ... passed"
        n_pass=$((n_pass + 1))
    fi
}

# gap <name> -- a known missing feature; never fatal, but loud once it lands
gap() {
    if compile; then
        echo "testing $1 ... GAP CLOSED (promote this entry)"
        n_fail=$((n_fail + 1))
    else
        echo "testing $1 ... known gap"
        n_gaps=$((n_gaps + 1))
    fi
}

# rungap <name> -- a known gap that is only visible at run time, because a
# wrong constant still compiles. Same contract as gap().
rungap() {
    cat > "$tmp/t.c"
    if "$compiler" -w -o "$tmp/t" "$tmp/t.c" > "$tmp/log" 2>&1 && "$tmp/t"; then
        echo "testing $1 ... GAP CLOSED (promote this entry)"
        n_fail=$((n_fail + 1))
    else
        echo "testing $1 ... known gap"
        n_gaps=$((n_gaps + 1))
    fi
}

# --- the three defect classes this net was created for ---------------

# A constant left operand used to make the folder answer `!x` instead of
# `x != 0`, so `1 && x` was true exactly when x was false. The exhaustive
# value table is in test/fold.c; this is the end-to-end property.
runit "constant-left && / || yield the operand's truth value" <<'EOF'
int main(void) {
    int one = 1, zero = 0;
    double half = 0.5, dzero = 0.0;
    int *live = &one, *null = 0;
    if (!(1 && one)) return 1;
    if (1 && zero) return 2;
    if (!(0 || one)) return 3;
    if (0 || zero) return 4;
    if (!(1 && half)) return 5;
    if (1 && dzero) return 6;
    if (!(1 && live)) return 7;
    if (1 && null) return 8;
    if (!(1 && one && 1)) return 9;
    if (1 && zero && 1) return 10;
    return 0;
}
EOF

# A right operand that is itself a ?: / && / || opens blocks of its own, so
# the merge PHI must name the block it actually finished in. Getting that
# wrong emitted a module LLVM rejected outright. test/control.c holds the
# wider table; this is the smallest shape of each kind.
runit "short-circuit right operand that opens blocks" <<'EOF'
int main(void) {
    if (1 && (0 ? 0 : 1) != 1) return 1;
    if (0 && (1 ? 0 : 1) != 0) return 2;
    if (0 || (0 ? 0 : 1) != 1) return 3;
    if (1 && (0 && 1) != 0) return 4;
    if (0 || (0 || 1) != 1) return 5;
    if (1 && (1 ? 1 : 0) && (0 || 1) != 1) return 6;
    return 0;
}
EOF

# --- 6.4 lexical elements ---------------------------------------------

runit "N3353 prefixed octal literal" <<'EOF'
int main(void) { return (0o17 == 15 && 0O777 == 511 && 017 == 15) ? 0 : 1; }
EOF

runit "N3353 \\o{...} octal escape" <<'EOF'
int main(void) { return ('\o{101}' == 'A' && '\o{0}' == 0) ? 0 : 1; }
EOF

runit "N3192 \\x{...} braced hex escape" <<'EOF'
int main(void) { return ('\x{41}' == 'A' && "\x{41}\x{42}"[1] == 'B') ? 0 : 1; }
EOF

runit "6.4.4 \\u{...} universal character name" <<'EOF'
int main(void) { return ('\u{41}' == 'A' && sizeof("\u{1F600}") == 5) ? 0 : 1; }
EOF

runit "6.4.5.2 wb / uwb bit-precise integer suffix" <<'EOF'
int main(void) {
    if (!(1wb == 1 && 1WB == 1 && 1uwb == 1u && 1UWB == 1u)) return 1;
    if (sizeof(2025wb) != 2) return 2;          /* 12 bits -> 2 bytes */
    unsigned _BitInt(8) u = 255uwb;
    return u == 255 ? 0 : 3;
}
EOF

runit "6.4.5.2 digit separators" <<'EOF'
int main(void) { return (1'000'000 == 1000000 && 0xFF'FF == 0xFFFF && 0b1010'1010 == 170) ? 0 : 1; }
EOF

runit "6.4.5.6 true / false / nullptr are literals" <<'EOF'
#include <stddef.h>
int main(void) { nullptr_t p = nullptr; return (true && !false && p == nullptr) ? 0 : 1; }
EOF

runit "H.5.1 _FloatN types and their literal suffixes" <<'EOF'
int main(void) {
    _Float16 a = 1.5f16; _Float32 b = 2.5f32; _Float64 c = 3.5f64; _Float128 d = 4.5f128;
    return (a == 1.5f16 && b == 2.5f32 && c == 3.5f64 && d == 4.5f128) ? 0 : 1;
}
EOF

runit "6.4.5.5 u8 / u / U / L literal prefixes" <<'EOF'
#include <uchar.h>
int main(void) {
    char8_t c = u8'x';
    return (c == 'x' && sizeof(u8"a") == 2 && sizeof(u"a") == 4 &&
            sizeof(U"a") == 8 && sizeof(L"a") == sizeof(wchar_t) * 2) ? 0 : 1;
}
EOF

# --- 6.5 expressions --------------------------------------------------

runit "N3370 case ranges" <<'EOF'
int f(int c) { switch (c) { case 1 ... 9: return 1; default: return 0; } }
int main(void) { return (f(0) == 0 && f(5) == 1 && f(9) == 1 && f(10) == 0) ? 0 : 1; }
EOF

# 6.6.2 requires the diagnostic, but gcc and clang only warn, so the
# program must still compile and run -- and a switch whose only case range
# is empty must still emit a valid case list. cxx used to accept this and
# then print `switch i32 %x, label %blk` with no brackets, which LLVM
# rejects ("expected '[' with switch table"); it also passed unnoticed
# because compiling only to IR stopped before the assembler.
cat > "$tmp/range.c" <<'EOF'
int f(int c) { switch (c) { case 9 ... 1: return 1; default: return 0; } }
int main(void) { return f(5); }
EOF
if "$compiler" -w -o "$tmp/range" "$tmp/range.c" > "$tmp/log" 2>&1 && "$tmp/range"; then
    echo "testing empty case range compiles, links and runs ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing empty case range compiles, links and runs ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi
if "$compiler" -S -o /dev/null "$tmp/range.c" 2>&1 | grep -q 'empty case range'; then
    echo "testing empty case range is diagnosed ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing empty case range is diagnosed ... FAILED"
    n_fail=$((n_fail + 1))
fi

runit "N3469 _Countof of an object and of a type name" <<'EOF'
int main(void) { int a[7]; return (_Countof(a) == 7 && _Countof(int[10]) == 10) ? 0 : 1; }
EOF

bad "_Lengthof is not a C2y keyword" <<'EOF'
int main(void) { int a[3]; return _Lengthof(a) == 3 ? 0 : 1; }
EOF

runit "N3260 _Generic with a type operand" <<'EOF'
int main(void) { return _Generic(int, int: 1, default: 0) == 1 ? 0 : 1; }
EOF

runit "6.5.2.1 an array operand of _Generic decays" <<'EOF'
int main(void) { int a[3]; return _Generic(a, int[3]: 1, int *: 2, default: 0) == 2 ? 0 : 1; }
EOF

runit "N3273 alignof of an incomplete array type" <<'EOF'
int main(void) { return _Alignof(int[]) == _Alignof(int) ? 0 : 1; }
EOF

runit "N3517 one-past-the-end and non-lvalue array subscripts" <<'EOF'
struct S { int a[3]; };
struct S mk(void) { struct S s = {{1, 2, 3}}; return s; }
int main(void) {
    int a[3];
    int *p = &a[3];
    if (p != a + 3) return 1;
    if (mk().a[1] != 2) return 2;
    if (((struct S){{4, 5, 6}}).a[2] != 6) return 3;
    if (sizeof(((struct S){{1, 2, 3}}).a) != 12) return 4;
    return 0;
}
EOF

runit "N3323 bool from a floating value" <<'EOF'
int main(void) { bool b = 0.5; bool c = 0.0; return (b && !c) ? 0 : 1; }
EOF

# --- 6.7 / 6.8 declarations and statements ----------------------------

runit "6.7.3.3 enum with a fixed underlying type" <<'EOF'
enum E : unsigned char { A = 200 };
enum F : long long { B = 5000000000LL };
int main(void) { return (sizeof(enum E) == 1 && A == 200 && B == 5000000000LL) ? 0 : 1; }
EOF

ok "6.7.3.2 alignas in a specifier-qualifier-list" <<'EOF'
enum E : alignas(2) unsigned char { A = 1 };
struct S { alignas(16) int a; };
int main(void) { return _Alignof(struct S) == 16 ? 0 : 1; }
EOF

bad "N3532 member access through an incomplete struct" <<'EOF'
struct S;
int f(struct S *p) { return p->a; }
EOF

runit "6.7.3.5 _Atomic ( type-name ) and N3312 relaxed alignment" <<'EOF'
struct S { int a; int b; };
int main(void) {
    _Atomic(int) v = 3;
    return (v == 3 && _Alignof(_Atomic int) == _Alignof(int) &&
            _Alignof(_Atomic struct S) >= _Alignof(struct S)) ? 0 : 1;
}
EOF

ok "6.7.13.2 attribute-declaration and vendor-prefixed attributes" <<'EOF'
[[maybe_unused]];
struct [[gnu::aligned(8)]] T { int a; };
[[gnu::aligned(8)]] int x;
int main(void) { return 0; }
EOF

runit "N3356 if / switch declarations" <<'EOF'
int f(void) { return 3; }
int main(void) {
    if (int x = f(); x != 3) return 1;
    switch (int y = f(); y) { case 3: break; default: return 2; }
    return 0;
}
EOF

runit "N3355 named break and continue" <<'EOF'
int main(void) {
    int n = 0;
    outer: for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) { n++; if (j == 1) continue outer; }
    if (n != 6) return 1;
    n = 0;
    done: for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) { n++; if (i == 1) break done; }
    return n == 4 ? 0 : 1;
}
EOF

bad "reject break to a non-loop label" <<'EOF'
int main(void) { int n = 0; for (int i = 0; i < 2; ++i) blk: { if (i == 1) break blk; n++; } return n; }
EOF

runit "N3451 empty initializer and anonymous member initialization" <<'EOF'
struct S { int a; union { int b; float c; }; };
int main(void) { struct S s = { }; struct S t = { 1, { .b = 2 } }; return (s.a == 0 && t.b == 2) ? 0 : 1; }
EOF

ok "6.7.12 static_assert with and without a message" <<'EOF'
static_assert(1);
static_assert(sizeof(int) == 4, "int is 4 bytes");
struct S { int a; static_assert(sizeof(int) == 4); };
int main(void) { return 0; }
EOF

runit "N3544 register classification" <<'EOF'
int main(void) { register int a = 1; register int b[2] = {1, 2}; return (a + b[1] == 3) ? 0 : 1; }
EOF

bad "reject the address of a register object" <<'EOF'
int main(void) { register int a = 1; int *p = &a; return *p; }
EOF

runit "N3563 nullptr_t representation and conversions" <<'EOF'
#include <stddef.h>
int main(void) {
    nullptr_t a = nullptr, b = nullptr;
    void *p = a;
    int *ip = nullptr;
    if (a != b || p != 0 || ip != 0) return 1;
    if (a || !(!a)) return 2;
    if (_Generic(nullptr, nullptr_t: 1, void *: 2, default: 0) != 1) return 3;
    return 0;
}
EOF

runit "N3623 the specified forms of main" <<'EOF'
int main(void) { return 0; }
EOF

ok "N3623 int main(int, char **)" <<'EOF'
int main(int argc, char **argv) { return (argc > 0 && argv) ? 0 : 1; }
EOF

runit "a return from main falls off the end as 0" <<'EOF'
int main(void) { }
EOF

# --- 6.6 constant expressions (N3447 / N3459 / N3558) -----------------

runit "constant expressions over the C2y operand set" <<'EOF'
enum { E = 5 };
int a[sizeof(int) == 4 ? 1 : -1];
int b[_Alignof(double) == 8 ? 1 : -1];
int c[_Countof("abcd") == 5 ? 1 : -1];
static_assert((int)(3.9) == 3);
static_assert((E + 1) * 2 == 12);
static_assert((_BitInt(65))1 == 1);
static_assert(sizeof(_BitInt(65)) == 16);
static_assert((_Float128)1.0 == 1.0);
int g[4] = {1, 2, 3, 4};
int *p = &g[2];
const int k[3] = {7, 8, 9};
int t1 = "ab"[1];
int t2 = k[2];
static_assert(sizeof(a) == 4 && sizeof(b) == 4 && sizeof(c) == 4);
int main(void) { return (*p == 3 && t1 == 98 && t2 == 9) ? 0 : 1; }
EOF

bad "static_assert(0) fails at translation time" <<'EOF'
static_assert(0, "must fail");
int main(void) { return 0; }
EOF

# --- 6.10 preprocessing -----------------------------------------------

ok "6.10.1 #elifdef and #elifndef" <<'EOF'
#define A 1
#if 0
#elifdef A
int ok = 1;
#elifndef B
int bad = 1;
#endif
int main(void) { return ok - 1; }
EOF

runit "N3457 __COUNTER__" <<'EOF'
int a = __COUNTER__;
int b = __COUNTER__;
int main(void) { return (b == a + 1) ? 0 : 1; }
EOF

runit "N3505 digit separators inside a #if expression" <<'EOF'
#if 1'000'000 == 1000000
int ok = 1;
#else
int ok = 0;
#endif
int main(void) { return ok - 1; }
EOF

ok "6.10.1 __has_include / __has_builtin" <<'EOF'
#if !__has_include(<stddef.h>)
#error no stddef
#endif
#if !__has_builtin(__builtin_alloca)
#error has_builtin
#endif
int main(void) { return 0; }
EOF

ok "6.7.13.3 __has_c_attribute" <<'EOF'
#if __has_c_attribute(deprecated) != 201904L
#error has_c_attribute
#endif
int main(void) { return 0; }
EOF

runit "6.10.5.2 __VA_OPT__" <<'EOF'
#define M(...) f(0 __VA_OPT__(,) __VA_ARGS__)
int f(int a, int b, int c) { return a + b + c; }
int main(void) { return M(1, 2) == 3 ? 0 : 1; }
EOF

printf 'ABCD' > "$tmp/emb.bin"
runit "#embed with limit / prefix / suffix" <<'EOF'
static const unsigned char d[] = {
#embed "emb.bin" limit(2) prefix(1 + 0,) suffix(+0)
};
int main(void) { return d[0] == 1 ? 0 : 1; }
EOF

runit "#embed __has_embed and the three result constants" <<'EOF'
#if __has_embed("emb.bin" limit(2)) != __STDC_EMBED_FOUND__
#error found
#endif
#if __has_embed("nope.bin") != __STDC_EMBED_NOT_FOUND__
#error not_found
#endif
#if __STDC_EMBED_FOUND__ != 1 || __STDC_EMBED_EMPTY__ != 2 || __STDC_EMBED_NOT_FOUND__ != 0
#error constants
#endif
int main(void) { return 0; }
EOF

ok "6.10.8 #pragma STDC FENV_ROUND with a C2y direction" <<'EOF'
#pragma STDC FENV_ACCESS ON
#pragma STDC FENV_ROUND FE_TONEARESTFROMZERO
#pragma STDC FENV_ROUND FE_DYNAMIC
#pragma STDC FP_CONTRACT DEFAULT
int main(void) { return 0; }
EOF

# --- side-effect evaluation counts ------------------------------------

# Folding and AST rewriting are where an operand quietly stops being
# evaluated once -- or starts being evaluated twice -- and a wrong count is
# invisible in the result: `0 && f()` that still calls f(), or an
# islessgreater that calls its operand twice, both answer correctly. The
# expected counts below are the ones clang produces; doc/effects.sh diffs
# the two directly over 39 cases and prints the table.
runit "side-effect evaluation counts" <<'EOF'
#include <math.h>
static int n;
static int fi(void) { n++; return 2; }
static double fd(void) { n++; return 2.0; }
static float ff(void) { n++; return 2.0f; }
#define CNT(e) (n = 0, (void)(e), n)
int main(void) {
    /* 7.12.18: every operand of every comparison macro exactly once */
    if (CNT(isgreater(fd(), 1.0)) != 1) return 1;
    if (CNT(isgreaterequal(fd(), 1.0)) != 1) return 2;
    if (CNT(isless(fd(), 1.0)) != 1) return 3;
    if (CNT(islessequal(fd(), 1.0)) != 1) return 4;
    if (CNT(islessgreater(fd(), 1.0)) != 1) return 5;
    if (CNT(isunordered(fd(), 1.0)) != 1) return 6;
    if (CNT(islessgreater(fd(), fd())) != 2) return 7;
    if (CNT(isunordered(fd(), fd())) != 2) return 8;
    if (CNT(islessgreater(ff(), 1.0f)) != 1) return 9;
    /* && and ||: the live side runs, the folded-away side must not */
    if (CNT(1 && fi()) != 1) return 10;
    if (CNT(0 && fi()) != 0) return 11;
    if (CNT(0 || fi()) != 1) return 12;
    if (CNT(1 || fi()) != 0) return 13;
    if (CNT(fi() && 1) != 1) return 14;
    if (CNT(1 && fi() && fi()) != 2) return 15;
    /* a right operand that opens blocks still short-circuits */
    if (CNT(fi() && (fi() ? 1 : 0)) != 2) return 16;
    if (CNT(fi() && (fi() && fi())) != 3) return 17;
    if (CNT(0 && (fi() ? 1 : 0)) != 0) return 18;
    if (CNT(1 || (fi() ? 1 : 0)) != 0) return 19;
    /* ?: evaluates one arm */
    if (CNT(1 ? fi() : fi()) != 1) return 20;
    if (CNT(0 ? fi() : fi()) != 1) return 21;
    if (CNT(fi() ? fi() : fi()) != 2) return 22;
    /* unevaluated operands */
    if (CNT(sizeof(fi())) != 0) return 23;
    if (CNT(_Alignof(typeof(fi()))) != 0) return 24;
    if (CNT(_Generic(fi(), int: 1, default: 0)) != 0) return 25;
    if (CNT(__builtin_constant_p(fi())) != 0) return 26;
    {
        int a[4];
        if (CNT(_Countof(a)) != 0) return 27;
    }
    {
        typeof(fi()) x = 0;
        (void)x;
        if (n != 0) return 28;
    }
    return 0;
}
EOF

# P5 (landed): _Float16/32/64/128 are keywords (C23 H.5.1), and H.5.1 gates
# exactly that on __STDC_IEC_60559_TYPES__. cxx implements the Annex H types
# but never declared the macro, so the keyword-ness rested on nothing -- the
# GNU compatibility macros happened to keep glibc from writing its own
# `typedef float _Float32;` instead. The macros that must stay absent are
# checked too: not defining __STDC_IEC_60559_DFP__ is how the standard says
# decimal floating types are unsupported (there is no
# __STDC_NO_DECIMAL_FLOAT__ in C23 or C2y).
ok "P5 IEEE feature macros match the implementation" <<'EOF'
#if !defined(__STDC_IEC_60559_TYPES__)
#error __STDC_IEC_60559_TYPES__ must be defined: _FloatN are keywords (H.5.1)
#endif
#ifdef __STDC_IEC_60559_DFP__
#error decimal floating types are not implemented
#endif
#ifdef __STDC_IEC_60559_COMPLEX__
#error complex types are not implemented
#endif
#ifndef __STDC_NO_COMPLEX__
#error complex types are not implemented, so __STDC_NO_COMPLEX__ is required
#endif
_Float32 f = 1.0f32;
int main(void) { return f == 1.0f32 ? 0 : 1; }
EOF

# P6 (landed): -pthread is one switch on two levels -- the thread library
# for the linker and _REENTRANT for the preprocessor. Without it every
# threaded program failed with "unknown argument", which is the one flag
# every build system passes.
cat > "$tmp/thr.c" <<'EOF'
#include <threads.h>
#ifndef _REENTRANT
#error -pthread must also define _REENTRANT
#endif
static int work(void *arg) { return *(int *)arg + 1; }
int main(void) {
    int n = 41;
    thrd_t t;
    if (thrd_create(&t, work, &n) != thrd_success) return 1;
    int r = 0;
    if (thrd_join(t, &r) != thrd_success) return 2;
    return r == 42 ? 0 : 3;
}
EOF
if "$compiler" -w -pthread -o "$tmp/thr" "$tmp/thr.c" > "$tmp/log" 2>&1 && "$tmp/thr"; then
    echo "testing -pthread links a threaded program ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -pthread links a threaded program ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# D4 (landed): -funsigned-char / -fsigned-char override plain char's
# signedness, which is implementation-defined (6.2.5) and otherwise fixed by
# the ABI -- signed on x86-64, unsigned on the ARM and RISC-V ABIs.
cat > "$tmp/char.c" <<'EOF'
int main(void) {
    char c = -1;
    signed char s = -1;
    if (s != -1) return 1;              /* signed char never changes */
    if ((unsigned char)255 != 255) return 2;
#if defined(EXPECT_SIGNED)
    if (c != -1) return 3;
    if (c >= 0) return 4;
#elif defined(EXPECT_UNSIGNED)
    if (c != 255) return 3;
    if (c < 0) return 4;
#endif
    return 0;
}
EOF
if "$compiler" -w -DEXPECT_SIGNED -fsigned-char -o "$tmp/char_s" "$tmp/char.c" > "$tmp/log" 2>&1 &&
    "$tmp/char_s" &&
    "$compiler" -w -DEXPECT_UNSIGNED -funsigned-char -o "$tmp/char_u" "$tmp/char.c" >> "$tmp/log" 2>&1 &&
    "$tmp/char_u"; then
    echo "testing -funsigned-char / -fsigned-char switch plain char ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -funsigned-char / -fsigned-char switch plain char ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# E3 must not report a local that is used. The flag that records a reference
# was once set on the wrong node, which made every local look unused; these
# are the shapes that revealed it -- a local passed as a call argument,
# reached through a member, and used as a subscript.
cat > "$tmp/used.c" <<'EOF'
#include <string.h>
struct S { char c; int i; };
static int f(int x) { return x; }
int main(void) {
    char buf[8];
    struct S s;
    int a = 1, c[3];
    int *p = &a;
    memset(buf, 0, sizeof buf);
    s.i = 2;
    c[0] = 3;
    *p = 4;
    return f(a) + buf[0] + s.i + c[0] + a;
}
EOF
if "$compiler" -Wunused-variable -S -o /dev/null "$tmp/used.c" 2>&1 | grep -q 'unused variable'; then
    echo "testing a used local is not reported as unused ... FAILED"
    "$compiler" -Wunused-variable -S -o /dev/null "$tmp/used.c" 2>&1 | grep 'unused variable' | sed 's/^/    /' | head -3
    n_fail=$((n_fail + 1))
else
    echo "testing a used local is not reported as unused ... passed"
    n_pass=$((n_pass + 1))
fi

cat > "$tmp/dead.c" <<'EOF'
int main(void) { int used = 1; int dead = 2; return used; }
EOF
if "$compiler" -Wunused-variable -S -o /dev/null "$tmp/dead.c" 2>&1 | grep -q "unused variable .*dead"; then
    echo "testing a genuinely unused local is reported ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a genuinely unused local is reported ... FAILED"
    n_fail=$((n_fail + 1))
fi

# -Wshift-count-negative / -Wshift-count-overflow are on by default in gcc
# and clang, and 6.5.7 makes both undefined. A negated constant is not an
# ND_NUM at this point -- fold_ast has not run -- so `x << -1` arrives as a
# negation of a literal and needs its own case.
cat > "$tmp/shift.c" <<'EOF'
int f1(int x) { return x << 33; }               /* overflow, literal   */
int f2(int x) { return x << 31; }               /* fine                */
int f3(int x) { return x << -1; }               /* negative            */
int f4(long x) { return (int)(x << 64); }       /* overflow            */
int f5(long x) { return (int)(x << 63); }       /* fine                */
int f6(int x) { int n = 3; return x << n; }     /* not constant, fine  */
int f7(unsigned x) { return x << 32; }          /* overflow            */
int f8(int x) { return x >> 33; }               /* overflow            */
/* Counts that are only constant once folded: checking in the parser saw
   neither of these, because fold_ast runs after parsing. */
int f9(int x) { return x << (32 + 1); }         /* overflow, folded 33 */
int f10(int x) { return x << (1 << 5); }        /* overflow, folded 32 */
int f11(int x) { return x << (2 + 1); }         /* fine, folded 3      */
int f12(long x) { return (int)(x << (64 - 0)); }/* overflow, folded 64 */
EOF
ovf=$("$compiler" -S -o /dev/null "$tmp/shift.c" 2>&1 | grep -c 'shift count >= width')
neg=$("$compiler" -S -o /dev/null "$tmp/shift.c" 2>&1 | grep -c 'shift count is negative')
if [ "$ovf" -eq 7 ] && [ "$neg" -eq 1 ]; then
    echo "testing shift count out of range is diagnosed ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing shift count out of range is diagnosed ... FAILED (overflow $ovf/7, negative $neg/1)"
    n_fail=$((n_fail + 1))
fi

# The two groups are independent, and both fall to -w.
a=$("$compiler" -Wno-shift-count-overflow -S -o /dev/null "$tmp/shift.c" 2>&1 | grep -c 'shift count')
b=$("$compiler" -Wno-shift-count-negative -S -o /dev/null "$tmp/shift.c" 2>&1 | grep -c 'shift count')
c=$("$compiler" -w -S -o /dev/null "$tmp/shift.c" 2>&1 | grep -c 'shift count')
if [ "$a" -eq 1 ] && [ "$b" -eq 7 ] && [ "$c" -eq 0 ]; then
    echo "testing the shift groups are independent ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the shift groups are independent ... FAILED (no-overflow $a, no-negative $b, -w $c)"
    n_fail=$((n_fail + 1))
fi

# E2 (landed): -Wimplicit-fallthrough. A case or default label is entered
# either by the switch's own dispatch or by falling out of the statement
# before it, so the parser carries one bit -- "can control reach what
# follows" -- that every statement writes on its way out and every label
# reads and clears. The shapes below are the ones where the answer is not
# obvious: an endless loop with a break does fall out, an endless loop
# without one does not, a switch with a default whose every label leaves
# does not, and a call to a noreturn function never comes back. Each of
# those is a decision the bit could get wrong in a way that only shows up
# on the next label, which is why they share one file and one count.
cat > "$tmp/fall.c" <<'EOF'
_Noreturn void stop(void);
int f(int x, int y) {
    int n = 0;
    switch (x) {
    case 1: n++;                        /* falls through            */
    case 2: break;
    case 3: return 1;
    case 4: [[fallthrough]];
    case 5: while (1) {}                /* never leaves the loop    */
    case 6: for (;;) { if (y) break; }  /* the break does leave it  */
    case 7: stop();                     /* never returns            */
    case 8: switch (y) { default: return 2; }  /* every path leaves */
    case 9: n += y;                     /* falls through            */
    default: n--;
    }
    return n;
}
EOF
on=$("$compiler" -Wimplicit-fallthrough -S -o /dev/null "$tmp/fall.c" 2>&1 | grep -c 'unannotated fall-through')
off=$("$compiler" -S -o /dev/null "$tmp/fall.c" 2>&1 | grep -c 'unannotated fall-through')
wall=$("$compiler" -Wall -Wextra -S -o /dev/null "$tmp/fall.c" 2>&1 | grep -c 'unannotated fall-through')
no=$("$compiler" -Wno-implicit-fallthrough -S -o /dev/null "$tmp/fall.c" 2>&1 | grep -c 'unannotated fall-through')
if [ "$on" -eq 3 ] && [ "$off" -eq 0 ] && [ "$wall" -eq 0 ] && [ "$no" -eq 0 ]; then
    echo "testing -Wimplicit-fallthrough counts the falling cases ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -Wimplicit-fallthrough counts the falling cases ... FAILED (want 3, got $on; default $off, -Wall $wall, -Wno $no)"
    n_fail=$((n_fail + 1))
fi

# The label the warning names must be the one being fallen into, and
# [[fallthrough]] must silence exactly one fall. The annotation sits
# directly in front of the label it falls into.
cat > "$tmp/fall2.c" <<'EOF'
int g(int x, int y) {
    switch (x) {
    case 1: y = 1; [[fallthrough]];
    case 2: return y;
    case 3: y = 2;
    case 4: [[fallthrough]];
    case 5: return y;
    }
    return 0;
}
EOF
lines=$("$compiler" -Wimplicit-fallthrough -S -o /dev/null "$tmp/fall2.c" 2>&1 | grep 'unannotated' | sed 's/.*fall2\.c://; s/:.*//' | tr '\n' ' ')
if [ "$lines" = "6 " ]; then
    echo "testing the fall-through is reported at the label fallen into ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the fall-through is reported at the label fallen into ... FAILED (lines '$lines', want '6 ')"
    n_fail=$((n_fail + 1))
fi

# E2 second half (landed): the annotation must sit directly in front of the
# label it falls into -- 6.7.13.2p2 wants an empty statement, and both
# references diagnose the forms where that empty statement is followed by
# anything else. clang calls it an error and gcc a warning; cxx follows
# clang, wording included, so the program is not merely diagnosed but refused.
cat > "$tmp/fall3.c" <<'EOF'
int f(int x) {
    switch (x) {
    case 1: [[fallthrough]]; x = 1;
    case 2: return x;
    }
    return 0;
}
EOF
cat > "$tmp/fall4.c" <<'EOF'
int f(int x) {
    switch (x) {
    case 1: x = 1; [[fallthrough]];
    }
    return x;
}
EOF
cat > "$tmp/fall5.c" <<'EOF'
int f(int x) {
    switch (x) {
    case 1: [[fallthrough]];
    l: case 2: return x;
    }
    return 0;
}
EOF
mis=$("$compiler" -S -o /dev/null "$tmp/fall3.c" 2>&1 | grep -c 'does not directly precede switch label')
last=$("$compiler" -S -o /dev/null "$tmp/fall4.c" 2>&1 | grep -c 'does not directly precede switch label')
lbl=$("$compiler" -S -o /dev/null "$tmp/fall5.c" 2>&1 | grep -c 'does not directly precede switch label')
good=$("$compiler" -S -o /dev/null "$tmp/fall2.c" 2>&1 | grep -c 'does not directly precede switch label')
"$compiler" -S -o /dev/null "$tmp/fall3.c" >/dev/null 2>&1
status=$?
if [ "$mis" -eq 1 ] && [ "$last" -eq 1 ] && [ "$lbl" -eq 1 ] && [ "$good" -eq 0 ] && [ "$status" -ne 0 ]; then
    echo "testing a misplaced fallthrough annotation is refused ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a misplaced fallthrough annotation is refused ... FAILED (misplaced $mis, at end $last, label $lbl, well placed $good, exit $status)"
    n_fail=$((n_fail + 1))
fi

# E4 (landed): -Wunused-function / -Wunused-variable at file scope. The
# answer comes from a reference graph rooted in the definitions another
# translation unit can see, not from "was this name ever mentioned": the
# pair of static functions that only call each other is dead, and so is the
# object whose only reader is a dead function. clang drops the same set from
# the IR at -O0, and neither gcc nor clang says so about the pair.
cat > "$tmp/dead.c" <<'EOF'
static int never_called(void) { return 1; }
static int a(void);
static int b(void);
static int a(void) { return b(); }
static int b(void) { return a(); }
static int called(void) { return 0; }
static int only_dead(void) { return 2; }
static int kept;
static void dead(void) { kept = only_dead(); }
int main(void) { return called(); }
EOF
fns=$("$compiler" -S -o /dev/null "$tmp/dead.c" 2>&1 | grep -c 'unused function')
vars=$("$compiler" -S -o /dev/null "$tmp/dead.c" 2>&1 | grep -c 'unused variable')
live=$("$compiler" -w -S -emit-llvm -o - "$tmp/dead.c" 2>/dev/null | grep -c '^define')
if [ "$fns" -eq 5 ] && [ "$vars" -eq 1 ] && [ "$live" -eq 2 ]; then
    echo "testing dead statics are diagnosed and not emitted ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing dead statics are diagnosed and not emitted ... FAILED (functions $fns/5, objects $vars/1, emitted $live/2)"
    n_fail=$((n_fail + 1))
fi

# The two halves stay switchable, and -w still silences both.
a=$("$compiler" -Wno-unused-function -S -o /dev/null "$tmp/dead.c" 2>&1 | grep -c 'unused function')
b=$("$compiler" -Wno-unused-variable -S -o /dev/null "$tmp/dead.c" 2>&1 | grep -c 'unused function')
c=$("$compiler" -w -S -o /dev/null "$tmp/dead.c" 2>&1 | grep -c 'unused')
if [ "$a" -eq 0 ] && [ "$b" -eq 5 ] && [ "$c" -eq 0 ]; then
    echo "testing the dead-static groups are independent ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the dead-static groups are independent ... FAILED (no-function $a, no-variable $b, -w $c)"
    n_fail=$((n_fail + 1))
fi

# E4 second half (landed): 6.5.3.4p2 makes the operand of sizeof
# unevaluated, so a name only a skipped operand mentions does not keep its
# definition alive. clang drops exactly these definitions from the IR at -O0
# (and calls them out under -Wall as -Wunneeded-internal-declaration); gcc
# keeps them and says nothing. cxx now follows clang's *emission*, and its
# own default-on unused-variable group reports the object once the graph
# says nothing reaches it.
cat > "$tmp/szof.c" <<'EOF'
static int bound_only;
int a[sizeof bound_only];
static int in_body;
static int reads_size(void) { return sizeof in_body; }
_Static_assert(sizeof bound_only == 4, "sized");
int main(void) { return sizeof a - 16 + reads_size(); }
EOF
warn=$("$compiler" -S -o /dev/null "$tmp/szof.c" 2>&1 | grep -c 'unused variable')
emit=$("$compiler" -w -S -emit-llvm -o - "$tmp/szof.c" 2>/dev/null | grep -cE '@bound_only|@in_body')
sil=$("$compiler" -w -S -o /dev/null "$tmp/szof.c" 2>&1 | grep -c 'unused')
if [ "$warn" -eq 2 ] && [ "$emit" -eq 0 ] && [ "$sil" -eq 0 ]; then
    echo "testing a sizeof-only object is reported and not emitted ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a sizeof-only object is reported and not emitted ... FAILED (warned $warn/2, emitted $emit/0, -w $sil)"
    n_fail=$((n_fail + 1))
fi

# The exception that keeps that rule safe: when the operand's type is a
# variable length array its size expression IS evaluated, so the names in it
# have to stay alive -- and the program has to still link, which is what a
# dropped definition would break.
cat > "$tmp/vlasz.c" <<'EOF'
static int n;
static int f(void) { return (int)sizeof(int[++n]); }
int main(void) { return f() == (int)sizeof(int) && n == 1 ? 0 : 1; }
EOF
"$compiler" -w -o "$tmp/vlasz" "$tmp/vlasz.c" > "$tmp/log" 2>&1 && "$tmp/vlasz"
if [ $? -eq 0 ]; then
    echo "testing a variable length sizeof keeps its names alive ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a variable length sizeof keeps its names alive ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# Block scope is the other exception: neither reference drops a block-scope
# static, and clang stays quiet about one that sizeof mentions, so cxx does
# not report an object it goes on to emit.
cat > "$tmp/blksz.c" <<'EOF'
int f(void) { static int q; return sizeof q; }
int main(void) { return f() - 4; }
EOF
warn=$("$compiler" -S -o /dev/null "$tmp/blksz.c" 2>&1 | grep -c 'unused')
emit=$("$compiler" -w -S -emit-llvm -o - "$tmp/blksz.c" 2>/dev/null | grep -cE '@[A-Za-z0-9_.]*q')
if [ "$warn" -eq 0 ] && [ "$emit" -eq 1 ]; then
    echo "testing a block-scope static sizeof still counts as used ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a block-scope static sizeof still counts as used ... FAILED (warned $warn/0, emitted $emit/1)"
    n_fail=$((n_fail + 1))
fi

# And an evaluated use next to the sizeof use keeps the object, obviously.
cat > "$tmp/both.c" <<'EOF'
static int x = 7;
int main(void) { return (int)(sizeof x) + x - 7; }
EOF
warn=$("$compiler" -S -o /dev/null "$tmp/both.c" 2>&1 | grep -c 'unused')
emit=$("$compiler" -w -S -emit-llvm -o - "$tmp/both.c" 2>/dev/null | grep -cE '@x')
if [ "$warn" -eq 0 ] && [ "$emit" -ge 1 ]; then
    echo "testing an evaluated use still keeps the object ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing an evaluated use still keeps the object ... FAILED (warned $warn/0, emitted $emit)"
    n_fail=$((n_fail + 1))
fi

# typeof wants the type, not the value, so its operand is parked the same
# way -- and a variable length operand is evaluated, like sizeof's.
cat > "$tmp/typeof.c" <<'EOF'
static int only_type;
static int live = 5;
int main(void) {
    typeof(only_type) a = 1;
    typeof_unqual(live) b = live;
    return a + b - 6;
}
EOF
warn=$("$compiler" -S -o /dev/null "$tmp/typeof.c" 2>&1 | grep -c 'unused variable')
drop=$("$compiler" -w -S -emit-llvm -o - "$tmp/typeof.c" 2>/dev/null | grep -c '@only_type')
live=$("$compiler" -w -S -emit-llvm -o - "$tmp/typeof.c" 2>/dev/null | grep -c '@live')
if [ "$warn" -eq 1 ] && [ "$drop" -eq 0 ] && [ "$live" -eq 2 ]; then
    echo "testing a typeof-only object is reported and not emitted ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a typeof-only object is reported and not emitted ... FAILED (warned $warn/1, @only_type $drop/0, @live $live/2)"
    n_fail=$((n_fail + 1))
fi

cat > "$tmp/tvla.c" <<'EOF'
static int n;
int main(void) { typeof(int[++n]) v; v[0] = 3; return n == 1 && v[0] == 3 ? 0 : 1; }
EOF
"$compiler" -w -o "$tmp/tvla" "$tmp/tvla.c" > "$tmp/log" 2>&1 && "$tmp/tvla"
if [ $? -eq 0 ]; then
    echo "testing a variable length typeof keeps its names alive ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a variable length typeof keeps its names alive ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# 6.5.1.1p3: the controlling expression of a generic selection is not
# evaluated, and neither is an association it does not select. clang leaves
# exactly those definitions out of the IR; cxx reports them too, through its
# own default-on unused groups.
cat > "$tmp/gen.c" <<'EOF'
static int ctrl_only;
static int unsel_only;
static int selected = 7;
int main(void) {
    int a = _Generic(ctrl_only, int: 1, default: 0);
    int b = _Generic(0, double: unsel_only, default: 0);
    int c = _Generic(0.0, double: selected, default: 0);
    return a + b + c - 8;
}
EOF
warn=$("$compiler" -S -o /dev/null "$tmp/gen.c" 2>&1 | grep -c 'unused variable')
drop=$("$compiler" -w -S -emit-llvm -o - "$tmp/gen.c" 2>/dev/null | grep -cE '@ctrl_only|@unsel_only')
keep=$("$compiler" -w -S -emit-llvm -o - "$tmp/gen.c" 2>/dev/null | grep -c '@selected')
if [ "$warn" -eq 2 ] && [ "$drop" -eq 0 ] && [ "$keep" -eq 2 ]; then
    echo "testing a _Generic mention that is never evaluated ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a _Generic mention that is never evaluated ... FAILED (warned $warn/2, dropped $drop/0, kept $keep/2)"
    n_fail=$((n_fail + 1))
fi

# 6.5.2.1p2: the lvalue, array-to-pointer and function-to-pointer conversions
# belong to an assignment expression operand. A type name designates the type
# it writes, so `int[3]` does not become `int *` and `int (void)` does not
# become a pointer -- gcc and clang both answer 9 and 2 below, and 5 for the
# expression form beside them, which does decay.
runit "a _Generic type name keeps the type it designates" <<'EOF'
static int a[3];
int main(void) {
    int t = _Generic(int[3], int *: 1, default: 9);
    int u = _Generic(int (void), int (*)(void): 1, default: 2);
    int v = _Generic(a, int *: 5, default: 0);
    return t * 100 + u * 10 + v == 925 ? 0 : 1;
}
EOF

# 6.5.2.1p3: a size expression inside a type name is not evaluated either, so
# `++n` never runs. The name is not reported as unused, though: clang keeps it
# and says nothing, and cxx follows the reference rather than dropping an
# object it would then be alone in complaining about.
cat > "$tmp/gvla.c" <<'EOF'
static int n;
int main(void) { return _Generic(int[++n], int *: 1, default: 9) + n * 10; }
EOF
val=$("$compiler" -w -o "$tmp/gvla" "$tmp/gvla.c" > "$tmp/log" 2>&1 && "$tmp/gvla"; echo $?)
keep=$("$compiler" -w -S -emit-llvm -o - "$tmp/gvla.c" 2>/dev/null | grep -c '@n')
warn=$("$compiler" -S -o /dev/null "$tmp/gvla.c" 2>&1 | grep -c 'unused')
if [ "$val" -eq 9 ] && [ "$keep" -ge 1 ] && [ "$warn" -eq 0 ]; then
    echo "testing a size expression in a generic type name is not evaluated ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a size expression in a generic type name is not evaluated ... FAILED (value $val/9, kept $keep, warned $warn/0)"
    n_fail=$((n_fail + 1))
fi

# Two shapes where an emitted initializer names something the graph would
# otherwise call dead. Both used to produce a module cxx itself refused
# ("use of undefined value"), because the name was credited to a function the
# graph had already dropped -- one of them ever since the reference graph
# landed, the other one was exposed by the sizeof rule above.
cat > "$tmp/lit.c" <<'EOF'
struct S { int *p; };
static int x;
int bound = sizeof((struct S){&x});
int main(void) { return bound != 0 ? 0 : 1; }
EOF
"$compiler" -w -o "$tmp/lit" "$tmp/lit.c" > "$tmp/log" 2>&1 && "$tmp/lit"
lit=$?
cat > "$tmp/blkinit.c" <<'EOF'
struct S { int *p; };
static int y;
static int dead(void) { static struct S s = {&y}; return s.p != 0; }
int main(void) { return 0; }
EOF
"$compiler" -w -o "$tmp/blkinit" "$tmp/blkinit.c" > "$tmp/log2" 2>&1 && "$tmp/blkinit"
blk=$?
if [ "$lit" -eq 0 ] && [ "$blk" -eq 0 ]; then
    echo "testing an emitted initializer keeps what it names ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing an emitted initializer keeps what it names ... FAILED (compound literal $lit, block static $blk)"
    sed 's/^/    /' "$tmp/log" "$tmp/log2" | head -4
    n_fail=$((n_fail + 1))
fi

# [GNU] __attribute__((cleanup(f))): the handler runs, with the object's
# address, when its scope is left. Every shape below is one gcc and clang
# agree on to the character -- the order within a scope is the reverse of the
# declarations, an inner block goes before an outer one, a return reads the
# result *before* the handlers run, and a for-init declaration is destroyed
# where the loop ends, once, whether it ran to the end or was left by a break.
cat > "$tmp/clean.c" <<'EOF'
#include <stdio.h>
static void hi(int *p) { printf("i(%d) ", *p); }
static void hb(int *p) { printf("b(%d) ", *p); }
static void ha(int *p) { printf("a(%d) ", *p); *p = 100; }
static int first(void) { __attribute__((cleanup(ha))) int x = 7; return x; }
static void second(void) {
    __attribute__((cleanup(hi))) int outer = 1;
    { __attribute__((cleanup(hb))) int inner = 2; printf("blk "); }
    printf("mid ");
}
int main(void) {
    for (__attribute__((cleanup(hi))) int i = 0; i < 3; i++) {
        __attribute__((cleanup(hb))) int b = i;
        if (i == 1) break;
    }
    for (int j = 0; j < 2; j++) {
        __attribute__((cleanup(ha))) int a = j;
        if (j == 0) continue;
        break;
    }
    switch (1) { case 1: { __attribute__((cleanup(hb))) int s = 5; break; } }
    { __attribute__((cleanup(hi))) int g = 6; goto out; }
    printf("skipped ");
out:
    printf("| first=%d ", first());
    second();
    printf("done ");
    return 0;
}
EOF
want="b(0) b(1) i(1) a(0) a(1) b(5) i(6) a(7) | first=7 blk b(2) mid i(1) done "
got=$("$compiler" -w -o "$tmp/clean" "$tmp/clean.c" 2>"$tmp/log" && "$tmp/clean" 2>&1)
if [ "$got" = "$want" ]; then
    echo "testing cleanup runs at every way out of a scope ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing cleanup runs at every way out of a scope ... FAILED"
    echo "    got  [$got]"
    echo "    want [$want]"
    n_fail=$((n_fail + 1))
fi

# The attribute is a variable attribute: both references warn and ignore it
# everywhere else, and they refuse a handler that is not a one-parameter
# function whose parameter the object's address can be passed to.
cat > "$tmp/clean2.c" <<'EOF'
static void h(int *p) { (void)p; }
__attribute__((cleanup(h))) static int g = 1;
int main(void) { __attribute__((cleanup(h))) static int s = 1; return 0; }
EOF
cat > "$tmp/clean3.c" <<'EOF'
int main(void) { int n = 0; __attribute__((cleanup(n))) int x = 1; return 0; }
EOF
cat > "$tmp/clean4.c" <<'EOF'
static void h(char *p) { (void)p; }
int main(void) { __attribute__((cleanup(h))) int x = 1; return 0; }
EOF
cat > "$tmp/clean5.c" <<'EOF'
static void h(int *p, int q) { (void)p; (void)q; }
int main(void) { __attribute__((cleanup(h))) int x = 1; return 0; }
EOF
cat > "$tmp/clean6.c" <<'EOF'
int main(void) { __attribute__((cleanup)) int x = 1; return 0; }
EOF
ignored=$("$compiler" -S -o /dev/null "$tmp/clean2.c" 2>&1 | grep -c 'only applies to local variables')
notfn=$("$compiler" -S -o /dev/null "$tmp/clean3.c" 2>&1 | grep -c 'argument ‘n’ is not a function')
badty=$("$compiler" -S -o /dev/null "$tmp/clean4.c" 2>&1 | grep -c "parameter has type ‘char \*’")
arity=$("$compiler" -S -o /dev/null "$tmp/clean5.c" 2>&1 | grep -c 'must take 1 parameter')
noarg=$("$compiler" -S -o /dev/null "$tmp/clean6.c" 2>&1 | grep -c 'takes one argument')
if [ "$ignored" -eq 2 ] && [ "$notfn" -eq 1 ] && [ "$badty" -eq 1 ] && [ "$arity" -eq 1 ] && [ "$noarg" -eq 1 ]; then
    echo "testing cleanup misuse is diagnosed like clang ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing cleanup misuse is diagnosed like clang ... FAILED (ignored $ignored/2, notfn $notfn, type $badty, arity $arity, noarg $noarg)"
    n_fail=$((n_fail + 1))
fi

# A variable length array keeps its length as the expression that computes
# it, and that pointer shares its storage with the array type's `len` field.
# is_compatible() read `len`, so `int (*)[3]` accepted `&vla` on some runs and
# refused it on others -- the same source gave different diagnostics twice in
# a row. A VLA now matches any length, which is what gcc and clang accept, and
# a fixed array is still compared.
cat > "$tmp/vlacompat.c" <<'EOF'
static void hv(int (*p)[3]) { (void)p; }
int main(void) { int n = 3; int a[n]; int b[3]; hv(&a); hv(&b); return 0; }
EOF
cat > "$tmp/vlacompat2.c" <<'EOF'
static void bad(int (*p)[4]) { (void)p; }
int main(void) { int a[3]; bad(&a); return 0; }
EOF
runs=0
for i in 1 2 3 4 5; do
    "$compiler" -w -S -o /dev/null "$tmp/vlacompat.c" >/dev/null 2>&1 && runs=$((runs + 1))
done
wrong=$("$compiler" -S -o /dev/null "$tmp/vlacompat2.c" 2>&1 | grep -c 'incompatible types')
if [ "$runs" -eq 5 ] && [ "$wrong" -ge 1 ]; then
    echo "testing a variable length array matches any length ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a variable length array matches any length ... FAILED (accepted $runs/5 runs, wrong bound diagnosed $wrong)"
    n_fail=$((n_fail + 1))
fi

# 6.8.6.1p1: a goto may not jump from outside the scope of an identifier with
# a variably modified type to inside it, and 6.8.5.3p2 says the same about the
# labels a switch dispatches to. The size expression is evaluated where the
# declaration is reached, so a jump that skips it leaves the object's type
# without a value -- both references refuse every shape below.
bad "goto jumps over a variable length array" <<'EOF'
int f(int n) { goto l; int a[n]; l: a[0] = 1; return 0; }
EOF
bad "goto jumps into the scope of a variable length array" <<'EOF'
int f(int n) { goto l; { int a[n]; l: a[0] = 1; } return 0; }
EOF
bad "goto jumps over a pointer to a variable length array" <<'EOF'
int f(int n) { goto l; int (*p)[n]; l: return p != 0; }
EOF
bad "goto jumps over a variable length array typedef" <<'EOF'
int f(int n) { goto l; typedef int T[n]; l: return sizeof(T); }
EOF
bad "a case label a switch reaches past a variable length array" <<'EOF'
int f(int n, int x) { switch (x) { case 1: ; int a[n]; case 2: return a[0]; } return 0; }
EOF
bad "a case label inside the scope of a variable length array" <<'EOF'
int f(int n, int x) { switch (x) { { int a[n]; case 1: return a[0]; } } return 0; }
EOF

# The message names the declaration that would be skipped.
cat > "$tmp/vmjump.c" <<'EOF'
int f(int n) { goto l; int a[n]; l: a[0] = 1; return 0; }
EOF
msg=$("$compiler" -S -o /dev/null "$tmp/vmjump.c" 2>&1 | grep -c 'jump bypasses initialization of variable length array ‘a’')
what=$("$compiler" -S -o /dev/null "$tmp/vmjump.c" 2>&1 | grep -c 'cannot jump from this goto statement to its label')

# And the jumps that leave the size expression alone are all fine.
runit "jumps that do not skip a variable length array" <<'EOF'
static int f(int n) { int a[n]; a[0] = 0; goto l; l: a[0] += 1; return a[0]; }
static int g(int n) { int a[n]; a[0] = 0; l2: if (a[0]++ < 1) goto l2; return a[0]; }
static int h(int n) { { int a[n]; a[0] = 1; } goto l3; { l3: ; } return 0; }
static int k(int n, int x) { int a[n]; a[0] = 3; switch (x) { case 1: return a[0]; default: return a[0] + 4; } }
static int m(int x) { switch (x) { case 1: ; int a[3]; a[0] = 1; case 2: return a[0]; } return 0; }
int main(void) { return f(3) + g(2) + h(4) + k(5, 7) + m(7) == 10 ? 0 : 1; }
EOF

if [ "$msg" -eq 1 ] && [ "$what" -eq 1 ]; then
    echo "testing a jump that skips a VLA is refused ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a jump that skips a VLA is refused ... FAILED (note $msg/1, error $what/1)"
    n_fail=$((n_fail + 1))
fi

# 6.2.1p7: a parameter name is in scope for the parameters after it, so a
# bound may name one -- and the bound is evaluated once per call, at the top of
# the body, where that parameter has its value. The inner dimension of a
# variably modified parameter is a real bound: `a[1][3]` steps by it, so a
# missing or mistyped one shows up as the wrong element.
runit "a VLA parameter bound names an earlier parameter" <<'EOF'
static int f(int n, int a[n]) { return a[n - 1]; }
static int g(int n, int m, int a[n][m]) { return a[n - 1][m - 1]; }
static int h(int m, int a[2][m]) { return (int)sizeof a[0] + a[1][3]; }
int main(void) {
    int a[3] = {1, 2, 9};
    int b[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 9}};
    if (f(3, a) != 9) return 1;
    if (g(2, 4, b) != 9) return 2;
    if (h(4, b) != 16 + 9) return 3;
    return 0;
}
EOF

# The same function declared first and defined later: the prototype's own
# parameter list is a scope of its own, and it must not leave anything behind
# for the definition (or for a function defined after it).
runit "a VLA parameter survives its prototype" <<'EOF'
static int f(int n, int a[n]);
static int g(int x) { return x; }
static int f(int n, int a[n]) { return a[n - 1] + g(0); }
int main(void) { int a[2] = {3, 6}; return f(2, a) == 6 ? 0 : 1; }
EOF

cat > "$tmp/vlastar.c" <<'EOF'
int f(int n, int a[*]);
int main(void) { return 0; }
EOF
cat > "$tmp/vlastar2.c" <<'EOF'
int f(int n, int a[*]) { return a[0]; }
int main(void) { return f(1, 0); }
EOF
proto=$("$compiler" -S -o /dev/null "$tmp/vlastar.c" 2>&1 | grep -c 'error')
defn=$("$compiler" -S -o /dev/null "$tmp/vlastar2.c" 2>&1 | grep -c 'not allowed in other than function prototype scope')
if [ "$proto" -eq 0 ] && [ "$defn" -eq 1 ]; then
    echo "testing [*] is a prototype-only bound ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing [*] is a prototype-only bound ... FAILED (prototype errors $proto, definition $defn/1)"
    n_fail=$((n_fail + 1))
fi

# E1 (landed): [[deprecated]] on a *type*. The tag is what the diagnostic
# names, at the use -- not at the definition, and not for a bare
# redeclaration -- which is where gcc and clang put it too. A use in a
# parameter list counts, and so does an enum.
cat > "$tmp/dep.c" <<'EOF'
struct [[deprecated]] S { int a; };
struct S;
struct S v;
enum [[deprecated]] E { A };
enum E w;
int uses(struct S s, enum E e) { return s.a + (int)e; }
int main(void) { return v.a + (int)w + uses(v, w); }
EOF
lines=$("$compiler" -S -o /dev/null "$tmp/dep.c" 2>&1 | grep 'is deprecated' | sed 's/.*dep\.c://; s/:.*//' | tr '\n' ' ')
if [ "$lines" = "3 5 6 6 " ]; then
    echo "testing a deprecated type is reported at each use ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a deprecated type is reported at each use ... FAILED (lines '$lines', want '3 5 6 6 ')"
    n_fail=$((n_fail + 1))
fi

# A ?: whose arm opens blocks of its own. gen_cond patched the arm block it
# started in, so the edge into the merge block and the merge PHI's
# predecessor both named a block that was not a predecessor: LLVM rejected
# the module with "PHI node entries do not match predecessors!". A nested ?:
# in either arm is enough to show it, and the value has to survive.
runit "?: with a conditional in either arm" <<'EOF'
int f(int a, int b) { return a ? 1 : b ? 2 : 3; }
int g(int a, int b) { return a ? b ? 4 : 5 : 6; }
int h(int a, int b) { return a ? 1 : (b ? 2 : 3); }
int k(int a, int b) { return (a ? b : 4) ? 5 : 6; }
int main(void) {
    if (f(1, 0) != 1 || f(0, 1) != 2 || f(0, 0) != 3) return 1;
    if (g(1, 1) != 4 || g(1, 0) != 5 || g(0, 0) != 6) return 2;
    if (h(1, 0) != 1 || h(0, 1) != 2 || h(0, 0) != 3) return 3;
    if (k(1, 1) != 5 || k(1, 0) != 6 || k(0, 1) != 5) return 4;
    return 0;
}
EOF

# P3 step 2 (landed): the transcoding functions of 7.26.2 and 7.26.3. They
# are static inline definitions in the header -- cxx ships no runtime library
# of its own -- so this checks the whole surface through the header: the
# round trip through UTF-16 and UTF-32, the counting mode, and the three
# failure codes with the "nothing moves" rule each one carries.
runit "P3 <stdmchar.h> transcoding" <<'EOF'
#include <stdmchar.h>
#include <stddef.h>
#include <string.h>

/* A, e-acute (2 bytes), euro (3), dolphin (4): the four UTF-8 lengths. */
static const char8_t src[] = u8"A\u00E9\u20AC\U0001F42C";
#define SRC_N (sizeof src / sizeof src[0])

int main(void) {
    char16_t c16[32];
    char32_t c32[32];
    char8_t back[32];
    char16_t *p16 = c16;
    char32_t *p32 = c32;
    char8_t *pb = back;
    const char8_t *in8 = src;
    const char16_t *in16;
    const char32_t *in32;
    size_t n16 = 32, n32 = 32, back_n = 32, in_n = SRC_N;

    if (stdc_c8snrtoc16sn(&n16, &p16, &in_n, &in8, nullptr) != stdc_mcerr_ok) return 1;
    if (in_n != 0 || n16 != 32 - 6) return 2; /* 4 BMP units, 2 for the dolphin, and the NUL */

    in16 = c16;
    in_n = 32 - n16;
    if (stdc_c16snrtoc32sn(&n32, &p32, &in_n, &in16, nullptr) != stdc_mcerr_ok) return 3;
    if (n32 != 32 - 5) return 4; /* five code points */

    in32 = c32;
    in_n = 32 - n32;
    if (stdc_c32snrtoc8sn(&back_n, &pb, &in_n, &in32, nullptr) != stdc_mcerr_ok) return 5;
    if (back_n != 32 - SRC_N || memcmp(back, src, SRC_N) != 0) return 6;

    /* the execution encodings are the ones the header's macros declare */
    {
        char mb[32];
        wchar_t wc[32];
        char *pm = mb;
        wchar_t *pw = wc;
        const char *inm;
        const wchar_t *inw;
        size_t nm = 32, nw = 32;
        in8 = src;
        in_n = SRC_N;
        if (stdc_c8snrtomcsn(&nm, &pm, &in_n, &in8, nullptr) != stdc_mcerr_ok) return 7;
        if (nm != 32 - SRC_N || memcmp(mb, src, SRC_N) != 0) return 8; /* mc is UTF-8 */
        inm = mb;
        in_n = SRC_N;
        if (stdc_mcsnrtomwcsn(&nw, &pw, &in_n, &inm, nullptr) != stdc_mcerr_ok) return 9;
        if (nw != 32 - 5) return 10; /* mwc is UTF-32 */
        if (wc[1] != 0xE9 || wc[2] != 0x20AC || wc[3] != 0x1F42C) return 11;
    }

    /* not enough room to write one unit of work: nothing moves */
    {
        char16_t one[1];
        char16_t *p = one;
        size_t out_n = 1;
        const char8_t *in2 = src + 6; /* the dolphin's four bytes */
        size_t in_n2 = 4;
        if (stdc_c8nrtoc16n(&out_n, &p, &in_n2, &in2, nullptr) != stdc_mcerr_insufficient_output) return 12;
        if (out_n != 1 || in_n2 != 4 || in2 != src + 6) return 13;
    }

    /* an unfinished sequence: nothing moves either */
    {
        char16_t out[4];
        char16_t *p = out;
        size_t out_n = 4;
        const char8_t *in2 = src + 1; /* the first byte of the two-byte e-acute */
        size_t in_n2 = 1;
        if (stdc_c8nrtoc16n(&out_n, &p, &in_n2, &in2, nullptr) != stdc_mcerr_incomplete_input) return 14;
        if (out_n != 4 || in_n2 != 1 || in2 != src + 1) return 15;
    }

    /* invalid sequences */
    {
        static const char8_t bad[] = {0xFF, 0};
        static const char8_t overlong[] = {0xC0, 0x80, 0};
        static const char32_t beyond[] = {0x110000, 0};
        static const char16_t lone[] = {0xD800, 0x0041};
        static const char16_t unfinished[] = {0xD800};
        char32_t out32[4];
        char8_t out8[4];
        char32_t *p32o = out32;
        char8_t *p8o = out8;
        size_t out_n, in_n2;
        const char8_t *inb;
        const char32_t *in32b;
        const char16_t *in16b;

        inb = bad;
        out_n = 4;
        in_n2 = 1;
        if (stdc_c8nrtoc32n(&out_n, &p32o, &in_n2, &inb, nullptr) != stdc_mcerr_invalid) return 16;
        inb = overlong;
        out_n = 4;
        in_n2 = 2;
        if (stdc_c8nrtoc32n(&out_n, &p32o, &in_n2, &inb, nullptr) != stdc_mcerr_invalid) return 17;
        in32b = beyond;
        out_n = 4;
        in_n2 = 1;
        if (stdc_c32nrtoc8n(&out_n, &p8o, &in_n2, &in32b, nullptr) != stdc_mcerr_invalid) return 18;
        in16b = lone;
        out_n = 4;
        in_n2 = 2;
        if (stdc_c16nrtoc8n(&out_n, &p8o, &in_n2, &in16b, nullptr) != stdc_mcerr_invalid) return 19;
        in16b = unfinished;
        out_n = 4;
        in_n2 = 1;
        if (stdc_c16nrtoc8n(&out_n, &p8o, &in_n2, &in16b, nullptr) != stdc_mcerr_incomplete_input) return 20;
    }

    /* counting mode: no output pointer, output_size says how much would go out */
    {
        const char8_t *in2 = src;
        size_t in_n2 = SRC_N;
        size_t need = SIZE_MAX;
        if (stdc_c8snrtoc16sn(&need, nullptr, &in_n2, &in2, nullptr) != stdc_mcerr_ok) return 21;
        if (need != SIZE_MAX - 6 || in_n2 != 0) return 22;
    }

    /* a null input resets the state and reports success */
    {
        mbstate_t st = {0};
        if (stdc_c8nrtoc16n(nullptr, nullptr, nullptr, nullptr, &st) != stdc_mcerr_ok) return 23;
        if (mbsinit(&st) == 0) return 24;
    }

    /* empty input is success for both families */
    {
        char16_t out[4];
        char16_t *p = out;
        size_t out_n = 4, in_n2 = 0;
        const char8_t *in2 = src;
        if (stdc_c8snrtoc16sn(&out_n, &p, &in_n2, &in2, nullptr) != stdc_mcerr_ok) return 25;
        if (out_n != 4 || in_n2 != 0) return 26;
    }

    return 0;
}
EOF

# --- known gaps (doc/cxx-c2y-plan.md) ---------------------------------

# P1 stage 1 (landed): the constant producers behind HUGE_VAL, INFINITY and
# NAN. glibc expands those to __builtin_huge_val*, __builtin_inf* and
# __builtin_nanf, which the parser now folds to the constant each names.
runit "P1 <math.h> NAN / INFINITY / HUGE_VAL" <<'EOF'
#include <math.h>
int main(void) {
    double n = NAN, i = INFINITY;
    if (n == n) return 1;
    if (!(i > 1e308)) return 2;
    if (!(HUGE_VAL > 1e308 && HUGE_VALF > 1e38f && HUGE_VALL > 1e308L)) return 3;
    if (sizeof(HUGE_VAL) != 8 || sizeof(HUGE_VALF) != 4 ||
        sizeof(HUGE_VALL) != sizeof(long double)) return 4;
    return 0;
}
EOF

# P1 stage 3 (landed): the classification family of 7.12.4. Each of these
# uses its operand value in more than one comparison, so they reach irgen,
# where the operand is evaluated once into a Ref and the Ref is reused.
# isfinite is two `one` comparisons -- false for an infinity because equal,
# false for a NaN because unordered -- and signbit is copysign(1.0, x) < 0,
# which carries a NaN's sign as 7.12.4.8 footnote 281 requires.
runit "P1 <math.h> isnan / isinf / isfinite / fpclassify / signbit" <<'EOF'
#include <math.h>
int main(void) {
    double n = NAN, i = INFINITY;
    float nf = (float)NAN, inf = (float)INFINITY;
    if (!isnan(n) || isnan(1.0)) return 1;
    if (!isinf(i) || !isinf(-i) || isinf(1.0)) return 2;
    if (!isfinite(1.0) || isfinite(i) || isfinite(n)) return 3;
    if (fpclassify(1.0) != FP_NORMAL || fpclassify(0.0) != FP_ZERO) return 4;
    if (fpclassify(i) != FP_INFINITE || fpclassify(n) != FP_NAN) return 5;
    if (!isnormal(1.0) || isnormal(0.0) || isnormal(i) || isnormal(n)) return 6;
    if (!signbit(-1.0) || signbit(1.0)) return 7;
    /* the sign of a zero and of a NaN, which no arithmetic test gives */
    if (!signbit(-0.0) || signbit(0.0)) return 8;
    if (!signbit(-n) || signbit(n)) return 9;
    /* float and long double reach the same builtins */
    if (!isnan(nf) || !isinf(inf) || !isfinite(1.0f) || !signbit(-0.0f)) return 10;
    if (!isnan((long double)n) || !isinf((long double)i)) return 11;
    if (!signbit(-(long double)n)) return 12;
    /* __builtin_isinf_sign answers 1 / -1 / 0 */
    if (__builtin_isinf_sign(i) != 1 || __builtin_isinf_sign(-i) != -1) return 13;
    if (__builtin_isinf_sign(1.0) != 0 || __builtin_isinf_sign(n) != 0) return 14;
    return 0;
}
EOF

# P1c (landed): x87 80-bit long double subnormals. round116_to_target()
# built a canonical fp128 form with an exponent field of at least 1, which
# only works while the target's smallest subnormal is still normal in
# binary128 (true for binary16/32/64). x87 bottoms out at 2^-16445, so the
# field went negative and wrapped into the sign bit: 0x1p-16383L was zero,
# 0x1p-16400L was a large negative number and LDBL_MIN/4 was -inf. The
# subnormal binary128 form is now produced instead.
runit "P1c x87 long double subnormal constants" <<'EOF'
#include <float.h>
long double a = 0x1p-16383L;    /* the smallest x87 subnormal, 0x4p-16385 */
long double b = LDBL_MIN / 2;   /* the same value, reached by folding */
long double c = LDBL_MIN / 4;
int main(void) {
    if (a == 0 || b == 0 || c == 0) return 1;
    if (!(a > 0) || !(b > 0) || !(c > 0)) return 2;   /* they used to go negative */
    if (b != a) return 3;                             /* 2^-16383 either way */
    if (!(c < b)) return 4;                           /* and stay ordered */
    if (c * 2 != b || b * 2 != LDBL_MIN) return 5;    /* exact scaling */
    return 0;
}
EOF

# P1 stage 2 (landed): the comparison macros of 7.12.18. The four that are
# one C operator each are rewritten by the parser, so each operand is
# evaluated once; islessgreater and isunordered need the `one` and `uno`
# fcmp predicates, because writing them as operators would put each operand
# in two positions and a shared node is generated twice by irgen while the
# block accounting counted it once.
runit "P1 <math.h> comparison macros" <<'EOF'
#include <math.h>
int main(void) {
    double n = NAN, i = INFINITY;
    if (!isgreater(2.0, 1.0) || isgreater(1.0, 2.0)) return 1;
    if (!isgreaterequal(1.0, 1.0) || isgreaterequal(1.0, 2.0)) return 2;
    if (!isless(1.0, 2.0) || isless(2.0, 1.0)) return 3;
    if (!islessequal(1.0, 1.0) || islessequal(2.0, 1.0)) return 4;
    if (!islessgreater(1.0, 2.0) || !islessgreater(2.0, 1.0) || islessgreater(1.0, 1.0)) return 5;
    if (!isunordered(n, 1.0) || !isunordered(1.0, n) || isunordered(1.0, 2.0)) return 6;
    /* every ordered macro answers 0 when either operand is a NaN */
    if (isgreater(n, 1.0) || isgreaterequal(n, 1.0) || isless(n, 1.0) || islessequal(n, 1.0)) return 7;
    if (isgreater(1.0, n) || isgreaterequal(1.0, n) || isless(1.0, n) || islessequal(1.0, n)) return 8;
    /* islessgreater is NOT `x != y`: a NaN pair is unequal but unordered */
    if (islessgreater(n, n) || !(n != n)) return 9;
    if (!isgreater(i, 1.0) || islessgreater(i, i)) return 10;
    return 0;
}
EOF

# P1b (landed): the signaling NaN producers. fp128_to_fp16/32/64/80_bits()
# used to canonicalise every NaN to the quiet form, so a signaling NaN could
# not reach the output; they now keep the quiet bit's state and the payload's
# leading bits, which reproduces gcc's and clang's patterns exactly.
runit "P1b <math.h> SNAN / __builtin_nans" <<'EOF'
#include <math.h>
#include <string.h>
int main(void) {
    double d = __builtin_nans("");
    float f = __builtin_nansf("");
    unsigned long long db;
    unsigned fb;
    memcpy(&db, &d, 8);
    memcpy(&fb, &f, 4);
    if (d == d || f == f) return 1;            /* both are NaNs */
    if (db != 0x7FF4000000000000ULL) return 2; /* payload in the top bit, quiet bit clear */
    if (fb != 0x7FA00000u) return 3;

    /* The quiet forms are unchanged, and the two differ: that difference is
     * the whole point of having both families. */
    double qd = NAN;
    unsigned long long qdb;
    memcpy(&qdb, &qd, 8);
    if (qdb != 0x7FF8000000000000ULL) return 4;
    if (db == qdb) return 5;

    /* the long double one, where the target has the x87 format */
#if defined(__x86_64__) && defined(__LDBL_MANT_DIG__) && __LDBL_MANT_DIG__ == 64
    long double ld = __builtin_nansl("");
    unsigned char lb[10];
    memcpy(lb, &ld, 10);
    if (lb[7] != 0xA0 || lb[8] != 0xFF || lb[9] != 0x7F) return 6; /* significand a0.., exp all ones */
#endif
    return 0;
}
EOF

# P2 (landed): a record-typed lvalue conversion must be the identity, because
# every other part of the compiler represents a record value by its address.
# It used to emit a real struct load, and the member access then used that
# value as its GEP base. That is exactly the shape atomic_load() lowers to --
# `(temp = *object, temp)` -- so every whole-aggregate atomic read produced
# invalid IR. The minimal case has nothing to do with atomics: `(0, t).a`,
# where the comma's result is not an lvalue (6.5.18 footnote 115) and so is
# converted before fold_ast rewrites the comma to its right operand.
runit "P2 whole-aggregate atomic load/store" <<'EOF'
#include <stdatomic.h>
struct S { int a; int b; };
int main(void) {
    _Atomic struct S v;
    /* the extra parentheses are load-bearing: braces do not protect a comma
     * from being a macro argument separator */
    atomic_store(&v, ((struct S){3, 4}));
    struct S w = atomic_load(&v);
    if (w.a != 3 || w.b != 4) return 1;
    if (atomic_load(&v).b != 4) return 2;
    atomic_init(&v, ((struct S){7, 8}));
    if (atomic_load(&v).a != 7 || atomic_load(&v).b != 8) return 3;
    return 0;
}
EOF

# P2b (landed): the read-modify-write family handed the aggregate over as an
# ADDRESS, so atomic_exchange(&v, x) emitted
#     atomicrmw xchg ptr %obj, ptr %slot_of_x
# and LLVM exchanged the pointer's VALUE rather than the aggregate's bytes.
# It now does what the load/store path does: read the operand's bytes out
# with an integer load, exchange integers, hand the result back through a
# slot. Found only at run time -- it compiled cleanly and then faulted.
runit "P2b atomic_exchange on a whole aggregate" <<'EOF'
#include <stdatomic.h>
struct S { int a; int b; };
struct W { long long a; };
int main(void) {
    _Atomic struct S v;
    atomic_store(&v, ((struct S){3, 4}));
    struct S old = atomic_exchange(&v, ((struct S){5, 6}));
    if (old.a != 3 || old.b != 4) return 1;
    if (atomic_load(&v).a != 5 || atomic_load(&v).b != 6) return 2;
    /* the 8-byte shape, and a second exchange in the other direction */
    _Atomic struct W w;
    atomic_store(&w, ((struct W){11}));
    if (atomic_exchange(&w, ((struct W){22})).a != 11) return 3;
    if (atomic_load(&w).a != 22) return 4;
    /* the value comes back as the aggregate, so a member access works */
    if (atomic_exchange(&v, ((struct S){7, 8})).b != 6) return 5;
    return 0;
}
EOF

# P2c (landed): atomic_compare_exchange_* on a whole aggregate. The compare
# value used to be the aggregate itself and the new value its ADDRESS, so the
# cmpxchg came out as `cmpxchg ptr, %struct.S, ptr` -- IR that LLVM rejects
# outright ("compare value and new value type do not match") instead of the
# `cmpxchg ptr, i64, i64` clang emits. Both operands are now read out as an
# integer of the aggregate's size, and the failure writeback stores that
# integer into *expected: byte for byte clang's shape.
runit "P2c atomic_compare_exchange on a whole aggregate" <<'EOF'
#include <stdatomic.h>
struct S { int a; int b; };
struct W { long long a; };
int main(void) {
    /* 8-byte record, strong CAS: success leaves *expected alone */
    _Atomic struct S v;
    atomic_init(&v, ((struct S){1, 2}));
    struct S exp = {1, 2};
    if (!atomic_compare_exchange_strong(&v, &exp, ((struct S){3, 4}))) return 1;
    if (exp.a != 1 || exp.b != 2) return 2;
    if (atomic_load(&v).a != 3 || atomic_load(&v).b != 4) return 3;
    /* failure: the object stands, *expected gets what it held */
    exp.a = 9; exp.b = 9;
    if (atomic_compare_exchange_strong(&v, &exp, ((struct S){5, 6}))) return 4;
    if (exp.a != 3 || exp.b != 4) return 5;
    if (atomic_load(&v).a != 3 || atomic_load(&v).b != 4) return 6;
    /* the weak form, and a record whose single member fills all 8 bytes */
    if (!atomic_compare_exchange_weak(&v, &exp, ((struct S){7, 8}))) return 7;
    _Atomic struct W w;
    atomic_init(&w, ((struct W){11}));
    struct W wexp = {11};
    if (!atomic_compare_exchange_strong(&w, &wexp, ((struct W){22}))) return 8;
    if (atomic_load(&w).a != 22) return 9;
    /* the aggregate result is still a _Bool, usable as a value */
    int ok = atomic_compare_exchange_strong(&v, &exp, ((struct S){1, 1}));
    if (ok != 0) return 10;
    return exp.a == 7 && exp.b == 8 ? 0 : 11;
}
EOF

# cmpxchg has the same size limit as the load/store path.
bad "reject an _Atomic aggregate too large for cmpxchg" <<'EOF'
#include <stdatomic.h>
struct S { long long a, b; };
int main(void) {
    _Atomic struct S v;
    struct S exp = {0, 0};
    return atomic_compare_exchange_strong(&v, &exp, ((struct S){1, 1}));
}
EOF

# The same limit the load/store path enforces: no atomic instruction covers
# an aggregate that is not 1/2/4/8 bytes.
bad "reject an _Atomic aggregate larger than 8 bytes" <<'EOF'
#include <stdatomic.h>
struct S { long long a, b; };
int main(void) { _Atomic struct S v; return (int)atomic_load(&v).a; }
EOF

runit "record lvalue conversion is the identity" <<'EOF'
struct S { int a; };
struct T { long long a; };
int g1(void) { struct S t = {7}; return (0, t).a; }
int g2(struct S t) { return (t, t).a; }
int g3(struct S t) { return t.a; }
int g4(void) { struct T t = {9}; return (int)(0, t).a; }
int main(void) {
    struct S s = {7};
    return (g1() == 7 && g2(s) == 7 && g3(s) == 7 && g4() == 9) ? 0 : 1;
}
EOF

# P4 (landed): 6.2.7 composite types. A later declaration can supply the
# length an earlier one omitted; the file-scope path reused the existing Sym
# and never looked at the new type again, so `int a[]; int a[10];` stayed
# incomplete and `sizeof(a)` was rejected. Only an array can gain
# information this way.
runit "P4 composite array types" <<'EOF'
int a[];
int a[10];
extern int b[];
int b[4];
int m[][3];
int m[2][3];
const char s[];
const char s[] = "abc";
static int c[];
static int c[4] = {1, 2, 3, 4};
int e[][2];
int e[3][2] = {{1, 2}, {3, 4}, {5, 6}};
int main(void) {
    if (sizeof(a) != 40 || _Countof(a) != 10) return 1;
    if (sizeof(b) != 16 || _Countof(b) != 4) return 2;
    if (sizeof(m) != 24 || _Countof(m) != 2) return 3;
    if (sizeof(s) != 4 || s[1] != 'b') return 4;
    if (sizeof(c) != 16 || c[3] != 4) return 5;
    if (sizeof(e) != 24 || _Countof(e) != 3) return 6;
    if (e[2][1] != 6) return 7;
    /* the completed object is writable through every declaration */
    a[9] = 42;
    m[1][2] = 7;
    if (a[9] != 42 || m[1][2] != 7) return 8;
    return 0;
}
EOF

# P3 step 1 (landed): 7.26's header, with the version macro of 7.26.14, the
# status type of 7.26.15/7.26.19 and the maximum-output macros of Table 7.8.
# The transcoding functions of 7.26.2 and 7.26.3 are step 2: declaring what
# the implementation cannot define would only move the failure from the
# include to the link.
runit "P3 <stdmchar.h> types and macros" <<'EOF'
#include <stdmchar.h>
#include <stddef.h>
#ifndef __STDC_VERSION_STDMCHAR_H__
#error __STDC_VERSION_STDMCHAR_H__ must come from the header
#endif
_Static_assert(__STDC_VERSION_STDMCHAR_H__ == 202500L, "7.26.14 version");
_Static_assert(STDC_C8_MAX == 4 && STDC_C16_MAX == 2 && STDC_C32_MAX == 1, "table 7.8");
_Static_assert(STDC_MC_MAX == 1 && STDC_MWC_MAX == 1, "table 7.8");
_Static_assert(stdc_mcerr_ok == 0 && stdc_mcerr_invalid == -1 &&
                   stdc_mcerr_incomplete_input == -2 && stdc_mcerr_insufficient_output == -3,
               "7.26.19");
_Static_assert(MB_UTF8 != 0 && WCHAR_UTF32 != 0 && WCHAR_WIDTH == 32, "7.33.1 encodings");
int main(void) {
    stdc_mcerr st = stdc_mcerr_incomplete_input; /* the typedef spelling */
    enum stdc_mcerr en = stdc_mcerr_ok;          /* and the enum one */
    mbstate_t ms = (mbstate_t){0};
    wchar_t wc = L'x';
    size_t sz = sizeof(wchar_t);
    char8_t c8 = u8'x';
    char16_t c16 = u'x';
    char32_t c32 = U'x';
    (void)ms;
    return (st == en + -2 && wc == L'x' && sz == 4 && c8 == 'x' && c16 == 'x' && c32 == 'x') ? 0 : 1;
}
EOF

# --- summary ----------------------------------------------------------
echo
if [ $n_fail -eq 0 ]; then
    echo "c2y: $n_pass passed, $n_gaps known gap(s)"
    exit 0
fi
echo "c2y: $n_pass passed, $n_fail FAILED, $n_gaps known gap(s)"
exit 1
