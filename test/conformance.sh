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

# Warning groups: each diagnostic belongs to one, and -Wno-<group> turns
# that group off. Before the flags existed the driver accepted every -W* and
# discarded it, so -Wno-deprecated-declarations was accepted but changed
# nothing.
cat > "$tmp/wno.c" <<'EOF'
__attribute__((deprecated)) void old_fn(void);
int main(void) { old_fn(); return 0; }
EOF
warned=$("$compiler" -S -o /dev/null "$tmp/wno.c" 2>&1 | grep -c deprecated)
quiet=$("$compiler" -Wno-deprecated-declarations -S -o /dev/null "$tmp/wno.c" 2>&1 | grep -c deprecated)
back=$("$compiler" -Wno-deprecated-declarations -Wdeprecated-declarations -S -o /dev/null "$tmp/wno.c" 2>&1 | grep -c deprecated)
if [ "$warned" -eq 1 ] && [ "$quiet" -eq 0 ] && [ "$back" -eq 1 ]; then
    echo "testing -Wno-<group> silences its group ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -Wno-<group> silences its group ... FAILED"
    echo "    deprecated warnings: default $warned, -Wno-deprecated-declarations $quiet, re-enabled $back"
    n_fail=$((n_fail + 1))
fi

# Groups are independent: silencing one must not silence another.
cat > "$tmp/wgrp.c" <<'EOF'
int f(int x) __attribute__((bogus_attribute_name));
int main(void) { return f(1); }
EOF
with_no=$("$compiler" -Wno-deprecated-declarations -S -o /dev/null "$tmp/wgrp.c" 2>&1 | grep -c 'unknown attribute')
with_attr=$("$compiler" -Wno-attributes -S -o /dev/null "$tmp/wgrp.c" 2>&1 | grep -c 'unknown attribute')
if [ "$with_no" -eq 1 ] && [ "$with_attr" -eq 0 ]; then
    echo "testing warning groups are independent ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing warning groups are independent ... FAILED"
    echo "    unknown-attribute warnings: -Wno-deprecated-declarations $with_no, -Wno-attributes $with_attr"
    n_fail=$((n_fail + 1))
fi

# An unrecognized group is an error, as it is in gcc and clang. Accepting
# every -W* and discarding it is how a typo used to pass unnoticed.
if "$compiler" -Wno-bogus-option -S -o /dev/null "$tmp/wno.c" > "$tmp/log" 2>&1; then
    echo "testing an unknown -W<group> is rejected ... FAILED (accepted)"
    n_fail=$((n_fail + 1))
elif grep -q 'unknown warning group' "$tmp/log"; then
    echo "testing an unknown -W<group> is rejected ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing an unknown -W<group> is rejected ... FAILED (wrong diagnostic)"
    head -2 "$tmp/log" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# The spellings a build system passes must keep working.
cat <<'EOF' | ok "-Wall, -Wextra and -w are accepted"
int main(void) { return 0; }
EOF

# `$` in identifiers is a GNU extension: cxx accepts it in the default mode,
# and D3 will make -pedantic reject it. Until then -pedantic is not even a
# recognised option, so only the default-mode behaviour is asserted.
# TODO(D3): `-pedantic` must reject `$` in an identifier.
cat > "$tmp/dollar.c" <<'EOF'
int main(void) { int a$b = 1; return a$b - 1; }
EOF
if "$compiler" -w -o "$tmp/dollar" "$tmp/dollar.c" > "$tmp/log" 2>&1 && "$tmp/dollar"; then
    echo "testing \$ in an identifier accepted by default ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing \$ in an identifier accepted by default ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi
if "$compiler" -pedantic -S -o /dev/null "$tmp/dollar.c" > /dev/null 2>&1; then
    echo "testing -pedantic rejects \$ ... FAILED (accepted)"
    n_fail=$((n_fail + 1))
else
    echo "testing -pedantic rejects \$ ... passed"
    n_pass=$((n_pass + 1))
fi

# -pedantic also warns on the GNU constructs cxx accepts, and
# -pedantic-errors stops on the first of them. One diagnostic per construct:
# a statement expression, `?:` with the middle operand omitted, a computed
# goto, __FUNCTION__ and the address of a label.
cat > "$tmp/gnu.c" <<'EOF'
int stmt(void) { return ({ int x = 1; x + 1; }); }
int cond(int x) { return x ?: 7; }
void cg(void *p) { goto *p; }
const char *fn(void) { return __FUNCTION__; }
void *lv(void) { l: return &&l; }
int main(void) { return stmt() - 2 + cond(1) + (fn() != 0) + (lv() != 0); }
EOF
ped=$("$compiler" -pedantic -S -o /dev/null "$tmp/gnu.c" 2>&1 | grep -c 'ISO C')
off=$("$compiler" -S -o /dev/null "$tmp/gnu.c" 2>&1 | grep -c 'ISO C')
nowarn=$("$compiler" -w -pedantic -S -o /dev/null "$tmp/gnu.c" 2>&1 | grep -c 'ISO C')
if [ "$ped" -eq 5 ] && [ "$off" -eq 0 ] && [ "$nowarn" -eq 0 ]; then
    echo "testing -pedantic names each GNU construct ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -pedantic names each GNU construct ... FAILED (pedantic $ped/5, default $off, -w $nowarn)"
    n_fail=$((n_fail + 1))
fi

# -pedantic-errors turns the first of them into an error and stops, and -w
# inhibits the mode as a whole -- gcc and clang stay silent for
# `-w -pedantic` and for `-w -pedantic-errors` alike.
err=$("$compiler" -pedantic-errors -S -o /dev/null "$tmp/gnu.c" 2>&1 | grep -c 'error:')
wnoerr=$("$compiler" -w -pedantic-errors -S -o /dev/null "$tmp/gnu.c" 2>&1 | grep -c 'error:')
if [ "$err" -eq 1 ] && [ "$wnoerr" -eq 0 ]; then
    echo "testing -pedantic-errors stops on a GNU construct ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -pedantic-errors stops on a GNU construct ... FAILED (errors $err, -w $wnoerr)"
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
   grep -q 'call i32 @f(i32 5, i32 %tmp[0-9]*, i32 20)' "$tmp/argfold.ll" &&
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
   grep -qE 'call i32 @f\(i32 2, i32 %tmp[0-9]+, i32 6, i32 %tmp[0-9]+, i32 10\)' "$tmp/argkeep.ll"; then
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

# #embed prefix/suffix join directly onto the bytes, and the bytes are
# spelled in decimal, so a prefix only forms a usable constant when
# something separates it from them -- `prefix(0x)` would need `0x` followed
# by a decimal byte, which no compiler accepts (gcc, clang and cxx all
# reject `0x 99` as C). The forms that must work are the ones whose prefix
# ends in a separator or an operator.
printf 'c' > "$tmp/pfx.bin"
cat > "$tmp/pfx.c" <<'EOF'
static const unsigned char d[] = {
#embed "pfx.bin" prefix(0x63 + 0,) suffix()
};
int main(void) { return d[0] == 0x63 ? 0 : 1; }
EOF
if "$compiler" -w -o "$tmp/pfx" "$tmp/pfx.c" > "$tmp/log" 2>&1 && "$tmp/pfx"; then
    echo "testing #embed prefix that ends in a separator ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing #embed prefix that ends in a separator ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
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

# A library named on the command line must reach the linker *after* the
# object files: `cc -o out -lm tmp.o` links, but the archive contributes
# nothing because no symbol is undefined yet when it is read. -l/-Wl, used
# to be replayed into the object-file list, so every program calling a libm
# function failed with "undefined reference to `sqrt'".
cat > "$tmp/libm.c" <<'EOF'
#include <math.h>
int main(void) { return sqrt(4.0) == 2.0 ? 0 : 1; }
EOF
if "$compiler" -w -lm -o "$tmp/libm" "$tmp/libm.c" > "$tmp/log" 2>&1 && "$tmp/libm"; then
    echo "testing -lm links after the object files ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -lm links after the object files ... FAILED"
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

# --- debug dumpers ----------------------------------------------------
# These two are the debugging infrastructure, so guard the properties that
# make them useful rather than their exact text: -raw-dump-tokens was
# advertised in the usage text but the option was spelled differently in
# the argument parser, so it never worked, and -ast-dump printed neither
# source locations nor the newer node kinds.
cat > "$tmp/dump.c" <<'EOF'
int f(int n) {
    int a[2] = {1, 2};
    return a[n];
}
EOF

if "$compiler" -raw-dump-tokens -c -o /dev/null "$tmp/dump.c" > "$tmp/rtok" 2>&1 &&
   grep -q 'identifier' "$tmp/rtok"; then
    echo "testing -raw-dump-tokens ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -raw-dump-tokens ... FAILED"
    sed 's/^/    /' "$tmp/rtok" | head -3
    n_fail=$((n_fail + 1))
fi

# -dump-tokens must attribute macro-expanded tokens to their spelling.
cat > "$tmp/mac.c" <<'EOF'
#define M(x) ((x) + 1)
int f(int n) { return M(n); }
EOF
if "$compiler" -dump-tokens -c -o /dev/null "$tmp/mac.c" > "$tmp/mtok" 2>&1 &&
   grep -q 'Spelling=' "$tmp/mtok"; then
    echo "testing -dump-tokens shows macro spelling ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -dump-tokens shows macro spelling ... FAILED"
    n_fail=$((n_fail + 1))
fi

# Every node kind must have a name and a source location.
if "$compiler" -ast-dump -c -o /dev/null "$tmp/dump.c" > "$tmp/ast" 2>&1 &&
   ! grep -q 'unknown' "$tmp/ast" &&
   grep -q 'SUBACCESS' "$tmp/ast" &&
   grep -q 'Loc=<' "$tmp/ast"; then
    echo "testing -ast-dump names, locations and coverage ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -ast-dump names, locations and coverage ... FAILED"
    grep -c 'unknown' "$tmp/ast" 2>/dev/null | sed 's/^/    unknown nodes: /'
    n_fail=$((n_fail + 1))
fi

# Array types print their dimensions in declarator order, and each
# dimension shows the length as written -- including VLA expressions, whose
# text comes back from the source rather than from a single AST token.
cat > "$tmp/arr.c" <<'EOF'
void f(int n, int m) {
    int c[3][4];
    int v[n + 1];
    int w[n][m];
    int (*p)[n][m];
    (void)c; (void)v; (void)w; (void)p; (void)m;
}
EOF
if "$compiler" -ast-dump -c -o /dev/null "$tmp/arr.c" > "$tmp/arr.out" 2>&1 &&
   grep -q 'c: int\[3\]\[4\]' "$tmp/arr.out" &&
   grep -q 'v: int\[n + 1\]' "$tmp/arr.out" &&
   grep -q 'w: int\[n\]\[m\]' "$tmp/arr.out" &&
   grep -q 'p: int (\*)\[n\]\[m\]' "$tmp/arr.out"; then
    echo "testing -ast-dump array dimensions in declarator order ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -ast-dump array dimensions in declarator order ... FAILED"
    grep -E '^    (c|v|w|p):' "$tmp/arr.out" 2>/dev/null | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# Type qualifiers must survive the dump: they were dropped entirely before.
cat > "$tmp/qual.c" <<'EOF'
void g(void) {
    const int a;
    volatile int b;
    _Atomic int c;
    const int *d;
    int *restrict e;
    const int f[2];
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
}
EOF
if "$compiler" -ast-dump -c -o /dev/null "$tmp/qual.c" > "$tmp/qual.out" 2>&1 &&
   grep -q 'a: const int' "$tmp/qual.out" &&
   grep -q 'b: volatile int' "$tmp/qual.out" &&
   grep -q 'c: _Atomic int' "$tmp/qual.out" &&
   grep -q 'd: const int \*' "$tmp/qual.out" &&
   grep -q 'e: int \*restrict' "$tmp/qual.out" &&
   grep -q 'f: const int\[2\]' "$tmp/qual.out"; then
    echo "testing -ast-dump type qualifiers ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -ast-dump type qualifiers ... FAILED"
    sed -n '/locals:/,/body:/p' "$tmp/qual.out" 2>/dev/null | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# --- builtins: declaration mode ---------------------------------------
# bswap was previously lowered at parse time to a dedicated ND_BSWAP node,
# which left it without a type or an address. It is now an ordinary
# implicitly declared function, so the whole normal call path applies.
cat > "$tmp/bs.c" <<'EOF'
int a = __builtin_bswap32(0x11223344);
int f(unsigned x) { return __builtin_bswap32(x); }
EOF
if "$compiler" -S -emit-llvm -o "$tmp/bs.ll" "$tmp/bs.c" 2>"$tmp/bs.err" &&
   grep -q 'llvm.bswap.i32' "$tmp/bs.ll"; then
    echo "testing __builtin_bswap32 lowers to llvm.bswap.i32 ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing __builtin_bswap32 lowers to llvm.bswap.i32 ... FAILED"
    head -3 "$tmp/bs.err" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# A constant argument is folded, including one written through the
# implicit conversion the parameter type triggers.
if grep -qE '^@a = .*i32 1144201745' "$tmp/bs.ll"; then
    echo "testing __builtin_bswap32 folds a constant argument ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing __builtin_bswap32 folds a constant argument ... FAILED"
    grep -E '^@a ' "$tmp/bs.ll" 2>/dev/null | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# The declaration mode gives a builtin an address, which the old dedicated
# node could not.
cat > "$tmp/bsp.c" <<'EOF'
int (*p)(unsigned) = __builtin_bswap32;
int f(unsigned x) { return p(x); }
EOF
if "$compiler" -w -S -emit-llvm -o "$tmp/bsp.ll" "$tmp/bsp.c" 2>"$tmp/bsp.err"; then
    echo "testing a builtin has an address ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a builtin has an address ... FAILED"
    head -3 "$tmp/bsp.err" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# A user declaration wins over the injected one.
cat > "$tmp/bsu.c" <<'EOF'
unsigned __builtin_bswap32(unsigned x) { return x; }
int f(void) { return (int)__builtin_bswap32(1); }
EOF
if "$compiler" -w -S -emit-llvm -o "$tmp/bsu.ll" "$tmp/bsu.c" 2>"$tmp/bsu.err"; then
    echo "testing a user declaration overrides a builtin ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a user declaration overrides a builtin ... FAILED"
    head -3 "$tmp/bsu.err" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# The implicit declaration goes to file scope and is looked up through
# enclosing scopes, so repeated use inside nested blocks must not declare
# again or conflict with itself.
cat > "$tmp/bss.c" <<'EOF'
int f(unsigned x) {
    int r = __builtin_bswap32(x);
    { int y = __builtin_bswap32(x); r += y; }
    for (int i = 0; i < 1; i++) r += __builtin_bswap32(x);
    return r;
}
int g(unsigned x) { return __builtin_bswap32(x) + 1; }
EOF
if "$compiler" -w -S -emit-llvm -o "$tmp/bss.ll" "$tmp/bss.c" 2>"$tmp/bss.err" &&
   [ "$(grep -c 'llvm.bswap.i32' "$tmp/bss.ll")" = "4" ]; then
    echo "testing a builtin resolves across nested scopes ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a builtin resolves across nested scopes ... FAILED"
    head -3 "$tmp/bss.err" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# A user declaration under a builtin's name makes it an ordinary function.
# Identification must be "is this the Sym we injected", not "does the name
# look like a builtin", or the call is lowered to the LLVM intrinsic even
# though the user declared a function.
cat > "$tmp/bsd.c" <<'EOF'
unsigned __builtin_bswap32(unsigned);
int f(unsigned x) { return (int)__builtin_bswap32(x); }
EOF
if "$compiler" -S -emit-llvm -o "$tmp/bsd.ll" "$tmp/bsd.c" 2>"$tmp/bsd.err" &&
   ! grep -q 'llvm.bswap' "$tmp/bsd.ll" &&
   grep -q 'call i32 @__builtin_bswap32' "$tmp/bsd.ll"; then
    echo "testing a same-named user declaration is not a builtin ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a same-named user declaration is not a builtin ... FAILED"
    grep -E 'llvm.bswap|call i32 @__builtin' "$tmp/bsd.ll" 2>/dev/null | sed 's/^/    /'
    head -3 "$tmp/bsd.err" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# Defining a function under a builtin's name is a redefinition, not a
# silent replacement of the builtin's body. (clang rejects it as
# "definition of builtin function"; matching that diagnostic is better
# than accepting the program and discarding the definition.)
cat > "$tmp/bsdf.c" <<'EOF'
int f(unsigned x) { return (int)__builtin_bswap32(x); }
unsigned __builtin_bswap32(unsigned x) { return x; }
EOF
if "$compiler" -c -o /dev/null "$tmp/bsdf.c" > "$tmp/bsdf.err" 2>&1; then
    echo "testing redefining a builtin is rejected ... FAILED (accepted)"
    n_fail=$((n_fail + 1))
elif grep -q 'definition of builtin function' "$tmp/bsdf.err"; then
    echo "testing redefining a builtin is rejected ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing redefining a builtin is rejected ... FAILED (wrong diagnostic)"
    head -2 "$tmp/bsdf.err" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# --- known gaps ------------------------------------------------------
# Listed so the gap shows up in the suite instead of passing silently.
# Was a gap until two things landed: the bit-counting builtins, and
# widening a shift's amount in irgen (6.5.7 promotes the operands
# separately, so their widths may differ, but LLVM needs one type).
ok "<stdbit.h> helpers" <<'EOF'
#include <stdbit.h>
int main(void) {
    if (stdc_leading_zeros(1u) != 31) return 1;
    if (stdc_trailing_zeros(8u) != 3) return 2;
    if (stdc_count_ones(0xf0f0u) != 8) return 3;
    return 0;
}
EOF

# A shift's operands are promoted separately, so a narrow amount next to a
# wide left operand has to be widened before the IR shift.
cat > "$tmp/shiftamt.c" <<'EOF'
int n(int);
unsigned long long g(int x) { return 1ull << n(x); }
int m(unsigned char c) { return 1 << c; }
EOF
"$compiler" -S -emit-llvm -o "$tmp/shiftamt.ll" "$tmp/shiftamt.c" 2>"$tmp/shiftamt.err"
if [ -f "$tmp/shiftamt.ll" ] &&
   grep -qE 'shl i64' "$tmp/shiftamt.ll" &&
   grep -qE 'shl i32' "$tmp/shiftamt.ll"; then
    echo "testing a shift amount is widened for the IR shift ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a shift amount is widened for the IR shift ... FAILED"
    grep -E 'shl |sext|zext' "$tmp/shiftamt.ll" 2>/dev/null | sed 's/^/    /'
    head -3 "$tmp/shiftamt.err" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# The bit-counting builtins return int whatever the operand width, so a
# wide operand computes at its own width and truncates, and a narrow one is
# promoted first -- both visible in the IR.
cat > "$tmp/bitcnt.c" <<'EOF'
int a(unsigned x) { return __builtin_clz(x); }
int b(unsigned long x) { return __builtin_clzl(x); }
int c(unsigned long long x) { return __builtin_popcountll(x); }
int d(unsigned char x) { return __builtin_popcount(x); }
EOF
if "$compiler" -S -emit-llvm -o "$tmp/bitcnt.ll" "$tmp/bitcnt.c" 2>"$tmp/bitcnt.err" &&
   grep -q 'llvm.ctlz.i32' "$tmp/bitcnt.ll" &&
   grep -q 'llvm.ctlz.i64' "$tmp/bitcnt.ll" &&
   grep -q 'llvm.ctpop.i64' "$tmp/bitcnt.ll" &&
   grep -q 'trunc i64' "$tmp/bitcnt.ll" &&
   grep -q 'zext i8' "$tmp/bitcnt.ll" &&
   grep -q 'i1 1' "$tmp/bitcnt.ll"; then
    echo "testing bit-counting builtins lower to llvm.ctlz/cttz/ctpop ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing bit-counting builtins lower to llvm.ctlz/cttz/ctpop ... FAILED"
    grep -oE '@llvm\.[a-z0-9.]+\([^)]*\)|trunc [a-z0-9]+|zext [a-z0-9]+' "$tmp/bitcnt.ll" 2>/dev/null | sed 's/^/    /'
    head -3 "$tmp/bitcnt.err" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# Constant folding must agree with the library, including the zero cases
# that the builtins define (clz(0) == width, ctz(0) == width).
cat > "$tmp/bitfold.c" <<'EOF'
int a = __builtin_clz(1);
int b = __builtin_clz(0);
int c = __builtin_ctz(0);
int d = __builtin_popcount(0xf0f0);
int e = __builtin_popcountll(0x123456789abcdef0ull);
int f = __builtin_clzl(1ul);
EOF
if "$compiler" -S -emit-llvm -o "$tmp/bitfold.ll" "$tmp/bitfold.c" 2>"$tmp/bitfold.err" &&
   grep -qE '^@a = .*i32 31' "$tmp/bitfold.ll" &&
   grep -qE '^@b = .*i32 32' "$tmp/bitfold.ll" &&
   grep -qE '^@c = .*i32 32' "$tmp/bitfold.ll" &&
   grep -qE '^@d = .*i32 8' "$tmp/bitfold.ll" &&
   grep -qE '^@e = .*i32 32' "$tmp/bitfold.ll"; then
    echo "testing bit-counting builtins fold ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing bit-counting builtins fold ... FAILED"
    grep -E '^@[a-f] ' "$tmp/bitfold.ll" 2>/dev/null | sed 's/^/    /'
    head -3 "$tmp/bitfold.err" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi


# Was a gap until va_start/va_arg/va_end/va_copy landed. The behaviour is
# checked at run time by test/function.c and test/f32.c, which call
# test/common's variadic fixtures; this asserts that it compiles.
ok "variadic function definition" <<'EOF'
#include <stdarg.h>
int sum(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int s = 0;
    for (int i = 0; i < n; i++) s += va_arg(ap, int);
    va_end(ap);
    return s;
}
double first_d(int n, ...) {
    va_list ap;
    va_start(ap, n);
    double d = va_arg(ap, double);
    va_end(ap);
    return d;
}
int main(void) {
    if (sum(3, 1, 2, 3) != 6) return 1;
    /* beyond the six general-purpose argument registers, so the reader
     * falls through to the overflow area */
    if (sum(8, 1, 2, 3, 4, 5, 6, 7, 8) != 36) return 2;
    if (first_d(1, 2.5) != 2.5) return 3;
    return 0;
}
EOF

# va_copy must yield an independent cursor: reading through the copy must
# leave the original positioned where it was.
ok "va_copy gives an independent cursor" <<'EOF'
#include <stdarg.h>
int probe(int n, ...) {
    va_list ap, aq;
    va_start(ap, n);
    va_copy(aq, ap);
    int from_copy = va_arg(aq, int);
    int from_orig = va_arg(ap, int);
    va_end(aq);
    va_end(ap);
    return from_copy * 10 + from_orig;
}
int main(void) { return probe(2, 1, 1) - 11; }
EOF

# ffs / parity / clrsb have no LLVM intrinsic: each is a short instruction
# sequence, including a select for the two whose zero input is special. The
# values below are the definitions in the compiler's own documentation of
# them, with the boundaries -- zero, the sign bit, all ones -- included.
cat > "$tmp/scan.c" <<'EOF'
int f0 = __builtin_ffs(0);
int f1 = __builtin_ffs(8);
int f2 = __builtin_ffs(-8);
int p0 = __builtin_parity(0);
int p1 = __builtin_parity(7);
int c0 = __builtin_clrsb(0);
int c1 = __builtin_clrsb(-1);
int c2 = __builtin_clrsb(1);
int w1 = __builtin_ffsl(1L << 40);
int w2 = __builtin_clrsbll(1LL << 62);
EOF
if "$compiler" -S -emit-llvm -o "$tmp/scan.ll" "$tmp/scan.c" 2>"$tmp/scan.err" &&
   grep -qE '^@f0 = .*i32 0' "$tmp/scan.ll" &&
   grep -qE '^@f1 = .*i32 4' "$tmp/scan.ll" &&
   grep -qE '^@f2 = .*i32 4' "$tmp/scan.ll" &&
   grep -qE '^@p0 = .*i32 0' "$tmp/scan.ll" &&
   grep -qE '^@p1 = .*i32 1' "$tmp/scan.ll" &&
   grep -qE '^@c0 = .*i32 31' "$tmp/scan.ll" &&
   grep -qE '^@c1 = .*i32 31' "$tmp/scan.ll" &&
   grep -qE '^@c2 = .*i32 30' "$tmp/scan.ll" &&
   grep -qE '^@w1 = .*i32 41' "$tmp/scan.ll" &&
   grep -qE '^@w2 = .*i32 0' "$tmp/scan.ll"; then
    echo "testing ffs/parity/clrsb fold ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing ffs/parity/clrsb fold ... FAILED"
    grep -E '^@[a-z0-9]+ ' "$tmp/scan.ll" 2>/dev/null | sed 's/^/    /'
    head -3 "$tmp/scan.err" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# The expansions are instruction sequences, so they must still refer to the
# right intrinsic: cttz for ffs, ctpop for parity, ctlz for clrsb.
cat > "$tmp/scanir.c" <<'EOF'
int a(int x)          { return __builtin_ffs(x); }
int b(unsigned x)     { return __builtin_parity(x); }
int c(int x)          { return __builtin_clrsb(x); }
EOF
"$compiler" -S -emit-llvm -o "$tmp/scanir.ll" "$tmp/scanir.c" 2>"$tmp/scanir.err"
if [ -f "$tmp/scanir.ll" ] &&
   grep -q 'llvm.cttz.i32' "$tmp/scanir.ll" &&
   grep -q 'llvm.ctpop.i32' "$tmp/scanir.ll" &&
   grep -q 'llvm.ctlz.i32' "$tmp/scanir.ll" &&
   grep -q 'select i1' "$tmp/scanir.ll"; then
    echo "testing ffs/parity/clrsb lower to instruction sequences ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing ffs/parity/clrsb lower to instruction sequences ... FAILED"
    grep -oE '@llvm\.[a-z0-9.]+\([^)]*\)|select [a-z0-9]+' "$tmp/scanir.ll" 2>/dev/null | sed 's/^/    /'
    head -3 "$tmp/scanir.err" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# The overflow builtins return { iN, i1 } in the IR -- two values, the
# result and a flag -- which is the one place a builtin's call yields an
# aggregate. They also fix the width from the argument, with no integer
# promotion, so each case below is a distinct intrinsic: a short operand
# must reach irasm as i16, not widened to i32.
cat <<'EOF' | ok "__builtin_{add,sub,mul}_overflow"
int main(void) {
    int r;
    unsigned ur;
    short sr;
    long long lr;

    if (!__builtin_add_overflow(2147483647, 1, &r) || r != -2147483647 - 1) return 1;
    if (__builtin_add_overflow(1, 2, &r) || r != 3) return 2;
    if (!__builtin_sub_overflow(-2147483647 - 1, 1, &r) || r != 2147483647) return 3;
    if (!__builtin_mul_overflow(65536, 65536, &r) || r != 0) return 4;

    /* unsigned operands select the u-form of the intrinsic */
    if (!__builtin_add_overflow(4294967295u, 1u, &ur) || ur != 0) return 5;

    /* the width comes from the operand: 16 bits here, not 32 */
    if (!__builtin_add_overflow((short)32767, (short)1, &sr) || sr != -32768) return 6;
    if (!__builtin_mul_overflow((short)256, (short)256, &sr) || sr != 0) return 7;

    /* 64-bit operands use the i64 intrinsic */
    if (!__builtin_mul_overflow(4000000000LL, 4000000000LL, &lr)) return 8;
    if (__builtin_mul_overflow(1000000LL, 1000000LL, &lr) || lr != 1000000000000LL) return 9;
    return 0;
}
EOF

# The intrinsic names carry both the operation and the signedness, and the
# width has to be the operand's own. Getting any of those wrong still
# produces valid IR that computes the wrong thing, so the names are checked
# directly.
cat > "$tmp/ovf.c" <<'EOF'
int a(int x, int y, int *r)          { return __builtin_add_overflow(x, y, r); }
int s(int x, int y, int *r)          { return __builtin_sub_overflow(x, y, r); }
int m(int x, int y, int *r)          { return __builtin_mul_overflow(x, y, r); }
int ua(unsigned x, unsigned y, unsigned *r) { return __builtin_add_overflow(x, y, r); }
int na(short x, short y, short *r)   { return __builtin_add_overflow(x, y, r); }
EOF
"$compiler" -w -S -emit-llvm -o "$tmp/ovf.ll" "$tmp/ovf.c" 2>"$tmp/ovf.err"
if [ -f "$tmp/ovf.ll" ] &&
   grep -q 'llvm.sadd.with.overflow.i32' "$tmp/ovf.ll" &&
   grep -q 'llvm.ssub.with.overflow.i32' "$tmp/ovf.ll" &&
   grep -q 'llvm.smul.with.overflow.i32' "$tmp/ovf.ll" &&
   grep -q 'llvm.uadd.with.overflow.i32' "$tmp/ovf.ll" &&
   grep -q 'llvm.sadd.with.overflow.i16' "$tmp/ovf.ll" &&
   grep -q 'extractvalue { i32, i1 }' "$tmp/ovf.ll"; then
    echo "testing overflow builtin intrinsic selection ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing overflow builtin intrinsic selection ... FAILED"
    grep -oE '@llvm\.[a-z0-9.]+' "$tmp/ovf.ll" 2>/dev/null | sort -u | sed 's/^/    /'
    head -3 "$tmp/ovf.err" 2>/dev/null | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# <stdckdint.h> (C23 7.20) is written entirely in terms of those builtins,
# so this is the end-to-end check that they are wired up completely.
cat <<'EOF' | ok "<stdckdint.h> ckd_add"
#include <stdckdint.h>
int main(void) {
    int r;
    if (!ckd_add(&r, 2147483647, 1) || r != -2147483647 - 1) return 1;
    if (ckd_add(&r, 1, 2) || r != 3) return 2;
    return 0;
}
EOF

# C23 6.7.7.1 allows a parameter-type-list that is a bare "...", with no
# parameter-list ahead of it, and 7.16.1.4 then gives va_start the
# one-operand form because there is no last parameter to name.
ok "bare ... parameter list" <<'EOF'
#include <stdarg.h>
int sum_all(...) {
    va_list ap;
    va_start(ap);
    int a = va_arg(ap, int);
    int b = va_arg(ap, int);
    va_end(ap);
    return a + b;
}
int main(void) { return sum_all(3, 4) - 7; }
EOF

# The bare form is the whole parameter-type-list, so nothing may follow the
# ellipsis. A second one is the case that matters: the loop's own ellipsis
# case sits after a comma check that the bare form leaves nothing for.
bad "reject a second ellipsis" <<'EOF'
int foo(... ...);
EOF

bad "reject a parameter after a bare ellipsis" <<'EOF'
int foo(..., int);
EOF

