#!/bin/bash
# 6.7.5p8: which `inline` definitions actually define the symbol, and which
# translation units link on their own. One shape per file, three compilers.
#
#   symbol: does the object define `f`?      link: does the file link alone?
#
# The point of the table is the first column: in C a definition whose every
# file scope declaration is `inline` without `extern` is an *inline
# definition*, which is not an external definition at all.
C=${1:-/home/memory/cxx/cxx}
t=`mktemp -d /tmp/cxx-inline-XXXXXX`
trap 'rm -rf $t' EXIT

# symbol <cc> <file> <obj>
symbol() {
    if [ "$1" = "$C" ]; then
        "$1" -w -S -emit-llvm -o "$3.ll" "$2" 2>/dev/null || return 1
        grep -qE '^define [^@]*@f\(' "$3.ll" && echo define || echo -
    else
        "$1" -std=c23 -w -c -o "$3.o" "$2" 2>/dev/null || return 1
        # t as well as T: a static inline function is defined in the object
        # too, only locally.
        nm "$3.o" 2>/dev/null | grep -qE ' [Tt] f$' && echo define || echo -
    fi
}

row() { # row <name> ; source on stdin
    local name=$1
    cat > "$t/$name.c"
    printf '%-30s' "$name"
    for cc in gcc clang "$C"; do
        local label=$(basename "$cc")
        local sym
        sym=$(symbol "$cc" "$t/$name.c" "$t/$name" 2>/dev/null) || sym=REJECT
        if "$cc" -w -o "$t/$name.bin" "$t/$name.c" >/dev/null 2>&1; then lnk=link; else lnk=NO-LINK; fi
        printf '  %-5s %-7s %-7s' "$label" "$sym" "$lnk"
    done
    echo
}

echo "shape                          compiler symbol  link"
row inline_definition <<'EOF'
inline int f(void) { return 1; }
int main(void) { return f() - 1; }
EOF
row extern_inline <<'EOF'
extern inline int f(void) { return 1; }
int main(void) { return f() - 1; }
EOF
row extern_decl_after <<'EOF'
inline int f(void) { return 1; }
extern int f(void);
int main(void) { return f() - 1; }
EOF
row plain_decl_first <<'EOF'
int f(void);
inline int f(void) { return 1; }
int main(void) { return f() - 1; }
EOF
row static_inline <<'EOF'
static inline int f(void) { return 1; }
int main(void) { return f() - 1; }
EOF
row inline_decl_only <<'EOF'
inline int f(void);
int main(void) { return 0; }
EOF
row extern_inline_decl_only <<'EOF'
extern inline int f(void);
int main(void) { return 0; }
EOF
row static_inline_decl_only <<'EOF'
static inline int f(void);
int main(void) { return 0; }
EOF
row address_taken <<'EOF'
inline int f(void) { return 1; }
int (*p)(void) = f;
int main(void) { return p == 0; }
EOF
row called_from_this_tu <<'EOF'
inline int f(void) { return 1; }
int g(void) { return f(); }
EOF
row static_local_inside <<'EOF'
inline int f(void) { static int q = 1; return q; }
int main(void) { return f() - 1; }
EOF
row static_const_local_inside <<'EOF'
inline int f(void) { static const int q = 1; return q; }
int main(void) { return f() - 1; }
EOF

echo
echo "=== the two-unit idiom: an inline definition here, the external one there"
cat > "$t/use.c" <<'EOF'
#include <stdio.h>
inline int twice(int x) { return x + x; }
int main(void) { printf("%d\n", twice(21)); return twice(21) == 42 ? 0 : 1; }
EOF
cat > "$t/def.c" <<'EOF'
extern inline int twice(int x) { return x + x; }
EOF
for cc in gcc clang "$C"; do
    printf '%-8s ' "$(basename "$cc")"
    if "$cc" -w -o "$t/two" "$t/use.c" "$t/def.c" >/dev/null 2>&1; then "$t/two"; else echo "link failed"; fi
done

echo
echo "=== the diagnostics (6.7.5p5 and p3)"
cat > "$t/warn.c" <<'EOF'
inline int f(void) { static int q = 1; return q; }
inline int g(void);
int main(void) { return f() - 1; }
EOF
for cc in gcc clang "$C"; do
    printf '%-8s\n' "$(basename "$cc")"
    "$cc" -std=c23 -S -o /dev/null "$t/warn.c" 2>&1 | grep -E 'warning:' | sed 's/^/     /'
    "$C" -S -o /dev/null "$t/warn.c" 2>&1 | grep -E 'warning:' | sed 's/^/     /' | head -0
done
printf 'cxx      (both, cxx wording above; -Wno-static-local-in-inline silences one)\n'
"$C" -S -o /dev/null "$t/warn.c" 2>&1 | grep -E 'warning:' | sed 's/^/     /'
