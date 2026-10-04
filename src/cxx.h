#ifndef CXX_H_
#define CXX_H_

#define ALIGN_UP(value, align) (((value) + (align) - 1) & ~((align) - 1))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define BIT_SUPERSET(a, b) (((a) & (b)) == (b))

#ifndef __GNUC__
#define __attribute__(x)
#endif

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

#include "attr.h"
#include "support/fp128.h"

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
typedef struct Target Target;

struct Target {
    char *name;
    char *triple;
    char *datalayout;
    char *sysroot;
    Type *ty_none;
    Type *ty_void;
    Type *ty_voidptr;  // void *, shared by every site needing one
    Type *ty_nullptr;
    Type *ty_bool;
    Type *ty_char;
    Type *ty_schar;
    Type *ty_uchar;
    Type *ty_short;
    Type *ty_ushort;
    Type *ty_int;
    Type *ty_uint;
    Type *ty_long;
    Type *ty_ulong;
    Type *ty_llong;
    Type *ty_ullong;
    Type *ty_float;
    Type *ty_double;
    Type *ty_ldouble;
    Type *ty_wchar;
    uint64_t int_max, uint_max;
    uint64_t long_max, ulong_max;
    uint64_t llong_max;
    bool ldouble_is_fp80;  // x87 80-bit (amd64) vs binary128 (others)
    char *llvm_features;   // target-features attribute for LLVM codegen
    char *llvm_abi;        // "target-abi" module flag (e.g. "lp64d")
    char *clang_mabi;      // -mabi driver flag matching llvm_abi (bare metal)
    char *clang_march;     // -march driver flag matching llvm_features
    char *predef;
};

extern Target T;
extern Type *bitint[129][2];
extern Type *f16;
extern Type *f32;
extern Type *f64;
extern Type *f128;

// Helpers for the new arithmetic types (type.c)
FpFormat fmt_of(Type *ty);
int bitint_width(Type *ty);
bool is_bitint128(Type *ty);
int64_t norm_bits(int64_t v, int width, bool is_unsigned);

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
extern char **embed_dirs;
extern int num_embed_dirs;

bool file_exists(char *path);

//
// Lexer
//

enum {
    SUF_UNSIGNED = 0x001,
    SUF_LONG = 0x002,
    SUF_LLONG = 0x004,
    SUF_FLOAT = 0x08,
    SUF_DOUBLE = 0x010,
    SUF_LDOUBLE = 0x020,
    SUF_BITINT = 0x040,
    SUF_F16 = 0x080,
    SUF_F32 = 0x100,
    SUF_F64 = 0x200,
    SUF_F128 = 0x400,
    SUF_NONDEC = 0x800,
};

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

    // Single-byte punctuators are encoded directly as their ASCII code.
    TK_COMMA = ',',
    TK_AS = '=',
    TK_BOR = '|',
    TK_XOR = '^',
    TK_BAND = '&',
    TK_LT = '<',
    TK_GT = '>',
    TK_PLUS = '+',
    TK_MINUS = '-',
    TK_STAR = '*',
    TK_SLASH = '/',
    TK_MOD = '%',
    TK_INVERT = '~',
    TK_NOT = '!',
    TK_DOT = '.',
    TK_LPAREN = '(',
    TK_RPAREN = ')',
    TK_LBRACKET = '[',
    TK_RBRACKET = ']',
    TK_LBRACE = '{',
    TK_RBRACE = '}',
    TK_SEMI = ';',
    TK_COLON = ':',
    TK_QUESTION = '?',
    TK_HASH = '#',

    // Multi-byte punctuators, keywords and all other kinds start at 128.
    TK_COLONCOLON = 128,  // ::
    TK_ELLIPSIS,          // ...
    TK_HASHHASH,          // ##
    TK_ADDAS,             // +=
    TK_SUBAS,             // -=
    TK_MULAS,             // *=
    TK_DIVAS,             // /=
    TK_MODAS,             // %=
    TK_ANDAS,             // &=
    TK_ORAS,              // |=
    TK_XORAS,             // ^=
    TK_LEFTAS,            // <<=
    TK_RIGHTAS,           // >>=
    TK_OR,                // ||
    TK_AND,               // &&
    TK_EQ,                // ==
    TK_NE,                // !=
    TK_LE,                // <=
    TK_GE,                // >=
    TK_LEFT,              // <<
    TK_RIGHT,             // >>
    TK_INC,               // ++
    TK_DEC,               // --
    TK_ARROW,             // ->
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
    TK_F16,
    TK_F32,
    TK_F64,
    TK_F128,
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
    // GNU `__extension__`: a no-op marker that suppresses pedantic
    // diagnostics. cxx has no such diagnostics yet, so it is simply
    // consumed (see the TK_EXTENSION cases in parser.c). It is placed
    // after TK_WHILE on purpose: the contiguous keyword range that
    // tk_is_keyword() tests is TK_KEYWORD..TK_WHILE.
    TK_EXTENSION,
    TK_OTHER,
    TK_ERR,
    TK_WARN,

    TK_NKIND,  // number of token kinds
};

