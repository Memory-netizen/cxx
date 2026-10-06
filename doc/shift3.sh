#!/bin/bash
# Extract the shift test from c2y.sh and compare all three compilers on it.
C=/home/memory/cxx/cxx
t=`mktemp -d /tmp/cxx-sh3-XXXXXX`
trap 'rm -rf $t' EXIT
awk '/cat > "\$tmp\/shift.c" <<.EOF./{f=1;next} f&&/^EOF$/{exit} f' \
    /home/memory/cxx/test/c2y.sh > "$t/a.c"
echo "lines: $(wc -l < "$t/a.c")"
echo "--- cxx";   $C -S -o /dev/null "$t/a.c" 2>&1 | grep -oE 'shift count [a-z><= ]*' | sort | uniq -c
echo "--- gcc";   gcc -std=c23 -c -o /dev/null "$t/a.c" 2>&1 | grep -oE '\[-W[a-z-]+\]' | sort | uniq -c
echo "--- clang"; clang -std=c2y -c -o /dev/null "$t/a.c" 2>&1 | grep -oE '\[-W[a-z-]+\]' | sort | uniq -c
echo "--- which lines"
echo "cxx  : $($C -S -o /dev/null "$t/a.c" 2>&1 | grep -oE 'a\.c:[0-9]+' | cut -d: -f2 | sort -n | uniq | tr '\n' ' ')"
echo "gcc  : $(gcc -std=c23 -c -o /dev/null "$t/a.c" 2>&1 | grep -oE 'a\.c:[0-9]+' | cut -d: -f2 | sort -n | uniq | tr '\n' ' ')"
echo "clang: $(clang -std=c2y -c -o /dev/null "$t/a.c" 2>&1 | grep -oE 'a\.c:[0-9]+' | cut -d: -f2 | sort -n | uniq | tr '\n' ' ')"
