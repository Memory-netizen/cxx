#ifndef CXX_H_
#define CXX_H_

#define _POSIX_C_SOURCE 200809L
#define ALIGN_UP(value, align) (((value) + (align) - 1) & ~((align) - 1))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct SrcFile SrcFile;
typedef struct Token Token;
typedef struct Node Node;
typedef struct Type Type;
typedef struct Ref Ref;
typedef struct Ir Ir;
typedef struct Blk Blk;
typedef struct Sym Sym;
typedef struct Fn Fn;
typedef struct Module Module;
typedef struct Con Con;
typedef struct Member Member;
typedef struct EnumVal EnumVal;
typedef struct Initializer Initializer;

extern SrcFile *cur_file;
extern Type *ty_void;
extern Type *ty_bool;
extern Type *ty_char;
extern Type *ty_schar;
extern Type *ty_uchar;
extern Type *ty_short;
extern Type *ty_ushort;
extern Type *ty_int;
extern Type *ty_uint;
extern Type *ty_long;
extern Type *ty_ulong;
extern Type *ty_llong;
extern Type *ty_ullong;
extern Type *ty_i1;
extern Type *ty_i32;
extern Type *ty_i64;

struct SrcFile {
    char *filename;
    char *content;
};

//
// Lexer
//

typedef enum {
    TK_EOF = -1,
    TK_PUNCT,
    TK_COMMA,  // ,
    TK_AS,     // =
    TK_ADDAS,  // +=
    TK_SUBAS,  // -=
    TK_MULAS,  // *=
    TK_DIVAS,  // /=
    TK_MODAS,  // %=

    TK_ANDAS,    // &=
    TK_ORAS,     // |=
    TK_XORAS,    // ^=
    TK_LEFTAS,   // <<=
    TK_RIGHTAS,  // >>=

    TK_OR,    // ||
    TK_AND,   // &&
    TK_BOR,   // |
    TK_XOR,   // ^
    TK_BAND,  // &

    TK_EQ,  // ==
    TK_NE,  // !=
    TK_LT,  // <
    TK_GT,  // >
    TK_LE,  // <=
    TK_GE,  // >=

    TK_LEFT,   // <<
    TK_RIGHT,  // >>

    TK_PLUS,   // +
    TK_MINUS,  // -
    TK_STAR,   // *
    TK_SLASH,  // /
    TK_MOD,    // %

    TK_INC,     // ++
    TK_DEC,     // --
    TK_INVERT,  // ~
    TK_NOT,     // !
    TK_DOT,     // .
    TK_ARROW,   // ->

    TK_LPAREN,      // (
    TK_RPAREN,      // )
    TK_LBRACKET,    // [
    TK_RBRACKET,    // ]
    TK_LBRACE,      // {
    TK_RBRACE,      // }
    TK_SEMI,        // ;
    TK_COLON,       // :
    TK_COLONCOLON,  // ::
    TK_QUESTION,    // ?
    TK_ELLIPSIS,    // ...
    TK_HASH,        // #
    TK_HASHHASH,    // ##
    TK_PUNCTEND,

    TK_IDENT,
    TK_NUM,
    TK_PPNUM,
    TK_CHARLIT,
    TK_STRLIT,

    TK_KEYWORD,
    TK_TRUE = TK_KEYWORD,
    TK_FALSE,
    TK_NULLPTR,

    // Function specifiers
    TK_INLINE,
    TK_NORETURN,

    // Storage-class specifiers
    TK_CONSTEXPR,
    TK_EXTERN,
    TK_REGISTER,
    TK_STATIC,
    TK_THREAD,
    TK_TYPEDEF,

    // Type specifiers
    TK_AUTO,  // Auto type inference
    TK_VOID,
    TK_CHAR,
    TK_SHORT,
    TK_INT,
    TK_LONG,
    TK_FLOAT,
    TK_DOUBLE,
    TK_SIGNED,
    TK_UNSIGNED,
    TK_BITINT,
    TK_BOOL,
    TK_ENUM,
    TK_STRUCT,
    TK_UNION,
    TK_TYPEOF,
    TK_TYPEOF_U,

    // Type qualifiers
    TK_CONST,
    TK_RESTRICT,
    TK_VOLATILE,
    TK_ATOMIC,

    // Align specifier
    TK_ALIGNAS,

    TK_ALIGNOF,
    TK_COUNTOF,
    TK_SIZEOF,

    TK_GENERIC,
    TK_ASM,
    TK_ATTR,

    TK_BREAK,
    TK_CASE,
    TK_CONTINUE,
    TK_DEFAULT,
    TK_DO,
    TK_ELSE,
    TK_FOR,
    TK_GOTO,
    TK_IF,
    TK_RETURN,
    TK_STATIC_ASSERT,
    TK_SWITCH,
    TK_WHILE,

} TokenKind;

