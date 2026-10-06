#!/bin/bash
C=/home/memory/cxx/cxx
t=`mktemp -d /tmp/cxx-wal-XXXXXX`
trap 'rm -rf $t' EXIT
printf 'int main(void) { return 0; }\n' > "$t/a.c"
printf '__attribute__((deprecated)) void f(void);\nint main(void) { f(); return 0; }\n' > "$t/d.c"

echo "=== the new group is accepted, and -Wall does not reach it"
for f in "" "-Wall" "-Wextra" "-Wimplicit-fallthrough" "-Wno-implicit-fallthrough" "-w"; do
    printf '  %-28s ' "[$f]"
    if $C $f -S -o /dev/null "$t/a.c" > "$t/o" 2>&1; then echo "accepted"; else echo "rejected: $(head -1 "$t/o")"; fi
done

echo
echo "=== groups that WERE on by default must stay on under -Wall"
for f in "" "-Wall"; do
    printf '  %-10s deprecated warnings=%s\n' "[$f]" \
        "$($C $f -S -o /dev/null "$t/d.c" 2>&1 | grep -c 'deprecated')"
done
printf '  %-10s deprecated warnings=%s\n' "[-w]" \
    "$($C -w -S -o /dev/null "$t/d.c" 2>&1 | grep -c 'deprecated')"
