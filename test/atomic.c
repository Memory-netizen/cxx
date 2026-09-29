#include <stdatomic.h>

#include "test.h"

_Atomic int x;
_Atomic volatile int vx;
_Atomic(int *) ap;
_Atomic int ax = 5;
_Atomic char ac = 1;

struct S {
    _Atomic int a;
    int b;
};
struct S s;

int ld2(_Atomic int *p) { return *p; }
void st2(_Atomic int *p, int v) { *p = v; }

int main() {
    // Plain accesses on an _Atomic object are seq_cst atomic accesses.
    ASSERT(0, x);
    x = 1;
    ASSERT(1, x);
    ASSERT(2, ++x);
    ASSERT(2, x--);
    ASSERT(1, x);

    // Builtins: every order valid for the operation.
    __c11_atomic_store(&x, 10, __ATOMIC_RELAXED);
    ASSERT(10, __c11_atomic_load(&x, __ATOMIC_RELAXED));
    __c11_atomic_store(&x, 11, __ATOMIC_RELEASE);
    ASSERT(11, __c11_atomic_load(&x, __ATOMIC_ACQUIRE));
    __c11_atomic_store(&x, 12, __ATOMIC_SEQ_CST);
    ASSERT(12, __c11_atomic_load(&x, __ATOMIC_CONSUME));
    ASSERT(12, __c11_atomic_load(&x, memory_order_seq_cst));

    // The stdatomic.h macro forms.
    atomic_store(&x, 20);
    ASSERT(20, atomic_load(&x));
    atomic_store_explicit(&x, 21, memory_order_relaxed);
    ASSERT(21, atomic_load_explicit(&x, memory_order_relaxed));

    // Compound assignment and ++/-- on atomics.
    x = 40;
    x += 2;
    ASSERT(42, x);
    x -= 2;
    ASSERT(40, x);
    x *= 3;
    ASSERT(120, x);
    x >>= 2;
    ASSERT(30, x);
    x ^= 0xF;
    ASSERT(17, x);
    x &= 0x1F;
    ASSERT(17, x);
    x |= 0x20;
    ASSERT(49, x);

    // _Atomic volatile must lower to "atomic volatile" LLVM accesses.
    vx = 7;
    ASSERT(7, vx);
    __c11_atomic_store(&vx, 8, __ATOMIC_RELAXED);
    ASSERT(8, __c11_atomic_load(&vx, __ATOMIC_ACQUIRE));

    // _Atomic pointer.
    int v = 3;
    __c11_atomic_store(&ap, &v, __ATOMIC_SEQ_CST);
    int *p = __c11_atomic_load(&ap, __ATOMIC_SEQ_CST);
    ASSERT(1, p == &v);
    ASSERT(3, *p);

    // Static and local initialization (not an atomic operation).
    ASSERT(5, ax);
    ASSERT(1, ac);
    atomic_int loc = 9;
    ASSERT(9, loc);
    __c11_atomic_store(&loc, 10, __ATOMIC_RELAXED);
    ASSERT(10, __c11_atomic_load(&loc, __ATOMIC_RELAXED));

    // Atomic members: through an object and through a pointer.
    s.a = 4;
    ASSERT(4, s.a);
    __c11_atomic_store(&s.a, 6, __ATOMIC_RELAXED);
    ASSERT(6, __c11_atomic_load(&s.a, __ATOMIC_RELAXED));
    s.a = 30;
    ASSERT(31, ++s.a);
    ASSERT(31, ld2(&s.a));

    // Atomic through a pointer parameter.
    st2(&x, 50);
    ASSERT(50, ld2(&x));
    ASSERT(50, x);

    // sizeof keeps the underlying size.
    ASSERT(4, sizeof(_Atomic int));
    ASSERT(4, sizeof(atomic_int));
    ASSERT(8, sizeof(_Atomic(double)));

    printf("OK\n");
    return 0;
}
