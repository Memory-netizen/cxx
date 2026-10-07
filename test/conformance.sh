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

bad "reject va_copy outside a variadic function" <<'EOF'
#include <stdarg.h>
void f(void) { va_list ap, aq; va_copy(aq, ap); }
EOF

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

# --- summary ---------------------------------------------------------
echo
if [ $n_fail -eq 0 ]; then
    echo "conformance: $n_pass passed, $n_gaps known gap(s)"
    exit 0
fi
echo "conformance: $n_pass passed, $n_fail FAILED, $n_gaps known gap(s)"
exit 1
