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

    // === same byte size, different _BitInt widths: width wins ===
    ASSERT(1, sizeof((_BitInt(3))1 + (_BitInt(4))1));           // _BitInt(4)
    ASSERT(1, sizeof((_BitInt(4))1 + (_BitInt(3))1));           // _BitInt(4)
    ASSERT(1, sizeof((_BitInt(3))1 + (unsigned _BitInt(4))1));  // unsigned _BitInt(4)
    ASSERT(1, sizeof((unsigned _BitInt(4))1 + (_BitInt(3))1));  // unsigned _BitInt(4)
    ASSERT(1, sizeof((unsigned _BitInt(3))1 + (_BitInt(4))1));  // signed _BitInt(4): width beats unsigned
    ASSERT(1, sizeof((_BitInt(4))1 + (unsigned _BitInt(3))1));  // signed _BitInt(4)
    ASSERT(2, sizeof((_BitInt(8))1 + (_BitInt(9))1));           // _BitInt(9)
    ASSERT(2, sizeof((_BitInt(9))1 + (_BitInt(16))1));          // _BitInt(16)
    ASSERT(1, _Generic((_BitInt(3))1 + (_BitInt(4))1, _BitInt(4): 1, _BitInt(3): 0, default: 0));
    ASSERT(1, _Generic((unsigned _BitInt(3))1 + (_BitInt(4))1, _BitInt(4): 1, unsigned _BitInt(3): 0, default: 0));
    // wrap semantics still hold for the equal-width pairs
    ASSERT(-2, (_BitInt(3))3 + (_BitInt(3))3);

    // === char-family + _BitInt: both sides undergo the integer
    // promotions, so the result is int (clang-verified) ===
    ASSERT(4, sizeof((char)1 + (_BitInt(14))1));
    ASSERT(4, sizeof((unsigned char)1 + (_BitInt(14))1));
    ASSERT(4, sizeof((signed char)1 + (_BitInt(14))1));
    ASSERT(4, sizeof((char)1 + (unsigned _BitInt(14))1));
    ASSERT(4, sizeof((short)1 + (_BitInt(14))1));
    ASSERT(4, sizeof((unsigned short)1 + (unsigned _BitInt(14))1));
    ASSERT(4, sizeof((char)1 + (_BitInt(8))1));
    ASSERT(4, sizeof((unsigned char)1 + (unsigned _BitInt(8))1));
    ASSERT(4, sizeof((char)1 + (unsigned _BitInt(8))1));
    ASSERT(4, sizeof((_BitInt(7))1 + (char)1));
    ASSERT(4, sizeof((unsigned _BitInt(7))1 + (char)1));
    ASSERT(4, sizeof((char)1 + (unsigned _BitInt(31))1));  // int
    ASSERT(4, sizeof((char)1 + (_BitInt(32))1));           // int
    ASSERT(4, sizeof((char)1 + (unsigned _BitInt(32))1));  // unsigned int
    ASSERT(1, _Generic((char)1 + (_BitInt(14))1, int: 1, default: 0));
    ASSERT(1, _Generic((char)1 + (unsigned _BitInt(32))1, unsigned int: 1, default: 0));
    // no promotion above 32 bits: _BitInt(33) wins over char
    ASSERT(8, sizeof((char)1 + (_BitInt(33))1));
    ASSERT(1, _Generic((char)1 + (_BitInt(33))1, _BitInt(33): 1, default: 0));

    // === _BitInt vs long/llong (clang-verified: promote, then equal
    // width -> standard wins with unsigned resolution; standard
    // represents all values -> standard; else _BitInt wins) ===
    ASSERT(1, _Generic((_BitInt(33))1 + 1, _BitInt(33): 1, int: 0, default: 0));
    ASSERT(1, _Generic((_BitInt(63))1 + 1LL, long long: 1, _BitInt(63): 0, default: 0));
    ASSERT(1, _Generic((_BitInt(64))1 + 1LL, long long: 1, _BitInt(64): 0, default: 0));
    ASSERT(1, _Generic((unsigned _BitInt(64))1 + 1LL, unsigned long long: 1, unsigned _BitInt(64): 0, default: 0));
    ASSERT(1, _Generic((unsigned _BitInt(64))1 + 1ULL, unsigned long long: 1, default: 0));
    ASSERT(1, _Generic((_BitInt(65))1 + 1L, _BitInt(65): 1, long: 0, default: 0));
    ASSERT(1, _Generic((_BitInt(65))1 + 1ULL, _BitInt(65): 1, unsigned long long: 0, default: 0));
    ASSERT(1, _Generic((_BitInt(7))1 + 1L, long: 1, int: 0, default: 0));
    ASSERT(1, _Generic((unsigned _BitInt(7))1 + 1UL, unsigned long: 1, default: 0));
#if __SIZEOF_LONG__ == 8
    // LP64: long (64-bit) wins over _BitInt(33..64)
    ASSERT(1, _Generic((_BitInt(33))1 + 1L, long: 1, _BitInt(33): 0, default: 0));
    ASSERT(1, _Generic((unsigned _BitInt(33))1 + 1L, long: 1, unsigned _BitInt(33): 0, default: 0));
    ASSERT(1, _Generic((_BitInt(40))1 + 1UL, unsigned long: 1, _BitInt(40): 0, default: 0));
    ASSERT(1, _Generic((_BitInt(64))1 + 1L, long: 1, _BitInt(64): 0, default: 0));
    ASSERT(8, sizeof((_BitInt(33))1 + 1L));
#else
    // ILP32: long is 32-bit and cannot hold _BitInt(33+)
    ASSERT(1, _Generic((_BitInt(33))1 + 1L, _BitInt(33): 1, long: 0, default: 0));
    ASSERT(1, _Generic((_BitInt(40))1 + 1UL,
               _BitInt(40): 1,
               unsigned _BitInt(40): 0,
               default: 0));  // 32-bit ulong cannot hold it
    ASSERT(1, _Generic((_BitInt(64))1 + 1L, _BitInt(64): 1, long: 0, default: 0));
    ASSERT(8, sizeof((_BitInt(33))1 + 1L));
#endif
    // value semantics
    ASSERT(-23L, (_BitInt(7))100 + 5L);
    ASSERT(0UL, (_BitInt(7)) - 1 + 1UL);

    printf("OK\n");
    return 0;
}
