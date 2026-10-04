#include <stdarg.h>

#include "test.h"

int ret3(void) {
    return 3;
    return 5;
}

int add2(int x, int y) { return x + y; }

int sub2(int x, int y) { return x - y; }

int add6(int a, int b, int c, int d, int e, int f) { return a + b + c + d + e + f; }

int addx(int *x, int y) { return *x + y; }

int sub_char(char a, char b, char c) { return a - b - c; }

int fib(int x) {
    if (x <= 1) return 1;
    return fib(x - 1) + fib(x - 2);
}

int sub_long(long a, long b, long c) { return a - b - c; }

int sub_short(short a, short b, short c) { return a - b - c; }

int g1;

int *g1_ptr(void) { return &g1; }
char int_to_char(int x) { return x; }

int div_long(long a, long b) { return a / b; }

_Bool bool_fn_add(_Bool x) { return x + 1; }
_Bool bool_fn_sub(_Bool x) { return x - 1; }

static int static_fn() { return 3; }

int param_decay(int x[]) { return x[0]; }
int param_decay2(int x[5]) { return sizeof x; }

int counter() {
    static int i;
    static int j = 1 + 1;
    return i++ + j++;
}

void ret_none() { return; }

_Bool true_fn();
_Bool false_fn();
char char_fn();
short short_fn();

unsigned char uchar_fn();
unsigned short ushort_fn();

signed char schar_fn();
short sshort_fn();
int omit1(int, int);
int omit(int, int b) { return b + 5; }

int add_all(int n, ...);
int read_first_int(int n, ...);
double read_first_double(int n, ...);

/* Checks both properties va_copy must have, encoding each result so that
 * a violation changes the value:
 *   first  -- read through the original (1st argument)
 *   same   -- read through the copy, which must still start where the
 *             original did, i.e. also yield the 1st argument
 *   second -- read through the copy again (2nd argument), which must not
 *             have moved the original
 */
static int copy_peek(int n, ...) {
    va_list ap, aq;
    va_start(ap, n);
    va_copy(aq, ap);
    int first = va_arg(ap, int);
    int same = va_arg(aq, int);
    int second = va_arg(aq, int);
    va_end(aq);
    va_end(ap);
    return first * 1000000 + same * 10000 + second;
}

double add_double(double x, double y);
float add_float(float x, float y);

float add_float3(float x, float y, float z) { return x + y + z; }

double add_double3(double x, double y, double z) { return x + y + z; }

int (*fnptr(int (*fn)(int n, ...)))(int, ...) { return fn; }

int param_decay3(int x()) { return x(); }

char *func_fn(void) { return __func__; }

char *function_fn(void) { return __FUNCTION__; }

double to_double(long double x) { return x; }

long double to_ldouble(int x) { return x; }