struct Token {
    TokenKind kind;
    Token *next;
    char *loc;
    size_t len;
    char *filename;
    int line;
    int col;
    bool is_sol;
    bool is_leadingws;
    union {
        uint32_t id;   // Uesd if kind == TK_IDENT;
        uint64_t val;  // Uesd if kind == TK_NUM;
    };
    Type *ty;  // Used if TK_NUM or TK_STR
};

bool match(Token **rest, Token *tok, TokenKind kind);
Token *skip(Token *tok, TokenKind kind);
Token *tokenize_file(char *filename);

//
// Parser
//

enum {
    Q_INLINE = 1 << 0,
    Q_NORETURN = 1 << 1,
};

typedef enum {
    SC_NONE,
    SC_AUTO = 1 << 0,
    SC_TYPEDEF = 1 << 1,
    SC_EXTERN = 1 << 2,
    SC_STATIC = 1 << 3,
    SC_THREAD = 1 << 4,
    SC_REG = 1 << 5,
} SClass;

// Variable or function
struct Sym {
    Sym *next;
    uint32_t id;  // Variable name
    Type *ty;     // Type
    int align;    // alignment

    // Local variable
    bool is_local;  // local or global/function
    int vreg;       // Virtual reg id

    // Global variable or function
    bool is_function;
    bool is_definition;
    bool is_str;
    SClass sclass;

    // Global variable
    uint32_t init_data;

    Initializer *init;

    // Function
    uint32_t funcspec;
    uint32_t nparam;
    Node *body;
    Node *labels;
    Sym *locals;

    Blk *start;
    Blk *end;
};

typedef enum {
    ND_NOP,  // do nothing
    // Expression
    ND_COMMA,  // ,
    ND_AS,     // =
    ND_ADDAS,  // +=
    ND_SUBAS,  // -=
    ND_PTRAS,  // ptr += num
    ND_MULAS,  // *=
    ND_DIVAS,  // /=
    ND_MODAS,  // %=

    ND_ANDAS,    // &=
    ND_ORAS,     // |=
    ND_XORAS,    // ^=
    ND_LEFTAS,   // <<=
    ND_RIGHTAS,  // >>=
    ND_BOR,      // |
    ND_XOR,      // ^
    ND_BAND,     // &
    ND_EQ,       // ==
    ND_NE,       // !=
    ND_LT,       // <
    ND_LE,       // <=
    ND_LEFT,     // <<
    ND_RIGHT,    // >>
    ND_ADD,      // +
    ND_SUB,      // -
    ND_MUL,      // *
    ND_DIV,      // /
    ND_MOD,      // %
    ND_PLUS,     // unary +
    ND_NEG,      // unary -
    ND_NOT,      // !
    ND_INVERT,   // ~
    ND_ADDR,     // unary &
    ND_DEREF,    // unary *
    ND_MEMBER,   // . (struct member access)
    ND_PTRADD,   // ptr + num
    ND_PREINC,   // pre ++
    ND_PREDEC,   // pre --
    ND_POSTINC,  // post ++
    ND_POSTDEC,  // post --
    ND_FUNCALL,  // Function call
    ND_IMCAST,   // Implicit cast
    ND_EXCAST,   // Cast
    ND_LVTOR,    // LValue to rvalue
    ND_LOGAND,   // &&
    ND_LOGOR,    // ||
    ND_COND,     // ?:
    ND_MEMZERO,  // Zero-clear a stack variable

    // Statement
    ND_RETURN,     // return
    ND_IF,         // if
    ND_WHILE,      // while
    ND_DO,         // do
    ND_FOR,        // for
    ND_EXPR_STMT,  // Expression statement
    ND_STMT_EXPR,  // Statement expression
    ND_COMP_STMT,  // {...}
    ND_GOTO,       // "goto"
    ND_LABEL,      // Labeled statement
    ND_BREAK,      // "break"
    ND_CONTINUE,   // "continue"
    ND_SWITCH,     // "switch"
    ND_CASE,       // "case"

    // Declare
    ND_DECL,

    // Term
    ND_VAR,  // Variable
    ND_NUM,  // Int
} NodeKind;

