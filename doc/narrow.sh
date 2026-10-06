#!/bin/bash
# Constant conversions that change the value. One line per shape, three
# compilers, default options only -- plus the opt-in floating group, which
# neither reference enables by default.
#
#   gcc  -Woverflow (no name of its own for the floating-to-integer case)
#   clang -Wconstant-conversion / -Wliteral-conversion
#   cxx   both, with clang's wording; -Wfloat-conversion for the rest
C=${1:-/home/memory/cxx/cxx}
t=`mktemp -d /tmp/cxx-narrow-XXXXXX`
trap 'rm -rf $t' EXIT

row() { # row <name> [extra cxx flags] ; source on stdin
    local name=$1 flags=$2
    cat > "$t/$name.c"
    printf '%-22s' "$name"
    for cc in gcc clang; do
        out=$($cc -std=c23 -c -o /dev/null "$t/$name.c" 2>&1 | grep -E 'warning:' | grep -v unused | head -1)
        printf '  %-5s %-4s' "$cc" "$([ -n "$out" ] && echo warn || echo -)"
    done
    out=$("$C" $flags -S -o /dev/null "$t/$name.c" 2>&1 | grep -E 'warning:|error:' | head -1)
    case $out in
        *error:*) verdict=err ;;
        *warning:*) verdict=warn ;;
        *) verdict=- ;;
    esac
    printf '  cxx   %-4s' "$verdict"
    printf ' %s\n' "$(echo "$out" | sed 's/.*warning: //;s/.*error: //' | cut -c1-58)"
}

echo "shape                  gcc   clang cxx   cxx's message"
row char_300 <<'EOF'
char c = 300;
int main(void) { return 0; }
EOF
row char_neg300 <<'EOF'
char c = -300;
int main(void) { return 0; }
EOF
row uchar_300 <<'EOF'
unsigned char c = 300;
int main(void) { return 0; }
EOF
row short_70000 <<'EOF'
short s = 70000;
int main(void) { return 0; }
EOF
row long_to_int <<'EOF'
int i = 3000000000;
int main(void) { return 0; }
EOF
row block_scope <<'EOF'
int main(void) { char c = 300; return c; }
EOF
row assignment <<'EOF'
int main(void) { char c; c = 300; return c; }
EOF
row argument <<'EOF'
void k(char);
int main(void) { k(300); return 0; }
EOF
row return_value <<'EOF'
char h(void) { return 300; }
int main(void) { return 0; }
EOF
row explicit_cast <<'EOF'
int main(void) { char c = (char)300; return c; }
EOF
row unsigned_minus_one <<'EOF'
unsigned u = -1;
unsigned char c = -1;
int main(void) { return 0; }
EOF
row fits_exactly <<'EOF'
char c = 100;
unsigned char uc = 255;
long l = -1;
_Bool b = 2;
int main(void) { return 0; }
EOF
row double_to_int <<'EOF'
int i = 1.5;
int main(void) { return 0; }
EOF
row double_to_int_range <<'EOF'
int i = 1e20;
int main(void) { return 0; }
EOF

echo
echo "=== the constant integer the target float cannot hold: default-on group"
row int_to_float <<'EOF'
float f = 16777217;
double d = 9007199254740993;
int main(void) { return 0; }
EOF

echo
echo "=== -Wfloat-conversion (off by default in both references)"
row float_precision "-Wfloat-conversion" <<'EOF'
float f = 1.1;
int main(void) { return 0; }
EOF
row float_overflow "-Wfloat-conversion" <<'EOF'
float f = 1e300;
int main(void) { return 0; }
EOF
row float_underflow "-Wfloat-conversion" <<'EOF'
float f = 1e-300;
int main(void) { return 0; }
EOF
row int_to_float "-Wfloat-conversion" <<'EOF'
float f = 16777217;
double d = 9007199254740993;
int main(void) { return 0; }
EOF

echo
echo "=== the same file with and without the flags"
cat > "$t/flags.c" <<'EOF'
char c = 300;
int i = 1.5;
float f = 1.1;
int main(void) { return 0; }
EOF
for f in "" "-Wfloat-conversion" "-Wno-constant-conversion" "-Wno-literal-conversion" "-w"; do
    n=$("$C" $f -S -o /dev/null "$t/flags.c" 2>&1 | grep -c 'warning')
    printf '  cxx %-26s %s warning(s)\n' "${f:-（default）}" "$n"
done
