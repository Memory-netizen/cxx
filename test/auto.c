#include "test.h"

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

    printf("OK\n");
    return 0;
}
