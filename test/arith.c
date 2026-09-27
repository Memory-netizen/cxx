#include "test.h"

int main() {
    ASSERT(0, 0);
    ASSERT(42, 42);
    ASSERT(21, 5 + 20 - 4);
    ASSERT(41, 12 + 34 - 5);
    ASSERT(47, 5 + 6 * 7);
    ASSERT(15, 5 * (9 - 6));
    ASSERT(4, (3 + 5) / 2);
    ASSERT(10, -10 + 20);
    ASSERT(10, - -10);
    ASSERT(10, - -+10);

    ASSERT(0, 0 == 1);
    ASSERT(1, 42 == 42);
    ASSERT(1, 0 != 1);
    ASSERT(0, 42 != 42);

    ASSERT(1, 0 < 1);
    ASSERT(0, 1 < 1);
    ASSERT(0, 2 < 1);
    ASSERT(1, 0 <= 1);
    ASSERT(1, 1 <= 1);
    ASSERT(0, 2 <= 1);

    ASSERT(1, 1 > 0);
    ASSERT(0, 1 > 1);
    ASSERT(0, 1 > 2);
    ASSERT(1, 1 >= 0);
    ASSERT(1, 1 >= 1);
    ASSERT(0, 1 >= 2);
    ASSERT(0, 1073741824 * 100 / 100);

    ASSERT(7, ({
               int i = 2;
               i += 5;
               i;
           }));
    ASSERT(7, ({
               int i = 2;
               i += 5;
           }));
    ASSERT(3, ({
               int i = 5;
               i -= 2;
               i;
           }));
    ASSERT(3, ({
               int i = 5;
               i -= 2;
           }));
    ASSERT(6, ({
               int i = 3;
               i *= 2;
               i;
           }));
    ASSERT(6, ({
               int i = 3;
               i *= 2;
           }));
    ASSERT(3, ({
               int i = 6;
               i /= 2;
               i;
           }));
    ASSERT(3, ({
               int i = 6;
               i /= 2;
           }));

    ASSERT(3, ({
               int i = 2;
               ++i;
           }));
    ASSERT(2, ({
               int a[3];
               a[0] = 0;
               a[1] = 1;
               a[2] = 2;
               int *p = a + 1;
               ++*p;
           }));
    ASSERT(0, ({
               int a[3];
               a[0] = 0;
               a[1] = 1;
               a[2] = 2;
               int *p = a + 1;
               --*p;
           }));

    ASSERT(2, ({
               int i = 2;
               i++;
           }));
    ASSERT(2, ({
               int i = 2;
               i--;
           }));
    ASSERT(3, ({
               int i = 2;
               i++;
               i;
           }));
    ASSERT(1, ({
               int i = 2;
               i--;
               i;
           }));
    ASSERT(1, ({
               int a[3];
               a[0] = 0;
               a[1] = 1;
               a[2] = 2;
               int *p = a + 1;
               *p++;
           }));
    ASSERT(1, ({
               int a[3];
               a[0] = 0;
               a[1] = 1;
               a[2] = 2;
               int *p = a + 1;
               *p--;
           }));

    ASSERT(0, ({
               int a[3];
               a[0] = 0;
               a[1] = 1;
               a[2] = 2;
               int *p = a + 1;
               (*p++)--;
               a[0];
           }));
    ASSERT(0, ({
               int a[3];
               a[0] = 0;
               a[1] = 1;
               a[2] = 2;
               int *p = a + 1;
               (*(p--))--;
               a[1];
           }));
    ASSERT(2, ({
               int a[3];
               a[0] = 0;
               a[1] = 1;
               a[2] = 2;
               int *p = a + 1;
               (*p)--;
               a[2];
           }));
    ASSERT(2, ({
               int a[3];
               a[0] = 0;
               a[1] = 1;
               a[2] = 2;
               int *p = a + 1;
               (*p)--;
               p++;
               *p;
           }));
    ASSERT(0, ({
               int a[3];
               a[0] = 0;
               a[1] = 1;
               a[2] = 2;
               int *p = a + 1;
               (*p++)--;
               a[0];
           }));
    ASSERT(0, ({
               int a[3];
               a[0] = 0;
               a[1] = 1;
               a[2] = 2;
               int *p = a + 1;
               (*p++)--;
               a[1];
           }));
    ASSERT(2, ({
               int a[3];
               a[0] = 0;
               a[1] = 1;
               a[2] = 2;
               int *p = a + 1;
               (*p++)--;
               a[2];
           }));
    ASSERT(2, ({
               int a[3];
               a[0] = 0;
               a[1] = 1;
               a[2] = 2;
               int *p = a + 1;
               (*p++)--;
               *p;
           }));

    ASSERT(0, !1);
    ASSERT(0, !2);
    ASSERT(1, !0);
    ASSERT(1, !(char)0);
    ASSERT(0, !(long)3);
    ASSERT(4, sizeof(!(char)0));
    ASSERT(4, sizeof(!(long)0));

    ASSERT(-1, ~0);
    ASSERT(0, ~-1);

    ASSERT(5, 17 % 6);
    ASSERT(5, ((long)17) % 6);
    ASSERT(2, ({
               int i = 10;
               i %= 4;
               i;
           }));
    ASSERT(2, ({
               long i = 10;
               i %= 4;
               i;
           }));

    ASSERT(0, 0 & 1);
    ASSERT(1, 3 & 1);
    ASSERT(3, 7 & 3);
    ASSERT(10, -1 & 10);

    ASSERT(1, 0 | 1);
    ASSERT(0b10011, 0b10000 | 0b00011);

    ASSERT(0, 0 ^ 0);
    ASSERT(0, 0b1111 ^ 0b1111);
    ASSERT(0b110100, 0b111000 ^ 0b001100);

    ASSERT(2, ({
               int i = 6;
               i &= 3;
               i;
           }));
    ASSERT(7, ({
               int i = 6;
               i |= 3;
               i;
           }));
    ASSERT(10, ({
               int i = 15;
               i ^= 5;
               i;
           }));
    ASSERT(1, 1 << 0);
    ASSERT(8, 1 << 3);
    ASSERT(10, 5 << 1);
    ASSERT(2, 5 >> 1);
    ASSERT(-1, -1 >> 1);
    ASSERT(1, ({
               int i = 1;
               i <<= 0;
               i;
           }));
    ASSERT(8, ({
               int i = 1;
               i <<= 3;
               i;
           }));
    ASSERT(10, ({
               int i = 5;
               i <<= 1;
               i;
           }));
    ASSERT(2, ({
               int i = 5;
               i >>= 1;
               i;
           }));
    ASSERT(-1, -1);
    ASSERT(-1, ({
        int i = -1;
        i;
    }));
    ASSERT(-1, ({
        int i = -1;
        i >>= 1;
        i;
    }));

    ASSERT(2, 0 ? 1 : 2);
    ASSERT(1, 1 ? 1 : 2);
    ASSERT(-1, 0 ? -2 : -1);
    ASSERT(-2, 1 ? -2 : -1);
    ASSERT(4, sizeof(0 ? 1 : 2));
    ASSERT(__SIZEOF_LONG__, sizeof(0 ? (long)1 : (long)2));
    ASSERT(-1, 0 ? (long)-2 : -1);
    ASSERT(-1, 0 ? -2 : (long)-1);
    ASSERT(-2, 1 ? (long)-2 : -1);
    ASSERT(-2, 1 ? -2 : (long)-1);
    1 ? -2 : (void)-1;

    ASSERT(20, ({
               int x;
               int *p = &x;
               p + 20 - p;
           }));
    ASSERT(1, ({
               int x;
               int *p = &x;
               p + 20 - p > 0;
           }));
    ASSERT(-20, ({
        int x;
        int *p = &x;
        p - 20 - p;
    }));
    ASSERT(1, ({
               int x;
               int *p = &x;
               p - 20 - p < 0;
           }));
    ASSERT(15, (char *)0xffffffffffffffff - (char *)0xfffffffffffffff0);
    ASSERT(-15, (char *)0xfffffffffffffff0 - (char *)0xffffffffffffffff);

    ASSERT(1, (void *)0xffffffffffffffff > (void *)0);

    ASSERT(2, true + true);
    ASSERT(1, true + false);
    ASSERT(6, true + 5);

    ASSERT(3, 3 ?: 5);
    ASSERT(5, 0 ?: 5);
    ASSERT(4, ({
               int i = 3;
               ++i ?: 10;
           }));

    ASSERT(3, (long double)3);
    ASSERT(5, (long double)3 + 2);
    ASSERT(6, (long double)3 * 2);
    ASSERT(5, (long double)3 + 2.0);

    // === Wide _BitInt(77) runtime arithmetic (wraps modulo 2^77) ===
    _BitInt(77) w1 = 1000000000000000000wb;  // 10^18, exact in 77 bits
    ASSERT(1, w1 * 2 > w1);
    ASSERT(1, w1 + 1 > w1);
    ASSERT(0, w1 - w1);
    ASSERT(0, w1 * 0);
    ASSERT(1, -w1 < 0);
    ASSERT(1, w1 / 2 == 500000000000000000wb);
    ASSERT(0, w1 % 2);
    unsigned _BitInt(77) wu = (unsigned _BitInt(77)) - 1;  // 2^77-1
    ASSERT(0, wu + 1);                                     // wraps to 0
    ASSERT(1, wu % 10 == 1);                               // 2^77 mod 10 = 2, so 2^77-1 mod 10 = 1
    ASSERT(1, wu / 2 == (((unsigned _BitInt(77))1 << 76) - 1));
    // shifts across the 64-bit boundary
    unsigned _BitInt(77) ws = 1;
    ws = ws << 70;
    ASSERT(1, ws > 0);
    ws = ws >> 69;
    ASSERT(2, ws);
    // mixed 77-bit and small int: the small side converts to _BitInt(77)
    ASSERT(1, w1 + 5 > w1);
    ASSERT(1, (unsigned _BitInt(77)) - 1 + 1 == 0);
    // comparisons across widths
    ASSERT(1, w1 > 999999999999999999LL);
    ASSERT(1, w1 != 0);

    // === Small _BitInt wrap (< 8 bits): same-type ops stay in _BitInt ===
    _BitInt(3) s3 = 3;
    ASSERT(-2, s3 + s3);  // 6 = 0b110 -> -2 in i3
    _BitInt(3) m3 = -4;
    ASSERT(-1, m3 + 3);
    ASSERT(-4, -m3);  // -(-4) = 4 wraps to -4 in i3
    unsigned _BitInt(4) u4 = 15;
    ASSERT(14, u4 + u4);  // 30 wraps to 14 in u4
    ASSERT(7, u4 / 2);
    u4 = u4 + 1;  // assignment truncates the promoted int result
    ASSERT(0, u4);

    printf("OK\n");
    return 0;
}