struct Token {
    Token *next;
    Token *origin;  // If this is expanded from a macro, the original token
    union {
        uint32_t id;  // Used if kind == TK_IDENT (also TK_LINE line numbers);
        char *msg;    // Used if token is broken;
        Fp128 fpval;  // TK_NUM floating constants
        Int128 ival;  // TK_NUM integer constants and TK_CHARLIT values
    };
    SrcFile *file;  // Source location
    uint32_t loc;   // byte offset into file->contents
    uint32_t len;
    uint32_t filename;  // Diagnostic filename
    int32_t line_delta;
    union {
        // SUF_NONDEC is 0x800: needs more than 8 bits
        uint16_t lit_suffix;  // Used if kind == TK_NUM
        uint8_t enc_prefix;   // Used if kind == TK_CHARLIT or kind == TK_STRLIT
    };
    uint8_t kind;
    bool is_sol;        // true if is starting of line
    bool is_leadingws;  // true if is leading space
    bool noexpand;      // true if this token shall not be macro-expanded
};

static inline char *tok_text(Token *tok) { return tok->file->contents + tok->loc; }

bool match(Token **rest, Token *tok, uint32_t kind);
Token *skip(Token *tok, uint32_t kind);
Token *tokenize_file(char *filename);
SrcFile *new_file(char *name, int file_no, char *contents);
Token *tokenize(SrcFile *file);
void convert_ppnumber(Token *tok);
void convert_keywords(Token *tok);
void convert_str_literal(Token *tok);
SrcFile **get_input_files(void);
void get_location(SrcFile *f, uint32_t loc, int *out_line, int *out_col);

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
    SC_AUTO = 1 << 4,
    SC_CONSTEXPR = 1 << 5,
    SC_TYPEDEF = 1 << 6,
} SClass;

// Variable or function
struct Sym {
    Sym *next;
    Sym *str_next;  // string-literal dedup chain
    uint32_t id;    // Variable name
    Type *ty;       // Type
    int align;      // alignment
    SClass sclass;

    // Local variable
    bool is_local;  // local or global/function
    int vreg;       // Virtual reg id

    // Global variable or function
    bool is_function;
    // Set only on the symbols the parser injects for A-class builtins. A
    // user declaration of the same name leaves it false, so the folder and
    // irgen can tell "the compiler's builtin" from "a user function that
    // happens to carry a builtin's name".
    bool is_builtin;
    bool is_defined;
    bool is_str;

    // GNU asm-name: `int f(void) __asm__("real_symbol");` declares the
    // C identifier f but emits/refers to real_symbol in the object file.
    // NULL when the declaration has no asm label.
    char *asm_name;

    // Attribute flags
    bool is_deprecated;
    bool is_nodiscard;
    bool is_maybe_unused;
    bool is_unused;

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

    int num_blk;
    int num_lbl;  // label blocks occupy blks[0..num_lbl)
    Blk *blks;

    int num_indirectbr;
    Blk **indirectbr;
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
    ND_GT,       // >
    ND_GE,       // >=
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
    ND_FENCE,
    ND_CAS,        // Atomic compare-and-swap
    ND_ATOMICRMW,  // Atomic read-modify-write (atomicrmw)
    ND_ALLOCA,     // __builtin_alloca / __builtin_alloca_with_align

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
    ND_GOTO_EXPR,  // "goto" labels-as-values
    ND_LABEL,      // Labeled statement
    ND_LABEL_VAL,  // [GNU] Labels-as-values
    ND_BREAK,      // "break"
    ND_CONTINUE,   // "continue"
    ND_SWITCH,     // "switch"
    ND_CASE,       // "case"

