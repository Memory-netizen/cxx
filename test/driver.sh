#!/bin/bash
# Compiler driver behavior: command-line parsing, output files, and the
# preprocessor/assembler/linker invocation. Diagnostic tests live in
# error.sh; LLVM IR emission checks live in ir.sh.
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

# __has_include: current directory first; __has_include_next: skip the
# current directory like #include_next.
echo '#include "file1.h"' > $tmp/has.c
echo '#if __has_include("file1.h")
HAS
#endif
#if __has_include_next("file1.h")
NEXT
#endif
#if !__has_include_next("nonexistent.h")
NO
#endif' > $tmp/next1/file1.h
$compiler -I$tmp/next1 -I$tmp/next2 -I$tmp/next3 -E $tmp/has.c > $tmp/has.out
grep -q HAS $tmp/has.out
check '__has_include'
grep -q NEXT $tmp/has.out
check '__has_include_next'
grep -q NO $tmp/has.out
check '__has_include_next negative'

# _Pragma: destringized and processed like #pragma (the only effective
# pragma is "once"), also when produced by macro expansion.
echo '_Pragma("once")
#define GUARDED 42
HDR_MARKER' > $tmp/pragma_once.h
echo '#include "pragma_once.h"
#include "pragma_once.h"
GUARDED' | $compiler -I$tmp -E -xc - | grep -c 'HDR_MARKER' | grep -q '^1$'
check '_Pragma once'
echo '#define ONCE _Pragma("once")
ONCE
#define GUARDED2 43
HDR_MARKER2' > $tmp/pragma_macro.h
echo '#include "pragma_macro.h"
#include "pragma_macro.h"
GUARDED2' | $compiler -I$tmp -E -xc - | grep -c 'HDR_MARKER2' | grep -q '^1$'
check '_Pragma from macro'
echo '_Pragma("GCC diagnostic push")
int x;' | $compiler -E -xc - | grep -q 'int x'
check '_Pragma unknown ignored'

# The compiler implements C23; advertise it.
echo '__STDC_VERSION__' | $compiler -E -xc - | grep -q '202311L'
check '__STDC_VERSION__'

# __has_c_attribute: stub, always 0 until attribute support lands
# (clang returns 202311L for standard attributes; documented divergence).
echo '#if __has_c_attribute(deprecated)
HASATTR
#else
NOATTR
#endif' | $compiler -E -xc - | grep -q NOATTR
check '__has_c_attribute stub'

# C23/C2y delimited universal character names (\u{...} and \U{...}).
printf 'int \\u{00E9}x = 65; int main() { return ("\\u{41}"[0] == 65 && \\u{00E9}x == 65 && "\\U{1F600}"[0] == (char)0xF0) ? 0 : 1; }\n' \
  | $compiler -o $tmp/ucn -xc - && $tmp/ucn
check 'delimited universal character names'

# #embed: replaced by a comma-separated list of the resource bytes
# (C23 6.10.4).
printf 'AB\001\377' > $tmp/data.bin
echo '#embed "data.bin"' | $compiler -I$tmp -E -xc - | grep -q '65,66,1,255'
check '#embed bytes'
echo '#define X 99
#embed "data.bin" limit(2) prefix(X + 1,) suffix(, 7)' | $compiler -I$tmp -E -xc - | grep -q '99 + 1,65,66, 7'
check '#embed limit prefix suffix'
: > $tmp/empty.bin
echo '#embed "empty.bin" prefix(1,) suffix(, 2) if_empty(42)' | $compiler -I$tmp -E -xc - | grep -q '^ *42$'
check '#embed if_empty'
echo '#embed "empty.bin" prefix(1,) suffix(, 2)' | $compiler -I$tmp -E -xc - | grep -c '1,' | grep -q '^0$'
check '#embed empty without if_empty'
echo '#define EMBF "data.bin"
#embed EMBF limit(3)' | $compiler -I$tmp -E -xc - | grep -q '65,66,1$'
check '#embed macro form'
# The underscored spellings name the same parameters (C23 6.10.3.1).
echo '#embed "data.bin" __limit__(1) __prefix__(7,) __suffix__(, 9)' | $compiler -I$tmp -E -xc - | grep -q '7,65, 9'
check '#embed underscored parameters'
echo '#embed "empty.bin" __if_empty__(42)' | $compiler -I$tmp -E -xc - | grep -q '^ *42$'
check '#embed underscored if_empty'

# __has_embed: found / empty / not found, with the __STDC_EMBED_* values.
echo '#if __has_embed("data.bin") == __STDC_EMBED_FOUND__
HAS_EMBED
#endif
#if __has_embed("empty.bin") == __STDC_EMBED_EMPTY__
HAS_EMPTY
#endif
#if !__has_embed("nonexistent_embed.bin")
HAS_NONE
#endif' | $compiler -I$tmp -E -xc - > $tmp/hasemb.out
grep -q HAS_EMBED $tmp/hasemb.out
check '__has_embed found'
grep -q HAS_EMPTY $tmp/hasemb.out
check '__has_embed empty'
grep -q HAS_NONE $tmp/hasemb.out
check '__has_embed not found'
echo '#if __has_embed("data.bin" __limit__(1))
HAS_ULIMIT
#endif' | $compiler -I$tmp -E -xc - | grep -q HAS_ULIMIT
check '__has_embed underscored parameter'
# An unknown parameter makes __has_embed report "not found" silently.
echo '#if !__has_embed("data.bin" bogus(1))
HAS_NOPARAM
#endif' | $compiler -I$tmp -E -xc - | grep -q HAS_NOPARAM
check '__has_embed unknown parameter'

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

echo OK
