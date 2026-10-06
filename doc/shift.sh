#!/bin/bash
# -Wshift-count-negative / -Wshift-count-overflow, against gcc and clang.
C=/home/memory/cxx/cxx
t=`mktemp -d /tmp/cxx-sh-XXXXXX`
trap 'rm -rf $t' EXIT

cat > "$t/a.c" <<'EOF'
int f1(int x) { return x << 33; }          /* overflow   */
int f2(int x) { return x << 31; }          /* fine       */
int f3(int x) { return x << -1; }          /* negative   */
int f4(long x) { return (int)(x << 64); }  /* overflow   */
int f5(long x) { return (int)(x << 63); }  /* fine       */
int f6(int x) { int n = 3; return x << n; }/* not constant */
int f7(unsigned x) { return x << 32; }     /* overflow   */
int f8(int x) { return x >> 33; }          /* overflow   */
EOF

run() { # run <compiler> <std> <flags...>
    local cc=$1 std=$2; shift 2
    "$cc" $std "$@" -c -o /dev/null "$t/a.c" 2>&1 |
        grep -oE '\[-W[a-zA-Z0-9_-]+\]' | sort | uniq -c | tr '\n' ' '
    echo
}

echo "=== gcc";   run gcc -std=c23
echo "=== clang"; run clang -std=c2y
echo "=== cxx"
$C -S -o /dev/null "$t/a.c" 2>&1 | grep -oE 'shift count[^,]*' | sort | uniq -c

echo
echo "=== line by line (cxx), to see which cases fire"
$C -S -o /dev/null "$t/a.c" 2>&1 | grep -E 'warning:' | sed 's/.*a\.c:/  line /' | cut -c1-60

echo
echo "=== the groups must be switchable"
for f in "-Wno-shift-count-overflow" "-Wno-shift-count-negative" "-w"; do
    printf '  %-30s %s\n' "[$f]" "$($C $f -S -o /dev/null "$t/a.c" 2>&1 | grep -c 'shift count')"
done

echo
echo "=== gcc/clang line-by-line for the same file"
echo "-- gcc"; gcc -std=c23 -c -o /dev/null "$t/a.c" 2>&1 | grep -E 'warning:' | sed 's/.*a\.c:/  line /' | cut -c1-52
echo "-- clang"; clang -std=c2y -c -o /dev/null "$t/a.c" 2>&1 | grep -E 'warning:' | sed 's/.*a\.c:/  line /' | cut -c1-52
