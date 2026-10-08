#!/bin/bash
# Configure and build cpython with cxx, the way chibicc's suite does it
# (chibicc/test/thirdparty/cpython.sh: `CC=$chibicc ./configure; make; make
# test`).
#
# The point is who decides which optional code paths exist. Building the tree
# as some *other* compiler configured it measures that compiler's
# configuration -- cpython's pyconfig.h here comes from a clang run, so it
# turns on SIMD units (_Py_HAVE_EFFICIENT_BUILTIN_SHUFFLEVECTOR,
# _Py_HACL_CAN_COMPILE_VEC*) that a compiler without vector types cannot
# serve, and the report then blames cxx for them. Running configure with cxx
# as CC lets the tree's own feature tests decide, which is what a real build
# would do.
#
# The build happens out of tree, so the source directory is left as it is.
#
#   bash doc/pycxx.sh [compiler] [builddir]
#
#   PB_SRC=~/rw/cpython   the source tree
#   PB_JOBS=8             make -j
#   PB_TEST=1             also run a slice of the test suite afterwards
set -u
C=${1:-./cxx}
BUILD=${2:-$HOME/rw/pycxx}
SRC=${PB_SRC:-$HOME/rw/cpython}
JOBS=${PB_JOBS:-8}
TEST=${PB_TEST:-0}

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1
case $C in
    /*) ;;
    *) [ -x "$C" ] && C=$root/${C#./} ;;
esac
[ -x "$C" ] || { echo "no compiler at $C" >&2; exit 2; }
[ -x "$SRC/configure" ] || { echo "no cpython source at $SRC" >&2; exit 2; }

mkdir -p "$BUILD" || exit 1
cd "$BUILD" || exit 1

echo "== configuring $SRC with $C =="
env CC="$C" "$SRC/configure" --without-ensurepip --disable-test-modules > configure.log 2>&1
conf=$?
echo "  configure exit $conf (log: $BUILD/configure.log)"
[ $conf -eq 0 ] || { tail -20 configure.log; exit 1; }

if [ -f pyconfig.h ]; then
    echo "  the switches configure chose:"
    grep -E '^#define (_Py_HAVE_EFFICIENT_BUILTIN_SHUFFLEVECTOR|_Py_HACL_CAN_COMPILE_VEC|_Py_HAVE_BUILTIN_SHUFFLEVECTOR)' pyconfig.h \
        | sed 's/^/    /' || true
fi

echo "== building (make -k -j$JOBS) =="
make -k -j"$JOBS" > make.log 2>&1
mk=$?
echo "  make exit $mk (log: $BUILD/make.log)"

objs=$(find . -name '*.o' 2>/dev/null | wc -l)
echo "  object files built: $objs"

if [ $mk -ne 0 ]; then
    echo
    echo "== failures =="
    grep -E 'error:' make.log | sed 's/.*error: //' | sed 's/[0-9]\+/N/g' \
        | sort | uniq -c | sort -rn | head -10
    echo
    echo "  files that failed (up to 12):"
    grep -E 'error:' make.log | grep -oE '[A-Za-z_][A-Za-z0-9_/.-]*\.c' | sort -u | head -12
fi

if [ -x ./python ]; then
    echo
    echo "== the interpreter answers? =="
    ./python -c 'print("hello from the cxx build", 6*7)' 2>&1 | head -3
fi

if [ "$TEST" = 1 ] && [ -x ./python ]; then
    echo
    echo "== a slice of the test suite =="
    timeout 1800 ./python -m test -j4 test_grammar test_dict test_math test_unicode 2>&1 | tail -15
fi