    // Declare
    ND_DECL,
    ND_INIT,

    // Term
    ND_VAR,        // Variable
    ND_NUM,        // Int
    ND_NULLPTR,    // nullptr
    ND_SUBACCESS,  // E[m] on an array operand (C2y 6.5.3.2)
    ND_SP_SAVE,
    ND_SP_RESTORE,
    // Byte swap of the integer in lhs, width taken from rhs->ival (16, 32
    // or 64). Lowered to the llvm.bswap.iN intrinsic; kept as its own node
    // so no shift/mask tree has to be synthesized (and typed) by hand.
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
            Node *lhs;  // Left-hand side
            Node *rhs;  // Right-hand side
            Node *desired;
            Member *member;       // Struct member access
            Type *compute_ty;     // Compound assign
            uint16_t mem_order;   // Atomic access memory order + 1; 0 = unspecified (seq_cst)
            uint16_t mem_order1;  // Atomic access memory order + 1; 0 = unspecified (seq_cst)
            int armw_op;          // ND_ATOMICRMW: A_* operation (atomicrmw)
            bool is_weak;
            bool is_signal;
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
            Node *args;
            uint32_t narg;
            Type *base_ty;
        };
        struct {
            uint32_t label;
            Node *target;
            Node *goto_next;
            Node *loop_next;
            int blk_idx;  // label block: index into the fn->blks array
            bool is_loop;
            bool is_switch;
            bool is_ref;
            bool is_addr;
        };
        struct {
            Sym *var;  // Used if kind == ND_VAR
            Node *var_init;
        };
        Fp128 fpval;  // ND_NUM floating constants
        Int128 ival;  // ND_NUM integer constants
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
bool constexpr_fold(Sym *var, int64_t *val, uint32_t *sym);
Fp128 eval_fp128(Node *node);
Node *new_node(NodeKind kind, Token *tok);
// Builtin identifiers, shared by every stage that dispatches on them: the
// parser (implicit declaration), opt_ast.c (constant folding), irgen.c (IR
// lowering) and the preprocessor (__has_builtin). One enum means those
// four stay in step, and -Wswitch on the dispatches forces a new builtin
// to be handled everywhere.
enum {
    BUILTIN_NONE = 0,  // is_builtin_fn() reports "not a builtin" with this
    BUILTIN_FN_ALLOCA = 1,
    BUILTIN_ALLOCA_WITH_ALIGN,
    BUILTIN_CONSTANT_P,
    BUILTIN_TYPES_COMPATIBLE_P,
    ATOMIC_STORE,
    ATOMIC_LOAD,
    ATOMIC_EXCHANGE,
    ATOMIC_FETCH_ADD,
    ATOMIC_FETCH_SUB,
    ATOMIC_FETCH_AND,
    ATOMIC_FETCH_OR,
    ATOMIC_FETCH_XOR,
    ATOMIC_COMPARE_EXCHANGE_WEAK,
    ATOMIC_COMPARE_EXCHANGE_STRONG,
    ATOMIC_THREAD_FENCE,
    ATOMIC_SIGNAL_FENCE,
    ATOMIC_IS_LOCK_FREE,
    // __builtin_bswap16/32/64
    BUILTIN_BSWAP16,
    BUILTIN_BSWAP32,
    BUILTIN_BSWAP64,

    // Bit counting. All return int whatever the operand width, so the
    // prototype is fixed and the width comes from the operand type: the
    // argument is converted to the declared parameter type first, which is
    // what makes a narrow operand count within 32 bits.
    BUILTIN_CLZ,
    BUILTIN_CLZL,
    BUILTIN_CLZLL,
    BUILTIN_CTZ,
    BUILTIN_CTZL,
    BUILTIN_CTZLL,
    BUILTIN_POPCOUNT,
    BUILTIN_POPCOUNTL,
    BUILTIN_POPCOUNTLL,

    // One past the last kind: the table's length, so nothing has to keep a
    // separate count in step with the enum. Not a builtin itself.
    NUM_BUILTINFN,
};

