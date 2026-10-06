#!/bin/bash
# Each group must answer to every name gcc or clang uses for it.
C=/home/memory/cxx/cxx
t=`mktemp -d /tmp/cxx-alias-XXXXXX`
trap 'rm -rf $t' EXIT

cat > "$t/at.c" <<'EOF'
int f(int x) __attribute__((bogus_attribute));
int main(void) { return f(1); }
EOF
cat > "$t/ao.c" <<'EOF'
#include <stdatomic.h>
int main(void) { _Atomic int v; return atomic_load_explicit(&v, memory_order_release); }
EOF
cat > "$t/nr.c" <<'EOF'
_Noreturn void f(void) { return; }
int main(void) { return 0; }
EOF
cat > "$t/bm.c" <<'EOF'
#undef __LINE__
int main(void) { return 0; }
EOF
cat > "$t/et.c" <<'EOF'
#ifdef X
#endif X
int main(void) { return 0; }
EOF

n() { # n <label> <file> <flags...>
    local label=$1 file=$2; shift 2
    local c
    c=$("$C" "$@" -S -o /dev/null "$file" 2>&1 | grep -c 'warning:')
    printf '  %-40s warnings=%d\n' "$label" "$c"
}

echo "=== attributes: gcc's name and clang's names are the same group"
n "default"                       "$t/at.c"
n "-Wattributes (gcc)"            "$t/at.c" -Wattributes
n "-Wunknown-attributes (clang)"  "$t/at.c" -Wunknown-attributes
n "-Wignored-attributes (clang)"  "$t/at.c" -Wignored-attributes
n "-Wno-unknown-attributes"       "$t/at.c" -Wno-unknown-attributes

echo "=== atomic memory order: gcc's name"
n "default"                          "$t/ao.c"
n "-Watomic-memory-ordering (clang)" "$t/ao.c" -Watomic-memory-ordering
n "-Winvalid-memory-model (gcc)"     "$t/ao.c" -Winvalid-memory-model

echo "=== extra tokens: clang's name and gcc's name"
n "default"                     "$t/et.c"
n "-Wextra-tokens (clang)"      "$t/et.c" -Wextra-tokens
n "-Wendif-labels (gcc)"        "$t/et.c" -Wendif-labels

echo "=== the corrected names"
n "noreturn, default"           "$t/nr.c"
n "noreturn, -Winvalid-noreturn" "$t/nr.c" -Winvalid-noreturn
n "undef builtin, default"      "$t/bm.c"
n "undef builtin, -Wbuiltin-macro-redefined" "$t/bm.c" -Wbuiltin-macro-redefined

echo
echo "=== every name still rejects a genuinely unknown one"
"$C" -Wno-totally-made-up -S -o /dev/null "$t/at.c" 2>&1 | head -1
