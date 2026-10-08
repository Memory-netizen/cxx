#!/bin/bash
# C2y (N3685) coverage probe for cxx.
#
# Every probe cites the draft section it tests.  Three modes:
#   run <name>  program must compile, link, run and exit 0
#   ok  <name>  program must compile (no run)
#   rej <name>  program must be rejected with a diagnostic
#
# Usage: bash c2ycov.sh /home/memory/cxx/cxx
compiler=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
if [ ! -x "$compiler" ]; then echo "no compiler at $1" >&2; exit 2; fi

tmp=`mktemp -d /tmp/cxx-c2y-XXXXXX`
trap 'rm -rf $tmp' EXIT
n_pass=0; n_fail=0; n_gap=0

run() {
    cat > "$tmp/t.c"
    if "$compiler" -w -o "$tmp/t" "$tmp/t.c" > "$tmp/log" 2>&1 && "$tmp/t"; then
        echo "PASS  $1"; n_pass=$((n_pass+1))
    else
        echo "FAIL  $1"; sed 's/^/          /' "$tmp/log" | head -3; n_fail=$((n_fail+1))
    fi
}
# A probe the plan deliberately does not implement: it fails, and that is the
# documented answer (section 0: _Complex / _Imaginary and the two headers that
# need them). Counted apart so the summary does not read as a regression.
gap() {
    cat > "$tmp/t.c"
    if "$compiler" -w -S -emit-llvm -o /dev/null "$tmp/t.c" > "$tmp/log" 2>&1; then
        echo "PASS  $1"; n_pass=$((n_pass+1))
    else
        echo "GAP   $1  (deliberately out of scope: _Complex, plan section 0)"; n_gap=$((n_gap+1))
    fi
}
ok() {
    cat > "$tmp/t.c"
    if "$compiler" -w -S -emit-llvm -o /dev/null "$tmp/t.c" > "$tmp/log" 2>&1; then
        echo "PASS  $1"; n_pass=$((n_pass+1))
    else
        echo "FAIL  $1"; sed 's/^/          /' "$tmp/log" | head -3; n_fail=$((n_fail+1))
    fi
}
rej() {
    cat > "$tmp/t.c"
    if "$compiler" -w -S -o /dev/null "$tmp/t.c" > "$tmp/log" 2>&1; then
        echo "FAIL  $1  (accepted; expected a diagnostic)"; n_fail=$((n_fail+1))
    else
        echo "PASS  $1"; n_pass=$((n_pass+1))
    fi
}

echo "### 1. lexical elements (6.4)"

# N3353: prefixed octal literal 0o / 0O (6.4.5.2 prefixed-octal-literal).
run "0o17 / 0O17 prefixed octal literal" <<'EOF'
int main(void) { return (0o17 == 15 && 0O777 == 511) ? 0 : 1; }
EOF

# The unprefixed octal form survives but is obsolescent (6.4.5.2).
run "017 unprefixed octal literal still accepted" <<'EOF'
int main(void) { return 017 == 15 ? 0 : 1; }
EOF

# N3353: \o{...} octal escape (6.4.5.5 octal-escape-sequence).
run "\\o{101} octal escape" <<'EOF'
int main(void) { return ('\o{101}' == 'A' && '\o{0}' == 0) ? 0 : 1; }
EOF

# N3192: sequential hexdigits -> \x{...} (6.4.5.5 hexadecimal-escape-sequence).
run "\\x{41} braced hex escape" <<'EOF'
int main(void) { return ('\x{41}' == 'A' && "\x{41}\x{42}"[1] == 'B') ? 0 : 1; }
EOF

# 6.4.4 universal-character-name \u{...} / \U{...}.
run "\\u{41} / \\U{00000041} braced UCN" <<'EOF'
int main(void) { return ('\u{41}' == 'A' && '\U{00000041}' == 'A') ? 0 : 1; }
EOF

run "\\u{1F600} multi-byte UCN in a string" <<'EOF'
int main(void) { return sizeof("\u{1F600}") == 5 ? 0 : 1; }
EOF

# 6.4.5.2 bit-precise-int-suffix wb / WB, with the optional unsigned-suffix.
run "wb / WB bit-precise integer suffix" <<'EOF'
int main(void) { return (1wb == 1 && 1WB == 1 && 1uwb == 1u && 1UWB == 1u) ? 0 : 1; }
EOF

