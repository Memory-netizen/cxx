#ifndef __STDDEF_H
#define __STDDEF_H

#define NULL ((void *)0)

typedef unsigned long size_t;
typedef long ptrdiff_t;
typedef unsigned int wchar_t;
typedef long max_align_t;

// C23 7.19.1p3: nullptr_t is the type of nullptr. Defining it with
// typeof keeps its identity (a distinct type that converts to void *)
// instead of aliasing it to void * here.
typedef typeof(nullptr) nullptr_t;

#define offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif
