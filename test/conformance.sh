#!/bin/bash
# Conformance regression net: the checks that must hold for cxx to be
# usable as a C compiler against the host's system headers.
#
# Why this file exists: the per-feature suites in test/*.c are harness
# programs compiled without any system header, and test/common is built
# by the host compiler. So `make test` stayed green while cxx could not
# include <stdio.h>, could not define a variadic function, and recursed
# forever on #include_next. Every check below corresponds to a bug that
# shipped past that green suite.
#
# Driver behavior lives in driver.sh, diagnostics in error.sh, LLVM IR in
# ir.sh.
#
# Usage: bash test/conformance.sh ./cxx
compiler=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
if [ ! -x "$compiler" ]; then
    echo "conformance: no compiler at '$1'" >&2
    exit 2
fi

tmp=`mktemp -d /tmp/cxx-conf-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

# Counter names avoid `pass`: bash keeps $? in a variable called `pass`,
# so `pass=$((pass+1))` is overwritten on the next command.
n_pass=0
n_fail=0
n_gaps=0

# Write the program to a file and compile it. The source is read from the
# function's stdin via a redirection at the call site (``ok name <<'EOF'``)
# rather than through a pipe: a pipeline would run the checker in a
# subshell, and every counter increment would be discarded -- an earlier
# draft reported "3 passed" for a 34-check run for exactly that reason.
# Reading from a file also keeps the compiler's exit status unambiguous
# (a pipeline reports the last command's status).
compile() {
    cat > "$tmp/t.c"
    "$compiler" -w -c -o /dev/null "$tmp/t.c" > "$tmp/log" 2>&1
}

# ok <name> -- the program must compile
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

# bad <name> -- the program must be rejected
bad() {
    if compile; then
        echo "testing $1 ... FAILED (expected a diagnostic)"
        n_fail=$((n_fail + 1))
    else
        echo "testing $1 ... passed"
        n_pass=$((n_pass + 1))
    fi
}

# gap <name> -- a known missing feature. Reported but never fatal, and it
# fails loudly once the feature lands, so the entry gets removed then.
gap() {
    if compile; then
        echo "testing $1 ... GAP CLOSED (remove this entry)"
        n_fail=$((n_fail + 1))
    else
        echo "testing $1 ... known gap"
        n_gaps=$((n_gaps + 1))
    fi
}

# --- system headers --------------------------------------------------
# <stdio.h> and friends used to be uncompilable: glibc's floatn-common.h
# writes `typedef float _Float32;` unless the compiler declares a GNU
# version, and cxx declares _FloatN as keywords (C23 H.5.1).
for h in stdio.h stdlib.h math.h string.h limits.h stdint.h inttypes.h \
         ctype.h assert.h time.h wchar.h threads.h stdarg.h stddef.h; do
    ok "include <$h>" <<EOF
#include <$h>
EOF
done

# <stddef.h> must spell wchar_t with the target's character type. It used
# to hard-code `typedef unsigned int wchar_t;`, which is a different type
# from the one the compiler uses for L"..." on every signed-wchar_t target
# (amd64/rv64/rv32), so a wide string initialiser was rejected as
# "array of inappropriate type".
cat <<'EOF' | ok "wchar_t from <stddef.h> matches L\"...\""
#include <stddef.h>
wchar_t w[] = L"abc";
int main(void) { return w[1] == 'b' ? 0 : 1; }
EOF

cat <<'EOF' | ok "wchar_t from <stddef.h> matches L'...'"
#include <stddef.h>
wchar_t c = L'x';
int main(void) { return c == 'x' ? 0 : 1; }
EOF

# stddef.h took its fundamental types from hard-coded LP64 spellings, which
# broke both size and alignment: on ILP32 (rv32) `unsigned long size_t`
# and `long ptrdiff_t` must be 4 bytes, not 8, and `max_align_t` must have
# the alignment of long double (16) rather than that of long.
cat <<'EOF' | ok "size_t and ptrdiff_t have the target's width"
#include <stddef.h>
int main(void) {
    return (sizeof(size_t) == sizeof(void *) &&
            sizeof(ptrdiff_t) == sizeof(void *)) ? 0 : 1;
}
EOF

cat <<'EOF' | ok "max_align_t has the greatest alignment"
#include <stddef.h>
int main(void) { return _Alignof(max_align_t) >= _Alignof(long double) ? 0 : 1; }
EOF

# `#include_next <limits.h>` while its guard sits *outside* the guard it
# re-enters. The search cursor must advance past the directory it just
# took the header from, or the include recurses to MAX_INCL_DEPTH.
ok "limits.h macros are usable" <<'EOF'
#include <limits.h>
#if CHAR_BIT != 8 || INT_MAX < 1 || LONG_MAX < 1
#error limits.h macros unusable
#endif
int main(void) { return 0; }
EOF

