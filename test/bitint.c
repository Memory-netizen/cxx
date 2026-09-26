#include <float.h>

#include "test.h"

int main() {
    // Sizes and alignments
    ASSERT(1, sizeof(_BitInt(8)));
    ASSERT(4, sizeof(_BitInt(20)));
    ASSERT(16, sizeof(_BitInt(65)));
    ASSERT(16, sizeof(_BitInt(128)));
    ASSERT(1, sizeof(unsigned _BitInt(1)));
    ASSERT(16, _Alignof(_BitInt(70)));

    // wb / uwb literal suffixes
    ASSERT(1, 1wb == (_BitInt(2))1);
    ASSERT(1, 1uwb == (unsigned _BitInt(1))1);
    ASSERT(3, (int)3wb);
    ASSERT(1, 127wb == (_BitInt(8))127);
    ASSERT(1, 128wb == (_BitInt(9))128);
    ASSERT(1, _Generic(1wb, _BitInt(2): 1, default: 0));
    ASSERT(1, _Generic(1uwb, unsigned _BitInt(1): 1, default: 0));

    // Wide literals beyond 64 bits
    ASSERT(1, 0x123456789abcdef01wb == (_BitInt(66))0x123456789abcdef01wb);

    // Arithmetic with wrap semantics
    ASSERT(1, (unsigned _BitInt(8))255 + 1 == 256);  // promotes to int
    ASSERT(1, (_BitInt(8))127 + 1 == 128);           // promotes to int
    ASSERT(1, (_BitInt(8)) - 128 - 1 == -129);
    ASSERT(1, (_BitInt(70))7 * 9 == 63);
    ASSERT(1, (_BitInt(70)) - 7 / 2 == -3);
    ASSERT(1, (_BitInt(70))7 % 3 == 1);
    ASSERT(1, (_BitInt(70)) - 7 % 3 == -1);

    // Wide arithmetic above 64 bits
    ASSERT(1, ((_BitInt(70))1 << 65) + 1 > 0);
    ASSERT(1, (((_BitInt(70))1 << 65) >> 65) == 1);
    ASSERT(1, ((unsigned _BitInt(128)) - 1) >> 127 == 1);
    ASSERT(1, (_BitInt(128)) - 1 >> 127 == -1);

    // Comparisons and logic
    ASSERT(1, (_BitInt(70))1 < (_BitInt(70))2);
    ASSERT(1, (unsigned _BitInt(70)) - 1 > 0);
    ASSERT(1, ~(_BitInt(70))0 == -1);
    ASSERT(1, ((_BitInt(70))5 & 3) == 1);
    ASSERT(1, ((_BitInt(70))5 | 3) == 7);
    ASSERT(1, ((_BitInt(70))5 ^ 3) == 6);

    // Casts
    ASSERT(5, (int)(_BitInt(70))5);
    ASSERT(1, (_BitInt(70))5 == 5);
    ASSERT(1, (_BitInt(8))300 == 44);                // truncation
    ASSERT(1, (_BitInt(70))(_BitInt(8)) - 1 == -1);  // sign extension
    ASSERT(1, (_BitInt(8)) - 1 == -1);

    // Mixed with standard integers follows the wider type
    ASSERT(1, _Generic((_BitInt(40))1 + 2, _BitInt(40): 1, default: 0));

    // Promotion of small _BitInt to int (C23 6.3.1.1)
    ASSERT(1, _Generic((_BitInt(8))1 + 1, int: 1, default: 0));
    ASSERT(1, _Generic((unsigned _BitInt(8))1 + 1, int: 1, default: 0));

    // Array indexing
    _BitInt(70) a[2] = {5, 6};
    ASSERT(6, a[1]);
    ASSERT(1, &a[1] - &a[0] == 1);

    printf("OK\n");
    return 0;
}
