#ifndef ATTR_H_
#define ATTR_H_

#include <stdint.h>

// Attribute names and lookup, shared by the parser and the preprocessor
// (__has_c_attribute / __has_attribute) so that they cannot drift.

typedef struct Token Token;
typedef struct AttrInfo AttrInfo;

// Namespaces. Unqualified names in __has_c_attribute are looked up in
// ATTR_NS_STD first; a match in a vendor namespace yields 1.
enum {
    ATTR_NS_STD = 0,
    ATTR_NS_GNU,
    ATTR_NS_CLANG,
};

// Syntactic positions that accept an attribute.
enum {
    ATTR_DECL = 1 << 0,   // declarations
    ATTR_TYPE = 1 << 1,   // types
    ATTR_FIELD = 1 << 2,  // struct/union members
    ATTR_PARAM = 1 << 3,  // function parameters
    ATTR_STMT = 1 << 4,   // statements
    ATTR_LABEL = 1 << 5,  // labels
};

struct AttrInfo {
    char *name;
    uint32_t ns;
    // __has_c_attribute result: the YYYYMM of introduction for standard
    // attributes, 1 for supported vendor attributes.
    int64_t version;
    uint32_t targets;
};

// Look up an attribute by namespace and name; "__x__" spellings are
// normalized to "x". Returns NULL if unknown.
AttrInfo *attr_lookup(char *ns, char *name);

// An attribute instance in a declaration context. Attached to Type
// (type attributes) or collected by declspecs (declaration attributes).
typedef struct Attr Attr;
struct Attr {
    Attr *next;
    AttrInfo *info;  // NULL for empty "[[]]" entries
    Token *tok;      // the attribute name token, for diagnostics
    Token *args;     // the '(' of the argument list, NULL if none
    bool is_gnu;     // written as __attribute__ (GNU spelling)
};

#endif
