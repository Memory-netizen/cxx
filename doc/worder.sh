#!/bin/bash
# Does `-w -Wunused-variable` re-enable the group, and does the order matter?
#
# GCC's manual says: "more specific options have priority over less specific
# ones, independently of their position in the command line. For options of
# the same specificity, the last one takes effect."
#
# So -w is the LEAST specific and a named -W<group> wins in either order.
C=/home/memory/cxx/cxx
t=`mktemp -d /tmp/cxx-wo-XXXXXX`
trap 'rm -rf $t' EXIT

cat > "$t/u.c" <<'EOF'
int main(void) { int unused = 1; return 0; }
EOF
cat > "$t/d.c" <<'EOF'
__attribute__((deprecated)) void f(void);
int main(void) { f(); return 0; }
EOF

count() { # count <compiler> <std> <file> <flags...>
    local cc=$1 std=$2 file=$3; shift 3
    "$cc" $std "$@" -c -o /dev/null "$file" 2>&1 | grep -cE 'warning:'
}

row() { # row <file> <flag...>
    local file=$1; shift
    local label="$*"
    printf '  %-40s gcc=%s clang=%s cxx=%s\n' "${label:-<none>}" \
        "$(count gcc -std=c23 "$t/$file" "$@")" \
        "$(count clang -std=c2y "$t/$file" "$@")" \
        "$(count $C -std=c2y "$t/$file" "$@")"
}

echo "=== unused variable: -w (all off) vs -Wunused-variable (one on)"
row u.c
row u.c -w
row u.c -Wunused-variable
row u.c -w -Wunused-variable
row u.c -Wunused-variable -w

echo
echo "=== the same shape with a group that is on by default"
row d.c
row d.c -w
row d.c -w -Wdeprecated-declarations
row d.c -Wdeprecated-declarations -w

echo
echo "=== -w against -Werror: which is more specific?"
row u.c -w -Werror
row u.c -Werror -w

echo
echo "=== a less specific positive vs a more specific negative"
row u.c -Wall -Wno-unused-variable
row u.c -Wno-unused-variable -Wall
