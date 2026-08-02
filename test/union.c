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

    printf("OK\n");
    return 0;
}
