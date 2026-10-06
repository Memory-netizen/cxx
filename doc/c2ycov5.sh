#!/bin/bash
# Wave 5: the last proposals that are observable from the compiler, plus the
# final full-suite run.
C=/home/memory/cxx/cxx
t=`mktemp -d /tmp/cxx-c2y5-XXXXXX`
trap 'rm -rf $t' EXIT
n_pass=0; n_fail=0
run() {
    cat > "$t/t.c"
    if $C -w -o "$t/t" "$t/t.c" > "$t/log" 2>&1 && "$t/t"; then
        echo "PASS  $1"; n_pass=$((n_pass+1))
    else
        echo "FAIL  $1"; sed 's/^/          /' "$t/log" | head -2; n_fail=$((n_fail+1))
    fi
}
ok() {
    cat > "$t/t.c"
    if $C -w -S -emit-llvm -o /dev/null "$t/t.c" > "$t/log" 2>&1; then
        echo "PASS  $1"; n_pass=$((n_pass+1))
    else
        echo "FAIL  $1"; sed 's/^/          /' "$t/log" | head -2; n_fail=$((n_fail+1))
    fi
}

echo "### Q. N3239 constants are literals"
run "typeof of each constant form" <<'EOF'
int main(void) {
    return (sizeof(typeof(1)) == 4 && sizeof(typeof(1L)) == 8 &&
            sizeof(typeof(1.0f)) == 4 && sizeof(typeof(1.0)) == 8 &&
            sizeof(typeof('a')) == 4 && sizeof(typeof(true)) == 1) ? 0 : 1;
}
EOF
# char8_t is a typedef of <uchar.h> in C23, not a keyword: without the
# include cxx and clang both reject the association, so it has to be there.
run "_Generic over literal types" <<'EOF'
#include <uchar.h>
int main(void) {
    return (_Generic(1.0f, float: 1, default: 0) &&
            _Generic(1.0, double: 1, default: 0) &&
            _Generic('a', int: 1, default: 0) &&
            _Generic(true, bool: 1, default: 0) &&
            _Generic(u8'x', char8_t: 1, default: 0)) ? 0 : 1;
}
EOF
# The entry's name is the requirement: 6.3.2.1 needs a modifiable lvalue
# on the left, and gcc, clang and cxx all diagnose it.
rej "a literal is not an lvalue" <<'EOF'
int main(void) { 1 = 2; return 0; }
EOF

echo
echo "### R. N3563 pointer / nullptr_t representation"
run "nullptr_t compared and converted" <<'EOF'
#include <stddef.h>
#include <stdio.h>
int main(void) {
    nullptr_t a = nullptr, b = nullptr;
    void *p = a;
    return (a == b && p == 0 && !a && (a ? 0 : 1)) ? 0 : 1;
}
EOF
run "nullptr in _Generic selects nullptr_t" <<'EOF'
#include <stddef.h>
int main(void) { return _Generic(nullptr, nullptr_t: 1, void *: 2, default: 0) == 1 ? 0 : 1; }
EOF
run "nullptr converts to any object pointer" <<'EOF'
#include <stddef.h>
int main(void) {
    int *ip = nullptr; char *cp = nullptr; double *dp = nullptr;
    return (!ip && !cp && !dp) ? 0 : 1;
}
EOF

echo
echo "### S. N3517 subscripting without decay, more cases"
run "&a[3][0] one past the end of a row" <<'EOF'
int main(void) { int a[3][4]; int *p = &a[3][0]; return p == (int *)a + 12 ? 0 : 1; }
EOF
run "address of a non-lvalue array member's element" <<'EOF'
struct S { int a[3]; };
int main(void) { const int *p = &((struct S){{1,2,3}}).a[1]; return *p == 2 ? 0 : 1; }
EOF
run "sizeof of a non-lvalue array member" <<'EOF'
struct S { int a[3]; };
int main(void) { return sizeof(((struct S){{1,2,3}}).a) == 12 ? 0 : 1; }
EOF

echo
echo "### T. N3312 atomic alignment, all sizes"
run "_Atomic alignment for every width and for aggregates" <<'EOF'
#include <stdatomic.h>
struct S { int a; int b; };
int main(void) {
    return (_Alignof(_Atomic char) == _Alignof(char) &&
            _Alignof(_Atomic short) == _Alignof(short) &&
            _Alignof(_Atomic int) == _Alignof(int) &&
            _Alignof(_Atomic long long) == _Alignof(long long) &&
            _Alignof(_Atomic struct S) >= _Alignof(struct S) &&
            sizeof(_Atomic int) == sizeof(int) &&
            sizeof(_Atomic struct S) == sizeof(struct S)) ? 0 : 1;
}
EOF
run "atomic_load/store on an _Atomic aggregate" <<'EOF'
#include <stdatomic.h>
struct S { int a; };
int main(void) { _Atomic struct S v; atomic_store(&v, (struct S){7}); return atomic_load(&v).a == 7 ? 0 : 1; }
EOF

echo
echo "### U. N3623 / J.2 main forms"
run "int main(void)" <<'EOF'
int main(void) { return 0; }
EOF
run "int main(int, char **)" <<'EOF'
int main(int c, char **v) { return (c > 0 && v != 0) ? 0 : 1; }
EOF
run "return from main falls off the end as 0" <<'EOF'
int main(void) { }
EOF

echo
echo "### V. #embed __has_embed forms"
printf 'ABCD' > "$t/e.bin"
run "__has_embed with angle brackets and a parameter" <<'EOF'
#if __has_embed("e.bin" limit(2)) != __STDC_EMBED_FOUND__
#error found
#endif
#if __has_embed("nope.bin") != __STDC_EMBED_NOT_FOUND__
#error notfound
#endif
#if __STDC_EMBED_FOUND__ != 1 || __STDC_EMBED_EMPTY__ != 2 || __STDC_EMBED_NOT_FOUND__ != 0
#error values
#endif
int main(void) { return 0; }
EOF

echo
echo "### W. Slay Some Earthly Demons: the constraint changes they carry"
rej "6.7.2: a block-scope function declaration may only be extern" <<'EOF'
int main(void) { static int f(void); return 0; }
EOF

echo
echo "c2ycov5: $n_pass passed, $n_fail failed"
