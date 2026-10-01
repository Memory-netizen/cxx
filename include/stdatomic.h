#ifndef __CXX_STDATOMIC_H
#define __CXX_STDATOMIC_H

#include <stddef.h>

typedef enum memory_order {
  memory_order_relaxed = __ATOMIC_RELAXED,
  memory_order_consume = __ATOMIC_CONSUME,
  memory_order_acquire = __ATOMIC_ACQUIRE,
  memory_order_release = __ATOMIC_RELEASE,
  memory_order_acq_rel = __ATOMIC_ACQ_REL,
  memory_order_seq_cst = __ATOMIC_SEQ_CST,
} memory_order;

#ifdef __cplusplus
typedef _Atomic(bool)               atomic_bool;
#else
typedef _Atomic(_Bool)              atomic_bool;
#endif
typedef _Atomic(char)               atomic_char;
typedef _Atomic(signed char)        atomic_schar;
typedef _Atomic(unsigned char)      atomic_uchar;
typedef _Atomic(short)              atomic_short;
typedef _Atomic(unsigned short)     atomic_ushort;
typedef _Atomic(int)                atomic_int;
typedef _Atomic(unsigned int)       atomic_uint;
typedef _Atomic(long)               atomic_long;
typedef _Atomic(unsigned long)      atomic_ulong;
typedef _Atomic(long long)          atomic_llong;
typedef _Atomic(unsigned long long) atomic_ullong;

/* 7.17.6 Atomic integer types (the implementation provides size_t and
 * ptrdiff_t from <stddef.h>) */
typedef _Atomic(size_t)             atomic_size_t;
typedef _Atomic(ptrdiff_t)          atomic_ptrdiff_t;

/* 7.17.1 Lock-free property macros. The __CLANG_ATOMIC_*_LOCK_FREE /
 * __GCC_ATOMIC_*_LOCK_FREE values are predefined per target (all 2
 * except riscv32, where 8-byte atomics use libcalls and LLONG is 1). */
#define ATOMIC_BOOL_LOCK_FREE       __CLANG_ATOMIC_BOOL_LOCK_FREE
#define ATOMIC_CHAR_LOCK_FREE       __CLANG_ATOMIC_CHAR_LOCK_FREE
#define ATOMIC_CHAR16_T_LOCK_FREE   __CLANG_ATOMIC_CHAR16_T_LOCK_FREE
#define ATOMIC_CHAR32_T_LOCK_FREE   __CLANG_ATOMIC_CHAR32_T_LOCK_FREE
#define ATOMIC_WCHAR_T_LOCK_FREE    __CLANG_ATOMIC_WCHAR_T_LOCK_FREE
#define ATOMIC_SHORT_LOCK_FREE      __CLANG_ATOMIC_SHORT_LOCK_FREE
#define ATOMIC_INT_LOCK_FREE        __CLANG_ATOMIC_INT_LOCK_FREE
#define ATOMIC_LONG_LOCK_FREE       __CLANG_ATOMIC_LONG_LOCK_FREE
#define ATOMIC_LLONG_LOCK_FREE      __CLANG_ATOMIC_LLONG_LOCK_FREE
#define ATOMIC_POINTER_LOCK_FREE    __CLANG_ATOMIC_POINTER_LOCK_FREE

/* 7.17.2.1 Initialization macro (deprecated in C17, removed in C23). */
#define ATOMIC_VAR_INIT(value) (value)

/* 7.17.2.2 Initialization function: a plain (non-atomic) store per the
 * standard; cxx's ordinary assignment to an _Atomic object is a seq_cst
 * atomic store, which is stronger but indistinguishable in practice. */
#define atomic_init(object, value) (*(object) = (value))

/* 7.17.3.1 kill_dependency: a no-op on all targets. */
#define kill_dependency(y) (y)

/* 7.17.4 Fences */

#define atomic_thread_fence(order) __c11_atomic_thread_fence(order)
#define atomic_signal_fence(order) __c11_atomic_signal_fence(order)

/* 7.17.5.1 Lock-free property of a given object: every target provides
 * native atomics up to the pointer size (riscv32's 8-byte atomics use
 * libcalls, so this is exact for sizeof > sizeof(void *)). */
#define atomic_is_lock_free(obj) __c11_atomic_is_lock_free(sizeof(*(obj)))

/* 7.17.7 Operations on atomic types */

#define atomic_store(object, desired) __c11_atomic_store(object, desired, __ATOMIC_SEQ_CST)
#define atomic_store_explicit __c11_atomic_store

#define atomic_load(object) __c11_atomic_load(object, __ATOMIC_SEQ_CST)
#define atomic_load_explicit __c11_atomic_load

#define atomic_exchange(object, desired) __c11_atomic_exchange(object, desired, __ATOMIC_SEQ_CST)
#define atomic_exchange_explicit __c11_atomic_exchange

#define atomic_fetch_add(object, operand) __c11_atomic_fetch_add(object, operand, __ATOMIC_SEQ_CST)
#define atomic_fetch_add_explicit __c11_atomic_fetch_add

#define atomic_fetch_sub(object, operand) __c11_atomic_fetch_sub(object, operand, __ATOMIC_SEQ_CST)
#define atomic_fetch_sub_explicit __c11_atomic_fetch_sub

#define atomic_fetch_or(object, operand) __c11_atomic_fetch_or(object, operand, __ATOMIC_SEQ_CST)
#define atomic_fetch_or_explicit __c11_atomic_fetch_or

#define atomic_fetch_xor(object, operand) __c11_atomic_fetch_xor(object, operand, __ATOMIC_SEQ_CST)
#define atomic_fetch_xor_explicit __c11_atomic_fetch_xor

#define atomic_fetch_and(object, operand) __c11_atomic_fetch_and(object, operand, __ATOMIC_SEQ_CST)
#define atomic_fetch_and_explicit __c11_atomic_fetch_and

#define atomic_compare_exchange_strong(object, expected, desired) __c11_atomic_compare_exchange_strong(object, expected, desired, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#define atomic_compare_exchange_strong_explicit __c11_atomic_compare_exchange_strong

#define atomic_compare_exchange_weak(object, expected, desired) __c11_atomic_compare_exchange_weak(object, expected, desired, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#define atomic_compare_exchange_weak_explicit __c11_atomic_compare_exchange_weak

/* 7.17.8 Atomic flag type and operations, backed by exchange/store. */
#ifdef __cplusplus
typedef _Atomic(bool) atomic_flag;
#else
typedef _Atomic(_Bool) atomic_flag;
#endif

#define ATOMIC_FLAG_INIT 0

#define atomic_flag_test_and_set(object) __c11_atomic_exchange(object, 1, __ATOMIC_SEQ_CST)
#define atomic_flag_test_and_set_explicit(object, order) __c11_atomic_exchange(object, 1, order)

#define atomic_flag_clear(object) __c11_atomic_store(object, 0, __ATOMIC_SEQ_CST)
#define atomic_flag_clear_explicit(object, order) __c11_atomic_store(object, 0, order)

#endif // __CXX_STDATOMIC_H
