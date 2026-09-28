#include "test.h"

int main() {
    {
        int const x;
    }
    {
        const int x;
    }
    {
        const int const const x;
    }
    ASSERT(5, ({
               const int x = 5;
               x;
           }));
    ASSERT(8, ({
               const int x = 8;
               const int *const y = &x;
               *y;
           }));
    ASSERT(6, ({
               const int x = 6;
               *(int *const)&x;
           }));

    // Q_MEMCONST: a struct/union with a const member is not assignable
    // as a whole, but initialization and member reads are fine
    {
        struct S {
            const int x;
        } s = {7};
        ASSERT(7, s.x);
    }
    {
        struct S {
            const int a[2];
        } s = {{8, 9}};
        ASSERT(17, s.a[0] + s.a[1]);
    }
    {
        struct Inner {
            const int x;
        };
        struct Outer {
            struct Inner in;
            const int y;
        } o = {{10}, 11};
        ASSERT(21, o.in.x + o.y);
    }
    {
        union U {
            const int x;
            int y;
        } u = {12};
        ASSERT(12, u.x);
    }
    {
        struct S {
            const int *p;  // pointee const: S stays assignable
        } s1 = {0}, s2 = {0};
        s1 = s2;
        ASSERT(1, 1);
    }
    // const on the aggregate object propagates down to its members:
    // initialization and reads work, writes are rejected at compile time
    {
        const struct {
            int x;
        } y = {13};
        ASSERT(13, y.x);
    }
    {
        struct B {
            int x;
        };
        const struct {
            struct B b;
            int a[2];
        } y = {{14}, {15, 16}};
        ASSERT(45, y.b.x + y.a[0] + y.a[1]);
    }

    printf("OK\n");
    return 0;
}