# --- GNU compatibility ----------------------------------------------
# __asm__("name") renames the emitted symbol. glibc's __REDIRECT builds
# it as `__asm__ (__ASMNAME ("alias"))`, and __ASMNAME prepends the
# (empty) __USER_LABEL_PREFIX__, so the argument is a run of adjacent
# string literals: "" "alias".
ok "asm-name declaration" <<'EOF'
int f(void) __asm__("real_sym");
int main(void) { return f(); }
EOF

ok "asm-name from adjacent string literals" <<'EOF'
int f(void) __asm__("" "real_sym");
int main(void) { return f(); }
EOF

ok "asm-name on a function definition" <<'EOF'
int f(void) __asm__("real_sym") { return 1; }
int main(void) { return f() - 1; }
EOF

ok "attribute after asm-name (__REDIRECT_NTH order)" <<'EOF'
int f(void) __asm__("g") __attribute__((__nothrow__ , __leaf__));
int main(void) { return f(); }
EOF

# The asm name, not the C identifier, must reach the object file.
cat > "$tmp/sym.c" <<'EOF'
int f(void) __asm__("conf_sym") { return 1; }
int main(void) { return 0; }
EOF
if "$compiler" -w -o "$tmp/sym" "$tmp/sym.c" > "$tmp/log" 2>&1 &&
   nm "$tmp/sym" 2>/dev/null | grep -q ' conf_sym$'; then
    echo "testing asm-name reaches the object file ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing asm-name reaches the object file ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# __extension__ is a no-op marker for pedantic diagnostics cxx does not
# have yet; glibc's <stdlib.h>/<wchar.h> prefix declarations with it.
ok "__extension__ before a declaration" <<'EOF'
__extension__ typedef int T;
int main(void) { T v = 1; return v - 1; }
EOF

# const/noreturn are keywords, so __attribute__((__const__)) needs the
# attribute parser to accept a keyword spelling.
ok "__attribute__((__const__)) with a keyword name" <<'EOF'
int f(void) __attribute__((__const__));
int f(void) { return 1; }
int main(void) { return f() - 1; }
EOF

# glibc spells this with one trailing underscore in bits/byteswap.h.
ok "__inline (single trailing underscore)" <<'EOF'
static __inline unsigned short f(unsigned short x) { return x; }
int main(void) { return f(0); }
EOF

# --- language: nullptr / nullptr_t ----------------------------------
# <stddef.h> must define nullptr_t (C23 7.19.1), and converting to it must
# not emit a conversion: only a null pointer constant or a nullptr_t may
# be converted to nullptr_t, so the value is always null (6.3.2.4).
ok "nullptr_t from <stddef.h>" <<'EOF'
#include <stddef.h>
int main(void) { nullptr_t n = nullptr; return n == nullptr ? 0 : 1; }
EOF

ok "nullptr_t from a null pointer constant" <<'EOF'
#include <stddef.h>
int main(void) { nullptr_t n = 0; return n == nullptr ? 0 : 1; }
EOF

# nullptr_t is a distinct scalar type (6.2.5) that is lowered to an IR
# pointer, so reading it must compare against null, not an integer 0.
cat > "$tmp/np.c" <<'EOF'
#include <stddef.h>
int printf(const char *, ...);
int main(void) {
    nullptr_t n = nullptr;
    int truthy = n ? 1 : 0;
    int b = (bool)n;
    int neg = !n;
    printf("%d %d %d\n", truthy, b, neg);
    return !(truthy == 0 && b == 0 && neg == 1);
}
EOF
if "$compiler" -w -o "$tmp/np" "$tmp/np.c" > "$tmp/log" 2>&1 && "$tmp/np" > /dev/null; then
    echo "testing nullptr_t to bool / logical not ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing nullptr_t to bool / logical not ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# 6.3.2.4: nullptr_t converts only to void, bool or a pointer type.
bad "reject nullptr_t to a non-pointer, non-bool type" <<'EOF'
#include <stddef.h>
int main(void) { nullptr_t n = nullptr; return (int)(long)n; }
EOF

# --- language: __typeof alias ---------------------------------------
# <stdbit.h> expands stdc_bit_floor/ceil to `(__typeof (x)) ...`.
ok "__typeof without parentheses" <<'EOF'
int main(void) { int x = 1; __typeof(x) y = 2; return y - 2; }
EOF

