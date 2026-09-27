#include "test.h"

int main() {
    ASSERT(1, _Generic(100.0, double: 1, int *: 2, int: 3, float: 4));
    ASSERT(2, _Generic((int *)0, double: 1, int *: 2, int: 3, float: 4));
    ASSERT(2, _Generic((int[3]){}, double: 1, int *: 2, int: 3, float: 4));
    ASSERT(3, _Generic(100, double: 1, int *: 2, int: 3, float: 4));
    ASSERT(4, _Generic(100.f, double: 1, int *: 2, int: 3, float: 4));

    // === _Generic with the extended types ===
    _BitInt(77) b = 1;
    unsigned _BitInt(3) ub3 = 1;
    _Float16 h = 1.0f16;
    _Float128 q = 1.0f128;
    ASSERT(1, _Generic(b, _BitInt(77): 1, default: 0));
    ASSERT(1, _Generic(ub3, unsigned _BitInt(3): 1, default: 0));
    ASSERT(2, _Generic(ub3, _BitInt(3): 1, unsigned _BitInt(3): 2, default: 0));
    ASSERT(1, _Generic(h, _Float16: 1, default: 0));
    ASSERT(1, _Generic(q, _Float128: 1, default: 0));
    ASSERT(2, _Generic(q, _Float16: 1, _Float128: 2, default: 0));
    ASSERT(3, _Generic(b + 1, _BitInt(77): 3, default: 0));                         // int converts to _BitInt(77)
    ASSERT(1, _Generic((_BitInt(3))1 + 1, int: 1, default: 0));                     // promoted
    ASSERT(2, _Generic((_BitInt(3))1 + (_BitInt(3))1, _BitInt(3): 2, default: 0));  // stays
    ASSERT(1, _Generic(-(_BitInt(3))1, _BitInt(3): 1, default: 0));                 // no unary promotion

    printf("OK\n");
    return 0;
}