# va_arg with an aggregate: the value is produced from the argument area
# rather than by a scalar load.
ok "va_arg with a struct" <<'EOF'
#include <stdarg.h>
struct S { int a, b; };
int sum_struct(int n, ...) {
    va_list ap;
    va_start(ap, n);
    struct S s = va_arg(ap, struct S);
    va_end(ap);
    return s.a + s.b;
}
int main(void) {
    struct S s = {3, 4};
    return sum_struct(1, s) - 7;
}
EOF

# The variadic builtins only make sense inside a variadic definition.
bad "reject va_start outside a variadic function" <<'EOF'
#include <stdarg.h>
int f(int n) { va_list ap; va_start(ap, n); return 0; }
EOF

bad "reject va_arg outside a variadic function" <<'EOF'
#include <stdarg.h>
int f(int n) { va_list ap; va_start(ap, n); return va_arg(ap, int); }
EOF

# 7.16.1.1 asks for a va_list, not for a variadic *function*: the helper that
# forwards one -- vprintf's shape -- has no parameter list of its own to be
# variadic, and both gcc and clang accept va_arg and va_copy there. The
# parameter is adjusted to a pointer (6.7.6.3p7), so the expansion has to read
# the pointer out of the variable: reading its address instead made this
# compile and then segfault.
cat > "$tmp/valist_fwd.c" <<'EOF'
#include <stdarg.h>

static int sum(int n, va_list ap) {
    int s = 0;
    for (int i = 0; i < n; i++) s += va_arg(ap, int);
    return s;
}

static int first_char(va_list ap) { return va_arg(ap, char *)[0]; }

static void copy_it(va_list dst, va_list src) { va_copy(dst, src); }

static int run(int n, ...) {
    va_list ap, saved;
    va_start(ap, n);
    va_copy(saved, ap);
    int a = sum(1, ap);
    copy_it(saved, saved);
    int b = sum(2, saved);
    va_end(saved);
    va_end(ap);
    return a + b;
}

static int run2(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int c = first_char(ap);
    va_end(ap);
    return c;
}

