#ifndef CXX_H_
#define CXX_H_

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Token Token;
typedef struct Node Node;

//
// util.c
//

void *emalloc(size_t n);
void freeall(void);

//
// Lexer
//

typedef enum {
    TK_NOP,
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

    TK_IDENT,
    TK_KEYWORD,
    TK_NUM,
    TK_PPNUM,
    TK_CHAR,
    TK_STR,
    TK_EOF,
} TokenKind;

struct Token {
    TokenKind kind;
    Token *next;
    char *loc;
    size_t len;
    int val;
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
    ND_EXPR_STMT,  // Expression statement
    ND_VAR,        // Variable
    ND_NUM,        // Int
} NodeKind;

// AST node type
struct Node {
    NodeKind kind;  // Node kind
    Node *next;     // Next node
    Node *lhs;      // Left-hand side
    Node *rhs;      // Right-hand side
    Obj *var;
    int val;
};

Function *parse(Token *tok);

//
// irgen.c
//

void irgen(Function *node);

#endif  // CXX_H_
