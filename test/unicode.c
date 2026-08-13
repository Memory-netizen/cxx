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

    ASSERT(46, '\o{56}');
    ASSERT(86, '\x{56}');
    ASSERT(86, '\u{56}');
    ASSERT(86, '\u0056');
    ASSERT(86, '\U{56}');
    ASSERT(86, '\U00000056');
    ASSERT(20320, L'你');

    ASSERT(-1, L'\xffffffff' >> 31);
    ASSERT(946, L'β');
    ASSERT(12354, L'あ');
    ASSERT(127843, L'🍣');

    printf("OK\n");
    return 0;
}
