#!/bin/bash

cleanup() {
    rm -f "$TMP_LL" "$TMP_EXE"
}
trap cleanup EXIT

assert() {
    expected="$1"
    input="$2"

    TMP_LL=$(mktemp --suffix=.ll)
    TMP_EXE=$(mktemp)

    ./build/cxx "$input" > "$TMP_LL" || exit

    clang "$TMP_LL" -o "$TMP_EXE" -Wno-override-module || { echo "clang compile failed"; exit 1; }

    "$TMP_EXE"
    actual="$?"

    if [ "$actual" = "$expected" ]; then
        echo "$input => $actual"
    else
        echo "$input => $expected expected, but got $actual"
        exit 1
    fi
}

assert 0 '{return 0;}'
assert 42 '{return 42;}'
assert 47 '{return 5+6*7;}'
assert 77 '{return (5+6)*7;}'
assert 14 '{return 5+6+3;}'
assert 5 '{return 20-15;}'
assert 3 '{return 20-15-2;}'
assert 2 '{return 20/(5*2);}'
assert 2 '{return 20/5/2;}'
assert 2 '{return 20%3;}'
assert 1 '{return 5*2-9;}'
assert 5 '{return 5*(2-1);}'

assert 23 '{return 5+6*3;}'
assert 33 '{return (5+6)*3;}'
assert 13 '{return 20-3*2-1;}'
assert 51 '{return (20-3)*(2+1);}'
assert 6 '{return 10/2+1;}'
assert 3 '{return 10/(2+1);}'
assert 2 '{return 10%3*2;}'
assert 0 '{return 10%3-1;}'
assert 4 '{return 10/3+1;}'

assert 32 '{return 1<<4+1;}'
assert 32 '{return 1<<(4+1);}'
assert 17 '{return (1<<4)+1;}'
assert 4 '{return 16>>2;}'
assert 8 '{return 16>>2-1;}'
assert 3 '{return (16>>2)-1;}'

assert 1 '{return 1<2+3;}'
assert 0 '{return 1<2*0;}'
assert 1 '{return 1+2>3-1;}'
assert 0 '{return 1+2<3-1;}'
assert 1 '{return 1==1+0;}'
assert 1 '{return 2==1+1;}'
assert 0 '{return 2!=1+1;}'
assert 0 '{return 3>2+1;}'
assert 1 '{return 3>=2+1;}'

assert 0 '{return 1&2;}'
assert 3 '{return 1|2;}'
assert 3 '{return 1^2;}'
assert 3 '{return 1&2|3;}'
assert 3 '{return 1|2&3;}'
assert 3 '{return 1^2&3;}'
assert 3 '{return 1^2|3;}'
assert 0 '{return 1<2 & 3>4;}'
assert 1 '{return 1<2 | 3>4;}'

assert 0 '{return (1<2) & (3>4);}'
assert 1 '{return (1<2) | (3>4);}'
assert 1 '{return (1<2) ^ (3>4);}'
assert 10 '{return 5*(3+2)-5*3;}'
assert 1 '{return 5*(3+2)==5*3+10;}'
assert 1 '{return (10-2*3)>=4;}'
assert 0 '{return (10-2*3)>4;}'
assert 16 '{return 10/2*3+1;}'
assert 2 '{return 10/(2*3)+1;}'
assert 10 '{return -10+20;}'
assert 10 '{return - -10;}'
assert 10 '{return - - +10;}'
assert 0 '{return !10;}'
assert 2 '{return ~5 + 8;}'
assert 3 '{2;8;return 3;}'
assert 3 '{return 2,8,3;}'

assert 3 '{a=3; return a;}'
assert 8 '{a=3; z=5; return a+z;}'
assert 6 '{a=b=3; return a+b;}'
assert 3 '{foo=3; return foo;}'
assert 8 '{foo123=3; bar=5; return foo123+bar;}'

assert 1 '{ return 1; 2; 3; }'
assert 2 '{ 1; return 2; 3; }'
assert 3 '{ 1; 2; return 3; }'

assert 3 '{ {1; {2;} return 3;} }'
assert 5 '{ ;{;};{} return 5; }'

assert 3 '{ if (0) return 2; return 3; }'
assert 3 '{ if (1-1) return 2; return 3; }'
assert 2 '{ if (1) return 2; return 3; }'
assert 2 '{ if (2-1) return 2; return 3; }'
assert 4 '{ if (0) { 1; 2; return 3; } else { return 4; } }'
assert 3 '{ if (1) { 1; 2; return 3; } else { return 4; } }'

assert 55 '{ i=0; j=0; for (i=0; i<=10; i=i+1) j=i+j; return j; }'
assert 3 '{ for (;;) {return 3;} return 5; }'

assert 10 '{ i=0; while(i<10) { i=i+1; } return i; }'
assert 1 '{ i=0; do  { i=i+1; } while(i<0); return i; }'

echo OK