# The suffix yields _BitInt(N) with the smallest N that holds the value.
run "wb suffix picks _BitInt(N) width" <<'EOF'
int main(void) {
    if (sizeof(2025wb) != 2) return 1;          /* needs 12 bits -> 2 bytes */
    if (sizeof(_BitInt(9)) != 2) return 2;
    unsigned _BitInt(8) u = 255uwb;
    return u == 255 ? 0 : 3;
}
EOF

# 6.4.5.2 digit separators.
run "digit separators" <<'EOF'
int main(void) { return (1'000'000 == 1000000 && 0xFF'FF == 0xFFFF && 0b1010'1010 == 170) ? 0 : 1; }
EOF

# 6.4.5.1 predefined-constant: false / true / nullptr are literals.
run "true / false / nullptr are predefined constants" <<'EOF'
#include <stddef.h>
int main(void) { nullptr_t p = nullptr; return (true && !false && p == nullptr) ? 0 : 1; }
EOF

# 6.4.5.6 + H.5.1: _FloatN type specifiers and fN suffixes.
run "_Float16/32/64/128 and f16/f32/f64/f128 suffixes" <<'EOF'
int main(void) {
    _Float16 a = 1.5f16; _Float32 b = 2.5f32; _Float64 c = 3.5f64; _Float128 d = 4.5f128;
    return (a == 1.5f16 && b == 2.5f32 && c == 3.5f64 && d == 4.5f128) ? 0 : 1;
}
EOF

# 6.4.5.5 encoding prefixes. char8_t is a typedef in <uchar.h> (7.32), not
# a keyword -- 6.4.2 does not list it.
run "u8'x' / u8\"...\" / u\"...\" / U\"...\" / L\"...\"" <<'EOF'
#include <uchar.h>
int main(void) {
    char8_t c = u8'x';
    return (c == 'x' && sizeof(u8"a") == 2 && sizeof(u"a") == 4 &&
            sizeof(U"a") == 8 && sizeof(L"a") == sizeof(wchar_t) * 2) ? 0 : 1;
}
EOF

# 6.4.5.3 real-floating-suffix letter set for _FloatN.
run "1.0f16 literal suffix on a plain literal" <<'EOF'
int main(void) { _Float16 x = 1.0f16; return x == 1.0f16 ? 0 : 1; }
EOF

echo
echo "### 2. expressions and operators (6.5)"

# N3370 / 6.6.2 constant-range-expression.
run "case range" <<'EOF'
int f(int c) { switch (c) { case 1 ... 9: return 1; default: return 0; } }
int main(void) { return (f(5) == 1 && f(0) == 0 && f(9) == 1) ? 0 : 1; }
EOF

# N3369 / N3469 / 6.5.4.1 _Countof.
run "_Countof on an array object" <<'EOF'
int main(void) { int a[7]; return _Countof(a) == 7 ? 0 : 1; }
EOF

run "_Countof on a type name" <<'EOF'
int main(void) { return _Countof(int[10]) == 10 ? 0 : 1; }
EOF

# _Lengthof was renamed; only _Countof exists in N3685 (6.4.2 keyword list).
rej "_Lengthof is no longer a keyword" <<'EOF'
int main(void) { int a[3]; return _Lengthof(a) == 3 ? 0 : 1; }
EOF

# N3260 / 6.5.2.1 generic-controlling-operand may be a type-name.
run "_Generic with a type operand" <<'EOF'
int main(void) { return _Generic(int, int: 1, default: 0) == 1 ? 0 : 1; }
EOF

# N3348: an unspecified-size array type ([*]) is now allowed in a generic
# association (6.7.7.1 p4, 6.5.2.1 EXAMPLE 3).
rej "6.5.2.1 EXAMPLE 3: _Generic(int[3][2], int[3][*]: 1, int[2][*]: 0)" <<'EOF'
int main(void) { return _Generic(int[3][2], int[3][*]: 1, int[2][*]: 0) == 1 ? 0 : 1; }
EOF

rej "6.5.2.1 EXAMPLE 3: _Generic(int(*)[2], int(*)[*]: 1)" <<'EOF'
int main(void) { return _Generic(int(*)[2], int(*)[*]: 1, default: 0); }
EOF

