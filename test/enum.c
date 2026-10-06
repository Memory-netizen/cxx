#include "test.h"

int main() {
    ASSERT(0, ({
               enum { zero, one, two };
               zero;
           }));
    ASSERT(1, ({
               enum { zero, one, two };
               one;
           }));
    ASSERT(2, ({
               enum { zero, one, two };
               two;
           }));
    ASSERT(5, ({
               enum { five = 5, six, seven };
               five;
           }));
    ASSERT(6, ({
               enum { five = 5, six, seven };
               six;
           }));
    ASSERT(0, ({
               enum { zero, five = 5, three = 3, four };
               zero;
           }));
    ASSERT(5, ({
               enum { zero, five = 5, three = 3, four };
               five;
           }));
    ASSERT(3, ({
               enum { zero, five = 5, three = 3, four };
               three;
           }));
    ASSERT(4, ({
               enum { zero, five = 5, three = 3, four };
               four;
           }));
    ASSERT(4, ({
               enum { zero, one, two } x;
               sizeof(x);
           }));
    ASSERT(4, ({
               enum t { zero, one, two };
               enum t y;
               sizeof(y);
           }));
    ASSERT(4, ({
               enum t { zero, one, two };
               enum t y;
               y = 4;
               y;
           }));

    ASSERT(2, ({
               enum t { zero, one, two };
               enum t {
                   zero,
                   one,
                   two,
               };
               two;
           }));

    // === the underlying type (C23 6.7.2.2) ===
    // With a fixed type, that type is the representation whatever the
    // enumerators would otherwise need.
    ASSERT(1, ({
               enum fu1 : unsigned char { fu1a, fu1b };
               sizeof(enum fu1);
           }));
    ASSERT(1, ({
               enum fu2 : signed char { fu2a = -128 };
               sizeof(enum fu2);
           }));
    ASSERT(2, ({
               enum fu3 : short { fu3a = 1000 };
               _Alignof(enum fu3);
           }));
    ASSERT(8, ({
               enum fu4 : long long { fu4a = 0x100000000L };
               sizeof(enum fu4);
           }));
    ASSERT(255, ({
               enum fu5 : unsigned char { fu5a = 255 };
               fu5a;
           }));
    ASSERT(8, ({
               enum fu6 : long long;
               enum fu6 : long long { fu6a };
               sizeof(enum fu6);
           }));
    // A fixed type reaches the layout of anything holding the enum.
    ASSERT(2, ({
               enum fu7 : char { fu7a };
               sizeof(struct {
                   enum fu7 a;
                   char c;
               });
           }));

    // An alignment specifier is allowed in the type specifier list and does
    // not change the type: the enum stays as aligned as its underlying type.
    ASSERT(4, ({
               enum fa1 : alignas(8) int { fa1a };
               _Alignof(enum fa1);
           }));
    ASSERT(1, ({
               enum fa2 : alignas(16) char { fa2a };
               _Alignof(enum fa2);
           }));

    // Without one, the narrowest type that holds every enumerator is chosen.
    ASSERT(4, ({
               enum au1 { au1a = 0x7fffffff };
               sizeof(enum au1);
           }));
    ASSERT(4, ({
               enum au2 { au2a = 0x80000000u };
               sizeof(enum au2);
           }));
    ASSERT(4, ({
               enum au3 { au3a = -1, au3b = 1 };
               sizeof(enum au3);
           }));
    ASSERT(8, ({
               enum au4 { au4a = 0x100000000L };
               sizeof(enum au4);
           }));
    ASSERT(16, ({
               enum au5 { au5a = 0x100000000L };
               sizeof(struct {
                   enum au5 a;
                   char c;
               });
           }));

    // An enumerator may be any value its underlying type holds. The value is
    // kept whole -- it is not narrowed to 64 bits on the way in -- and the
    // width of the enum is chosen from it.
    ASSERT(8, ({
               enum wb { wb0 = 0xFFFFFFFFFFFFFFFFUL };
               sizeof(enum wb);
           }));
    ASSERT(1, ({
               enum wb { wb0 = 0xFFFFFFFFFFFFFFFFUL };
               wb0 > 0;
           }));
    ASSERT(1, ({
               enum wb { wb0 = 0xFFFFFFFFFFFFFFFFUL };
               (unsigned long long)wb0 == 0xFFFFFFFFFFFFFFFFULL;
           }));
    ASSERT(8, ({
               enum ws { ws0 = 0x7FFFFFFFFFFFFFFFL };
               sizeof(enum ws);
           }));
    ASSERT(8, ({
               enum wn { wn0 = -0x7FFFFFFFFFFFFFFFL - 1 };
               sizeof(enum wn);
           }));
    ASSERT(1, ({
               enum wn { wn0 = -0x7FFFFFFFFFFFFFFFL - 1 };
               wn0 < 0;
           }));
    ASSERT(4, ({
               enum wu { wu0 = 0x80000000u };
               sizeof(enum wu);
           }));
    ASSERT(1, ({
               enum wu { wu0 = 0x80000000u };
               wu0 > 0;
           }));

    printf("OK\n");
    return 0;
}
