#ifndef CXX_H_
#define CXX_H_

#define ALIGN_UP(value, align) (((value) + (align) - 1) & ~((align) - 1))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define BIT_HAS(bits, n) (((bits) & (n)) != 0)
#define BIT_SET(bits, n) ((bits) |= (n))
#define BIT_INTER(a, b) ((a) & (b))
#define BIT_UNION(a, b) ((a) | (b))
#define BIT_DIFF(a, b) ((a) & ~(b))
#define BIT_SUPERSET(a, b) (((a) & (b)) == (b))
#define BIT_SUBSET(a, b) (((a) & (b)) == (a))

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct SrcFile SrcFile;
typedef struct Token Token;
typedef struct Node Node;
typedef struct Type Type;
typedef struct Ref Ref;
typedef struct Ir Ir;
typedef struct Phi Phi;
typedef struct Blk Blk;
typedef struct Sym Sym;
typedef struct Fn Fn;
typedef struct Module Module;
typedef struct Con Con;
typedef struct Member Member;
typedef struct EnumVal EnumVal;
typedef struct Initializer Initializer;

extern Type *ty_void;
extern Type *ty_nullptr;
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
extern Type *ty_float;
extern Type *ty_double;
extern Type *ty_ldouble;
extern Type *ty_i1;
extern Type *ty_i32;
extern Type *ty_i64;

struct SrcFile {
    char *name;
    uint32_t id;
    int file_no;
    char *contents;
    size_t size;
    uint32_t *line_offsets;
    int num_lines;
};

//
// main.c
//

extern char *base_file;
extern char **include_paths;
extern int num_include_paths;

bool file_exists(char *path);

//
// Lexer
//

enum {
    PREFIX_NONE,
    PREFIX_u8,
    PREFIX_u,
    PREFIX_U,
    PREFIX_L,
};

enum {
    TK_EOF,
    TK_NL,
    TK_WS,
    TK_COMMENT,
    TK_LINE,
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
    TK_OTHER,
    TK_ERR,
    TK_WARN,
};

struct Token {
    Token *next;
    Token *origin;  // If this is expanded from a macro, the original token
    union {
        uint32_t id;   // Uesd if kind == TK_IDENT;
        uint64_t val;  // Uesd if kind == TK_NUM;
        double fval;   // Uesd if kind == TK_NUM;
        char *msg;     // Used if token is broken;
    };
    Type *ty;       // Used if TK_NUM or TK_STR
    SrcFile *file;  // Source location
    char *loc;
    uint32_t filename;  // Diagnostic filename
    int32_t line_delta;
    uint16_t len;
    uint8_t kind;
    uint8_t enc_prefix;  // Used if kind == TK_CHARLIT or kind == TK_STRLIT
    bool is_sol;         // true if is starting of line
    bool is_leadingws;   // true if is leading space
    bool noexpand;       // true if this token shall not be macro-expanded
};

bool match(Token **rest, Token *tok, uint32_t kind);
Token *skip(Token *tok, uint32_t kind);
Token *tokenize_file(char *filename);
SrcFile *new_file(char *name, int file_no, char *contents);
Token *tokenize(SrcFile *file);
void convert_ppnumber(Token *tok);
void convert_keywords(Token *tok);
void convert_str_literal(Token *tok);
SrcFile **get_input_files(void);
void get_location(SrcFile *f, char *loc, int *out_line, int *out_col);

//
// preprocess.c
//

void init_macros(void);
void cmd_include_file(char *str);
void cmd_define_macro(char *str);
void cmd_undef_macro(char *name);
Token *preprocess(Token *tok);
void join_adjacent_string_literals(Token *tok1);

//
// Parser
//

enum {
    Q_INLINE = 1 << 0,
    Q_NORETURN = 1 << 1,
};

typedef enum {
    SC_NONE,
    SC_EXTERN = 1 << 0,
    SC_STATIC = 1 << 1,
    SC_REG = 1 << 2,
    SC_THREAD = 1 << 3,
    SC_TYPEDEF = 1 << 4,
} SClass;

// Variable or function
struct Sym {
    Sym *next;
    uint32_t id;  // Variable name
    Type *ty;     // Type
    int align;    // alignment
    SClass sclass;

    // Local variable
    bool is_local;  // local or global/function
    int vreg;       // Virtual reg id

    // Global variable or function
    bool is_function;
    bool is_defined;
    bool is_str;

    // Global variable
    uint32_t init_data;

    Initializer *init;

    // Function
    uint32_t funcspec;
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
    ND_INIT,

    // Term
    ND_VAR,      // Variable
    ND_NUM,      // Int
    ND_NULLPTR,  // nullptr
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
                Node *case_next;
            };
            Blk *brk_blk;
            Blk *cont_blk;
        };
        struct {
            // Function call
            Node *func;
            Type *func_ty;
            Node *args;
            uint32_t narg;
        };
        struct {
            uint32_t label;
            union {
                Node *target;
            };
            Node *goto_next;
            Node *loop_next;
            bool is_loop;
            bool is_switch;
            Blk *blk;
        };
        struct {
            Sym *var;  // Used if kind == ND_VAR
            Node *var_init;
        };
        int64_t val;  // Used if kind == ND_NUM
        double fval;  // Used if kind == ND_NUM
    };
    Node *label_ring;
    Node *label_body;
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

    // Only one member can be initialized for a union.
    // `mem` is used to clarify which member is initialized.
    Member *mem;
};

