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

$compiler -c -MD -MF $tmp/md-mf.d -I. $tmp/md2.c
grep -q -z '^md2.o:.*md2\.c .*/out2\.h' $tmp/md-mf.d
check -MD

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

$compiler -c -MMD -MF $tmp/mmd-mf.d -I. $tmp/mmd2.c
grep -q -z '^mmd2.o:.*mmd2\.c .*/out2\.h' $tmp/mmd-mf.d
check -MMD-MF

echo 'int main(){}' >> $tmp/sys.c
! $compiler -MMD -I$tmp $tmp/sys.c | grep -q 'stdbool.h'
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

echo OK
