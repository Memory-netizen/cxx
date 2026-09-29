#!/bin/bash
compiler=$1

tmp=`mktemp -d /tmp/cxx-test-XXXXXX`
trap 'rm -rf $tmp' INT TERM HUP EXIT
echo 'int main() {}' > $tmp/empty.c

check() {
    if [ $? -eq 0 ]; then
        echo "testing $1 ... passed"
    else
        echo "testing $1 ... failed"
        exit 1
    fi
}

# -o
rm -f $tmp/out
$compiler -o $tmp/out $tmp/empty.c
[ -f $tmp/out ]
check -o

# --help
$compiler --help 2>&1 | grep -q cxx
check --help

# -S
echo 'int main() {}' | $compiler -S -o - -xc - | grep -q 'main:'
check -S

# Default output file
rm -f $tmp/out.o $tmp/out.s
echo 'int main() {}' > $tmp/out.c
(cd $tmp; $OLDPWD/$compiler -c out.c)
[ -f $tmp/out.o ]
check 'default output file'

(cd $tmp; $OLDPWD/$compiler -c -S out.c)
[ -f $tmp/out.s ]
check 'default output file'

# Multiple input files
rm -f $tmp/foo.o $tmp/bar.o
echo 'int x;' > $tmp/foo.c
echo 'int y;' > $tmp/bar.c
(cd $tmp; $OLDPWD/$compiler -c $tmp/foo.c $tmp/bar.c)
[ -f $tmp/foo.o ] && [ -f $tmp/bar.o ]
check 'multiple input files'

rm -f $tmp/foo.s $tmp/bar.s
echo 'int x;' > $tmp/foo.c
echo 'int y;' > $tmp/bar.c
(cd $tmp; $OLDPWD/$compiler -c -S $tmp/foo.c $tmp/bar.c)
[ -f $tmp/foo.s ] && [ -f $tmp/bar.s ]
check 'multiple input files'

# Run linker
rm -f $tmp/foo
echo 'int main() { return 0; }' | $compiler -o $tmp/foo -xc -
$tmp/foo
check linker

rm -f $tmp/foo
echo 'int bar(); int main() { return bar(); }' > $tmp/foo.c
echo 'int bar() { return 42; }' > $tmp/bar.c
$compiler -o $tmp/foo $tmp/foo.c $tmp/bar.c
$tmp/foo
[ "$?" = 42 ]
check linker

# a.out
rm -f $tmp/a.out
echo 'int main() {}' > $tmp/foo.c
(cd $tmp; $OLDPWD/$compiler foo.c)
[ -f $tmp/a.out ]
check a.out

# -E
echo foo > $tmp/out
echo "#include \"$tmp/out\"" | $compiler -E -xc - | grep -q foo
check -E

echo foo > $tmp/out1
echo "#include \"$tmp/out1\"" | $compiler -E -o $tmp/out2 -xc -
cat $tmp/out2 | grep -q foo
check '-E and -o'

# -I
mkdir $tmp/dir
echo foo > $tmp/dir/i-option-test
echo "#include \"i-option-test\"" | $compiler -I$tmp/dir -E -xc - | grep -q foo
check -I

# -D
echo foo | $compiler -Dfoo -E -xc - | grep -q 1
check -D

# -D
echo foo | $compiler -Dfoo=bar -E -xc - | grep -q bar
check -D

# -U
echo foo | $compiler -Dfoo=bar -Ufoo -E -xc - | grep -q foo
check -U

# ignored options
$compiler -c -O -Wall -g -std=c11 -ffreestanding -fno-builtin \
         -fno-omit-frame-pointer -fno-stack-protector -fno-strict-aliasing \
         -m64 -mno-red-zone -w -o /dev/null $tmp/empty.c
check 'ignored options'

# -include
echo foo > $tmp/out.h
echo bar | $compiler -include $tmp/out.h -E -o- -xc - | grep -q -z 'foo.*bar'
check -include

# -x
echo 'int x;' | $compiler -c -xc -o $tmp/foo.o -
check -xc
echo 'x:' | $compiler -c -x assembler -o $tmp/foo.o -
check '-x assembler'

