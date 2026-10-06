#!/bin/bash
# For every warning site in cxx, build a program that triggers the equivalent
# diagnostic in gcc and clang, and read the group each of them reports. The
# [-W...] tag is the authority: the group names in cxx's table were chosen by
# hand and several were guesses.
#
# cxx's own group is determined empirically too: -Wno-<group> is tried for
# each name in the table and the one that makes the warning disappear is the
# group it belongs to.
CXX=/home/memory/cxx/cxx
t=`mktemp -d /tmp/cxx-wgrp-XXXXXX`
trap 'rm -rf $t' EXIT

GROUPS="deprecated-declarations unused-result attributes return-type cpp atomic-memory-ordering unused-variable implicit-function-declaration"

cxx_group() { # cxx_group <file> -> prints the group that silences it
    local file=$1 base n
    base=$($CXX -S -o /dev/null "$file" 2>&1 | grep -c 'warning:')
    if [ "$base" -eq 0 ]; then echo "(cxx is silent)"; return; fi
    for g in $GROUPS; do
        n=$($CXX -Wno-$g -S -o /dev/null "$file" 2>&1 | grep -c 'warning:')
        if [ "$n" -lt "$base" ]; then echo "-W$g"; return; fi
    done
    echo "(WG_DEFAULT: only -w silences it)"
}

tag() { # tag <compiler> <std> <file>
    "$1" "$2" -c -o /dev/null "$3" 2>&1 | grep -oE '\[-W[a-zA-Z0-9=_-]+\]' | head -1 | tr -d '[]'
}

probe() { # probe <name> ; source on stdin
    local name=$1
    cat > "$t/$name.c"
    printf '%-22s cxx=%-38s gcc=%-34s clang=%s\n' \
        "$name" "$(cxx_group "$t/$name.c")" \
        "$(tag gcc -std=c23 "$t/$name.c")" \
        "$(tag clang -std=c2y "$t/$name.c")"
}

echo "### parser.c sites"

probe atomic-order <<'EOF'
#include <stdatomic.h>
int main(void) { _Atomic int v; return atomic_load_explicit(&v, memory_order_release); }
EOF

probe deprecated <<'EOF'
__attribute__((deprecated)) void f(void);
int main(void) { f(); return 0; }
EOF

probe nodiscard <<'EOF'
[[nodiscard]] int f(void);
int main(void) { f(); return 0; }
EOF

probe noreturn-returns <<'EOF'
_Noreturn void f(void) { return; }
int main(void) { return 0; }
EOF

probe empty-case-range <<'EOF'
int f(int c) { switch (c) { case 9 ... 1: return 1; default: return 0; } }
int main(void) { return f(5); }
EOF

probe nothing-declared <<'EOF'
struct S { int a; };
int main(void) { struct S; return 0; }
EOF

probe unknown-attribute <<'EOF'
int f(int x) __attribute__((bogus_attribute));
int main(void) { return f(1); }
EOF

probe nodiscard-on-object <<'EOF'
[[nodiscard]] int x;
int main(void) { return x; }
EOF

probe unused-variable <<'EOF'
int main(void) { int a = 1; return 0; }
EOF

echo
echo "### preprocess.c sites"

probe hash-warning <<'EOF'
#warning hello
int main(void) { return 0; }
EOF

probe redefine-builtin-macro <<'EOF'
#define __LINE__ 1
int main(void) { return __LINE__; }
EOF

probe undef-builtin-macro <<'EOF'
#undef __LINE__
int main(void) { return 0; }
EOF

probe macro-name-whitespace <<'EOF'
#define f (1)
int main(void) { return f; }
EOF

probe extra-token <<'EOF'
#ifdef X
#endif X
int main(void) { return 0; }
EOF
