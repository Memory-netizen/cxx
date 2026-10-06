#include "test.h"

// The alignment of a _BitInt wider than one word. AAPCS64 gives such a type
// sixteen-byte alignment; SysV AMD64 and RISC-V cap it at the long long
// alignment, as clang did everywhere before LLVM 105dd60 ("[Clang][AArch64]
// Fixed incorrect _BitInt alignment", #90602). The size is sixteen either
// way, so this one number decides the layout of any struct holding one.
#ifdef __aarch64__
#define WIDE_BITINT_ALIGN 16
#else
#define WIDE_BITINT_ALIGN 8
#endif

int _Alignas(512) g1;
int _Alignas(512) g2;
char g3;
int g4;
long g5;
char g6;

int main() {
    ASSERT(1, _Alignof(char));
    ASSERT(2, _Alignof(short));
    ASSERT(4, _Alignof(int));
    ASSERT(__SIZEOF_LONG__, _Alignof(long));
    ASSERT(8, _Alignof(long long));
    ASSERT(1, _Alignof(char[3]));
    ASSERT(4, _Alignof(int[3]));
    ASSERT(1, _Alignof(struct {
               char a;
               char b;
           }[2]));
    ASSERT(__SIZEOF_LONG__, _Alignof(struct {
               char a;
               long b;
           }[2]));

    ASSERT(1, ({
               _Alignas(char) char x, y;
               &x - &y;
           }));
    ASSERT(__SIZEOF_LONG__, ({
               _Alignas(long) char x, y;
               &x - &y;
           }));
    ASSERT(32, ({
               _Alignas(32) char x, y;
               &x - &y;
           }));
    ASSERT(32, ({
               _Alignas(32) int *x, *y;
               ((char *)&x) - ((char *)&y);
           }));
    ASSERT(16, ({
               struct {
                   _Alignas(16) char x, y;
               } a;
               &a.y - &a.x;
           }));
    ASSERT(8, ({
               struct T {
                   _Alignas(8) char a;
               };
               _Alignof(struct T);
           }));

    ASSERT(0, (long)(char *)&g1 % 512);
    ASSERT(0, (long)(char *)&g2 % 512);
    ASSERT(0, (long)(char *)&g4 % 4);
    ASSERT(0, (long)(char *)&g5 % __SIZEOF_LONG__);

    ASSERT(1, ({
               char x;
               _Alignof(x);
           }));
    ASSERT(4, ({
               int x;
               _Alignof(x);
           }));
    ASSERT(1, ({
               char x;
               _Alignof x;
           }));
    ASSERT(4, ({
               int x;
               _Alignof x;
           }));

    ASSERT(1, _Alignof(char) << 31 >> 31);
    ASSERT(1, _Alignof(char) << (__SIZE_WIDTH__ - 1) >> (__SIZE_WIDTH__ - 1));
    ASSERT(1, ({
               char x;
               _Alignof(x) << (__SIZE_WIDTH__ - 1) >> (__SIZE_WIDTH__ - 1);
           }));

    ASSERT(1, _Alignof(true));
    ASSERT(1, _Alignof(false));
    ASSERT(__SIZEOF_POINTER__, _Alignof(nullptr));

    ASSERT(4, _Alignof(main));

    ASSERT(1, _Alignof(_BitInt(3)));
    ASSERT(2, _Alignof(_BitInt(9)));
    ASSERT(4, _Alignof(_BitInt(17)));
    ASSERT(8, _Alignof(_BitInt(33)));
    ASSERT(WIDE_BITINT_ALIGN, _Alignof(_BitInt(65)));
    ASSERT(WIDE_BITINT_ALIGN, _Alignof(_BitInt(77)));
    ASSERT(WIDE_BITINT_ALIGN, _Alignof(_BitInt(128)));
    ASSERT(2, _Alignof(_Float16));
    ASSERT(4, _Alignof(_Float32));
    ASSERT(8, _Alignof(_Float64));
    ASSERT(16, _Alignof(_Float128));

    printf("OK\n");
    return 0;
}
