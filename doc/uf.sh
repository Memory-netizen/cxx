#!/bin/bash
# -Wunused-function / -Wunused-variable at file scope, and the reachability
# walk behind them.
#
# cxx answers these from a reference graph built while parsing: roots are the
# definitions another translation unit can see, and everything they reach
# stays. gcc and clang instead ask "was this name ever mentioned", which
# cannot see a pair of static functions that only call each other; both do
# drop them from the object file, though, so the two questions disagree
# inside clang itself.
C=${1:-/home/memory/cxx/cxx}
t=`mktemp -d /tmp/cxx-uf2-XXXXXX`
trap 'rm -rf $t' EXIT

cat > "$t/a.c" <<'EOF'
static int never_used(void) { return 1; }

/* a and b reference each other, so neither is reachable from main */
static int a(void);
static int b(void);
static int a(void) { return b(); }
static int b(void) { return a(); }

static int used(void) { return 0; }

/* reachable only through a non-static function */
static int via_extern(void) { return 7; }
int exported(void) { return via_extern(); }

/* address taken: not a call, but still a use */
static int addr_taken(void) { return 2; }
int (*fp)(void) = addr_taken;

int main(void) { return used() + exported() + (fp != 0); }
EOF

cat > "$t/b.c" <<'EOF'
static int plain;                       /* never named            */
static const int konst = 7;             /* never named, const     */
static const char text[] = "hi";        /* array of const         */
static int live = 1;
static int read_by_live(void) { return live; }
static void dead(void) { plain = 1; }   /* the only user of plain */
static void target(void) {}
static void (*table[1])(void) = { target };  /* itself unused     */
static int sizeof_only;
_Static_assert(sizeof sizeof_only == 4, "sized");
/* a block-scope static belongs to its function: gcc and clang report an
   unused one, and clang still emits it, unlike a file-scope one */
static int block_scope(void) { static int q = 1; return 0; }
int main(void) { return read_by_live() - 1 + block_scope(); }
EOF

show() { # show <name> <file>
    echo "======================== $1"
    echo "-- gcc -Wall"
    gcc -std=c23 -Wall -c -o /dev/null "$2" 2>&1 | grep -E 'warning:' | sed 's/^/     /'
    echo "-- clang -Wall"
    clang -std=c23 -Wall -c -o /dev/null "$2" 2>&1 | grep -E 'warning:' | sed 's/^/     /'
    echo "-- cxx"
    $C -S -o /dev/null "$2" 2>&1 | grep -E 'warning:' | sed 's/^/     /'
    echo "-- IR: clang vs cxx"
    clang -std=c23 -O0 -S -emit-llvm -o "$t/c.ll" "$2" 2>/dev/null
    $C -w -S -emit-llvm -o "$t/x.ll" "$2" 2>/dev/null
    diff <(grep -E '^define|^@' "$t/c.ll" | sed 's/ #0 {$//;s/,$//') \
         <(grep -E '^define|^@' "$t/x.ll" | sed 's/ {$//;s/,$//') | sed 's/^/     /'
}

show "functions: mutual recursion, address taken, via extern" "$t/a.c"
show "objects: unused, const, dead-referenced, used by a root" "$t/b.c"

echo
echo "======================== the groups are switchable"
for f in "" "-Wno-unused-function" "-Wno-unused-variable" "-Wno-unused-const-variable" "-w"; do
    printf '  %-30s function=%s variable=%s\n' "[${f:-<default>}]" \
        "$($C $f -S -o /dev/null "$t/b.c" 2>&1 | grep -c 'unused function')" \
        "$($C $f -S -o /dev/null "$t/b.c" 2>&1 | grep -c 'unused variable')"
done
printf '  %-30s %s\n' "[-Wunused-function -Werror]" \
    "$($C -Wunused-function -Werror -S -o /dev/null "$t/a.c" 2>&1 | grep -c 'error: unused function')"

echo
echo "======================== a static function is still emitted when a root reaches it"
cat > "$t/c.c" <<'EOF'
static void kept(void) {}
int main(void) { kept(); return 0; }
EOF
$C -w -S -emit-llvm -o "$t/d.ll" "$t/c.c" && grep -c '@kept' "$t/d.ll" | sed 's/^/  uses of @kept: /'
