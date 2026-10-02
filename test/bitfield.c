#include "test.h"

struct {
    char a;
    int b : 5;
    int c : 10;
} g45 = {1, 2, 3}, g46 = {};

struct {
    char a;
    int : 0;
    int c : 10;
} g47 = {1, 2};

int main() {
    ASSERT(4, sizeof(struct { int x : 1; }));
    ASSERT(__SIZEOF_LONG__, sizeof(struct { long x : 1; }));

    struct bit1 {
        short a;
        char b;
        int c : 2;
        int d : 3;
        int e : 3;
    };

    ASSERT(4, sizeof(struct bit1));
    ASSERT(1, ({
               struct bit1 x;
               x.a = 1;
               x.b = 2;
               x.c = 3;
               x.d = 4;
               x.e = 5;
               x.a;
           }));
    ASSERT(1, ({
               struct bit1 x = {1, 2, 3, 4, 5};
               x.a;
           }));
    ASSERT(2, ({
               struct bit1 x = {1, 2, 3, 4, 5};
               x.b;
           }));
    ASSERT(-1, ({
        struct bit1 x = {1, 2, 3, 4, 5};
        x.c;
    }));
    ASSERT(-4, ({
        struct bit1 x = {1, 2, 3, 4, 5};
        x.d;
    }));
    ASSERT(-3, ({
        struct bit1 x = {1, 2, 3, 4, 5};
        x.e;
    }));

    ASSERT(1, g45.a);
    ASSERT(2, g45.b);
    ASSERT(3, g45.c);

    ASSERT(0, g46.a);
    ASSERT(0, g46.b);
    ASSERT(0, g46.c);

    typedef struct {
        int a : 10;
        int b : 10;
        int c : 10;
    } T3;

    ASSERT(1, ({
               T3 x = {1, 2, 3};
               x.a++;
           }));
    ASSERT(2, ({
               T3 x = {1, 2, 3};
               x.b++;
           }));
    ASSERT(3, ({
               T3 x = {1, 2, 3};
               x.c++;
           }));

    ASSERT(2, ({
               T3 x = {1, 2, 3};
               ++x.a;
           }));
    ASSERT(3, ({
               T3 x = {1, 2, 3};
               ++x.b;
           }));
    ASSERT(4, ({
               T3 x = {1, 2, 3};
               ++x.c;
           }));

    ASSERT(6, ({
               T3 x = {1, 2, 3};
               x.a += 5;
               x.a;
           }));
    ASSERT(7, ({
               T3 x = {1, 2, 3};
               x.b += 5;
               x.b;
           }));
    ASSERT(8, ({
               T3 x = {1, 2, 3};
               x.c += 5;
               x.c;
           }));

    ASSERT(4, sizeof(struct {
               int a : 3;
               int b : 1;
               int c : 5;
           }));
    ASSERT(8, sizeof(struct {
               int a : 3;
               int : 0;
               int c : 5;
           }));
    ASSERT(4, sizeof(struct {
               int a : 3;
               int : 0;
           }));

    typedef struct {
        int a : 4;
        int : 0;
        int c : 4;
    } T4;

    ASSERT(1, ({
               T4 x = {1, 2};
               x.a;
           }));
    ASSERT(2, ({
               T4 x = {1, 2};
               x.c;
           }));

    ASSERT(1, g47.a);
    ASSERT(2, g47.c);

    // === _BitInt bitfields ===
    struct {
        _BitInt(3) a : 3;
        _BitInt(5) b : 5;
    } bs = {3, 10};
    ASSERT(3, bs.a);
    ASSERT(10, bs.b);
    bs.a = -4;
    ASSERT(-4, bs.a);
    struct {
        unsigned _BitInt(4) u : 4;
    } bu = {15};
    ASSERT(15, bu.u);
    ASSERT(1, sizeof(bu));
    struct {
        _BitInt(33) w : 33;
    } bw = {1234567890wb};
    ASSERT(1, bw.w == 1234567890wb);
    bw.w = -1;
    ASSERT(-1, bw.w);

    // Mixed declared types share the storage unit; the unit is anchored
    // at multiples of the declared type's size (as in gcc/clang).
    ASSERT(4, sizeof(struct {
               char a : 3;
               int b : 5;
           }));
    ASSERT(2, sizeof(struct {
               char a : 5;
               char b : 5;
           }));
    ASSERT(3, sizeof(struct {
               char a : 1;
               char b : 8;
               char c : 8;
           }));
    ASSERT(8, sizeof(struct {
               char a : 3;
               int b : 30;
           }));
    ASSERT(4, sizeof(struct {
               int a : 28;
               char b : 4;
           }));
    ASSERT(4, sizeof(struct {
               int a : 1;
               char b : 1;
               int c : 1;
           }));
    ASSERT(3, ({
               struct {
                   char a : 3;
                   int b : 5;
               } x = {3, 17};
               x.a;
           }));
    ASSERT(15, ({
               struct {
                   char a : 3;
                   int b : 5;
               } x = {3, 15};
               x.b;
           }));
    ASSERT(-1, ({
        struct {
            char a : 3;
            int b : 5;
        } x = {0, -1};
        x.b;
    }));
    ASSERT(3, ({
               struct __attribute__((packed)) {
                   char a : 3;
                   int b : 29;
               } x;
               x.a = 3;
               x.a;
           }));
    ASSERT(-1, ({
        struct __attribute__((packed)) {
            char a : 3;
            int b : 29;
        } x;
        x.a = 0;
        x.b = -1;
        x.b;
    }));
    ASSERT(2, sizeof(struct __attribute__((packed)) {
               char a : 5;
               char b : 5;
               char c : 5;
           }));

    printf("OK\n");
    return 0;
}
