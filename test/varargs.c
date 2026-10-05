#include <stdarg.h>

#include "test.h"

/* Variadic function *definitions*: every function below reads its
 * arguments back through va_start / va_arg / va_end / va_copy, so the
 * program under test is the compiler's own expansion, not libc's printf.
 * The printf calls in test.h's assert only report the results. */

static int sum_ints(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int total = 0;
    for (int i = 0; i < n; i++) total += va_arg(ap, int);
    va_end(ap);
    return total;
}

/* Reading the named parameters in a different order than written. */
static int mixed(int tag, ...) {
    va_list ap;
    va_start(ap, tag);
    int i = va_arg(ap, int);
    double d = va_arg(ap, double);
    char *s = va_arg(ap, char *);
    va_end(ap);
    return i + (int)d + (int)strlen(s) + tag;
}

/* Default argument promotions: a float argument is passed as double and a
 * narrow integer as int (C23 6.5.2.2p7), so the callee reads them back at
 * the promoted types. */
static int promoted(int n, ...) {
    va_list ap;
    va_start(ap, n);
    double d = va_arg(ap, double);
    int i = va_arg(ap, int);
    va_end(ap);
    return (int)(d * 2) + i + n;
}

/* va_copy must leave two lists that advance independently: reading one
 * must not disturb the other. */
static int copied(int n, ...) {
    va_list ap, cp;
    va_start(ap, n);
    va_copy(cp, ap);

    int a = va_arg(ap, int);
    int b = va_arg(cp, int); /* the copy still sees the first argument */
    int c = va_arg(ap, int); /* the original advanced past it */
    int d = va_arg(cp, int); /* and the copy advances on its own */

    va_end(cp);
    va_end(ap);
    return a * 1000 + b * 100 + c * 10 + d;
}

/* A variadic function whose list is a bare "...": there is no last named
 * parameter, and va_start takes a single operand (C23 7.16.1.4). */
static int bare(...) {
    va_list ap;
    va_start(ap);
    int a = va_arg(ap, int);
    int b = va_arg(ap, int);
    va_end(ap);
    return a * 10 + b;
}

/* Each argument is read back at its own width. The results are compared
 * at their own types rather than accumulated into an int: converting a
 * double to a 64-bit integer needs a soft-float helper (__fixdfdi) that
 * the bare-metal rv32 harness does not provide, and what is under test
 * here is the width va_arg reads, not float conversion. */
/* Each argument is read back at its own width, with several widths in one
 * list: on rv32 this only works when the target's declared floating-point
 * ABI matches the code LLVM generates (see src/rv32/target.c). */
static int widths(int n, ...) {
    va_list ap;
    va_start(ap, n);
    long long a = va_arg(ap, long long);
    unsigned u = va_arg(ap, unsigned);
    long l = va_arg(ap, long);
    va_end(ap);
    return a == 1234567890123LL && u == 4000000000u && l == -7 && n == 4;
}

/* Enough arguments to leave the register save area on every target. */
static int many(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int total = 0;
    for (int i = 0; i < n; i++) total += va_arg(ap, int) * (i + 1);
    va_end(ap);
    return total;
}

int main() {
    ASSERT(6, sum_ints(3, 1, 2, 3));
    ASSERT(0, sum_ints(0));
    ASSERT(10, sum_ints(4, 1, 2, 3, 4));

    /* 100 + 5 + 5 + 7 */
    ASSERT(117, mixed(7, 100, 5.9, "abcde"));
    /* 31 + 0 + 3 */
    ASSERT(34, promoted(3, 15.5, 0));
    /* a=1 b=1 c=2 d=2 */
    ASSERT(1122, copied(2, 1, 2));

    ASSERT(12, bare(1, 2));
    ASSERT(7, bare(0, 7));

    /* several widths in one list, each read back at its own width */
    ASSERT(1, widths(4, 1234567890123LL, 4000000000u, -7L));

    ASSERT(1, many(1, 1));
    ASSERT(30, many(4, 1, 2, 3, 4));
    /* 1*1 + 2*2 + ... + 8*8 */
    ASSERT(204, many(8, 1, 2, 3, 4, 5, 6, 7, 8));

    /* A nested variadic call must not disturb the outer call's list. */
    ASSERT(15, sum_ints(2, 10, sum_ints(2, 2, 3)));

    printf("OK\n");
    return 0;
}