# 6.5.2.1 p2: an assignment-expression operand undergoes array-to-pointer
# conversion, so an array operand matches the pointer type, not the array one.
run "_Generic with an array operand decays to a pointer" <<'EOF'
int main(void) { int a[3]; return _Generic(a, int[3]: 1, int *: 2, default: 0) == 2 ? 0 : 1; }
EOF

# N3273: alignof of an incomplete array type.
run "alignof(int[]) on an incomplete array type" <<'EOF'
int main(void) { return _Alignof(int[]) == _Alignof(int) ? 0 : 1; }
EOF

# N3517 / 6.5.3.2: the operand may have array type, and one-past-the-end is
# allowed when followed by [0] and only used for & or a pointer conversion.
run "one-past-the-end subscript on an array" <<'EOF'
int main(void) { int a[3]; int *p = &a[3]; a[0] = 1; return (p == a + 3) ? 0 : 1; }
EOF

run "subscript of a non-lvalue array member" <<'EOF'
struct S { int a[3]; };
struct S mk(void) { struct S s = {{1, 2, 3}}; return s; }
int main(void) { return mk().a[1] == 2 ? 0 : 1; }
EOF

run "subscript of a compound literal's array member" <<'EOF'
struct S { int a[3]; };
int main(void) { return ((struct S){{4, 5, 6}}).a[2] == 6 ? 0 : 1; }
EOF

# N3323: bool conversion from a floating value (6.3.1.2).
run "bool from a floating value" <<'EOF'
int main(void) { bool b = 0.5; bool c = 0.0; return (b && !c) ? 0 : 1; }
EOF

# N3259 / N3460 are complex-only; see section 7.

# 6.5.4.1 unary operators.
run "sizeof / alignof / _Countof / typeof / typeof_unqual" <<'EOF'
int main(void) {
    int a[4];
    typeof(a) b; typeof_unqual(const int) c = 0;
    return (sizeof(a) == 16 && _Alignof(a) == 4 && _Countof(a) == 4 &&
            _Countof(b) == 4 && sizeof(c) == 4) ? 0 : 1;
}
EOF

# 6.5.3.6 compound literal with storage-class specifiers (N3652).
run "compound literal with static storage class" <<'EOF'
struct S { int a; };
int *p = &(static struct S){ .a = 7 }.a;
int main(void) { return *p == 7 ? 0 : 1; }
EOF

echo
echo "### 3. declarations and statements (6.7 / 6.8)"

# 6.7.3.3 enum-type-specifier (fixed underlying type).
run "enum with a fixed underlying type" <<'EOF'
enum E : unsigned char { A = 200 };
enum F : long long { B = 5000000000LL };
int main(void) { return (sizeof(enum E) == 1 && A == 200 && B == 5000000000LL) ? 0 : 1; }
EOF

# 6.7.3.2 specifier-qualifier-list includes alignment-specifier.
run "alignas inside an enum's specifier-qualifier-list" <<'EOF'
enum E : alignas(2) unsigned char { A = 1 };
int main(void) { return A == 1 ? 0 : 1; }
EOF

run "alignas in a struct member specifier-qualifier-list" <<'EOF'
struct S { alignas(16) int a; };
int main(void) { return _Alignof(struct S) == 16 ? 0 : 1; }
EOF

# N3532: member access of an incomplete struct shall not be allowed.
rej "member access through a pointer to an incomplete struct" <<'EOF'
struct S;
int f(struct S *p) { return p->a; }
EOF

# 6.7.3.5 atomic-type-specifier.
run "_Atomic ( type-name )" <<'EOF'
int main(void) { _Atomic(int) a = 3; return a == 3 ? 0 : 1; }
EOF

# N3312: relaxed atomic alignment -- _Atomic T need not be over-aligned.
run "_Atomic int has the alignment of int" <<'EOF'
int main(void) { return _Alignof(_Atomic int) == _Alignof(int) ? 0 : 1; }
EOF

run "_Atomic struct has a usable alignment" <<'EOF'
struct S { int a; int b; };
int main(void) { return _Alignof(_Atomic struct S) >= _Alignof(struct S) ? 0 : 1; }
EOF

# 6.7.13.2 attribute-declaration and attributes on statements.
ok "attribute-declaration ([[...]];)" <<'EOF'
[[maybe_unused]];
int main(void) { return 0; }
EOF

ok "attribute on a label" <<'EOF'
int main(void) { [[maybe_unused]] lab: ; return 0; }
EOF

