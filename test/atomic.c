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

    // compare_exchange: failure stores the actual value into *expected
    // (C11 7.17.7.4p2), success leaves it untouched.
    int e;
    x = 3;
    e = 5;
    ASSERT(0, atomic_compare_exchange_strong(&x, &e, 7));
    ASSERT(3, e);
    ASSERT(3, x);

    e = 3;
    _Bool b = atomic_compare_exchange_strong(&x, &e, 9);
    ASSERT(1, b);
    ASSERT(3, e);
    ASSERT(9, x);

    // Explicit orders: release success / relaxed failure; acquire
    // success / consume failure (consume maps to acquire in LLVM).
    e = 9;
    ASSERT(1, atomic_compare_exchange_strong_explicit(&x, &e, 11, memory_order_release, memory_order_relaxed));
    ASSERT(9, e);
    ASSERT(11, x);

    e = 11;
    ASSERT(1, atomic_compare_exchange_weak_explicit(&x, &e, 13, memory_order_acquire, memory_order_consume));
    ASSERT(11, e);
    ASSERT(13, x);

    // weak may fail spuriously, so the classic retry loop must converge.
    e = 13;
    while (!atomic_compare_exchange_weak(&x, &e, 15)) {
    }
    ASSERT(15, x);
    ASSERT(13, e);

    // _Atomic volatile object.
    e = 8;
    vx = 8;
    ASSERT(1, atomic_compare_exchange_strong(&vx, &e, 9));
    ASSERT(9, vx);
    ASSERT(8, e);

    // _Atomic pointer: on failure *expected becomes the actual pointer.
    int pv1 = 1;
    int *ep = &pv1;
    ap = 0;
    ASSERT(0, atomic_compare_exchange_strong(&ap, &ep, &pv1));
    ASSERT(1, ep == 0);
    ep = 0;
    ASSERT(1, atomic_compare_exchange_strong(&ap, &ep, &pv1));
    ASSERT(1, ep == 0);
    ASSERT(1, ap == &pv1);

    // char.
    ac = 1;
    char ec = 2;
    ASSERT(0, atomic_compare_exchange_strong(&ac, &ec, 3));
    ASSERT(1, ec);
    ec = 1;
    ASSERT(1, atomic_compare_exchange_strong(&ac, &ec, 4));
    ASSERT(4, ac);

    // CAS through a pointer-variable object (lvalue conversion).
    _Atomic int *xp = &x;
    x = 60;
    e = 60;
    ASSERT(1, atomic_compare_exchange_strong(xp, &e, 61));
    ASSERT(61, x);
    ASSERT(60, e);

    // atomic_exchange returns the previous value (C11 7.17.7.4p2).
    x = 20;
    ASSERT(20, atomic_exchange(&x, 21));
    ASSERT(21, x);
    ASSERT(21, atomic_exchange_explicit(&x, 22, memory_order_release));
    ASSERT(22, x);
    ASSERT(22, atomic_exchange_explicit(&x, 23, memory_order_relaxed));
    ASSERT(23, x);

    // The object argument may be any pointer expression, not just &x.
    ASSERT(23, atomic_exchange(xp, 24));
    ASSERT(24, x);

    // _Atomic volatile, char, and pointer objects.
    vx = 30;
    ASSERT(30, atomic_exchange(&vx, 31));
    ASSERT(31, vx);

    ASSERT(4, atomic_exchange(&ac, 5));
    ASSERT(5, ac);

    int pv = 9;
    ap = 0;
    ASSERT(1, atomic_exchange(&ap, &pv) == 0);
    ASSERT(1, ap == &pv);

    // The result participates in larger expressions.
    x = 40;
    ASSERT(41, atomic_exchange(&x, 41) + 1);
    ASSERT(41, x);

    printf("OK\n");
    return 0;
}
