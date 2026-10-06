#!/bin/bash
# LLVM IR emission checks: language features verified through the
# emitted IR text (digraphs, atomics). Driver behavior lives in
# driver.sh; diagnostics live in error.sh.
compiler=$1

tmp=`mktemp -d /tmp/cxx-test-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT

check() {
    if [ $? -eq 0 ]; then
        echo "testing $1 ... passed"
    else
        echo "testing $1 ... failed"
        exit 1
    fi
}

# Digraphs
echo 'int main() <% return 0; %>' | $compiler -S -o - -xc - | grep -q 'main:'
check 'digraph <% %>'
printf 'int a<:2:> = <%%1,2%%>;\nint main() { return a<:0:> + a<:1:>; }\n' \
  | $compiler -S -o - -xc - | grep -q 'main:'
check 'digraph <: :>'
printf '%%:define M 42\nint x = M;\n' | $compiler -E -xc - | grep -q 'int x = 42;'
check 'digraph %:'
printf '#define CAT(a,b) a %%:%%: b\nCAT(x,y)\n' | $compiler -E -xc - | grep -q 'xy'
check 'digraph %:%:'

# Atomics: accesses to _Atomic objects become LLVM atomic ops; plain
# accesses default to seq_cst (C11 7.17.3 order -> LLVM order mapping).
echo '_Atomic int x; void f(void) { x = 1; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'store atomic i32 1, ptr @x seq_cst, align 4'
check 'atomic store default seq_cst'

echo '_Atomic int x; int f(void) { return x; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'load atomic i32, ptr @x seq_cst, align 4'
check 'atomic load default seq_cst'

echo '#include <stdatomic.h>
_Atomic int x;
void f(void) { __c11_atomic_store(&x, 1, __ATOMIC_RELEASE); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'ptr @x release, align 4'
check 'atomic store release'

echo '#include <stdatomic.h>
_Atomic int x;
void f(void) { __c11_atomic_store(&x, 1, __ATOMIC_RELAXED); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'ptr @x monotonic, align 4'
check 'atomic store relaxed'

echo '#include <stdatomic.h>
_Atomic int x;
int f(void) { return __c11_atomic_load(&x, __ATOMIC_ACQUIRE); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'load atomic i32, ptr @x acquire, align 4'
check 'atomic load acquire'

# LLVM dropped the consume order; it is emitted as acquire.
echo '#include <stdatomic.h>
_Atomic int x;
int f(void) { return __c11_atomic_load(&x, __ATOMIC_CONSUME); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'load atomic i32, ptr @x acquire, align 4'
check 'atomic load consume maps to acquire'

echo '#include <stdatomic.h>
_Atomic int x;
int f(void) { return __c11_atomic_load(&x, __ATOMIC_RELAXED); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'load atomic i32, ptr @x monotonic, align 4'
check 'atomic load relaxed'

# _Atomic volatile: "atomic" must precede "volatile" (LLVM syntax).
echo '_Atomic volatile int vx; void f(void) { vx = 3; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'store atomic volatile i32 3, ptr @vx seq_cst, align 4'
check 'atomic volatile store'
echo '_Atomic volatile int vx; int f(void) { return vx; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'load atomic volatile i32, ptr @vx seq_cst, align 4'
check 'atomic volatile load'
echo '_Atomic volatile int vx; void f(void) { vx = 3; }' \
  | $compiler -S -o - -xc - | grep -q 'vx:'
check 'atomic volatile end to end'

# A nested atomic access in the RHS must not leak its order onto the
# outer operation: the relaxed load keeps monotonic, the compound
# assignment itself stays seq_cst.
echo '#include <stdatomic.h>
_Atomic int x, y;
void f(void) { x += __c11_atomic_load(&y, __ATOMIC_RELAXED); }' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/nest.ll
grep -q 'ptr @y monotonic, align 4' $tmp/nest.ll && grep -qE 'atomicrmw add ptr %tmp[0-9]+, i32 %tmp[0-9]+ seq_cst' $tmp/nest.ll
check 'nested atomic order'

# compare_exchange: weak keyword, both orders, non-i32 types.
echo '#include <stdatomic.h>
_Atomic long x; long e;
int f(void) { return __c11_atomic_compare_exchange_weak(&x, &e, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED); }' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/cas.ll
grep -q 'cmpxchg weak ptr @x, i64 %tmp' $tmp/cas.ll
check 'cmpxchg weak i64'
grep -q 'acquire monotonic, align 8' $tmp/cas.ll
check 'cmpxchg success/failure orders'
grep -q 'extractvalue { i64, i1 } %' $tmp/cas.ll
check 'extractvalue i64 type'
grep -q 'zext i1 %' $tmp/cas.ll
check 'cmpxchg result zext to bool'

# On failure the actual value is stored back into *expected.
echo '#include <stdatomic.h>
_Atomic int x; int e;
int f(void) { return __c11_atomic_compare_exchange_strong(&x, &e, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'store i32 %tmp[0-9]*, ptr @e, align 4'
check 'cas failure writes back expected'

# _Atomic volatile: cmpxchg takes the volatile keyword.
echo '#include <stdatomic.h>
_Atomic volatile int vx; int e;
int f(void) { return __c11_atomic_compare_exchange_strong(&vx, &e, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'cmpxchg volatile ptr @vx, i32 %'
check 'cmpxchg volatile'

# release is a legal success order for cmpxchg (unlike plain stores).
echo '#include <stdatomic.h>
_Atomic int x; int e;
int f(void) { return __c11_atomic_compare_exchange_strong(&x, &e, 1, __ATOMIC_RELEASE, __ATOMIC_RELAXED); }' \
  | $compiler -S -emit-llvm -o - -xc - 2>/dev/null | grep -q 'release monotonic, align 4'
check 'cmpxchg release success order'

# atomic_exchange: atomicrmw xchg with the requested order.
echo '#include <stdatomic.h>
_Atomic int x;
int f(void) { return __c11_atomic_exchange(&x, 1, __ATOMIC_RELEASE); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'atomicrmw xchg ptr @x, i32 %'
check 'atomicrmw xchg'
echo '#include <stdatomic.h>
_Atomic int x;
int f(void) { return __c11_atomic_exchange(&x, 1, __ATOMIC_RELEASE); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q ' release, align 4'
check 'atomicrmw exchange release order'
echo '#include <stdatomic.h>
_Atomic int x;
int f(void) { return __c11_atomic_exchange(&x, 1, __ATOMIC_RELAXED); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q ' monotonic, align 4'
check 'atomicrmw exchange relaxed order'
echo '#include <stdatomic.h>
_Atomic volatile int vx;
int f(void) { return __c11_atomic_exchange(&vx, 1, __ATOMIC_SEQ_CST); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'atomicrmw volatile xchg ptr @vx, i32 %'
check 'atomicrmw volatile'

# fetch_*: atomicrmw op selection and operand types.
echo '#include <stdatomic.h>
_Atomic int x;
int f(void) { return __c11_atomic_fetch_add(&x, 1, __ATOMIC_SEQ_CST); }
int g(void) { return __c11_atomic_fetch_sub(&x, 1, __ATOMIC_SEQ_CST); }
int h(void) { return __c11_atomic_fetch_and(&x, 1, __ATOMIC_SEQ_CST); }
int i(void) { return __c11_atomic_fetch_or(&x, 1, __ATOMIC_SEQ_CST); }
int j(void) { return __c11_atomic_fetch_xor(&x, 1, __ATOMIC_SEQ_CST); }' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/fetch.ll
grep -q 'atomicrmw add ptr @x, i32 %' $tmp/fetch.ll
check 'atomicrmw fetch_add'
grep -q 'atomicrmw sub ptr @x, i32 %' $tmp/fetch.ll
check 'atomicrmw fetch_sub'
grep -q 'atomicrmw and ptr @x, i32 %' $tmp/fetch.ll
check 'atomicrmw fetch_and'
grep -q 'atomicrmw or ptr @x, i32 %' $tmp/fetch.ll
check 'atomicrmw fetch_or'
grep -q 'atomicrmw xor ptr @x, i32 %' $tmp/fetch.ll
check 'atomicrmw fetch_xor'

# fetch_add/sub on pointers take a pointer-sized integer operand;
# floating atomics use fadd/fsub.
echo '#include <stdatomic.h>
_Atomic(int *) ap;
int *f(void) { return __c11_atomic_fetch_add(&ap, 1, __ATOMIC_SEQ_CST); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'atomicrmw add ptr @ap, i64 %'
check 'atomicrmw fetch_add pointer operand'
echo '#include <stdatomic.h>
_Atomic float af;
float f(void) { return __c11_atomic_fetch_add(&af, 1.0f, __ATOMIC_SEQ_CST); }
float g(void) { return __c11_atomic_fetch_sub(&af, 0.5f, __ATOMIC_SEQ_CST); }' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/ffetch.ll
grep -q 'atomicrmw fadd ptr @af, float %' $tmp/ffetch.ll
check 'atomicrmw fadd'
grep -q 'atomicrmw fsub ptr @af, float %' $tmp/ffetch.ll
check 'atomicrmw fsub'

# Compound assignment on atomics (C11 6.5.16.2p3): +,-,&,|,^ use a
# single atomicrmw with a local recompute of the new value; the postfix
# ++ value is the atomicrmw result itself (no recompute).
echo '#include <stdatomic.h>
_Atomic int x;
int f(void) { x += 3; return x; }
int g(void) { x++; return x; }' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/casgn.ll
[ $(grep -c 'atomicrmw add ptr %tmp[0-9]*, i32 %' $tmp/casgn.ll) -eq 2 ]
check 'compound assign atomicrmw'
[ $(grep -c 'add i32 %' $tmp/casgn.ll) -eq 1 ]
check 'compound recompute only for +='

# *,/,%,<<,>> lower to a CAS loop.
echo '#include <stdatomic.h>
_Atomic int x;
int f(void) { x *= 2; return x; }' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/casloop.ll
grep -q 'cmpxchg ptr %tmp[0-9]*, i32 %' $tmp/casloop.ll
check 'compound assign cas loop'
grep -q 'extractvalue { i32, i1 } %' $tmp/casloop.ll
check 'cas loop extractvalue'
grep -q 'load atomic i32, ptr %tmp[0-9]* seq_cst' $tmp/casloop.ll
check 'cas loop atomic load'

# Pointer compound assignment uses a ptr cmpxchg loop.
echo '#include <stdatomic.h>
_Atomic(int *) ap;
int *f(void) { ap += 1; return ap; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'cmpxchg ptr %tmp[0-9]*, ptr %'
check 'pointer compound cas loop'

# Float += / -= use atomicrmw fadd/fsub.
echo '#include <stdatomic.h>
_Atomic float af;
float f(void) { af += 1.0f; return af; }
float g(void) { af -= 0.5f; return af; }' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/fcasgn.ll
grep -q 'atomicrmw fadd ptr %' $tmp/fcasgn.ll
check 'float compound fadd'
grep -q 'atomicrmw fsub ptr %' $tmp/fcasgn.ll
check 'float compound fsub'

# End to end: the transformed loop runs correctly.
echo '#include <stdatomic.h>
_Atomic int x;
int main() { x = 1; x += 2; x *= 3; return x == 9 ? 0 : 1; }' \
  | $compiler -o $tmp/cmpd -xc - && $tmp/cmpd
check 'compound assign end to end'

# alloca: emitted at the call site; plain alloca is an i8 array aligned
# to 16 bytes; with_align takes the alignment in bits; VLAs allocate
# their element type.
echo 'int g(int n) { int *p = __builtin_alloca(n); return *p; }
int h(int n) { int *p = __builtin_alloca_with_align(n, 32); return *p; }' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/alloca.ll
grep -q 'alloca i8, i64 %.*, align 16' $tmp/alloca.ll
check 'alloca i8 align 16'
grep -q 'alloca i8, i64 %.*, align 4' $tmp/alloca.ll
check 'alloca_with_align bits to bytes'
echo 'int f(int n) { int a[n]; return a[0]; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'alloca i32, i64 %'
check 'VLA typed alloca'

# Fences: thread fences emit plain fence instructions (consume maps to
# acquire); signal fences carry the singlethread syncscope.
echo '#include <stdatomic.h>
void f(void) {
  atomic_thread_fence(memory_order_seq_cst);
  atomic_thread_fence(memory_order_consume);
  atomic_thread_fence(memory_order_relaxed);
  atomic_signal_fence(memory_order_acquire);
}' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/fence.ll
grep -q 'fence seq_cst' $tmp/fence.ll
check 'thread fence seq_cst'
grep -q 'fence acquire' $tmp/fence.ll
check 'thread fence consume maps to acquire'
! grep -q 'fence monotonic' $tmp/fence.ll
check 'thread fence relaxed dropped'
grep -q 'fence syncscope("singlethread") acquire' $tmp/fence.ll
check 'signal fence singlethread syncscope'

# Floating compare_exchange: LLVM cmpxchg takes integer operands, so
# the floats are bitcast to an unsigned _BitInt of the same width and
# the failure writeback type-puns the integer into the float expected
# (clang emits the same shape).
echo '#include <stdatomic.h>
_Atomic float af;
float e;
int f(void) { float d = 1.5f; return atomic_compare_exchange_strong(&af, &e, d); }' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/fcas.ll
grep -q 'bitcast float %.* to i32' $tmp/fcas.ll
check 'float cas bitcast'
grep -q 'cmpxchg ptr @af, i32 %' $tmp/fcas.ll
check 'float cas i32 cmpxchg'
grep -q 'extractvalue { i32, i1 } %' $tmp/fcas.ll
check 'float cas extractvalue'
grep -q 'store i32 %.*, ptr @e' $tmp/fcas.ll
check 'float cas writeback type pun'

echo '#include <stdatomic.h>
_Atomic double ad;
double e;
int f(void) { double d = 1.5; return atomic_compare_exchange_strong(&ad, &e, d); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'bitcast double %.* to i64'
check 'double cas bitcast'

echo '#include <stdatomic.h>
_Atomic _Float128 af;
_Float128 e;
int f(void) { _Float128 d = 1.5; return atomic_compare_exchange_strong(&af, &e, d); }' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/f128cas.ll
grep -q 'bitcast fp128 %.* to i128' $tmp/f128cas.ll
check 'fp128 cas bitcast'
grep -q 'cmpxchg ptr @af, i128 %' $tmp/f128cas.ll
check 'fp128 cas i128 cmpxchg'

# Float compound *=, /= reuse the bitcast CAS loop.
echo '#include <stdatomic.h>
_Atomic float af;
float f(void) { af *= 2.0f; return af; }' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/fcasgn.ll
grep -q 'bitcast float %.* to i32' $tmp/fcasgn.ll
check 'float *= bitcast loop'
grep -q 'cmpxchg ptr %.*, i32 %' $tmp/fcasgn.ll
check 'float *= cmpxchg'
grep -q 'fmul float' $tmp/fcasgn.ll
check 'float *= fmul'

# Whole access to _Atomic aggregates moves the bit pattern through a
# same-size integer (clang emits the same shape).
echo '#include <stdatomic.h>
struct S { int a, b; };
_Atomic struct S x, y;
void f(void) { x = y; }' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/atagg.ll
grep -q 'load atomic i64, ptr @y seq_cst' $tmp/atagg.ll
check 'atomic aggregate load i64'
grep -q 'store atomic i64 %.*, ptr @x seq_cst' $tmp/atagg.ll
check 'atomic aggregate store i64'
echo '#include <stdatomic.h>
struct S { int a, b; };
_Atomic struct S y;
void f(void) { _Atomic struct S x = y; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'load atomic i64, ptr @y seq_cst'
check 'atomic aggregate init reads atomically'

# Non-atomic accesses must stay atomic-free.
echo 'volatile int v; void f(void) { v = 1; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'store volatile i32 1, ptr @v, align 4'
check 'plain volatile store'
echo 'int x; void f(void) { x = 1; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'store i32 1, ptr @x, align 4'
check 'plain store'

echo 'struct __attribute__((packed)) P { char c; int i; };
int f(struct P *p) { p->i = 3; return p->i; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'align 1'
check 'packed member access align 1'
echo 'int x __attribute__((aligned(16)));' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q '@x = .*align 16'
check 'global aligned(16)'

# A null pointer constant is a constant expression with the value 0
# (6.3.2.3), and its conversion yields a null pointer rather than a pointer
# built from an integer. Emitting inttoptr there put an extra instruction in
# front of every `p == 0`, `p != 0` and `p = 0`.
echo 'int f(void *p) { return p != 0; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'icmp ne ptr .*, null'
check 'a null pointer constant converts to null'
echo 'void *f(long n) { return (void *)n; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q inttoptr
check 'a non-constant integer still converts with inttoptr'

echo OK
