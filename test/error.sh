#!/bin/bash
# Compiler diagnostics: error and warning messages, and the exit codes
# of rejected programs. Driver behavior lives in driver.sh; LLVM IR
# emission checks live in ir.sh.
compiler=$1

check() {
    if [ $? -eq 0 ]; then
        echo "testing $1 ... passed"
    else
        echo "testing $1 ... failed"
        exit 1
    fi
}

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

# Bytes that are punctuation but not C punctuators must not hang the lexer
printf 'int \\ b;\n' | timeout 5 $compiler -S -o /dev/null -xc - 2>/dev/null
[ $? -eq 1 ]
check 'stray backslash'
echo 'int a = ..;' | timeout 5 $compiler -S -o /dev/null -xc - 2>/dev/null
[ $? -eq 1 ]
check 'double dot'

# An order the operation does not allow warns and falls back to seq_cst.
echo '#include <stdatomic.h>
_Atomic int x;
void f(void) { __c11_atomic_store(&x, 1, __ATOMIC_ACQUIRE); }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'memory order argument to atomic operation is invalid'
check 'invalid store order warning'
echo '#include <stdatomic.h>
_Atomic int x;
void f(void) { __c11_atomic_store(&x, 1, __ATOMIC_ACQUIRE); }' \
  | $compiler -S -emit-llvm -o - -xc - 2>/dev/null | grep -qE 'store atomic i32 %tmp[0-9]+, ptr @x seq_cst'
check 'invalid order falls back to seq_cst'
echo '#include <stdatomic.h>
_Atomic int x;
int f(void) { return __c11_atomic_load(&x, __ATOMIC_ACQ_REL); }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'memory order argument to atomic operation is invalid'
check 'invalid load order warning'

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

echo '#include <stdatomic.h>
_Atomic int x;
int f(void) { return __c11_atomic_exchange(&x, 1, 99); }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'memory order argument to atomic operation is invalid'
check 'exchange invalid order warning'

# An invalid fence order warns and falls back to seq_cst (clang emits
# nothing for this UB; cxx is deliberately stricter).
echo '#include <stdatomic.h>
void f(void) { atomic_thread_fence(99); }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'memory order argument to atomic operation is invalid'
check 'fence invalid order warning'

