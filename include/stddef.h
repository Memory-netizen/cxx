#ifndef __STDDEF_H
#define __STDDEF_H

#define NULL ((void *)0)

// The fundamental types are target-dependent, so take them from the
// compiler's own predefined macros instead of hard-coding LP64 spellings.
// On ILP32 targets (rv32) hard-coding broke both size and alignment:
// `unsigned long size_t` and `long ptrdiff_t` are 8 bytes there but have
// to be 4.
typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
// wchar_t is signed int on amd64/rv64/rv32 and unsigned int on arm64
// (AAPCS64). A fixed spelling made it a different type from the one the
// compiler uses for L"..." / L'...', so a wide string initialiser such as
// `wchar_t w[] = L"..."` was rejected as "array of inappropriate type" on
// the signed targets.
typedef __WCHAR_TYPE__ wchar_t;

// C23 7.19.2: an object type whose alignment is as great as any supported
// type. That alignment comes from long double (16 on every target here),
// which `long` does not provide -- on ILP32 it is only 4. A union is
// enough to carry the alignment, and gives the correct value on all four
// targets without spelling out a per-target struct.
typedef union {
    long long __max_align_ll;
    long double __max_align_ld;
} max_align_t;

// C23 7.19.1p3: nullptr_t is the type of nullptr. Defining it with
// typeof keeps its identity (a distinct type that converts to void *)
// instead of aliasing it to void * here.
typedef typeof(nullptr) nullptr_t;

#define offsetof(type, member) __builtin_offsetof(type, member)

#endif
