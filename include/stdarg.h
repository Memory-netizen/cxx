#ifndef __STDARG_H
#define __STDARG_H

/* The layout of va_list is the target's business: the compiler publishes
 * __builtin_va_list, built by the target's own va_list_type(). Keeping it
 * here would put an ABI's field order, widths and alignment in a header
 * that has to be edited whenever a target is added. */
typedef __builtin_va_list va_list;

/* C23 7.16.1.4 gives va_start two forms: va_start(ap, last) for the usual
 * definition, and va_start(ap) for one whose parameter list is a bare
 * "...", where there is no last parameter to name. A variadic macro with
 * __VA_ARGS__ covers both, and __VA_ARGS__ may be empty in C23. */
#define va_start(ap, ...) __builtin_va_start(ap __VA_OPT__(, ) __VA_ARGS__)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(dst, src) __builtin_va_copy(dst, src)

#define __GNUC_VA_LIST 1
typedef va_list __gnuc_va_list;

#endif