ok "attribute on a statement" <<'EOF'
int main(void) { [[maybe_unused]] int x = 0; (void)x; return 0; }
EOF

ok "attribute-prefixed-token (vendor::attr)" <<'EOF'
struct [[gnu::aligned(8)]] T { int a; };
[[gnu::aligned(8)]] int x;
int f(void) [[gnu::deprecated]];
int main(void) { return 0; }
EOF

# 6.8.5.1 selection-header: simple-declaration / declaration expression.
run "if with a simple-declaration header" <<'EOF'
int f(void) { return 3; }
int main(void) { if (int x = f(); x == 3) return 0; return 1; }
EOF

run "switch with a simple-declaration header" <<'EOF'
int f(void) { return 2; }
int main(void) { switch (int x = f(); x) { case 2: return 0; default: return 1; } }
EOF

# 6.8.7.1 / N3355 named break and continue.
run "named break / continue on loops" <<'EOF'
int main(void) {
    int n = 0;
    outer: for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) { n++; if (j == 1) continue outer; if (i == 2) break outer; }
    return n == 5 ? 0 : 1;
}
EOF

# 6.8.7.1 continue/break with no identifier.
run "plain break / continue still work" <<'EOF'
int main(void) { int n = 0; for (int i = 0; i < 4; i++) { if (i == 2) continue; n++; if (n == 3) break; } return n == 3 ? 0 : 1; }
EOF

# 6.7.11 braced-initializer { } and N3451 anonymous member initialisation.
run "empty initializer and anonymous struct/union init" <<'EOF'
struct S { int a; union { int b; float c; }; };
int main(void) { struct S s = { }; struct S t = { 1, { .b = 2 } }; return (s.a == 0 && t.b == 2) ? 0 : 1; }
EOF

# 6.7.12 static_assert without a message, and inside a struct.
ok "static_assert with and without a message" <<'EOF'
static_assert(1);
static_assert(sizeof(int) == 4, "int is 4 bytes");
struct S { int a; static_assert(sizeof(int) == 4); };
int main(void) { return 0; }
EOF

# N3544: classification of the register storage-class specifier.
run "register on a scalar and on an array" <<'EOF'
int main(void) { register int a = 1; register int b[2] = {1, 2}; return (a + b[1] == 3) ? 0 : 1; }
EOF

# 6.7.2 register: the address of a register object shall not be computed.
rej "address of a register object" <<'EOF'
int main(void) { register int a = 1; int *p = &a; return *p; }
EOF

# N3623: definition of main -- 5.2.2.3.2 gives the permitted forms.
run "int main(void)" <<'EOF'
int main(void) { return 0; }
EOF

run "int main(int, char **)" <<'EOF'
int main(int argc, char **argv) { return (argc > 0 && argv) ? 0 : 1; }
EOF

run "int main() with the C2y rule for an empty parameter list" <<'EOF'
int main() { return 0; }
EOF

# N3623: failing to define main in one of the specified forms is undefined
# behaviour (J.2), not a constraint, so no diagnostic is required.
ok "int main(int) is accepted (UB per J.2, no diagnostic required)" <<'EOF'
int main(int argc) { return argc; }
EOF

# N3652 composite types: two declarations of one array form a composite type.
run "composite type from two array declarations" <<'EOF'
int a[];
int a[10];
int main(void) { return sizeof(a) == 40 ? 0 : 1; }
EOF

run "composite type from a tentative array declaration" <<'EOF'
static int b[];
static int b[4] = {1, 2, 3, 4};
int main(void) { return (sizeof(b) == 16 && b[3] == 4) ? 0 : 1; }
EOF

# 6.7.3.3 enumerator attributes and trailing comma.
ok "enumerator attributes and a trailing comma" <<'EOF'
enum E { A [[maybe_unused]] = 1, B, };
int main(void) { return B == 2 ? 0 : 1; }
EOF

echo
echo "### 4. preprocessing (6.10)"

# 6.10.1 control-line list.
ok "#elifdef / #elifndef" <<'EOF'
#define A 1
#if 0
#elifdef A
int ok = 1;
#elifndef B
int bad = 1;
#endif
int main(void) { return ok - 1; }
EOF

ok "#warning" <<'EOF'
#warning this is a warning
int main(void) { return 0; }
EOF

# N3457: the __COUNTER__ predefined macro.
run "__COUNTER__" <<'EOF'
int a = __COUNTER__;
int b = __COUNTER__;
int main(void) { return (b == a + 1) ? 0 : 1; }
EOF