echo 'int x;' > $tmp/foo.c
$compiler -c -x assembler -x none -o $tmp/foo.o $tmp/foo.c
check '-x none'

# -E
echo foo | $compiler -E - | grep -q foo
check -E

# #include_next
mkdir -p $tmp/next1 $tmp/next2 $tmp/next3
echo '#include "file1.h"' > $tmp/file.c
echo '#include_next "file1.h"' > $tmp/next1/file1.h
echo '#include_next "file2.h"' > $tmp/next2/file1.h
echo 'foo' > $tmp/next3/file2.h
$compiler -I$tmp/next1 -I$tmp/next2 -I$tmp/next3 -E $tmp/file.c | grep -q foo
check '#include_next'

# BOM marker
printf '\xef\xbb\xbfxyz\n' | $compiler -E -o- - | grep -q '^xyz'
check 'BOM marker'

# -idirafter
mkdir -p $tmp/dir1 $tmp/dir2
echo foo > $tmp/dir1/idirafter
echo bar > $tmp/dir2/idirafter
echo "#include \"idirafter\"" | $compiler -I$tmp/dir1 -I$tmp/dir2 -E - | grep -q foo
check -idirafter
echo "#include \"idirafter\"" | $compiler -idirafter $tmp/dir1 -I$tmp/dir2 -E - | grep -q bar
check -idirafter

# -isystem
mkdir -p $tmp/sys1 $tmp/sys2
echo foo > $tmp/sys1/isystem
echo bar > $tmp/sys2/isystem
echo "#include \"isystem\"" | $compiler -isystem $tmp/sys1 -E - | grep -q foo
check -isystem

# -isystem multiple
echo "#include \"isystem\"" | $compiler -isystem $tmp/sys1 -isystem $tmp/sys2 -E - | grep -q foo
check -isystem

# -I takes precedence over -isystem, regardless of command-line order
echo "#include \"isystem\"" | $compiler -isystem $tmp/sys1 -I$tmp/sys2 -E - | grep -q bar
check '-I before -isystem'
echo "#include \"isystem\"" | $compiler -I$tmp/sys2 -isystem $tmp/sys1 -E - | grep -q bar
check '-I before -isystem'

# -isystem paths are treated as system headers, filtered by -MM
mkdir -p $tmp/sysinc
echo foo > $tmp/sysinc/sysinc.h
echo '#include <sysinc.h>' > $tmp/sysmain.c
! $compiler -MM -isystem $tmp/sysinc $tmp/sysmain.c | grep -q 'sysinc.h'
check '-isystem with -MM'
$compiler -M -isystem $tmp/sysinc $tmp/sysmain.c | grep -q 'sysinc.h'
check '-isystem with -M'

# .a file
echo 'void foo() {}' | $compiler -c -xc -o $tmp/foo.o -
echo 'void bar() {}' | $compiler -c -xc -o $tmp/bar.o -
ar rcs $tmp/foo.a $tmp/foo.o $tmp/bar.o
echo 'void foo(); void bar(); int main() { foo(); bar(); }' > $tmp/main.c
$compiler -o $tmp/foo $tmp/main.c $tmp/foo.a
check '.a'

# .so file
echo 'void foo() {}' | cc -fPIC -c -xc -o $tmp/foo.o -
echo 'void bar() {}' | cc -fPIC -c -xc -o $tmp/bar.o -
cc -shared -o $tmp/foo.so $tmp/foo.o $tmp/bar.o
echo 'void foo(); void bar(); int main() { foo(); bar(); }' > $tmp/main.c
$compiler -o $tmp/foo $tmp/main.c $tmp/foo.so
check '.so'

# -l
echo 'double sqrt(double x); int main(void){ sqrt(25.0); }' > $tmp/main.c
$compiler -o $tmp/sqrt $tmp/main.c -lm
check '-l'

# -M
echo '#include "out2.h"' > $tmp/out.c
echo '#include "out3.h"' >> $tmp/out.c
touch $tmp/out2.h $tmp/out3.h
$compiler -M -I$tmp $tmp/out.c | grep -q -z '^out.o: .*/out\.c .*/out2\.h .*/out3\.h'
check -M

