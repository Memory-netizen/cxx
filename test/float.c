#include "test.h"

int main() {
    ASSERT(35, (float)(char)35);
    ASSERT(35, (float)(short)35);
    ASSERT(35, (float)(int)35);
    ASSERT(35, (float)(long)35);
    ASSERT(35, (float)(unsigned char)35);
    ASSERT(35, (float)(unsigned short)35);
    ASSERT(35, (float)(unsigned int)35);
    ASSERT(35, (float)(unsigned long)35);

    ASSERT(35, (double)(char)35);
    ASSERT(35, (double)(short)35);
    ASSERT(35, (double)(int)35);
    ASSERT(35, (double)(long)35);
    ASSERT(35, (double)(unsigned char)35);
    ASSERT(35, (double)(unsigned short)35);
    ASSERT(35, (double)(unsigned int)35);
    ASSERT(35, (double)(unsigned long)35);

    ASSERT(35, (char)(float)35);
    ASSERT(35, (short)(float)35);
    ASSERT(35, (int)(float)35);
    ASSERT(35, (long)(float)35);
    ASSERT(35, (unsigned char)(float)35);
    ASSERT(35, (unsigned short)(float)35);
    ASSERT(35, (unsigned int)(float)35);
    ASSERT(35, (unsigned long)(float)35);

    ASSERT(35, (char)(double)35);
    ASSERT(35, (short)(double)35);
    ASSERT(35, (int)(double)35);
    ASSERT(35, (long)(double)35);
    ASSERT(35, (unsigned char)(double)35);
    ASSERT(35, (unsigned short)(double)35);
    ASSERT(35, (unsigned int)(double)35);
    ASSERT(35, (unsigned long)(double)35);

    ASSERT(1, 2e3 == 2e3);
    ASSERT(0, 2e3 == 2e5);
    ASSERT(1, 2.0 == 2);
    ASSERT(0, 5.1 < 5);
    ASSERT(0, 5.0 < 5);
    ASSERT(1, 4.9 < 5);
    ASSERT(0, 5.1 <= 5);
    ASSERT(1, 5.0 <= 5);
    ASSERT(1, 4.9 <= 5);

    ASSERT(1, 2e3f == 2e3);
    ASSERT(0, 2e3f == 2e5);
    ASSERT(1, 2.0f == 2);
    ASSERT(0, 5.1f < 5);
    ASSERT(0, 5.0f < 5);
    ASSERT(1, 4.9f < 5);
    ASSERT(0, 5.1f <= 5);
    ASSERT(1, 5.0f <= 5);
    ASSERT(1, 4.9f <= 5);

    ASSERT(6, 2.3 + 3.8);
    ASSERT(-1, 2.3 - 3.8);
    ASSERT(-3, -3.8);
    ASSERT(13, 3.3 * 4);
    ASSERT(2, 5.0 / 2);

    ASSERT(6, 2.3f + 3.8f);
    ASSERT(6, 2.3f + 3.8);
    ASSERT(-1, 2.3f - 3.8);
    ASSERT(-3, -3.8f);
    ASSERT(13, 3.3f * 4);
    ASSERT(2, 5.0f / 2);

    ASSERT(0, 0.0 / 0.0 == 0.0 / 0.0);
    ASSERT(1, 0.0 / 0.0 != 0.0 / 0.0);

    ASSERT(0, 0.0 / 0.0 < 0);
    ASSERT(0, 0.0 / 0.0 <= 0);
    ASSERT(0, 0.0 / 0.0 > 0);
    ASSERT(0, 0.0 / 0.0 >= 0);

    ASSERT(0, !3.);
    ASSERT(1, !0.);
    ASSERT(0, !3.f);
    ASSERT(1, !0.f);

    ASSERT(5, 0.0 ? 3 : 5);
    ASSERT(3, 1.2 ? 3 : 5);

    // === interactions with _Float16/_Float128 ===
    ASSERT(1, 1.5f + 0.5f16 == 2.0f);  // f16 converts to float
    ASSERT(1, 0.5 + 0.5f16 == 1.0);
    ASSERT(1, 1.5f128 + 2.25 == 3.75f128);   // double converts to f128
    ASSERT(1, 1.5f + 2.25f128 == 3.75f128);  // float converts to f128
    ASSERT(1, (double)1.5f128 == 1.5);
    ASSERT(1, (float)0.5f16 == 0.5f);
    ASSERT(1, 3.0f128 / 2.0f128 == 1.5f128);
    ASSERT(1, (int)2.5f16 == 2);
    ASSERT(1, (long long)1e18f128 == 1000000000000000000LL);
    _Float128 q = 0.0f128;
    q = q + 1.0f128 / 3.0f128;
    ASSERT(1, q > 0.3 && q < 0.34);
    _Float16 h = 1.0f16;
    h = h / 3.0f16;
    ASSERT(1, h > 0.3f16 && h < 0.34f16);

    printf("OK\n");
    return 0;
}
