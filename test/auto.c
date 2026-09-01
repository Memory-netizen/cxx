#include "test.h"

auto g76 = 5;
auto g77 = 3.14;
auto g78 = 5ll;
auto g79 = 3.14f;

int main() {
    {
        int auto x;
    }
    {
        auto int x;
    }
    ASSERT(5, ({
               auto int x = 5;
               x;
           }));
    ASSERT(8, ({
               auto int x = 8;
               auto int *y = &x;
               *y;
           }));
    ASSERT(6, ({
               auto int x = 6;
               *(int *)&x;
           }));

    ASSERT(4, ({
               auto x = 6;
               sizeof(x);
           }));
    ASSERT(8, ({
               auto x = 3.14;
               sizeof(x);
           }));
    ASSERT(4, ({
               static auto x = 6;
               sizeof(x);
           }));
    ASSERT(8, ({
               static auto x = 3.14;
               sizeof(x);
           }));
    ASSERT(8, ({
               static auto x = 6ll;
               sizeof(x);
           }));
    ASSERT(4, ({
               static auto x = 3.14f;
               sizeof(x);
           }));

    ASSERT(4, sizeof(g76));
    ASSERT(8, sizeof(g77));
    ASSERT(8, sizeof(g78));
    ASSERT(4, sizeof(g79));

    printf("OK\n");
    return 0;
}
