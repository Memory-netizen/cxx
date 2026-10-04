// nullptr and nullptr_t (C23 6.4.2, 7.19.1).
//
// 6.3.2.4 conversion rules covered here: only a null pointer constant
// or a nullptr_t may be converted *to* nullptr_t (so the value is
// always null), and nullptr_t may only be converted to void, bool or a
// pointer type. The rejection of `(long)nullptr` is checked in
// test/error.sh.
#include <stddef.h>

#include "test.h"

int main() {
    // nullptr_t has pointer size.
    ASSERT(1, ({
               nullptr_t n;
               sizeof(n) == sizeof(void *);
           }));

    // Two nullptr_t values are equal, and equal a null pointer.
    ASSERT(1, ({
               nullptr_t a = nullptr;
               nullptr_t b = nullptr;
               a == b;
           }));
    ASSERT(1, ({
               nullptr_t n = nullptr;
               n == (void *)0;
           }));
    ASSERT(0, ({
               nullptr_t n = nullptr;
               n != nullptr;
           }));

    // nullptr converts to an object pointer and to a function pointer.
    ASSERT(1, ({
               nullptr_t n = nullptr;
               int *p = n;
               p == nullptr;
           }));
    ASSERT(1, ({
               nullptr_t n = nullptr;
               void (*f)(void) = n;
               f == nullptr;
           }));

    // nullptr is assignable directly to a pointer without the typedef.
    ASSERT(1, ({
               int *p = nullptr;
               p == nullptr;
           }));
    ASSERT(1, ({
               void *p = nullptr;
               p == nullptr;
           }));

    // A null nullptr_t is false in a condition.
    ASSERT(0, ({
               nullptr_t n = nullptr;
               n ? 1 : 0;
           }));

    // nullptr_t is a distinct type that survives through typedefs.
    ASSERT(1, ({
               typedef nullptr_t np;
               np n = nullptr;
               n == nullptr;
           }));

    // 6.3.2.4: a null pointer constant and a nullptr_t may both be
    // converted to nullptr_t; the result is always the null pointer.
    ASSERT(1, ({
               nullptr_t n = 0;
               n == nullptr;
           }));
    ASSERT(1, ({
               nullptr_t n = (nullptr_t)0;
               n == nullptr;
           }));
    ASSERT(1, ({
               nullptr_t a = nullptr;
               nullptr_t b = a;
               b == nullptr;
           }));

    // nullptr_t converts to void (no value produced) and to bool.
    // nullptr_t has exactly one value, so a bool conversion is always
    // false (6.2.5: it is a distinct scalar type).
    ASSERT(1, ({
               nullptr_t n = nullptr;
               (void)n;
               1;
           }));
    ASSERT(0, ({
               nullptr_t n = nullptr;
               (bool)n;
           }));
    ASSERT(0, ({
               nullptr_t n = nullptr;
               n ? 1 : 0;
           }));
    // Logical negation goes through a different irgen path than a bool
    // cast (ND_NOT vs cast), so cover it too.
    ASSERT(1, ({
               nullptr_t n = nullptr;
               !n;
           }));
    return 0;
}
