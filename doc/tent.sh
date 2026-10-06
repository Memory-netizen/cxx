#!/bin/bash
# 6.9.2p2: what a file-scope array with no length becomes, and the three
# neighbouring incomplete-array shapes that used to reach LLVM as `[-1 x T]`
# (clang's backend rejects that with "expected number in address space").
C=${1:-/home/memory/cxx/cxx}
t=`mktemp -d /tmp/cxx-tent-XXXXXX`
trap 'rm -rf $t' EXIT

row() { # row <name> ; source on stdin
    cat > "$t/$1.c"
    printf '%-28s' "$1"
    for cc in gcc clang "$C"; do
        if $cc -std=c23 -w -S -o /dev/null "$t/$1.c" > "$t/e" 2>&1; then v=ok; else v=ERR; fi
        printf '  %-5s %s' "$(basename "$cc")" "$v"
    done
    printf '   cxx: '
    "$C" -S -o /dev/null "$t/$1.c" 2>&1 | grep -E 'error:|warning:' | head -1 | sed 's/.*: //' | cut -c1-64
    [ -z "$("$C" -S -o /dev/null "$t/$1.c" 2>&1)" ] && echo "   cxx: -"
}

echo "=== the shapes"
row tentative_no_length <<'EOF'
int arr[];
int main(void) { return 0; }
EOF
row tentative_static <<'EOF'
static int arr[];
int main(void) { return 0; }
EOF
row defined_here <<'EOF'
int arr[] = {1, 2, 3};
int main(void) { return 0; }
EOF
row completed_later <<'EOF'
int arr[];
int arr[10];
int main(void) { return 0; }
EOF
row extern_declaration <<'EOF'
extern int arr[];
int main(void) { return 0; }
EOF
row block_definition <<'EOF'
int main(void) { int arr[]; return 0; }
EOF
row block_static <<'EOF'
int main(void) { static int arr[]; return 0; }
EOF
row block_extern <<'EOF'
int main(void) { extern int arr[]; return 0; }
EOF
row incomplete_element <<'EOF'
struct S;
struct S arr[];
int main(void) { return 0; }
EOF

echo
echo "=== the emitted object (the point of the exercise)"
printf 'int arr[];\n' > "$t/one.c"
printf 'extern int arr[];\nint *q = arr;\n' > "$t/two.c"
for f in one two; do
    printf '  %-6s cxx:   ' "$f"
    "$C" -w -S -emit-llvm -o - "$t/$f.c" 2>/dev/null | grep -E '^@arr'
    printf '  %-6s clang: ' "$f"
    clang -std=c23 -w -S -emit-llvm -o - "$t/$f.c" 2>/dev/null | grep -E '^@arr'
done

echo
echo "=== one element, proved by a second unit that says arr[1]"
cat > "$t/def.c" <<'EOF'
int arr[];
int *p = arr;
EOF
cat > "$t/use.c" <<'EOF'
extern int arr[1];
int main(void) { arr[0] = 7; return arr[0] - 7; }
EOF
for cc in gcc clang "$C"; do
    printf '  %-6s ' "$(basename "$cc")"
    if $cc -std=c23 -w -o "$t/two" "$t/def.c" "$t/use.c" >/dev/null 2>&1 && "$t/two"; then echo "linked and ran"; else echo "FAILED"; fi
done

echo
echo "=== the diagnostic"
cat > "$t/warn.c" <<'EOF'
int arr[];
int done[] = {1, 2, 3};
int sized[4];
int late[];
int late[10];
EOF
printf '  gcc:   '; gcc -std=c23 -c -o /dev/null "$t/warn.c" 2>&1 | grep -c 'assumed to have one element' | sed 's/$/ warning(s)/'
printf '  clang: '; clang -std=c23 -c -o /dev/null "$t/warn.c" 2>&1 | grep -c 'assumed to have one element' | sed 's/$/ warning(s)/'
printf '  cxx:   '; "$C" -S -o /dev/null "$t/warn.c" 2>&1 | grep -c 'assumed to have one element' | sed 's/$/ warning(s)/'
printf '  cxx -Wno-tentative-definition-array: '
"$C" -Wno-tentative-definition-array -S -o /dev/null "$t/warn.c" 2>&1 | grep -c 'assumed to have one element' | sed 's/$/ warning(s)/'