# -MF
$compiler -MF $tmp/mf -M -I$tmp $tmp/out.c
grep -q -z '^out.o: .*/out\.c .*/out2\.h .*/out3\.h' $tmp/mf
check -MF

# -MP
$compiler -MF $tmp/mp -MP -M -I$tmp $tmp/out.c
grep -q '^.*/out2.h:' $tmp/mp
check -MP
grep -q '^.*/out3.h:' $tmp/mp
check -MP

# -MT
$compiler -MT foo -M -I$tmp $tmp/out.c | grep -q '^foo:'
check -MT
$compiler -MT foo -MT bar -M -I$tmp $tmp/out.c | grep -q '^foo bar:'
check -MT

# -MD
echo '#include "out2.h"' > $tmp/md2.c
echo '#include "out3.h"' > $tmp/md3.c
(cd $tmp; $OLDPWD/$compiler -c -MD -I. md2.c md3.c)
grep -q -z '^md2.o:.* md2\.c .* ./out2\.h' $tmp/md2.d
check -MD
grep -q -z '^md3.o:.* md3\.c .* ./out3\.h' $tmp/md3.d
check -MD

(cd $tmp; $OLDPWD/$compiler -c -MD -MF md-mf.d -I. md2.c)
grep -q -z '^md2.o:.*md2\.c .*/out2\.h' $tmp/md-mf.d
check -MD-MF

# -MQ
$compiler -MQ foo -M -I$tmp $tmp/out.c | grep -q '^foo:'
check -MQ
$compiler -MQ foo -MQ bar -M -I$tmp $tmp/out.c | grep -q '^foo bar:'
check -MQ

# -MM
echo '#include <stdbool.h>' > $tmp/sys.c
! $compiler -MM -I$tmp $tmp/sys.c | grep -q 'stdbool.h'
check -MM
$compiler -M -I$tmp $tmp/sys.c | grep -q 'stdbool.h'
check -M

# -MMD
echo '#include "out2.h"' > $tmp/mmd2.c
echo '#include "out3.h"' > $tmp/mmd3.c
(cd $tmp; $OLDPWD/$compiler -c -MMD -I. mmd2.c mmd3.c)
grep -q -z '^mmd2.o:.* mmd2\.c .* ./out2\.h' $tmp/mmd2.d
check -MMD
grep -q -z '^mmd3.o:.* mmd3\.c .* ./out3\.h' $tmp/mmd3.d
check -MMD

(cd $tmp; $OLDPWD/$compiler -c -MMD -MF mmd-mf.d -I. mmd2.c)
grep -q -z '^mmd2.o:.*mmd2\.c .*/out2\.h' $tmp/mmd-mf.d
check -MMD-MF

echo 'int main(){}' >> $tmp/sys.c
(cd $tmp; ! $OLDPWD/$compiler -MMD -I$tmp sys.c | grep -q 'stdbool.h')
check -MMD

# -static
echo 'extern int bar; int foo() { return bar; }' > $tmp/foo.c
echo 'int foo(); int bar=3; int main() { foo(); }' > $tmp/bar.c
$compiler -static -o $tmp/foo $tmp/foo.c $tmp/bar.c
check -static
file $tmp/foo | grep -q 'statically linked'
check -static

# -fpic
echo 'extern int bar; int foo() { return bar; }' | $compiler -fPIC -xc -c -o $tmp/foo.o -
cc -shared -o $tmp/foo.so $tmp/foo.o
echo 'int foo(); int bar=3; int main() { foo(); }' > $tmp/main.c
$compiler -o $tmp/foo $tmp/main.c $tmp/foo.so
check -fPIC

# -shared
echo 'extern int bar; int foo() { return bar; }' > $tmp/foo.c
echo 'int foo(); int bar=3; int main() { foo(); }' > $tmp/bar.c
$compiler -fPIC -shared -o $tmp/foo.so $tmp/foo.c $tmp/bar.c
check -shared

# -L
echo 'extern int bar; int foo() { return bar; }' > $tmp/foo.c
$compiler -fPIC -shared -o $tmp/libfoobar.so $tmp/foo.c
echo 'int foo(); int bar=3; int main() { foo(); }' > $tmp/bar.c
$compiler -o $tmp/foo $tmp/bar.c -L$tmp -lfoobar
check -L

