#ifndef __STDARG_H
#define __STDARG_H

// va_list layouts, one per target ABI. The names need not match the ones
// clang emits, but the field order, widths and alignment must: LLVM's
// va_start/va_arg expansion reads the structure it is handed, so a layout
// that differs produces wrong results rather than a diagnostic.
#if defined(__x86_64__)
// System V AMD64: a register save area plus offsets into it. va_list is an
// array of one so that passing it passes a pointer, as the ABI requires.
typedef struct {
    unsigned int gp_offset;
    unsigned int fp_offset;
    void *overflow_arg_area;
    void *reg_save_area;
} __va_elem;

typedef __va_elem va_list[1];

#elif defined(__aarch64__)
// AAPCS64: three region pointers and two running offsets. Passed by
// reference, so it is a plain structure.
typedef struct {
    void *__stack;
    void *__gr_top;
    void *__vr_top;
    int __gr_offs;
    int __vr_offs;
} __va_elem;

typedef __va_elem va_list;

#else
// RISC-V (both ILP32 and LP64): va_list is a single pointer that walks the
// argument area.
typedef void *va_list;

#endif

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(dst, src) __builtin_va_copy(dst, src)

#define __GNUC_VA_LIST 1
typedef va_list __gnuc_va_list;

#endif
