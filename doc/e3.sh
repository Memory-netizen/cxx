#!/bin/bash
# E3: -Wunused-variable.
C=/home/memory/cxx/cxx
CC=clang
t=`mktemp -d /tmp/cxx-e3-XXXXXX`
trap 'rm -rf $t' EXIT

cat > "$t/a.c" <<'EOF'
int used(int x) { int a = x; return a; }
int unused(int x) { int a = 1; return x; }
int unused2(void) { int a = 1, b = 2; return 0; }
int maybe(void) { [[maybe_unused]] int a = 1; return 0; }
int attr(void) { int a __attribute__((unused)) = 1; return 0; }
int param_only(int p) { return 0; }
int scope(void) { { int inner = 1; } return 0; }
int assigned(void) { int a; a = 1; return 0; }
static int s_unused(void) { static int q = 1; return 0; }
EOF

count() { # count <flag...>  (the flag is the label; "" means default)
    local label="$*"
    "$C" "$@" -S -o /dev/null "$t/a.c" > "$t/o" 2>&1
    printf '%-34s unused-warnings=%d  %s\n' "${label:-<default>}" \
        "$(grep -c 'unused variable' "$t/o")" \
        "$(grep -oE "unused variable .[a-z0-9_]+." "$t/o" | tr '\n' ' ')"
}

echo "=== cxx"
count -w
count
count -Wno-unused-variable
count -Wunused-variable
count -Wall
count -Werror

echo
echo "=== clang, for the same file (which names are reported)"
$CC -std=c2y -w -Wunused-variable -S -o /dev/null "$t/a.c" 2>&1 | grep -oE "unused variable '[a-z0-9_]+'" | sort | tr '\n' ' '; echo

echo
echo "=== a program whose locals are all used must stay silent"
cat > "$t/b.c" <<'EOF'
#include <stdio.h>
int main(void) {
    int a = 1, b = 2, c = a + b;
    char buf[8];
    for (int i = 0; i < 3; i++) buf[i] = (char)('0' + i);
    printf("%d %s\n", c, buf);
    return 0;
}
EOF
"$C" -w -o "$t/b" "$t/b.c" 2>&1 | head -3 && "$t/b"
printf 'exit=%d\n' "$?"
