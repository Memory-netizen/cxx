#ifndef CXX_H_
#define CXX_H_

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Token Token;
typedef struct Node Node;

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

    // Storage-class specifiers
    TK_CONSTEXPR,
    TK_EXTERN,
    TK_REGISTER,
    TK_STATIC,
    TK_THREAD,
    TK_TYPEDEF,

    // Type specifiers
    TK_AUTO,  // Auto get type
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
    TK_TYPEOF_UNQUAL,

    // Type qualifiers
    TK_CONST,
    TK_RESTRICT,
    TK_VOLATILE,
    TK_ATOMIC,

    // Function specifiers
    TK_INLINE,
    TK_NORETURN,

    TK_ALIGNAS,
    TK_ALIGNOF,
    TK_COUNTOF,
    TK_SIZEOF,

    TK_GENERIC,
    TK_ASM,
    TK_ATTRIBUTE,

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
    int val;  // Uesd if kind == TK_NUM;
};

Token *tokenize(char *input);

//
// Parser
//

// Local variable
typedef struct Obj Obj;
struct Obj {
    Obj *next;
    char *name;  // Variable name
    int vreg;    // No of virtual reg
};

// Function
typedef struct Function Function;
struct Function {
    Node *body;
    Obj *locals;
};

typedef enum {
    ND_COMMA,      // ,
    ND_AS,         // =
    ND_BOR,        // |
    ND_XOR,        // ^
    ND_BAND,       // &
    ND_EQ,         // ==
    ND_NE,         // !=
    ND_LT,         // <
    ND_GT,         // >
    ND_LE,         // <=
    ND_GE,         // >=
    ND_LEFT,       // <<
    ND_RIGHT,      // >>
    ND_ADD,        // +
    ND_SUB,        // -
    ND_MUL,        // *
    ND_DIV,        // /
    ND_MOD,        // %
    ND_PLUS,       // unary +
    ND_NEG,        // unary -
    ND_NOT,        // !
    ND_INVERT,     // ~
    ND_RETURN,     // return
    ND_IF,         // if
    ND_WHILE,      // while
    ND_DO,         // do
    ND_FOR,        // for
    ND_EXPR_STMT,  // Expression statement
    ND_COMP_STMT,  // {...}
    ND_VAR,        // Variable
    ND_NUM,        // Int
} NodeKind;

// AST node type
struct Node {
    NodeKind kind;  // Node kind
    Node *next;     // Next node

    union {
        struct {
            Node *lhs;  // Left-hand side
            Node *rhs;  // Right-hand side
        };
        struct {
            Node *init;
            Node *cond;
            union {
                Node *then;
                Node *body;  // Block
            };
            union {
                Node *els;
                Node *inc;
            };
        };
        Obj *var;  // Used if kind == ND_VAR
        int val;   // Used if kind == ND_NUM
    };
};

Function *parse(Token *tok);

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

    // Conversion
    IR_EXT,

    // Compare
    IR_CMP_NE,
    IR_CMP_EQ,
    IR_CMP_GE,
    IR_CMP_GT,
    IR_CMP_LE,
    IR_CMP_LT,
} IrKind;

typedef struct Ref Ref;
typedef struct Ir Ir;
typedef struct Blk Blk;

enum {
    RSlot,
    RTmp,
    RInt,
};

#define R \
    (Ref) { RTmp, 0 }
#define TMP(x) \
    (Ref) { RTmp, x }
#define SLOT(x) \
    (Ref) { RSlot, x }
#define INT(x) \
    (Ref) { RInt, x }

struct Ref {
    uint32_t type : 3;
    int32_t val : 29;
};

static inline int refeq(Ref a, Ref b) { return a.type == b.type && a.val == b.val; }

struct Ir {
    IrKind op;
    Ref dst;
    Ref args[2];
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

Blk *irgen(Function *node);
void dump_fn(Blk *b);

//
// util.c
//

void *emalloc(size_t n);
void freeall(void);
void *vnew(size_t len, size_t esz);
void *vgrow(void *data, size_t len);

#endif  // CXX_H_
