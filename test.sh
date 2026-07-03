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

assert 47 '5+6*7'
assert 77 '(5+6)*7'
assert 14 '5+6+3'
assert 5 '20-15'
assert 3 '20-15-2'
assert 2 '20/(5*2)'
assert 2 '20/5/2'
assert 2 '20%3'
assert 1 '5*2-9'
assert 5 '5*(2-1)'

assert 23 '5+6*3'
assert 33 '(5+6)*3'
assert 13 '20-3*2-1'
assert 51 '(20-3)*(2+1)'
assert 6 '10/2+1'
assert 3 '10/(2+1)'
assert 2 '10%3*2'
assert 0 '10%3-1'
assert 4 '10/3+1'

assert 32 '1<<4+1'
assert 32 '1<<(4+1)'
assert 17 '(1<<4)+1'
assert 4 '16>>2'
assert 8 '16>>2-1'
assert 3 '(16>>2)-1'

assert 1 '1<2+3'
assert 0 '1<2*0'
assert 1 '1+2>3-1'
assert 0 '1+2<3-1'
assert 1 '1==1+0'
assert 1 '2==1+1'
assert 0 '2!=1+1'
assert 0 '3>2+1'
assert 1 '3>=2+1'

assert 0 '1&2'
assert 3 '1|2'
assert 3 '1^2'
assert 3 '1&2|3'
assert 3 '1|2&3'
assert 3 '1^2&3'
assert 3 '1^2|3'

assert 0 '1<2 & 3>4'
assert 1 '1<2 | 3>4'

assert 0 '(1<2) & (3>4)'
assert 1 '(1<2) | (3>4)'
assert 1 '(1<2) ^ (3>4)'

assert 10 '5*(3+2)-5*3'
assert 1 '5*(3+2)==5*3+10'
assert 1 '(10-2*3)>=4'
assert 0 '(10-2*3)>4'
assert 16 '10/2*3+1'
assert 2 '10/(2*3)+1'

echo OK
