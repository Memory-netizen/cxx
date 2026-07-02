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

    clang "$TMP_LL" -o "$TMP_EXE" || { echo "clang compile failed"; exit 1; }

    "$TMP_EXE"
    actual="$?"

    if [ "$actual" = "$expected" ]; then
        echo "$input => $actual"
    else
        echo "$input => $expected expected, but got $actual"
        exit 1
    fi
}

assert 0 0
assert 42 42

echo OK
