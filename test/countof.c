#include "test.h"

int main() {
    ASSERT(5, _Countof(int[5]));
    ASSERT(6, _Countof("hello"));
    ASSERT(6, _Countof((char[]){"hello"}));
    ASSERT(3, ({
               char x[][5] = {
                   [1][2 ... 4] = 'a',
                   'b',
               };
               _Countof(x);
           }));

    printf("OK\n");
    return 0;
}
