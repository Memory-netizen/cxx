#include "test.h"

/* Passing and returning records by value. test/struct.c covers records as
 * objects; this covers them as function values, which the compiler had no
 * coverage for at all until the calling convention was fixed -- every case
 * below used to be rejected or miscompiled. */

struct Pair {
    int a;
    int b;
};

struct Quad {
    long a;
    long b;
    long c;
    long d;
};

struct Fp {
    double x;
    double y;
};

struct Nested {
    struct Pair inner;
    int tail;
};

union U {
    int i;
    double d;
};

static struct Pair make_pair(int a, int b) {
    struct Pair p;
    p.a = a;
    p.b = b;
    return p;
}

static struct Quad make_quad(long a, long b, long c, long d) {
    struct Quad q;
    q.a = a;
    q.b = b;
    q.c = c;
    q.d = d;
    return q;
}

static struct Fp make_fp(double x, double y) {
    struct Fp f;
    f.x = x;
    f.y = y;
    return f;
}

static struct Nested make_nested(int a, int b, int tail) {
    struct Nested n;
    n.inner.a = a;
    n.inner.b = b;
    n.tail = tail;
    return n;
}

static int take_pair(struct Pair p) { return p.a * 100 + p.b; }

static long take_quad(struct Quad q) { return q.a + q.b * 10 + q.c * 100 + q.d * 1000; }

static double take_fp(struct Fp f) { return f.x + f.y; }

static int take_nested(struct Nested n) { return n.inner.a + n.inner.b + n.tail; }

static struct Pair bump(struct Pair p) {
    p.a++;
    p.b++;
    return p;
}

int main() {
    /* return, then read through the result */
    ASSERT(12, ({
               struct Pair p = make_pair(1, 2);
               p.a * 10 + p.b;
           }));
    ASSERT(1, make_pair(1, 2).a);
    ASSERT(2, make_pair(1, 2).b);

    /* return a record larger than two registers */
    ASSERT(1, ({
               struct Quad q = make_quad(1, 2, 3, 4);
               q.a;
           }));
    ASSERT(4321, ({
               struct Quad q = make_quad(1, 2, 3, 4);
               q.a + q.b * 10 + q.c * 100 + q.d * 1000;
           }));
    ASSERT(3, make_quad(1, 2, 3, 4).c);
    ASSERT(4, make_quad(1, 2, 3, 4).d);

    /* floating-point members */
    ASSERT(1, make_fp(1.5, 2.5).x == 1.5);
    ASSERT(1, make_fp(1.5, 2.5).y == 2.5);

    /* a record containing a record */
    ASSERT(11, ({
               struct Nested n = make_nested(1, 2, 8);
               n.inner.a + n.inner.b + n.tail;
           }));
    ASSERT(1, make_nested(1, 2, 8).inner.a);
    ASSERT(8, make_nested(1, 2, 8).tail);

    /* passing a record by value */
    ASSERT(102, take_pair(make_pair(1, 2)));
    ASSERT(102, ({
               struct Pair p = make_pair(1, 2);
               take_pair(p);
           }));
    ASSERT(4321, take_quad(make_quad(1, 2, 3, 4)));
    ASSERT(1, take_fp(make_fp(1.5, 2.5)) == 4.0);
    ASSERT(11, take_nested(make_nested(1, 2, 8)));

    /* the callee's copy is its own: modifying it must not touch the
     * caller's object (6.5.2.2p4) */
    ASSERT(102, ({
               struct Pair p = make_pair(1, 2);
               take_pair(p); /* the callee's copy is discarded */
               p.a * 100 + p.b;
           }));

    /* argument and result in one expression */
    ASSERT(203, take_pair(bump(make_pair(1, 2))));
    ASSERT(2, bump(make_pair(1, 2)).a);
    ASSERT(1, ({
               struct Pair p = make_pair(1, 2);
               bump(p);
               p.a; /* unchanged: bump worked on a copy */
           }));

    /* a record returned by value used as an aggregate initializer */
    ASSERT(102, ({
               struct Pair p = make_pair(1, 2);
               struct Pair q = p;
               q.a * 100 + q.b;
           }));

    /* unions travel the same path */
    ASSERT(7, ({
               union U u;
               u.i = 7;
               u.i;
           }));

    printf("OK\n");
    return 0;
}
