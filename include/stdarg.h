#ifndef __STDARG_H
#define __STDARG_H

/* The layout of va_list is the target's business: the compiler publishes
 * __builtin_va_list, built by the target's own va_list_type(). Keeping it
 * here would put an ABI's field order, widths and alignment in a header
 * that has to be edited whenever a target is added. */
typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(dst, src) __builtin_va_copy(dst, src)

#define __GNUC_VA_LIST 1
typedef va_list __gnuc_va_list;

#endif