// AST node type
struct Node {
    NodeKind kind;   // Node kind
    Node *next;      // Next node
    Type *ty;        // Type
    Token *tok;      // Representative token
    bool is_lvalue;  // LValue

    union {
        struct {
            Node *lhs;         // Left-hand side
            Node *rhs;         // Right-hand side
            Member *member;    // Struct member access
            Type *compute_ty;  // Compound assign
        };
        struct {
            union {
                Node *init;
                Node *default_case;
            };
            Node *cond;
            union {
                Node *then;
                Node *body;  // Block or statement expression
            };
            union {
                Node *els;
                Node *inc;
            };
        };
        struct {
            // Function call
            uint32_t func;
            Type *func_ty;
            Node *args;
            uint32_t narg;
        };
        struct {
            uint32_t label;
            union {
                Node *target;
                Node *label_body;
            };
            Blk *blk;
        };
        struct {
            Sym *var;  // Used if kind == ND_VAR
            Node *var_init;
        };
    };
    Node *goto_next;
    Node *case_next;
    int64_t val;  // Used if kind == ND_NUM
};

// Represents a variable initializer
struct Initializer {
    Initializer *next;
    Type *ty;
    Token *tok;
    bool is_flexible;
    bool is_inited;

    // For scalar type
    Node *expr;
    Con *val;

    // For aggregate type
    Initializer **child;
};

Node *new_unary(NodeKind kind, Node *expr, Token *tok);
void new_imcast(Node **expr, Type *ty);
void lvalue_convert(Node **expr);
Module *parse(Token *tok);

//
// type.c
//

enum {
    Q_CONST = 1 << 0,
    Q_VOLATILE = 1 << 1,
    Q_RESTRICT = 1 << 2,
    Q_MEMCONST = 1 << 3,
};

typedef enum {
    TY_VOID,
    TY_I1,
    TY_I32,
    TY_I64,
    TY_CHAR,
    TY_UCHAR,
    TY_SCHAR,
    TY_BOOL,
    TY_SHORT,
    TY_INT,
    TY_LONG,
    TY_LLONG,
    TY_FLOAT,
    TY_DOUBLE,
    TY_LDOUBLE,
    TY_ENUM,
    TY_PTR,
    TY_FUNC,
    TY_ARRAY,
    TY_STRUCT,
    TY_UNION,
} TypeKind;

struct Type {
    TypeKind kind;
    uint32_t qual;     // qualifiers
    int size;          // sizeof() value
    int align;         // alignof() value
    bool is_unsigned;  // unsigned or signed
    uint32_t id;
    uint32_t uid;
    // Declaration
    Token *name;
    Type *next;
    Type *base;

    // Data
    union {
        struct {
            // Array
            int len;
            bool is_static;
            bool is_star;
        };
        struct {
            // Function
            Type *ret;
            Type *params;
            bool is_variadic;
        };
        struct {
            // Struct, union or enum
            union {
                Member *members;
                EnumVal *enumvals;
            };
            bool is_flexible;
            bool is_anon;
        };
    };
};

// Struct member
struct Member {
    Member *next;
    Type *ty;
    Token *name;
    uint32_t idx;
    int align;
    int offset;
    bool is_align;
};

struct EnumVal {
    EnumVal *next;
    uint32_t name;
    int64_t val;
};