int main() {
    ASSERT(3, ret3());
    ASSERT(8, add2(3, 5));
    ASSERT(2, sub2(5, 3));
    ASSERT(21, add6(1, 2, 3, 4, 5, 6));
    ASSERT(66, add6(1, 2, add6(3, 4, 5, 6, 7, 8), 9, 10, 11));
    ASSERT(136, add6(1, 2, add6(3, add6(4, 5, 6, 7, 8, 9), 10, 11, 12, 13), 14, 15, 16));

    ASSERT(7, add2(3, 4));
    ASSERT(1, sub2(4, 3));
    ASSERT(55, fib(9));

    ASSERT(1, ({ sub_char(7, 3, 3); }));

    ASSERT(1, sub_long(7, 3, 3));
    ASSERT(1, sub_short(7, 3, 3));

    g1 = 3;

    ASSERT(3, *g1_ptr());
    ASSERT(5, int_to_char(261));
    ASSERT(5, int_to_char(261));
    ASSERT(-5, div_long(-10, 2));

    ASSERT(1, bool_fn_add(3));
    ASSERT(0, bool_fn_sub(3));
    ASSERT(1, bool_fn_add(-3));
    ASSERT(0, bool_fn_sub(-3));
    ASSERT(1, bool_fn_add(0));
    ASSERT(1, bool_fn_sub(0));

    ASSERT(3, static_fn());
    ASSERT(3, ({
               int x[2];
               x[0] = 3;
               param_decay(x);
           }));
    ASSERT(__SIZEOF_POINTER__, ({
               int x[2];
               param_decay2(x);
           }));

    ASSERT(3, ({
               int x[2];
               x[0] = 3;
               param_decay(x);
           }));

    ASSERT(2, counter());
    ASSERT(4, counter());
    ASSERT(6, counter());

    ret_none();

    ASSERT(1, true_fn());
    ASSERT(0, false_fn());
    ASSERT(3, char_fn());
    ASSERT(5, short_fn());
    ASSERT(12, omit(6, 7));

    ASSERT(6, add_all(3, 1, 2, 3));
    ASSERT(5, add_all(4, 1, 2, 3, -1));

    // === variadic arguments ===
    // More than the six general-purpose argument registers, so the reader
    // has to move from the register save area to the overflow area.
    ASSERT(36, add_all(8, 1, 2, 3, 4, 5, 6, 7, 8));
    ASSERT(55, add_all(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
    // The last named parameter still comes from its own register, so the
    // variadic area starts after it.
    ASSERT(15, add_all(5, 1, 2, 3, 4, 5));

    // Default argument promotions at the ABI level: the reader asks for
    // int/double, so a narrower argument must have been promoted by the
    // caller.
    ASSERT(7, read_first_int(1, 7));
    ASSERT(-1, read_first_int(1, -1));
    ASSERT(65, read_first_int(1, 'A'));
    // 0.5 is exactly representable, so the comparison is exact.
    ASSERT(1, read_first_double(1, 0.5) == 0.5);
    ASSERT(1, read_first_double(1, 2.5) == 2.5);
    // A float is promoted to double on the way in.
    ASSERT(1, read_first_double(1, (float)1.25) == 1.25);

    // va_copy must give an independent cursor.
    // 1*1000000 + 1*10000 + 2: the copy still held the 1st argument when
    // asked, and then advanced to the 2nd on its own.
    ASSERT(1010002, copy_peek(3, 1, 2, 3));
    // Only the first two variadic arguments are read (5 and 5).
    ASSERT(5050005, copy_peek(4, 5, 5, 6, 7));

    ASSERT(0, ({
               char buf[100];
               sprintf(buf, "%d %d %s", 1, 2, "foo");
               strcmp("1 2 foo", buf);
           }));

    ASSERT(251, uchar_fn());
    ASSERT(65528, ushort_fn());
    ASSERT(-5, schar_fn());
    ASSERT(-8, sshort_fn());

    ASSERT(6, add_float(2.3, 3.8));
    ASSERT(6, add_double(2.3, 3.8));

    ASSERT(7, add_float3(2.5, 2.5, 2.5));
    ASSERT(7, add_double3(2.5, 2.5, 2.5));

    ASSERT(0, ({
               char buf[100];
               sprintf(buf, "%.1f", (float)3.5);
               strcmp(buf, "3.5");
           }));

    ASSERT(5, (add2)(2, 3));
    ASSERT(5, (&add2)(2, 3));
    ASSERT(7, ({
               int (*fn)(int, int) = add2;
               fn(2, 5);
           }));
    ASSERT(6, fnptr(add_all)(3, 1, 2, 3));
    ASSERT(5, (***add2)(2, 3));

    ASSERT(3, param_decay3(ret3));

    ASSERT(5, sizeof(__func__));
    ASSERT(0, strcmp("main", __func__));
    ASSERT(0, strcmp("func_fn", func_fn()));

    ASSERT(0, strcmp("main", __FUNCTION__));
    ASSERT(0, strcmp("function_fn", function_fn()));

    ASSERT(1, to_double(3.5) == 3.5);
    ASSERT(0, to_double(3.5) == 3);

    ASSERT(1, (long double)5.0 == (long double)5.0);
    ASSERT(0, (long double)5.0 == (long double)5.2);

    ASSERT(1, to_ldouble(5.0) == 5.0);
    ASSERT(0, to_ldouble(5.0) == 5.2);

    // === functions with wide _BitInt / fp128 params and returns ===
    _BitInt(77) wsum = 0;
    for (int i = 1; i <= 5; i++) wsum = wsum + 1000000000000000000wb;
    ASSERT(5000000000000000000wb, wsum);
    _Float128 qprod = 1.0f128;
    for (int i = 0; i < 4; i++) qprod = qprod * 2.0f128;
    ASSERT(16.0f128, qprod);
    _Float16 hacc = 0.0f16;
    for (int i = 0; i < 4; i++) hacc = hacc + 0.25f16;
    ASSERT(1.0f16, hacc);

    printf("OK\n");
    return 0;
}