# -Wl,
echo 'int foo() {}' | $compiler -c -o $tmp/foo.o -xc -
echo 'int foo() {}' | $compiler -c -o $tmp/bar.o -xc -
echo 'int main() {}' | $compiler -c -o $tmp/baz.o -xc -
$compiler -Wl,-z,muldefs,--data-sections -o $tmp/foo $tmp/foo.o $tmp/bar.o $tmp/baz.o
check -Wl,

# -Xlinker
echo 'int foo() {}' | $compiler -c -o $tmp/foo.o -xc -
echo 'int foo() {}' | $compiler -c -o $tmp/bar.o -xc -
echo 'int main() {}' | $compiler -c -o $tmp/baz.o -xc -
$compiler -Xlinker -z -Xlinker muldefs -Xlinker --data-sections -o $tmp/foo $tmp/foo.o $tmp/bar.o $tmp/baz.o
check -Xlinker

# -fcommon
! echo 'int foo;' | $compiler -S -emit-llvm -o- -xc - | grep -q 'common'
check '-fno-common (default)'

echo 'int foo;' | $compiler -fcommon -S -emit-llvm -o- -xc - | grep -q 'common'
check '-fcommon'

# -fno-common
! echo 'int foo;' | $compiler -fno-common -S -emit-llvm -o- -xc - | grep -q 'common'
check '-fno-common'

# -w
! echo '#warning warning' | $compiler -w -S -o /dev/null -xc - 2>&1 | grep -q 'warning'
check -w

# -Werror
echo '#warning warning' | $compiler -Werror -S -o /dev/null -xc - 2>&1 | grep -q 'error'
check -Werror

# Q_MEMCONST: a struct/union with a const member is not assignable as
# a whole (const members propagate through nesting and arrays)
echo 'struct S { const int x; }; void f(struct S *a, struct S *b) { *a = *b; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'read-only'
check 'struct assignment with const member'

echo 'struct I { const int x; }; struct S { struct I i; }; void f(struct S *a, struct S *b) { *a = *b; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'read-only'
check 'nested struct with const member'

echo 'struct S { const int a[2]; }; void f(struct S *a, struct S *b) { *a = *b; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'read-only'
check 'array-of-const member'

! echo 'struct S { const int *p; }; void f(struct S *a, struct S *b) { *a = *b; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'error'
check 'pointer-to-const member stays assignable'

# const of an aggregate object propagates down to its members (6.5.2.3)
echo 'const struct { int x; } y; void f(void) { y.x = 5; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'read-only'
check 'member of const struct'

echo 'struct B { int x; }; const struct A { struct B b; } a; void f(void) { a.b.x = 5; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'read-only'
check 'nested member of const struct'

echo 'const struct S { int a[2]; } s; void f(void) { s.a[0] = 5; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'read-only'
check 'array element of const struct'

echo 'const struct S { int x; } *p; void f(void) { p->x = 5; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'read-only'
check 'member through pointer to const struct'

# struct members cannot have variably modified type (6.7.6.2p2)
echo 'int n; struct S { int a[n]; };' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'variably modified'
check 'VLA struct member'

echo 'int f(int n) { struct S { int (*p)[n]; }; return 0; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'variably modified'
check 'pointer-to-VLA struct member'

# static_assert: failing assertions carry the message; the C23 form
# without a message works; the message must be a string literal
echo 'static_assert(0, "boom");' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'static assertion failed: boom'
check 'static_assert failure message'

echo 'static_assert(0);' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'static assertion failed'
check 'static_assert without message'

echo '_Static_assert(0, "x");' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'static assertion failed: x'
check '_Static_assert spelling'

echo 'static_assert(1, 5);' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'must be a string literal'
check 'static_assert message type'

echo 'struct S { static_assert(0, "inner"); int x; };' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'static assertion failed: inner'
check 'static_assert inside struct'

# Digraphs
echo 'int main() <% return 0; %>' | $compiler -S -o - -xc - | grep -q 'main:'
check 'digraph <% %>'
printf 'int a<:2:> = <%%1,2%%>;\nint main() { return a<:0:> + a<:1:>; }\n' \
  | $compiler -S -o - -xc - | grep -q 'main:'
