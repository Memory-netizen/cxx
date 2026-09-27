#include "test.h"

int main() {
    ASSERT(97, 'a');
    ASSERT(10, '\n');
    ASSERT(128, '\x80');

    ASSERT(511, 0777);
    ASSERT(0, 0x0);
    ASSERT(10, 0xa);
    ASSERT(10, 0XA);
    ASSERT(48879, 0xbeef);
    ASSERT(48879, 0xBEEF);
    ASSERT(48879, 0XBEEF);
    ASSERT(0, 0b0);
    ASSERT(1, 0b1);
    ASSERT(47, 0b101111);
    ASSERT(47, 0B101111);

    ASSERT(4, sizeof(0));
    ASSERT(__SIZEOF_LONG__, sizeof(0L));
    ASSERT(__SIZEOF_LONG__, sizeof(0LU));
    ASSERT(__SIZEOF_LONG__, sizeof(0UL));
    ASSERT(8, sizeof(0LL));
    ASSERT(8, sizeof(0LLU));
    ASSERT(8, sizeof(0Ull));
    ASSERT(__SIZEOF_LONG__, sizeof(0l));
    ASSERT(8, sizeof(0ll));
    ASSERT(__SIZEOF_LONG__, sizeof(0x0L));
    ASSERT(__SIZEOF_LONG__, sizeof(0b0L));
    ASSERT(4, sizeof(2147483647));
    ASSERT(8, sizeof(2147483648));
    ASSERT(-1, 0xffffffffffffffff);
    ASSERT(8, sizeof(0xffffffffffffffff));
    ASSERT(4, sizeof(4294967295U));
    ASSERT(8, sizeof(4294967296U));

    ASSERT(3, -1U >> 30);
    ASSERT(3, -1Ul >> (__SIZEOF_LONG__ * 8 - 2));
    ASSERT(3, -1ull >> 62);

    ASSERT(1, 0xffffffffffffffffl >> 63);
    ASSERT(1, 0xffffffffffffffffll >> 63);

    ASSERT(-1, 18446744073709551615);
    ASSERT(8, sizeof(18446744073709551615));
    ASSERT(1, 18446744073709551615 >> 63);

    ASSERT(-1, 0xffffffffffffffff);
    ASSERT(8, sizeof(0xffffffffffffffff));
    ASSERT(1, 0xffffffffffffffff >> 63);

    ASSERT(-1, 01777777777777777777777);
    ASSERT(8, sizeof(01777777777777777777777));
    ASSERT(1, 01777777777777777777777 >> 63);

    ASSERT(-1, 0b1111111111111111111111111111111111111111111111111111111111111111);
    ASSERT(8, sizeof(0b1111111111111111111111111111111111111111111111111111111111111111));
    ASSERT(1, 0b1111111111111111111111111111111111111111111111111111111111111111 >> 63);

    ASSERT(8, sizeof(2147483648));
    ASSERT(4, sizeof(2147483647));

    ASSERT(8, sizeof(0x1ffffffff));
    ASSERT(4, sizeof(0xffffffff));
    ASSERT(1, 0xffffffff >> 31);

    ASSERT(8, sizeof(040000000000));
    ASSERT(4, sizeof(037777777777));
    ASSERT(1, 037777777777 >> 31);

    ASSERT(8, sizeof(0b111111111111111111111111111111111));
    ASSERT(4, sizeof(0b11111111111111111111111111111111));
    ASSERT(1, 0b11111111111111111111111111111111 >> 31);

    ASSERT(-1, 1 << 31 >> 31);
    ASSERT(-1, 01 << 31 >> 31);
    ASSERT(-1, 0x1 << 31 >> 31);
    ASSERT(-1, 0b1 << 31 >> 31);

    0.0;
    1.0;
    3e+8;
    0x10.1p0;
    .1E4f;

    ASSERT(4, sizeof(8.f));
    ASSERT(4, sizeof(0.3F));
    ASSERT(8, sizeof(0.));
    ASSERT(8, sizeof(.0));
    ASSERT(__SIZEOF_LONG_DOUBLE__, sizeof(5.l));
    ASSERT(__SIZEOF_LONG_DOUBLE__, sizeof(2.0L));

    assert(1, size\
of(char),
           "sizeof(char)");

    ASSERT(4, sizeof(L'\0'));
    ASSERT(97, L'a');

    // Literals that previously overflowed the 64-bit/double intermediates
    // in convert_pp_num (now delegated to the fp128/int128 parsers).
    ASSERT(1, 12345678901234567890123.0 == 1.2345678901234568e22);
    ASSERT(1, 1e4000 > 1e300);
    ASSERT(16, sizeof(123456789012345678901234567890uwb));
    ASSERT(1, 123456789012345678901234567890uwb == 123456789012345678901234567890uwb);
    ASSERT(1, 0x1.0000000000001p0 == 1.0 + 0x1p-52);
    ASSERT(1, 0x1.8p-1 == 0.75);
    ASSERT(1, 1e-3 == 0.001);
    ASSERT(1, 0.1f32 == 0.1f);
    ASSERT(1, 1e4000L > 0);
    ASSERT(1, 0x1p+4000f128 > 0);
    ASSERT(1, 1e-4000f128 > 0);

    // === big integer literals: hex/octal/binary up to 128 bits ===
    ASSERT(1, 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFuwb == 340282366920938463463374607431768211455uwb);
    ASSERT(1, 0o3777777777777777777777777777777777777777777uwb == 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFuwb);
    ASSERT(
        1,
        0b11111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111uwb ==
            0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFuwb);
    ASSERT(16, sizeof(0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFuwb));
    ASSERT(1, 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFuwb >> 64 == 0xFFFFFFFFFFFFFFFFuwb);
    // digit separators in big literals
    ASSERT(1, 123456789012345678901234567890uwb == 123'456'789'012'345'678'901'234'567'890uwb);
    ASSERT(1, 0xFFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFF'FFFFuwb == 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFuwb);
    // large float literals
    ASSERT(1, 1e4000f128 > 1e3000f128);
    ASSERT(1, 0x1p+16383f128 > 0);
    ASSERT(1, 0x1p-16494f128 > 0);

    printf("OK\n");
    return 0;
}
