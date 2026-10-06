#!/bin/bash
# E1 / E2: what do gcc and clang actually diagnose, and in which group?
#
# The flags go to all three compilers -- an earlier version of this probe
# passed a separate std flag per compiler and silently dropped the rest, so
# gcc was measured without -Wimplicit-fallthrough and the table it produced
# blamed gcc for a warning it never had a chance to give.
CXX=/home/memory/cxx/cxx
t=`mktemp -d /tmp/cxx-e12-XXXXXX`
trap 'rm -rf $t' EXIT

tag() { local cc=$1; "$cc" "${@:2}" -c -o /dev/null 2>&1 | grep -oE '\[-W[a-zA-Z0-9=_-]+\]' | head -1 | tr -d '[]'; }
n() { local cc=$1; "$cc" "${@:2}" -c -o /dev/null 2>&1 | grep -cE 'warning:'; }
show() { # show <label> <file> <flags...>
    local label=$1 file=$2; shift 2
    printf '%-32s cxx=%d/%-24s gcc=%d/%-26s clang=%d/%s\n' \
        "$label" \
        "$(n $CXX -std=c2y "$@" "$file")" "$(tag $CXX -std=c2y "$@" "$file")" \
        "$(n gcc -std=c23 "$@" "$file")" "$(tag gcc -std=c23 "$@" "$file")" \
        "$(n clang -std=c23 "$@" "$file")" "$(tag clang -std=c23 "$@" "$file")"
}

cat > "$t/dep1.c" <<'EOF'
struct [[deprecated]] S { int a; };
struct S v;
int main(void) { return v.a; }
EOF
cat > "$t/dep2.c" <<'EOF'
struct [[deprecated]] S { int a; };
int main(void) { struct S s = {1}; return s.a - 1; }
EOF
cat > "$t/dep3.c" <<'EOF'
typedef int [[deprecated]] myint;
int main(void) { myint x = 1; return x - 1; }
EOF
cat > "$t/dep4.c" <<'EOF'
enum [[deprecated]] E { A };
int main(void) { return A; }
EOF

cat > "$t/fall1.c" <<'EOF'
int f(int x) { switch (x) { case 1: x++; case 2: return x; } return 0; }
int main(void) { return f(1); }
EOF
cat > "$t/fall2.c" <<'EOF'
int f(int x) { switch (x) { case 1: x++; [[fallthrough]]; case 2: return x; } return 0; }
int main(void) { return f(1); }
EOF
cat > "$t/fall3.c" <<'EOF'
int f(int x) { switch (x) { case 1: x++; break; case 2: return x; } return 0; }
int main(void) { return f(1); }
EOF

echo "### E1 - deprecated types"
show "struct [[deprecated]], global use" "$t/dep1.c"
show "struct [[deprecated]], local use" "$t/dep2.c"
show "typedef [[deprecated]]" "$t/dep3.c"
show "enum [[deprecated]]" "$t/dep4.c"

echo
echo "### E2 - implicit fallthrough"
show "fallthrough, default" "$t/fall1.c"
show "fallthrough, -Wall" "$t/fall1.c" -Wall
show "fallthrough, -Wextra" "$t/fall1.c" -Wextra
show "fallthrough, own flag" "$t/fall1.c" -Wimplicit-fallthrough
show "[[fallthrough]] honoured" "$t/fall2.c" -Wimplicit-fallthrough
show "break, no fallthrough" "$t/fall3.c" -Wimplicit-fallthrough