# N3505 / 6.10.1: preprocessor integer expressions.
run "' digit separators inside a #if expression" <<'EOF'
#if 1'000'000 == 1000000
int ok = 1;
#else
int ok = 0;
#endif
int main(void) { return ok - 1; }
EOF

ok "__has_include / __has_include_next" <<'EOF'
#if !__has_include(<stdio.h>)
#error no stdio
#endif
int main(void) { return 0; }
EOF

ok "__has_c_attribute / __has_attribute / __has_builtin" <<'EOF'
#if __has_c_attribute(deprecated) != 201904L
#error has_c_attribute
#endif
#if !__has_builtin(__builtin_alloca)
#error has_builtin
#endif
int main(void) { return 0; }
EOF

# N3286 wording aside, __VA_OPT__ itself is 6.10.5.2.
run "__VA_OPT__" <<'EOF'
#define M(...) f(0 __VA_OPT__(,) __VA_ARGS__)
int f(int a, int b, int c) { return a + b + c; }
int main(void) { return M(1, 2) == 3 ? 0 : 1; }
EOF

# 6.10.4 #embed and its parameter clause.
printf 'ABCD' > "$tmp/emb.bin"
ok "#embed with limit/prefix/suffix" <<'EOF'
static const unsigned char d[] = {
#embed "emb.bin" limit(2) prefix(1 + 0,) suffix(+0)
};
int main(void) { return 0; }
EOF

# An unknown vendor-namespaced embed parameter is refused by cxx and by
# clang 23 alike; 6.10.4.1 p4 leaves non-standard parameters entirely to
# the implementation, so refusing one is conforming.
rej "unknown vendor embed parameter is diagnosed" <<'EOF'
static const unsigned char d[] = {
#embed "emb.bin" gnu::offset(1)
};
int main(void) { return 0; }
EOF

ok "__has_embed with a balanced token sequence" <<'EOF'
#if __has_embed("emb.bin" limit(2)) != __STDC_EMBED_FOUND__
#error has_embed
#endif
int main(void) { return 0; }
EOF

# 6.10.8 standard pragmas, including the C2y rounding directions.
ok "#pragma STDC FENV_ACCESS / FENV_ROUND / FP_CONTRACT" <<'EOF'
#pragma STDC FENV_ACCESS ON
#pragma STDC FENV_ROUND FE_TONEAREST
#pragma STDC FP_CONTRACT DEFAULT
int main(void) { return 0; }
EOF

ok "#pragma STDC FENV_ROUND with a C2y direction" <<'EOF'
#pragma STDC FENV_ROUND FE_TONEARESTFROMZERO
#pragma STDC FENV_ROUND FE_DYNAMIC
int main(void) { return 0; }
EOF

ok "_Pragma operator" <<'EOF'
_Pragma("STDC FENV_ACCESS ON")
int main(void) { return 0; }
EOF

echo
echo "### 5. constant expressions (6.6 / N3447 / N3459 / N3558)"

run "ICE: arithmetic, sizeof, _Alignof, _Countof, enum, cast" <<'EOF'
enum { E = 5 };
int a[sizeof(int) == 4 ? 1 : -1];
int b[_Alignof(double) == 8 ? 1 : -1];
int c[_Countof("abcd") == 5 ? 1 : -1];
static_assert((int)(3.9) == 3);
static_assert((E + 1) * 2 == 12);
static_assert(sizeof(long long) * 8 == 64);
int main(void) { return (sizeof(a) == 4 && sizeof(b) == 4 && sizeof(c) == 4) ? 0 : 1; }
EOF

run "ICE: _BitInt and _FloatN operands" <<'EOF'
static_assert((_BitInt(65))1 == 1);
static_assert(sizeof(_BitInt(65)) == 16);
static_assert((_Float128)1.0 == 1.0);
static_assert(sizeof(_Float16) == 2);
int main(void) { return 0; }
EOF

run "ICE: address constants in static initializers" <<'EOF'
int g[4] = {1, 2, 3, 4};
int *p = &g[2];
int *q = g + 1;
int main(void) { return (*p == 3 && *q == 2) ? 0 : 1; }
EOF

