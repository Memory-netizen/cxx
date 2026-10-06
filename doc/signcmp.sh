#!/bin/bash
# -Wsign-compare: a comparison whose operands differ in signedness, so that
# the signed one is converted to unsigned (6.3.1.1 then 6.3.1.8). gcc and
# clang both keep this group out of the C defaults -- it arrives with
# -Wextra -- and cxx follows: the flag turns it on.
#
#   gcc   -Wsign-compare (with -Wextra)
#   clang -Wsign-compare (with -Wextra)
#   cxx   -Wsign-compare
#
# The interesting rows are the silent ones: they are the promotion rules
# (unsigned char promotes to int, so nothing is mixed) and the heuristics
# both references use (a _Bool is 0 or 1; a non-negative constant on the
# signed side fits; the unsigned side being a literal zero is an idiom).
C=${1:-/home/memory/cxx/cxx}
t=`mktemp -d /tmp/cxx-signcmp-XXXXXX`
trap 'rm -rf $t' EXIT

row() { # row <name> <expected: warn|-> ; source on stdin
    local name=$1 want=$2
    cat > "$t/$name.c"
    printf '%-22s' "$name"
    for cc in gcc clang; do
        out=$($cc -std=c23 -Wall -Wextra -c -o /dev/null "$t/$name.c" 2>&1 | grep -E 'different signs|different signedness' | head -1)
        printf '  %-5s %-4s' "$cc" "$([ -n "$out" ] && echo warn || echo -)"
    done
    out=$("$C" -Wsign-compare -S -o /dev/null "$t/$name.c" 2>&1 | grep 'different signedness' | head -1)
    got=$([ -n "$out" ] && echo warn || echo -)
    printf '  cxx   %-4s' "$got"
    if [ "$got" = "$want" ]; then printf 'ok '; else printf 'MISMATCH(want %s) ' "$want"; fi
    printf '%s\n' "$(echo "$out" | sed 's/.*warning: //' | cut -c1-52)"
}

row int_lt_uint warn <<'EOF'
int f(int i, unsigned u) { return i < u; }
int main(void) { return 0; }
EOF
row int_gt_uint warn <<'EOF'
int f(int i, unsigned u) { return i > u; }
int main(void) { return 0; }
EOF
row int_le_ge_uint warn <<'EOF'
int f(int i, unsigned u) { return i <= u ? 1 : i >= u; }
int main(void) { return 0; }
EOF
row int_eq_uint warn <<'EOF'
int f(int i, unsigned u) { return i == u; }
int main(void) { return 0; }
EOF
row int_ne_uint warn <<'EOF'
int f(int i, unsigned u) { return i != u; }
int main(void) { return 0; }
EOF
row uint_uint - <<'EOF'
int f(unsigned a, unsigned b) { return a < b; }
int main(void) { return 0; }
EOF
row int_long - <<'EOF'
int f(int i, long l) { return i < l; }
int main(void) { return 0; }
EOF
row uchar_int - <<'EOF'
int f(unsigned char c, int i) { return c < i; }
int main(void) { return 0; }
EOF
row char_uint warn <<'EOF'
int f(char c, unsigned u) { return c < u; }
int main(void) { return 0; }
EOF
row short_uint warn <<'EOF'
int f(short s, unsigned u) { return s < u; }
int main(void) { return 0; }
EOF
row bool_uint - <<'EOF'
int f(_Bool b, unsigned u) { return b < u; }
int main(void) { return 0; }
EOF
row sizeof_int warn <<'EOF'
int f(int i, int *p) { return sizeof(*p) < i ? 1 : 0; }
int main(void) { return 0; }
EOF
row lit_1u warn <<'EOF'
int f(int i) { return i < 1u; }
int main(void) { return 0; }
EOF
row lit_0u - <<'EOF'
int f(int i) { return i < 0u; }
int main(void) { return 0; }
EOF
row lit_plain - <<'EOF'
int f(int i) { return i < 5; }
int main(void) { return 0; }
EOF
row float_mix - <<'EOF'
int f(int i, double d) { return i < d; }
int main(void) { return 0; }
EOF
row ptr_mix - <<'EOF'
int f(int *p, unsigned u) { return p < u; }
int main(void) { return 0; }
EOF

echo
echo "=== the flag"
printf 'int f(int i, unsigned u) { return i < u; }\nint main(void) { return 0; }\n' > "$t/flag.c"
printf '  cxx default:                     %s warning(s)\n' "$("$C" -S -o /dev/null "$t/flag.c" 2>&1 | grep -c 'different signedness')"
printf '  cxx -Wsign-compare:              %s\n' "$("$C" -Wsign-compare -S -o /dev/null "$t/flag.c" 2>&1 | grep -c 'different signedness')"
printf '  cxx -Wsign-compare -Wno-sign-compare: %s\n' "$("$C" -Wsign-compare -Wno-sign-compare -S -o /dev/null "$t/flag.c" 2>&1 | grep -c 'different signedness')"
printf '  cxx -Wsign-compare -w:           %s\n' "$("$C" -Wsign-compare -w -S -o /dev/null "$t/flag.c" 2>&1 | grep -c 'different signedness')"
printf '  gcc -Wextra:                     %s\n' "$(gcc -std=c23 -Wextra -c -o /dev/null "$t/flag.c" 2>&1 | grep -c 'different signedness')"
printf '  clang -Wextra:                   %s\n' "$(clang -std=c23 -Wextra -c -o /dev/null "$t/flag.c" 2>&1 | grep -c 'different signs')"
