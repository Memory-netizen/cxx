#include "test.h"

int main() {
    ASSERT(131585, (int)8590066177);
    ASSERT(513, (short)8590066177);
    ASSERT(1, (char)8590066177);
    ASSERT(1, (long)1);
    ASSERT(0, (long)&*(int *)0);
    ASSERT(513, ({
               int x = 512;
               *(char *)&x = 1;
               x;
           }));
    ASSERT(5, ({
               int x = 5;
               long y = (long)&x;
               *(int *)y;
           }));

    (void)1;

    ASSERT(255, (unsigned char)255);
    ASSERT(-1, (signed char)255);
    ASSERT(255, (unsigned char)255);
    ASSERT(-1, (short)65535);
    ASSERT(65535, (unsigned short)65535);
    ASSERT(-1, (int)0xffffffff);
    ASSERT(0xffffffff, (unsigned)0xffffffff);

    ASSERT(1, -1 < 1);
    ASSERT(0, -1 < (unsigned)1);
    ASSERT(254, (char)127 + (char)127);
    ASSERT(65534, (short)32767 + (short)32767);
    ASSERT(-1, -1 >> 1);
    ASSERT(-1, (unsigned long)-1);
    ASSERT(2147483647, ((unsigned)-1) >> 1);
    ASSERT(-50, (-100) / 2);
    ASSERT(2147483598, ((unsigned)-100) / 2);
    ASSERT((unsigned long)-1 / 2 - 49, ((unsigned long)-100) / 2);
    ASSERT(0, ((long)-1) / (unsigned)100);
    ASSERT(-2, (-100) % 7);
    ASSERT(2, ((unsigned)-100) % 7);
    ASSERT((unsigned long)-1 % 9, ((unsigned long)-100) % 9);

    ASSERT(65535, (int)(unsigned short)65535);
    ASSERT(65535, ({
               unsigned short x = 65535;
               x;
           }));
    ASSERT(65535, ({
               unsigned short x = 65535;
               (int)x;
           }));

    ASSERT(-1, ({
        typedef short T;
        T x = 65535;
        (int)x;
    }));
    ASSERT(65535, ({
               typedef unsigned short T;
               T x = 65535;
               (int)x;
           }));
    ASSERT(0, (_Bool)0.0);
    ASSERT(1, (_Bool)0.1);
    ASSERT(3, (char)3.0);
    ASSERT(1000, (short)1000.3);
    ASSERT(3, (int)3.99);
    ASSERT(2000000000, (long)2e9);
    ASSERT(3, (float)3.5);
    ASSERT(5, (double)(float)5.5);
    ASSERT(3, (float)3);
    ASSERT(3, (double)3);
    ASSERT(3, (float)3.);
    ASSERT(3, (double)3.f);

    // === Wide/small _BitInt casts: truncation and sign extension ===
    ASSERT(-1, (long long)(_BitInt(77)) - 1);
    ASSERT(0, (long long)((_BitInt(77))1 << 64));  // low 64 bits
    ASSERT(65535, (unsigned long long)(unsigned _BitInt(77))65535);
    ASSERT(-1, (long long)(_BitInt(3))7);  // 7 -> -1 in i3
    ASSERT(7, (unsigned _BitInt(3))7);
    ASSERT(-8, (_BitInt(4))8);  // 8 = 0b1000 -> -8
    ASSERT(0, (_BitInt(3))8);   // 8 mod 8 = 0
    // widening sign-extends
    ASSERT(1, (long long)(_BitInt(3))1);
    ASSERT(-1, (long long)(_BitInt(3)) - 1);
    // narrowing _BitInt(77) -> _BitInt(3)
    _BitInt(77) w = (1LL << 62) + 1;
    ASSERT(1, (_BitInt(3))w);
    ASSERT(-2, (_BitInt(3))6);
    // _BitInt <-> floating
    ASSERT(3, (int)(_Float128)3.5f128);
    ASSERT(-3, (int)(_Float128)-3.5f128);
    ASSERT(1, (_Float128)(_BitInt(80))1000000000000000000wb == 1e18f128);
    ASSERT(1, (_BitInt(80))(_Float128)1000000000000000000wb == 1000000000000000000wb);
    ASSERT(1, (_BitInt(80))(_Float128)0.5f128 == 0);
    ASSERT(1, (_BitInt(80))(_Float128)-0.5f128 == 0);  // truncates toward zero
    // fp16 <-> fp128
    ASSERT(1, (_Float128)0.5f16 == 0.5f128);
    ASSERT(1, (_Float16)0.5f128 == 0.5f16);
    ASSERT(1, (_Float16)65536.0f128 > 0);       // overflows f16 -> inf
    ASSERT(1, (_Float16)1e-300f128 == 0.0f16);  // underflows to zero
    ASSERT(1, (double)1.5f16 == 1.5);

    printf("OK\n");
    return 0;
}
