#!/bin/bash
# D3: -pedantic / -pedantic-errors and the GNU extension points.
C=/home/memory/cxx/cxx
t=`mktemp -d /tmp/cxx-ped-XXXXXX`
trap 'rm -rf $t' EXIT

printf 'int main(void) { int a$b = 1; return a$b - 1; }\n' > "$t/dollar.c"
cat > "$t/gnu.c" <<'EOF'
int stmt(void) { return ({ int x = 1; x + 1; }); }
int cond(int x) { return x ?: 7; }
void cg(void *p) { goto *p; }
const char *fn(void) { return __FUNCTION__; }
void *lv(void) { l: return &&l; }
int main(void) { return stmt() - 2 + cond(1) + (fn() != 0) + (lv() != 0); }
EOF
printf 'int main(void) { return 0; }\n' > "$t/plain.c"

run() { # run <label> <file> <args...>
    local label=$1 file=$2; shift 2
    "$C" "$@" -S -o /dev/null "$file" > "$t/o" 2>&1
    printf '%-44s exit=%d  %s\n' "$label" "$?" "$(grep -m1 -oE '(error|warning): .{0,44}' "$t/o")"
}

echo "=== \$ in an identifier"
run "default"                       "$t/dollar.c"
run "-pedantic"                     "$t/dollar.c" -pedantic
run "-pedantic-errors"              "$t/dollar.c" -pedantic-errors
run "-Wpedantic (same mode)"        "$t/dollar.c" -Wpedantic
run "-pedantic -Wno-pedantic"       "$t/dollar.c" -pedantic -Wno-pedantic
run "plain program, -pedantic"      "$t/plain.c"  -pedantic

echo
echo "=== one GNU construct per line, and what -pedantic says about each"
run "default"                       "$t/gnu.c"
run "-pedantic"                     "$t/gnu.c" -pedantic
run "-pedantic-errors"              "$t/gnu.c" -pedantic-errors
run "-w -pedantic"                  "$t/gnu.c" -w -pedantic
run "-w -pedantic-errors"           "$t/gnu.c" -w -pedantic-errors
echo "-- every diagnostic -pedantic gives, and gcc's for the same file:"
"$C" -pedantic -S -o /dev/null "$t/gnu.c" 2>&1 | grep -E 'ISO C' | sed 's/^/     /'
gcc -std=c23 -pedantic -fsyntax-only "$t/gnu.c" 2>&1 | grep -E 'ISO C|non-standard' | sed 's/^/     gcc: /' 

echo
echo "=== -pedantic must not break the system headers"
printf '#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <math.h>\nint main(void){char*p=malloc(4);strcpy(p,"a");printf("%%s\\n",p);free(p);return 0;}\n' > "$t/sys.c"
run "stdio/stdlib with -pedantic"   "$t/sys.c" -pedantic