int64_t const_expr(Token **rest, Token *tok);
Node *new_unary(NodeKind kind, Node *expr, Token *tok);
void new_imcast(Node **expr, Type *ty);
void lvalue_convert(Node **expr);
void integer_promotion(Node **expr);
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
    TY_NULLPTR,
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
            // Array or ptr
            int len;
            bool is_static;
            bool is_star;
        };
        struct {
            // Function
            Type *ret;
            Type *params;
            uint32_t nparam;
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

    // Bitfield
    bool is_bitfield;
    int bit_offset;
    int bit_width;
    Type *unit_ty;
};

struct EnumVal {
    EnumVal *next;
    Token *name;
    int64_t val;
};

enum {
    CTX_AS,
    CTX_INIT,
    CTX_CALL,
    CTX_RET,
};

bool is_void(Type *ty);
bool is_bool(Type *ty);
bool is_integer(Type *ty);
bool is_flonum(Type *ty);
bool is_arith(Type *ty);
bool is_pointer(Type *ty);
bool is_nullptr(Type *ty);
bool is_null_constant(Node *node);
bool is_scalar(Type *ty);
bool is_record(Type *ty);
bool is_funcptr(Type *ty);
bool is_compatible(Type *t1, Type *t2);
void check_asop(Type *dst, Node *src, int ctx);
Type *pointer_to(Type *base, uint32_t qual);
Type *func_type(Type *return_ty);
Type *array_of(Type *base, int size);
Type *struct_type(bool is_union);
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
    IR_EXT,
    IR_TRUNC,
    IR_INTTOFP,
    IR_FPTOINT,
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
        double d;
    } bits;
};

enum {
    RUndef,
    RTmp,
    RCon,
    RSlot,
    RGlb,
};

#define R \
    (Ref) { RUndef, 0, NULL }
#define TMP(x, ty) \
    (Ref) { RTmp, x, ty }
#define SLOT(x, ty) \
    (Ref) { RSlot, x, ty }
#define GLB(x, ty) \
    (Ref) { RGlb, x, ty }
#define CON(x, ty) \
    (Ref) { RCon, x, ty }

#define BOOL(x)                    \
    ({                             \
        Ref tmp = getcon(x, curm); \
        tmp.ty = ty_bool;          \
        tmp;                       \
    })
#define INT(x)                     \
    ({                             \
        Ref tmp = getcon(x, curm); \
        tmp.ty = ty_int;           \
        tmp;                       \
    })
#define LONG(x)                    \
    ({                             \
        Ref tmp = getcon(x, curm); \
        tmp.ty = ty_long;          \
        tmp;                       \
    })
#define FLOAT(x)                   \
    ({                             \
        Ref tmp = getcon(x, curm); \
        tmp.ty = ty_float;         \
        tmp;                       \
    })
#define DOUBLE(x)                  \
    ({                             \
        Ref tmp = getcon(x, curm); \
        tmp.ty = ty_double;        \
        tmp;                       \
    })
#define NULLPTR                                        \
    ({                                                 \
        Ref tmp = newcon(&(Con){CAddr, 0, {0}}, curm); \
        tmp.ty = ty_nullptr;                           \
        tmp;                                           \
    })

struct Ref {
    uint32_t type;
    uint32_t val;
    Type *ty;
};

static inline int refeq(Ref a, Ref b) { return a.type == b.type && a.val == b.val && a.ty == b.ty; }

struct Ir {
    Ref dst;
    Ir *prev, *next;
    uint16_t op;
    uint16_t narg;
    Ref args[];
};

struct Phi {
    Ref result;
    Ref *arg;
    Blk **blk;
    int num_arg;
    Phi *next;
};

struct Blk {
    int blk_id;
    Blk *next;
    Phi *phi;

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

    Blk **pred;
    uint32_t num_pred;
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
void dump_raw_tokens(Token *tok);
void dump_tokens(Token *tok);
void fold_ast(Module *prog);

//
// unicode.c
//

int encode_utf8(char *buf, uint32_t c);
uint32_t decode_utf8(char **new_pos, char *p, bool *success);
bool is_ident1(uint32_t c);
bool is_ident2(uint32_t c);
int display_width(char *p, int len);

//
// util.c
//

void fatal(char *fmt, ...);
void error_at(SrcFile *file, char *loc, const char *msg, ...);
void error(Token *tok, const char *msg, ...);
void warning(Token *tok, const char *msg, ...);
void diag(char *level, Token *tok, const char *msg, ...);
void diag_exit(char *level, Token *tok, const char *msg, ...);

void *emalloc(size_t n);
void freeall(void);
void *vnew(size_t len, size_t esz);
void *vgrow(void *data, size_t len);

char *format(char *s, ...);
uint32_t intern(char *s, uint32_t len);
char *str(uint32_t id);
uint32_t str_len(uint32_t id);
char *escape_char_to_string(char c);

Ref newcon(Con *c0, Module *md);
Ref getcon(int64_t val, Module *md);

#endif  // CXX_H_