# --- language: named loops ------------------------------------------
ok "named continue to a loop label" <<'EOF'
int main(void) { int n = 0; w: while (n < 3) { n++; if (n == 2) continue w; } return n - 3; }
EOF

ok "named break to a do-while label" <<'EOF'
int main(void) { int n = 0; d: do { n++; if (n == 2) break d; } while (n < 5); return n - 2; }
EOF

ok "named break to a switch label" <<'EOF'
int main(void) { int n = 0; sw: switch (n) { case 0: n = 1; break sw; } return n - 1; }
EOF

bad "reject break to a non-loop label" <<'EOF'
int main(void) { int n = 0; for (int i = 0; i < 2; ++i) blk: { if (i == 1) break blk; n++; } return n; }
EOF

# --- preprocessor ----------------------------------------------------
# __VA_OPT__ itself is covered in depth by test/macro.c; what this net
# adds is the interaction with the glibc-spelled headers below.
ok "__has_include" <<'EOF'
#if __has_include(<stdio.h>)
int main(void) { return 0; }
#else
#error no stdio
#endif
EOF

ok "__has_include_next and __has_builtin" <<'EOF'
#if !__has_include_next(<limits.h>)
#error __has_include_next must see the next limits.h
#endif
#if !__has_builtin(__builtin_alloca)
#error __has_builtin
#endif
int main(void) { return 0; }
EOF

# --- constant folding of call arguments -------------------------------
# opt_ast.c's ND_FUNCALL case used to call fold_node() on each argument and
# discard the replacement node, so *no* argument of *any* call was ever
# folded: f(2 + 3) kept the add for the backend. Silent, because the IR is
# still correct -- just unoptimised. Check the folded form reaches the IR.
cat > "$tmp/argfold.c" <<'EOF'
int f(int, int, int);
int g(int x) { return f(2 + 3, x, 4 * 5); }
EOF
if "$compiler" -w -emit-llvm -S -o "$tmp/argfold.ll" "$tmp/argfold.c" > "$tmp/log" 2>&1 &&
   grep -q 'call i32 @f(i32 5, i32 %[0-9]*, i32 20)' "$tmp/argfold.ll" &&
   ! grep -q 'mul i32' "$tmp/argfold.ll"; then
    echo "testing folding of call arguments ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing folding of call arguments ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    grep -n 'call i32 @f\|mul i32' "$tmp/argfold.ll" 2>/dev/null | sed 's/^/    /' | head -4
    n_fail=$((n_fail + 1))
fi

# Folding arguments must not disturb the argument list: a partial fold has
# to keep every argument, in order.
cat > "$tmp/argkeep.c" <<'EOF'
int f(int, int, int, int, int);
int g(int a) { return f(1 + 1, a, 3 + 3, a, 5 + 5); }
EOF
if "$compiler" -w -emit-llvm -S -o "$tmp/argkeep.ll" "$tmp/argkeep.c" > "$tmp/log" 2>&1 &&
   grep -qE 'call i32 @f\(i32 2, i32 %[0-9]+, i32 6, i32 %[0-9]+, i32 10\)' "$tmp/argkeep.ll"; then
    echo "testing call arguments survive folding ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing call arguments survive folding ... FAILED"
    grep -n 'call i32 @f' "$tmp/argkeep.ll" 2>/dev/null | sed 's/^/    /' | head -3
    n_fail=$((n_fail + 1))
fi

# #embed is C23 and glibc-free, but a regression here breaks every
# header that uses it, so keep one end-to-end check.
printf 'ABCD' > "$tmp/emb.bin"
cat > "$tmp/emb.c" <<'EOF'
static const unsigned char d[] = {
#embed "emb.bin"
};
int main(void) { return d[0] == 'A' ? 0 : 1; }
EOF
if "$compiler" -w -c -o /dev/null "$tmp/emb.c" > "$tmp/log" 2>&1; then
    echo "testing #embed of a resource ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing #embed of a resource ... FAILED"
    n_fail=$((n_fail + 1))
fi

# --- end to end ------------------------------------------------------
# The whole point of the header work: an ordinary C program that uses
# stdio and stdlib must compile, link and run.
cat > "$tmp/e2e.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    char *p = malloc(32);
    if (!p) return 2;
    strcpy(p, "cxx");
    int n = printf("%s %d\n", p, 42);
    free(p);
    return n > 0 ? 0 : 1;
}
EOF
if "$compiler" -w -o "$tmp/e2e" "$tmp/e2e.c" > "$tmp/log" 2>&1 &&
   "$tmp/e2e" > "$tmp/e2e.out" 2>&1 &&
   grep -q 'cxx 42' "$tmp/e2e.out"; then
    echo "testing printf/malloc end to end ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing printf/malloc end to end ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# Two C identifiers may denote one object-file symbol. glibc's <stdlib.h>