// How a builtin is handled. The distinction is whether it can be written
// as an ordinary C function prototype: if so the parser injects a real
// declaration and everything downstream sees a normal call, which gives
// the builtin a type, an address and a place in overload resolution.
typedef enum {
    BCLASS_DECL,     // implicit `extern` declaration; ordinary ND_FUNCALL
    BCLASS_SPECIAL,  // needs parse_builtin_fn: no prototype can express it
} BuiltinClass;

// Everything one builtin needs, in one row; defined in parser.c so that
// every stage shares a single table. Adding a builtin that is a plain
// intrinsic over a fixed prototype -- the common case -- is one entry.
//
// `intrinsic` is a printf format taking the operand width in bits, or NULL
// when irgen handles the builtin by kind instead.
typedef struct BuiltinDef {
    char *name;  // the C spelling, e.g. "__builtin_bswap32"
    // No kind field: builtin_defs[] is written in BUILTIN_* order, so a
    // row's kind is its index + 1. Storing it too would be a second
    // source of truth. parser.c asserts the table covers every kind.
    BuiltinClass cls;
    char *intrinsic;  // e.g. "llvm.bswap.i%d", or NULL
    int ret;          // BuiltinTargetType selector for the result
    int args;         // BuiltinTargetType selector for the parameters
    bool uniform;     // every parameter has the type above
    // A literal i1 argument appended after the operands, or -1 for none.
    // llvm.ctlz/cttz are the reason: their second argument (is_zero_undef,
    // an immarg) is true for clz/ctz and false for clrsb, so it is a
    // property of the builtin rather than a value the call site supplies.
    // It is emitted as a real operand, not spliced into the intrinsic name.
    int extra_arg;
    uint32_t nargs;
    Token *tok;   // spelling token; carries the file for diagnostics
    uint32_t id;  // interned name, filled on first use
} BuiltinDef;

// Selects a Type out of Target; used by BuiltinDef.ret/args.
typedef enum {
    BT_VOID,
    BT_BOOL,
    BT_SHORT,
    BT_USHORT,
    BT_INT,
    BT_UINT,
    BT_LONG,
    BT_ULONG,
    BT_LLONG,
    BT_ULLONG,
    BT_VOIDPTR,
    BT_NONE,  // BCLASS_SPECIAL: the shape comes from parser code
} BuiltinTargetType;

extern BuiltinDef builtin_defs[];
#define BUILTIN_ROW(kind) (builtin_def(kind))
BuiltinDef *builtin_def(int kind);
// The declared type of a builtin: a function type with its parameters
// attached (func_type() alone sets only the return type, and fncall()
// walks params and reports arity with nparam).
Type *builtin_type(int kind);
// Constant-fold a builtin call; NULL when it cannot be folded. Shared with
// the parser, because global initialisers go through eval2(), which never
// runs fold_ast().
Node *fold_builtin_call(int kind, Node *call);
// Record the '[' that declared one array dimension, for the AST dumper.
void array_bracket_note(Type *ty, Token *l_bracket);

int is_builtin_fn(uint32_t id);
BuiltinClass builtin_class(int kind);
// The builtin kind of a call's callee, or BUILTIN_NONE. Accepts the shapes a
// callee takes after postfix() decayed a function designator.
int builtin_kind_of(Node *func);
Node *new_unary(NodeKind kind, Node *expr, Token *tok);
void new_imcast(Node **expr, Type *ty);
void lvalue_convert(Node **expr);
void modifiable_lvalue(Node *node);
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
    Q_ATOMIC = 1 << 4,
};

// Memory orders (C11 7.17.3; values of __ATOMIC_* / memory_order).
enum {
    MEM_ORDER_RELAXED,
    MEM_ORDER_CONSUME,
    MEM_ORDER_ACQUIRE,
    MEM_ORDER_RELEASE,
    MEM_ORDER_ACQ_REL,
    MEM_ORDER_SEQ_CST,
};

typedef enum {
    TY_NONE,
    TY_VOID,
    TY_NULLPTR,
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
    TY_VLA,
    TY_ARRAY,
    TY_STRUCT,
    TY_UNION,
    TY_F16,
    TY_F32,
    TY_F64,
    TY_F128,
    TY_BITINT = 0x1000,
} TypeKind;

struct Type {
    TypeKind kind;
    uint32_t qual;     // qualifiers
    int size;          // sizeof() value
    int align;         // alignof() value
    bool is_unsigned;  // unsigned or signed
    bool is_packed;    // packed attribute on a record type
    uint32_t id;
    uint32_t uid;
    // Declaration
    Token *name;
    Type *next;
    Type *base;
    Type *origin;  // for type compatibility check
    Attr *attrs;   // attributes attached to the type