check 'digraph <: :>'
printf '%%:define M 42\nint x = M;\n' | $compiler -E -xc - | grep -q 'int x = 42;'
check 'digraph %:'
printf '#define CAT(a,b) a %%:%%: b\nCAT(x,y)\n' | $compiler -E -xc - | grep -q 'xy'
check 'digraph %:%:'

# Bytes that are punctuation but not C punctuators must not hang the lexer
printf 'int \\ b;\n' | timeout 5 $compiler -S -o /dev/null -xc - 2>/dev/null
[ $? -eq 1 ]
check 'stray backslash'
echo 'int a = ..;' | timeout 5 $compiler -S -o /dev/null -xc - 2>/dev/null
[ $? -eq 1 ]
check 'double dot'

# Atomics: accesses to _Atomic objects become LLVM atomic ops; plain
# accesses default to seq_cst (C11 7.17.3 order -> LLVM order mapping).
echo '_Atomic int x; void f(void) { x = 1; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'store atomic i32 1, ptr @x seq_cst, align 4'
check 'atomic store default seq_cst'

echo '_Atomic int x; int f(void) { return x; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'load atomic i32, ptr @x seq_cst, align 4'
check 'atomic load default seq_cst'

echo '#include <stdatomic.h>
_Atomic int x;
void f(void) { __c11_atomic_store(&x, 1, __ATOMIC_RELEASE); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'ptr @x release, align 4'
check 'atomic store release'

echo '#include <stdatomic.h>
_Atomic int x;
void f(void) { __c11_atomic_store(&x, 1, __ATOMIC_RELAXED); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'ptr @x monotonic, align 4'
check 'atomic store relaxed'

echo '#include <stdatomic.h>
_Atomic int x;
int f(void) { return __c11_atomic_load(&x, __ATOMIC_ACQUIRE); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'load atomic i32, ptr @x acquire, align 4'
check 'atomic load acquire'

# LLVM dropped the consume order; it is emitted as acquire.
echo '#include <stdatomic.h>
_Atomic int x;
int f(void) { return __c11_atomic_load(&x, __ATOMIC_CONSUME); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'load atomic i32, ptr @x acquire, align 4'
check 'atomic load consume maps to acquire'

echo '#include <stdatomic.h>
_Atomic int x;
int f(void) { return __c11_atomic_load(&x, __ATOMIC_RELAXED); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'load atomic i32, ptr @x monotonic, align 4'
check 'atomic load relaxed'

# _Atomic volatile: "atomic" must precede "volatile" (LLVM syntax).
echo '_Atomic volatile int vx; void f(void) { vx = 3; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'store atomic volatile i32 3, ptr @vx seq_cst, align 4'
check 'atomic volatile store'
echo '_Atomic volatile int vx; int f(void) { return vx; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'load atomic volatile i32, ptr @vx seq_cst, align 4'
check 'atomic volatile load'
echo '_Atomic volatile int vx; void f(void) { vx = 3; }' \
  | $compiler -S -o - -xc - | grep -q 'vx:'
check 'atomic volatile end to end'

# An order the operation does not allow warns and falls back to seq_cst.
echo '#include <stdatomic.h>
_Atomic int x;
void f(void) { __c11_atomic_store(&x, 1, __ATOMIC_ACQUIRE); }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'memory order argument to atomic operation is invalid'
check 'invalid store order warning'
echo '#include <stdatomic.h>
_Atomic int x;
void f(void) { __c11_atomic_store(&x, 1, __ATOMIC_ACQUIRE); }' \
  | $compiler -S -emit-llvm -o - -xc - 2>/dev/null | grep -q 'store atomic i32 %2, ptr @x seq_cst'
check 'invalid order falls back to seq_cst'
echo '#include <stdatomic.h>
_Atomic int x;
int f(void) { return __c11_atomic_load(&x, __ATOMIC_ACQ_REL); }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'memory order argument to atomic operation is invalid'
check 'invalid load order warning'

# A nested atomic access in the RHS must not leak its order onto the
# outer operation.
echo '#include <stdatomic.h>
_Atomic int x, y;
void f(void) { x += __c11_atomic_load(&y, __ATOMIC_RELAXED); }' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/nest.ll
grep -q 'ptr @y monotonic, align 4' $tmp/nest.ll && grep -q 'ptr @x seq_cst, align 4' $tmp/nest.ll
check 'nested atomic order'

# compare_exchange: weak keyword, both orders, non-i32 types.
echo '#include <stdatomic.h>
_Atomic long x; long e;
int f(void) { return __c11_atomic_compare_exchange_weak(&x, &e, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED); }' \
  | $compiler -S -emit-llvm -o - -xc - > $tmp/cas.ll
grep -q 'cmpxchg weak ptr @x, i64 %' $tmp/cas.ll
check 'cmpxchg weak i64'
grep -q 'acquire monotonic, align 8' $tmp/cas.ll
check 'cmpxchg success/failure orders'
grep -q 'extractvalue { i64, i1 } %' $tmp/cas.ll
check 'extractvalue i64 type'
grep -q 'zext i1 %' $tmp/cas.ll
check 'cmpxchg result zext to bool'

# On failure the actual value is stored back into *expected.
echo '#include <stdatomic.h>
_Atomic int x; int e;
int f(void) { return __c11_atomic_compare_exchange_strong(&x, &e, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'store i32 %[0-9]*, ptr @e, align 4'
check 'cas failure writes back expected'

# _Atomic volatile: cmpxchg takes the volatile keyword.
echo '#include <stdatomic.h>
_Atomic volatile int vx; int e;
int f(void) { return __c11_atomic_compare_exchange_strong(&vx, &e, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'cmpxchg volatile ptr @vx, i32 %'
check 'cmpxchg volatile'

# release is a legal success order for cmpxchg (unlike plain stores).
echo '#include <stdatomic.h>
_Atomic int x; int e;
int f(void) { return __c11_atomic_compare_exchange_strong(&x, &e, 1, __ATOMIC_RELEASE, __ATOMIC_RELAXED); }' \
  | $compiler -S -emit-llvm -o - -xc - 2>/dev/null | grep -q 'release monotonic, align 4'
check 'cmpxchg release success order'

# An invalid failure order warns and falls back to monotonic.
echo '#include <stdatomic.h>
_Atomic int x; int e;
int f(void) { return __c11_atomic_compare_exchange_strong(&x, &e, 1, __ATOMIC_SEQ_CST, __ATOMIC_RELEASE); }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'failure memory order argument to atomic operation is invalid'
check 'invalid failure order warning'
echo '#include <stdatomic.h>
_Atomic int x; int e;
int f(void) { return __c11_atomic_compare_exchange_strong(&x, &e, 1, __ATOMIC_RELEASE, __ATOMIC_RELEASE); }' \
  | $compiler -S -emit-llvm -o - -xc - 2>/dev/null | grep -q 'release monotonic, align 4'
check 'invalid failure order falls back to monotonic'

# An invalid success order warns and falls back to seq_cst.
echo '#include <stdatomic.h>
_Atomic int x; int e;
int f(void) { return __c11_atomic_compare_exchange_strong(&x, &e, 1, 99, __ATOMIC_RELAXED); }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'success memory order argument to atomic operation is invalid'
check 'invalid success order warning'

# The expected argument must be a pointer to the same non-atomic type.
echo '#include <stdatomic.h>
_Atomic int x;
int f(void) { return __c11_atomic_compare_exchange_strong(&x, 5, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'second argument to atomic operation must be a pointer to non-atomic type'
check 'cas expected non-pointer'
echo '#include <stdatomic.h>
_Atomic int x; long e;
int f(void) { return __c11_atomic_compare_exchange_strong(&x, &e, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'second argument to atomic operation must be a pointer to the same type'
check 'cas expected type mismatch'

# Non-atomic accesses must stay atomic-free.
echo 'volatile int v; void f(void) { v = 1; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'store volatile i32 1, ptr @v, align 4'
check 'plain volatile store'
echo 'int x; void f(void) { x = 1; }' \
  | $compiler -S -emit-llvm -o - -xc - | grep -q 'store i32 1, ptr @x, align 4'
check 'plain store'

echo OK
