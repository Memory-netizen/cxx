#!/bin/bash
# GNU asm statements: gcc, clang and cxx on the same shapes.
#
# Usage: bash doc/asm.sh ./cxx
#
# Two kinds of shape. `run` ones are compiled and executed by all three, and
# the exit status -- which is where each program leaves its answer -- has to
# agree. `refuse` ones are compiled only, and all three have to refuse them;
# where cxx's own message is worth reading it is printed, and a compiler that
# dies (a signal rather than a diagnostic) counts as a mismatch.
set -u
C=${1:-./cxx}
tmp=`mktemp -d /tmp/cxx-asm-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

pass=0
fail=0

verdict() { # verdict <cc> <src> <bin>; prints "exit=N", "REJECT" or "CRASH"
    local cc=$1 src=$2 bin=$3
    $cc -w -std=gnu23 -o "$bin" "$src" > "$tmp/$(basename "$cc").log" 2>&1
    local st=$?
    if [ $st -ne 0 ]; then
        # A compiler that died rather than diagnosed is not a verdict.
        if [ $st -ge 128 ]; then echo CRASH; else echo REJECT; fi
        return
    fi
    "$bin" > /dev/null 2>&1
    echo "exit=$?"
}

run() { # run <name> <want>; program on stdin, answering in its exit status
    local name=$1 want=$2
    cat > "$tmp/$name.c"
    local ok=1 line=""
    for cc in gcc clang "$C"; do
        local got=$(verdict "$cc" "$tmp/$name.c" "$tmp/$name.$$")
        [ "$got" != "exit=$want" ] && ok=0
        line="$line $(basename $cc)=$got"
    done
    if [ $ok -eq 1 ]; then
        printf '  ok        %-22s exit=%s\n' "$name" "$want"
        pass=$((pass + 1))
    else
        printf '  MISMATCH  %-22s want exit=%s:%s\n' "$name" "$want" "$line"
        fail=$((fail + 1))
    fi
}

refuse() { # refuse <name>; program on stdin
    local name=$1
    cat > "$tmp/$name.c"
    local ok=1 line=""
    for cc in gcc clang "$C"; do
        local got=$(verdict "$cc" "$tmp/$name.c" "$tmp/$name.$$")
        [ "$got" != REJECT ] && ok=0
        line="$line $(basename $cc)=$got"
    done
    if [ $ok -eq 1 ]; then
        printf '  ok        %-22s refused by all three\n' "$name"
        pass=$((pass + 1))
    else
        printf '  MISMATCH  %-22s want refused:%s\n' "$name" "$line"
        fail=$((fail + 1))
    fi
}

# note <name> -- the two references disagree, so there is no verdict to check:
# what each of the three did is printed and cxx's behaviour is recorded in
# doc/cxx-c2y-plan.md.
note() {
    local name=$1
    cat > "$tmp/$name.c"
    local line=""
    for cc in gcc clang "$C"; do
        local got=$(verdict "$cc" "$tmp/$name.c" "$tmp/$name.$$")
        line="$line $(basename $cc)=$got"
    done
    printf '  note      %-22s%s\n' "$name" "$line"
}

echo "== basic forms"
run basic 42 <<'EOF'
int main(void) { __asm__("nop"); return 42; }
EOF
run string_concat 3 <<'EOF'
int main(void) { int y; __asm__("movl $3," "\t%0" : "=r"(y)); return y; }
EOF
run macro 5 <<'EOF'
#define BARRIER() __asm__ __volatile__("" ::: "memory")
int main(void) { int y; __asm__("movl $5, %0" : "=r"(y)); BARRIER(); return y; }
EOF
run in_loop 30 <<'EOF'
int main(void) { int i, s = 0; for (i = 0; i < 3; i++) { __asm__("addl $10, %0" : "+r"(s)); } return s; }
EOF
run in_stmt_expr 7 <<'EOF'
int main(void) { int y; int z = ({ __asm__("movl $7, %0" : "=r"(y)); y; }); return z; }
EOF
run qualifiers 1 <<'EOF'
int main(void) { int y; __asm__ __volatile__ inline("movl $1, %0" : "=r"(y)); return y; }
EOF

echo "== operands"
run out_in 7 <<'EOF'
int main(void) { int y; int x = 7; __asm__("movl %1, %0" : "=r"(y) : "r"(x)); return y; }
EOF
run literal_dollar 42 <<'EOF'
int main(void) { int y; __asm__("movl $42, %0" : "=r"(y)); return y; }
EOF
run mem_out 5 <<'EOF'
int main(void) { int x = 0; __asm__("movl $5, %0" : "=m"(x)); return x; }
EOF
run mem_in 9 <<'EOF'
int main(void) { int x = 9, y; __asm__("movl %1, %0" : "=r"(y) : "m"(x)); return y; }
EOF
run plus_r 9 <<'EOF'
int main(void) { int x = 7; __asm__("addl $2, %0" : "+r"(x)); return x; }
EOF
run plus_m 12 <<'EOF'
int main(void) { int x = 10; __asm__("addl $2, %0" : "+m"(x)); return x; }
EOF
run named 3 <<'EOF'
int main(void) { int y; int x = 3; __asm__("movl %[a], %[b]" : [b] "=r"(y) : [a] "r"(x)); return y; }
EOF
run immediate 5 <<'EOF'
int main(void) { int y; __asm__("movl %1, %0" : "=r"(y) : "i"(5)); return y; }
EOF
run matching 8 <<'EOF'
int main(void) { int x = 4; __asm__("addl %1, %0" : "=r"(x) : "0"(x)); return x; }
EOF
run earlyclobber 6 <<'EOF'
int main(void) { int x = 4, y; __asm__("leal 2(%1), %0" : "=&r"(y) : "r"(x)); return y; }
EOF
run two_out 12 <<'EOF'
int main(void) { int a, b; __asm__("movl $5, %0\n\tmovl $7, %1" : "=r"(a), "=r"(b)); return a + b; }
EOF
run three_out 24 <<'EOF'
int main(void) { int a, b, c; __asm__("movl $6, %0\n\tmovl $8, %1\n\tmovl $10, %2" : "=r"(a), "=r"(b), "=r"(c)); return a + b + c; }
EOF
run mixed_out 49 <<'EOF'
int main(void) {
    int x = 11, y = 22, z = 33;
    __asm__ volatile("movl %3, %0\n\tmovl %4, %1\n\tmovl %5, %2"
                     : "=r"(x), "=m"(y), "=r"(z)
                     : "r"(101), "r"(102), "r"(103));
    return (x + y + z) & 0xff;
}
EOF
run fixed_reg 3 <<'EOF'
int main(void) { int y; __asm__("movl $3, %%eax" : "=a"(y)); return y; }
EOF
run fixed_reg_word 4 <<'EOF'
int main(void) { long y; __asm__("movq $4, %%rax" : "=a"(y)); return (int)y; }
EOF
run sse_reg 2 <<'EOF'
int main(void) { double d = 2.0, e; __asm__("movsd %1, %0" : "=x"(e) : "x"(d)); return (int)e; }
EOF
run ptr_operand 6 <<'EOF'
int main(void) { int v = 6, *p = &v, y; __asm__("movl (%1), %0" : "=r"(y) : "r"(p)); return y; }
EOF
run struct_mem 1 <<'EOF'
struct S { int a, b; };
int main(void) { struct S s = {1, 2}; int y; __asm__("movl %1, %0" : "=r"(y) : "m"(s)); return y; }
EOF
run struct_out_piece 5 <<'EOF'
struct S { int a; };
int main(void) { struct S s; __asm__("movl $5, %0" : "=r"(s)); return s.a; }
EOF
run x87_operand 0 <<'EOF'
int main(void) { long double d = 3.0L, e; __asm__("fstpl %0" : "=m"(e)); return (int)e; }
EOF
run clobbers 0 <<'EOF'
int main(void) { __asm__ __volatile__("" ::: "memory", "cc"); return 0; }
EOF
run clobbers_with_operands 9 <<'EOF'
int main(void) {
    int x = 4, y = 0;
    __asm__ __volatile__("leal 5(%1), %0" : "=r"(y) : "r"(x) : "memory", "cc");
    return y;
}
EOF
run clobber_reg 0 <<'EOF'
int main(void) { __asm__ __volatile__("" ::: "r11", "memory"); return 0; }
EOF
run escape_newline 1 <<'EOF'
int main(void) { int y; __asm__("movl $1, %0\n\t" "nop" : "=r"(y)); return y; }
EOF
run arr_whole 0 <<'EOF'
int main(void) { int a[4] = {1, 2, 3, 4}; __asm__ __volatile__("" :: "m"(a)); return 0; }
EOF
run arr_elem_out 4 <<'EOF'
int main(void) { int a[2] = {0, 0}; __asm__("movl $4, %0" : "=m"(a[1])); return a[1]; }
EOF
run field_out 6 <<'EOF'
struct S { int a, b; };
int main(void) { struct S s = {0, 0}; __asm__("movl $6, %0" : "=r"(s.b)); return s.b; }
EOF
run mod_b 4 <<'EOF'
int main(void) { int y; __asm__("movzbl %b1, %0" : "=r"(y) : "r"(0x0104)); return y; }
EOF
run mod_w 4 <<'EOF'
int main(void) { int y; __asm__("movzwl %w1, %0" : "=r"(y) : "r"(0x104)); return y; }
EOF
run mod_named 9 <<'EOF'
int main(void) { int y; __asm__("movl %[v], %0" : "=r"(y) : [v] "i"(9)); return y; }
EOF
run modconst 6 <<'EOF'
int main(void) { long y; __asm__("movl $6, %k0" : "=r"(y)); return (int)y; }
EOF
run rm_alternative 5 <<'EOF'
int main(void) { int x = 5, y; __asm__("movl %1, %0" : "=r"(y) : "rm"(x)); return y; }
EOF
run if_else 2 <<'EOF'
int main(void) { int y = 0; if (y == 0) { __asm__("movl $2, %0" : "=r"(y)); } else { y = 9; } return y; }
EOF
run switch_case 3 <<'EOF'
int main(void) { int y = 0; switch (1) { case 1: __asm__("movl $3, %0" : "=r"(y)); break; default: y = 9; } return y; }
EOF
run goto_after_vla 1 <<'EOF'
int f(int n) { int a[n]; __asm__ goto("" :::: lab); a[0] = 1; lab: return a[0]; }
int main(void) { return f(1); }
EOF
run cleanup_and_goto 111 <<'EOF'
static int n;
static void h(int *p) { (void)p; n++; }
int main(void) {
    { __attribute__((cleanup(h))) int x = 0; (void)x; __asm__ goto("" :::: lab); n += 10; lab: n += 100; }
    return n;
}
EOF

echo "== asm goto"
run goto_jump 20 <<'EOF'
int main(void) { int r = 0; __asm__ goto("jmp %l0" :::: lab); r = 1; lab: return r ? 10 : 20; }
EOF
run goto_fallthrough 20 <<'EOF'
int main(void) { int r = 0; __asm__ goto("" :::: lab); r = 2; lab: return r * 10; }
EOF
run goto_named 20 <<'EOF'
int main(void) {
    int x = 1, r = 0;
    __asm__ goto("testl %0, %0; jne %l[bb]" : : "r"(x) : : aa, bb);
    r = 1; aa: r += 10; goto done; bb: r += 20; done: return r;
}
EOF
run goto_condition 30 <<'EOF'
int main(void) {
    int x = 0, r = 0;
    __asm__ goto("testl %0, %0; je %l[zero]" : : "r"(x) : : zero);
    r = 3; zero: return r + 30;
}
EOF
run goto_output 7 <<'EOF'
int main(void) { int y = 0; __asm__ goto("movl $7, %0" : "=r"(y) : : : lab); lab: return y; }
EOF

echo "== file scope"
run file_scope_data 7 <<'EOF'
__asm__(".globl myval\n.data\nmyval: .long 7\n.text");
extern int myval;
int main(void) { return myval; }
EOF
run file_scope_two 3 <<'EOF'
__asm__(".globl one\n.data\none: .long 1\n.text");
__asm__(".globl two\n.data\ntwo: .long 2\n.text");
extern int one, two;
int main(void) { return one + two; }
EOF
run file_scope_pct 4 <<'EOF'
__asm__(".globl three\n.data\nthree: .long 4\n.text");
extern int three;
int main(void) { return three; }
EOF

echo "== refused"
refuse bool_operand <<'EOF'
int main(void) { _Bool b = 0; __asm__("movl $1, %0" : "+r"(b)); return b ? 1 : 0; }
EOF
refuse nonlvalue_out <<'EOF'
void f(void) { __asm__("" : "=r"(1)); }
EOF
refuse const_out <<'EOF'
void f(void) { const int x = 1; __asm__("" : "=r"(x)); }
EOF
refuse bitfield_memory <<'EOF'
struct S { int a : 4; };
void f(struct S *s) { int y; __asm__("" : "=r"(y) : "m"(s->a)); }
EOF
refuse output_without_eq <<'EOF'
void f(int x) { __asm__("" : "r"(x)); }
EOF
refuse input_with_eq <<'EOF'
void f(int x) { __asm__("" : : "=r"(x)); }
EOF
refuse duplicate_name <<'EOF'
void f(int x, int y) { __asm__("" : [n] "=r"(x), [n] "=r"(y)); }
EOF
refuse unknown_name <<'EOF'
void f(int x) { __asm__("mov %[nope], %0" : "=r"(x)); }
EOF
refuse operand_out_of_range <<'EOF'
void f(int x) { __asm__("mov %9, %0" : "=r"(x)); }
EOF
refuse label_not_a_label <<'EOF'
void f(int x) { __asm__ goto("jmp %l0" : : "r"(x) : : a); a: ; }
EOF
refuse file_scope_operands <<'EOF'
__asm__("" : "=r"(1));
EOF
refuse file_scope_volatile <<'EOF'
__asm__ volatile(".globl v1");
EOF
refuse file_scope_operand_number <<'EOF'
__asm__("movl %0, %eax");
EOF
refuse five_sections <<'EOF'
void f(void) { __asm__("" ::: "memory" : : "x"); }
EOF
refuse template_not_string <<'EOF'
void f(void) { __asm__("" :: "r"(1), 2); }
EOF
refuse goto_labels_without_goto <<'EOF'
void f(void) { __asm__("" :::: nowhere); }
EOF
refuse undeclared_label <<'EOF'
void f(void) { __asm__ goto("" :::: nowhere); }
EOF
refuse vla_jump <<'EOF'
int f(int n) { __asm__ goto("" :::: lab); int a[n]; lab: return a[0]; }
EOF

echo "== the two references disagree (informational; cxx follows clang)"
# gcc takes the bit-field's address and asks the assembler for a byte
# register, which the `movl` in the template cannot use; clang writes the
# field through its access unit and leaves the neighbouring field alone, and
# so does cxx.
note bitfield_out <<'EOF'
struct S { int a : 4, b : 4; };
int main(void) {
    struct S s = {0, 0};
    s.b = 3;
    __asm__("movl $5, %0" : "=r"(s.a));
    return s.a * 10 + s.b;
}
EOF

echo
echo "asm: $pass passed, $fail mismatch"
[ $fail -eq 0 ]