    // Data
    union {
        struct {
            // Array or ptr
            int len;
            bool is_static;
            bool is_star;
        };
        struct {
            // Variable-length array
            Node *vla_len;  // # of elements
            Sym *vla_cnt;   // _Countof() value
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
    bool is_packed;

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
    Attr *attrs;
};

enum {
    CTX_AS,
    CTX_INIT,
    CTX_CALL,
    CTX_RET,
};

bool is_void(Type *ty);
bool is_bool(Type *ty);
bool is_char(Type *ty);
bool is_integer(Type *ty);
bool is_flonum(Type *ty);
bool is_interchange(Type *ty);
bool is_fpval(Type *ty);
bool is_arith(Type *ty);
bool is_pointer(Type *ty);
bool is_nullptr(Type *ty);
bool is_null_constant(Node *node);
bool is_scalar(Type *ty);
bool is_record(Type *ty);
bool is_array(Type *ty);
bool is_funcptr(Type *ty);
bool is_compatible(Type *t1, Type *t2);
int float_rank(Type *ty);
void check_asop(Type *dst, Node *src, int ctx);
Type *pointer_to(Type *base, uint32_t qual);
Type *func_type(Type *return_ty);
Type *array_of(Type *base, int size);
Type *vla_of(Type *base, Node *expr);
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
    IR_INDIRECTBR,
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
    IR_EXTRACTVAL,
    IR_FENCE,
    IR_CMPXCHG,
    IR_ATOMICRMW,

    IR_MEMCPY,
    IR_MEMMOV,
    IR_MEMSET,
    IR_TLSADDR,
    IR_SP_SAVE,
    IR_SP_RESTORE,

    // Conversion
    IR_EXT,
    IR_TRUNC,
    IR_INTTOFP,
    IR_FPTOINT,
    IR_PTRTOINT,
    IR_INTTOPTR,
    IR_BITCAST,

    // Compare
    IR_CMP_NE,
    IR_CMP_EQ,
    IR_CMP_LE,
    IR_CMP_LT,

