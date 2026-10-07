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

// How a target's __builtin_va_arg walks its va_list. The control flow --
// test, two candidate addresses, join -- is the same everywhere and lives
// in irgen; what differs per ABI is which field holds the cursor, how much
// room is left, and how far the cursor moves, so that is what a target
// describes here.
typedef enum {
    VA_MEM_REGS,    // separate register save and overflow areas, chosen by a
                    // running offset (amd64, arm64)
    VA_MEM_LINEAR,  // the va_list is a single pointer that walks the
                    // argument area (rv64, rv32)
    // One aggregate whose pieces travel in different register files: each
    // piece is read from its own save area and each cursor advances on its
    // own. struct { int; double } arrives with the int in a general-purpose
    // register and the double in an SSE one, and the two areas are separate
    // blocks of memory.
    VA_MEM_MIXED,
    // The argument is always in the overflow area -- a record too large for
    // the registers -- and its cursor advances by the argument's own
    // rounded-up size, which a fixed table cannot express.
    VA_MEM_OVERFLOW,
    // A record whose pieces each sit in their own SIMD register. The save
    // area spaces registers by a stride that is not the piece width, so the
    // pieces must be gathered before the record can be read back. AAPCS64
    // homogeneous floating-point aggregates and SysV AMD64's two-SSE-piece
    // records both land here.
    VA_MEM_SIMD,
} VaArgKind;

typedef struct {
    VaArgKind kind;

    // VA_MEM_REGS: index and type of the running offset field within the
    // va_list structure, the largest offset that still has room for one
    // more argument of this class, the fields naming the area to read and
    // the area to exhaust, and how far each cursor advances.
    // The type is the target's own static Type object (its address is a
    // compile-time constant, unlike the run-time Target field that points
    // at it), so the tables can stay static.
    int offset_field;
    Type *offset_ty;
    // Whether the offset counts down from zero and goes negative once the
    // named registers are used up (AAPCS64), rather than counting up to a
    // size bound (SysV AMD64). The two need different tests.
    bool offset_negative;
    int offset_bound;
    int reg_field;
    int mem_field;
    // VA_MEM_MIXED: the floating-point running offset and its save-area
    // field, used alongside the integer pair above.
    int fp_offset_field;
    int fp_reg_field;
    int fp_offset_bound;
    int reg_step;  // register save area: all classes
    int mem_step;  // overflow area

    // The alignment the overflow cursor is first raised to, for a type
    // that needs more than the ABI's stack alignment. Zero when the
    // type's alignment never exceeds it.
    int mem_align;

    // VA_MEM_SIMD: the distance between consecutive register slots in the
    // save area, and the offset value past which the area has no room for
    // this record -- the register cursor plus one record's worth of slots
    // must not exceed it. The descending form (AAPCS64) ends at zero.
    int reg_stride;
    int offset_limit;

    // A MEMORY-class aggregate does not travel as its own bytes: the slot
    // holds a pointer to the caller's copy, and only one slot is consumed
    // whatever the aggregate's size. Both the single-cursor form (RISC-V) and
    // the register/overflow pair (AAPCS64) spell it this way, so this is a
    // property of the slot rather than of the cursor.
    bool agg_by_ptr;
} VaArgOps;

// How the ABI passes an aggregate. `npiece` is 0 for the memory class,
// which travels through a pointer: byval as an argument, sret as the result.
// Otherwise each piece is one register wide and `piece` gives the type to
// load it as -- an integer type for a piece that is INTEGER class, a floating
// type for one that is SSE class. `off` is the piece's offset in the
// aggregate, so it can be read straight out of the object.
// The largest number of register pieces any supported ABI splits an
// aggregate into. SysV AMD64 and RISC-V use at most two (eight-byte slots or
// two XLEN); AAPCS64 uses up to four, one per element of a homogeneous
// floating-point aggregate. The bound must cover the largest of them or the
// classifier writes past the end of the array.
#define MAX_AGG_PIECES 4

