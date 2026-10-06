#!/bin/bash
# Diagnostic coverage status (the counts that the previous script mangled).
C=/home/memory/cxx/cxx
t=`mktemp -d /tmp/cxx-dx2-XXXXXX`
trap 'rm -rf $t' EXIT
cat > "$t/dep.c" <<'EOF'
__attribute__((deprecated)) void old_fn(void);
int main(void) { old_fn(); return 0; }
EOF
cat > "$t/unused.c" <<'EOF'
int main(void) { int unused_var = 1; return 0; }
EOF
cat > "$t/fall.c" <<'EOF'
int f(int x) { switch (x) { case 1: x++; case 2: return x; } return 0; }
EOF
cat > "$t/dep2.c" <<'EOF'
struct [[deprecated]] S { int a; };
int main(void) { struct S s = {1}; return s.a - 1; }
EOF

n() { # n <label> <pattern> <command...>
    local label=$1 pat=$2; shift 2
    "$@" >"$t/o" 2>&1
    printf '%-42s %s\n' "$label" "$(grep -c -- "$pat" "$t/o")"
}
n "deprecated, no flag"        deprecated $C -S -o /dev/null "$t/dep.c"
n "deprecated, -Wno-deprecated-declarations" deprecated $C -Wno-deprecated-declarations -S -o /dev/null "$t/dep.c"
n "deprecated, -w"             deprecated $C -w -S -o /dev/null "$t/dep.c"
n "deprecated, -Wno-bogus"     deprecated $C -Wno-bogus-option -S -o /dev/null "$t/dep.c"
n "unused variable, -Wall"     unused     $C -Wall -S -o /dev/null "$t/unused.c"
n "unused variable, -Wunused-variable" unused $C -Wunused-variable -S -o /dev/null "$t/unused.c"
n "implicit fallthrough, -Wall" fallthrough $C -Wall -S -o /dev/null "$t/fall.c"
n "deprecated TYPE use, -Wall" deprecated $C -Wall -S -o /dev/null "$t/dep2.c"
echo "(counts are matching lines in the compiler's output)"
