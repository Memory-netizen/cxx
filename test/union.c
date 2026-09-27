#include "test.h"

int main() {
    ASSERT(8, ({
               union {
                   int a;
                   char b[6];
               } x;
               sizeof(x);
           }));
    ASSERT(3, ({
               union {
                   int a;
                   char b[4];
               } x;
               x.a = 515;
               x.b[0];
           }));
    ASSERT(2, ({
               union {
                   int a;
                   char b[4];
               } x;
               x.a = 515;
               x.b[1];
           }));
    ASSERT(0, ({
               union {
                   int a;
                   char b[4];
               } x;
               x.a = 515;
               x.b[2];
           }));
    ASSERT(0, ({
               union {
                   int a;
                   char b[4];
               } x;
               x.a = 515;
               x.b[3];
           }));
    ASSERT(3, ({
               union {
                   int a, b;
               } x, y;
               x.a = 3;
               y.a = 5;
               y = x;
               y.a;
           }));
    ASSERT(3, ({
               union {
                   struct {
                       int a, b;
                   } c;
               } x, y;
               x.c.b = 3;
               y.c.b = 5;
               y = x;
               y.c.b;
           }));

    ASSERT(5, ({
               union u;
               union u {
                   int x;
               };
               union u {
                   int x;
               };
               union u u1;
               u1.x = 5;
               u1.x;
           }));

    ASSERT(0xef, ({
               union {
                   struct {
                       unsigned char a, b, c, d;
                   };
                   long e;
               } x;
               x.e = 0xdeadbeef;
               x.a;
           }));
    ASSERT(0xbe, ({
               union {
                   struct {
                       unsigned char a, b, c, d;
                   };
                   long e;
               } x;
               x.e = 0xdeadbeef;
               x.b;
           }));
    ASSERT(0xad, ({
               union {
                   struct {
                       unsigned char a, b, c, d;
                   };
                   long e;
               } x;
               x.e = 0xdeadbeef;
               x.c;
           }));
    ASSERT(0xde, ({
               union {
                   struct {
                       unsigned char a, b, c, d;
                   };
                   long e;
               } x;
               x.e = 0xdeadbeef;
               x.d;
           }));

    ASSERT(3, ({
               struct {
                   union {
                       int a, b;
                   };
                   union {
                       int c, d;
                   };
               } x;
               x.a = 3;
               x.b;
           }));
    ASSERT(5, ({
               struct {
                   union {
                       int a, b;
                   };
                   union {
                       int c, d;
                   };
               } x;
               x.d = 5;
               x.c;
           }));

    // === unions with wide _BitInt members ===
    ASSERT(16, sizeof(union {
               _BitInt(77) b;
               char c;
           }));
    ASSERT(16, sizeof(union {
               _Float128 q;
               long l;
           }));
    ASSERT(4, sizeof(union {
               _Float16 h;
               int i;
           }));
    union {
        _BitInt(77) b;
        char c[16];
    } ub = {1234567890123456789012wb};
    ASSERT(1, ub.b == 1234567890123456789012wb);
    union {
        _Float16 h;
        unsigned short u;
    } uf = {0.5f16};
    ASSERT(0x3800, uf.u);  // f16 0.5 bit pattern

    printf("OK\n");
    return 0;
}