typedef struct {
    int npiece;  // 0 => memory class (pointer), else number of pieces
    int size;    // size of the aggregate in bytes
    // How the pieces are spelled in the IR when there is more than one:
    // a record of the pieces (SysV, RISC-V) or an array of one repeated
    // element type (AAPCS64, where a homogeneous floating-point aggregate
    // travels as [N x float] and the backend takes the elements from the
    // SIMD registers).
    bool shape_array;
    // AAPCS64: every leaf is the same floating-point type. The pieces then
    // hold that element type rather than a per-eightbyte width, so the
    // caller and the callee agree on which registers the value comes in.
    bool is_hfa;
    struct {
        int off;
        int size;
        Type *ty;
    } piece[MAX_AGG_PIECES];
} AggClass;

// An asm operand's constraint is a language of its own, and LLVM's spelling
// of it is a second one: the same letter can name a fixed register (amd64 'a'
// is "{ax}") or a memory operand, which LLVM has to be told is indirect ('m'
// is "*m"). One row per letter that needs converting; every other letter
// passes through, which is what clang does with the rest of them too.
//   reg -- a fixed-register letter: the operand travels in that register
//   in/out -- a whole-constraint replacement, for an input and for an output
//             (they differ: 'X' is any value as an input and any memory
//             location as an output)
typedef struct {
    char letter;
    char *reg;
    char *in;
    char *out;
} AsmConsConv;

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
    // The alignment of _BitInt(N) for N above 64 bits. Such a type is sixteen
    // bytes everywhere, but AAPCS64 aligns it to sixteen and the other targets
    // to eight, which decides whether a struct holding one is 24 bytes or 32.
    int bitint_align;
    // The width of one floating-point register, in bits: the widest floating
    // type a single register can carry, and so the widest one an ABI can hand
    // over as a leaf. AAPCS64 has 128-bit SIMD registers, SysV AMD64's SSE
    // registers are 128 bits but a 128-bit float takes a register pair, and
    // RISC-V's are 64.
    int fp_reg_bits;
    // How this ABI extends a narrow integer argument or result. The IR marks
    // such a value signext or zeroext so that the caller and the callee agree
    // on what the upper bits hold; leaving the mark off where the ABI defines
    // them makes each side extend defensively instead.
    //   ext_bits  -- plain integers narrower than this many bits are marked
    //   ext_bitint -- every _BitInt is marked whatever its width
    // Zero and false mean the ABI leaves the upper bits undefined (AAPCS64).
    int ext_bits;
    bool ext_bitint;
    char *llvm_features;  // target-features attribute for LLVM codegen
    char *llvm_abi;       // "target-abi" module flag (e.g. "lp64d")
    char *clang_mabi;     // -mabi driver flag matching llvm_abi (bare metal)
    char *clang_march;    // -march driver flag matching llvm_features
    char *predef;

    // This target's va_arg policy (va_arg_ops) for a requested type; defined in its
    // target.c. It is per-type because an ABI with separate general-purpose
    // and SIMD argument registers decides between them on the type.
    VaArgOps *(*va_arg_ops)(Type *want);
    // How this target passes an aggregate (see AggClass). A variadic call has
    // no prototype to classify against, and a call or definition has to agree
    // with the other compiler's idea of the ABI, so the split has to be
    // written into the IR rather than left to the backend.
    void (*classify_aggregate)(Type *agg, AggClass *out);
    // How a variadic call lowers an aggregate, when that differs from the
    // prototyped case. RISC-V is why it exists: the callee has no prototype
    // to read the argument against, so the base ABI applies and nothing
    // travels in the floating-point registers -- struct { float; float }
    // reaches va_arg as one i64. NULL means the prototype rule stands.
    void (*classify_variadic)(Type *agg, AggClass *out);
    // Builds the records that carry flattened pieces, one shape per
    // combination of integer/floating pieces. Finite and fixed, so all of
    // them are built up front and looked up afterwards. They must go through
    // insert_ty(), the same list every other record goes through, or the IR
    // would name a type it never defines.
    void (*classify_publish)(void);
    Type *(*pieces_type)(Type *agg);
    // The single value an aggregate is *returned* as, when the ABI hands it
    // back in a register even though an argument of the same type goes in
    // memory. SysV AMD64 is the reason: an x87 long double comes back in
    // st(0) as x86_fp80. NULL when the classifier's answer covers returns
    // too, which is every other case.
    Type *(*agg_ret_value)(Type *agg);
    // A scalar this ABI passes by reference rather than in registers, which
    // the classifier then treats like an aggregate it has put in memory.
    // RISC-V is the reason it exists: a _BitInt wider than two XLEN words has
    // no register pair to go in. NULL when the target has no such type.
    bool (*scalar_by_ref)(Type *ty);
    // The va_arg policy for an aggregate whose pieces travel in different
    // register files, or NULL when the target has none: such a type needs a
    // second cursor and a second save area, which the single-file tables do
    // not carry.
    VaArgOps *(*va_arg_ops_for_mixed)(void);
    // How many IR parameters one C parameter of this type becomes once the
    // ABI has lowered it: an aggregate may arrive flattened, one parameter
    // per register piece. The numbering of every slot depends on this count.
    int (*abi_param_slots)(Type *ty);
    // Whether a memory-class aggregate parameter is spelled byval (SysV AMD64,
    // where the copy is the callee's) or a plain pointer (AAPCS64 and RISC-V,
    // where the caller has already made the copy).
    bool agg_byval_param;
    // Whether a one-piece aggregate still travels as an array of one element
    // (AAPCS64: [1 x float]) rather than as the bare piece (SysV).
    bool agg_always_array;
    // Whether a non-homogeneous composite is widened to whole registers:
    // AAPCS64 passes every such composite in full eight-byte registers, so a
    // one-byte struct travels as i64. SysV and RISC-V keep the exact width
    // that fits, which is what their va_arg reads back.
    bool agg_full_regs;
    // A soft-float ABI has no floating-point registers at all: every member
    // is packed into the integer registers, floating ones included.
    bool agg_no_fp;

    // The type of this target's va_list as stdarg.h would declare it --
    // an array of one on amd64, a structure on arm64, a pointer on
    // RISC-V. The parser injects it as __builtin_va_list, so the layout
    // stays out of the headers and the variadic builtins check their
    // operand by ordinary type compatibility.
    Type *(*va_list_type)(void);

    // This target's asm constraint letters that need converting (see
    // AsmConsConv), and the clobber list every asm statement implicitly has.
    // The second is how clang models x86: an asm may change the flags and the
    // template does not say so, so they are always clobbered. NULL where the
    // target's flags are not a register the template can touch.
    AsmConsConv *asm_cons;
    int num_asm_cons;
    char *asm_clobbers;
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
// The _BitInt table is shared and statically built, so the one field that
// depends on the target is corrected once the target has been chosen.
void bitint_align_wide(int align);
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
    // A block-scope `static`: the object belongs to one function. The
    // unused-object walk reports it, but it stays in the output the way
    // clang leaves it there -- only a file-scope one is left out.
    bool is_block_static;
    // Set only on the symbols the parser injects for A-class builtins. A
    // user declaration of the same name leaves it false, so the folder and
    // irgen can tell "the compiler's builtin" from "a user function that
    // happens to carry a builtin's name".
    bool is_builtin;
    bool is_defined;
    // Set by irgen: the aggregate result is written through a hidden leading
    // pointer (the ABI memory class), so the IR signature has to name it.
    bool abi_sret;
    bool is_str;

    // GNU asm-name: `int f(void) __asm__("real_symbol");` declares the
    // C identifier f but emits/refers to real_symbol in the object file.
    // NULL when the declaration has no asm label.
    char *asm_name;

    // Token that declared this symbol, for diagnostics that fire long after
    // the declaration (the unused-variable warning is reported at the end of
    // the function). NULL when the symbol is the compiler's own.
    Token *tok;
    // Set when an identifier resolves to this symbol. -Wunused-variable is
    // the absence of it.
    bool is_referenced;

    // The reference graph: every file-scope symbol this function's body or
    // this object's initializer names, in the order they were resolved and
    // without duplicates. It is filled where the name is resolved -- the
    // only point that knows who is referring -- and kept afterwards, so an
    // analysis that runs later (check_unused_statics() below, then whatever
    // an optimization pass wants) does not have to walk the trees again.
    Sym **refs;
    uint32_t num_refs;
    // Set by that analysis: a symbol is reachable when a definition another
    // translation unit can see reaches it through the graph above.
    bool is_reachable;
    // An internal-linkage definition nothing can reach: it is diagnosed and
    // left out of the output.
    bool is_dead;

    // __attribute__((cleanup(f))): the attribute as written, kept for the
    // declaration that owns the object. The handler is looked up only where a
    // handler can run -- an automatic object at block scope -- so a misplaced
    // attribute costs nothing but its warning.
    Attr *cleanup_attr;

    // Attribute flags
    bool is_deprecated;
    bool is_nodiscard;
    bool is_maybe_unused;
    bool is_unused;

    // Global variable
    uint32_t init_data;

    Initializer *init;

    // 6.7.5p8: whether every file scope declaration of this function
    // carries the inline specifier and none carries extern. That is what
    // makes the definition here an *inline definition*, which does not
    // define the symbol. A later declaration can take it away again, so the
    // answer is read at the end of the translation unit, in parse().
    bool all_decls_inline;
    // Set by that read: this definition is an inline definition, so the
    // emitters leave it out. Calls to it stay, and become undefined
    // references exactly as they do under gcc and clang.
    bool is_inline_def;
    // 6.7.5p3 forbids an inline definition to define a modifiable object
    // with static storage duration; this is the token of one if the body
    // does, remembered until the definition's kind is known.
    Token *static_local_tok;

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

