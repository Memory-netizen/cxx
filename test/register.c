#include "test.h"

int main() {
    {
        int register x;
    }
    {
        register int x;
    }
    ASSERT(5, ({
               register int x = 5;
               x;
           }));

    printf("OK\n");
    return 0;
}
