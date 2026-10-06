#!/bin/bash
# D1: warning groups.
C=/home/memory/cxx/cxx
t=`mktemp -d /tmp/cxx-wg-XXXXXX`
trap 'rm -rf $t' EXIT

cat > "$t/dep.c" <<'EOF'
__attribute__((deprecated)) void old_fn(void);
int main(void) { old_fn(); return 0; }
EOF
cat > "$t/nd.c" <<'EOF'
struct S { int a; };
__attribute__((nodiscard)) int f(void);
int main(void) { f(); return 0; }
EOF
cat > "$t/at.c" <<'EOF'
int f(int x) __attribute__((bogus_attribute_name));
int main(void) { return f(1); }
EOF

show() { # show <label> <file> <args...>
    local label=$1 file=$2; shift 2
    "$C" "$@" -S -o /dev/null "$file" > "$t/o" 2>&1
    local rc=$?
    local n
    n=$(grep -c 'warning:' "$t/o")
    printf '%-42s exit=%d warnings=%d %s\n' "$label" "$rc" "$n" "$(grep -m1 -oE '(error|warning): .{0,48}' "$t/o")"
}

echo "=== deprecated group"
show "deprecated, default"            "$t/dep.c"
show "deprecated, -Wno-deprecated-declarations" "$t/dep.c" -Wno-deprecated-declarations
show "deprecated, -Wdeprecated-declarations"    "$t/dep.c" -Wdeprecated-declarations
show "deprecated, -Wall"              "$t/dep.c" -Wall
show "deprecated, -w"                 "$t/dep.c" -w
show "deprecated, -Werror"            "$t/dep.c" -Werror

echo
echo "=== the other groups"
show "nodiscard, default"             "$t/nd.c"
show "nodiscard, -Wno-unused-result"  "$t/nd.c" -Wno-unused-result
show "unknown attribute, default"     "$t/at.c"
show "unknown attribute, -Wno-attributes" "$t/at.c" -Wno-attributes
show "unknown attribute, -Wno-deprecated-declarations" "$t/at.c" -Wno-deprecated-declarations

echo
echo "=== unknown groups must be rejected"
for g in -Wno-bogus-option -Wbogus -Wno-unused-var; do
    printf '%-24s ' "$g"
    if "$C" "$g" -S -o /dev/null "$t/dep.c" > "$t/o" 2>&1; then
        echo "ACCEPTED (should not be)"
    else
        echo "rejected: $(grep -m1 -oE '(error|fatal error): .{0,40}' "$t/o")"
    fi
done

echo
echo "=== compatibility spellings that must keep working"
for g in -Wall -Wextra -Werror -Wl,-z,muldefs -w; do
    printf '%-24s ' "$g"
    if "$C" "$g" -S -o /dev/null "$t/dep.c" > "$t/o" 2>&1; then echo ok; else echo "FAILED: $(head -1 "$t/o")"; fi
done
