#include "test.h"

int main() {
    ASSERT(3, ({
               typeof(int) x = 3;
               x;
           }));
    ASSERT(3, ({
               typeof(1) x = 3;
               x;
           }));
    ASSERT(4, ({
               int x;
               typeof(x) y;
               sizeof(y);
           }));
    ASSERT(__SIZEOF_POINTER__, ({
               int x;
               typeof(&x) y;
               sizeof(y);
           }));
    ASSERT(4, ({
               typeof("foo") x;
               sizeof(x);
           }));
    ASSERT(12, sizeof(typeof(struct { int a, b, c; })));

    ASSERT(42, ({
               typeof_unqual(const int) x;
               x = 42;
               x;
           }));

    // === typeof with the extended types ===
    _BitInt(77) tb = 1;
    typeof(tb) tb2 = 2;
    ASSERT(1, tb + tb2 == 3);
    typeof(tb + tb) tsame = 0;  // same-type add stays _BitInt(77)
    ASSERT(16, sizeof(tsame));
    typeof(tb + 1) tprom = 0;  // mixed with int stays _BitInt(77) (int cannot hold it)
    ASSERT(16, sizeof(tprom));
    typeof((_BitInt(3))1 + (_BitInt(3))1) tsmall = 0;
    ASSERT(1, sizeof(tsmall));
    typeof((_BitInt(3))1 + 1) tint = 0;
    ASSERT(4, sizeof(tint));
    _Float128 tq = 1.0f128;
    typeof(tq) tq2 = 2.0f128;
    ASSERT(1, tq + tq2 == 3.0f128);
    typeof(tq + tq) tqs = 0;
    ASSERT(16, sizeof(tqs));

    printf("OK\n");
    return 0;
}