// One operand of a GNU extended asm statement. asm is not ISO C in any form
// -- the standard has none -- so the whole construct is the GNU one, and both
// references implement it the same way apart from wording.
//
// The fields after `expr` are the lowering, worked out once at parse time
// because the template has to be rewritten in terms of them: a reference in
// the template (`%0`, `%[name]`, `%l1`) is an index into one numbering, and
// that numbering has to be settled before the template can be emitted.
typedef struct AsmOperand AsmOperand;
struct AsmOperand {
    AsmOperand *next;
    Token *tok;  // the string or ‘(’ that introduced it
    char *name;  // [name], or NULL
    char *cons;  // the constraint as written
    char *conv;  // the same constraint in LLVM's spelling ("=*m", "={ax}")
    Node *expr;  // output: the lvalue; input: the value
    bool is_output;
    bool is_plus;      // ‘+’: read as well as written
    bool is_indirect;  // the constraint denotes memory: the operand is an address
    // A ‘+’ operand is one operand to GCC but two constraints to LLVM: the
    // output above, and an input that shares its register ("0") or its
    // address ("*m"). The second is spelled here.
    char *conv_in;
    uint32_t index;  // position in GCC's numbering: outputs first, then inputs
    // Where the IR call numbers this operand in its constraint string. The
    // two agree for everything the program may name; they differ only for the
    // input half of a ‘+’ operand, which GCC does not number separately but
    // the constraint string has to spell out, and which therefore follows
    // every numbered input.
    uint32_t pos;
    int arg_pos;  // position among the call's arguments, or -1 for a return value
    // The same two for that input half, which is not an operand of its own.
    uint32_t plus_pos;
    int plus_arg_pos;
};