# Atomic aggregates: member access is undefined behavior (clang errors,
# gcc warns); brace initialization is rejected like clang.
echo 'struct S { int a; };
_Atomic struct S s;
int f(void) { return s.a; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'accessing a member of an atomic structure or union is undefined behavior'
check 'atomic struct member read'
echo 'struct S { int a; };
_Atomic struct S s;
void g(void) { s.a = 5; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'accessing a member of an atomic structure or union is undefined behavior'
check 'atomic struct member write'
echo 'struct S { int a, b; };
_Atomic struct S x = { 1, 2 };' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "illegal initializer type '_Atomic(struct.S)'"
check 'atomic struct brace init'

# __has_include/_next are only usable inside preprocessing directives.
echo 'int x = __has_include_next("foo.h");' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "'__has_include_next' must be used within a preprocessing directive"
check 'has_include_next outside directive'

# _Pragma requires a parenthesized string literal.
echo 'int x = _Pragma(5);' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q '_Pragma takes a parenthesized string literal'
check '_Pragma non-string argument'

# #embed parameter diagnostics.
echo '#embed "nonexistent_embed.bin"' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "'nonexistent_embed.bin' file not found"
check 'embed file not found'
echo '#embed "x.bin" limit(1) limit(2)' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "cannot specify parameter 'limit' twice"
check 'embed duplicate parameter'
echo '#embed "x.bin" limit(1) __limit__(1)' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "cannot specify parameter '__limit__' twice"
check 'embed duplicate across spellings'
echo '#embed "x.bin" bogus(1)' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "unknown embed preprocessor parameter 'bogus'"
check 'embed unknown parameter'
echo '#embed "x.bin" limit(-1)' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "invalid value '-1'; must be positive"
check 'embed negative limit'

# __has_embed diagnostics.
echo '#if __has_embed("x.bin" limit(1) limit(2))
#endif' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "cannot specify parameter 'limit' twice"
check 'has_embed duplicate parameter'
echo '#if __has_embed("x.bin" limit(-1))
#endif' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "invalid value '-1'; must be positive"
check 'has_embed negative limit'
echo 'int x = __has_embed("foo.bin");' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "'__has_embed' must be used within a preprocessing directive"
check 'has_embed outside directive'
echo '#if __has_embed
#endif' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "missing '(' after '__has_embed'"
check 'has_embed missing paren'

# __has_c_attribute diagnostics (the evaluation is a stub returning 0).
echo 'int x = __has_c_attribute(deprecated);' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "'__has_c_attribute' must be used within a preprocessing directive"
check 'has_c_attribute outside directive'
echo '#if __has_c_attribute
#endif' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "missing '(' after '__has_c_attribute'"
check 'has_c_attribute missing paren'
# No attribute argument: clang errors with "missing ')'".
echo '#if __has_c_attribute(noreturn(a))
#endif' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "missing ')'"
check 'has_c_attribute rejects attribute arguments'
echo '#if __has_attribute(packed(1))
#endif' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "missing ')'"
check '__has_attribute rejects arguments'

# Attribute position diagnostics (as in clang).
echo '[[noreturn]] int v;' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'noreturn.*can only appear on functions'
check 'noreturn on variable'
echo '[[noreturn]];' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'noreturn.*only applies to functions'
check 'standalone noreturn attribute'
echo '[[unused]] int x;' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "unknown attribute 'unused' ignored"
check 'unused unknown at declspec position'
echo '[[packed]] int x;' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "unknown attribute 'packed' ignored"
check 'packed unknown at declspec position'
echo '[[ aligned(8) ]] int x;' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "unknown attribute 'aligned' ignored"
check 'aligned unknown at declspec position'

# Statement / label attribute position diagnostics (as in clang).
echo 'int f(void){ [[deprecated]]; return 0; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'cannot be applied to a statement'
check 'decl attribute on statement'
echo '[[fallthrough]];' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'cannot be applied to a declaration'
check 'statement attribute as declaration'
echo 'int f(void){ [[fallthrough]]; return 0; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'outside switch statement'
check 'fallthrough outside switch'
echo 'int f(int x){ if (x) [[fallthrough]] {} return 0; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'only applies to empty statements'
check 'fallthrough on non-empty statement'
echo 'void g(void) [[gnu::noreturn]];' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'ignored, because it cannot be applied to a type'
check 'post-declarator type attribute warning'
echo 'int x [[gnu::packed]];' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "ignored, because it cannot be applied to a type"
check 'packed on non-record type'
echo 'int *p [[gnu::packed]];' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q "ignored, because it cannot be applied to a type"
check 'packed on pointer type'

echo '[[gnu::packed]] struct S { char c; };' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'misplaced attributes'
check 'misplaced type attribute'
echo '[[nodiscard]] int x;' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'only applies to functions'
check 'nodiscard on variable'

# Delimited universal character names: empty and surrogate are invalid.
echo 'char *s = "\u{}";' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'empty delimited universal character name'
check 'empty delimited escape'
echo 'char *s = "\u{D800}";' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'invalid universal character'
check 'surrogate delimited escape'

# Builtin functions must be directly called (no &-use).
echo 'int f(void) { return (int)__builtin_alloca; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'builtin functions must be directly called'
check 'alloca must be directly called'

# __builtin_alloca_with_align: constant power-of-2 alignment >= 8 bits.
echo 'int f(int n) { return (int)__builtin_alloca_with_align(n, n); }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'must be a constant integer'
check 'alloca_with_align constant align'
echo 'int f(int n) { return (int)__builtin_alloca_with_align(n, 12); }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'not a positive power of 2'
check 'alloca_with_align power of 2'
echo 'int f(int n) { return (int)__builtin_alloca_with_align(n, 4); }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'must be 8 or greater'
check 'alloca_with_align min 8'

# fetch_and/or/xor apply to integer atomics only (C11 7.17.7.5).
echo '#include <stdatomic.h>
_Atomic float af;
float f(void) { return atomic_fetch_and(&af, 1.0f); }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'must be a pointer to atomic integer'
check 'fetch_and on float'
echo '#include <stdatomic.h>
_Atomic(int *) ap;
int *f(void) { return atomic_fetch_or(&ap, 1); }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'must be a pointer to atomic integer'
check 'fetch_or on pointer'

echo OK

# Named loops (C2y N3355): the operand of break/continue names a label, so
# it is a different lookup from a plain break. The positive cases live in
# test/control.c; what follows are the constraint violations.
echo 'int main(void){ foo: { } continue foo; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'continue statement not within a loop'
check 'continue naming a label that is not on a loop'

echo 'int main(void){ foo: { } break foo; }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'break statement not within loop or switch'
check 'break naming a label that is not on a loop'

echo 'int main(void){ for(;;){ break nope; } }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'does not refer to a named loop or .switch.'
check 'break naming an undefined label'

echo 'int main(void){ for(;;){ continue nope; } }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'does not refer to a named loop'
check 'continue naming an undefined label'

echo 'int main(void){ a: for(;;){ break a; } a: for(;;){ break a; } }' \
  | $compiler -S -o /dev/null -xc - 2>&1 | grep -q 'redefinition of label'
check 'redefinition of a loop label'

# A label lives in its own name space, so sharing a name with an object is
# legal -- gcc accepts this too (clang only rejects it as a C2y feature).
echo 'int main(void){ int x = 0; x: for(;;){ break x; } return x; }' \
  | $compiler -S -o /dev/null -xc - > /dev/null 2>&1
check 'a loop label may share a name with an object'
