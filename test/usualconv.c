#include "test.h"

static int ret10(void) { return 10; }

int main() {
    ASSERT((long)-5, -10 + (long)5);
    ASSERT((long)-15, -10 - (long)5);
    ASSERT((long)-50, -10 * (long)5);
    ASSERT((long)-2, -10 / (long)5);

    ASSERT(1, -2 < (long)-1);
    ASSERT(1, -2 <= (long)-1);
    ASSERT(0, -2 > (long)-1);
    ASSERT(0, -2 >= (long)-1);

    ASSERT(1, (long)-2 < -1);
    ASSERT(1, (long)-2 <= -1);
    ASSERT(0, (long)-2 > -1);
    ASSERT(0, (long)-2 >= -1);

    ASSERT(0, 2147483647 + 2147483647 + 2);
    ASSERT((long)-1, ({
               long x;
               x = -1;
               x;
           }));

    ASSERT(1, ({
               char x[3];
               x[0] = 0;
               x[1] = 1;
               x[2] = 2;
               char *y = x + 1;
               y[0];
           }));
    ASSERT(0, ({
               char x[3];
               x[0] = 0;
               x[1] = 1;
               x[2] = 2;
               char *y = x + 1;
               y[-1];
           }));
    ASSERT(5, ({
               struct t {
                   char a;
               } x, y;
               x.a = 5;
               y = x;
               y.a;
           }));

    ASSERT(10, (1 ? ret10 : (void *)0)());

    // === _BitInt usual arithmetic conversions (C23 6.3.1.8) ===
    // same-type pairs stay _BitInt (wrapping), wider/unsigned wins
    ASSERT(1, sizeof((_BitInt(3))1 + (_BitInt(3))1));
    ASSERT(1, sizeof((unsigned _BitInt(4))1 + (unsigned _BitInt(4))1));
    ASSERT(1, sizeof((_BitInt(3))1 + (unsigned _BitInt(3))1));  // unsigned wins
    ASSERT(1, sizeof((_BitInt(3))1 + (unsigned _BitInt(4))1));  // wider wins
    ASSERT(2, sizeof((_BitInt(9))1 + (_BitInt(9))1));
    // mixed with int: promoted to int when it fits
    ASSERT(4, sizeof((_BitInt(3))1 + 1));
    ASSERT(4, sizeof((unsigned _BitInt(4))1 + 1));
    ASSERT(4, sizeof((unsigned _BitInt(32))1 + 1));  // -> unsigned int
    // wide _BitInt wins over int
    ASSERT(8, sizeof((_BitInt(40))1 + 1));
    ASSERT(16, sizeof((_BitInt(77))1 + 1));
    // value semantics of the conversion
    ASSERT(-2, (_BitInt(3))3 + (_BitInt(3))3);
    ASSERT(6, (_BitInt(3)) - 1 + (unsigned _BitInt(3))7);
    ASSERT(16, (unsigned _BitInt(4))15 + 1);
    ASSERT(14, (unsigned _BitInt(4))15 + (unsigned _BitInt(4))15);
    // unary minus does not promote _BitInt
    ASSERT(1, sizeof(-(_BitInt(3))1));
    ASSERT(-4, -(_BitInt(3)) - 4);

    printf("OK\n");
    return 0;
}
