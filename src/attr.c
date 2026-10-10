#include "attr.h"

#include <stdint.h>
#include <string.h>

// Names are compared by their interned id (see attr_lookup), which is
// the only piece of the interning table this file needs.
uint32_t intern(char *s, uint32_t len);

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
    // __attribute__((alias("target"))): the declared name is another name for
    // an object or function defined in this translation unit. Handled in
    // parser.c, where the symbol's emitted name is settled.
    {"alias", ATTR_NS_GNU, 1, ATTR_DECL},
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
    // Run before `main` and after it returns, through the initializer arrays
    // the ABI provides (llvm.global_ctors and llvm.global_dtors in LLVM's
    // spelling). An optional argument is the priority; without one the
    // function runs at 65535, the default both references use.
    {"constructor", ATTR_NS_GNU, 1, ATTR_DECL},
    {"destructor", ATTR_NS_GNU, 1, ATTR_DECL},
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
    // A parameter of this union type takes any of the union's member types
    // directly, which is how glibc declares the address parameter of
    // connect(), bind(), accept() and sendto() under _GNU_SOURCE.
    {"transparent_union", ATTR_NS_GNU, 1, ATTR_TYPE | ATTR_DECL},

    // Clang attributes.
    {"annotate", ATTR_NS_CLANG, 1, ATTR_DECL},

    // The handler runs when the object's scope is left; it is a variable
    // attribute, so a declaration is the only position that accepts it.
    {"cleanup", ATTR_NS_GNU, 1, ATTR_DECL},
};

static uint32_t ns_of(char *ns) {
    if (!ns) return ATTR_NS_STD;
    if (!strcmp(ns, "gnu")) return ATTR_NS_GNU;
    if (!strcmp(ns, "clang")) return ATTR_NS_CLANG;
    return (uint32_t)-1;
}

// The interned id of each row, filled on first use. Comparing those is what
// the loop below does now: it used to call strlen() and strncmp() on every row
// of every lookup, and attribute parsing runs for each declaration.
static uint32_t attr_ids[sizeof(attrs) / sizeof(attrs[0])];

AttrInfo *attr_lookup(char *ns, char *name) {
    uint32_t n = ns_of(ns);
    if (n == (uint32_t)-1) return NULL;

    // glibc's headers spell the reserved forms __name__; what they stand for
    // is `name`, and interning that is the only string work left here. The
    // spelling costs one table entry per name, which is not where this
    // compiler's memory goes.
    int len = strlen(name);
    if (len > 4 && name[0] == '_' && name[1] == '_' && name[len - 2] == '_' && name[len - 1] == '_') {
        name += 2;
        len -= 4;
    }
    uint32_t id = intern(name, len);

    for (size_t i = 0; i < sizeof(attrs) / sizeof(attrs[0]); i++) {
        if (!attr_ids[i]) attr_ids[i] = intern(attrs[i].name, strlen(attrs[i].name));
        if (attrs[i].ns == n && attr_ids[i] == id) return &attrs[i];
    }
    return NULL;
}
