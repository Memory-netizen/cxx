#!/bin/bash
# A variably modified parameter: gcc, clang and cxx side by side.
#
# Usage: bash doc/vlaparam.sh ./cxx
#
# Every shape is a whole program, and the three compilers' exit status has to
# agree: the value an inner dimension produces is what a wrong or missing bound
# changes first.
set -u
C=${1:-./cxx}
tmp=`mktemp -d /tmp/cxx-vlaparam-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

pass=0
fail=0

shape() { # shape <name> <expected "exit:stdout">
    name=$1
    cat > "$tmp/$name.c"
    ok=1
    ref=""
    line=""
    for cc in gcc clang "$C"; do
        bin="$tmp/$name.bin"
        if ! $cc -std=c23 -w -o "$bin" "$tmp/$name.c" > "$tmp/$name.err" 2>&1; then
            got="refused"
        else
            out=$("$bin" 2>&1)
            got="$?:$out"
        fi
        [ -z "$ref" ] && ref="$got"
        [ "$got" != "$ref" ] && ok=0
        line="$line $cc=[$got]"
    done
    if [ $ok -eq 1 ] && [ "$ref" != "$2" ]; then
        ok=0
        line="$line (expected [$2])"
    fi
    if [ $ok -eq 1 ]; then
        printf '  ok        %-18s %s\n' "$name" "$ref"
        pass=$((pass + 1))
    else
        printf '  MISMATCH  %-18s%s\n' "$name" "$line"
        fail=$((fail + 1))
    fi
}

echo "== a parameter bound may name an earlier parameter (6.2.1p7)"
shape one_dim "9:" <<'EOF'
static int f(int n, int a[n]) { return a[n - 1]; }
int main(void) { int a[3] = {1, 2, 9}; return f(3, a); }
EOF

shape two_bounds "9:" <<'EOF'
static int f(int n, int m, int a[n][m]) { return a[n - 1][m - 1]; }
int main(void) { int a[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 9}}; return f(2, 4, a); }
EOF

shape bound_expression "5:" <<'EOF'
static int f(int n, int a[n * 2]) { return a[n]; }
int main(void) { int a[8] = {1, 2, 3, 4, 5, 6, 7, 9}; return f(4, a); }
EOF

shape two_vla_parameters "9:" <<'EOF'
static int f(int n, int a[n], int b[n + 1]) { return a[n - 1] + b[n]; }
int main(void) { int a[3] = {1, 2, 4}, b[4] = {1, 1, 1, 5}; return f(3, a, b); }
EOF

shape inner_sizeof "16:" <<'EOF'
static int f(int m, int a[2][m]) { return (int)sizeof a[0]; }
int main(void) { int a[2][4]; return f(4, a); }
EOF

shape inner_countof "4:" <<'EOF'
static int f(int m, int a[2][m]) { return (int)_Countof(a[0]); }
int main(void) { int a[2][4]; return f(4, a); }
EOF

shape static_bound "8:" <<'EOF'
static int f(int n, int a[static n]) { return a[n - 1]; }
int main(void) { int a[3] = {1, 2, 8}; return f(3, a); }
EOF

shape global_bound "9:" <<'EOF'
static int m = 4;
static int f(int a[2][m]) { return a[1][3]; }
int main(void) { int a[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 9}}; return f(a); }
EOF

shape prototype_first "6:" <<'EOF'
static int f(int n, int a[n]);
static int g(int x) { return x; }
static int f(int n, int a[n]) { return a[n - 1] + g(0); }
int main(void) { int a[2] = {3, 6}; return f(2, a); }
EOF

shape nested_fptr "7:" <<'EOF'
typedef int (*cb)(int n, int a[n]);
static int g(int n, int a[n]) { return a[n - 1]; }
static int h(cb fn, int n, int a[n]) { return fn(n, a); }
int main(void) { int a[3] = {1, 2, 7}; return h(g, 3, a); }
EOF

shape vla_local_beside "6:" <<'EOF'
static int f(int n, int a[n]) { int b[n]; b[0] = a[n - 1]; return b[0]; }
int main(void) { int a[3] = {1, 2, 6}; return f(3, a); }
EOF

echo
echo "== [*] is a prototype-only bound"
shape star_prototype "0:" <<'EOF'
static int f(int n, int a[*]);
int main(void) { return 0; }
EOF
shape star_definition "refused" <<'EOF'
static int f(int n, int a[*]) { return a[0]; }
int main(void) { return f(1, 0); }
EOF

echo
echo "vlaparam: $pass passed, $fail mismatch(es)"
[ $fail -eq 0 ]
