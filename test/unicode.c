#include "test.h"

int 世界 = 42;

int main() {
    ASSERT(42, 世界);
    ASSERT(3, ({
               int π = 3;
               π;
           }));
    ASSERT(3, ({
               int 你好 = 3;
               你好;
           }));
    ASSERT(3, ({
               int あβ0 = 3;
               あβ0;
           }));

    ASSERT(5, ({
               int $$$ = 5;
               $$$;
           }));

    ASSERT(42, \u4e16\u{754c});
    ASSERT(42, \U00004e16\U{754c});

    printf("OK\n");
    return 0;
}