bool is_void(Type *ty);
bool is_integer(Type *ty);
bool is_flonum(Type *ty);
bool is_arith(Type *ty);
bool is_pointer(Type *ty);
bool is_scalar(Type *ty);
bool is_funcptr(Type *ty);
bool is_compatible(Type *t1, Type *t2);
Type *pointer_to(Type *base, uint32_t qual);
Type *func_type(Type *return_ty);
Type *array_of(Type *base, int size);
Type *struct_type(void);
Type *enum_type(void);
Type *copy_type(Type *ty);
Type *type_qual(Type *ty, uint32_t qual);
Type *type_unqual(Type *ty);
void add_type(Node *node);

//
// irgen.c
//
typedef enum {
    IR_NOP,
    // Terminator
    IR_RET,
    IR_JMP,
    IR_JNZ,
    IR_SWITCH,
    IR_HLT,

    // Arithmetic
    IR_ADD,
    IR_SUB,
    IR_MUL,
    IR_DIV,
    IR_REM,
    IR_NEG,

    // Bitwise
    IR_AND,
    IR_OR,
    IR_XOR,
    IR_SHL,
    IR_SHR,

    // Memory
    IR_ALLOCA,
    IR_LORD,
    IR_STR,
    IR_GEP,
    IR_MEMCPY,
    IR_MEMSET,

    // Conversion
    IR_ZEXT,
    IR_SEXT,
    IR_TRUNC,
    IR_PTRTOINT,
    IR_INTTOPTR,

    // Compare
    IR_CMP_NE,
    IR_CMP_EQ,
    IR_CMP_LE,
    IR_CMP_LT,

    // Other
    IR_CALL,
    IR_CNT,
} IrKind;

struct Con {
    enum {
        CUndef,
        CBits,
        CAddr,
    } type;
    uint32_t sym;
    union {
        int64_t i;
    } bits;
};

enum {
    RTmp,
    RCon,
    RSlot,
    RGlb,
};

#define R \
    (Ref) { RTmp, 0, NULL }
#define TMP(x, ty) \
    (Ref) { RTmp, x, ty }
#define SLOT(x, ty) \
    (Ref) { RSlot, x, ty }
#define GLB(x, ty) \
    (Ref) { RGlb, x, ty }
#define CON(x, ty) \
    (Ref) { RCon, x, ty }

#define INT(x) getcon(x, curm, ty_i32)
#define LONG(x) getcon(x, curm, ty_i64)

struct Ref {
    uint32_t type;
    uint32_t val;
    Type *ty;
};

static inline int refeq(Ref a, Ref b) { return a.type == b.type && a.val == b.val; }

struct Ir {
    IrKind op;
    Ref dst;
    Ref *args;
    uint32_t narg;
    Ir *prev, *next;
};

struct Blk {
    int blk_id;

    Ir *head;  // First ir
    Ir *tail;  // Last ir before Terminator
    struct {
        IrKind type;
        Ref arg;
        Ref *args;
    } jmp;
    uint32_t narg;
    Blk *succ1;
    Blk *succ2;
    Blk **succ;
    Blk *next;
};

struct Module {
    Sym *fns;
    Sym *data;
    Type *tys;
    Con *con;
    int ncon;
};

Module *irgen(Module *node);
void dump_module(Module *module, FILE *out);
void dump_ast(Module *prog);
void dump_tokens(Token *tok);

//
// util.c
//

void fatal(char *fmt, ...);
void error(Token *tok, const char *msg, ...);
void error_at(char *loc, const char *msg, ...);
void warning(Token *tok, const char *msg, ...);
void warning_at(char *loc, const char *msg, ...);
void note(Token *tok, const char *msg, ...);
void note_at(char *loc, const char *msg, ...);
void diag(Token *tok, char *level, const char *msg, ...);
void diag_at(char *loc, char *level, const char *msg, ...);

void *emalloc(size_t n);
void freeall(void);
void *vnew(size_t len, size_t esz);
void *vgrow(void *data, size_t len);

char *format(char *s, ...);
uint32_t intern(char *s, int len);
char *str(uint32_t id);
uint32_t str_len(uint32_t id);
char *escape_char_to_string(char c);

Ref newcon(Con *c0, Module *md, Type *ty);
Ref getcon(int64_t val, Module *md, Type *ty);

#endif  // CXX_H_
