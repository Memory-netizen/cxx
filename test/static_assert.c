#include "test.h"

// Passing assertions compile to nothing, at file and block scope,
// with or without the C23-optional message.
static_assert(1, "file scope");
static_assert(sizeof(int) == 4);
static_assert(2 + 2 == 4, "arithmetic");
_Static_assert(sizeof(char) == 1, "char size");
static_assert(sizeof(long) >= 4, "long at least 32 bits");
static_assert(sizeof(void *) == __SIZEOF_POINTER__, "pointer predef");

struct S {
    int x;
    char c;
};
static_assert(sizeof(struct S) >= 5, "struct size");

enum E { E1 = 5 };
static_assert(E1 == 5, "enum value");

// static_assert-declaration is also a member-declaration (6.7.2.1)
struct S2 {
    static_assert(sizeof(int) == 4, "inside struct");
    _Static_assert(1, "underscore spelling");
    int x;
    static_assert(1);
    int y;
};
union U2 {
    static_assert(1, "inside union");
    int x;
};

int main() {
    static_assert(1, "block scope");
    static_assert(sizeof(int) >= 2, "int at least 16 bits");

    struct S2 s = {1, 2};
    union U2 u = {3};
    ASSERT(6, s.x + s.y + u.x);

    printf("OK\n");
    return 0;
}
