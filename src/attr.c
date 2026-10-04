#include "attr.h"

#include <string.h>

static AttrInfo attrs[] = {
    // Standard attributes (C23 6.7.12): version is the YYYYMM of
    // introduction, as reported by clang's __has_c_attribute.
    {"deprecated", ATTR_NS_STD, 201904L, ATTR_DECL | ATTR_TYPE | ATTR_FIELD | ATTR_PARAM},
    {"fallthrough", ATTR_NS_STD, 201910L, ATTR_STMT},
    {"nodiscard", ATTR_NS_STD, 202003L, ATTR_DECL | ATTR_TYPE},
    {"maybe_unused", ATTR_NS_STD, 202106L, ATTR_DECL | ATTR_TYPE | ATTR_FIELD | ATTR_PARAM | ATTR_LABEL},
    {"noreturn", ATTR_NS_STD, 202202L, ATTR_DECL},

    // GNU attributes (__attribute__ and [[gnu::...]]).
    {"packed", ATTR_NS_GNU, 1, ATTR_TYPE | ATTR_FIELD},
    {"aligned", ATTR_NS_GNU, 1, ATTR_DECL | ATTR_TYPE | ATTR_FIELD},
    {"noreturn", ATTR_NS_GNU, 1, ATTR_DECL},
    {"deprecated", ATTR_NS_GNU, 1, ATTR_DECL | ATTR_TYPE | ATTR_FIELD | ATTR_PARAM},
    {"unused", ATTR_NS_GNU, 1, ATTR_DECL | ATTR_TYPE | ATTR_FIELD | ATTR_PARAM | ATTR_STMT | ATTR_LABEL},
    {"fallthrough", ATTR_NS_GNU, 1, ATTR_STMT},
    {"maybe_unused", ATTR_NS_GNU, 1, ATTR_DECL | ATTR_TYPE | ATTR_FIELD | ATTR_PARAM | ATTR_LABEL},
    {"nodiscard", ATTR_NS_GNU, 1, ATTR_DECL | ATTR_TYPE},
    // Function attributes glibc's headers rely on (spelled __const__,
    // __pure__, __malloc__ in the headers; attr_lookup strips the
    // surrounding double underscores). They are accepted and otherwise
    // ignored: cxx does no cross-call optimisation, so they carry no
    // semantics yet.
    {"const", ATTR_NS_GNU, 1, ATTR_DECL},
    {"pure", ATTR_NS_GNU, 1, ATTR_DECL},
    {"malloc", ATTR_NS_GNU, 1, ATTR_DECL},
    {"nothrow", ATTR_NS_GNU, 1, ATTR_DECL},
    {"leaf", ATTR_NS_GNU, 1, ATTR_DECL},
    {"nonnull", ATTR_NS_GNU, 1, ATTR_DECL | ATTR_PARAM},
    {"warn_unused_result", ATTR_NS_GNU, 1, ATTR_DECL},
    {"always_inline", ATTR_NS_GNU, 1, ATTR_DECL},
    {"noinline", ATTR_NS_GNU, 1, ATTR_DECL},
    {"format", ATTR_NS_GNU, 1, ATTR_DECL},
    {"sentinel", ATTR_NS_GNU, 1, ATTR_DECL},

    // Clang attributes.
    {"annotate", ATTR_NS_CLANG, 1, ATTR_DECL},
};

static uint32_t ns_of(char *ns) {
    if (!ns) return ATTR_NS_STD;
    if (!strcmp(ns, "gnu")) return ATTR_NS_GNU;
    if (!strcmp(ns, "clang")) return ATTR_NS_CLANG;
    return (uint32_t)-1;
}

AttrInfo *attr_lookup(char *ns, char *name) {
    uint32_t n = ns_of(ns);
    if (n == (uint32_t)-1) return NULL;

    int len = strlen(name);
    if (len > 4 && !strncmp(name, "__", 2) && !strcmp(name + len - 2, "__")) {
        name += 2;
        len -= 4;
    }

    for (size_t i = 0; i < sizeof(attrs) / sizeof(attrs[0]); i++) {
        AttrInfo *a = &attrs[i];
        if (a->ns == n && strlen(a->name) == (size_t)len && !strncmp(a->name, name, len)) return a;
    }
    return NULL;
}