int main(void) {
    if (run(2, 5, 6) != 16) return 1;
    if (run2("x", "hello") != 'h') return 2;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/valist_fwd" "$tmp/valist_fwd.c" >/dev/null 2>&1 && "$tmp/valist_fwd"; then
    echo "testing a va_list forwarded to a helper ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a va_list forwarded to a helper ... FAILED"
    n_fail=$((n_fail + 1))
fi

# --- 6.7.5 Function specifiers ----------------------------------------

# 6.7.5p8: a definition whose every file scope declaration is `inline`
# without `extern` is an *inline definition*, which does not define the
# symbol. The unit has the definition but the object holds an undefined
# reference, so this program does not link on its own -- exactly what gcc
# and clang produce at -O0 as well.
cat > "$tmp/inl_use.c" <<'EOF'
inline int twice(int x) { return x + x; }
int main(void) { return twice(21) == 42 ? 0 : 1; }
EOF
if "$compiler" -w -o "$tmp/inl_use" "$tmp/inl_use.c" > "$tmp/log" 2>&1; then
    echo "testing an inline definition does not define the symbol ... FAILED (it linked)"
    n_fail=$((n_fail + 1))
else
    echo "testing an inline definition does not define the symbol ... passed"
    n_pass=$((n_pass + 1))
fi

# The complement of p8: another unit's `extern inline` is an external
# definition, and the two together are a program.
cat > "$tmp/inl_def.c" <<'EOF'
extern inline int twice(int x) { return x + x; }
EOF
if "$compiler" -w -o "$tmp/inl" "$tmp/inl_use.c" "$tmp/inl_def.c" > "$tmp/log" 2>&1 && "$tmp/inl"; then
    echo "testing extern inline in another unit completes the program ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing extern inline in another unit completes the program ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# "all the file scope declarations" is the whole unit, not the ones seen so
# far: an extern declaration after the definition makes it an external one.
cat > "$tmp/inl_ext.c" <<'EOF'
inline int twice(int x) { return x + x; }
extern int twice(int x);
int main(void) { return twice(21) == 42 ? 0 : 1; }
EOF
if "$compiler" -w -o "$tmp/inl_ext" "$tmp/inl_ext.c" > "$tmp/log" 2>&1 && "$tmp/inl_ext"; then
    echo "testing a later extern declaration makes it an external definition ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a later extern declaration makes it an external definition ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# A declaration without inline does the same thing.
cat > "$tmp/inl_plain.c" <<'EOF'
int twice(int x);
inline int twice(int x) { return x + x; }
int main(void) { return twice(21) == 42 ? 0 : 1; }
EOF
if "$compiler" -w -o "$tmp/inl_plain" "$tmp/inl_plain.c" > "$tmp/log" 2>&1 && "$tmp/inl_plain"; then
    echo "testing a declaration without inline keeps the external definition ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a declaration without inline keeps the external definition ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# static inline has internal linkage, so it is a definition here and links
# on its own; p8's first sentence leaves it alone.
cat > "$tmp/inl_static.c" <<'EOF'
static inline int twice(int x) { return x + x; }
int main(void) { return twice(21) == 42 ? 0 : 1; }
EOF
if "$compiler" -w -o "$tmp/inl_static" "$tmp/inl_static.c" > "$tmp/log" 2>&1 && "$tmp/inl_static"; then
    echo "testing a static inline function is defined here ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a static inline function is defined here ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# p5 (declared inline, never defined here) and p3 (a modifiable static local
# in an inline definition) are constraints: gcc diagnoses both, clang only
# the second. The static-local one has its own group, clang's name.
cat > "$tmp/inl_warn.c" <<'EOF'
inline int f(void) { static int q = 1; return q; }
inline int g(void);
int main(void) { return f() - 1; }
EOF
w=$("$compiler" -S -o /dev/null "$tmp/inl_warn.c" 2>&1 | grep -c 'declared but never defined')
s=$("$compiler" -S -o /dev/null "$tmp/inl_warn.c" 2>&1 | grep -c 'static local')
n=$("$compiler" -Wno-static-local-in-inline -S -o /dev/null "$tmp/inl_warn.c" 2>&1 | grep -c 'static local')
if [ "$w" -eq 1 ] && [ "$s" -eq 1 ] && [ "$n" -eq 0 ]; then
    echo "testing the inline constraints are diagnosed ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the inline constraints are diagnosed ... FAILED (never-defined $w/1, static-local $s/1, -Wno $n)"
    n_fail=$((n_fail + 1))
fi

# --- 6.9.2p2: arrays left without a length ----------------------------

# A tentative definition whose type is still an array of unknown size at the
# end of the unit has one element, and the object is emitted with that
# length: the second unit declares extern int arr[1] and both agree.
cat > "$tmp/tent_def.c" <<'EOF'
int arr[];
int *p = arr;
EOF
cat > "$tmp/tent_use.c" <<'EOF'
extern int arr[1];
int main(void) { arr[0] = 7; return arr[0] - 7; }
EOF
if "$compiler" -w -o "$tmp/tent" "$tmp/tent_def.c" "$tmp/tent_use.c" > "$tmp/log" 2>&1 && "$tmp/tent"; then
    echo "testing a tentative array definition holds one element ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a tentative array definition holds one element ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# The assumption is announced once per such array -- and only for those: an
# initializer, an explicit length, or a later declaration that completes the
# type all leave the type known by the end of the unit.
cat > "$tmp/tent_warn.c" <<'EOF'
int arr[];
int done[] = {1, 2, 3};
int sized[4];
int late[];
int late[10];
EOF
w=$("$compiler" -S -o /dev/null "$tmp/tent_warn.c" 2>&1 | grep -c 'assumed to have one element')
n=$("$compiler" -Wno-tentative-definition-array -S -o /dev/null "$tmp/tent_warn.c" 2>&1 | grep -c 'assumed to have one element')
if [ "$w" -eq 1 ] && [ "$n" -eq 0 ]; then
    echo "testing the one-element assumption is diagnosed ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the one-element assumption is diagnosed ... FAILED ($w warnings, $n with -Wno)"
    n_fail=$((n_fail + 1))
fi

# An extern declaration is not a definition, so there is no length to
# assume. LLVM has no global of unknown size, so it is declared with the
# length zero -- the one clang writes there too.
cat > "$tmp/tent_decl.c" <<'EOF'
extern int arr[];
int main(void) { return 0; }
EOF
if "$compiler" -w -S -emit-llvm -o "$tmp/tent_decl.ll" "$tmp/tent_decl.c" > "$tmp/log" 2>&1 &&
    grep -q 'external global \[0 x i32\]' "$tmp/tent_decl.ll"; then
    echo "testing an extern array declaration is emitted with no length ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing an extern array declaration is emitted with no length ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# A block scope definition has no tentative rule to lean on: it needs a size
# or an initializer (6.7.9), which both references diagnose.
bad "an array definition in a block needs a size" <<'EOF'
int main(void) { int arr[]; return 0; }
EOF

bad "a static array in a block needs a size" <<'EOF'
int main(void) { static int arr[]; return 0; }
EOF

# 6.7.6.2p1: an array's element type has to be complete.
bad "an array of incomplete element type" <<'EOF'
struct S;
struct S arr[];
int main(void) { return 0; }
EOF

# --- constant conversions that change the value -----------------------

# 6.3.1.3: an implicit conversion is worth a word when the constant does not
# fit the target. The integer cases are clang's -Wconstant-conversion (gcc's
# -Woverflow covers all but `long` to `int`), the floating-to-integer ones
# clang's -Wliteral-conversion. Both are on by default, and explicit casts
# and the `unsigned u = -1;` idiom are not diagnosed at all.
cat > "$tmp/conv.c" <<'EOF'
char c = 300;
unsigned char uc = 300;
short s = 70000;
int big = 3000000000;
int frac = 1.5;
unsigned u = -1;
unsigned char minus_one = -1;
char fine = 100;
char cast = (char)300;
long wider = -1;
int main(void) {
    char block = 300;
    return block + c + uc + s + big + frac + (int)u + minus_one + fine + cast + (int)wider;
}
EOF
w=$("$compiler" -S -o /dev/null "$tmp/conv.c" 2>&1 | grep -c 'changes value')
no_const=$("$compiler" -Wno-constant-conversion -S -o /dev/null "$tmp/conv.c" 2>&1 | grep -c 'changes value')
no_lit=$("$compiler" -Wno-literal-conversion -S -o /dev/null "$tmp/conv.c" 2>&1 | grep -c 'changes value')
nowarn=$("$compiler" -w -S -o /dev/null "$tmp/conv.c" 2>&1 | grep -c 'changes value')
if [ "$w" -eq 6 ] && [ "$no_const" -eq 1 ] && [ "$no_lit" -eq 5 ] && [ "$nowarn" -eq 0 ]; then
    echo "testing constant conversions are diagnosed ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing constant conversions are diagnosed ... FAILED ($w, $no_const, $no_lit, $nowarn)"
    n_fail=$((n_fail + 1))
fi

# The floating cases are gcc's -Wfloat-conversion, off by default in both
# references because `float f = 1.1;` would otherwise warn everywhere -- with
# one exception, which clang also carves out: a *constant integer* that the
# target float cannot hold exactly has its own group and that group is on by
# default (-Wimplicit-const-int-float-conversion there, so -Wno-float-conversion
# does not silence it either). The message has to spell out both values: a
# printer too coarse to tell 1.1 from the float it rounds to made it read
# "from 1.1 to 1.1".
cat > "$tmp/convf.c" <<'EOF'
float f = 1.1;
float big = 1e300;
float g = 16777217;
int main(void) { return 0; }
EOF
d=$("$compiler" -S -o /dev/null "$tmp/convf.c" 2>&1 | grep -c 'changes value')
nof=$("$compiler" -Wno-float-conversion -S -o /dev/null "$tmp/convf.c" 2>&1 | grep -c 'changes value')
noi=$("$compiler" -Wno-implicit-const-int-float-conversion -S -o /dev/null "$tmp/convf.c" 2>&1 | grep -c 'changes value')
e=$("$compiler" -Wfloat-conversion -S -o /dev/null "$tmp/convf.c" 2>&1 | grep -c 'changes value')
t=$("$compiler" -Wfloat-conversion -S -o /dev/null "$tmp/convf.c" 2>&1 | grep -c '1.10000002')
i=$("$compiler" -Wfloat-conversion -S -o /dev/null "$tmp/convf.c" 2>&1 | grep -c '16777216')
if [ "$d" -eq 1 ] && [ "$nof" -eq 1 ] && [ "$noi" -eq 0 ] && [ "$e" -eq 3 ] && [ "$t" -eq 1 ] && [ "$i" -eq 1 ]; then
    echo "testing float conversions are diagnosed on request ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing float conversions are diagnosed on request ... FAILED (default $d, -Wno-float $nof, -Wno-const-int $noi, -Wfloat $e, $t, $i)"
    n_fail=$((n_fail + 1))
fi

# --- out-of-range values: 6.3.1.4 -------------------------------------

# A static initializer may not be built on an undefined conversion, so an
# out-of-range float-to-integer one is refused outright (clang refuses these
# too, with an internal message; gcc saturates and warns instead).
bad "a static initializer converting an out-of-range float" <<'EOF'
int i = 1e20;
int main(void) { return 0; }
EOF

# The refusal has to say what would fit -- and, since "choose another value"
# is guesswork without one, the value nearest to the one written: gcc prints
# the same saturation ("changes value from 1.0e+20 to 2147483647").
cat > "$tmp/fits.c" <<'EOF'
int i = 1e20;
int main(void) { return 0; }
EOF
n=$("$compiler" -S -o /dev/null "$tmp/fits.c" 2>&1 | grep -c 'holds -2147483648 to 2147483647; the nearest representable value is 2147483647')
if [ "$n" -eq 1 ]; then
    echo "testing the refusal names the range that would fit ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the refusal names the range that would fit ... FAILED ($n)"
    n_fail=$((n_fail + 1))
fi

# Each wrong direction saturates to its own end of the range, and a NaN is
# near nothing so it gets no value at all.
cat > "$tmp/sat1.c" <<'EOF'
long l = -1e30;
int main(void) { return 0; }
EOF
cat > "$tmp/sat2.c" <<'EOF'
unsigned u = -1e10;
int main(void) { return 0; }
EOF
cat > "$tmp/sat3.c" <<'EOF'
short s = 1e10;
int main(void) { return 0; }
EOF
lo=$("$compiler" -S -o /dev/null "$tmp/sat1.c" 2>&1 | grep -c 'nearest representable value is -9223372036854775808')
uz=$("$compiler" -S -o /dev/null "$tmp/sat2.c" 2>&1 | grep -c 'nearest representable value is 0')
sh=$("$compiler" -S -o /dev/null "$tmp/sat3.c" 2>&1 | grep -c 'nearest representable value is 32767')
if [ "$lo" -eq 1 ] && [ "$uz" -eq 1 ] && [ "$sh" -eq 1 ]; then
    echo "testing the nearest value saturates each direction ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the nearest value saturates each direction ... FAILED (long $lo, unsigned $uz, short $sh)"
    n_fail=$((n_fail + 1))
fi

bad "an explicit cast of the same value" <<'EOF'
int i = (int)1e20;
int main(void) { return 0; }
EOF

# Inside a function the same conversion is a warning, not an error, and a
# value that does fit keeps its ordinary diagnosis.
cat > "$tmp/range.c" <<'EOF'
int main(void) {
    int i = 1e20;
    int j = 1.5;
    return i + j;
}
EOF
w=$("$compiler" -S -o /dev/null "$tmp/range.c" 2>&1 | grep -c 'warning')
e=$("$compiler" -S -o /dev/null "$tmp/range.c" 2>&1 | grep -c 'error')
note=$("$compiler" -S -o /dev/null "$tmp/range.c" 2>&1 | grep -c 'nearest representable value')
quiet=$("$compiler" -Wno-literal-conversion -S -o /dev/null "$tmp/range.c" 2>&1 | grep -c 'nearest representable value')
if [ "$w" -eq 2 ] && [ "$e" -eq 0 ] && [ "$note" -eq 1 ] && [ "$quiet" -eq 0 ]; then
    echo "testing an out-of-range conversion is only a warning in a body ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing an out-of-range conversion is only a warning in a body ... FAILED ($w, $e, $note, $quiet)"
    n_fail=$((n_fail + 1))
fi

# 6.4.4.2: a constant that overflows its own type becomes an infinity, and
# that is worth saying. The check is in the parser, not the lexer, so the
# same literal inside a dead #if branch stays quiet.
cat > "$tmp/litrange.c" <<'EOF'
double d = 1e400;
float f = 1e40f;
long double x = 1e5000L;
double ok = 1e308;
float down = 1e300;
#if 0
double dead = 1e400;
#endif
int main(void) { return 0; }
EOF
n=$("$compiler" -S -o /dev/null "$tmp/litrange.c" 2>&1 | grep -c 'too large for type')
z=$("$compiler" -Wno-literal-range -S -o /dev/null "$tmp/litrange.c" 2>&1 | grep -c 'too large for type')
if [ "$n" -eq 3 ] && [ "$z" -eq 0 ]; then
    echo "testing a floating constant beyond its own type is diagnosed ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a floating constant beyond its own type is diagnosed ... FAILED ($n, $z)"
    n_fail=$((n_fail + 1))
fi

# --- __attribute__((cleanup(f))) -------------------------------------

# The attribute belongs to a variable, and to an automatic one at that. Both
# references warn and ignore it on a file-scope object, a block-scope static,
# a parameter (a prototype's as much as a definition's), a typedef -- on
# either side of the declarator -- and a function declaration. The handler
# itself, and the shapes where it runs, live in test/c2y.sh.
cat > "$tmp/cleanpos.c" <<'EOF'
static void h(int *p) { (void)p; }
__attribute__((cleanup(h))) static int g = 1;
__attribute__((cleanup(h))) typedef int T;
typedef int U __attribute__((cleanup(h)));
static void f(__attribute__((cleanup(h))) int x);
static void e(__attribute__((cleanup(h))) int x) { (void)x; }
static void onfn(void) __attribute__((cleanup(h)));
int main(void) { __attribute__((cleanup(h))) static int s = 1; T t = 0; U u = 0; (void)t; (void)u; return 0; }
EOF
n=$("$compiler" -S -o /dev/null "$tmp/cleanpos.c" 2>&1 | grep -c 'only applies to local variables')
q=$("$compiler" -Wno-attributes -S -o /dev/null "$tmp/cleanpos.c" 2>&1 | grep -c 'only applies to local variables')
w=$("$compiler" -w -S -o /dev/null "$tmp/cleanpos.c" 2>&1 | grep -c 'only applies to local variables')
if [ "$n" -eq 7 ] && [ "$q" -eq 0 ] && [ "$w" -eq 0 ]; then
    echo "testing cleanup outside a local variable is ignored ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing cleanup outside a local variable is ignored ... FAILED ($n/7, $q, $w)"
    n_fail=$((n_fail + 1))
fi

# The same attribute in all four spellings and positions on a variable that
# can use it: the handler runs in every one, and none of them draws a warning
# of its own. (The C23 spelling after the declarator was rejected as a type
# attribute before this round.)
cat > "$tmp/cleanspell.c" <<'EOF'
int seen;
static void h(int *p) { seen += *p; }
int main(void) {
    __attribute__((cleanup(h))) int a = 1;
    int b __attribute__((cleanup(h))) = 2;
    [[gnu::cleanup(h)]] int c = 4;
    int d [[gnu::cleanup(h)]] = 8;
    return seen == 0 ? 0 : 1;
}
EOF
if "$compiler" -w -o "$tmp/cleanspell" "$tmp/cleanspell.c" >/dev/null 2>&1 && "$tmp/cleanspell"; then
    own=$("$compiler" -S -o /dev/null "$tmp/cleanspell.c" 2>&1 | grep -cE 'warning:|error:')
    if [ "$own" -eq 0 ]; then
        echo "testing every spelling of cleanup runs the handler ... passed"
        n_pass=$((n_pass + 1))
    else
        echo "testing every spelling of cleanup runs the handler ... FAILED (own diagnostics: $own)"
        n_fail=$((n_fail + 1))
    fi
else
    echo "testing every spelling of cleanup runs the handler ... FAILED"
    n_fail=$((n_fail + 1))
fi

# --- -Wsign-compare --------------------------------------------------

# Mixed signedness in a comparison converts the signed operand to unsigned.
# gcc and clang keep this group out of the C defaults, so the entry passes
# the flag itself; the silent rows are the promotion rules and the
# heuristics both references use.
cat > "$tmp/signcmp.c" <<'EOF'
int rel(int i, unsigned u) { return i < u; }
int eq(int i, unsigned u) { return i == u; }
int promoted(unsigned char c, int i) { return c < i; }
int boolish(_Bool b, unsigned u) { return b < u; }
int zero(int i) { return i < 0u; }
int fitted(int i) { return i < 5; }
int plain(unsigned a, unsigned b) { return a < b; }
int main(void) { return 0; }
EOF
on=$("$compiler" -Wsign-compare -S -o /dev/null "$tmp/signcmp.c" 2>&1 | grep -c 'different signedness')
off=$("$compiler" -S -o /dev/null "$tmp/signcmp.c" 2>&1 | grep -c 'different signedness')
quiet=$("$compiler" -Wsign-compare -Wno-sign-compare -S -o /dev/null "$tmp/signcmp.c" 2>&1 | grep -c 'different signedness')
if [ "$on" -eq 2 ] && [ "$off" -eq 0 ] && [ "$quiet" -eq 0 ]; then
    echo "testing mixed-sign comparisons are diagnosed on request ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing mixed-sign comparisons are diagnosed on request ... FAILED ($on, $off, $quiet)"
    n_fail=$((n_fail + 1))
fi

# --- GNU asm statements ----------------------------------------------

# asm is not ISO C in any form, so the whole construct is the GNU one. The
# shapes below are the ones both references accept; the misuse shapes after
# them are the ones both refuse.
ok 'a basic asm statement' <<'EOF'
void f(void) { __asm__("nop"); }
EOF

ok 'extended asm with every operand kind' <<'EOF'
int f(int x) {
    int y;
    __asm__ __volatile__("movl %[in], %[out]"
                         : [out] "=r"(y), "+m"(x)
                         : [in] "r"(x), "i"(3)
                         : "memory", "cc");
    return y;
}
EOF

ok 'an asm goto statement' <<'EOF'
int f(int x) { __asm__ goto("testl %0, %0; jne %l[out]" : : "r"(x) : : out); return 0; out: return 1; }
EOF

ok 'a file-scope asm statement' <<'EOF'
__asm__(".globl f\n.text");
int f(void) { return 0; }
EOF

bad 'an asm output that is not an lvalue' <<'EOF'
void f(void) { __asm__("" : "=r"(1)); }
EOF

bad 'an asm operand number with no operand' <<'EOF'
void f(int x) { __asm__("mov %9, %0" : "=r"(x)); }
EOF

bad 'an asm operand at file scope' <<'EOF'
__asm__("" : "=r"(1));
EOF

bad 'an asm output constraint without =' <<'EOF'
void f(int x) { __asm__("" : "r"(x)); }
EOF

# What the template writes comes back in the object it named, the input half
# of a `+` reads that object's current value, and an asm goto's jump lands on
# its label.
cat > "$tmp/asmrun.c" <<'EOF'
int main(void) {
    int x = 7, y = 0;
    __asm__("movl %1, %0" : "=r"(y) : "r"(x));
    if (y != 7) return 1;
    __asm__("addl $2, %0" : "+r"(y));
    if (y != 9) return 2;
    __asm__("addl $3, %0" : "+m"(y));
    if (y != 12) return 3;
    __asm__ goto("testl %0, %0; jne %l[lab]" : : "r"(y) : : lab);
    return 4;
lab:
    return y == 12 ? 0 : 5;
}
EOF
if "$compiler" -w -o "$tmp/asmrun" "$tmp/asmrun.c" >/dev/null 2>&1 && "$tmp/asmrun"; then
    echo "testing asm operands and asm goto run ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing asm operands and asm goto run ... FAILED"
    n_fail=$((n_fail + 1))
fi

# --- anonymous members, designators, and __PRETTY_FUNCTION__ ---------

# The members of an anonymous struct or union member are members of the
# enclosing record (6.7.2.1p13), and a designator may name one of them. Three
# of the shapes below crashed the parser -- the duplicate-name check walked
# into an anonymous member's own member list with a null name token -- and the
# chained designators (`.in.x = 9`, `[0].a = 11`) were refused.
cat > "$tmp/anon.c" <<'EOF'
struct A { int tag; struct { int a; union { int b; }; }; };
struct B { int tag; struct { int a; unsigned : 3; }; };
struct C { int kind; union { struct { int lhs; int rhs; }; struct { int cond; }; }; int tail; };
struct D { struct { int a; }; int tail; };
struct E { int kind; struct { int x, y; } in; };
struct F { int a, b; };
int main(void) {
    struct A a = { .tag = 1, .a = 2, .b = 3 };
    struct B b = { .tag = 4, .a = 5 };
    struct C c = { .lhs = 6, .tail = 7 };
    struct D d = { .tail = 8 };
    struct E e = { .in.x = 9, .in.y = 10 };
    struct F v[2] = { [0].a = 11, [1].b = 12 };
    if (a.tag + a.a + a.b != 6) return 1;
    if (b.tag + b.a != 9) return 2;
    if (c.lhs + c.tail != 13) return 3;
    if (d.tail != 8) return 4;
    if (e.in.x + e.in.y != 19) return 5;
    if (v[0].a + v[1].b != 23) return 6;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/anon" "$tmp/anon.c" >/dev/null 2>&1 && "$tmp/anon"; then
    echo "testing anonymous members and designators run ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing anonymous members and designators run ... FAILED"
    n_fail=$((n_fail + 1))
fi

# A designator that names a sibling of an anonymous member is not one of that
# member's own names, and a name no member has is still refused.
bad 'a designator naming no member at all' <<'EOF'
struct S { int a; struct { int b; }; };
int f(void) { struct S s = { .nope = 1 }; return s.a; }
EOF

bad 'a member list without a comma' <<'EOF'
struct S { int a, b; };
int f(void) { struct S s = { .a = 1 .b = 2 }; return s.a; }
EOF

# [GNU] __PRETTY_FUNCTION__ is another name of __func__ -- gcc's reading of
# it in C, clang spells the signature out -- and glibc's <assert.h> uses it.
cat > "$tmp/pretty.c" <<'EOF'
#include <assert.h>
static const char *name(void) { return __PRETTY_FUNCTION__; }
int main(void) {
    assert(1);
    return name()[0] == 'n' && __func__[0] == 'm' ? 0 : 1;
}
EOF
if "$compiler" -w -o "$tmp/pretty" "$tmp/pretty.c" >/dev/null 2>&1 && "$tmp/pretty"; then
    echo "testing __PRETTY_FUNCTION__ and glibc's assert ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing __PRETTY_FUNCTION__ and glibc's assert ... FAILED"
    n_fail=$((n_fail + 1))
fi

n=$("$compiler" -pedantic -S -o /dev/null "$tmp/pretty.c" 2>&1 | grep -c 'does not support ‘__PRETTY_FUNCTION__’ predefined identifier')
# Two uses, in fact: the one written here and the one glibc's assert expands.
if [ "$n" -ge 1 ]; then
    echo "testing __PRETTY_FUNCTION__ is diagnosed as an extension ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing __PRETTY_FUNCTION__ is diagnosed as an extension ... FAILED ($n)"
    n_fail=$((n_fail + 1))
fi

# --- a definition is a definition, wherever an extern came first -----

# 6.9.2p2 and 6.9.2p1: a file-scope declaration with an initializer defines
# the object, and so does one that is a tentative definition -- even when an
# `extern` declaration of the same name came first. The symbol's storage
# class is the first declaration's, so without clearing it the unit emitted
# `@t = external global` and the link failed with `undefined reference to t`
# (which is how cxx could not link against itself).
cat > "$tmp/deflink.c" <<'EOF'
extern int t1;
int t1;
extern int t2;
int t2 = 5;
extern int t3[3];
int t3[3];
EOF
cat > "$tmp/defmain.c" <<'EOF'
extern int t1, t2, t3[];
int main(void) { return t1 + t2 + t3[0] - 5; }
EOF
if "$compiler" -w -o "$tmp/deflink" "$tmp/deflink.c" "$tmp/defmain.c" >/dev/null 2>&1 && "$tmp/deflink"; then
    echo "testing a definition after an extern declaration links ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a definition after an extern declaration links ... FAILED"
    n_fail=$((n_fail + 1))
fi

# A file-scope compound literal's object has no linkage, so each translation
# unit gets its own: both used to be emitted as `.compoundliteral` with
# external linkage, and the link failed with `multiple definition`.
cat > "$tmp/cl1.c" <<'EOF'
int *p = &(int){1};
int one(void) { return *p; }
EOF
cat > "$tmp/cl2.c" <<'EOF'
int *q = &(int){2};
int two(void) { return *q; }
EOF
cat > "$tmp/clmain.c" <<'EOF'
int one(void), two(void);
int main(void) { return one() + two() - 3; }
EOF
if "$compiler" -w -o "$tmp/cl" "$tmp/cl1.c" "$tmp/cl2.c" "$tmp/clmain.c" >/dev/null 2>&1 && "$tmp/cl"; then
    echo "testing compound literals do not collide across units ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing compound literals do not collide across units ... FAILED"
    n_fail=$((n_fail + 1))
fi

# --- a subscript's index is pointer-sized -----------------------------

# C2y 6.5.3.2 makes `E1[E2]` designate the element `*(E1 + E2)` would, so the
# index takes part in the address computation at the pointer's width. It did
# not: the index kept its own type, and an `unsigned char` index of 200 reached
# LLVM as an i8 GEP index -- which LLVM *sign*-extends, so the access landed
# 224 bytes before the array. That is the shape of cxx's own
# `op_table[tok->kind]` (`Token.kind` is a uint8_t and every kind that matters
# is >= 128), which is why the compiler cxx built could not parse `&&`, `==`,
# `static`, or a single system header.
cat > "$tmp/subidx.c" <<'EOF'
int tbl[256];
int get(unsigned char i) { return tbl[i]; }
int viaptr(unsigned char i) { return *(tbl + i); }
int main(void) {
    unsigned char i = 200;
    unsigned int big = 3000000000u;
    tbl[200] = 7;
    if (get(200) != 7) return 1;
    if (get(i) != 7) return 2;
    if (tbl[i] != 7) return 3;
    if (tbl[(unsigned char)200] != 7) return 4;
    if (viaptr(i) != 7) return 5;
    if (tbl[big - 3000000000u + 200u] != 7) return 6;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/subidx" "$tmp/subidx.c" >/dev/null 2>&1 && "$tmp/subidx"; then
    echo "testing a narrow unsigned subscript reaches its element ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a narrow unsigned subscript reaches its element ... FAILED"
    n_fail=$((n_fail + 1))
fi

# --- constructs real projects rely on ---------------------------------

# __builtin_expect(x, c) is x -- lua, git and cpython all spell their hot-path
# hints that way -- and offsetof is an integer constant expression (7.19p3), so
# it can size an array. Both were missing, and each stopped real code: lua's
# lapi.c and libpng's pngread.c would not compile at all.
cat > "$tmp/realcode.c" <<'EOF'
#include <stdarg.h>
#include <stddef.h>

struct S { int a; long b; char c[4]; };
struct T { char pad[offsetof(struct S, c[2])]; int x; };

static int first(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int v = va_arg(ap, int);
    va_end(ap);
    return v;
}

int main(void) {
    if (offsetof(struct S, b) != 8) return 1;
    if (offsetof(struct S, c[2]) != 18) return 2;
    if (offsetof(struct T, x) != 20) return 3;
    if (sizeof(struct T) != 24) return 4;
    if (__builtin_expect(first(3, 7, 8, 9), 7) != 7) return 5;
    if (__builtin_expect(0, 1) != 0) return 6;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/realcode" "$tmp/realcode.c" >/dev/null 2>&1 && "$tmp/realcode"; then
    echo "testing __builtin_expect and offsetof as a constant ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing __builtin_expect and offsetof as a constant ... FAILED"
    n_fail=$((n_fail + 1))
fi

# --- the two things that kept sqlite from compiling --------------------

# A function declared with an asm label is emitted under that label, so a
# reference to it has to use the label too: sqlite's aSyscall table mentions
# fcntl, which glibc redirects to fcntl64, and the reference said @fcntl while
# the declaration said @"fcntl64" -- LLVM refused the module.
cat > "$tmp/asmref.c" <<'EOF'
int real(void) { return 7; }
int alias(void) __asm__("real");
int (*p)(void) = alias;
int (*table[])(void) = { alias, alias };
int main(void) { return p() + table[1]() - 14; }
EOF
if "$compiler" -w -o "$tmp/asmref" "$tmp/asmref.c" >/dev/null 2>&1 && "$tmp/asmref"; then
    echo "testing a reference uses the emitted name ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a reference uses the emitted name ... FAILED"
    n_fail=$((n_fail + 1))
fi

# The reachability walk seeds the names parsing already rooted -- an emitted
# initializer roots them (a block-scope static's does) -- and follows their
# references. Leaving them out of the worklist stopped the walk one step in,
# so what they mention was dropped as unused while the reference stayed.
cat > "$tmp/rooted.c" <<'EOF'
static int inner[] = { 7, 8 };
static const int *chain[] = { inner, inner + 1 };
static const int *const *pick(void) {
    static const int *const *rooted = chain;
    return rooted;
}
int main(void) { return (*pick())[0] - 7 + (*pick())[1] - 8; }
EOF
if "$compiler" -w -o "$tmp/rooted" "$tmp/rooted.c" >/dev/null 2>&1 && "$tmp/rooted"; then
    echo "testing a rooted name's references stay alive ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a rooted name's references stay alive ... FAILED"
    n_fail=$((n_fail + 1))
fi

# --- a member whose type is a qualified copy of its own struct ---------

# `struct V { volatile struct V *next; }` copies V to attach the qualifier
# while V is still being defined, and the copy has to learn V's members when
# V is laid out. It did not, so a second `->` through such a member lost the
# members: `head->next->next` was "no member named 'next'". git's list.h has
# exactly that shape (`volatile struct volatile_list_head *next, *prev`) and
# it stopped twelve of git's translation units.
cat > "$tmp/qualcopy.c" <<'EOF'
struct V {
    volatile struct V *next, *prev;
    int payload;
};
struct W {
    const struct W *link;
    int v;
};

static void chain(struct V *head, struct V *newp) {
    head->next->prev = newp;
    newp->next = head->next;
    newp->prev = head;
    head->next = newp;
    newp->next->payload = 1;
}

int main(void) {
    struct V a = { &a, &a, 0 }, b = { 0, 0, 0 };
    struct W w = { 0, 7 };
    chain(&a, &b);
    if (a.next != &b || b.prev != &a || b.next != &a) return 1;
    if (a.payload != 1) return 2;
    if (w.link != 0 || w.v != 7) return 3;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/qualcopy" "$tmp/qualcopy.c" >/dev/null 2>&1 && "$tmp/qualcopy"; then
    echo "testing a member of a qualified copy of its own struct ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a member of a qualified copy of its own struct ... FAILED"
    n_fail=$((n_fail + 1))
fi

# --- what this round's real code found -------------------------------
# cpython, git, tinycc and libpng each stopped cxx on something the suite had
# no case for. One check per thing, so the next round starts from here.

# The command-line directives share one buffer and the offset the next one is
# written at was counted by hand -- but `#define NAME 1` is two characters
# longer than that count allowed for, so each definition was written over the
# tail of the one before it. Of a run of -D without a value only the first
# survived, which is why -DNDEBUG ate cpython's -DPy_BUILD_CORE and its
# headers answered with `#error "this header requires Py_BUILD_CORE define"`.
cat > "$tmp/manydefs.c" <<'EOF'
#ifndef ONE
#error ONE
#endif
#ifndef TWO
#error TWO
#endif
#ifndef THREE
#error THREE
#endif
#ifndef FOUR
#error FOUR
#endif
int f(void) { return ONE + TWO + THREE + FOUR; }
EOF
if "$compiler" -w -DONE -DTWO -DTHREE -DFOUR -c -o /dev/null "$tmp/manydefs.c" > "$tmp/log" 2>&1; then
    echo "testing a run of -D without a value ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a run of -D without a value ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# With a -D present the stream the preprocessor walks starts with the command
# line's own directives, and the display name was taken from that first token:
# every diagnostic in the file was reported at `<command line>`.
cat > "$tmp/where.c" <<'EOF'
int f(void) { return no_such_function(); }
EOF
"$compiler" -w -DUNUSED -c -o /dev/null "$tmp/where.c" > "$tmp/log" 2>&1
if grep -q 'where\.c:' "$tmp/log"; then
    echo "testing a diagnostic names the file, not the command line ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a diagnostic names the file, not the command line ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -3
    n_fail=$((n_fail + 1))
fi

# A macro name passed as an argument is only *called* once the body puts a
# parenthesis after it, so what the argument expansion left unpainted has to
# stay expandable: libpng's PNG_IMAGE_PIXEL_(PNG_IMAGE_SAMPLE_CHANNELS, fmt)
# writes `test(fmt)` in its body. Marking every token of an expanded argument
# "do not expand again" -- what this used to do -- left the call unresolved.
cat > "$tmp/argcall.c" <<'EOF'
#define SAMPLE_CHANNELS(fmt) (((fmt) & 3) + 1)
#define PIXEL_(test, fmt) (((fmt) & 4) ? 1 : test(fmt))
#define PIXEL_CHANNELS(fmt) PIXEL_(SAMPLE_CHANNELS, fmt)
int main(void) { return PIXEL_CHANNELS(2) == 3 ? 0 : 1; }
EOF
if "$compiler" -w -o "$tmp/argcall" "$tmp/argcall.c" >/dev/null 2>&1 && "$tmp/argcall"; then
    echo "testing a macro name called where the body calls it ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a macro name called where the body calls it ... FAILED"
    "$compiler" -w -o "$tmp/argcall" "$tmp/argcall.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# A record's constant initializer is one byte image, and the elements the type
# is written with may cut a bit-field in half: a 12-bit field's access unit is
# two bytes, so the 8-bit field after it starts inside that element and ends
# in the next one. libpng's read_chunks table is a record of six such fields,
# and the elements were walked by grouping fields with equal offsets, which
# invented one value per field and left a negative-length pad, `[-1 x i8]`,
# between two of them. Every field has to be written where it lies.
cat > "$tmp/bfimage.c" <<'EOF'
struct B {
    unsigned a : 12, b : 8, c : 4, d : 4, e : 1;
};
static const struct B bt = { 13, 208, 15, 9, 1 };
int main(void) {
    if (sizeof bt != 4) return 20;
    unsigned one = 1;
    if (*(unsigned char *)&one == 1) {
        static const unsigned char want[4] = {0x0d, 0x00, 0xfd, 0x19};
        const unsigned char *p = (const unsigned char *)&bt;
        for (int i = 0; i < 4; i++)
            if (p[i] != want[i]) return i + 1;
    }
    if (bt.a != 13 || bt.b != 208 || bt.c != 15 || bt.d != 9 || bt.e != 1) return 10;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/bfimage" "$tmp/bfimage.c" >/dev/null 2>&1 && "$tmp/bfimage"; then
    echo "testing a bit-field constant record image ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a bit-field constant record image ... FAILED"
    "$compiler" -w -o "$tmp/bfimage" "$tmp/bfimage.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# 6.7.6.3p15: a parameter declared with a qualified type is taken as having
# the unqualified version of it. tinycc's tcc.h declares `strtof` and
# `strtold` again with no `restrict` against glibc's <stdlib.h>, and the
# qualifier check rejected every one of its thirty sources.
cat > "$tmp/paramqual.c" <<'EOF'
#include <stdlib.h>
extern float strtof(const char *nptr, char **endptr);
extern long double strtold(const char *nptr, char **endptr);
extern int f(const char *restrict, char **restrict);
extern int f(const char *, char **);
int main(void) { return 0; }
EOF
if "$compiler" -w -c -o /dev/null "$tmp/paramqual.c" > "$tmp/log" 2>&1; then
    echo "testing a parameter's qualifier does not break compatibility ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a parameter's qualifier does not break compatibility ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# 6.10.3.4p1: the replacement list is rescanned together with the tokens that
# follow the invocation, so a replacement that ends in the name of a
# function-like macro is a call when the file puts a parenthesis after it.
# tinycc writes `ELFW(ST_BIND)(sym->st_info)` over
# `#define ELFW(type) ELF64_##type`; expanding the body on its own left
# `ELF64_ST_BIND(...)` standing as an implicit function declaration.
cat > "$tmp/pastecall.c" <<'EOF'
#define ELFW(type) ELF64_##type
#define ELF64_ST_BIND(v) (((v) >> 4) & 0xf)
#define ELF64_ST_INFO(b, t) (((b) << 4) + ((t) & 0xf))
int f(int info) { return ELFW(ST_BIND)(info); }
int main(void) { return f(0x21) == 2 ? 0 : 1; }
EOF
if "$compiler" -w -o "$tmp/pastecall" "$tmp/pastecall.c" >/dev/null 2>&1 && "$tmp/pastecall"; then
    echo "testing a pasted macro name called by the source ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a pasted macro name called by the source ... FAILED"
    "$compiler" -w -o "$tmp/pastecall" "$tmp/pastecall.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# GNU `__extension__` marks the expression after it as one to be accepted
# without a pedantic diagnostic, and what follows it is a *cast* expression:
# lua's `#define cast_func(p) (__extension__ (voidf)(p))` re-entered the
# expression parser below the cast, so the `(voidf)(p)` was no longer read as
# one and the initialisation it feeds was rejected.
cat > "$tmp/extension.c" <<'EOF'
typedef void (*voidf)(void);
typedef int (*lua_CFunction)(void *);
void *dlsym(void *, const char *);
#define cast(t, e) ((t)(e))
#define cast_func(p) (__extension__ (voidf)(p))
#define cast_Lfunc(p) cast(lua_CFunction, cast_func(p))
int main(void) { lua_CFunction f = cast_Lfunc(dlsym(0, "x")); return f == 0 ? 0 : 1; }
EOF
if "$compiler" -w -c -o /dev/null "$tmp/extension.c" > "$tmp/log" 2>&1; then
    echo "testing __extension__ before a cast ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing __extension__ before a cast ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# 6.9.2p2: a declaration with `extern` and no initializer is not a
# definition, and only a definition needs a complete type. cpython declares
# every object in PyAPI_DATA that way, and git's headers declare records the
# same way; cxx rejected each one as `variable 'X' has incomplete type`.
cat > "$tmp/externinc.c" <<'EOF'
struct Never;
typedef struct Never NeverT;
extern struct Never a;
extern NeverT b;
void f(void) {
    extern NeverT c;
    (void)&c;
}
int main(void) { return 0; }
EOF
if "$compiler" -w -c -o /dev/null "$tmp/externinc.c" > "$tmp/log" 2>&1; then
    echo "testing an extern declaration of an incomplete type ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing an extern declaration of an incomplete type ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# GCC's generic atomics address the value instead of passing it:
# __atomic_load(ptr, ret, order) and __atomic_store(ptr, val, order). They are
# not aliases of the _n spelling -- the arguments differ -- and they take the
# address of an ordinary object, where the C11 spelling requires an _Atomic
# one. cpython's pyatomic_gcc.h writes both for every width it has no _n form
# for, which was the single blocker behind 322 of its 371 failing units.
cat > "$tmp/genericatomic.c" <<'EOF'
struct S { int a; double d; };
int main(void) {
    int x = 7, y = 0;
    double p = 1.5, q = 0;
    struct S s = { 3, 2.5 }, t = { 0, 0 };
    __atomic_load(&x, &y, __ATOMIC_RELAXED);
    __atomic_load(&p, &q, __ATOMIC_SEQ_CST);
    __atomic_load(&s, &t, __ATOMIC_RELAXED);
    __atomic_store(&y, &x, __ATOMIC_RELAXED);
    if (y != 7 || q != 1.5 || t.a != 3 || t.d != 2.5) return 1;
    return __atomic_load_n(&x, __ATOMIC_RELAXED) == 7 ? 0 : 2;
}
EOF
if "$compiler" -w -o "$tmp/genericatomic" "$tmp/genericatomic.c" >/dev/null 2>&1 && "$tmp/genericatomic"; then
    echo "testing the generic __atomic load and store ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the generic __atomic load and store ... FAILED"
    "$compiler" -w -o "$tmp/genericatomic" "$tmp/genericatomic.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# GCC spells the overflow builtins once per operation and once per type, and
# cpython's bundled mimalloc calls __builtin_umull_overflow: those calls were
# the whole of a 13-translation-unit sample's "implicit declaration" class.
cat > "$tmp/typedovf.c" <<'EOF'
int main(void) {
    unsigned long u = 0;
    unsigned long long uu = 0;
    long s = 0;
    int bad = 0;
    bad += __builtin_umull_overflow(3UL, 4UL, &u) != 0 || u != 12;
    bad += __builtin_umul_overflow(3U, 4U, (unsigned *)&u) != 0;
    bad += __builtin_umull_overflow(1UL << 40, 1UL << 40, &u) != 1;
    bad += __builtin_smull_overflow(-3L, 4L, &s) != 0 || s != -12;
    bad += __builtin_smulll_overflow(1LL << 62, 4LL, (long long *)&uu) != 1;
    bad += __builtin_mul_overflow(6ULL, 7ULL, &uu) != 0 || uu != 42;
    return bad;
}
EOF
if "$compiler" -w -o "$tmp/typedovf" "$tmp/typedovf.c" >/dev/null 2>&1 && "$tmp/typedovf"; then
    echo "testing the typed overflow builtins ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the typed overflow builtins ... FAILED"
    "$compiler" -w -o "$tmp/typedovf" "$tmp/typedovf.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# __builtin_assume_aligned and __builtin_unreachable: cpython's mimalloc and
# its Py_UNREACHABLE reach for both. The first is the pointer it is given; the
# second is a no-op here, because cxx has no unreachable terminator to emit --
# reaching it is undefined either way, so no result depends on which it is.
cat > "$tmp/gnubuiltins.c" <<'EOF'
#include <string.h>
int classify(int x) {
    switch (x) {
        case 1: return 10;
        case 2: return 20;
        default: __builtin_unreachable();
    }
}
int main(void) {
    char buf[64];
    strcpy(buf, "ok");
    char *p = __builtin_assume_aligned(buf, 16);
    void *v = __builtin_assume_aligned((void *)buf, 8, 0);
    if (p != buf || v != (void *)buf) return 1;
    if (classify(1) != 10 || classify(2) != 20) return 2;
    return p[0] == 'o' ? 0 : 3;
}
EOF
if "$compiler" -w -o "$tmp/gnubuiltins" "$tmp/gnubuiltins.c" >/dev/null 2>&1 && "$tmp/gnubuiltins"; then
    echo "testing assume_aligned and unreachable ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing assume_aligned and unreachable ... FAILED"
    "$compiler" -w -o "$tmp/gnubuiltins" "$tmp/gnubuiltins.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# `_Pragma` is an operator, not a function: wherever it is written it has to be
# consumed before the parser sees it, including when a wrapper macro carries it
# -- cpython's pyport.h defines _Py_COMP_DIAG_PUSH that way and those wrappers
# reached the parser as calls.
cat > "$tmp/pragmaop.c" <<'EOF'
#define _Py_COMP_DIAG_PUSH _Pragma("GCC diagnostic push")
#define _Py_COMP_DIAG_IGNORE_DEPR_DECLS \
    _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define _Py_COMP_DIAG_POP _Pragma("GCC diagnostic pop")
_Py_COMP_DIAG_PUSH
_Py_COMP_DIAG_IGNORE_DEPR_DECLS
static inline int f(void) { return 1; }
_Py_COMP_DIAG_POP
int main(void) { return f() == 1 ? 0 : 1; }
EOF
if "$compiler" -w -o "$tmp/pragmaop" "$tmp/pragmaop.c" >/dev/null 2>&1 && "$tmp/pragmaop"; then
    echo "testing a macro that carries _Pragma ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a macro that carries _Pragma ... FAILED"
    "$compiler" -w -o "$tmp/pragmaop" "$tmp/pragmaop.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# <limits.h> is clang's own, and it does `#include_next <limits.h>` after
# defining _GCC_LIMITS_H_. GCC's include directory must therefore not be next
# in line, or that directory's copy steps aside as asked and glibc's
# <bits/posix1_lim.h> -- where SSIZE_MAX lives -- is never read. cpython's
# pyport.h asks for SSIZE_MAX, and 22 units of a 60-unit sample failed on it.
cat > "$tmp/ssizemax.c" <<'EOF'
#define _GNU_SOURCE 1
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
int main(void) {
    if (SSIZE_MAX != LONG_MAX) return 1;
    if (SIZE_MAX <= (size_t)SSIZE_MAX) return 2;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/ssizemax" "$tmp/ssizemax.c" >/dev/null 2>&1 && "$tmp/ssizemax"; then
    echo "testing SSIZE_MAX from <limits.h> ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing SSIZE_MAX from <limits.h> ... FAILED"
    "$compiler" -w -o "$tmp/ssizemax" "$tmp/ssizemax.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# va_end closes a va_list that may have been handed to a function that is not
# itself variadic -- the vprintf shape. Only va_start has to be in a variadic
# function (7.16.1.1p1); gcc and clang both accept the rest, and cpython's
# object_vacall(), git's helpers and tinycc's all do it.
cat > "$tmp/fwdvalist.c" <<'EOF'
#include <stdarg.h>
static int sum(int n, va_list ap) {
    int total = 0;
    va_list copy;
    va_copy(copy, ap);
    for (int i = 0; i < n; i++) total += va_arg(ap, int);
    va_end(copy);
    va_end(ap);
    return total;
}
static int call(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int total = sum(n, ap);
    va_end(ap);
    return total;
}
int main(void) { return call(3, 1, 2, 3) == 6 ? 0 : 1; }
EOF
if "$compiler" -w -o "$tmp/fwdvalist" "$tmp/fwdvalist.c" >/dev/null 2>&1 && "$tmp/fwdvalist"; then
    echo "testing va_end on a forwarded va_list ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing va_end on a forwarded va_list ... FAILED"
    "$compiler" -w -o "$tmp/fwdvalist" "$tmp/fwdvalist.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# GCC's dialect alternatives in an extended asm template: `{att|intel}` picks
# one arm for the assembler in use, and LLVM spells it `$(att$|intel$)`. A
# *basic* statement is not rewritten at all -- cpython's pycore_pystate.h
# writes the extended form for x86-64, and every module that includes it
# failed with `Expected '}'` until the rewrite landed.
cat > "$tmp/dialect.c" <<'EOF'
#include <stdint.h>
#include <stddef.h>
static uintptr_t stack_pointer(void) {
    uintptr_t result;
    __asm__ ("{movq %%rsp, %0|mov %0, rsp}" : "=r" (result));
    return result;
}
int main(void) {
    uintptr_t sp = stack_pointer();
    char here;
    uintptr_t local = (uintptr_t)&here;
    uintptr_t diff = sp > local ? sp - local : local - sp;
    return (sp != 0 && diff < 65536) ? 0 : 1;
}
EOF
if "$compiler" -w -o "$tmp/dialect" "$tmp/dialect.c" >/dev/null 2>&1 && "$tmp/dialect"; then
    echo "testing asm dialect alternatives ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing asm dialect alternatives ... FAILED"
    "$compiler" -w -o "$tmp/dialect" "$tmp/dialect.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# A pointer conversion that only drops a qualifier is a constraint violation
# (6.5.16.1p1) that both references diagnose and then compile through:
# `free(p)` with a `const char *p` is -Wdiscarded-qualifiers in gcc and
# -Wincompatible-pointer-types-discards-qualifiers in clang, and cxx refused
# to compile git's bloom.c, tinycc's tccrun.c and five cpython units over it.
# A function designator reaching a `void *` parameter is the GNU extension
# both references accept in silence.
cat > "$tmp/qualconv.c" <<'EOF'
static void take(const void *p) { (void)p; }
static void release(void *p) { (void)p; }
static void fn(int a) { (void)a; }
int main(void) {
    const char *c = "x";
    take(c);
    release(c);
    char *q = c;
    (void)q;
    take(fn);
    return c[0] == 'x' ? 0 : 1;
}
EOF
if "$compiler" -w -o "$tmp/qualconv" "$tmp/qualconv.c" >/dev/null 2>&1 && "$tmp/qualconv"; then
    echo "testing a qualifier-dropping pointer conversion ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a qualifier-dropping pointer conversion ... FAILED"
    "$compiler" -w -o "$tmp/qualconv" "$tmp/qualconv.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# ... and it is a diagnostic, not a silence: gcc's group name has to turn it
# on, -w and -Wno-discarded-qualifiers have to turn it off.
if "$compiler" -Wdiscarded-qualifiers -c -o /dev/null "$tmp/qualconv.c" > "$tmp/log" 2>&1 &&
    grep -q 'discards qualifiers' "$tmp/log" &&
    "$compiler" -w -c -o /dev/null "$tmp/qualconv.c" > "$tmp/log2" 2>&1 &&
    ! grep -q 'discards qualifiers' "$tmp/log2"; then
    echo "testing -Wdiscarded-qualifiers names the warning ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -Wdiscarded-qualifiers names the warning ... FAILED"
    head -3 "$tmp/log" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# 6.10.3.4p1: the replacement is rescanned together with the tokens that
# follow the invocation, so a replacement ending in a function-like macro's
# name is a call when the file supplies the parenthesis. tinycc writes
# `ELFW(ST_BIND)(x)` over `#define ELFW(type) ELF64_##type`, and
# `#define WRAP ELFW(ST_TYPE)` followed by `WRAP(b)`; the rescan also has to
# run outside the invocation's own window of disabled names, or the inner
# `ELFW(ST_TYPE)(b)` in `ELFW(ST_INFO)(a, ELFW(ST_TYPE)(b))` stays unexpanded.
cat > "$tmp/rescancall.c" <<'EOF'
#define ELFW(type) ELF64_##type
#define ELF64_ST_TYPE(v) ((v) & 0xf)
#define ELF64_ST_BIND(v) (((v) >> 4) & 0xf)
#define ELF64_ST_INFO(b, t) (((b) << 4) + ((t) & 0xf))
#define WRAP ELFW(ST_TYPE)
int f(int info) { return ELFW(ST_BIND)(info); }
int g(int b, int t) { return ELFW(ST_INFO)(b, ELFW(ST_TYPE)(t)); }
int h(int v) { return WRAP(v); }
int main(void) {
    if (f(0x21) != 2) return 1;
    if (g(2, 1) != 0x21) return 2;
    if (h(0x1f) != 0xf) return 3;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/rescancall" "$tmp/rescancall.c" >/dev/null 2>&1 && "$tmp/rescancall"; then
    echo "testing a replacement rescanned with what follows ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a replacement rescanned with what follows ... FAILED"
    "$compiler" -w -o "$tmp/rescancall" "$tmp/rescancall.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# A file is guarded only when its *guard* runs to the end of it. Any trailing
# `#endif` used to count, so a header that does something after its guard --
# tinycc's tcc.h selects TCC_SET_STATE below `#endif _TCC_H` -- was skipped
# whole on the second read and its tail never re-evaluated.
mkdir -p "$tmp/guard"
cat > "$tmp/guard/g.h" <<'EOF'
#ifndef G_H
#define G_H
#define BODY_SEEN 1
#endif

#undef PICKED
#ifdef FLAG
#define PICKED 2
#else
#define PICKED 3
#endif
EOF
cat > "$tmp/guard.c" <<'EOF'
#include "guard/g.h"
#define FLAG 1
#include "guard/g.h"
int main(void) { return PICKED == 2 ? 0 : 1; }
EOF
if "$compiler" -w -I"$tmp" -o "$tmp/guardprog" "$tmp/guard.c" >/dev/null 2>&1 && "$tmp/guardprog"; then
    echo "testing code after an include guard's #endif ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing code after an include guard's #endif ... FAILED"
    "$compiler" -w -I"$tmp" -o "$tmp/guardprog" "$tmp/guard.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# `__attribute` with one pair of underscores is the older spelling of
# `__attribute__`, and both references take it; tinycc's lib/dsohandle.c
# writes `void *h __attribute((visibility("hidden"))) = &h;`.
cat > "$tmp/attrspell.c" <<'EOF'
void *h __attribute((visibility("hidden"))) = &h;
int f(void) __attribute((noinline));
int f(void) { return 0; }
int main(void) { return h == &h && f() == 0 ? 0 : 1; }
EOF
if "$compiler" -w -o "$tmp/attrspell" "$tmp/attrspell.c" >/dev/null 2>&1 && "$tmp/attrspell"; then
    echo "testing __attribute with one pair of underscores ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing __attribute with one pair of underscores ... FAILED"
    "$compiler" -w -o "$tmp/attrspell" "$tmp/attrspell.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# GNU C gives `void *` arithmetic a byte step, and `getelementptr void` is not
# IR: LLVM answers "void type only allowed for function results". tinycc's
# __bound_ptr_add() returns `p + offset` on a `void *`, and four cpython units
# had the same shape.
cat > "$tmp/voidarith.c" <<'EOF'
#include <string.h>
int main(void) {
    char buf[16];
    strcpy(buf, "abcdef");
    void *p = buf;
    void *q = p + 2;
    void *r = q;
    r += 3;
    void *s = r++;
    if ((char *)q != buf + 2) return 1;
    if ((char *)r != buf + 6) return 2;
    if ((char *)s != buf + 5) return 3;
    if ((char *)(s - 1) != buf + 4) return 4;
    return strcmp((char *)q, "cdef") == 0 ? 0 : 5;
}
EOF
if "$compiler" -w -o "$tmp/voidarith" "$tmp/voidarith.c" >/dev/null 2>&1 && "$tmp/voidarith"; then
    echo "testing void pointer arithmetic ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing void pointer arithmetic ... FAILED"
    "$compiler" -w -o "$tmp/voidarith" "$tmp/voidarith.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# __builtin_frame_address and __builtin_return_address, and the constant the
# level has to be: without the check the IR carried a non-immediate immarg and
# LLVM refused it, which is a diagnostic about the IR rather than about the
# program. tinycc's backtrace stubs are the callers that exist.
cat > "$tmp/frameaddr.c" <<'EOF'
#include <stdint.h>
static int check(void) {
    uintptr_t here = (uintptr_t)__builtin_frame_address(0);
    uintptr_t local = (uintptr_t)&here;
    long d = (long)(here > local ? here - local : local - here);
    if (here == 0 || d > 65536) return 1;
    if (__builtin_return_address(0) == 0) return 2;
    if (__builtin_frame_address(1) == 0) return 3;
    return 0;
}
int main(void) { return check(); }
EOF
if "$compiler" -w -o "$tmp/frameaddr" "$tmp/frameaddr.c" >/dev/null 2>&1 && "$tmp/frameaddr"; then
    echo "testing frame and return address ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing frame and return address ... FAILED"
    "$compiler" -w -o "$tmp/frameaddr" "$tmp/frameaddr.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

cat > "$tmp/framevar.c" <<'EOF'
void *f(unsigned n) { return __builtin_frame_address(n); }
EOF
"$compiler" -w -c -o /dev/null "$tmp/framevar.c" > "$tmp/log" 2>&1
if grep -q 'must be a constant integer' "$tmp/log"; then
    echo "testing a non-constant frame level is diagnosed ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a non-constant frame level is diagnosed ... FAILED"
    head -3 "$tmp/log" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# The x86-64 psABI names the fields of __va_list_tag, and clang's builtin type
# has them: code that walks the record spells them. tinycc's lib/va_list.c
# implements __va_arg on top of ap->gp_offset and friends.
cat > "$tmp/valistfields.c" <<'EOF'
#include <stdarg.h>
#include <stddef.h>
int main(void) {
    va_list ap;
    /* Written and read by name: the fields are the point, not varargs. */
    ap->gp_offset = 1;
    ap->fp_offset = 2;
    ap->overflow_arg_area = 0;
    ap->reg_save_area = 0;
    /* The record's own layout, measured through the names above. */
    if ((char *)&ap->fp_offset - (char *)ap != 4) return 1;
    if ((char *)&ap->overflow_arg_area - (char *)ap != 8) return 2;
    if ((char *)&ap->reg_save_area - (char *)ap != 16) return 3;
    return ap->gp_offset == 1 && ap->fp_offset == 2 ? 0 : 4;
}
EOF
if "$compiler" -w -o "$tmp/valistfields" "$tmp/valistfields.c" >/dev/null 2>&1 && "$tmp/valistfields"; then
    echo "testing the va_list field names ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the va_list field names ... FAILED"
    "$compiler" -w -o "$tmp/valistfields" "$tmp/valistfields.c" 2>&1 | head -4 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# __atomic_compare_exchange is the _n form with the desired value addressed,
# and it writes the current value back through `expected` when it fails.
cat > "$tmp/genericacx.c" <<'EOF'
#include <stdatomic.h>
int main(void) {
    int v = 5, cmp, xchg;
    cmp = 5;
    xchg = 9;
    if (!__atomic_compare_exchange(&v, &cmp, &xchg, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return 1;
    if (v != 9 || cmp != 5) return 2;
    cmp = 5;
    xchg = 7;
    if (__atomic_compare_exchange(&v, &cmp, &xchg, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return 3;
    if (v != 9 || cmp != 9) return 4;
    unsigned char b = 1, bc = 1, bx = 2;
    if (!__atomic_compare_exchange(&b, &bc, &bx, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return 5;
    return b == 2 ? 0 : 6;
}
EOF
if "$compiler" -w -o "$tmp/genericacx" "$tmp/genericacx.c" >/dev/null 2>&1 && "$tmp/genericacx"; then
    echo "testing the generic __atomic_compare_exchange ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the generic __atomic_compare_exchange ... FAILED"
    "$compiler" -w -o "$tmp/genericacx" "$tmp/genericacx.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# A record is named in a diagnostic by its tag. `uid` is the name the IR
# prints and is 0 for the records the compiler builds itself -- the ABI's
# va_list, an aggregate shape -- where str(0) is not a string: a member lookup
# on one reported "no member named 'gp_offset' in '__INT_FAST8_TYPE__'", which
# is whatever the interning table's first slot happened to hold.
# cxx stops at the first error, so each shape gets its own file.
recdiag_ok=1
for want in "struct S" "union U" "struct <anonymous>"; do
    case $want in
        "struct S") decl="struct S { int a; } x;" ;;
        "union U") decl="union U { int a; } x;" ;;
        *) decl="struct { int a; } x;" ;;
    esac
    printf '%s\nint f(void) { return x.b; }\n' "$decl" > "$tmp/recdiag.c"
    "$compiler" -w -c -o /dev/null "$tmp/recdiag.c" > "$tmp/log" 2>&1
    grep -q "in .$want." "$tmp/log" || recdiag_ok=0
    grep -q '__INT_FAST8_TYPE__' "$tmp/log" && recdiag_ok=0
done
if [ "$recdiag_ok" = 1 ]; then
    echo "testing a record's name in a diagnostic ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a record's name in a diagnostic ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -5
    n_fail=$((n_fail + 1))
fi

cat > "$tmp/recdiag2.c" <<'EOF'
#include <stdarg.h>
unsigned f(va_list ap) { return ap->gp_offset + ap->nope; }
EOF
"$compiler" -w -c -o /dev/null "$tmp/recdiag2.c" > "$tmp/log" 2>&1
if grep -q '__va_list_tag' "$tmp/log"; then
    echo "testing a builtin record's name in a diagnostic ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a builtin record's name in a diagnostic ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# glibc declares the address parameter of connect(), bind() and accept() as a
# transparent union under _GNU_SOURCE -- a union of `struct sockaddr *` and
# friends with __attribute__((transparent_union)) -- so the call passes one of
# the members, never the union. Without it every socket call is an
# incompatible-types error.
cat > "$tmp/transparent.c" <<'EOF'
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
union my_arg {
    const struct sockaddr *sa;
    const void *v;
} __attribute__((transparent_union));
static int takes(union my_arg a) { return a.v == 0 ? 0 : 1; }
int main(void) {
    struct sockaddr_storage ss;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        (void)connect(fd, (struct sockaddr *)&ss, sizeof(ss));
        close(fd);
    }
    return takes((const void *)0) == 0 && takes(&ss) == 1 ? 0 : 1;
}
EOF
if "$compiler" -w -o "$tmp/transparent" "$tmp/transparent.c" >/dev/null 2>&1 && "$tmp/transparent"; then
    echo "testing a transparent union parameter ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a transparent union parameter ... FAILED"
    "$compiler" -w -o "$tmp/transparent" "$tmp/transparent.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# -E output has to re-tokenize to the same tokens (6.10.3.3). An argument's own
# leading whitespace says nothing about the token it follows after
# substitution: `#define COMMON(prefix) sa_family_t prefix;` called as
# `COMMON(sa_family)` printed `sa_family_tsa_family;`.
cat > "$tmp/retok.c" <<'EOF'
#define COMMON(prefix) sa_family_t prefix;
typedef int sa_family_t;
struct s {
    COMMON(sa_family)
};
#define P +
int y = 1 P + 2;
#define N 5
int z = N;
EOF
"$compiler" -E -P "$tmp/retok.c" > "$tmp/retok_pre.c" 2>&1
"$compiler" -w -c -o /dev/null "$tmp/retok_pre.c" > "$tmp/log" 2>&1
if grep -q 'sa_family_t sa_family;' "$tmp/retok_pre.c" && grep -q 'int y = 1 + + 2;' "$tmp/retok_pre.c" &&
    grep -q 'int z = 5;' "$tmp/retok_pre.c" && [ ! -s "$tmp/log" ]; then
    echo "testing -E output re-tokenizes ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -E output re-tokenizes ... FAILED"
    head -4 "$tmp/retok_pre.c" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# 6.7.6.3p4: a declaration may name an incomplete parameter type (git's
# reflog-walk.h does), a definition may not. The check used to sit in the
# parameter parser and dereference a name token an unnamed parameter has not.
cat > "$tmp/incparam.c" <<'EOF'
struct date_mode;
void show(struct date_mode, int force);
int main(void) { return 0; }
EOF
cat > "$tmp/incparam2.c" <<'EOF'
struct date_mode;
void show(struct date_mode m) { (void)m; }
EOF
"$compiler" -w -c -o /dev/null "$tmp/incparam.c" > "$tmp/log" 2>&1
if [ ! -s "$tmp/log" ] &&
    "$compiler" -w -c -o /dev/null "$tmp/incparam2.c" > "$tmp/log2" 2>&1; then
    echo "testing an incomplete parameter type ... FAILED"
    echo "    the definition was accepted"
    sed 's/^/    /' "$tmp/log" | head -3
    n_fail=$((n_fail + 1))
elif grep -q 'incomplete type' "$tmp/log2"; then
    echo "testing an incomplete parameter type ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing an incomplete parameter type ... FAILED"
    head -2 "$tmp/log2" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# 6.7.6.3p2: `register` is the only storage class a parameter may carry, and
# git's kwset.c writes `register struct tree const *tree`.
cat > "$tmp/regparam.c" <<'EOF'
struct tree { int x; };
static int depth(register struct tree const *t) { return t ? t->x : 0; }
static int plain(int register n) { return n; }
int main(void) { struct tree t = {7}; return depth(&t) + plain(1) == 8 ? 0 : 1; }
EOF
if "$compiler" -w -o "$tmp/regparam" "$tmp/regparam.c" >/dev/null 2>&1 && "$tmp/regparam"; then
    echo "testing a register parameter ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a register parameter ... FAILED"
    "$compiler" -w -o "$tmp/regparam" "$tmp/regparam.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# `noreturn` on a function pointer is a type both references have -- git's
# usage.c declares `static __attribute__((noreturn)) report_fn usage_routine`
# with report_fn a pointer typedef -- and on anything else it is ignored with
# a warning, which is what -Wattributes and -Wignored-attributes say.
cat > "$tmp/noreturnptr.c" <<'EOF'
typedef void (*report_fn)(const char *err, ...);
static void builtin_fn(const char *err, ...) { (void)err; }
static __attribute__((noreturn)) report_fn routine = builtin_fn;
static __attribute__((noreturn)) int counter = 1;
int main(void) { return routine == builtin_fn && counter == 1 ? 0 : 1; }
EOF
"$compiler" -c -o /dev/null "$tmp/noreturnptr.c" > "$tmp/log" 2>&1
if grep -q 'noreturn. attribute ignored' "$tmp/log" && ! grep -q 'error' "$tmp/log" &&
    "$compiler" -w -o "$tmp/noreturnptr" "$tmp/noreturnptr.c" >/dev/null 2>&1 && "$tmp/noreturnptr"; then
    echo "testing noreturn on a function pointer ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing noreturn on a function pointer ... FAILED"
    head -3 "$tmp/log" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# 6.7.2.2p4: an enumerated type is compatible with the implementation's choice
# of integer type -- unsigned here when no enumerator is negative, as both
# references do it. git's odb/source-packed.c assigns a `int (..., unsigned)`
# function to a `int (..., enum odb_write_object_flags)` member.
cat > "$tmp/enumcompat.c" <<'EOF'
enum odb_write_object_flags { ODB_WRITE_OBJECT_NOOP = 1 };
typedef int (*writer)(const void *buf, enum odb_write_object_flags flags);
static int impl(const void *buf, unsigned flags);
int main(void) {
    writer w = impl;
    if ((enum odb_write_object_flags)-1 < 0) return 1;
    return w(0, 0) == 0 ? 0 : 2;
}
static int impl(const void *buf, unsigned flags) { (void)buf; return (int)flags; }
EOF
if "$compiler" -w -o "$tmp/enumcompat" "$tmp/enumcompat.c" >/dev/null 2>&1 && "$tmp/enumcompat"; then
    echo "testing an enum against its integer type ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing an enum against its integer type ... FAILED"
    "$compiler" -w -o "$tmp/enumcompat" "$tmp/enumcompat.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# A union initializer written through a member that is not the union's
# canonical one is emitted as that member (clang's type-punning form), and the
# type containing it then has to be spelled out the same way or LLVM reports
# "element 0 of struct initializer doesn't match struct element type".
# cpython's struct _object is exactly this shape: a union of an int64_t
# refcount and a struct of three smaller fields.
cat > "$tmp/punnedunion.c" <<'EOF'
#include <stdint.h>
struct _object {
    __extension__ union {
        int64_t ob_refcnt_full;
        struct {
            uint32_t ob_refcnt;
            uint16_t ob_overflow;
            uint16_t ob_flags;
        };
    };
    void *ob_type;
};
struct _object none = { { 3221225472U }, 0 };
struct _object flags = { { .ob_flags = 5 }, 0 };
union named { int64_t full; struct { uint32_t a; uint16_t b, c; } inner; };
union named y = { .inner = { 7, 8, 9 } };
int main(void) {
    if (none.ob_refcnt != 3221225472U) return 1;
    if (none.ob_flags != 0) return 2;
    if (flags.ob_flags != 5) return 3;
    if (y.inner.a != 7 || y.inner.b != 8 || y.inner.c != 9) return 4;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/punnedunion" "$tmp/punnedunion.c" >/dev/null 2>&1 && "$tmp/punnedunion"; then
    echo "testing a punned union in a named record ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a punned union in a named record ... FAILED"
    "$compiler" -w -o "$tmp/punnedunion" "$tmp/punnedunion.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# The same through an array, where each element can write a different member:
# a packed struct of per-element element types, which is what an array is.
cat > "$tmp/punnedarray.c" <<'EOF'
#include <stdint.h>
struct slot {
    int kind;
    union {
        int64_t wide;
        struct { uint32_t lo; uint16_t mid, hi; } parts;
    };
};
struct slot slots[] = {
    { 1, { .wide = 9 } },
    { 2, { .parts = { 0x11, 0x22, 0x33 } } },
    { 3, { .parts = { 0x44, 0x55, 0x66 } } },
};
int main(void) {
    if (slots[0].wide != 9) return 1;
    if (slots[1].parts.lo != 0x11 || slots[1].parts.mid != 0x22 || slots[1].parts.hi != 0x33) return 2;
    if (slots[2].parts.lo != 0x44 || slots[2].parts.hi != 0x66) return 3;
    if (sizeof(slots) != 3 * sizeof(struct slot)) return 4;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/punnedarray" "$tmp/punnedarray.c" >/dev/null 2>&1 && "$tmp/punnedarray"; then
    echo "testing a punned union in an array ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a punned union in an array ... FAILED"
    "$compiler" -w -o "$tmp/punnedarray" "$tmp/punnedarray.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# An attribute's argument list may run over several lines. curl's
# typecheck-gcc.h declares its warnings as
#
#   static void __attribute__((__warning__(
#   "curl_easy_setopt expects a long argument for this option"))) id(void) ...
#
# and a token at the start of a line used to end the argument list, so every
# curl_easy_setopt call was `expected ')'`.
cat > "$tmp/attrmultiline.c" <<'EOF'
static void __attribute__((__warning__(
"an old function"))) __attribute__((__unused__)) __attribute__((__noinline__)) old_one(void)
{
    __asm__("");
}
void __attribute__((unused,
                    noinline)) two(void);
void __attribute__((unused, noinline)) two(void) {}
int main(void) { two(); return 0; }
EOF
if "$compiler" -w -o "$tmp/attrmultiline" "$tmp/attrmultiline.c" >/dev/null 2>&1 && "$tmp/attrmultiline"; then
    echo "testing an attribute list over several lines ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing an attribute list over several lines ... FAILED"
    "$compiler" -w -o "$tmp/attrmultiline" "$tmp/attrmultiline.c" 2>&1 | head -3 | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# The list still has to be terminated: a missing `)` is reported, at EOF.
cat > "$tmp/attrbad.c" <<'EOF'
void __attribute__((unused g(void);
EOF
"$compiler" -w -c -o /dev/null "$tmp/attrbad.c" > "$tmp/log" 2>&1
if grep -q "expected" "$tmp/log"; then
    echo "testing a missing attribute close paren ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a missing attribute close paren ... FAILED"
    head -2 "$tmp/log" | sed 's/^/    /'
    n_fail=$((n_fail + 1))
fi

# --- variable length arrays ------------------------------------------
# The bound of a variable length array is evaluated once, where the
# declaration is reached: the size of the object and the counter every
# later sizeof reads are that one value. cxx evaluated it twice -- once
# into the counter, once more as the alloca's size -- which ran its side
# effects twice (`char a[n++]` left n at n + 2) and, for a bound the irgen
# branches on, asked for blocks the parse-time count had not reserved
# (assert(blk_used < curf->num_blk), cpython's Modules/socketmodule.c).
cat > "$tmp/vlaonce.c" <<'EOF'
int main(void) {
    int n = 3;
    char a[n++];
    a[0] = 1;
    if (n != 4 || sizeof(a) != 3 || a[0] != 1) return 1;

    int k = 0;
    int c[3][k++ + 2];
    if (k != 1 || sizeof(c) != 24 || sizeof(c[0]) != 8) return 2;

    int m = 2;
    char b[m > 4 ? m : 4];
    if (sizeof(b) != 4) return 3;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/vlaonce" "$tmp/vlaonce.c" > "$tmp/log" 2>&1 && "$tmp/vlaonce"; then
    echo "testing a variable length bound is evaluated once ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a variable length bound is evaluated once ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# A variably modified typedef evaluates its bound where the typedef is
# declared, and every later use of the type reads the size it captured:
# that is what both references do, and what makes `sizeof(T)` a value
# rather than an uninitialized counter.
cat > "$tmp/vlatd.c" <<'EOF'
int main(void) {
    int n = 5;
    typedef int T[n];
    n = 7;
    if (sizeof(T) != 20) return 1;
    T x;
    x[0] = 1;
    if (sizeof(x) != 20 || x[0] != 1) return 2;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/vlatd" "$tmp/vlatd.c" > "$tmp/log" 2>&1 && "$tmp/vlatd"; then
    echo "testing a variable length typedef captures its bound ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a variable length typedef captures its bound ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# A partly initialized record costs the stack one frame per element
# *written*, not one per element it has: the initializer's comma chain is
# walked from its left end, and a `char path[PATH_MAX + 1]` member left
# zero -- Python/crossinterp.c's struct _unpickle_context -- put 4097
# frames there and overflowed the stack.
cat > "$tmp/biginit.c" <<'EOF'
struct big { int a; char path[4096 + 1]; int b; };
struct outer { int x; struct big inner; int y; };
struct outer g = { .x = 1, .inner = { .b = 7 }, .y = 2 };
int main(void) {
    struct outer l = { .x = 1, .inner = { .b = 7 }, .y = 2 };
    if (l.x != 1 || l.inner.b != 7 || l.y != 2) return 1;
    if (g.x != 1 || g.inner.b != 7 || g.y != 2) return 2;
    for (int i = 0; i < 4097; i++) if (l.inner.path[i] != 0) return 3;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/biginit" "$tmp/biginit.c" > "$tmp/log" 2>&1 && "$tmp/biginit"; then
    echo "testing a partly initialized large record ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a partly initialized large record ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# A variadic call spells its callee's type out, and the type has to be the
# one the declaration of the callee is printed with: a record parameter is
# one value per register piece there, not the record. LLVM rejected the
# disagreement ("argument is not of expected type"), which is how
# Python/codegen.c and Python/compile.c failed to build.
cat > "$tmp/vcall.c" <<'EOF'
struct loc { int a, b, c, d; };
struct P { int x; };
int err(struct P *p, struct loc l, const char *fmt, ...);
int use(struct P *p) {
    struct loc l = { 1, 2, 3, 4 };
    return err(p, l, "x %d", 1);
}
EOF
if "$compiler" -w -c -o /dev/null "$tmp/vcall.c" > "$tmp/log" 2>&1; then
    echo "testing a record parameter before a variadic call ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a record parameter before a variadic call ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- noreturn --------------------------------------------------------
# 6.7.13.3p2 allows the standard attribute only on the declaration of a
# function, and clang reports a breach as an error. The GNU spelling is
# ignored with a warning by both references, and both accept it on a
# function pointer -- git's usage.c declares several that way.
bad "standard noreturn on an object" <<'EOF'
[[noreturn]] int v;
EOF

ok "GNU noreturn on an object is ignored" <<'EOF'
int v __attribute__((noreturn));
int main(void) { return 0; }
EOF

ok "noreturn on a function pointer object" <<'EOF'
typedef void (*report_fn)(const char *);
static report_fn usage_routine __attribute__((noreturn));
int main(void) { return usage_routine == 0 ? 0 : 1; }
EOF

# --- elided braces through an array of arrays -------------------------
# 6.7.9p20 lets the braces around an inner aggregate be left out, so a struct
# member that is an array of arrays takes the rest of the list:
#
#     struct T { char c[2][4]; } t = {"abc", "def"};   /* c[1] too */
#
# The member's initializer arrives with no brace of its own and takes the
# string path, which read one string and returned; the comma and "def" were
# dropped at the struct level and c[1] stayed zero. The file-scope form goes
# through the list path, which is why it always worked. A one-dimensional
# member is left alone: there the comma belongs to the member after it.
cat > "$tmp/elide.c" <<'EOF'
#include <stdio.h>
struct T { char c[2][4]; } t = {"abc", "def"};
struct U { char c[2][4]; };
struct U u = {{"abc", "def"}};
struct P { char a[4]; char b[4]; } p = {"ab", "cd"};
struct Q { char a[2][4]; char b[4]; } q = {"ab", "cd", "ef"};
int main(void) {
    printf("[%s][%s] [%s][%s] [%s][%s] [%s][%s][%s]\n", t.c[0], t.c[1], u.c[0], u.c[1], p.a, p.b,
           q.a[0], q.a[1], q.b);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/elide" "$tmp/elide.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/elide")" = "[abc][def] [abc][def] [ab][cd] [ab][cd][ef]" ]; then
    echo "testing elided braces through an array of arrays ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing elided braces through an array of arrays ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- an array of pointers is not a character array --------------------
# `struct tag { char *s[2]; } s = {"abc", "def"};` initializes s[0] and s[1],
# each a `char *`, from the string literals -- ordinary pointer initializers.
# cxx saw a string in front of an array, took it for a character-array
# initializer, found `char *` where it wanted `char`, and refused
# C/0030/0067 with "array of inappropriate type initialized from string
# constant". The branch is now entered only when the string belongs to the
# array; `int a[3] = "abc"` still errors, as both references do.
cat > "$tmp/ptrarr.c" <<'EOF'
#include <stdio.h>
char *g[2] = {"abc", "def"};
struct tag { char *s[2]; } s = {"abc", "def"};
struct t2 { char c[2][4]; } t = {"abc", "def"};
int main(void) { printf("%s %s %s %s %s\n", g[0], g[1], s.s[0], s.s[1], t.c[0]); return 0; }
EOF
if "$compiler" -w -o "$tmp/ptrarr" "$tmp/ptrarr.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/ptrarr")" = "abc def abc def abc" ] &&
   ! "$compiler" -w -c -o "$tmp/ptrarr.o" - <<'EOF' > "$tmp/log2" 2>&1
int a[3] = "abc";
EOF
then
    echo "testing an array of pointers initialized from strings ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing an array of pointers initialized from strings ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a tag-only declaration inside a block ----------------------------
# 6.7.2.3p7: `struct S;` -- a tag and nothing else -- inside a block declares
# a *new* tag of that scope and hides any outer one; the type is incomplete
# until the block completes it, so a pointer declared in between completes to
# the inner type. C/0053/0440 declares `struct stag *p;` and only then
# `struct stag { char a; };`, and requires sizeof(*p) == 1 while sizeof(st),
# declared before the tag-only line, stays 4.
cat > "$tmp/taghide.c" <<'EOF'
#include <stdio.h>
struct stag { int a; };
struct stag *q;
int f(void) { return sizeof(struct stag) == 4 && sizeof(*q) == 4; }
int main(void) {
    struct stag st;
    struct stag;
    struct stag *p;
    struct stag { char a; };
    printf("%d %d %d %d\n", (int)sizeof(st), (int)sizeof(struct stag), (int)sizeof(*p), f());
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/taghide" "$tmp/taghide.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/taghide")" = "4 1 1 1" ]; then
    echo "testing a tag-only declaration inside a block ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a tag-only declaration inside a block ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- #line with macro arguments ---------------------------------------
# 6.10.4 macro-replaces the whole directive line before the line number is
# read, so `#define int1 200` with `#line int1` names line 200, and a macro
# file name works too. C/0048/0085 writes both; cxx asked for a pp-number and
# refused it. The expansion goes through expand_macro(), which is an
# accumulator -- it returns the tail of the chain it built, so the dummy head
# is read back as `dummy.next` (taking the return value for the head is right
# for a one-token line and wrong for `#line 300 "x.c"`).
cat > "$tmp/linemac.c" <<'EOF'
#include <stdio.h>
#define N 42
#define F "from_macro.c"
#line N
int a = __LINE__;
#line N F
int b = __LINE__;
const char *f = __FILE__;
#line 300 "renamed.c"
int c = __LINE__;
int main(void) { printf("%d %d %s %d\n", a, b, f, c); return 0; }
EOF
if "$compiler" -w -o "$tmp/linemac" "$tmp/linemac.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/linemac")" = "42 42 from_macro.c 300" ]; then
    echo "testing #line with macro arguments ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing #line with macro arguments ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a block-scope extern and the file-scope definition ----------------
# `extern int a;` inside a function declares the file-scope object, and the
# `int a;` below defines it. Two symbols carry the name, the declaration comes
# first in the module's list, and the emitter's "already emitted" set recorded
# its name -- so the definition was dropped, the unit defined nothing, and the
# link failed with `undefined reference to 'a'` (32 tests in the full run).
# LLVM allows a global to be declared or defined in a module, not both, so the
# second fix was to collect the defined names first and print no declaration of
# one. Same for functions: a prototype before the body.
cat > "$tmp/tentative.c" <<'EOF'
#include <stdio.h>
int f(void) { extern int a; a = 10; return a; }
int a;
int g(void);
int g(void) { return a; }
int main(void) { printf("%d %d\n", f(), g()); return 0; }
EOF
if "$compiler" -w -o "$tmp/tentative" "$tmp/tentative.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/tentative")" = "10 10" ]; then
    echo "testing a block-scope extern and the file-scope definition ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a block-scope extern and the file-scope definition ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a long double compared against a constant -------------------------
# An integer immediate used as a floating constant was printed as a double bit
# pattern whatever type it was used with: `fcmp oeq x86_fp80 %x,
# 0x0000000000000000` is "floating point constant does not have type
# 'x86_fp80'", and LLVM refuses the module. That was the largest real defect
# class in the full run (61 tests). Both printing paths now spell the literal
# for the type the instruction uses, and an immediate in a wider instruction
# gets that instruction's type.
cat > "$tmp/ldcmp.c" <<'EOF'
#include <stdio.h>
int main(void) {
    long double a = 0.0L, b = 1.5L, c = 128.0L, d = -0.0L;
    double x = 0.0;
    printf("%d %d %d %d %d %d\n", a == 0, a == 0.0L, b == 1.5, c == 128, b == 2.5, x == 0);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/ldcmp" "$tmp/ldcmp.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/ldcmp")" = "1 1 1 1 0 1" ]; then
    echo "testing a long double compared against a constant ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a long double compared against a constant ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- repeating an identical definition (C23 onward) --------------------
# C23 allows the same struct, union or enum to be defined again with an
# identical definition; only a conflicting one is an error. In C17 both
# references reject even the identical one, and cxx follows its target
# standard (N3685) in every -std=, as it does for `()` and implicit
# declarations. The comparison that spots a conflict must not assume a member
# has a name -- an unnamed bit-field does not, and reading name->id there was
# a segmentation fault.
cat > "$tmp/redef.c" <<'EOF'
#include <stdio.h>
struct Same { unsigned char :0; unsigned char :0; unsigned char m3; };
struct Same { unsigned char :0; unsigned char :0; unsigned char m3; };
union USame { unsigned char :0; int m; };
union USame { unsigned char :0; int m; };
enum ESame { E1, E2 };
enum ESame { E1, E2 };
struct Same v = { 1 };
union USame u = { 5 };
int main(void) { printf("%d %d %d\n", v.m3, u.m, (int)E2); return 0; }
EOF
cat > "$tmp/redefbad.c" <<'EOF'
struct Diff { int x; };
struct Diff { long x; };
EOF
if "$compiler" -w -o "$tmp/redef" "$tmp/redef.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/redef")" = "1 5 1" ] &&
   ! "$compiler" -w -c -o "$tmp/redefbad.o" "$tmp/redefbad.c" > "$tmp/log2" 2>&1 &&
   grep -q 'redefinition' "$tmp/log2"; then
    echo "testing an identical repeated definition ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing an identical repeated definition ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a bit-field straddling an element boundary -----------------------
# `m3:3` starting at bit 6 reaches bit 8, so the byte holding its last bit is
# not covered by the element the cursor had already passed. The element a
# member owns is the bytes its *bits* occupy -- not its access unit, which for
# `unsigned long m2:29` after `unsigned char m1:5` is eight bytes while the
# bits take five. Getting that wrong either loses the field's last bits or
# swallows the member that follows. With it right, C/0013's 676 tests all
# match the reference. Each shape is checked in a translation unit of its own:
# put together, clang and gcc disagree with each other about the result.
cat > "$tmp/straddle.c" <<'EOF'
#include <stdio.h>
struct D { unsigned long m1:3; unsigned long :3; unsigned long m3:3; };
struct D d[2] = {{ 1, 2 },{ 3, 4 }};
int main(void) {
    printf("%lu %lu %lu %lu\n", d[0].m1, d[0].m3, d[1].m1, d[1].m3);
    return 0;
}
EOF
cat > "$tmp/straddle2.c" <<'EOF'
#include <stdio.h>
struct S { unsigned char m1:5; unsigned long m2:29; unsigned char m3; };
struct S s = { 1, 1000, 7 };
int main(void) {
    struct S loc = { 2, 2000, 9 };
    printf("%lu %lu %lu | %lu %lu %lu\n", (unsigned long)s.m1, (unsigned long)s.m2, (unsigned long)s.m3,
           (unsigned long)loc.m1, (unsigned long)loc.m2, (unsigned long)loc.m3);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/straddle" "$tmp/straddle.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/straddle")" = "1 2 3 4" ] &&
   "$compiler" -w -o "$tmp/straddle2" "$tmp/straddle2.c" >> "$tmp/log" 2>&1 &&
   [ "$("$tmp/straddle2")" = "1 1000 7 | 2 2000 9" ]; then
    echo "testing a bit-field straddling an element boundary ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a bit-field straddling an element boundary ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a struct around a punned union -----------------------------------
# The value of a member that is written punned makes every containing type
# inline as well, and the *type* printer has to agree with the value printer.
# print_init_ty()'s union branch kept the older test, so for
#
#     struct HOLD { int tag; union OUT o; };
#     struct HOLD h = { 7, { { 1 } } };
#
# the type came out `{ i32, [4 x i8], %union.OUT }` while the value was
# `{ { i8, [7 x i8] } } { ... }`: "element 2 of struct initializer doesn't match
# struct element type". With both printers agreeing, C/0013 reaches 671 of 676
# with no compile failure left.
cat > "$tmp/holdpunned.c" <<'EOF'
#include <stdio.h>
union SUB { unsigned char m1; unsigned long m2; };
union MID { unsigned char c1; union SUB s; };
union OUT { unsigned long :0; union SUB m2; };
struct HOLD { int tag; union OUT o; };
struct DEEP { union OUT a; int b; union MID c; };
union OUT  x = { { 1 } };
union MID  y = { { 1 } };
struct HOLD h = { 7, { { 1 } } };
struct DEEP d = { { { 1 } }, 5, { { 1 } } };
int main(void) {
    union OUT loc = { { 2 } };
    struct HOLD hl = { 3, { { 4 } } };
    printf("%d %d | %d | %d %d | %d %d | %d | %d %d\n",
           x.m2.m1, (int)x.m2.m2, y.s.m1, h.tag, h.o.m2.m1, d.a.m2.m1, d.b, d.c.s.m1,
           loc.m2.m1, hl.tag, hl.o.m2.m1);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/holdpunned" "$tmp/holdpunned.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/holdpunned")" = "1 1 | 1 | 7 1 | 1 5 | 1 | 2 3" ]; then
    echo "testing a struct around a punned union ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a struct around a punned union ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a union punned one level down ------------------------------------
# dump_init()'s union branch decided the *type* spelling with `mem != canon`,
# while an aggregate member's *value* is written by dump_init(child, ...),
# which makes the same decision one level down. For
#
#     union SUB { unsigned char m1; unsigned long m2; };   /* canon: m2 */
#     union OUT { unsigned long :0; union SUB m2; };
#     union OUT x = { { 1 } };                             /* names SUB's m1 */
#
# SUB's image is the punned form, so OUT's element type has to be spelled that
# way too; OUT's prefix stayed `%union.OUT` and LLVM answered "element 0 of
# struct initializer doesn't match struct element type". One predicate now
# decides it for all three printers. C/0013 went from 663 to 667 passing and
# its compile failures from 8 to 4.
cat > "$tmp/punned.c" <<'EOF'
#include <stdio.h>
union SUB { unsigned char m1; unsigned long m2; };
union MID { unsigned char c1; union SUB s; };
union OUT { unsigned long :0; union SUB m2; };
union OUT x = { { 1 } };
union MID y = { { 1 } };
int main(void) {
    union OUT loc = { { 2 } };
    printf("%d %d | %d %d | %d\n", x.m2.m1, (int)x.m2.m2, y.s.m1, (int)y.s.m2, loc.m2.m1);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/punned" "$tmp/punned.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/punned")" = "1 1 | 1 1 | 2" ]; then
    echo "testing a union punned one level down ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a union punned one level down ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- unnamed members in front of a braced initializer ------------------
# `{ 1 }` does not go through struct_initializer2(): initializer2() sends a
# braced initializer to struct_initializer1(), which had the same one-step skip
# that R83b fixed on the unbraced path only. With two unnamed members in front,
# the value went into the second of them and the member after it kept zero:
#
#     struct { unsigned char :0; unsigned char :0; unsigned char m3; } x = { 1 };
#
# A union's initializer names its first *named* member (6.7.9p9), so it skips
# unnamed ones too. C/0013 went from 501 to 663 passing on this.
cat > "$tmp/skipunnamed.c" <<'EOF'
#include <stdio.h>
struct L0 { unsigned char :0; unsigned char m3; } l0 = { 1 };
struct L2 { unsigned char :0; unsigned char :0; unsigned char m3; } l2 = { 1 };
struct L3 { unsigned char :0; unsigned char :3; unsigned char m3; } l3 = { 1 };
struct L4 { unsigned char :3; unsigned char :0; unsigned char m3; } l4 = { 1 };
struct L5 { unsigned char :0; unsigned char :0; unsigned char :0; unsigned char m3; } l5 = { 1 };
struct M0 { unsigned char m1; unsigned char :0; unsigned char :0; unsigned char m3; } m0 = { 1, 2 };
union  U0 { unsigned char :0; int m; } u0 = { 7 };
union  U1 { unsigned long :0; unsigned long m:3; } u1 = { 5 };
int main(void) {
    struct L2 loc = { 9 };
    printf("%d %d %d %d %d | %d %d | %d %d | %d\n",
           l0.m3, l2.m3, l3.m3, l4.m3, l5.m3, m0.m1, m0.m3, u0.m, (int)u1.m, loc.m3);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/skipunnamed" "$tmp/skipunnamed.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/skipunnamed")" = "1 1 1 1 1 | 1 2 | 7 5 | 9" ]; then
    echo "testing unnamed members before a braced initializer ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing unnamed members before a braced initializer ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a bit-field unit that lands off its own alignment ----------------
# cxx's elements are the access units, laid where the bits are; LLVM puts an
# element of a `{ ... }` type where its alignment asks. For
#
#     struct E { unsigned char m1; unsigned long m2:29; unsigned char m3; };
#
# the 29-bit field's unit is a four-byte element at byte 1, where LLVM would
# place an i32 at byte 4 -- so the initializer wrote one place and the load
# read another, and `e.m2` came out 2 << 24. Such a record now sets
# layout_packed, which spells the type `<{ ... }>` (R64's mechanism) and puts
# every element where it is written. C/0013 went from 425 to 501 passing.
cat > "$tmp/unitfx.c" <<'EOF'
#include <stdio.h>
struct E { unsigned char m1; unsigned long m2:29; unsigned char m3; } e = { 1, 2, 3 };
struct W { unsigned char m1; unsigned long   :29; unsigned char m3; } w = { 1, 2 };
struct L { unsigned char m1; unsigned short m2:9;  unsigned char m3; } l = { 1, 300, 3 };
int main(void) {
    struct E le = { 4, 5, 6 };
    printf("%d %d %d | %d %d | %d %d %d | %d %d %d | %zu %zu\n",
           e.m1, (int)e.m2, e.m3, w.m1, w.m3, l.m1, (int)l.m2, l.m3,
           le.m1, (int)le.m2, le.m3, sizeof(struct E), sizeof(struct L));
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/unitfx" "$tmp/unitfx.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/unitfx")" = "1 2 3 | 1 2 | 1 300 3 | 4 5 6 | 8 6" ]; then
    echo "testing a bit-field unit off its own alignment ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a bit-field unit off its own alignment ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a union's bit-field member ---------------------------------------
# Every member of a union starts at bit zero, and layout_struct() says so with
# an early `continue` -- which used to skip the only place that sets
# mem->unit_ty. A bit-field member of a union then reached the loader, the
# storer and the image printer with a null access unit and the compiler died
# (`union { int m:3; } u = { 1 };`). This was the whole of C/0013's compile
# failures: 32 of them, down to 2.
cat > "$tmp/unionbf.c" <<'EOF'
#include <stdio.h>
union U1 { unsigned long long m:3; } u1 = { 5 };
union U2 { int m:3; } u2 = { 3 };
union U3 { unsigned char m:3; unsigned char c; } u3 = { .c = 0xAB };
union U4 { int m:3; int n:5; } u4 = { .n = 17 };
union U5 { unsigned short m:9; unsigned char c; } u5 = { .c = 0x7F };
int main(void) {
    union U2 local = { -2 };
    printf("%d %d %d %d %d %d | %zu %zu %zu\n", (int)u1.m, u2.m, u3.c, u4.n, u5.c, local.m,
           sizeof(union U1), sizeof(union U2), sizeof(union U5));
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/unionbf" "$tmp/unionbf.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/unionbf")" = "5 3 171 -15 127 -2 | 8 4 2" ]; then
    echo "testing a union's bit-field member ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a union's bit-field member ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a zero-width bit-field owns no element ----------------------------
# The layout gives `unsigned char :0;` the offset of the member that follows,
# and that member is the one the element belongs to. The emitter treated the
# field as an element of its own, moved its cursor past the byte, and then
# skipped the real member (`off < pos`) -- so the value and the byte were both
# lost, and the emitted type grew an element the object does not have. The
# suite's C/0013 is 676 files of this shape; 60 of its 303 output differences
# went away with this.
cat > "$tmp/zerowidth.c" <<'EOF'
#include <stdio.h>
struct A { unsigned char m1; unsigned char   :0; unsigned char m3; } a = { 1, 2 };
struct B { unsigned char m1; unsigned char   :3; unsigned char m3; } b = { 1, 2 };
struct D { unsigned char m1; unsigned long long:0; unsigned char m3; } d = { 1, 2 };
struct Z { unsigned char m1; unsigned char   :0; };
struct Z z = { 7 };
int main(void) {
    printf("%d %d | %d %d | %d %d | %zu %d\n",
           a.m1, a.m3, b.m1, b.m3, d.m1, d.m3, sizeof(struct Z), z.m1);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/zerowidth" "$tmp/zerowidth.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/zerowidth")" = "1 2 | 1 2 | 1 2 | 1 7" ]; then
    echo "testing a zero-width bit-field owns no element ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a zero-width bit-field owns no element ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- the input half of a '+' asm operand ------------------------------
# GCC's `+` operand is one operand; LLVM's constraint string needs two. Where
# the constraint names nothing but memory the input half names the same memory
# and travels as the same address ("*m") -- clang emits "=*m,*m" for
# `"+m"(x)`. For a constraint that may live in a register, clang emits a
# matching number and passes the object's *value*: `"+g"(h)` comes out as
# "=*imr,0" with an i32 operand. cxx repeated the converted letters either way,
# so the input was marked indirect and handed the address; LLVM then refused
# the elementtype attribute, which belongs to an indirect constraint only.
# That is ffmpeg's libavcodec/x86/rnd_template.c.
cat > "$tmp/pluscons.c" <<'EOF'
#include <stdio.h>
#include <stdint.h>
static int run(int h, const unsigned char *pixels, unsigned char *block, long line_size) {
    int i = h;
    __asm__ volatile("movq   (%1), %%mm0 \n\t"
                     "add    %3, %1      \n\t"
                     "subl   $2, %0      \n\t"
                     "jnz    1f          \n\t"
                     "1:                 \n\t"
                     : "+g"(i), "+S"(pixels)
                     : "D"(block), "r"(line_size)
                     : "rax", "memory");
    return i;
}
static int mem(int *p) {
    int v = 7;
    __asm__ volatile("addl $1, %0" : "+m"(v));   /* the same-address form */
    __asm__ volatile("addl $2, %0" : "+r"(v));   /* the matching-number form */
    return v + *p;
}
static int explicit_match(int v) {
    int r = 0;
    __asm__ volatile("movl %1, %0" : "=r"(r) : "0"(v));
    return r;
}
int main(void) {
    unsigned char b[8] = {0}, p[8] = {0};
    int k = 5;
    printf("%d %d %d\n", run(3, p, b, 8), mem(&k), explicit_match(41));
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/pluscons" "$tmp/pluscons.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/pluscons")" = "1 15 41" ]; then
    echo "testing the input half of a '+' asm operand ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the input half of a '+' asm operand ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- anonymous records get distinct names -----------------------------
# insert_ty() numbered a type by counting the entries already in `types` with
# a matching id. Anonymous records arrive two ways -- a tagless `struct { ... }`
# is given intern("anon") for an id, while one the compiler builds itself has
# no id at all -- and the two were counted apart, each starting at anon.1. Two
# unrelated structs then came out as %struct.anon.1 and LLVM refused the module
# as a redefinition (libavcodec/jpegxl_parser.c). One sequence for both.
cat > "$tmp/anon.c" <<'EOF'
#include <stdio.h>
struct A { struct { int x, y; } p; };
struct B { struct { double d; } q; };
union  U { struct { char c; } s; int i; };
struct C { struct { struct { int deep; } in; } out; };
int main(void) {
    struct A a = {{1, 2}};
    struct B b = {{3.5}};
    union U u = {{7}};
    struct C c = {{{9}}};
    printf("%d %d %.1f %d %d\n", a.p.x, a.p.y, b.q.d, u.s.c, c.out.in.deep);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/anon" "$tmp/anon.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/anon")" = "1 2 3.5 7 9" ]; then
    echo "testing anonymous records get distinct names ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing anonymous records get distinct names ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a one-byte struct array is not a byte string ---------------------
# `c"..."` is an [N x i8] constant, so the short form is only right when the
# element is an integer type. libswscale's FormatEntry is a struct one byte
# wide, and cxx wrote its 227-element table as a string -- LLVM refused the
# module twice ("'[227 x i8]' but expected '[227 x %struct.FormatEntry]'" and
# "redefinition of type"). A byte array keeps the short form.
cat > "$tmp/bytestr.c" <<'EOF'
#include <stdio.h>
struct One { char c; };
struct One tab[4] = {{1}, {2}, {3}, {4}};
char bytes[4] = {5, 6, 7, 8};
union U { char c; };
union U uni[2] = {{9}, {10}};
int main(void) {
    printf("%d %d %d %d | %d %d %d %d | %d %d\n", tab[0].c, tab[1].c, tab[2].c, tab[3].c,
           bytes[0], bytes[1], bytes[2], bytes[3], uni[0].c, uni[1].c);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/bytestr" "$tmp/bytestr.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/bytestr")" = "1 2 3 4 | 5 6 7 8 | 9 10" ]; then
    echo "testing a one-byte struct array is not a byte string ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a one-byte struct array is not a byte string ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- passing an aggregate by value ------------------------------------
# The value has to be given a home before it can travel, and that copy was
# written with IR_STORE -- which put the address where the bytes belong, since
# in this IR an aggregate value *is* its address. LLVM refused the module:
# "'%tmp104' defined with type 'ptr' but expected '%struct.SchedulerNode'",
# which is ffmpeg's fftools/ffmpeg_sched.c. The copies elsewhere (the va_arg
# paths) are IR_MEMCPY, and this one is now too.
cat > "$tmp/aggarg.c" <<'EOF'
#include <stdio.h>
struct S { int a, b, c; };
static int sum(struct S s) { return s.a + s.b + s.c; }
struct Holder { struct S node; };
struct Holder h = {{1, 2, 3}};
static struct S get(void) { struct S s = {4, 5, 6}; return s; }
static int pass(struct S a, struct S b) { return sum(a) * 10 + sum(b); }
int main(void) {
    struct S v = {10, 20, 30};
    printf("%d %d %d %d\n", sum(h.node), sum(get()), sum((struct S){7, 8, 9}), pass(v, h.node));
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/aggarg" "$tmp/aggarg.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/aggarg")" = "6 15 24 606" ]; then
    echo "testing a by-value aggregate argument ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a by-value aggregate argument ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- an array as an asm memory operand, and the local's slot -----------
# Two bugs that met in one program. `"+m"(state)` is an output *and* an
# indirect operand: the input half of the ND_ASM loop already kept the array
# itself rather than the pointer it decayed to, the output half did not, so the
# lvalue check saw the decayed node and refused it -- ffmpeg's
# libavutil/utils.c:105 (`uint16_t state[14]` with `"fstenv %0"`).
#
# And a local object's storage was allocated with the *declared* alignment:
# 8 for a `double[2]`, where an array of 16 bytes or more has to sit 16-aligned
# on x86-64 (object_align, R63) or a `movaps` on it faults. Globals and
# __builtin_alloca were fixed then; the fn->locals alloca was missed.
cat > "$tmp/asmarr.c" <<'EOF'
#include <stdio.h>
#include <stdint.h>
int main(void) {
    uint16_t state[14] = {0};
    double a[2] = { 1.0, 2.0 }, r[2];
    __asm__ volatile("fstenv %0 \n\t" : "+m"(state) : : "memory");
    __asm__("movaps %1, %%xmm0\n\tmovaps %%xmm0, %0" : "=m"(r) : "m"(a));
    printf("%zu %d %g %g\n", sizeof(state), (int)state[0], r[0], r[1]);
    (void)state[0];
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/asmarr" "$tmp/asmarr.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/asmarr" 2>/dev/null | sed 's/^[0-9]* /s /')" = "s 28 895 1 2" ]; then
    echo "testing an array as an asm memory operand ... passed"
    n_pass=$((n_pass + 1))
elif [ -x "$tmp/asmarr" ]; then
    # fstenv's state[0] is machine-specific; the point is that it runs.
    got=$("$tmp/asmarr" 2>/dev/null)
    case "$got" in
        "28 "*" 1 2") echo "testing an array as an asm memory operand ... passed"; n_pass=$((n_pass + 1)) ;;
        *) echo "testing an array as an asm memory operand ... FAILED"; echo "    got: $got"; n_fail=$((n_fail + 1)) ;;
    esac
else
    echo "testing an array as an asm memory operand ... FAILED (compile)"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- an attribute between declarators ---------------------------------
# GCC allows an attribute at the *beginning* of a declarator as well as after
# it, and FFmpeg leans on that: `int i, ret, av_unused(version), nb_curves;`
# with av_unused expanding to __attribute__((unused)) is vf_curves.c:591, and
# ripemd.c:111 has the same shape. cxx read attributes before and after a
# declarator but not at the start of one, so it stopped at "expected identifier
# or '('". Both declaration paths -- the block one and the file-scope one --
# take the list now and attach it to the declarator that follows.
cat > "$tmp/attrdecl.c" <<'EOF'
#include <stdio.h>
#define av_unused __attribute__((unused))
int g1, g2, av_unused(g3), g4;
int main(void) {
    int i, ret, av_unused(version), nb_curves;
    unsigned a, b, av_unused t;
    i = 2; ret = 3; version = 1; nb_curves = 4;
    a = 5; b = 6; t = 7;
    g1 = 8; g2 = 9; g3 = 10; g4 = 11;
    printf("%d %d %d %d %u %u %u %d %d %d %d\n", i, ret, version, nb_curves, a, b, t, g1, g2, g3, g4);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/attrdecl" "$tmp/attrdecl.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/attrdecl")" = "2 3 1 4 5 6 7 8 9 10 11" ]; then
    echo "testing an attribute at the start of a declarator ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing an attribute at the start of a declarator ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- an argument-count diagnostic on an anonymous callee --------------
# `int (*mpfp)(); mpfp(0);` calls through a pointer, and the function type it
# points to has no name: all three argument-count diagnostics read
# ty->name->len, so the compiler died with signal 11 -- the Fujitsu suite's
# C/0048_0001, whose parser got there through `int mpfff(), (*mpfp)(), ii;`.
# clang words this case without a name and cxx follows it. Both directions are
# checked: the too-many one was reachable, the too-few one was not.
cat > "$tmp/anonymous.c" <<'EOF'
int (*mpfp)();
int (*mpfp2)(int, int);
int main(void) { if (0) return (*mpfp)(0); return (*mpfp2)(); }
EOF
if "$compiler" -w -c -o "$tmp/anonymous.o" "$tmp/anonymous.c" > "$tmp/log" 2>&1; then
    echo "testing a bad call through a pointer ... FAILED (accepted)"
    n_fail=$((n_fail + 1))
elif grep -q 'internal compiler error' "$tmp/log"; then
    echo "testing a bad call through a pointer ... FAILED (compiler died)"
    n_fail=$((n_fail + 1))
else
    n_ok=$(grep -c 'too many arguments\|too few arguments' "$tmp/log" || true)
    if [ "$n_ok" -ge 1 ]; then
        echo "testing a bad call through a pointer ... passed"
        n_pass=$((n_pass + 1))
    else
        echo "testing a bad call through a pointer ... FAILED (no argument-count diagnostic)"
        sed 's/^/    /' "$tmp/log" | head -3
        n_fail=$((n_fail + 1))
    fi
fi

# --- a null pointer constant written as an expression -----------------
# 6.3.2.3p3: an integer constant expression with the value 0 is one, and
# is_null_constant() only knows a literal. gcc accepts these silently, clang
# with a warning; cxx refused them until the Fujitsu suite's C/0150_0002
# (`fp0(1-1)` with `int fp0(int *)`) turned up. A real mismatch stays an error.
cat > "$tmp/nullconst.c" <<'EOF'
#include <stdio.h>
int fp0(int *p) { return p == 0; }
int main(void) {
    int *a = 1 - 1;
    int *b = 0 + 0;
    int *c = 2 - 2;
    printf("%d %d %d %d\n", fp0(1 - 1), a == 0, b == 0, c == 0);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/nullconst" "$tmp/nullconst.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/nullconst")" = "1 1 1 1" ]; then
    echo "testing a null pointer constant written as an expression ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a null pointer constant written as an expression ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

cat > "$tmp/nullconst2.c" <<'EOF'
int main(void) { int a = 1; int *p = a; return *p; }
EOF
if "$compiler" -w -c -o "$tmp/nullconst2.o" "$tmp/nullconst2.c" > "$tmp/log" 2>&1; then
    echo "testing a non-constant integer to pointer stays an error ... FAILED (accepted)"
    n_fail=$((n_fail + 1))
else
    echo "testing a non-constant integer to pointer stays an error ... passed"
    n_pass=$((n_pass + 1))
fi

# --- __label__, GNU's block-local labels ------------------------------
# `__label__ a, b;` declares names whose scope is the block, so that two
# blocks -- or two expansions of a macro carrying one inside a statement
# expression, which is what the Fujitsu suite's C/0059 does -- may each define
# the same name. cxx resolves labels through one function-wide list of ids and
# checks it for duplicates, so a declared name is given an id of its own,
# mangled per declaration; the label statement, the goto and the
# labels-as-values form all ask for it the same way.
cat > "$tmp/labeldecl.c" <<'EOF'
#include <stdio.h>
#define T(idx) ({          \
    __label__ a, done;     \
    int v = 0;             \
    if ((idx) == 1) goto a;\
    v = 9;                 \
    goto done;             \
  a: v = 1;                \
  done: v;                 \
})
void t1(void) {
    {
        __label__ L;
        goto L;
        printf("*NG*\n");
    L:
        printf("*OK*\n");
    }
}
int main(void) { t1(); printf("%d %d\n", T(1), T(0)); return 0; }
EOF
if "$compiler" -w -o "$tmp/labeldecl" "$tmp/labeldecl.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/labeldecl" | tr '\n' ' ')" = "*OK* 1 9 " ]; then
    echo "testing __label__ in blocks and statement expressions ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing __label__ in blocks and statement expressions ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# The name is still one label inside its own block, and it is invisible
# outside it.
cat > "$tmp/labeldecl2.c" <<'EOF'
int main(void) { { __label__ L; L: ; L: ; } return 0; }
EOF
if "$compiler" -w -c -o "$tmp/labeldecl2.o" "$tmp/labeldecl2.c" > "$tmp/log" 2>&1; then
    echo "testing a repeated __label__ label in one block is an error ... FAILED (accepted)"
    n_fail=$((n_fail + 1))
else
    echo "testing a repeated __label__ label in one block is an error ... passed"
    n_pass=$((n_pass + 1))
fi

cat > "$tmp/labeldecl3.c" <<'EOF'
int main(void) { { __label__ L; L: ; } goto L; return 0; }
EOF
if "$compiler" -w -c -o "$tmp/labeldecl3.o" "$tmp/labeldecl3.c" > "$tmp/log" 2>&1; then
    echo "testing a jump to a __label__ from outside its block ... FAILED (accepted)"
    n_fail=$((n_fail + 1))
else
    echo "testing a jump to a __label__ from outside its block ... passed"
    n_pass=$((n_pass + 1))
fi

# --- __attribute__((alias("target"))) ---------------------------------
# The declared name is another name for an object defined in this unit. cxx
# accepted the attribute and dropped it, so the call went to a symbol nothing
# defined ("undefined reference to foo_impl", the Fujitsu suite's C/0108).
# The attribute sits after the declarator, which puts it on the type rather
# than in the declaration specifiers' list -- both are consulted.
cat > "$tmp/alias.c" <<'EOF'
#include <stdio.h>
void foo(void) { printf("OK\n"); }
static void foo_impl(void) __attribute__((alias("foo")));
int bar(void) { return 7; }
extern int bar_alias(void) __attribute__((alias("bar")));
int main(void) { foo_impl(); foo(); foo_impl(); printf("%d\n", bar_alias()); return 0; }
EOF
if "$compiler" -w -o "$tmp/alias" "$tmp/alias.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/alias" | tr '\n' ' ')" = "OK OK OK 7 " ]; then
    echo "testing __attribute__((alias)) ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing __attribute__((alias)) ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- extra arguments on __sync_lock_* ---------------------------------
# gcc and clang both tolerate them on these two builtins (the Fujitsu suite
# passes one to each); both refuse them on the fetch family, and so does cxx.
cat > "$tmp/syncextra.c" <<'EOF'
#include <stdio.h>
int main(void) {
    int a = 1, b = 2, d = 10;
    int r = __sync_lock_test_and_set(&a, b, &d);
    printf("%d %d\n", r, a);
    __sync_lock_release(&a, &d);
    printf("%d\n", a);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/syncextra" "$tmp/syncextra.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/syncextra" | tr '\n' ' ')" = "1 2 0 " ]; then
    echo "testing extra arguments on the __sync_lock builtins ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing extra arguments on the __sync_lock builtins ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

cat > "$tmp/syncnoextra.c" <<'EOF'
int main(void) { int a = 1, d = 9; return __sync_fetch_and_add(&a, 3, &d); }
EOF
if "$compiler" -w -c -o "$tmp/syncnoextra.o" "$tmp/syncnoextra.c" > "$tmp/log" 2>&1; then
    echo "testing the fetch family still refuses extra arguments ... FAILED (accepted)"
    n_fail=$((n_fail + 1))
else
    echo "testing the fetch family still refuses extra arguments ... passed"
    n_pass=$((n_pass + 1))
fi

# --- __alignof, GNU's older spelling ----------------------------------
cat > "$tmp/alignof.c" <<'EOF'
#include <stdio.h>
struct S { char c; double d; };
int main(void) {
    printf("%zu %zu %zu\n", __alignof(int), __alignof(struct S), __alignof__(double));
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/alignof" "$tmp/alignof.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/alignof")" = "4 8 8" ]; then
    echo "testing __alignof ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing __alignof ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- GCC's older __sync_* atomics -------------------------------------
# The Fujitsu suite's C/0044 uses __sync_lock_test_and_set and
# __sync_lock_release on every integer width; cxx had neither (only
# __sync_synchronize), so it stopped at "implicit declaration of function".
# They are the __atomic_* operations with the memory order in the name: the
# lock_test_and_set acquires, the lock_release releases, the fetch_and_*
# family is a full barrier. clang has them all, gcc has them all.
cat > "$tmp/sync.c" <<'EOF'
#include <stdio.h>
int main(void) {
    char c = 1, r;
    int a = 1;
    r = __sync_lock_test_and_set(&c, 7);
    printf("%d %d\n", r, c);
    __sync_lock_release(&c);
    printf("%d\n", c);
    printf("%d\n", __sync_fetch_and_add(&a, 3));
    printf("%d\n", __sync_fetch_and_sub(&a, 1));
    printf("%d\n", __sync_fetch_and_or(&a, 8));
    printf("%d\n", __sync_fetch_and_and(&a, 12));
    printf("%d\n", __sync_fetch_and_xor(&a, 1));
    __sync_synchronize();
    printf("%d\n", a);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/sync" "$tmp/sync.c" > "$tmp/log" 2>&1; then
    got=$("$tmp/sync" | tr '\n' ' ')
    want="1 7 0 1 4 3 11 8 9 "
    if [ "$got" = "$want" ]; then
        echo "testing the __sync_* atomics ... passed"
        n_pass=$((n_pass + 1))
    else
        echo "testing the __sync_* atomics ... FAILED"
        echo "    got  $got"
        echo "    want $want"
        n_fail=$((n_fail + 1))
    fi
else
    echo "testing the __sync_* atomics ... FAILED (compile)"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- __fp16, the ACLE spelling of the half type ----------------------
# cxx has _Float16 as a type of its own and had no __fp16, so code that spells
# the ACLE name -- the Fujitsu suite's C/0194 passes `__fp16 *restrict` -- was
# refused. clang takes it on every target; gcc has only __bf16. cxx takes the
# name as another spelling of _Float16, which matches clang on representation,
# arithmetic and ABI. The two deliberate differences are recorded in the plan:
# clang's _Generic/__builtin_types_compatible_p tell the two types apart, and
# on targets without half-precision parameters clang lets __fp16 appear only
# behind a pointer.
cat > "$tmp/fp16.c" <<'EOF'
#include <stdio.h>
__fp16 g = 1.5;
void take(__fp16 *restrict p, int n) {
    for (int i = 0; i < n; i++) p[i] += 0.1f16;
}
int main(void) {
    __fp16 a = 1.5, b = 2.5;
    __fp16 c = a + b;
    take(&g, 1);
    printf("%zu %g %d\n", sizeof(__fp16), (double)c, (int)g);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/fp16" "$tmp/fp16.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/fp16")" = "2 4 1" ]; then
    echo "testing __fp16 as the half type ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing __fp16 as the half type ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- -Wpointer-sign covers the plain `char` variants ------------------
# `char` is a type of its own, distinct from both `signed char` and
# `unsigned char`. `sign_only_difference()` asked for a difference in
# signedness, and on x86-64 `char` and `signed char` agree on it, so passing
# `signed char *` where `char *` is wanted was refused outright instead of
# warned about. clang warns for both variants; gcc says nothing; cxx follows
# clang. A real mismatch (`double *` for `int *`) stays an error.
cat > "$tmp/psign.c" <<'EOF'
#include <string.h>
#include <stdio.h>
void sub(void) {
    char cbuf[4];
    signed char sbuf[4];
    unsigned char ubuf[4];
    strcpy(cbuf, "ab");
    strcpy(sbuf, "cd");
    strcpy(ubuf, "ef");
    printf("%s %s %s\n", cbuf, sbuf, ubuf);
}
int main(void) { sub(); return 0; }
EOF
if "$compiler" -w -o "$tmp/psign" "$tmp/psign.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/psign")" = "ab cd ef" ]; then
    echo "testing the char variants pass with a pointer-sign warning ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the char variants pass with a pointer-sign warning ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# The warning has to be reported without -w, and the mismatch must still be
# refused: the relaxation is about the char family only.
cat > "$tmp/psign2.c" <<'EOF'
#include <string.h>
void sub(void) { signed char buf[4]; strcpy(buf, "cd"); }
EOF
if "$compiler" -std=c23 -c -o "$tmp/psign2.o" "$tmp/psign2.c" 2> "$tmp/log"; then
    if grep -q 'differ in signedness' "$tmp/log"; then
        echo "testing the pointer-sign warning is reported ... passed"
        n_pass=$((n_pass + 1))
    else
        echo "testing the pointer-sign warning is reported ... FAILED (silent)"
        n_fail=$((n_fail + 1))
    fi
else
    echo "testing the pointer-sign warning is reported ... FAILED (refused)"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

cat > "$tmp/psign3.c" <<'EOF'
void f(int *p);
void g(void) { double *d = 0; f(d); }
EOF
if "$compiler" -std=c23 -c -o "$tmp/psign3.o" "$tmp/psign3.c" > "$tmp/log" 2>&1; then
    echo "testing a real pointer mismatch is still refused ... FAILED (accepted)"
    n_fail=$((n_fail + 1))
else
    echo "testing a real pointer mismatch is still refused ... passed"
    n_pass=$((n_pass + 1))
fi

# --- #pragma pack and packed members reach the IR --------------------
# The layout already honoured the pragma -- the sizes came out right -- but
# `is_packed` is set from __attribute__((packed)) only, so a record packed by
# the pragma was still written `{ i8, double }` and LLVM put the double at
# offset 8 instead of 1. A member-level packed had the same gap. Both now set
# the flag the printer reads, and a pragma that lowers nothing (pack(8) on
# these shapes) still spells the record the plain way.
cat > "$tmp/packpragma.c" <<'EOF'
#include <stdio.h>
#pragma pack(1)
struct P1 { char c; double i; };
#pragma pack()
#pragma pack(2)
struct P2 { char c; double i; };
#pragma pack()
struct MP { char c; int i __attribute__((packed)); };
#pragma pack(4)
struct D4P { double d; char c; };
#pragma pack()
struct P1 p1 = { 1, 2 };
struct P2 p2 = { 3, 4 };
struct MP mp = { 5, 6 };
struct D4P d4 = { 7, 8 };
int main(void) {
    printf("%d %g %d %g %d %d %g %d %zu %zu %zu\n", p1.c, p1.i, p2.c, p2.i, mp.c, mp.i,
           d4.d, d4.c, sizeof(struct P1), sizeof(struct P2), sizeof(struct D4P));
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/packpragma" "$tmp/packpragma.c" > "$tmp/log" 2>&1; then
    got=$("$tmp/packpragma")
    want="1 2 3 4 5 6 7 8 9 10 12"
    if [ "$got" = "$want" ]; then
        echo "testing #pragma pack and packed members ... passed"
        n_pass=$((n_pass + 1))
    else
        echo "testing #pragma pack and packed members ... FAILED"
        echo "    got  $got"
        echo "    want $want"
        n_fail=$((n_fail + 1))
    fi
else
    echo "testing #pragma pack and packed members ... FAILED (compile)"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a packed record in LLVM IR --------------------------------------
# The type body gives every member its C offset with explicit `[N x i8]`
# padding, which only reproduces the layout if LLVM adds none of its own: the
# record has to be spelled `<{ ... }>`. With plain braces a `double` after a
# `char` moves from offset 1 to 8, and an initializer written against the type
# fills the wrong bytes -- the Fujitsu suite's C/0091 read 0 instead of 2 and
# printed NG. Accesses were never affected: they go through byte
# getelementprs, which is why only the initializer showed it.
cat > "$tmp/packed.c" <<'EOF'
#include <stdio.h>
#include <stddef.h>
struct __attribute__((packed)) A { char c; double i; };
struct __attribute__((packed)) B { char c; long i; short s; };
struct __attribute__((packed)) C { char c; int a[2]; };
union  __attribute__((packed)) U { char c; double d; };
struct A a = { 1, 2 };
struct B b = { 3, 4, 5 };
struct C c = { 6, { 7, 8 } };
union U u = { .d = 9 };
static struct A make(void) { struct A x = { 10, 11 }; return x; }
int main(void) {
    struct A m = make();
    a.i = a.i + 1.0;
    printf("%d %g %d %ld %d %d %d %d %g | %d %g %zu %zu %zu\n",
           a.c, a.i, b.c, b.i, b.s, c.c, c.a[0], c.a[1], u.d,
           m.c, m.i, offsetof(struct A, i), offsetof(struct B, i), sizeof(struct A));
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/packed" "$tmp/packed.c" > "$tmp/log" 2>&1; then
    got=$("$tmp/packed")
    want=$(printf '1 3 3 4 5 6 7 8 9 | 10 11 1 1 9')
    if [ "$got" = "$want" ]; then
        echo "testing a packed record's type and initializer ... passed"
        n_pass=$((n_pass + 1))
    else
        echo "testing a packed record's type and initializer ... FAILED"
        echo "    got  $got"
        echo "    want $want"
        n_fail=$((n_fail + 1))
    fi
else
    echo "testing a packed record's type and initializer ... FAILED (compile)"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- x86-64: an array of 16 bytes or more is 16-aligned --------------
# The psABI's rule for objects, and what SSE code assumes of what it loads with
# movaps. It is the object's alignment and not the type's: a member array keeps
# its element's, so `struct { char buf[16]; char c; }` stays 17 bytes with
# alignment 1. cxx used the element's for objects too, so a movaps on such a
# global faulted -- the Fujitsu suite's C/0163 is exactly that program.
cat > "$tmp/bigalign.c" <<'EOF'
#include <stdio.h>
double init[] = { 1.0, 2.0 };   /* completed by the initializer */
double data[] = { 5.0, 6.0 };
double res[] = { 0, 0 };
struct S { char buf[16]; char c; };
struct T { double a[2]; };
int main(void) {
#if defined(__x86_64__)
    asm("movaps init(%rip), %xmm0\n\t"
        "movaps data(%rip), %xmm1\n\t"
        "addsd %xmm1, %xmm0\n\t"
        "movaps %xmm0, res(%rip)");
    printf("%g %g\n", res[0], res[1]);
#else
    printf("%g %g\n", init[0] + data[0], init[1]);
#endif
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/bigalign" "$tmp/bigalign.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/bigalign")" = "6 2" ]; then
    echo "testing a 16-byte array global is 16-aligned (movaps) ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a 16-byte array global is 16-aligned (movaps) ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# The rule must not reach the aggregate layout: gcc and clang both give these
# sizes and alignments, and the type-side version of the fix broke them.
cat > "$tmp/bigalign2.c" <<'EOF'
#include <stdio.h>
struct A { double a[2]; };
struct B { float f[4]; };
struct C { char buf[16]; char c; };
struct D { char c; double a[2]; };
int main(void) {
    printf("%zu/%zu %zu/%zu %zu/%zu %zu/%zu\n",
           sizeof(struct A), _Alignof(struct A), sizeof(struct B), _Alignof(struct B),
           sizeof(struct C), _Alignof(struct C), sizeof(struct D), _Alignof(struct D));
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/bigalign2" "$tmp/bigalign2.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/bigalign2")" = "16/8 16/4 17/1 24/8" ]; then
    echo "testing the 16-byte rule stays out of the aggregate layout ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the 16-byte rule stays out of the aggregate layout ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a block-scope declaration and the file-scope definition ---------
# 6.2.2p2: one identifier in two scopes with external linkage is one entity --
# the linkage ties them, not the visibility of the name (the block's name is
# not visible at file scope, which is why the definition's lookup did not find
# the declaration). cxx made two symbols, and the printer, which emits one
# entry per object-file name with the first one winning, kept the declaration
# and dropped the definition: the link failed with "undefined reference".
cat > "$tmp/blkscope.c" <<'EOF'
#include <stdio.h>
int f(int);                       /* file scope, so the block below is a redeclaration */
static int counter = 0;
int main(void) {
    {
        int g(int);               /* block scope: no storage class means extern */
        extern int x;             /* the object case, same rule */
        counter = g(1) + x;
    }
    printf("%d\n", counter);
    return 0;
}
int g(int v) { return v + 40; }
int x = 1;
EOF
if "$compiler" -w -o "$tmp/blkscope" "$tmp/blkscope.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/blkscope")" = "42" ]; then
    echo "testing a block-scope declaration with a later definition ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a block-scope declaration with a later definition ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# The definition may also come first, which always worked; and a `static`
# definition after a non-static declaration stays an error (6.2.2p7).
cat > "$tmp/blkscope2.c" <<'EOF'
int h(int v) { return v + 1; }
int main(void) { { int h(int); return h(1) == 2 ? 0 : 1; } }
EOF
if "$compiler" -w -o "$tmp/blkscope2" "$tmp/blkscope2.c" > "$tmp/log" 2>&1 &&
   "$tmp/blkscope2"; then
    echo "testing a definition before the block-scope declaration ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a definition before the block-scope declaration ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

cat > "$tmp/blkscope3.c" <<'EOF'
int main(void) { { int k(int); return k(1); } }
static int k(int v) { return v; }
EOF
if "$compiler" -w -c -o "$tmp/blkscope3.o" "$tmp/blkscope3.c" > "$tmp/log" 2>&1; then
    echo "testing static after non-static stays an error ... FAILED (accepted)"
    n_fail=$((n_fail + 1))
else
    echo "testing static after non-static stays an error ... passed"
    n_pass=$((n_pass + 1))
fi

# --- a hexadecimal fraction with no digits before the point ---------
# 6.4.4.2 writes the fraction as `hexadecimal-digit-sequence_opt .
# hexadecimal-digit-sequence`, so `0x.8p0` is the fraction alone -- the
# Fujitsu suite's C/0015 writes `float d4 = 0x.8p0f;`. cxx demanded a digit
# right after the prefix and called the rest an invalid suffix. A dot with
# digits on neither side is not a constant, and neither is a hex float with no
# exponent.
cat > "$tmp/hexfrac.c" <<'EOF'
#include <stdio.h>
int main(void) {
    double a = 0x.8p0;        /* 0.5 */
    double b = 0x.8p1;        /* 1.0 */
    float  c = 0x.8p0f;
    double d = 0x.0000001p0;
    long double e = 0x.8p0L;
    double f = 0x1.p0;        /* the other side of the point */
    printf("%.6f %.6f %.6f %.9f %.6Lf %.6f\n", a, b, (double)c, d, e, f);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/hexfrac" "$tmp/hexfrac.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/hexfrac")" = "0.500000 1.000000 0.500000 0.000000004 0.500000 1.000000" ]; then
    echo "testing a hexadecimal fraction with no digits before the point ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a hexadecimal fraction with no digits before the point ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

cat > "$tmp/hexbad.c" <<'EOF'
double a = 0x.p0;
int main(void) { return 0; }
EOF
if "$compiler" -w -c -o "$tmp/hexbad.o" "$tmp/hexbad.c" > "$tmp/log" 2>&1; then
    echo "testing 0x.p0 is not a constant ... FAILED (accepted)"
    n_fail=$((n_fail + 1))
else
    echo "testing 0x.p0 is not a constant ... passed"
    n_pass=$((n_pass + 1))
fi

# --- _Atomic as a type qualifier -------------------------------------
# 6.7.3p1 lists _Atomic with const, volatile and restrict, and as a qualifier
# it designates an atomic type: `int * _Atomic p` makes the pointer itself the
# atomic object. cxx's typequal() knew the other three only, so the
# declaration died with "expected ',' before '_Atomic'" while gcc and clang
# took it. The bit is the one the specifier form `_Atomic(T)` sets, so the
# atomic load/store path behind it applies unchanged.
cat > "$tmp/atomicq.c" <<'EOF'
#include <stdatomic.h>
#include <stdio.h>
int * _Atomic ap;                 /* an atomic pointer */
_Atomic int ai;                   /* the specifier form, unchanged */
int * _Atomic * aap;
const _Atomic int *cap;
struct S { int a; };
struct S * _Atomic sp;
int main(void) {
    int x = 5;
    struct S s = {9};
    ap = &x;
    sp = &s;
    atomic_store(&ai, 7);
    if (atomic_load(&ai) != 7) return 1;
    if (ap != &x) return 2;
    if (sp->a != 9) return 3;
    if (sizeof(ap) != sizeof(int *)) return 4;
    (void)aap; (void)cap;
    printf("%d %d %zu\n", *ap, atomic_load(&ai), sizeof(ap));
    return 0;
}
EOF
if "$compiler" -std=c23 -w -o "$tmp/atomicq" "$tmp/atomicq.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/atomicq")" = "5 7 8" ]; then
    echo "testing _Atomic as a type qualifier ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing _Atomic as a type qualifier ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- adding a qualifier inside an array -----------------------------
# 6.7.3p9 puts a qualifier written on an array type on the element, so
# `const uint8_t (*)[256]` is a qualified version of `uint8_t (*)[256]` and the
# assignment is the ordinary "add a qualifier" conversion. cxx compared the
# pointee with is_compatible(), which wants the qualifiers equal, and refused
# -- three shapes of it across ~15 of FFmpeg's files:
#
#   vc1_mc.c:225   luty = v->curr_luty;                  uint8_t (*)[256]
#   dcaenc.c:193   bitalloc_tables[i][j] = dst - offset; uint16_t (*)[2]
#   dsd.c:106      const double (*const c)[256] = a ? t1 : t2;
#
# The trap stays: a qualifier below a pointer is part of the pointed-to type,
# so `const int **` and `int **` remain different types.
cat > "$tmp/pointee_qual.c" <<'EOF'
#include <stdint.h>
#include <stdio.h>
static double t1[2][256], t2[2][256];
static uint16_t tab[3][2];
static int *ints[2];
int main(void) {
    const double (*const ctables)[256] = 1 ? t1 : t2;   /* dsd.c:106 */
    const uint8_t (*luty)[256];
    uint8_t (*cur)[256] = 0;
    const uint16_t (*bt)[2] = tab - 0;                  /* dcaenc.c:193 */
    const int *const *cp = (const int *const *)ints;    /* adding below: fine */
    luty = cur;                                         /* vc1_mc.c:225 */
    if (ctables != t1 && ctables != t2) return 1;
    if (luty != cur) return 2;
    if (bt != tab) return 3;
    (void)cp;
    printf("%d %d\n", (int)sizeof(*ctables), (int)sizeof(*luty));
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/pointee_qual" "$tmp/pointee_qual.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/pointee_qual")" = "2048 256" ]; then
    echo "testing a qualifier added inside an array ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a qualifier added inside an array ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# A qualifier below a pointer is diagnosed, and converted anyway -- clang's
# answer ("assigning to 'const int **' from 'int **' discards qualifiers"),
# where gcc refuses. C/0137's five tests are written for clang's answer, and
# 6.5.16.1p1 wants pointers to qualified or unqualified versions of compatible
# types, which this pair is once the qualifier is set aside.
cat > "$tmp/pointee_bad.c" <<'EOF'
int g(int **p) { const int **q = p; (void)q; return 0; }
int h(void) { volatile long long **a; long long *b = 0; a = &b; return **a != 0; }
EOF
if "$compiler" -c -o "$tmp/pointee_bad.o" "$tmp/pointee_bad.c" > "$tmp/log" 2>&1 &&
   grep -q 'discards qualifiers' "$tmp/log"; then
    echo "testing the qualifier trap below a pointer ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the qualifier trap below a pointer ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# Dropping one inside an array is the direction that discards, and it warns.
cat > "$tmp/pointee_drop.c" <<'EOF'
#include <stdint.h>
void f(const uint8_t (*src)[256]) { uint8_t (*dst)[256] = src; (void)dst; }
EOF
if "$compiler" -c -o "$tmp/pointee_drop.o" "$tmp/pointee_drop.c" > "$tmp/log" 2>&1 &&
   grep -q 'discards qualifiers' "$tmp/log"; then
    echo "testing a qualifier dropped inside an array warns ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a qualifier dropped inside an array warns ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- every atomic_ name 7.17.6 requires ------------------------------
# The draft's Table 7.6 lists 38 type names. cxx's <stdatomic.h> had 14 of
# them plus size_t/ptrdiff_t; the whole char16_t/char32_t/wchar_t row, the
# eight least, the eight fast, intptr/uintptr and intmax/uintmax were missing,
# so `atomic_uintptr_t refcount;` was "a type specifier is required for all
# declarations". That is three of FFmpeg's files, and the names come from
# <stdint.h> and <uchar.h>, which the header now includes.
cat > "$tmp/atomic_names.c" <<'EOF'
#include <stdatomic.h>
#include <stdio.h>
atomic_bool v1; atomic_char v2; atomic_schar v3; atomic_uchar v4;
atomic_short v5; atomic_ushort v6; atomic_int v7; atomic_uint v8;
atomic_long v9; atomic_ulong v10; atomic_llong v11; atomic_ullong v12;
atomic_char16_t v13; atomic_char32_t v14; atomic_wchar_t v15;
atomic_int_least8_t v16; atomic_uint_least8_t v17;
atomic_int_least16_t v18; atomic_uint_least16_t v19;
atomic_int_least32_t v20; atomic_uint_least32_t v21;
atomic_int_least64_t v22; atomic_uint_least64_t v23;
atomic_int_fast8_t v24; atomic_uint_fast8_t v25;
atomic_int_fast16_t v26; atomic_uint_fast16_t v27;
atomic_int_fast32_t v28; atomic_uint_fast32_t v29;
atomic_int_fast64_t v30; atomic_uint_fast64_t v31;
atomic_intptr_t v32; atomic_uintptr_t v33;
atomic_size_t v34; atomic_ptrdiff_t v35;
atomic_intmax_t v36; atomic_uintmax_t v37;
/* each name has to be the atomic of its direct type, and usable */
_Static_assert(sizeof(atomic_uintptr_t) == sizeof(uintptr_t), "atomic_uintptr_t");
_Static_assert(sizeof(atomic_int_least64_t) == sizeof(int_least64_t), "atomic_int_least64_t");
int main(void) {
    atomic_store(&v33, (uintptr_t)0x1234);
    atomic_store(&v22, (int_least64_t)-7);
    atomic_store(&v14, (char32_t)0x41);
    if (atomic_load(&v33) != 0x1234) return 1;
    if (atomic_load(&v22) != -7) return 2;
    if (atomic_load(&v14) != 0x41) return 3;
    printf("%zu %zu\n", sizeof(atomic_uintptr_t), sizeof(atomic_int_least64_t));
    return 0;
}
EOF
if "$compiler" -std=c23 -w -o "$tmp/atomic_names" "$tmp/atomic_names.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/atomic_names")" = "8 8" ]; then
    echo "testing every atomic_ type name of 7.17.6 ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing every atomic_ type name of 7.17.6 ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- -Wp, -Wa, and -x assembler-with-cpp ----------------------------
# None of these is a warning switch, though all three start with -W or look
# like a -W argument. cxx read `-Wa,--version` as -W + "a,--version" and
# refused it as an unknown warning group; the kernel runs exactly that in
# scripts/as-version.sh and passes -Wp,-MMD,$(depfile) on every compile
# (seven times in its Makefiles) and -Wa,--fatal-warnings in its Kbuild.
# Both references hand the first to the assembler and the second to the
# preprocessor. -Wp,-MD,file / -Wp,-MMD,file are what -MD/-MMD with -MF file
# spell, which is how cxx already writes dependencies.
cat > "$tmp/wp.c" <<'EOF'
int x;
int f(void) { return x; }
EOF
rm -f "$tmp/wp.d"
if "$compiler" -Wp,-MMD,"$tmp/wp.d" -c -o "$tmp/wp.o" "$tmp/wp.c" > "$tmp/log" 2>&1 &&
   grep -q 'wp.o' "$tmp/wp.d" 2>/dev/null && grep -q 'wp.c' "$tmp/wp.d" 2>/dev/null; then
    echo "testing -Wp,-MMD,file writes a dependency file ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -Wp,-MMD,file writes a dependency file ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -3
    n_fail=$((n_fail + 1))
fi

if "$compiler" -Wa,--fatal-warnings -c -o "$tmp/wp.o" "$tmp/wp.c" > "$tmp/log" 2>&1; then
    echo "testing -Wa, options reach the assembler ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -Wa, options reach the assembler ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -3
    n_fail=$((n_fail + 1))
fi

# An option cxx cannot honour is refused by name, never dropped: a silently
# ignored -Wp,-D would change the program.
printf '#include <stdio.h>\nint main(void) { return 0; }\n' > "$tmp/wp2.c"
if "$compiler" -Wp,-Dfoo=1 -c -o "$tmp/wp2.o" "$tmp/wp2.c" > "$tmp/log" 2>&1; then
    echo "testing an unsupported -Wp, option is refused ... FAILED (accepted)"
    n_fail=$((n_fail + 1))
elif grep -q 'unsupported preprocessor option' "$tmp/log"; then
    echo "testing an unsupported -Wp, option is refused ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing an unsupported -Wp, option is refused ... FAILED (wrong message)"
    sed 's/^/    /' "$tmp/log" | head -3
    n_fail=$((n_fail + 1))
fi

# -x assembler-with-cpp is the name of the mode cxx already has: the
# preprocessor runs first, then the assembler, which is what a .S file is.
cat > "$tmp/wp3.s" <<'EOF'
#define VALUE 42
.globl answer
answer:
	.long VALUE
EOF
if "$compiler" -x assembler-with-cpp -c -o "$tmp/wp3.o" "$tmp/wp3.s" > "$tmp/log" 2>&1; then
    echo "testing -x assembler-with-cpp ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -x assembler-with-cpp ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -3
    n_fail=$((n_fail + 1))
fi

# --- -funsigned-char / -fsigned-char keep the macros in step -------
# The switch is implementation-defined (6.2.5) and has three visible effects:
# plain char's signedness, the __CHAR_UNSIGNED__ macro, and <limits.h>'s
# CHAR_MIN/CHAR_MAX -- glibc derives the last from the macro. cxx used to flip
# only the first, so `-funsigned-char` reported CHAR_MIN as -128; and it
# rejected -fno-signed-char, which both references read as -funsigned-char.
# `signed char` and `unsigned char` are separate types and must not move.
cat > "$tmp/charsign.c" <<'EOF'
#include <stdio.h>
#include <limits.h>
int main(void) {
    char c = -1;
    signed char sc = -1;
    unsigned char uc = 255;
#ifdef __CHAR_UNSIGNED__
    int macro = 1;
#else
    int macro = 0;
#endif
    printf("%s %d %d %d %d %d\n", c < 0 ? "s" : "u", macro, (int)CHAR_MIN, (int)CHAR_MAX,
           (int)sc, (int)uc);
    return 0;
}
EOF
charsign_case() { # charsign_case <flags> <expected>
    rm -f "$tmp/charsign"
    if "$compiler" $1 -w -o "$tmp/charsign" "$tmp/charsign.c" > "$tmp/log" 2>&1 &&
       [ "$("$tmp/charsign")" = "$2" ]; then
        echo "testing char signedness with '$1' ... passed"
        n_pass=$((n_pass + 1))
    else
        echo "testing char signedness with '$1' ... FAILED (wanted '$2', got '$("$tmp/charsign" 2>/dev/null)')"
        sed 's/^/    /' "$tmp/log" | head -3
        n_fail=$((n_fail + 1))
    fi
}
charsign_case "-fsigned-char"   "s 0 -128 127 -1 255"
charsign_case "-funsigned-char" "u 1 0 255 -1 255"
charsign_case "-fno-signed-char" "u 1 0 255 -1 255"

# --- a constant condition keeps only the arm it takes ---------------
# cxx emitted both arms. The arm not taken can hold an asm whose operand is
# not a constant for an immediate constraint, and LLVM validates every
# function before it optimises, so it refused the whole translation unit even
# though nothing could reach it. ffmpeg writes exactly this
# (`if (__builtin_constant_p(s)) asm(.. "i" ..) else asm(.. "c" ..)`, in
# libavcodec/x86/mathops.h) and 176 of its objects stopped there. gcc emits
# only the arm taken.
#
# A function that defines a label keeps both arms: a goto, asm goto included,
# can still name a label inside the one that would go.
cat > "$tmp/constcond.c" <<'EOF'
#include <stdio.h>
static int taken(void) {
    int x = 1;
    if (0) { x = 100; }
    if (1) { x = x + 1; }
    if (0) { x = 100; } else { x = x + 1; }
    return x;
}
/* the ffmpeg shape: only the arm with the constant operand may survive */
static inline int shift_it(int a, int s) {
    if (__builtin_constant_p(s))
        __asm__("shrl %1, %0\n\t" : "+r"(a) : "i"(-s & 0x1F));
    else
        __asm__("shrl %1, %0\n\t" : "+r"(a) : "c"((unsigned char)(-s)));
    return a;
}
static int with_label(int x) {
    if (0) { goto out; }
    x += 1;
    return x;
out:
    return 42;
}
int main(void) {
    /* -s & 0x1F for s = 7 is 25, so 0x80u >> 25 is 0 and (1u << 25) >> 25 is
       1; both references print these. */
    if (taken() != 3) return 1;
    if (shift_it(0x80u, 7) != 0) return 2;
    if (with_label(1) != 2) return 3;
    if (shift_it(1u << 25, 7) != 1) return 4;
    if (shift_it(0xffu, 4) != 0) return 5;
    printf("%d %d %d\n", taken(), shift_it(0x80u, 7), with_label(1));
    printf("%d %d\n", shift_it(1u << 25, 7), shift_it(0xffu, 4));
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/constcond" "$tmp/constcond.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/constcond")" = "3 0 2
1 0" ]; then
    echo "testing a constant condition keeps only the arm it takes ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a constant condition keeps only the arm it takes ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- the address of an array a subscript produced -------------------
# A static initializer may name the row of a multidimensional array:
# `static int *p = m[1];`. The constant evaluator's shared ND_SUBACCESS /
# ND_MEMBER tail read `node->member->offset` unconditionally, and a subscript
# has no member -- so this dereferenced NULL and killed cxx with SIGSEGV.
# It is the crash behind 27 of FFmpeg's failing objects, whose MPEG
# translation units build static tables out of such rows.
cat > "$tmp/rowaddr.c" <<'EOF'
#include <stdio.h>
static int m[2][3] = {{1, 2, 3}, {4, 5, 6}};
static int *p = m[1];
static int *r = m[0];
static int (*q)[3] = &m[0];
static char c[2][4] = {"abc", "def"};
static char *cp = c[1];
struct S { int a[3]; };
static struct S s = {{7, 8, 9}};
static int *t = s.a;
int main(void) {
    if (p != m[1] || r != m[0] || t != s.a || cp != c[1]) return 1;
    if (p[0] != 4 || p[2] != 6 || r[0] != 1) return 2;
    if (cp[0] != 'd' || cp[2] != 'f') return 3;
    if (t[0] != 7 || (*q)[1] != 2) return 4;
    printf("%d %d %d %s\n", p[0], r[0], t[2], cp);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/rowaddr" "$tmp/rowaddr.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/rowaddr")" = "4 1 9 def" ]; then
    echo "testing the address of an array a subscript produced ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the address of an array a subscript produced ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a sign difference between pointed-to types ---------------------
# gcc's and clang's -Wpointer-sign, which is in -Wall: the pointed-to types
# differ only in signedness, and both references convert and carry on. cxx
# refused the file. FFmpeg has two of these and its build stopped on them
# (av_strcasecmp(standard.name, s->standard) with __u8[32] against a
# `const char *`, and av_fast_malloc(&c->y, &c->y_size, ...) with `int *`
# against an `unsigned int *`).
cat > "$tmp/ptrsign.c" <<'EOF'
#include <stdio.h>
static char *take(const char *s) { return (char *)s; }
static int takeu(unsigned int *n) { return (int)*n; }
int main(void) {
    unsigned char u[4] = "abc";
    int n = 5;
    char *r = take(u);                 /* unsigned char * for const char * */
    if (takeu(&n) != 5) return 1;      /* int * for unsigned int * */
    if (r[0] != 'a') return 2;
    return 0;
}
EOF
if "$compiler" -Wpointer-sign -o "$tmp/ptrsign" "$tmp/ptrsign.c" > "$tmp/log" 2>&1 &&
   "$tmp/ptrsign" && [ "$(grep -c 'differ in signedness' "$tmp/log")" = 2 ]; then
    echo "testing a sign difference between pointed-to types ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a sign difference between pointed-to types ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# -Wno-pointer-sign silences it ...
cat > "$tmp/ptrsign2.c" <<'EOF'
void take(const char *s);
int main(void) { unsigned char u[4] = "abc"; take(u); return 0; }
EOF
if "$compiler" -Wno-pointer-sign -c -o "$tmp/ptrsign2.o" "$tmp/ptrsign2.c" > "$tmp/log" 2>&1 &&
   ! grep -q 'differ in signedness' "$tmp/log"; then
    echo "testing -Wno-pointer-sign silences it ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -Wno-pointer-sign silences it ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# ... while a mismatch that is not a sign difference stays an error.
cat > "$tmp/ptrbad.c" <<'EOF'
void g(double *);
int main(void) { int x = 0; g(&x); return 0; }
EOF
if "$compiler" -c -o "$tmp/ptrbad.o" "$tmp/ptrbad.c" > "$tmp/log" 2>&1; then
    echo "testing a real pointer mismatch is still refused ... FAILED (accepted)"
    n_fail=$((n_fail + 1))
elif grep -q 'incompatible types when passing argument' "$tmp/log"; then
    echo "testing a real pointer mismatch is still refused ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a real pointer mismatch is still refused ... FAILED (wrong message)"
    sed 's/^/    /' "$tmp/log" | head -3
    n_fail=$((n_fail + 1))
fi

# --- packed and an aligned bit-field together ----------------------
# `#pragma pack(push,1)` caps a member's alignment, but an explicitly aligned
# bit-field still starts at the next boundary that alignment asks for -- one
# byte, here. `long long z:63` ends at bit 81 and `a` moves to bit 88, so the
# record is 12 bytes and not 11. tinycc's tests2/95_bitfields.c keeps its
# "PACKED - WITH ALIGN" sections under exactly this pragma.
cat > "$tmp/packalign.c" <<'EOF'
#include <stdio.h>
#pragma pack(push, 1)
struct P {
    int x : 12;
    char y : 6;
    long long z : 63;
    __attribute__((aligned(16))) char a : 4;
    long long b : 2;
};
#pragma pack(pop)
struct Q {
    int x : 12;
    char y : 6;
    long long z : 63;
    __attribute__((aligned(16))) char a : 4;
    long long b : 2;
};
struct R {
    int x : 12;
    char y : 6;
    long long z : 63;
    char a : 4;
    long long b : 2;
};
int main(void) {
    if (__alignof__(struct P) != 1 || sizeof(struct P) != 12) {
        printf("P %d %d\n", __alignof__(struct P), (int)sizeof(struct P));
        return 1;
    }
    if (__alignof__(struct Q) != 16 || sizeof(struct Q) != 32) {
        printf("Q %d %d\n", __alignof__(struct Q), (int)sizeof(struct Q));
        return 2;
    }
    if (__alignof__(struct R) != 8 || sizeof(struct R) != 24) {
        printf("R %d %d\n", __alignof__(struct R), (int)sizeof(struct R));
        return 3;
    }
    /* the field really is at byte 11 of P and byte 16 of Q */
    struct P p;
    unsigned char *q = (unsigned char *)&p;
    for (int i = 0; i < (int)sizeof p; i++) q[i] = 0;
    p.a = -1;
    if (q[11] != 0x0f) { printf("P.a at 11 = %02x\n", q[11]); return 4; }
    struct Q s;
    q = (unsigned char *)&s;
    for (int i = 0; i < (int)sizeof s; i++) q[i] = 0;
    s.a = -1;
    if (q[16] != 0x0f) { printf("Q.a at 16 = %02x\n", q[16]); return 5; }
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/packalign" "$tmp/packalign.c" > "$tmp/log" 2>&1 && "$tmp/packalign"; then
    echo "testing packed with an aligned bit-field ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing packed with an aligned bit-field ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a VLA's address depends on its size alone ---------------------
# Jumping back into the scope of a variable length array re-executes its
# declaration without leaving the block, so a fresh alloca per pass walks the
# stack down one array at a time. gcc and clang hoist the array into the frame
# (they can bound `n % 100 + 1`); cxx records where the object started on the
# first execution and puts the stack pointer back there before allocating, so
# the address is a function of the size. tinycc's tests2/122_vla_reuse.c
# measures exactly this: it records the address per size over the first
# hundred passes and compares on every later one.
cat > "$tmp/vlareuse.c" <<'EOF'
#include <stdio.h>
int main(void) {
    int n = 0, first = 1;
    int *p[101];
    if (0) {
    lab:;
    }
    int x[n % 100 + 1];
    if (first == 0) {
        if (&x[0] != p[n % 100 + 1]) {
            printf("ERROR: %p %p\n", (void *)&x[0], (void *)p[n % 100 + 1]);
            return 1;
        }
    } else {
        p[n % 100 + 1] = &x[0];
        first = n < 100;
    }
    x[0] = 1;
    x[n % 100] = 2;
    n++;
    if (n < 20000)
        goto lab;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/vlareuse" "$tmp/vlareuse.c" > "$tmp/log" 2>&1 && "$tmp/vlareuse"; then
    echo "testing a VLA's address depends on its size alone ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a VLA's address depends on its size alone ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- several VLAs in one scope must not overlap --------------------
# The reuse slot is only sound when the function declares a single VLA: with
# two, restoring one declaration's base frees the object of the other, which
# is still alive. cxx turns the guard off when the function has more than one,
# which leaves the plain stack discipline -- objects below one another, never
# overlapping, as the sizes grow from pass to pass.
cat > "$tmp/vlamulti.c" <<'EOF'
#include <stdio.h>
int main(void) {
    for (int r = 1; r <= 6; r++) {
        int a[r], b[2 * r], c[3 * r];
        a[0] = 11;
        b[0] = 22;
        c[0] = 33;
        if ((char *)a <= (char *)b || (char *)b <= (char *)c)
            return 1;                        /* declared order, downward */
        if ((char *)a - (char *)b < 2 * r * (int)sizeof(int))
            return 2;                        /* a and b must not overlap */
        if ((char *)b - (char *)c < 3 * r * (int)sizeof(int))
            return 3;                        /* b and c must not overlap */
        if (a[0] != 11 || b[0] != 22 || c[0] != 33)
            return 4;
    }
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/vlamulti" "$tmp/vlamulti.c" > "$tmp/log" 2>&1 && "$tmp/vlamulti"; then
    echo "testing several VLAs in one scope do not overlap ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing several VLAs in one scope do not overlap ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a GNU aligned attribute on a member ---------------------------
# `__attribute__((aligned(16))) int b;` is a member attribute: it raises the
# member's alignment and with it the record's. cxx parsed it and dropped it --
# the member loop passed NULL for both the alignment and the attribute list of
# declspecs, so only the post-declarator spelling reached the member. An
# explicitly aligned bit-field also starts on its alignment (gcc puts
# `__attribute__((aligned(16))) char a : 4;` at byte 16), while `_Alignas`
# stays refused on a bit-field, which is what gcc does too.
cat > "$tmp/memalign.c" <<'EOF'
#include <stdio.h>
#define A __attribute__((aligned(16)))
struct S1 { int x : 12; char y : 6; long long z : 63; A char a : 4; long long b : 2; };
struct S2 { char a; A int b; };
struct S3 { char a; int b; };
struct S4 { A char a : 4; };
int main(void) {
    if (__alignof__(struct S1) != 16 || sizeof(struct S1) != 32) {
        printf("S1 %d %d\n", __alignof__(struct S1), (int)sizeof(struct S1));
        return 1;
    }
    if (__alignof__(struct S2) != 16 || sizeof(struct S2) != 32) {
        printf("S2 %d %d\n", __alignof__(struct S2), (int)sizeof(struct S2));
        return 2;
    }
    if (__alignof__(struct S3) != 4 || sizeof(struct S3) != 8) {
        printf("S3 %d %d\n", __alignof__(struct S3), (int)sizeof(struct S3));
        return 3;
    }
    if (__alignof__(struct S4) != 16 || sizeof(struct S4) != 16) {
        printf("S4 %d %d\n", __alignof__(struct S4), (int)sizeof(struct S4));
        return 4;
    }
    /* the aligned bit-field really is at byte 16 of S1 */
    struct S1 s;
    unsigned char *q = (unsigned char *)&s;
    for (int i = 0; i < (int)sizeof s; i++) q[i] = 0;
    s.a = -1;
    if (q[16] != 0x0f) { printf("a at %d = %02x\n", 16, q[16]); return 5; }
    for (int i = 0; i < 16; i++)
        if (q[i]) { printf("stray byte %d = %02x\n", i, q[i]); return 6; }
    /* the post-declarator spelling must keep working */
    struct S5 { char a; int b __attribute__((aligned(16))); };
    if (__alignof__(struct S5) != 16 || sizeof(struct S5) != 32) {
        printf("S5 %d %d\n", __alignof__(struct S5), (int)sizeof(struct S5));
        return 7;
    }
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/memalign" "$tmp/memalign.c" > "$tmp/log" 2>&1 && "$tmp/memalign"; then
    echo "testing a GNU aligned attribute on a member ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a GNU aligned attribute on a member ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# `_Alignas` on a bit-field is still an error, as in gcc.
cat > "$tmp/alnbf.c" <<'EOF'
struct S { _Alignas(16) char a : 4; };
EOF
if "$compiler" -w -c -o "$tmp/alnbf.o" "$tmp/alnbf.c" > "$tmp/log" 2>&1; then
    echo "testing _Alignas on a bit-field is refused ... FAILED (accepted)"
    n_fail=$((n_fail + 1))
else
    if grep -q "_Alignas' cannot be applied to a bit-field" "$tmp/log"; then
        echo "testing _Alignas on a bit-field is refused ... passed"
        n_pass=$((n_pass + 1))
    else
        echo "testing _Alignas on a bit-field is refused ... FAILED (wrong message)"
        sed 's/^/    /' "$tmp/log" | head -3
        n_fail=$((n_fail + 1))
    fi
fi

# --- a bit-field that reaches past the widest unit ------------------
# A packed record can put a 63-bit field at bit offset 2, so the field begins
# in one eight-byte unit and ends in the next (bit 81). cxx chose a unit of
# min_bytes_for_bits(bit_offset + width) bytes, but that helper stopped at 8,
# so the unit was too small: the load's shift amount came out as
# 64 - 63 - 2 = -1, which LLVM takes modulo 64, and the field read back as
# 0xc000000000000000. The unit is a _BitInt past eight bytes now, and the
# shift amounts come from the unit's width rather than its size (a _BitInt's
# size is rounded up to what the target holds: _BitInt(72) has size 16).
# tinycc's tests2/95_bitfields.c TEST 2 measures exactly this.
cat > "$tmp/widebf.c" <<'EOF'
#include <stdio.h>
#include <string.h>
struct __attribute__((packed)) S {
    int x : 12;
    char y : 6;
    long long z : 63;
    char a : 4;
    long long b : 2;
};
static void dump(void *p, int s) {
    for (int i = s; --i >= 0;)
        printf("%02X", ((unsigned char *)p)[i]);
    printf("\n");
}
int main(void) {
    struct S s;
    memset(&s, 0, sizeof s);
    if (sizeof s != 11) { printf("size %d\n", (int)sizeof s); return 1; }
    s.x = -1, s.y = -1, s.z = -1, s.a = -1, s.b = -1;
    printf("set  : "), dump(&s, sizeof s);
    s.x = 3, s.y = 30, s.z = 0x123456789abcdef0LL, s.a += 5, ++s.a, s.b = 2;
    printf("value: "), dump(&s, sizeof s);
    if (s.x != 3 || s.y != 30 || s.z != 0x123456789abcdef0LL || s.a != 5 || s.b != -2) {
        printf("read : %d %d %llx %d %d\n", s.x, s.y, (unsigned long long)s.z, s.a, s.b);
        return 2;
    }
    /* the same field through a pointer, and a second field past the first */
    struct S *p = &s;
    if (p->z != 0x123456789abcdef0LL) { printf("ptr: %llx\n", (unsigned long long)p->z); return 3; }
    p->z = -1;
    if (p->z != -1) { printf("neg: %llx\n", (unsigned long long)p->z); return 4; }
    if (p->x != 3) { printf("neighbour: %d\n", p->x); return 5; }
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/widebf" "$tmp/widebf.c" > "$tmp/log" 2>&1 && "$tmp/widebf"; then
    echo "testing a bit-field past the widest unit ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a bit-field past the widest unit ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- pointer minus an unsigned integer -----------------------------
# The integer operand of a pointer subtraction is converted to a ptrdiff_t
# before the subtraction; cxx negated it in its own type and then widened the
# result by its (unsigned) type, so the offset came out as 2**32 - n. Found
# through tinycc: its tcc_eh_frame_hdr() does `cie = rd - cie_offset + 4` with
# an `unsigned int cie_offset`, and the cxx-built tcc then read every CIE at a
# wild address, counted no frame descriptors, wrote an empty .eh_frame_hdr,
# and no longer reproduced itself (work item T2).
cat > "$tmp/ptrsub.c" <<'EOF'
#include <stdio.h>
int main(void) {
    static unsigned char buf[256];
    unsigned char *data = buf;
    unsigned char *rd = data + 56;
    unsigned int u = 28;
    int s = 28;
    unsigned long ul = 28;
    if ((long)((rd - u) - data) != 28) { printf("unsigned int: %ld\n", (long)((rd - u) - data)); return 1; }
    if ((long)((rd - s) - data) != 28) { printf("int: %ld\n", (long)((rd - s) - data)); return 2; }
    if ((long)((rd - ul) - data) != 28) { printf("unsigned long: %ld\n", (long)((rd - ul) - data)); return 3; }
    if ((long)((rd - u + 4) - data) != 32) { printf("then +4: %ld\n", (long)((rd - u + 4) - data)); return 4; }
    /* and the value must be usable as an address */
    buf[56 - 28] = 9;
    unsigned char *at = rd - u;
    if (*at != 9) { printf("deref: %d\n", *at); return 5; }
    /* the same shape one past the end, as a loop bound */
    unsigned char *end = data + 100;
    long n = 0;
    for (unsigned char *q = rd - u; q < end; q += 8) n++;
    if (n != 9) { printf("loop: %ld\n", n); return 6; }
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/ptrsub" "$tmp/ptrsub.c" > "$tmp/log" 2>&1 && "$tmp/ptrsub"; then
    echo "testing pointer minus an unsigned integer ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing pointer minus an unsigned integer ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- an attribute anywhere in a declarator -------------------------
# GNU allows the attribute between the type and the `*`, or between the `(`
# of a nested declarator and its own declarator -- tinycc's
# tests2/82_attribs_position.c is written to check exactly that, with
# `int(ATTR *)(void)`. cxx read the attribute as part of the type and then
# asked for the `)` it was looking at.
cat > "$tmp/attrib.c" <<'EOF'
#define ATTR __attribute__((__noinline__))
static int actual_function(void) { return 42; }
int main(void) {
    void *fp = &actual_function;
    int a = ((ATTR int (*)(void))fp)();
    int b = ((int(ATTR *)(void))fp)();
    if (a != 42 || b != 42)
        return 1;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/attrib" "$tmp/attrib.c" > "$tmp/log" 2>&1 && "$tmp/attrib"; then
    echo "testing an attribute in the middle of a declarator ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing an attribute in the middle of a declarator ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a range designator evaluates its initializer once ----------------
# `int dd[] = {[0 ... 1] = ++c, [2 ... 3] = ++c}` leaves 1 1 2 2: gcc reads
# the expression once per range and gives every element of the range that
# value. cxx read it once per element and left 1 2 3 4. The evaluation hangs
# on the element rather than on the element's initializer, so a later
# designator overwriting that element cannot take it away -- which is what
# tinycc's tests2/90_struct-init.c does with `[1 ... 2] = &sys_ni` followed
# by `[1] = 0`, and what made the second element of that range read an
# unwritten temporary and call through whatever was on the stack.
cat > "$tmp/range.c" <<'EOF'
#include <stdio.h>
typedef void (*fptr)(void);
static int calls;
static void one(void) { calls += 1; }
static void two(void) { calls += 2; }
static void ni(void) { calls += 100; }
int main(void) {
    int c = 0;
    int dd[] = {[0 ... 1] = ++c, [2 ... 3] = ++c};
    if (c != 2) { printf("c=%d\n", c); return 1; }
    if (dd[0] != 1 || dd[1] != 1 || dd[2] != 2 || dd[3] != 2) {
        printf("dd=%d %d %d %d\n", dd[0], dd[1], dd[2], dd[3]);
        return 2;
    }
    /* the value comes from a variable, so the temporary holds a value and
       not the variable's address */
    int elt = 7;
    struct T { unsigned char s[16]; unsigned char a; };
    struct T t = {{[1 ... 5] = 9, [6 ... 10] = elt, [4 ... 7] = elt + 1}, 1};
    if (t.s[0] != 0 || t.s[1] != 9 || t.s[4] != 8 || t.s[7] != 8 || t.s[8] != 7 ||
        t.s[11] != 0 || t.a != 1) {
        printf("s=%d %d %d %d %d\n", t.s[1], t.s[4], t.s[7], t.s[8], t.s[11]);
        return 3;
    }
    /* a later designator overrides one element of the range, and the
       evaluation still happens: the other elements keep its value */
    const fptr tab[4] = {[0 ... 3] = ni, [1] = 0, [2] = one};
    for (int i = 0; i < 4; i++)
        if (tab[i]) tab[i]();
    if (calls != 201) { printf("calls=%d\n", calls); return 4; }
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/range" "$tmp/range.c" > "$tmp/log" 2>&1 && "$tmp/range"; then
    echo "testing a range designator's single evaluation ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a range designator's single evaluation ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a VLA's storage is released on every way out of its scope --------
# C11 6.2.4p6: the storage lasts until the block is left, and goto, break and
# continue leave it just as falling off the end does. cxx released it with a
# statement at the end of the block, so a jump past that statement leaked the
# stack pointer and the next declaration landed lower every pass -- measured
# as a sixteen-byte drift per pass where gcc and clang come back to the same
# address. The release now travels with the cleanup handlers, which were
# always built per jump.
cat > "$tmp/vlaleave.c" <<'EOF'
#include <stdio.h>
static int failures;
static void check(const char *what, void *first, void *now) {
    if (first != now) {
        printf("%s: drift %ld\n", what, (long)((char *)now - (char *)first));
        failures++;
    }
}
int main(void) {
    int n = 4;
    /* goto out of the block */
    void *p = 0;
    for (int pass = 0; pass < 2; pass++) {
        {
            int a[n];
            a[0] = pass;
            if (pass == 0) p = a;
            goto out;
        }
    out:;
    }
    /* the address only matches if the release ran */
    {
        int b[n];
        b[0] = 9;
        check("goto", p, b);
    }
    /* continue out of the block, inside a loop */
    {
        void *q = 0;
        for (int i = 0; i < 2; i++) {
            {
                int c[n];
                c[0] = i;
                if (i == 0) q = c;
                else check("continue", q, c);
                continue;
            }
        }
    }
    /* break out of the block */
    {
        void *r = 0;
        for (int i = 0; i < 2; i++) {
            {
                int d[n];
                d[0] = i;
                if (i == 0) r = d;
                break;
            }
        }
        int e[n];
        e[0] = 1;
        check("break", r, e);
    }
    return failures ? 1 : 0;
}
EOF
if "$compiler" -w -o "$tmp/vlaleave" "$tmp/vlaleave.c" > "$tmp/log" 2>&1 && "$tmp/vlaleave"; then
    echo "testing a VLA released on every way out of its scope ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a VLA released on every way out of its scope ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a handler runs before the array it names goes away ---------------
# The release is part of the same teardown as the cleanup calls, and runs
# after them: a handler of the same block may still read the array.
cat > "$tmp/vlaclean.c" <<'EOF'
#include <stdio.h>
static int seen;
static void h(int *p) { seen = p[0]; }
int main(void) {
    int n = 4;
    {
        int a[n];
        a[0] = 1234;
        __attribute__((cleanup(h))) int x = a[0];
        (void)x;
    }
    if (seen != 1234) {
        printf("handler saw %d\n", seen);
        return 1;
    }
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/vlaclean" "$tmp/vlaclean.c" > "$tmp/log" 2>&1 && "$tmp/vlaclean"; then
    echo "testing a handler reading the array of its own block ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a handler reading the array of its own block ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a compound literal inside a static initializer ----------------
# The object a compound literal names has static storage, and copying its
# *value* into another static object is what gcc and clang do: the element
# keeps the relocation. cxx mounted the copy only from a `constexpr` source,
# so the element kept its expression and folded to zero --
# `global_wrap[0].func` was NULL and tinycc's tests2/90_struct-init.c called
# through it.
cat > "$tmp/complit.c" <<'EOF'
struct wrap { void (*func)(void); int n; };
static int calls;
static void one(void) { calls += 1; }
static void two(void) { calls += 2; }
static struct wrap table[] = {((struct wrap){one, 1}), ((struct wrap){two, 2})};
static struct wrap single = ((struct wrap){one, 7});
int main(void) {
    if (!table[0].func || !table[1].func) return 1;
    table[0].func();
    table[1].func();
    if (calls != 3) return 2;
    if (table[0].n != 1 || table[1].n != 2) return 3;
    if (!single.func) return 4;
    single.func();
    if (calls != 4 || single.n != 7) return 5;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/complit" "$tmp/complit.c" > "$tmp/log" 2>&1 && "$tmp/complit"; then
    echo "testing a compound literal in a static initializer ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a compound literal in a static initializer ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a flexible array member keeps its record the same type ----------
# cxx leaves an incomplete flexible member a zero-length array and completes a
# copy of the record when an initializer gives it elements. Comparing the
# members by length made the two records different types, so `struct W *`
# refused the address of an object of that same `struct W` -- tinycc's
# tests2/90_struct-init.c is where that showed up (`struct W` ends in
# `struct S s[]`, and `gw` is initialized).
cat > "$tmp/fam.c" <<'EOF'
struct S { int a; };
struct W { int n; struct S s[]; };
struct W gw = {2, {{10}, {20}}};
static void want(struct W *w) { w->n = 3; }
static int sum(struct W *w) { return w->n + w->s[0].a + w->s[1].a; }
int main(void) {
    struct W empty = {1};
    want(&gw);
    if (sum(&gw) != 33) return 1;
    if (gw.s[1].a != 20) return 2;
    /* the same type, with or without elements in the initializer */
    want(&empty);
    if (empty.n != 3) return 3;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/fam" "$tmp/fam.c" > "$tmp/log" 2>&1 && "$tmp/fam"; then
    echo "testing a pointer to a record with a flexible array member ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a pointer to a record with a flexible array member ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a case range is a range, not a list of values --------------------
# `case lo ... hi:` is one label compared against both ends. Expanding it into
# one label per value made tinycc's tests2/118_switch.c -- which has a range
# over the whole of `long long` -- walk 9e18 values, and the compiler never
# finished.
cat > "$tmp/caserange.c" <<'EOF'
static int digits(long long n) {
    switch (n) {
    case 1LL ... 9LL: return 1;
    case 10LL ... 99LL: return 2;
    case 100LL ... 999LL: return 3;
    case -9223372036854775807LL - 1LL ... -1LL: return -1;
    case 0: return 0;
    }
    return 4;
}
static int small(int n) {
    switch (n) {
    case 0 ... 3: return 1;
    case 4 ... 7: return 2;
    default: return 3;
    }
}
int main(void) {
    if (digits(5) != 1 || digits(42) != 2 || digits(500) != 3) return 1;
    if (digits(1000) != 4) return 2;
    if (digits(-9223372036854775807LL - 1LL) != -1 || digits(-1) != -1) return 3;
    if (digits(0) != 0) return 4;
    if (small(0) != 1 || small(3) != 1 || small(4) != 2 || small(7) != 2 || small(8) != 3) return 5;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/caserange" "$tmp/caserange.c" > "$tmp/log" 2>&1 && "$tmp/caserange"; then
    echo "testing a case range over the whole of long long ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a case range over the whole of long long ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- the conditional operator's type (C11 6.5.15p6) -------------------
# In order: a null pointer constant takes the other arm's type; two pointers
# to compatible types make a pointer to the composite type, which carries the
# qualifiers of *both*; one arm pointing at void makes the result a qualified
# void pointer; and an incomplete array type is completed by the other arm.
# tinycc's tests2/94_generic.c walks through every one of them.
cat > "$tmp/condty.c" <<'EOF'
_Static_assert(_Generic(0 ? (long *)0 : (void *)0, long *: 1, default: 0), "a null constant takes the other type");
_Static_assert(_Generic(0 ? (long *)0 : 0, long *: 1, default: 0), "an integer zero likewise");
_Static_assert(_Generic(0 ? (long volatile *)0 : (long const *)0, long const volatile *: 1, default: 0),
               "the qualifiers of both arms combine");
_Static_assert(_Generic(0 ? (int volatile *)0 : (void const *)1, const volatile void *: 1, default: 0),
               "a void arm makes the result a qualified void pointer");
_Static_assert(_Generic(0 ? (int volatile *)0 : (void const *)0, const volatile void *: 1, default: 0),
               "a qualified void pointer is not a null pointer constant");
_Static_assert(_Generic(0 ? (int (*)[])0 : (int (*)[4])0, int (*)[4]: 1, default: 0),
               "an incomplete array type is completed by the other arm");
int main(void) { return 0; }
EOF
if "$compiler" -w -o "$tmp/condty" "$tmp/condty.c" > "$tmp/log" 2>&1; then
    echo "testing the conditional operator's pointer types ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the conditional operator's pointer types ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a string initializes a character array of any rank ----------------
# C11 6.7.9p14 gives a string literal to an array of character type, and gcc
# and clang read that as the innermost elements: `char m[2][3] = {"abc"}` fills
# the whole element in order. tinycc's tests2/90_struct-init.c writes
# `static char m1[][2][3] = {..., "abc"}`. cxx also takes the bare form
# `char x[2][3] = "abc"`, which both references refuse: a deliberate
# divergence, recorded in the plan.
cat > "$tmp/strarr.c" <<'EOF'
static char a[2][3] = {"abc"};
static char b[][2][3] = {{{1, 2, 3}, {4, 5, 6}}, {{7}, 8}, "xyz"};
static char c[2][2][2] = {"abcd"};
static char d[2][3] = {{"ab"}, {"cd"}};
int main(void) {
    if (a[0][0] != 'a' || a[0][2] != 'c' || a[1][0] != 0 || a[1][2] != 0) return 1;
    if (b[2][0][0] != 'x' || b[2][0][2] != 'z' || b[2][1][0] != 0) return 2;
    /* one innermost array per string */
    if (c[0][0][0] != 'a' || c[0][0][1] != 'b') return 3;
    if (c[0][1][0] != 0 || c[1][1][1] != 0) return 6;
    if (b[0][0][0] != 1 || b[1][1][0] != 8) return 4;
    if (d[1][1] != 'd' || d[1][2] != 0) return 5;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/strarr" "$tmp/strarr.c" > "$tmp/log" 2>&1 && "$tmp/strarr"; then
    echo "testing a string initializing a character array of any rank ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a string initializing a character array of any rank ... FAILED"
    sed 's/^/    /' "$tmp/log" "$tmp/log2" 2>/dev/null | head -4
    n_fail=$((n_fail + 1))
fi

# --- a cast that converts nothing, and a cast that drops qualifiers --
# `(struct S)s` where s already is a struct S is allowed (C says a cast needs
# a scalar type, but a conversion to the operand's own type is none), and a
# cast produces a value, so its top-level qualifiers are gone: `(float
# const)x` has type float, which is what a _Generic selector sees. tinycc's
# tests2/90_struct-init.c and 94_generic.c are both about this.
cat > "$tmp/castqual.c" <<'EOF'
struct S { int a, b; };
static struct S id(struct S s) { return (struct S)s; }
static int pick(void) {
    /* the qualifier is not part of the value's type */
    return _Generic((float const)1.0f, float: 1, default: 0);
}
int main(void) {
    struct S v = {1, 2};
    if (id(v).a != 1 || id(v).b != 2) return 1;
    if (pick() != 1) return 2;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/castqual" "$tmp/castqual.c" > "$tmp/log" 2>&1 && "$tmp/castqual"; then
    echo "testing a no-op cast and the qualifiers a cast drops ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a no-op cast and the qualifiers a cast drops ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- obsolete GNU field designators -----------------------------------
# `{a: 1}` is what gcc and clang still accept for `{.a = 1}` (each warns).
# tinycc's tests2/90_struct-init.c writes both spellings in one file.
cat > "$tmp/olddes.c" <<'EOF'
struct S { int a, b; int c[2]; };
struct S v = {a: 1, b: 2, c: {3, 4}};
struct S w = {.a = 5, .b = 6, .c = {7, 8}};
struct t { struct S s; int n; };
struct t u = {s: {a: 9}, n: 10};
int main(void) {
    if (v.a != 1 || v.b != 2 || v.c[0] != 3 || v.c[1] != 4) return 1;
    if (w.a != 5 || w.c[1] != 8) return 2;
    if (u.s.a != 9 || u.s.b != 0 || u.n != 10) return 3;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/olddes" "$tmp/olddes.c" > "$tmp/log" 2>&1 && "$tmp/olddes"; then
    echo "testing obsolete GNU field designators ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing obsolete GNU field designators ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- attributes in front of a type name -------------------------------
# `((ATTR int (*)(void))p)()` names its type with an attribute first, and a
# type name may open with one. tinycc's tests2/82_attribs_position.c has it.
cat > "$tmp/attrname.c" <<'EOF'
#define ATTR __attribute__((__noinline__))
static int actual(void) { return 42; }
int main(void) {
    void *p = (void *)&actual;
    int a = ((ATTR int (*)(void))p)();
    return a == 42 ? 0 : 1;
}
EOF
if "$compiler" -w -o "$tmp/attrname" "$tmp/attrname.c" > "$tmp/log" 2>&1 && "$tmp/attrname"; then
    echo "testing attributes in front of a type name ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing attributes in front of a type name ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a file-scope asm statement reaches the assembler verbatim --------
# LLVM doubles a dollar sign in an *inline* asm template ($ is its operand
# marker there), but a `module asm` string is the assembler's own text. cxx
# doubled it everywhere, so `movl $0x1234ABCD, %eax` arrived as
# `$$0x1234ABCD`: the assembler read a symbol name and the link failed on a
# relocation. tinycc's tests2/98_al_ax_extend.c is this statement.
cat > "$tmp/asmdecl.c" <<'EOF'
extern int write(int, void *, int);
asm(".text;"
    ".globl us;.globl ss;"
    "us:;ss:;"
    "movl $0x1234ABCD, %eax;"
    "ret;");
unsigned short us(void);
short ss(void);
int main(void) {
    char buf[32];
    int n = 0;
    unsigned v = us() + 1;
    for (int i = 28; i >= 0; i -= 4) buf[n++] = "0123456789ABCDEF"[(v >> i) & 15];
    buf[n++] = '\n';
    write(1, buf, n);
    /* the value is 0x1234ABCD + 1, truncated to the function's return type */
    return (unsigned short)(0x1234ABCD + 1) == (unsigned short)v ? 0 : 1;
}
EOF
if "$compiler" -w -o "$tmp/asmdecl" "$tmp/asmdecl.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/asmdecl" 2>/dev/null)" = "0000ABCE" ]; then
    echo "testing a file-scope asm statement with a dollar sign ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a file-scope asm statement with a dollar sign ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- __attribute__((constructor)) and ((destructor)) ------------------
# The platform calls these before `main` and after it returns, through the
# initializer arrays LLVM spells llvm.global_ctors / llvm.global_dtors. A
# lower priority runs first. tinycc's tests2/108_constructor.c uses the
# default priority; the ordering is what the argument is for.
cat > "$tmp/ctors.c" <<'EOF'
extern int write(int, void *, int);
static void out(const char *s, int n) { write(1, (void *)s, n); }
static void __attribute__((constructor(101))) first(void) { out("first ", 6); }
static void __attribute__((constructor)) second(void) { out("second ", 7); }
static void __attribute__((destructor)) last(void) { out("last\n", 5); }
int main(void) {
    out("main ", 5);
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/ctors" "$tmp/ctors.c" > "$tmp/log" 2>&1 &&
   [ "$("$tmp/ctors" 2>/dev/null)" = "first second main last" ]; then
    echo "testing constructor and destructor functions ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing constructor and destructor functions ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a qualifier on an array type qualifies the element ---------------
# C11 6.7.3p9, kept by C23: "If the specification of an array type includes
# any type qualifiers, the element type is so-qualified, not the array type."
# cxx qualified the array itself, so three spellings of one object looked like
# three types, and `restrict` in a specifier list was refused outright.
# tinycc's tests2/39_typedef.c and 100_c99array-decls.c are both about this.
cat > "$tmp/arrqual.c" <<'EOF'
typedef int A[3];
extern A const ca;
extern const A ca;
extern const int ca[3];
extern const int ca[3]; /* the same type, again */

typedef int *pa[2];
typedef restrict pa rpa; /* the pointers are the restrict-qualified ones */
typedef int *restrict rp;
typedef rp rparr[2];

_Static_assert(__builtin_types_compatible_p(rpa, rparr), "restrict reaches the element");
_Static_assert(__builtin_types_compatible_p(rpa, int *restrict[2]), "and the element is a pointer");
_Static_assert(!__builtin_types_compatible_p(int **, rp *), "a nested restrict is kept");
_Static_assert(sizeof(A) == 3 * sizeof(int), "the array is still three ints");

const int ca[3] = {1, 2, 3};
int main(void) {
    rpa v = {0, 0};
    if (ca[0] != 1 || ca[2] != 3) return 1;
    if (sizeof(v) != 2 * sizeof(int *)) return 2;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/arrqual" "$tmp/arrqual.c" > "$tmp/log" 2>&1 && "$tmp/arrqual"; then
    echo "testing a qualifier on an array type qualifying the element ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a qualifier on an array type qualifying the element ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- #pragma push_macro / pop_macro -----------------------------------
# C23 6.10.11. A push remembers what a name resolves to (including "nothing")
# and a pop puts it back, nesting included. tinycc's tests2/77_push_pop_macro.c
# checks exactly that, with the pragma names themselves defined as macros.
cat > "$tmp/pushmac.c" <<'EOF'
#define m 1
#define f(a) ((a) + 1)
int main(void) {
#pragma push_macro("m")
#undef m
#define m 2
#pragma push_macro("m")
#undef m
#define m 3
    if (m != 3) return 1;
#pragma pop_macro("m")
    if (m != 2) return 2;
#pragma pop_macro("m")
    if (m != 1) return 3;
    /* a name that was undefined at the push goes back to undefined */
#pragma push_macro("n")
#define n 7
    if (n != 7) return 4;
#pragma pop_macro("n")
#ifdef n
    return 5;
#endif
    /* a function-like macro comes back whole, parameters included */
#pragma push_macro("f")
#undef f
#define f(a) ((a) + 100)
    if (f(1) != 101) return 6;
#pragma pop_macro("f")
    if (f(1) != 2) return 7;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/pushmac" "$tmp/pushmac.c" > "$tmp/log" 2>&1 && "$tmp/pushmac"; then
    echo "testing #pragma push_macro / pop_macro ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing #pragma push_macro / pop_macro ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a bit-field store leaves its neighbours alone --------------------
# The clear mask is trimmed to the storage unit's width with `(1ULL <<
# total_bits) - 1`. For an eight-byte unit that shifts by 64, which the
# machine takes modulo 64: the expression is zero, the clear mask becomes
# zero, and a store wipes the whole unit instead of the field's bits. It cost
# tinycc's tests2/95_bitfields.c a `char` sitting next to a 38-bit field.
cat > "$tmp/bfneigh.c" <<'EOF'
struct s {
    long long x : 45;
    long long : 2;
    long long y : 30;
    unsigned long long z : 38;
    char a;
    short b;
};
int main(void) {
    struct s v;
    unsigned char *p = (unsigned char *)&v;
    for (unsigned i = 0; i < sizeof v; i++) p[i] = 0;
    /* a neighbour in the same eight-byte unit as z, and one outside it */
    v.a = -1;
    v.b = -1;
    v.z = 120;
    if (v.a != -1 || v.b != -1) return 1;
    /* the same for the wide fields at the front of the record */
    v.z = ~0ULL;
    if (v.a != -1 || v.b != -1) return 2;
    v.z = 0;
    if (v.a != -1 || v.b != -1) return 3;
    /* and setting all of them to -1 then to values, as the tinycc test does */
    v.x = -1, v.y = -1, v.z = -1, v.a = -1, v.b = -1;
    if (v.a != -1 || v.b != -1) return 4;
    v.x = 0x123456789ULL, v.y = 120 << 25, v.z = 120, v.a += 0x44, ++v.a, v.b = 0x77;
    if (v.a != 0x44 || v.b != 0x77) return 5;
    if (v.z != 120) return 6;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/bfneigh" "$tmp/bfneigh.c" > "$tmp/log" 2>&1 && "$tmp/bfneigh"; then
    echo "testing a bit-field store leaving its neighbours alone ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a bit-field store leaving its neighbours alone ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- bit-fields wider than four bytes ---------------------------------
# The masks a bit-field store uses are as wide as its storage unit. Computed
# as int32_t, a 45-bit field's clear mask became its low half -- zero -- and
# the store wiped the field it was meant to preserve: `s.x = ~0` wrote eight
# bytes of FF where gcc and clang write FF FF FF FF FF 1F.
cat > "$tmp/widebf.c" <<'EOF'
struct s {
    long long x : 45;
    long long : 2;
    long long y : 30;
    unsigned long long z : 38;
    char a;
    short b;
};
static int bytes_eq(void *p, int n, const unsigned char *want) {
    unsigned char *q = p;
    for (int i = 0; i < n; i++)
        if (q[i] != want[i]) return 0;
    return 1;
}
int main(void) {
    struct s v;
    unsigned char *p = (unsigned char *)&v;
    for (unsigned i = 0; i < sizeof v; i++) p[i] = 0;
    v.x = ~0ULL;
    {   /* the 45-bit field, and nothing after it */
        const unsigned char want[24] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x1F};
        if (!bytes_eq(&v, 24, want)) return 1;
    }
    for (unsigned i = 0; i < sizeof v; i++) p[i] = 0;
    v.y = ~0ULL;
    {   /* bits 47..76: four bytes at offset 8 */
        const unsigned char want[24] = {0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0x3F};
        if (!bytes_eq(&v, 24, want)) return 2;
    }
    for (unsigned i = 0; i < sizeof v; i++) p[i] = 0;
    v.z = ~0ULL;
    {   /* bits 79..114: offset 16, five bytes, 0x3F of the last */
        const unsigned char want[24] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                        0xFF, 0xFF, 0xFF, 0xFF, 0x3F};
        if (!bytes_eq(&v, 24, want)) return 3;
    }
    /* writing one field must leave its neighbours alone */
    for (unsigned i = 0; i < sizeof v; i++) p[i] = 0;
    v.x = ~0ULL;
    if (v.y != 0 || v.z != 0 || v.a != 0 || v.b != 0) return 4;
    for (unsigned i = 0; i < sizeof v; i++) p[i] = 0;
    v.z = ~0ULL;
    if (v.a != 0 || v.b != 0) return 5;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/widebf" "$tmp/widebf.c" > "$tmp/log" 2>&1 && "$tmp/widebf"; then
    echo "testing bit-fields wider than four bytes ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing bit-fields wider than four bytes ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- #pragma pack ------------------------------------------------------
# The other way real code asks for a packed record (tinycc's
# tests2/95_bitfields.c runs its whole packed half through it). It caps the
# alignment of every record declared after it, and pack(1) gives the packed
# layout -- no storage-unit boundary between bit-fields either.
cat > "$tmp/pack.c" <<'EOF'
#pragma pack(push, 1)
struct a { char c; int i; };
#pragma pack(pop)
struct b { char c; int i; };
#pragma pack(2)
struct c { char x; int i; };
#pragma pack()
struct d { char x; int i; };
#pragma pack(1)
struct e { unsigned x : 12; unsigned char y : 7; };
#pragma pack()
struct f { unsigned x : 12; unsigned char y : 7; };
int main(void) {
    if (sizeof(struct a) != 5 || _Alignof(struct a) != 1) return 1;
    if (sizeof(struct b) != 8 || _Alignof(struct b) != 4) return 2;
    if (sizeof(struct c) != 6 || _Alignof(struct c) != 2) return 3;
    if (sizeof(struct d) != 8 || _Alignof(struct d) != 4) return 4;
    /* twelve bits then seven, contiguous: three bytes */
    if (sizeof(struct e) != 3 || _Alignof(struct e) != 1) return 5;
    if (sizeof(struct f) != 4 || _Alignof(struct f) != 4) return 6;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/pack" "$tmp/pack.c" > "$tmp/log" 2>&1 && "$tmp/pack"; then
    echo "testing #pragma pack ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing #pragma pack ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- variadic aggregate arguments follow the register budget -----------
# An aggregate argument is passed in the registers only when every one of its
# eightbytes fits; the rest go to the overflow area, which the IR spells
# `byval`. cxx sent every one of them as its pieces, so the backend gave each
# piece its own stack slot instead of the argument's slot, and the callee's
# va_arg read the wrong bytes. tinycc's tests2/73_arm64.c passes six nine-byte
# structs through a variadic call; clang's own IR for it splits them into
# pieces (the ones that fit) and byval pointers (the rest).
cat > "$tmp/varagg.c" <<'EOF'
#include <stdarg.h>
struct s9 { char x[9]; };
static int check(int n, ...) {
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) {
        struct s9 v = va_arg(ap, struct s9);
        for (int j = 0; j < 9; j++)
            if (v.x[j] != (char)('A' + i)) return i * 16 + j + 1;
    }
    va_end(ap);
    return 0;
}
static struct s9 mk(char c) {
    struct s9 v;
    for (int j = 0; j < 9; j++) v.x[j] = c;
    return v;
}
int main(void) {
    /* one, two: register pieces; three, four, five: the overflow area */
    return check(5, mk('A'), mk('B'), mk('C'), mk('D'), mk('E'));
}
EOF
if "$compiler" -w -o "$tmp/varagg" "$tmp/varagg.c" > "$tmp/log" 2>&1 && "$tmp/varagg"; then
    echo "testing variadic aggregates across the register budget ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing variadic aggregates across the register budget ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a register piece is not stored past the object -------------------
# An aggregate argument travels as whole registers: `struct { char x[3]; }`
# as an i32 (four bytes for three), `struct { char x[11]; }` as an i64 and an
# i32 (twelve for eleven), `struct { char x[13]; }` as two i64s (sixteen for
# thirteen). The callee stores those pieces into the parameter's slot, and
# the bytes past the object belong to whatever the compiler put next to it --
# tinycc's tests2/73_arm64.c died with SIGILL because one of them was part of
# a saved return address.
cat > "$tmp/aggsz.c" <<'EOF'
#include <string.h>
struct s1 { char x[1]; };  struct s2 { char x[2]; };  struct s3 { char x[3]; };
struct s4 { char x[4]; };  struct s5 { char x[5]; };  struct s6 { char x[6]; };
struct s7 { char x[7]; };  struct s8 { char x[8]; };  struct s9 { char x[9]; };
struct s10 { char x[10]; }; struct s11 { char x[11]; }; struct s12 { char x[12]; };
struct s13 { char x[13]; }; struct s15 { char x[15]; }; struct s17 { char x[17]; };
static int c1(struct s1 a) { return a.x[0]; }
static int c2(struct s2 a) { return a.x[1]; }
static int c3(struct s3 a) { return a.x[2]; }
static int c4(struct s4 a) { return a.x[3]; }
static int c5(struct s5 a) { return a.x[4]; }
static int c6(struct s6 a) { return a.x[5]; }
static int c7(struct s7 a) { return a.x[6]; }
static int c8(struct s8 a) { return a.x[7]; }
static int c9(struct s9 a) { return a.x[8]; }
static int c10(struct s10 a) { return a.x[9]; }
static int c11(struct s11 a) { return a.x[10]; }
static int c12(struct s12 a) { return a.x[11]; }
static int c13(struct s13 a) { return a.x[12]; }
static int c15(struct s15 a) { return a.x[14]; }
static int c17(struct s17 a) { return a.x[16]; }
int main(void) {
    struct s1 v1;   struct s2 v2;   struct s3 v3;   struct s4 v4;   struct s5 v5;
    struct s6 v6;   struct s7 v7;   struct s8 v8;   struct s9 v9;   struct s10 v10;
    struct s11 v11; struct s12 v12; struct s13 v13; struct s15 v15; struct s17 v17;
    memset(&v1, 'a', sizeof v1);   memset(&v2, 'b', sizeof v2);
    memset(&v3, 'c', sizeof v3);   memset(&v4, 'd', sizeof v4);
    memset(&v5, 'e', sizeof v5);   memset(&v6, 'f', sizeof v6);
    memset(&v7, 'g', sizeof v7);   memset(&v8, 'h', sizeof v8);
    memset(&v9, 'i', sizeof v9);   memset(&v10, 'j', sizeof v10);
    memset(&v11, 'k', sizeof v11); memset(&v12, 'l', sizeof v12);
    memset(&v13, 'm', sizeof v13); memset(&v15, 'o', sizeof v15);
    memset(&v17, 'q', sizeof v17);
    /* every shape in one function: the callee's slots sit next to each
       other and to main's own locals, so an over-wide store shows up */
    if (c1(v1) != 'a') return 1;
    if (c2(v2) != 'b') return 2;
    if (c3(v3) != 'c') return 3;
    if (c4(v4) != 'd') return 4;
    if (c5(v5) != 'e') return 5;
    if (c6(v6) != 'f') return 6;
    if (c7(v7) != 'g') return 7;
    if (c8(v8) != 'h') return 8;
    if (c9(v9) != 'i') return 9;
    if (c10(v10) != 'j') return 10;
    if (c11(v11) != 'k') return 11;
    if (c12(v12) != 'l') return 12;
    if (c13(v13) != 'm') return 13;
    if (c15(v15) != 'o') return 14;
    if (c17(v17) != 'q') return 15;
    /* and the arguments themselves are still intact afterwards */
    if (v3.x[2] != 'c' || v9.x[8] != 'i' || v11.x[10] != 'k' || v13.x[12] != 'm') return 16;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/aggsz" "$tmp/aggsz.c" > "$tmp/log" 2>&1 && "$tmp/aggsz"; then
    echo "testing aggregate arguments of every register shape ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing aggregate arguments of every register shape ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- converting an integer to a pointer keeps its value ---------------
# LLVM's inttoptr zero-extends a narrower operand, so `(void *) -1` has to be
# widened first: gcc and clang make it all ones, and that is the value
# MAP_FAILED has and every mmap caller compares against. cxx used to
# zero-extend it, so the comparison said success and the program wrote
# through an unmapped address.
cat > "$tmp/ptrconv.c" <<'EOF'
int main(void) {
    void *a = (void *) -1;
    void *b = (void *) -1L;
    void *c = (void *) (int) -1;
    long l = -1;
    void *d = (void *) l;
    unsigned u = 0xFFFFFFFFu;
    void *e = (void *) u;
    if ((long)a != -1L) return 1;
    if ((long)b != -1L) return 2;
    if ((long)c != -1L) return 3;
    if ((long)d != -1L) return 4;
    if ((unsigned long)e != 0xFFFFFFFFUL) return 5;   /* unsigned stays unsigned */
    if (a == (void *)0) return 6;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/ptrconv" "$tmp/ptrconv.c" > "$tmp/log" 2>&1 && "$tmp/ptrconv"; then
    echo "testing an integer converted to a pointer ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing an integer converted to a pointer ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- a void lvalue has nothing to load --------------------------------
# `*pv` on a `void *` has type void (6.5.3.2p1) and may only appear where its
# value is discarded: `i ? *pv : *pv` is DR 106, and cxx emitted `load void`,
# which the backend refuses with "void type only allowed for function
# results".
ok "a void lvalue is not loaded" <<'EOF'
void tst(void *pv, int i) { i ? *pv : *pv; }
int main(void) { int x = 5; tst(&x, 1); tst(&x, 0); return x - 5; }
EOF

# --- a qualified return type loses its qualifier -----------------------
# `const int f(void)` returns int (6.7.6.3), and `int *restrict f(void)` a
# plain `int *`; the qualifier the pointer *points at* stays. gcc and clang
# only mention the drop under -Wextra, but the type is unqualified either
# way -- tinycc's tests2/150_return_qualifiers.c asserts exactly this.
ok "a function's return type is unqualified" <<'EOF'
const int cf(void);
volatile int vf(void);
int *restrict rf(void);
const int *pcf(void);
_Static_assert(__builtin_types_compatible_p(__typeof__(cf()) *, int *), "const");
_Static_assert(__builtin_types_compatible_p(__typeof__(vf()) *, int *), "volatile");
_Static_assert(__builtin_types_compatible_p(__typeof__(rf()) *, int **), "restrict");
_Static_assert(__builtin_types_compatible_p(__typeof__(pcf()) *, const int **), "pointee kept");
int main(void) { return 0; }
EOF

# --- one header, several spellings ------------------------------------
# `#pragma once` and the include-guard shortcut key on the *file*, not on the
# spelling that reached the compiler: "x.h", "./x.h" and "sub/../x.h" are one
# header, and tinycc's tests2/18_include.c includes its header exactly that
# way (cxx printed the header's output three times).
mkdir -p "$tmp/inc/sub"
cat > "$tmp/inc/once.h" <<'EOF'
#pragma once
extern int once_hits;
static inline void once_bump(void) { once_hits++; }
EOF
cat > "$tmp/inc/guard.h" <<'EOF'
#ifndef GUARD_H
#define GUARD_H
extern int guard_hits;
static inline void guard_bump(void) { guard_hits++; }
#endif
EOF
cat > "$tmp/inc/main.c" <<'EOF'
#include "once.h"
#include "./once.h"
#include "sub/../once.h"
#include "guard.h"
#include "./guard.h"
#include "sub/../guard.h"
int once_hits, guard_hits;
int main(void) {
    once_bump();
    guard_bump();
    return (once_hits == 1 && guard_hits == 1) ? 0 : 1;
}
EOF
if "$compiler" -w -o "$tmp/spellings" "$tmp/inc/main.c" > "$tmp/log" 2>&1 && "$tmp/spellings"; then
    echo "testing one header under three spellings ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing one header under three spellings ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- bit-fields -------------------------------------------------------
# A bit-field promotes by its width (6.3.1.1p2, "as restricted by the
# width"): a field whose range fits in int is an int whatever its declared
# type, which is what makes `u31 - 100` a signed comparison. gcc and clang
# agree case by case; cxx answered unsigned int for every unsigned field.
cat > "$tmp/bfpromo.c" <<'EOF'
struct S {
    unsigned u3 : 3, u31 : 31, u32 : 32;
    int s31 : 31, s32 : 32;
    unsigned long long ull31 : 31, ull33 : 33;
};
int main(void) {
    struct S s = {0};
    /* (x) - 100 < 0 is true only when x promoted to a signed type */
    if (!((s.u3 - 100 < 0))) return 1;
    if (!((s.u31 - 100 < 0))) return 2;
    if (s.u32 - 100 < 0) return 3;
    if (!((s.s31 - 100 < 0))) return 4;
    if (!((s.s32 - 100 < 0))) return 5;
    if (!((s.ull31 - 100 < 0))) return 6;
    if (s.ull33 - 100 < 0) return 7;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/bfpromo" "$tmp/bfpromo.c" > "$tmp/log" 2>&1 && "$tmp/bfpromo"; then
    echo "testing a bit-field promotes by its width ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a bit-field promotes by its width ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# And a store writes only the field's bits: everything above the width
# belongs to the next field, so `s.x = ~0u` on an `unsigned x : 12` must
# leave the four bits that `y : 7` lives in alone.
cat > "$tmp/bfstore.c" <<'EOF'
struct S { unsigned x : 12; unsigned char y : 7; unsigned z : 28; unsigned a : 4; unsigned b : 5; };
static int low(unsigned char *p, int n, unsigned char *out) { for (int i = 0; i < n; i++) out[i] = p[i]; return 0; }
int main(void) {
    struct S s;
    unsigned char *p = (unsigned char *)&s;
    for (unsigned i = 0; i < sizeof s; i++) p[i] = 0;
    s.x = ~0u;
    if (p[0] != 0xFF || p[1] != 0x0F) return 1;   /* 12 bits, not 16 */
    for (unsigned i = 0; i < sizeof s; i++) p[i] = 0;
    s.y = ~0u;
    /* y's byte-sized unit starts at bit 16: 00 00 7F, as gcc and clang have it */
    if (p[1] != 0x00 || p[2] != 0x7F) return 2;   /* 7 bits, not 8 */
    for (unsigned i = 0; i < sizeof s; i++) p[i] = 0;
    s.b = ~0u;
    if (p[8] != 0x1F) return 3;                   /* 5 bits, not 8 */
    /* and a store must not disturb its neighbour */
    for (unsigned i = 0; i < sizeof s; i++) p[i] = 0;
    s.x = ~0u;
    if (s.y != 0 || s.z != 0 || s.a != 0 || s.b != 0) return 4;
    /* the values still read back */
    for (unsigned i = 0; i < sizeof s; i++) p[i] = 0;
    s.x = 0x123; s.y = 0x45; s.z = 0x555555; s.a = 6; s.b = 7;
    if (s.x != 0x123 || s.y != 0x45 || s.z != 0x555555 || s.a != 6 || s.b != 7) return 5;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/bfstore" "$tmp/bfstore.c" > "$tmp/log" 2>&1 && "$tmp/bfstore"; then
    echo "testing a bit-field store writes only its bits ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a bit-field store writes only its bits ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- .S files ---------------------------------------------------------
# Assembler that the C preprocessor runs first: cpython ships
# Python/asm_trampoline_x86_64.S, which picks its symbols with
# `#ifdef __x86_64__`, and the driver used to answer `unknown file
# extension`.
cat > "$tmp/asm.S" <<'EOF'
#ifdef __x86_64__
    .globl cxxconf_asm_start
cxxconf_asm_start:
    mov $7, %eax
    ret
#else
#error "this check is for x86-64"
#endif
EOF
cat > "$tmp/asmuse.c" <<'EOF'
int cxxconf_asm_start(void);
int main(void) { return cxxconf_asm_start() == 7 ? 0 : 1; }
EOF
if "$compiler" -w -o "$tmp/asmuse" "$tmp/asmuse.c" "$tmp/asm.S" > "$tmp/log" 2>&1 && "$tmp/asmuse"; then
    echo "testing assembler that the preprocessor runs first ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing assembler that the preprocessor runs first ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- an attribute cxx has never heard of is a warning -----------------
# vector_size is one of them, and it has to stay compilable: glibc's own
# <link.h> writes `typedef float La_x86_64_xmm __attribute__
# ((__vector_size__ (16)))`, <execinfo.h> pulls it in, and a program that
# merely includes <link.h> must still build (making it an error cost
# cpython's Python/traceback.c and Modules/_ctypes/callproc.c).
ok "an attribute cxx does not know is a warning" <<'EOF'
typedef unsigned char v16 __attribute__((vector_size(16)));
typedef unsigned char v16u __attribute__((__vector_size__(16)));
int x __attribute__((frobnicate));
int main(void) { return sizeof(v16) + sizeof(v16u) + sizeof(x) > 0 ? 0 : 1; }
EOF

ok "glibc's <link.h> still compiles" <<'EOF'
#include <execinfo.h>
#include <link.h>
int main(void) { return 0; }
EOF

# --- machine flags ----------------------------------------------------
# -m... is the backend's: clang is what turns cxx's IR into instructions, so
# the flag is handed to it, and the macros that go with it are taken from
# clang (a header that takes an AVX2 path while clang was told -mno-avx2
# would generate the instructions the flag forbids). -m32 changes the target,
# which cxx's own type model does not have: it is refused rather than
# silently mis-compiled.
cat > "$tmp/mflags.c" <<'EOF'
int main(void) {
#ifdef __AVX2__
    return 0;
#else
    return 1;
#endif
}
EOF
if "$compiler" -w -mavx2 -o "$tmp/mflags" "$tmp/mflags.c" > "$tmp/log" 2>&1 && "$tmp/mflags"; then
    echo "testing -mavx2 asks the backend and defines the macro ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -mavx2 asks the backend and defines the macro ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

cat > "$tmp/mflags3.c" <<'EOF'
int main(void) {
#ifdef __AVX2__
    return 0;
#else
    return 1;
#endif
}
EOF
if "$compiler" -w -march=x86-64-v3 -o "$tmp/mflags3" "$tmp/mflags3.c" > "$tmp/log" 2>&1 && "$tmp/mflags3"; then
    echo "testing -march=x86-64-v3 carries its feature set ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -march=x86-64-v3 carries its feature set ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# A flag that asks for nothing new leaves the baseline alone: cxx does not
# advertise the x86 feature set by itself (see the amd64 target).
cat > "$tmp/mflags4.c" <<'EOF'
int main(void) {
#ifdef __AVX2__
    return 1;
#else
    return 0;
#endif
}
EOF
if "$compiler" -w -m64 -o "$tmp/mflags4" "$tmp/mflags4.c" > "$tmp/log" 2>&1 && "$tmp/mflags4"; then
    echo "testing a neutral machine flag changes nothing ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a neutral machine flag changes nothing ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

if "$compiler" -m32 -c -o "$tmp/mflags32.o" "$tmp/mflags.c" > "$tmp/log" 2>&1; then
    echo "testing -m32 is refused (no 32-bit x86 target) ... FAILED"
    n_fail=$((n_fail + 1))
else
    if grep -q 'no 32-bit x86 target' "$tmp/log"; then
        echo "testing -m32 is refused (no 32-bit x86 target) ... passed"
        n_pass=$((n_pass + 1))
    else
        echo "testing -m32 is refused (no 32-bit x86 target) ... FAILED"
        sed 's/^/    /' "$tmp/log" | head -4
        n_fail=$((n_fail + 1))
    fi
fi

# --- clang's operators in #if -----------------------------------------
# __has_extension() and __building_module() are clang's, and clang's own
# headers are written in terms of them: xmmintrin.h guards a block on
# `!__building_module(_Builtin_intrinsics)` and hresetintrin.h one on
# `__has_extension(gnu_asm)`. cxx answered "called object '0' is not a
# function", which is one unknown identifier followed by its argument list.
cat > "$tmp/hasext.c" <<'EOF'
#if __has_extension(gnu_asm)
#error "cxx implements no clang extension"
#endif
#if __building_module(_Builtin_intrinsics)
#error "cxx builds no module"
#endif
#if !__building_module(anything) && !__has_extension(anything)
int ok;
#endif
int main(void) { return 0; }
EOF
if "$compiler" -w -o "$tmp/hasext" "$tmp/hasext.c" > "$tmp/log" 2>&1 && "$tmp/hasext"; then
    echo "testing __has_extension and __building_module in #if ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing __has_extension and __building_module in #if ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- directives between macro arguments -------------------------------
# A conditional may sit between the arguments of a macro invocation. gcc
# and clang evaluate it there and the argument goes on; cxx used to cut the
# invocation at the directive, and the collector then ran off the end of
# the file looking for the `)` that closes it (cpython's
# Python/jit_unwind.c writes its DWARF CIE and FDE through
# DWRF_SECTION(name, ... #ifdef __x86_64__ ... #endif ...)).
cat > "$tmp/macrocond.c" <<'EOF'
#define SET(v, body) do { v += 1; body; } while (0)
int main(void) {
    int x = 0;
    SET(x,
        x += 10;
#if 1
        x += 100;
#  if 0
        x += 1000;
#  else
        x += 20;
#  endif
#elif 0
        x += 10000;
#else
#    error "the branch that is not taken is not part of the argument"
#endif
        x += 200;
    );
    return x == 331 ? 0 : 1;
}
EOF
if "$compiler" -w -o "$tmp/macrocond" "$tmp/macrocond.c" > "$tmp/log" 2>&1 && "$tmp/macrocond"; then
    echo "testing a conditional between macro arguments ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a conditional between macro arguments ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# `__alignof__(object)` is the alignment of the object. An `aligned`
# attribute or `_Alignas` raises it above the type's -- cpython's
# Py_ALIGNED(64) buffer is asserted to be 64-aligned
# (Modules/_testcapimodule.c), and cxx answered with the type's 1.
cat > "$tmp/alignof.c" <<'EOF'
__attribute__((aligned(64))) char global_buf[4];
int main(void) {
    _Alignas(64) char buf[4];
    __attribute__((aligned(32))) int x;
    if (__alignof__(buf) < 64) return 1;
    if (__alignof__(x) < 32) return 2;
    if (__alignof__(global_buf) < 64) return 3;
    if (((unsigned long)buf % 64) != 0) return 4;
    if (((unsigned long)&global_buf % 64) != 0) return 5;
    /* a plain object still reports its type's alignment */
    char plain[4];
    if (__alignof__(plain) != 1) return 6;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/alignof" "$tmp/alignof.c" > "$tmp/log" 2>&1 && "$tmp/alignof"; then
    echo "testing __alignof__ of an over-aligned object ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing __alignof__ of an over-aligned object ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- the memory builtins ---------------------------------------------
# __builtin_memset and its siblings are the library functions of the same
# name. They are builtins here (so __has_builtin answers for them and an
# address can be taken), and the header that declares the library function
# is not needed to call one -- glibc's CPU_ZERO_S writes
# `__builtin_memset (cpusetp, '\0', __size)`, which was an implicit
# declaration of `__builtin_memset` (cpython's Modules/posixmodule.c).
cat > "$tmp/membuiltin.c" <<'EOF'
#if !__has_builtin(__builtin_memset) || !__has_builtin(__builtin_memcpy) || \
    !__has_builtin(__builtin_memmove) || !__has_builtin(__builtin_memcmp)
#error a memory builtin is missing
#endif
int main(void) {
    char a[8], b[8];
    __builtin_memset(a, 'a', 7);
    a[7] = 0;
    __builtin_memcpy(b, a, 8);
    __builtin_memmove(b + 1, b, 3);
    if (b[7] != 0 || b[1] != 'a') return 1;
    if (__builtin_memcmp(b, "aaaa", 4) != 0) return 2;
    if (__builtin_memcmp(b, "aaab", 4) >= 0) return 3;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/membuiltin" "$tmp/membuiltin.c" > "$tmp/log" 2>&1 && "$tmp/membuiltin"; then
    echo "testing the memory builtins ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing the memory builtins ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- parenthesized string initializers -------------------------------
# `static const char name[] = (PREFIX "name");` is how cpython's
# Modules/_testsinglephase.c writes one, and both references read the
# parentheses as if they were not there -- including around a wide literal,
# whose `const` sits on the array's element and used to be compared as a
# difference in type.
cat > "$tmp/parenstr.c" <<'EOF'
#include <stddef.h>
#include <uchar.h>
static const char a[] = ("hello");
static const char b[] = (("hi"));
static const char c[] = ("ab" "cd");
static const wchar_t d[] = (L"wide");
static const char16_t e[] = u"u16";
int main(void) {
    return (a[0] == 'h' && b[0] == 'h' && c[2] == 'c' && d[0] == L'w' && e[0] == u'u') ? 0 : 1;
}
EOF
if "$compiler" -w -o "$tmp/parenstr" "$tmp/parenstr.c" > "$tmp/log" 2>&1 && "$tmp/parenstr"; then
    echo "testing a parenthesized string initializer ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing a parenthesized string initializer ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- the ISO/GNU mode switch -----------------------------------------
# An ISO mode defines __STRICT_ANSI__, a GNU one does not. Headers and
# projects ask it whether GNU extensions are in play -- cpython's
# Py_ARRAY_LENGTH() puts Py_BUILD_ASSERT_EXPR(), a comma expression, into
# the array bound only when it is *not* defined, and a comma expression is
# not an integer constant expression, so a constant bound came out
# variably modified (Objects/typeobject.c).
cat > "$tmp/strict.c" <<'EOF'
#ifndef __STRICT_ANSI__
#error __STRICT_ANSI__ is not defined in an ISO mode
#endif
int iso_mode;
EOF
cat > "$tmp/gnumode.c" <<'EOF'
#ifdef __STRICT_ANSI__
#error __STRICT_ANSI__ is defined in a GNU mode
#endif
int gnu_mode;
EOF
if "$compiler" -std=c11 -w -c -o /dev/null "$tmp/strict.c" > "$tmp/log" 2>&1 &&
   "$compiler" -std=gnu11 -w -c -o /dev/null "$tmp/gnumode.c" >> "$tmp/log" 2>&1; then
    echo "testing -std=c11 is strict and -std=gnu11 is not ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing -std=c11 is strict and -std=gnu11 is not ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# __inline and __inline__ are the GNU spellings of `inline`, and gcc and
# clang make them keywords rather than macros. As a macro the spelling is
# re-expandable: expat's internal.h defines `inline` as `__inline`, so the
# expansion came back as a plain identifier and byteswap.h's
# `static __inline __uint16_t` no longer had a function specifier
# (cpython's Modules/expat/xmltok.c and xmlrole.c).
cat > "$tmp/inlkw.c" <<'EOF'
#ifdef __inline__
#error __inline__ is a macro
#endif
#define inline __inline
static inline unsigned short swap(unsigned short x) { return x; }
int main(void) { return swap(1) - 1; }
EOF
if "$compiler" -w -o "$tmp/inlkw" "$tmp/inlkw.c" > "$tmp/log" 2>&1 && "$tmp/inlkw"; then
    echo "testing __inline is a keyword, not a macro ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing __inline is a keyword, not a macro ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# --- more variable length arrays -------------------------------------
# `sizeof` and `_Countof` of an object read the counter its declaration
# wrote; there is no bound of their own to evaluate, and building the
# expression around a null operand took the compiler down. The bound
# `((void)sizeof(int), 4)` is the constant 4: the cast throws the value
# away, and clang reads it the same way.
cat > "$tmp/vlasz.c" <<'EOF'
int main(void) {
    int n = 3;
    int a[n];
    if (sizeof(a) != 3 * sizeof(int)) return 1;
    if (sizeof(int[n]) != 3 * sizeof(int)) return 2;
    if (_Countof(a) != 3) return 3;
    int b[((void)sizeof(int), 4)];
    if (sizeof(b) != 4 * sizeof(int)) return 4;
    return 0;
}
EOF
if "$compiler" -w -o "$tmp/vlasz" "$tmp/vlasz.c" > "$tmp/log" 2>&1 && "$tmp/vlasz"; then
    echo "testing sizeof and _Countof of a variable length object ... passed"
    n_pass=$((n_pass + 1))
else
    echo "testing sizeof and _Countof of a variable length object ... FAILED"
    sed 's/^/    /' "$tmp/log" | head -4
    n_fail=$((n_fail + 1))
fi

# 6.7.6.2p2: a variably modified type belongs to a block scope. Taking such
# a bound as a constant emitted the object with no type at all, which only
# the backend noticed.
bad "variably modified type at file scope" <<'EOF'
int n;
int a[n];
EOF

# --- summary ---------------------------------------------------------
echo
if [ $n_fail -eq 0 ]; then
    echo "conformance: $n_pass passed, $n_gaps known gap(s)"
    exit 0
fi
echo "conformance: $n_pass passed, $n_fail FAILED, $n_gaps known gap(s)"
exit 1