# redirects both strtoq and strtoll to __isoc23_strtoll via __REDIRECT;
# emitting one declaration per identifier declares the same symbol twice,
# which LLVM rejects ("invalid redefinition of function").
cat > "$tmp/alias.c" <<'EOF'
int real_fn(void) { return 42; }
extern int alias_a(void) __asm__("real_fn");
extern int alias_b(void) __asm__("real_fn");
int main(void) { return alias_a() + alias_b() - 84; }
EOF
if "$compiler" -w -o "$tmp/alias" "$tmp/alias.c" > "$tmp/log" 2>&1 &&
   "$tmp/alias"; then
    echo "testing two identifiers sharing one asm name ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing two identifiers sharing one asm name ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- variadic arguments ----------------------------------------------
# Default argument promotions (6.5.2.2p7) for the types narrower than int.
# These used to be a no-op: integer_promotion() wrapped the operand in an
# ND_IMCAST before lvalue_convert(), which dropped the is_lvalue flag, so
# the load never happened and the cast was applied to the variable's
# address ("invalid cast opcode for cast from 'ptr'"). Passing a char or
# short to printf was impossible, and invisible while test/common was the
# only caller (it is built by the host compiler).
cat > "$tmp/promote.c" <<'EOF'
#include <stdio.h>
int main(void) {
    char c = 'A';
    signed char sc = -2;
    unsigned char uc = 0xAB;
    short s = -300;
    unsigned short us = 0xBEEF;
    float f = 1.5f;
    int n = printf("%c %d %02X %d %04X %.1f\n", c, sc, uc, s, us, (double)f);
    return n > 0 ? 0 : 1;
}
EOF
if "$compiler" -w -o "$tmp/promote" "$tmp/promote.c" > "$tmp/log" 2>&1 &&
   "$tmp/promote" | grep -q 'A -2 AB -300 BEEF 1.5'; then
    echo "testing variadic default argument promotions ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing variadic default argument promotions ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# __builtin_bswap* lower to ND_BSWAP -> llvm.bswap.iN. glibc's
# <bits/byteswap.h> needs them once __GNUC__ >= 7, and <stdlib.h> pulls
# that header in.
cat > "$tmp/bswap.c" <<'EOF'
int printf(const char *, ...);
int main(void) {
    unsigned short a = __builtin_bswap16(0x1234);
    unsigned int   b = __builtin_bswap32(0x12345678u);
    unsigned long  c = __builtin_bswap64(0x123456789abcdef0ul);
    if (a != 0x3412 || b != 0x78563412u || c != 0xf0debc9a78563412ul) return 1;
    printf("%04x %08x %016lx\n", a, b, c);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/bswap" "$tmp/bswap.c" > "$tmp/log" 2>&1 &&
   "$tmp/bswap" | grep -q '3412 78563412 f0debc9a78563412'; then
    echo "testing __builtin_bswap16/32/64 ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing __builtin_bswap16/32/64 ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- string literal subscripts as constants ---------------------------
# A subscript of a string literal is usable in a static initializer: the
# translator knows the bytes. clang accepts it (C23 6.6) while a subscript
# of a non-constexpr array is still rejected, so this is not the same rule.
# eval2 had no path for it and reported "invalid initializer".
cat > "$tmp/strsub.c" <<'EOF'
int t1 = "ab"[1];
int t2 = L"ab"[1];
int t3 = "abc"[2];
int t4 = L"\xffff"[0];
int main(void) { return (t1 == 98 && t2 == 98 && t3 == 99 && t4 == 65535) ? 0 : 1; }
EOF
if "$compiler" -w -o "$tmp/strsub" "$tmp/strsub.c" > "$tmp/log" 2>&1 && "$tmp/strsub"; then
    echo "testing string literal subscript as a constant ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing string literal subscript as a constant ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# The same rule must stay closed for a plain array, so that this does not
# silently become "any subscript folds".
cat <<'EOF' | bad "reject a non-constexpr array subscript as a constant"
int arr[2] = {7, 8};
int t = arr[1];
EOF

cat <<'EOF' | bad "reject an out-of-range string subscript"
int t = "ab"[2];
EOF

# A const-qualified object may be read as a constant (clang accepts it);
# only an *unqualified* array stays rejected. The qualifier of an array
# lives on its innermost element type, not on the array type.
cat <<'EOF' | ok "accept a const array subscript as a constant"
const int arr[2] = {7, 8};
int t = arr[1];
int main(void) { return t == 8 ? 0 : 1; }
EOF

cat <<'EOF' | bad "reject an unqualified array subscript as a constant"
int arr[2] = {7, 8};
int t = arr[1];
EOF

cat <<'EOF' | ok "accept a constexpr array subscript as a constant"
constexpr int arr[2] = {7, 8};
int t = arr[1];
int main(void) { return t == 8 ? 0 : 1; }
EOF

# Multi-dimensional subscripts and member chains: the access chain is
# collected outer-first (m[1][0] parses as (m[1])[0]) but the initializer
# tree descends inner-first, so the indices must be replayed in reverse.
cat <<'EOF' | ok "fold a multi-dimensional const array subscript"
const int m[2][2] = {{1, 2}, {3, 4}};
const int c[2][2][2] = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};
int a = m[1][0];
int b = c[1][0][1];
int main(void) { return (a == 3 && b == 6) ? 0 : 1; }
EOF

cat <<'EOF' | ok "fold a const member chain"
struct T { int a[3]; };
const struct T g = {{4, 5, 6}};
struct S { int a; };
const struct S s[2] = {{71}, {82}};
int x = g.a[2];
int y = s[1].a;
int main(void) { return (x == 6 && y == 82) ? 0 : 1; }
EOF

# The access chain is held in a dynamically grown array, so nesting is
# bounded by memory rather than by a fixed path buffer.
cat <<'EOF' | ok "fold a deeply nested const access chain"
struct L0 { int v; };
struct L1 { struct L0 a; };
struct L2 { struct L1 a; };
struct L3 { struct L2 a; };
struct L4 { struct L3 a; };
struct L5 { struct L4 a; };
struct L6 { struct L5 a; };
struct L7 { struct L6 a; };
struct L8 { struct L7 a; };
const struct L8 g = {{{{{{{{42}}}}}}}};
const int m[2][2][2][2][2][2] = {{{{{{7}}}}}};
int x = g.a.a.a.a.a.a.a.a.v;
int y = m[1][1][1][1][1][0];
int main(void) { return (x == 42 && y == 0) ? 0 : 1; }
EOF

# An implicitly initialised subobject of a const object is still a
# constant: 6.6 needs the object's value, and an omitted initialiser
# leaves it zero. Rejecting it would be wrong (clang accepts and folds 0).
cat <<'EOF' | ok "fold implicitly zero-initialised const members"
const int a[4] = {1, 2};
struct S { int x, y; };
const struct S s = {5};
const int m[2][2] = {{7}};
int t1 = a[3];
int t2 = s.y;
int t3 = m[1][1];
int main(void) { return (t1 == 0 && t2 == 0 && t3 == 0) ? 0 : 1; }
EOF

# const struct / union members, including nesting.
cat <<'EOF' | ok "fold const struct and union members"
struct S { int a[3]; };
const struct S s = {{1, 2, 3}};
union U { int a; char b; };
const union U u = {42};
struct O { struct I { int x; } i; };
const struct O o = {{9}};
int x = s.a[1];
int y = u.a;
int z = o.i.x;
int main(void) { return (x == 2 && y == 42 && z == 9) ? 0 : 1; }
EOF

# Regression: an address taken through a member that *is* an array was
# accepted before and must stay accepted (test/initializer.c relies on it).
cat <<'EOF' | ok "accept an address through an array member"
struct T { struct S { int a[3]; } a; };
struct T g = {{{1, 2, 3}}};
int *p = g.a.a;
int main(void) { return p[0] == 1 ? 0 : 1; }
EOF

# --- known gaps ------------------------------------------------------
# Listed so the gap shows up in the suite instead of passing silently.
gap "<stdbit.h> helpers (needs __builtin_clz*)" <<'EOF'
#include <stdbit.h>
int main(void) { return (int)stdc_leading_zeros(1u); }
EOF

gap "variadic function definition (needs va_start/va_arg)" <<'EOF'
#include <stdarg.h>
int sum(int n, ...) { va_list ap; va_start(ap, n); int s = va_arg(ap, int); va_end(ap); return s; }
int main(void) { return sum(1, 5) - 5; }
EOF

# --- summary ---------------------------------------------------------
echo
if [ $n_fail -eq 0 ]; then
    echo "conformance: $n_pass passed, $n_gaps known gap(s)"
    exit 0
fi
echo "conformance: $n_pass passed, $n_fail FAILED, $n_gaps known gap(s)"
exit 1