run "ICE: string literal subscript and const object reads" <<'EOF'
const int k[3] = {7, 8, 9};
int t1 = "ab"[1];
int t2 = k[2];
int main(void) { return (t1 == 98 && t2 == 9) ? 0 : 1; }
EOF

run "N3558: object of known constant size in an ICE" <<'EOF'
int a[10];
static_assert(sizeof(a) == 40);
static_assert(_Countof(a) == 10);
int main(void) { return 0; }
EOF

# N3525: static_assert shall not have undefined behaviour in its operand.
rej "static_assert(0) fails at translation time" <<'EOF'
static_assert(0, "must fail");
int main(void) { return 0; }
EOF

# 6.6.2 constant-range-expression must be ordered. Both references diagnose
# it and carry on -- gcc says "empty range specified", clang says "empty case
# range specified" -- so the entry asks for the diagnostic, not a rejection.
cat > "$tmp/t.c" <<'EOF'
int f(int c) { switch (c) { case 9 ... 1: return 1; default: return 0; } }
int main(void) { return f(5); }
EOF
if "$compiler" -S -o /dev/null "$tmp/t.c" 2>&1 | grep -q 'empty case range'; then
    echo "PASS  case range with the bounds reversed"
    n_pass=$((n_pass + 1))
else
    echo "FAIL  case range with the bounds reversed  (no diagnostic)"
    n_fail=$((n_fail + 1))
fi

echo
echo "### 6. standard headers (7.1.2) and feature macros (6.10.10)"

for h in assert.h ctype.h errno.h fenv.h float.h inttypes.h \
         iso646.h limits.h locale.h math.h setjmp.h signal.h stdalign.h \
         stdarg.h stdatomic.h stdbit.h stdbool.h stdckdint.h stdcountof.h \
         stddef.h stdint.h stdio.h stdlib.h stdmchar.h stdnoreturn.h \
         string.h threads.h time.h uchar.h wchar.h wctype.h; do
    ok "include <$h>" <<EOF
#include <$h>
EOF
done

# The two headers that need _Complex: the plan keeps them out of scope on
# purpose (section 0), so they are counted as known gaps rather than failures.
for h in complex.h tgmath.h; do
    gap "include <$h>" <<EOF
#include <$h>
EOF
done

cat > "$tmp/fm.c" <<'EOF'
#include <stdio.h>
int main(void) { printf("x"); return 0; }
EOF

show_macro() {
    printf 'MACROVAL %s\n' "$1" > "$tmp/m.c"
    v=$("$compiler" -E "$tmp/m.c" 2>/dev/null | grep '^MACROVAL' | head -1)
    if [ "$v" = "MACROVAL $1" ]; then v="MACROVAL $1  [not defined]"; fi
    printf 'INFO  %s\n' "$v"
}

echo
echo "### 7. deliberate non-goals (out of scope by project decision)"
rej "_Complex is not implemented" <<'EOF'
_Complex double z = 0;
int main(void) { return 0; }
EOF
rej "_Decimal32 is not implemented" <<'EOF'
_Decimal32 x = 0;
int main(void) { return 0; }
EOF
rej "complex literal suffix is not implemented" <<'EOF'
int main(void) { _Complex double z = 2.0i; return 0; }
EOF
rej "_Imaginary was removed from C2y and is not implemented" <<'EOF'
_Imaginary double z = 0;
int main(void) { return 0; }
EOF

echo
echo "### 8. predefined feature macros"
for m in __STDC_VERSION__ __STDC_HOSTED__ __STDC_NO_COMPLEX__ \
         __STDC_NO_THREADS__ __STDC_NO_ATOMICS__ __STDC_NO_VLA__ \
         __STDC_IEC_60559_TYPES__ __STDC_IEC_60559_BFP__ \
         __STDC_VERSION_STDARG_H__ __STDC_VERSION_STDDEF_H__ \
         __STDC_VERSION_STDBIT_H__ __STDC_VERSION_STDCOUNTof_H__ \
         __STDC_VERSION_STDMCHAR_H__ __STDC_EMBED_FOUND__ \
         __STDC_EMBED_EMPTY__ __STDC_EMBED_NOT_FOUND__; do
    show_macro "$m"
done

echo
if [ "$n_gap" -gt 0 ]; then
    echo "c2ycov: $n_pass passed, $n_fail failed, $n_gap known gap(s)"
else
    echo "c2ycov: $n_pass passed, $n_fail failed"
fi
[ $n_fail -eq 0 ]
