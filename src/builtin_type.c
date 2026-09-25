#include "cxx.h"
#define TYPE(a, b, c, d)  \
    {                     \
        .kind = a,        \
        .size = b,        \
        .align = c,       \
        .is_unsigned = d, \
    }

Type ty_i1_ = TYPE(TY_I1, 1, 1, true);
Type ty_i32_ = TYPE(TY_I32, 4, 4, false);
Type ty_i64_ = TYPE(TY_I64, 8, 8, false);

#undef TYPE
