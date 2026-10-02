#include "test.h"

constexpr int cx = 5;
constexpr int cy = cx * 3;
int ca[cx];
int cg = cx;
constexpr int cagg[5] = {5};
int cagg0 = cagg[0];
constexpr struct CaggS {
    int a;
    int b;
} caggs = {3, 4};
int caggsb = caggs.b;
constexpr struct CaggOut {
    struct CaggS in;
    int x;
} caggout = {5, 6, 7};
struct CaggS caggcopy = caggout.in;
struct CaggS2 {
    struct CaggS a;
    int y;
} caggcopy2 = {caggs, 9};

int main() {
    ASSERT(5, cx);
    ASSERT(15, cy);
    ASSERT(5, sizeof(ca) / sizeof(int));
    ASSERT(5, cg);
    ASSERT(5, cagg0);
    ASSERT(4, caggsb);
    ASSERT(5, caggcopy.a);
    ASSERT(6, caggcopy.b);
    ASSERT(3, caggcopy2.a.a);
    ASSERT(4, caggcopy2.a.b);
    ASSERT(9, caggcopy2.y);

    ASSERT(2, ({
               constexpr int x = 2;
               switch (2) {
                   case x:
                       break;
               }
               x;
           }));

    ASSERT(1, ({
               constexpr int x = 5;
               static_assert(x == 5, "");
               int *p = (int *)&x;
               *p = 6;
               x == 5;
           }));

    ASSERT(6, ({
               constexpr int a = 2;
               constexpr int b = a * 3;
               int c[b];
               sizeof(c) / 4;
           }));

    ASSERT(1, ({
               constexpr int x = 5;
               struct S {
                   int a;
                   int b;
               } s = {x, 6};
               s.a + s.b == 11;
           }));

    ASSERT(0, ({
               constexpr unsigned x = 0xFFFFFFFFu;
               x == 0xFFFFFFFFu ? 0 : 1;
           }));

    // Aggregates keep their storage at runtime (reads are loads, as in
    // clang); constant contexts fold elements through the initializer.
    ASSERT(6, ({
               constexpr int x[5] = {5};
               int *p = (int *)x;
               *p = 6;
               x[0];
           }));
    ASSERT(8, ({
               constexpr struct S {
                   int a[2];
               } s = {{7, 8}};
               s.a[1];
           }));
    ASSERT(3, ({
               constexpr struct S {
                   int a[2][2];
               } s = {{{1, 2}, {3, 4}}};
               s.a[1][0];
           }));

    printf("OK\n");
    return 0;
}
