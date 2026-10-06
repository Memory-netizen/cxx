#!/bin/bash
# 6.8.6.1p1 and 6.8.5.3p2: a jump may not pass the declaration of an identifier
# with a variably modified type.
#
# Usage: bash doc/vmgoto.sh ./cxx
#
# Each shape is compiled by gcc, clang and cxx: the verdict (refused or
# accepted) has to agree, and cxx's message is printed for the refused ones so
# the wording can be compared with the references by eye.
set -u
C=${1:-./cxx}
tmp=`mktemp -d /tmp/cxx-vmgoto-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

pass=0
fail=0

shape() { # shape <name> <expected: refused|accepted>; program on stdin
    name=$1
    cat > "$tmp/$name.c"
    ok=1
    line=""
    for cc in gcc clang "$C"; do
        if $cc -std=c23 -S -o /dev/null "$tmp/$name.c" > "$tmp/$name.log" 2>&1; then
            got=accepted
        else
            got=refused
        fi
        [ "$got" != "$2" ] && ok=0
        line="$line $cc=$got"
    done
    if [ $ok -eq 1 ]; then
        printf '  ok        %-18s %s\n' "$name" "$2"
        pass=$((pass + 1))
    else
        printf '  MISMATCH  %-18s expected %s:%s\n' "$name" "$2" "$line"
        fail=$((fail + 1))
    fi
}

echo "== goto (6.8.6.1p1)"
shape goto_into_block refused <<'EOF'
int f(int n) { goto l; { int a[n]; l: a[0] = 1; } return 0; }
EOF
shape goto_over_decl refused <<'EOF'
int f(int n) { goto l; int a[n]; l: a[0] = 1; return 0; }
EOF
shape goto_after_decl accepted <<'EOF'
int f(int n) { int a[n]; goto l; l: a[0] = 1; return 0; }
EOF
shape goto_over_vm_pointer refused <<'EOF'
int f(int n) { goto l; int (*p)[n]; l: return p != 0; }
EOF
shape goto_over_fixed_array accepted <<'EOF'
int f(void) { goto l; int a[3]; l: a[0] = 1; return a[0]; }
EOF
shape goto_other_scope accepted <<'EOF'
int f(int n) { { int a[n]; a[0] = 1; } goto l; { l: ; } return 0; }
EOF
shape goto_backward_over_vla refused <<'EOF'
int f(int n) { l: goto done; int a[n]; a[0] = 1; done: return 0; }
EOF
shape goto_backward_to_label accepted <<'EOF'
int f(int n) { int a[n]; l: goto l; return a[0]; }
EOF
shape goto_over_vm_typedef refused <<'EOF'
int f(int n) { goto l; typedef int T[n]; l: return sizeof(T); }
EOF
shape goto_inside_scope accepted <<'EOF'
int f(int n) { int a[n]; { goto l; } l: return a[0]; }
EOF
shape goto_before_decl accepted <<'EOF'
int f(int n) { goto l; { l: ; } int a[n]; return 0; }
EOF
shape goto_undeclared_label refused <<'EOF'
int f(void) { goto nowhere; return 0; }
EOF

echo
echo "== switch (6.8.5.3p2)"
shape case_after_vla refused <<'EOF'
int f(int n, int x) { switch (x) { case 1: ; int a[n]; case 2: return a[0]; } return 0; }
EOF
shape case_in_vla_block refused <<'EOF'
int f(int n, int x) { switch (x) { { int a[n]; case 1: return a[0]; } } return 0; }
EOF
shape case_before_vla accepted <<'EOF'
int f(int n, int x) { switch (x) { case 1: ; int a[n]; return 0; } return 0; }
EOF
shape vla_outside_switch accepted <<'EOF'
int f(int n, int x) { int a[n]; switch (x) { case 1: return a[0]; } return 0; }
EOF
shape case_with_fixed_array accepted <<'EOF'
int f(int x) { switch (x) { case 1: ; int a[3]; case 2: return a[0]; } return 0; }
EOF

echo
echo "== cxx's wording for the refused shapes"
for name in goto_over_decl goto_over_vm_typedef case_after_vla; do
    "$C" -S -o /dev/null "$tmp/$name.c" 2>&1 | grep -E 'error|note' | sed 's/^/  /' | head -2
done

echo
echo "vmgoto: $pass passed, $fail mismatch(es)"
[ $fail -eq 0 ]
