#!/bin/bash
# The shift check now lives in the folder, so a count that only becomes a
# constant by folding is covered too, and a literal one is not reported twice.
C=/home/memory/cxx/cxx
t=`mktemp -d /tmp/cxx-sh2-XXXXXX`
trap 'rm -rf $t' EXIT

cat > "$t/a.c" <<'EOF'
int f1(int x) { return x << 33; }             /* overflow, literal    */
int f2(int x) { return x << 31; }             /* fine                 */
int f3(int x) { return x << -1; }             /* negative             */
int f4(int x) { return x << (2 + 1); }        /* fine, folded 3       */
int f5(int x) { return x << (32 + 1); }       /* overflow, folded 33  */
int f6(int x) { return x << (1 << 5); }       /* overflow, folded 32  */
int f7(long x) { return (int)(x << (64 - 0)); } /* overflow, folded 64 */
int f8(int x) { int n = 3; return x << n; }   /* not constant, fine   */
int f9(int x) { return x >> 33; }             /* overflow             */
int f10(int x) { return x << 30; }            /* fine                 */
EOF

n() { "$1" "$2" "$3" -c -o /dev/null "$4" 2>&1 | grep -c 'warning:'; }
lines() { "$1" "$2" "$3" -c -o /dev/null "$4" 2>&1 | grep -oE "$5" | tr '\n' ' '; }

echo "=== counts"
printf 'cxx   overflow=%s negative=%s\n' \
  "$($C -S -o /dev/null "$t/a.c" 2>&1 | grep -c 'shift count >= width')" \
  "$($C -S -o /dev/null "$t/a.c" 2>&1 | grep -c 'shift count is negative')"
echo "gcc   $(gcc -std=c23 -c -o /dev/null "$t/a.c" 2>&1 | grep -oE '\[-W[a-z-]+\]' | sort | uniq -c | tr '\n' ' ')"
echo "clang $(clang -std=c2y -c -o /dev/null "$t/a.c" 2>&1 | grep -oE '\[-W[a-z-]+\]' | sort | uniq -c | tr '\n' ' ')"

echo
echo "=== which lines fire (line numbers, should be identical)"
echo "-- cxx  "; $C -S -o /dev/null "$t/a.c" 2>&1 | grep -oE 'a\.c:[0-9]+' | sort -t: -k2 -n | uniq | tr '\n' ' '; echo
echo "-- gcc  "; gcc -std=c23 -c -o /dev/null "$t/a.c" 2>&1 | grep -oE 'a\.c:[0-9]+' | sort -t: -k2 -n | uniq | tr '\n' ' '; echo
echo "-- clang"; clang -std=c2y -c -o /dev/null "$t/a.c" 2>&1 | grep -oE 'a\.c:[0-9]+' | sort -t: -k2 -n | uniq | tr '\n' ' '; echo
