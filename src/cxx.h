#ifndef CXX_H_
#define CXX_H_

#define _POSIX_C_SOURCE 200809L
#define ALIGN_UP(value, align) (((value) + (align) - 1) & ~((align) - 1))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
typedef struct Member Member;

extern SrcFile *cur_file;
extern Type *ty_void;
extern Type *ty_bool;
extern Type *ty_char;
extern Type *ty_short;
extern Type *ty_int;
extern Type *ty_long;
extern Type *ty_llong;
extern Type *ty_i1;
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
    TK_COMMA,
    TK_AS,
    TK_ADDAS,
    TK_SUBAS,
    TK_MULAS,
    TK_DIVAS,
    TK_MODAS,

    TK_ANDAS,
    TK_ORAS,
    TK_XORAS,
    TK_LEFTAS,
    TK_RIGHTAS,

    TK_OR,
    TK_AND,
    TK_BOR,
    TK_XOR,
    TK_BAND,

    TK_EQ,
    TK_NE,
    TK_LT,
    TK_GT,
    TK_LE,
    TK_GE,

    TK_LEFT,
    TK_RIGHT,

    TK_PLUS,
    TK_MINUS,
    TK_STAR,
    TK_SLASH,
    TK_MOD,

    TK_INC,
    TK_DEC,
    TK_INVERT,
    TK_NOT,
    TK_DOT,
    TK_ARROW,

    TK_LPAREN,
    TK_RPAREN,
    TK_LBRACKET,
    TK_RBRACKET,
    TK_LBRACE,
    TK_RBRACE,
    TK_SEMI,
    TK_COLON,
    TK_COLONCOLON,
    TK_QUESTION,
    TK_ELLIPSIS,
    TK_HASH,
    TK_HASHHASH,
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
    union {
        uint32_t id;  // Uesd if kind == TK_IDENT;
        int64_t val;  // Uesd if kind == TK_NUM;
    };
};

bool match(Token **rest, Token *tok, TokenKind kind);
Token *skip(Token *tok, TokenKind kind);
Token *tokenize_file(char *filename);

//
// Parser
//

typedef enum {
    SC_NONE,
    SC_AUTO,
    SC_TYPEDEF,
    SC_EXTERN,
    SC_STATIC,
    SC_REG,
} SClass;

// Variable or function
struct Sym {
    Sym *next;
    uint32_t id;  // Variable name
    Type *ty;     // Type

    // Local variable
    bool is_local;  // local or global/function
    int vreg;       // Virtual reg id

    // Global variable or function
    bool is_function;
    bool is_definition;
    bool is_str;

    // Global variable
    uint32_t init_data;

    Node *init;

    // Function
    Sym *params;
    uint32_t nparam;
    Node *body;
    Sym *locals;

    Blk *start;
    Blk *end;
};

typedef enum {
    // Expression
    ND_COMMA,   // ,
    ND_AS,      // =
    ND_BOR,     // |
    ND_XOR,     // ^
    ND_BAND,    // &
    ND_EQ,      // ==
    ND_NE,      // !=
    ND_LT,      // <
    ND_LE,      // <=
    ND_LEFT,    // <<
    ND_RIGHT,   // >>
    ND_ADD,     // +
    ND_SUB,     // -
    ND_MUL,     // *
    ND_DIV,     // /
    ND_MOD,     // %
    ND_PLUS,    // unary +
    ND_NEG,     // unary -
    ND_NOT,     // !
    ND_INVERT,  // ~
    ND_ADDR,    // unary &
    ND_DEREF,   // unary *
    ND_MEMBER,  // . (struct member access)
    ND_PTRADD,
    ND_FUNCALL,  // Function call
    ND_IMCAST,
    ND_EXCAST,

    // Statement
    ND_RETURN,     // return
    ND_IF,         // if
    ND_WHILE,      // while
    ND_DO,         // do
    ND_FOR,        // for
    ND_EXPR_STMT,  // Expression statement
    ND_STMT_EXPR,  // Statement expression
    ND_COMP_STMT,  // {...}

    // Declare
    ND_DECL,

    // Term
    ND_VAR,  // Variable
    ND_NUM,  // Int
} NodeKind;

// AST node type
struct Node {
    NodeKind kind;  // Node kind
    Node *next;     // Next node
    Type *ty;       // Type
    Token *tok;     // Representative token

    union {
        struct {
            Node *lhs;       // Left-hand side
            Node *rhs;       // Right-hand side
            Member *member;  // Struct member access
        };
        struct {
            Node *init;
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
        Sym *var;     // Used if kind == ND_VAR
        int64_t val;  // Used if kind == ND_NUM
    };
};

Node *new_imcast(Node *expr, Type *ty);
Module *parse(Token *tok);

//
// type.c
//

typedef enum {
    TY_VOID,
    TY_I1,
    TY_I64,
    TY_CHAR,
    TY_BOOL,
    TY_SHORT,
    TY_INT,
    TY_LONG,
    TY_LLONG,
    TY_ENUM,
    TY_PTR,
    TY_FUNC,
    TY_ARRAY,
    TY_STRUCT,
    TY_UNION,
} TypeKind;

struct Type {
    TypeKind kind;
    int size;   // sizeof() value
    int align;  // alignof() value
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
        };
        struct {
            // Function
            Type *ret;
            Type *params;
        };
        struct {
            // Struct
            Member *members;
        };
    };
};

// Struct member
struct Member {
    Member *next;
    Type *ty;
    Token *name;
    uint32_t idx;
    int offset;
};

bool is_integer(Type *ty);
bool is_pointer(Type *ty);
Type *pointer_to(Type *base);
Type *func_type(Type *return_ty);
Type *array_of(Type *base, int size);
Type *enum_type(void);
Type *copy_type(Type *ty);
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
} IrKind;

enum {
    RSlot,
    RTmp,
    RInt,
    RGlb,
};

#define R \
    (Ref) { RTmp, 0, NULL }
#define TMP(x, ty) \
    (Ref) { RTmp, x, ty }
#define SLOT(x, ty) \
    (Ref) { RSlot, x, ty }
#define INT(x) \
    (Ref) { RInt, x, ty_int }
#define LONG(x) \
    (Ref) { RInt, x, ty_long }
#define GLB(x, ty) \
    (Ref) { RGlb, x, ty }

struct Ref {
    uint32_t type : 3;
    int32_t val : 29;
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
    } jmp;

    Blk *succ1;
    Blk *succ2;
    Blk *next;
};

struct Module {
    Sym *fns;
    Sym *data;
    Type *tys;
};

Module *irgen(Module *node);
void dump_module(Module *module, FILE *out);

//
// util.c
//

void fatal(char *fmt, ...);
void error(char *loc, const char *msg, ...);
void warning(char *loc, const char *msg, ...);
void note(char *loc, const char *msg, ...);

void *emalloc(size_t n);
void freeall(void);
void *vnew(size_t len, size_t esz);
void *vgrow(void *data, size_t len);

char *format(char *s, ...);
uint32_t intern(char *s, int len);
char *str(uint32_t id);
uint32_t str_len(uint32_t id);
char *escape_char_to_string(char c);

#endif  // CXX_H_
