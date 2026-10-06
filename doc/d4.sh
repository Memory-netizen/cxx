#!/bin/bash
# D4: -funsigned-char / -fsigned-char.
C=/home/memory/cxx/cxx
CC=clang
t=`mktemp -d /tmp/cxx-d4-XXXXXX`
trap 'rm -rf $t' EXIT

cat > "$t/a.c" <<'EOF'
#include <stdio.h>
int main(void) {
    char c = -1;
    signed char s = -1;
    unsigned char u = 255;
    printf("char=%d signed=%d unsigned=%d  (char)-1==255:%d  c<0:%d\n",
           (int)c, (int)s, (int)u, (char)-1 == 255, c < 0);
    printf("sizeof char=%zu  _Generic: %d\n", sizeof(char),
           _Generic(c, char: 1, signed char: 2, unsigned char: 3));
    return 0;
}
EOF

show() { # show <label> <compiler> <args...>
    local label=$1 comp=$2; shift 2
    printf '%-34s ' "$label"
    "$comp" "$@" -o "$t/x" "$t/a.c" > "$t/o" 2>&1 && "$t/x" || { echo "FAILED"; head -2 "$t/o"; }
}

echo "=== cxx (amd64: the ABI default is signed char)"
show "default"                 "$C"
show "-fsigned-char"           "$C" -fsigned-char
show "-funsigned-char"         "$C" -funsigned-char
show "-funsigned then -fsigned" "$C" -funsigned-char -fsigned-char

echo
echo "=== clang, for the same three"
show "default"                 "$CC" -std=c2y
show "-fsigned-char"           "$CC" -std=c2y -fsigned-char
show "-funsigned-char"         "$CC" -std=c2y -funsigned-char

echo
echo "=== the switch must reach the whole compiler, not just one expression"
cat > "$t/b.c" <<'EOF'
#include <stdio.h>
#include <string.h>
static int is_neg(char c) { return c < 0; }
int main(void) {
    char buf[4];
    memset(buf, 0xFF, 4);
    /* char arithmetic, a call boundary, and a char in a struct */
    struct S { char c; } s;
    s.c = -1;
    printf("%d %d %d %d\n", is_neg(-1), (int)buf[0], s.c < 0, 'a' + 200 > 0 ? 1 : 0);
    return 0;
}
EOF
for f in "" "-funsigned-char"; do
    printf '  cxx %-18s ' "[$f]"; $C $f -o "$t/y" "$t/b.c" 2>&1 | head -2 && "$t/y"
done
for f in "" "-funsigned-char"; do
    printf '  clang %-16s ' "[$f]"; $CC -std=c2y $f -o "$t/yc" "$t/b.c" 2>/dev/null && "$t/yc"
done
