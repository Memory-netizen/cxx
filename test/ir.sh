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
# outer operation.
echo '#include <stdatomic.h>
_Atomic int x, y;
void f(void) { x += __c11_atomic_load(&y, __ATOMIC_RELAXED); }' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/nest.ll
grep -q 'ptr @y monotonic, align 4' $tmp/nest.ll && grep -q 'ptr @x seq_cst, align 4' $tmp/nest.ll
check 'nested atomic order'

# compare_exchange: weak keyword, both orders, non-i32 types.
echo '#include <stdatomic.h>
_Atomic long x; long e;
int f(void) { return __c11_atomic_compare_exchange_weak(&x, &e, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED); }' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/cas.ll
grep -q 'cmpxchg weak ptr @x, i64 %' $tmp/cas.ll
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
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'store i32 %[0-9]*, ptr @e, align 4'
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

# Non-atomic accesses must stay atomic-free.
echo 'volatile int v; void f(void) { v = 1; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'store volatile i32 1, ptr @v, align 4'
check 'plain volatile store'
echo 'int x; void f(void) { x = 1; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'store i32 1, ptr @x, align 4'
check 'plain store'

echo OK