    // Other
    IR_CALL,
    IR_VA_ARG,
    IR_VA_START,
    IR_VA_COPY,
    IR_VA_END,
    IR_SELECT,
    IR_CNT,
} IrKind;

enum {
    A_XCHG,
    A_ADD,
    A_SUB,
    A_AND,
    A_NAND,
    A_OR,
    A_XOR,
    A_MAX,
    A_MIN,
    A_UMAX,
    A_UMIN,
    A_FADD,
    A_FSUB,
    A_FMAX,
    A_FMIN,
    A_UINC_WRAP,
    A_UDEC_WRAP,
    A_USUB_COND,
    A_USUB_SAT,
};

struct Con {
    int hnext;  // constant-pool hash chain (index into Module.con)
    enum {
        CUndef,
        CBits,
        CBits128,
        CAddr,
    } type;
    uint32_t sym;
    union {
        int64_t i;
        Int128 i128;  // _BitInt(65..128)
        Fp128 f128;   // fp128 / f16 / f32 / f64 bit patterns
    } bits;
};

enum {
    RUndef,
    RTmp,
    RCon,
    RInt,  // int32 immediate, encoded directly in val (no pool)
    RSlot,
    RGlb,
    RLabel,
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

// Small integer immediates are encoded directly in the Ref (RInt) and
// never touch the constant pool; only values outside the int32 range
// go through getcon/newcon.
#define BOOL(x) ((Ref){RInt, (int32_t)(x), T.ty_bool})
#define INT(x) ((Ref){RInt, (int32_t)(x), T.ty_int})
#define LONG(x)                                                                                             \
    ({                                                                                                      \
        Ref tmp = (int64_t)(x) >= INT_MIN && (int64_t)(x) <= INT_MAX ? (Ref){RInt, (int32_t)(x), T.ty_long} \
                                                                     : getcon(x, curm);                     \
        tmp.ty = T.ty_long;                                                                                 \
        tmp;                                                                                                \
    })
#define FLOAT(x)                   \
    ({                             \
        Ref tmp = getcon(x, curm); \
        tmp.ty = T.ty_float;       \
        tmp;                       \
    })
#define DOUBLE(x)                  \
    ({                             \
        Ref tmp = getcon(x, curm); \
        tmp.ty = T.ty_double;      \
        tmp;                       \
    })
#define LDOUBLE(x)                 \
    ({                             \
        Ref tmp = getcon(x, curm); \
        tmp.ty = T.ty_ldouble;     \
        tmp;                       \
    })
// A null pointer constant: the integer 0, typed void * -- the same thing
// as (void *)0. It needs no nullptr-specific machinery, so it is an RInt
// immediate (zero is always representable) rather than a constant-pool
// entry, and every site that needs a null pointer shares this one form.
#define NULLPTR                \
    ({                         \
        Ref tmp = INT(0);      \
        tmp.ty = T.ty_voidptr; \
        tmp;                   \
    })

// 16 bytes on 64-bit hosts: passed/returned in two registers, while a
// 24-byte struct would go through a hidden sret pointer (memory traffic
// on every Ref passed by value in irgen/dumpir).
struct Ref {
    uint32_t type;
    int32_t val;  // RTmp/RSlot id, RCon/RGlb index/id, RInt immediate,
                  // RLabel intern id of "fnname..idx"
    Type *ty;
};

static inline int refeq(Ref a, Ref b) { return a.type == b.type && a.val == b.val && a.ty == b.ty; }

struct Ir {
    Ref dst;
    Ir *prev, *next;
    uint16_t op;
    uint16_t narg;
    uint8_t mem_order;   // Atomic load/store order; cmpxchg success order
    uint8_t mem_order1;  // cmpxchg failure order
    uint8_t is_weak;     // cmpxchg weak
    uint8_t is_signal;
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
    int blk_no;
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
    // Hash over con (chain addressing, stored as indices so vgrow may
    // relocate the array) so constant-pool dedup stays O(1) for
    // modules with many globals/64-bit constants.
    int *con_ht;
    int con_cap;
    int con_n;
    bool has_vla;
};

Module *irgen(Module *node);
void dump_module(Module *module, FILE *out);
// True when tok is in the contiguous keyword range TK_KEYWORD..TK_WHILE.
// Keyword tokens keep their interned id (keywordize only rewrites kind),
// so callers needing the spelling (e.g. attribute names such as
// __const__) can use str(tok->id).
static inline bool tk_is_keyword(Token *tok) { return tok->kind >= TK_KEYWORD && tok->kind <= TK_WHILE; }
// Record that identifier `id` is emitted under the object-file symbol
// `name` (GNU `__asm__("name")` declaration label).
void register_asm_name(uint32_t id, char *name);
void dump_ast(Module *prog);

// Debug support: record the '[' that declared one array dimension, so a
// dumper can show the length exactly as written. A no-op unless the AST
// dumper is in use.
void array_bracket_note(Type *ty, Token *l_bracket);
void dump_raw_tokens(Token *tok);
void dump_tokens(Token *tok);

//
// opt_ast.c
//
void fold_ast(Module *prog);
Node *fold_node(Node *node);

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

void fatal(char *fmt, ...) __attribute__((format(printf, 1, 2)));
void error_at(SrcFile *file, uint32_t loc, const char *msg, ...) __attribute__((format(printf, 3, 4)));
void error(Token *tok, const char *msg, ...) __attribute__((format(printf, 2, 3)));
void warning(Token *tok, const char *msg, ...) __attribute__((format(printf, 2, 3)));
void diag(char *level, Token *tok, const char *msg, ...) __attribute__((format(printf, 3, 4)));
void diag_exit(char *level, Token *tok, const char *msg, ...) __attribute__((format(printf, 3, 4)));

void *emalloc(size_t n);
void *vnew(size_t len, size_t esz);
void *vgrow(void *data, size_t len);

char *format(char *s, ...) __attribute__((format(printf, 1, 2)));
uint32_t intern(char *s, uint32_t len);
char *str(uint32_t id);
uint32_t str_len(uint32_t id);
char *escape_char_to_string(char c);

Ref newcon(Con *c0, Module *md);
Ref getcon(int64_t val, Module *md);

//
// literal.c
//
Type *infer_numtype(Token *tok);
Type *infer_chartype(Token *tok);
Type *infer_strtype(Token *tok);

#endif  // CXX_H_