// The qualifiers an asm statement can carry. GCC also spells them with
// underscores, which the lexer folds into the same keyword.
enum {
    ASM_VOLATILE = 1 << 0,  // asm volatile: the statement has effects
    ASM_INLINE = 1 << 1,    // asm inline: a hint, spelled in the IR nowhere
    ASM_GOTO = 1 << 2,      // asm goto: the template may jump to a label
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
    // Variadic argument access. va_arg is an expression; the other two are
    // statements in effect but appear in expression position.
    ND_VA_START,   // lhs: address of the va_list object
    ND_VA_END,     // lhs: address of the va_list object
    ND_VA_ARG,     // lhs: the va_list value, rhs: NULL, ty: requested type
    ND_VA_COPY,    // lhs: destination va_list address, rhs: source address
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
    ND_ASM,        // GNU "asm" statement (basic asm, extended asm, asm goto)

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
        struct {
            // ND_ASM: the template, already rewritten in LLVM's spelling --
            // `%0` became `$0`, `%%` became `%`, a literal `$` became `$$` --
            // because the index an operand reference carries is settled here
            // and nothing downstream could work it out again.
            char *asm_tmpl;
            AsmOperand *asm_ops;  // outputs first, then inputs, in source order
            char *asm_cons;       // the LLVM constraint string
            // ASM_*: the qualifiers, and whether the goto-label list is there.
            uint32_t asm_flags;
            int asm_nops;
            int asm_nret;  // register outputs: the call's return values
            int asm_narg;  // operands the call passes (inputs + indirect outputs)
            // asm goto: the labels, in the order the template names them.
            Node **asm_labels;
            int asm_nlabels;
        };
        Fp128 fpval;  // ND_NUM floating constants
        Int128 ival;  // ND_NUM integer constants
    };
    Node *label_ring;
    Node *label_body;
    // The cleanup handlers this jump has to run first, as one expression in
    // source order. A jump is the only statement that leaves scopes without
    // reaching the end of their blocks, so it carries the calls itself.
    Node *unwind;
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

    // Variadic argument access. These need special parsing (va_arg's second
    // operand is a type name, not an expression) and per-target expansion,
    // so they are not declarable.
    BUILTIN_VA_START,
    BUILTIN_VA_END,
    BUILTIN_VA_ARG,
    BUILTIN_VA_COPY,

    // Bit scanning. Like clz/ctz these return int whatever the operand
    // width. They differ from that family in having no LLVM intrinsic: each
    // is a short instruction sequence, which irgen builds directly. The
    // operand types carry the signedness the expansions need -- ffs and
    // clrsb take a signed argument, parity an unsigned one.
    BUILTIN_FFS,
    BUILTIN_FFSL,
    BUILTIN_FFSLL,
    BUILTIN_PARITY,
    BUILTIN_PARITYL,
    BUILTIN_PARITYLL,
    BUILTIN_CLRSB,
    BUILTIN_CLRSBL,
    BUILTIN_CLRSBLL,

    // Arithmetic with overflow reporting: __builtin_add_overflow and its
    // siblings, which <stdckdint.h> is written in terms of. Their IR is a
    // two-value return -- { iN, i1 } -- so they are the one family here
    // that needs an aggregate in the IR.
    BUILTIN_ADD_OVERFLOW,
    BUILTIN_SUB_OVERFLOW,
    BUILTIN_MUL_OVERFLOW,

    // The floating constant producers behind HUGE_VAL, INFINITY and NAN
    // (7.12.11.2, F.10.11). They have no runtime behaviour: the parser
    // folds each call to the constant it names, so neither the folder nor
    // irgen ever sees one. The nans family is the signaling form, which the
    // converters in fp128.c now carry through instead of canonicalising
    // (doc/cxx-c2y-plan.md P1b).
    BUILTIN_HUGE_VAL,
    BUILTIN_HUGE_VALF,
    BUILTIN_HUGE_VALL,
    BUILTIN_INF,
    BUILTIN_INFF,
    BUILTIN_INFL,
    BUILTIN_NANF,
    BUILTIN_NAN,
    BUILTIN_NANL,
    BUILTIN_NANSF,
    BUILTIN_NANS,
    BUILTIN_NANSL,

    // 7.12.18: the comparison macros. Four are one C operator each and are
    // rewritten by the parser; islessgreater and isunordered need the `one`
    // and `uno` predicates, so they reach irgen as a call.
    BUILTIN_ISGREATER,
    BUILTIN_ISGREATEREQUAL,
    BUILTIN_ISLESS,
    BUILTIN_ISLESSEQUAL,
    BUILTIN_ISLESSGREATER,
    BUILTIN_ISUNORDERED,

    // 7.12.4 / 7.12.3: the classification and sign macros. Each shares its
    // operand across several comparisons, so all of them reach irgen.
    BUILTIN_ISNAN,
    BUILTIN_ISINF,
    BUILTIN_ISINF_SIGN,
    BUILTIN_ISFINITE,
    BUILTIN_ISNORMAL,
    BUILTIN_SIGNBIT,
    BUILTIN_FPCLASSIFY,
    // __builtin_offsetof(type, member-designator): an integer constant
    // expression, so it can size an array whatever the header spells it as.
    BUILTIN_OFFSETOF,
    // A hint about which value the first argument usually has; the result is
    // that argument. Real code spells it `__builtin_expect(x, 0)`, and lua,
    // git and cpython all use it in their hot paths.
    BUILTIN_EXPECT,

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
    // How many operands the intrinsic itself takes: 1 for ctpop and bswap,
    // 2 for ctlz/cttz, whose second operand is extra_arg above. Kept
    // separate from extra_arg because 0 is a meaningful immediate (clrsb's
    // ctlz wants is_zero_undef = false), so extra_arg alone cannot say
    // whether a second operand is present.
    int intrinsic_args;
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
    // Qualified copies of this type, threaded through `next_copy`. A copy is
    // the same type with a qualifier on it, so a tag completed after the copy
    // was made (6.7.2.3p4) has to pass its shape on.
    Type *copies;
    Type *next_copy;
    Attr *attrs;  // attributes attached to the type
    // Set on the synthesised function type of a builtin that has no
    // prototype -- one whose arguments must keep their own types. `id`
    // then carries the builtin's kind, which is how a call site is
    // recognised as that builtin without a scope lookup.
    bool is_builtin;

    // Data
    union {
        struct {
            // Array or ptr. `len` is only the length of a *fixed* array: for a
            // variable length one these bytes are the low half of vla_len, the
            // pointer to the expression that computes the length, so reading
            // `len` there compares an address -- which is what made
            // is_compatible() answer differently from one run to the next.
            // A variable length array matches any length instead.
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
    // Full width: an enumerator may be any value its underlying type holds,
    // and the width is chosen from the enumerators when none is fixed.
    Int128 val;
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
// A floating type that one floating-point register can carry, and so one an
// ABI can hand over as a leaf in a register of its own. The IEC 60559
// interchange types are the same widths under other names, so they qualify
// exactly where float and double do; a 128-bit float needs a 128-bit
// register, and an x87 long double is not in a register file at all.
bool is_fp_leaf(Type *ty);
// The extension mark an integer argument or result carries in the IR, or NULL
// when this ABI leaves the upper bits undefined.
char *ext_attr(Type *ty);
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
Type *agg_shape_type(AggClass *c);
int agg_param_slots(Type *ty, AggClass *c);
Type *agg_param_shape_type(Type *ty, AggClass *c);
// Whether the lowered pieces travel as separate parameters -- and as separate
// operands at a call -- rather than as one value of the shape
// agg_param_shape_type returns. A record of pieces is one value per piece; an
// array shape, or a shape that came down to a single piece, is one whole
// value. It follows from the shape rather than from a per-target flag,
// because one target can need both: RISC-V sends struct { float; float } as
// two floats and struct { long; long } as one [2 x i64].
bool agg_is_per_piece(AggClass *c);
// The RISC-V ABI, shared by the two RISC-V targets. They differ in XLEN and
// in whether they have floating-point registers at all, and the classifier
// reads both off the active Target, so one copy serves rv64, rv32 and the
// bare-metal rv32.
void rv_classify_aggregate(Type *agg, AggClass *out);
void rv_classify_variadic(Type *agg, AggClass *out);
Type *rv_pieces_type(Type *agg);
int rv_param_slots(Type *ty);
Type *vla_of(Type *base, Node *expr);
Type *struct_type(bool is_union);
Type *enum_type(void);
// Give an enum the type that holds every one of its enumerators.
void enum_set_underlying(Type *ty, EnumVal *vals);
Type *copy_type(Type *ty);
void complete_copies(Type *ty);
// Give a type its IR name and add it to the module's type list.
void insert_ty(Type *ty, char *kind);
Type *type_qual(Type *ty, uint32_t qual);
Type *type_unqual(Type *ty);
void add_type(Node *node);
// 6.3.2.1's usual arithmetic conversions on a pair, as add_type
// applies to the operands of a binary operator. Exported for the
// comparison macros, which need the converted pair on its own.
void usual_arith_conv(Node **lhs, Node **rhs);

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
    // The two floating predicates no C operator spells: `one` is
    // "ordered and not equal" and `uno` is "unordered". They exist for
    // __builtin_islessgreater and __builtin_isunordered, whose operands
    // cannot be written out twice; both are always floating, so the
    // integer columns of their op_str row are never reached.
    IR_CMP_ONE,
    IR_CMP_UNO,

    // Other
    IR_CALL,
    IR_VA_ARG,
    IR_VA_START,
    IR_VA_COPY,
    IR_VA_END,
    IR_SELECT,
    // An asm statement: one inline-asm call, whose operands are the ones the
    // constraint string names and whose result is the register outputs. An
    // asm goto is this instruction plus a terminator (IR_CALLBR) that names
    // the labels; the two are printed as the one callbr they spell.
    IR_ASM,
    // Terminator: the fall-through and the label list of an asm goto.
    IR_CALLBR,
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
    // IR_CALL: the call passes a hidden result pointer as its first operand,
    // which the printer has to mark sret rather than byval. Recorded on the
    // instruction because printing happens long after generation.
    uint8_t is_sret;
    // IR_CALL: the operand index (1-based) carrying a byval aggregate copy,
    // or 0 when there is none. A pointer to a record is not by itself a
    // byval argument -- the user may simply have passed such a pointer -- so
    // which operand is a copy is recorded where it is decided.
    uint8_t byval_at;
    // IR_ASM: the template and the constraint string, the two halves of the
    // statement that the operands alone do not spell. `asm_ind` marks, one
    // byte per argument, the operands a "*" constraint made indirect: LLVM
    // requires those to carry an elementtype, and which ones they are cannot
    // be read off the operand itself.
    char *asm_tmpl;
    char *asm_cons;
    uint8_t *asm_ind;
    // ASM_VOLATILE says the statement may not be dropped or moved, which
    // LLVM spells `sideeffect`; ASM_GOTO makes it a callbr, with the labels
    // as the terminator that follows it.
    uint8_t asm_nret;  // register outputs: the leading constraint positions
    uint8_t asm_flags;
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
    // File-scope asm statements, in source order: LLVM's `module asm`, which
    // is emitted before the globals the way clang emits it.
    char **masm;
    int num_masm;
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
// Warning groups. Bit flags so that -w, -Wall and -Wno-<group> compose,
// and every group starts out enabled -- which is what cxx did before the
// flags existed, where the only control was -w for all of them.
enum {
    // A diagnostic with no group of its own: on unless -w. Used where no
    // gcc or clang group name fits the message.
    WG_DEFAULT = 0,
    WG_DEPRECATED = 1u << 0,
    WG_UNUSED_RESULT = 1u << 1,  // [[nodiscard]], as in gcc and clang
    WG_ATTRIBUTES = 1u << 2,
    WG_RETURN_TYPE = 1u << 3,
    WG_CPP = 1u << 4,  // the preprocessor's own diagnostics, #warning included
    WG_MEMORY_ORDER = 1u << 5,
    // Accepted for compatibility. Neither has a diagnostic yet: an implicit
    // function declaration is a constraint violation in C23 and so an
    // error, and the unused-variable warning is plan item E3.
    WG_UNUSED_VARIABLE = 1u << 6,
    WG_IMPLICIT_FUNCTION_DECLARATION = 1u << 7,
    // clang's name for a function declared noreturn that returns; gcc does
    // not diagnose it at all.
    WG_INVALID_NORETURN = 1u << 8,
    // Redefining or undefining a builtin macro. clang's name; gcc is silent.
    WG_BUILTIN_MACRO_REDEFINED = 1u << 9,
    // Tokens after a directive that takes none: clang calls it
    // extra-tokens, gcc calls it endif-labels.
    WG_EXTRA_TOKENS = 1u << 10,
    // Not in -Wall. clang reports a switch fallthrough only when this flag
    // is given -- neither -Wall nor -Wextra turns it on -- and gcc does not
    // diagnose it by default at all. Groups with the WG_OFF_DEFAULT mark
    // start cleared and are enabled only by their own -W<name>.
    WG_IMPLICIT_FALLTHROUGH = 1u << 11,
    // On by default in gcc and clang, not part of -Wall.
    WG_SHIFT_COUNT_NEGATIVE = 1u << 12,
    WG_SHIFT_COUNT_OVERFLOW = 1u << 13,
    // A static function or object that no reachable code names. gcc and
    // clang split the object case in two: a const object is reported under
    // -Wunused-const-variable, anything else under -Wunused-variable.
    WG_UNUSED_FUNCTION = 1u << 14,
    WG_UNUSED_CONST_VARIABLE = 1u << 15,
    // clang's name for a non-constant static local defined in an inline
    // function (6.7.5p3); gcc diagnoses the same thing with no name of its
    // own. clang is silent about the other half of p3, a reference to an
    // identifier with internal linkage, and gcc warns about that one.
    WG_STATIC_LOCAL_IN_INLINE = 1u << 16,
    // clang's name for 6.9.2p2's assumption that an array left without a
    // length holds one element; gcc warns about it with no name of its own.
    WG_TENTATIVE_DEFINITION_ARRAY = 1u << 17,
    // clang's name for an integer constant implicitly converted to an
    // integer type that cannot represent it; gcc's -Woverflow covers the
    // same ground by default, minus the long -> int case.
    WG_CONSTANT_CONVERSION = 1u << 18,
    // clang's name for a floating constant converted to an integer: the
    // fraction is dropped, or the value is out of range. clang has it on;
    // gcc leaves both to -Wconversion.
    WG_LITERAL_CONVERSION = 1u << 19,
    // gcc's name for a constant conversion to a floating type that rounding
    // changes -- double to float, or an integer wider than the target's
    // significand. Neither reference has it on by default: -Wconversion
    // (gcc) and -Wimplicit-float-conversion (clang) turn it on.
    WG_FLOAT_CONVERSION = 1u << 20,
    // clang's name (gcc calls it -Woverflow) for a floating constant whose
    // value does not fit the type it was written as. This one is a parse
    // time diagnostic: the literal is rounded to its own type there, and the
    // result is an infinity -- which no constant spelling can produce,
    // because the infinities of <math.h> are identifiers, not literals.
    WG_LITERAL_RANGE = 1u << 21,
    // gcc's and clang's name for a relational operator whose operands have
    // different signedness, so the signed one is converted to unsigned.
    // Both references reach it through -Wextra in C, not by default.
    WG_SIGN_COMPARE = 1u << 22,
    // A constant integer that the target floating type cannot hold exactly:
    // clang has this one *on* by default (-Wimplicit-const-int-float-conversion),
    // and so does cxx -- a constant the program does not get is worth saying
    // even without a flag. The wider -Wfloat-conversion stays opt-in.
    WG_CONST_INT_FLOAT_CONVERSION = 1u << 23,
    WG_ALL = (1u << 24) - 1,
    // Groups that -Wall does not enable.
    WG_OFF_DEFAULT = WG_IMPLICIT_FALLTHROUGH | WG_FLOAT_CONVERSION | WG_SIGN_COMPARE,
};

// The scalar type names the conversion diagnostics use ("unsigned char",
// "long long", ...). Defined next to warning(), which is its only user.
const char *diag_ty_name(Type *ty);

// Does the value fit the range of an integer type? The same predicate
// fold_cast() uses, shared so that a static initializer and a conversion
// inside a function agree on what counts as out of range.
// True while a static initializer's expression is being folded, where an
// out-of-range conversion is an error rather than a warning (parser.c).
extern bool in_static_init;

bool fits_target(Int128 v, Type *ty);

// The scalar type names the conversion diagnostics use ("unsigned char",
// "long long", ...).
const char *diag_ty_name(Type *ty);

bool wg_enabled(int group);
void warning(int group, Token *tok, const char *msg, ...) __attribute__((format(printf, 3, 4)));

// -pedantic: diagnose the constructs ISO C forbids and cxx accepts as GNU
// extensions. -pedantic-errors makes them errors instead of warnings.
extern bool opt_pedantic;
extern bool opt_pedantic_errors;
void pedantic(Token *tok, const char *msg, ...) __attribute__((format(printf, 2, 3)));
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
