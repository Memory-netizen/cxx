#include "cxx.h"

static FILE *out_file;
// ---------------------------------------------------------------- output ----
// The IR leaves in small pieces -- 323 call sites, 231 of them a bare literal,
// averaging five bytes -- and putting each through fprintf cost 397 instructions,
// three quarters of it the machinery around the format engine rather than the
// formatting itself: 24% of the whole front end to write out 1.4 MB. So the pieces
// go into a buffer, the file sees one write per block, and the formatting this
// program actually uses is done here. Anything unexpected is a fatal error rather
// than a silent difference in the output.
#define OBUF_SIZE (256 * 1024)
static char obuf[OBUF_SIZE];
static size_t obuf_len;
static size_t obuf_total;  // what fprintf would have returned

static void oflush(void) {
    if (obuf_len && out_file) {
        fwrite(obuf, 1, obuf_len, out_file);
        obuf_len = 0;
    }
}

static void oput(const char *s, size_t n) {
    obuf_total += n;
    if (n >= OBUF_SIZE) {
        oflush();
        if (out_file) fwrite(s, 1, n, out_file);
        return;
    }
    if (obuf_len + n > OBUF_SIZE) oflush();
    memcpy(obuf + obuf_len, s, n);
    obuf_len += n;
}

static void och(char c) { oput(&c, 1); }

static void ou64(uint64_t v, unsigned base, int width, bool upper, char pad, bool left) {
    static const char lo[] = "0123456789abcdef";
    static const char up[] = "0123456789ABCDEF";
    const char *digits = upper ? up : lo;
    char tmp[72];
    int n = 0;
    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v);
    if (!left)
        for (int i = n; i < width; i++) och(pad);
    while (n > 0) och(tmp[--n]);
    if (left)
        for (int i = n; i < width; i++) och(pad);
}

static int ofmt(const char *fmt, ...) {
    size_t start = obuf_total;
    va_list ap;
    va_start(ap, fmt);

    const char *lit = fmt;
    const char *p = fmt;
    while (*p) {
        if (*p != '%') {
            p++;
            continue;
        }
        if (p > lit) oput(lit, (size_t)(p - lit));
        p++;

        // printf's order: flags, width, precision, length, conversion. A `*` is a
        // width when it comes before the '.' and a precision after it -- %*.s is a
        // width of spaces with an empty string, which is how this file indents.
        bool left = false, zero = false;
        for (;; p++) {
            if (*p == '-') left = true;
            else if (*p == '0') zero = true;
            else break;
        }
        if (*p == '+' || *p == ' ' || *p == '#')
            fatal("dumpir: unsupported flag '%%%c' in \"%s\"", *p, fmt);
        int width = 0;
        if (*p == '*') {
            width = va_arg(ap, int);
            p++;
        } else {
            while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        }
        if (width < 0) {
            left = true;
            width = -width;
        }
        int prec = -1;
        if (*p == '.') {
            p++;
            prec = 0;
            while (*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0');
        }
        if (*p == '*') {
            prec = va_arg(ap, int);
            p++;
        }
        int mod = 0;  // 0 = int, 1 = long, 2 = long long, 4 = size_t, 5 = ptrdiff_t
        if (*p == 'l') {
            p++;
            mod = 1;
            if (*p == 'l') {
                p++;
                mod = 2;
            }
        } else if (*p == 'z') {
            p++;
            mod = 4;
        } else if (*p == 't') {
            p++;
            mod = 5;
        } else if (*p == 'h') {
            p++;
            if (*p == 'h') p++;
        }

        char c = *p ? *p++ : 0;
        switch (c) {
        case '%':
            och('%');
            break;
        case 'c':
            och((char)va_arg(ap, int));
            break;
        case 's': {
            const char *sv = va_arg(ap, const char *);
            if (!sv) sv = "(null)";
            size_t n = strlen(sv);
            if (prec >= 0 && (size_t)prec < n) n = (size_t)prec;
            if (!left)
                for (int i = (int)n; i < width; i++) och(' ');
            oput(sv, n);
            if (left)
                for (int i = (int)n; i < width; i++) och(' ');
            break;
        }
        case 'd':
        case 'i': {
            int64_t v;
            if (mod == 1) v = va_arg(ap, long);
            else if (mod == 2) v = va_arg(ap, long long);
            else if (mod == 4) v = (int64_t)va_arg(ap, size_t);
            else if (mod == 5) v = (int64_t)va_arg(ap, ptrdiff_t);
            else v = va_arg(ap, int);
            uint64_t u;
            if (v < 0) {
                och('-');
                u = (uint64_t)(-(v + 1)) + 1;  // INT64_MIN safe
            } else {
                u = (uint64_t)v;
            }
            ou64(u, 10, width, false, zero ? '0' : ' ', left);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o':
        case 'b': {
            uint64_t u;
            if (mod == 1) u = va_arg(ap, unsigned long);
            else if (mod == 2) u = va_arg(ap, unsigned long long);
            else if (mod == 4) u = va_arg(ap, size_t);
            else if (mod == 5) u = (uint64_t)va_arg(ap, ptrdiff_t);
            else u = va_arg(ap, unsigned int);
            ou64(u, c == 'u' ? 10 : c == 'o' ? 8 : c == 'b' ? 2 : 16, width, c == 'X', zero ? '0' : ' ', left);
            break;
        }
        case 'e':
        case 'f':
        case 'g': {
            // One site uses %.17e; the rest of the float work can stay in stdio.
            double d = va_arg(ap, double);
            char tmp[64];
            int n = snprintf(tmp, sizeof(tmp), "%.*e", prec < 0 ? 6 : prec, d);
            if (n > 0) oput(tmp, (size_t)n < sizeof(tmp) ? (size_t)n : sizeof(tmp) - 1);
            break;
        }
        default:
            fatal("dumpir: unsupported format '%%%c' in \"%s\"", c, fmt);
        }
        lit = p;
    }
    if (p > lit) oput(lit, (size_t)(p - lit));
    va_end(ap);
    return (int)(obuf_total - start);
}

// Every fprintf in this file wrote to out_file, so the stream argument is dropped
// here rather than at 323 call sites: the pieces go to the buffer above.
#define fprintf(stream, ...) ofmt(__VA_ARGS__)

static Module *curm;
extern bool opt_fpic;
extern bool opt_fcommon;

// Is this operand spelled `byval(T)`?
static bool is_byval(Ir *ir, uint32_t i) {
    for (int k = 0; k < ir->nbyval; k++)
        if (ir->byval_at[k] == i) return true;
    return false;
}

// True when this symbol's object-file name was already emitted (see the
// definition below dump_str); used to fold asm-name aliases together.
static bool already_emitted(Sym *sym);

static const char *op_str[][3] = {
    [IR_ADD] = {"add", "add", "fadd"},
    [IR_SUB] = {"sub", "sub", "fsub"},
    [IR_MUL] = {"mul", "mul", "fmul"},
    [IR_DIV] = {"sdiv", "udiv", "fdiv"},
    [IR_REM] = {"srem", "urem", NULL},
    [IR_AND] = {"and", "and", NULL},
    [IR_OR] = {"or", "or", NULL},
    [IR_XOR] = {"xor", "xor", NULL},
    [IR_SHL] = {"shl", "shl", NULL},
    [IR_SHR] = {"ashr", "lshr", NULL},
    [IR_CMP_EQ] = {"icmp eq", "icmp eq", "fcmp oeq"},
    [IR_CMP_NE] = {"icmp ne", "icmp ne", "fcmp une"},
    [IR_CMP_LE] = {"icmp sle", "icmp ule", "fcmp ole"},
    [IR_CMP_LT] = {"icmp slt", "icmp ult", "fcmp olt"},
    [IR_CMP_ONE] = {"icmp ne", "icmp ne", "fcmp one"},
    [IR_CMP_UNO] = {"icmp eq", "icmp eq", "fcmp uno"},
    [IR_SELECT] = {"select", "select", NULL},
    [IR_EXT] = {"sext", "zext", "fpext"},
    [IR_TRUNC] = {"trunc", "trunc", "fptrunc"},
    [IR_FPTOINT] = {"fptosi", "fptoui", NULL},
    [IR_INTTOFP] = {"sitofp", "uitofp", NULL},
    [IR_PTRTOINT] = {"ptrtoint", "ptrtoint", NULL},
    [IR_INTTOPTR] = {"inttoptr", "inttoptr", NULL},
};

// LLVM has no 'consume' order (dropped in favor of acquire).
static const char *mem_order_str[] = {
    [MEM_ORDER_RELAXED] = "monotonic", [MEM_ORDER_CONSUME] = "acquire", [MEM_ORDER_ACQUIRE] = "acquire",
    [MEM_ORDER_RELEASE] = "release",   [MEM_ORDER_ACQ_REL] = "acq_rel", [MEM_ORDER_SEQ_CST] = "seq_cst",
};

static const char *atomicrmw_op[] = {
    [A_XCHG] = "xchg",
    [A_ADD] = "add",
    [A_SUB] = "sub",
    [A_AND] = "and",
    [A_NAND] = "nand",
    [A_OR] = "or",
    [A_XOR] = "xor",
    [A_MAX] = "max",
    [A_MIN] = "min",
    [A_UMAX] = "umax",
    [A_UMIN] = "umin",
    [A_FADD] = "fadd",
    [A_FSUB] = "fsub",
    [A_FMAX] = "fmax",
    [A_FMIN] = "fmin",
    [A_UINC_WRAP] = "uinc_wrap",
    [A_UDEC_WRAP] = "udec_wrap",
    [A_USUB_COND] = "usub_cond",
    [A_USUB_SAT] = "usub_sat",
};

static const char *ty_str[] = {
    [TY_VOID] = "void",     [TY_BOOL] = "i8",       [TY_CHAR] = "i8",     [TY_SCHAR] = "i8",  [TY_UCHAR] = "i8",
    [TY_SHORT] = "i16",     [TY_INT] = "i32",       [TY_LONG] = "i64",    [TY_LLONG] = "i64", [TY_FLOAT] = "float",
    [TY_DOUBLE] = "double", [TY_LDOUBLE] = "fp128", [TY_F16] = "half",    [TY_F32] = "float", [TY_F64] = "double",
    [TY_F128] = "fp128",    [TY_PTR] = "ptr",       [TY_NULLPTR] = "ptr",
};

static const char *asm_name_of(uint32_t id);
static void print_sym_name(uint32_t id);

// The name a reference uses is the name the symbol is *emitted* under, asm
// label included: glibc redirects fcntl to fcntl64 with __asm__, and printing
// the C identifier here left the reference pointing at a symbol that was never
// declared -- sqlite's aSyscall initializer mentions fcntl, and LLVM refused
// the module with "use of undefined value '@fcntl'".
static void print_ident(uint32_t id) {
    char *ident = str(id);
    int len = str_len(id);

    const char *asm_name = asm_name_of(id);
    if (asm_name) {
        ident = (char *)asm_name;
        len = (int)strlen(asm_name);
    }

    bool needs_quote = false;
    for (int i = 0; i < len; i++) {
        unsigned char c = ident[i];
        if (c > 0x7F || c == '$') {
            needs_quote = true;
            break;
        }
    }

    if (!needs_quote) {
        fprintf(out_file, "%s", ident);
        return;
    }

    fprintf(out_file, "\"");
    for (int i = 0; i < len; i++) {
        unsigned char c = ident[i];
        if (c <= 0x7F) {
            och((char)c);
        } else
            fprintf(out_file, "\\%02X", c);
    }
    fprintf(out_file, "\"");
}

// GNU asm labels (`int f(void) __asm__("real");`) rename a symbol in the
// object file. The IR still refers to it under the C identifier, but
// every place the symbol is emitted -- definition, declaration, call
// target, global reference -- must spell the asm name instead. The
// mapping is keyed by the interned identifier because Ref (RGlb) only
// carries the id, not the Sym.
static struct {
    uint32_t id;
    char *name;
} *asm_names;
static int num_asm_names;

void register_asm_name(uint32_t id, char *name) {
    for (int i = 0; i < num_asm_names; i++)
        if (asm_names[i].id == id) {
            asm_names[i].name = name;
            return;
        }
    if (!asm_names)
        asm_names = vnew(4, sizeof(asm_names[0]));
    else
        asm_names = vgrow(asm_names, num_asm_names + 1);
    asm_names[num_asm_names].id = id;
    asm_names[num_asm_names++].name = name;
}

// Print the object-file name of an identifier, honouring a registered
// name (an asm label, or an LLVM intrinsic).
//
// A registered name is quoted so that it is one verbatim symbol
// (@"__isoc23_fscanf"). An LLVM intrinsic is the exception: there the
// overload suffix is part of the identifier, so @"llvm.bswap.i32" would
// name a different -- and invalid -- symbol, and it must be printed bare.
// Intrinsics are the only registered name that is not a C identifier,
// which is what "contains a dot" detects.
// The asm label a symbol is emitted under, or NULL.
static const char *asm_name_of(uint32_t id) {
    for (int i = 0; i < num_asm_names; i++)
        if (asm_names[i].id == id) return asm_names[i].name;
    return NULL;
}

// The functions that run at startup (`ctors`) or at shutdown, in priority
// order, as the initializer array LLVM expects. Nothing is printed when there
// are none, so a program without the attribute emits what it always did.
static void dump_init_array(const char *name, bool ctors) {
    int n = 0;
    for (Sym *fn = curm->fns; fn; fn = fn->next)
        if (ctors ? fn->ctor_prio : fn->dtor_prio) n++;
    if (!n) return;

    // Priority order, by insertion sort: the list is short and this keeps
    // equal priorities in declaration order.
    Sym **sorted = vnew(n, sizeof(Sym *));
    int k = 0;
    for (Sym *fn = curm->fns; fn; fn = fn->next)
        if (ctors ? fn->ctor_prio : fn->dtor_prio) sorted[k++] = fn;
    for (int i = 1; i < n; i++) {
        Sym *x = sorted[i];
        int p = ctors ? x->ctor_prio : x->dtor_prio;
        int j = i - 1;
        while (j >= 0 && (ctors ? sorted[j]->ctor_prio : sorted[j]->dtor_prio) > p) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = x;
    }

    fprintf(out_file, "\n@%s = appending global [%d x { i32, ptr, ptr }] [", name, n);
    for (int i = 0; i < n; i++) {
        fprintf(out_file, "%s{ i32, ptr, ptr } { i32 %d, ptr @", i ? ", " : "",
                ctors ? sorted[i]->ctor_prio : sorted[i]->dtor_prio);
        print_sym_name(sorted[i]->id);
        fprintf(out_file, ", ptr null }");
    }
    fprintf(out_file, "]\n");
}

static void print_sym_name(uint32_t id) {
    for (int i = 0; i < num_asm_names; i++)
        if (asm_names[i].id == id) {
            if (strchr(asm_names[i].name, '.'))
                fprintf(out_file, "%s", asm_names[i].name);
            else
                fprintf(out_file, "\"%s\"", asm_names[i].name);
            return;
        }
    print_ident(id);
}

static bool is_agg(Type *ty);
static int abi_param_count(Type *ty);
static void print_param_type(Type *ty, int i, bool bare);

static void print_type(Type *ty) {
    if (!ty) {
        fprintf(out_file, "void");
        return;
    }
    if (ty->kind == TY_ARRAY) {
        // A declaration of an array of unknown size -- `extern int arr[];`
        // -- has no length to print; clang writes zero there, and zero is
        // the one length LLVM accepts for a global that is never allocated.
        fprintf(out_file, "[%d x ", ty->len < 0 ? 0 : ty->len);
        print_type(ty->base);
        fprintf(out_file, "]");
        return;
    }
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        if (!ty->uid) {
            // An unnamed aggregate is an internal one -- the { iN, i1 } an
            // overflow builtin returns. LLVM spells those inline, and
            // "%0" would not even name a type.
            fprintf(out_file, "{ ");
            for (Member *m = ty->members; m; m = m->next) {
                print_type(m->ty);
                if (m->next) fprintf(out_file, ", ");
            }
            fprintf(out_file, " }");
            return;
        }
        fprintf(out_file, "%%");
        print_ident(ty->uid);
        return;
    }
    if (ty->kind & TY_BITINT) {
        fprintf(out_file, "i%d", ty->kind & 0xFFF);
        return;
    }
    if (ty->kind == TY_LDOUBLE && T.ldouble_is_fp80) {
        fprintf(out_file, "x86_fp80");
        return;
    }
    if (ty->kind == TY_ENUM) {
        // The underlying type an enum chose decides how wide it is in the IR,
        // not the name "enum": an enum needing 64 bits travels as an i64.
        fprintf(out_file, "i%d", ty->size * 8);
        return;
    }
    if (ty->kind == TY_LONG) {
        // 64-bit on LP64 targets, 32-bit on ILP32 (rv32)
        fprintf(out_file, "i%d", T.ty_long->size * 8);
        return;
    }
    fprintf(out_file, "%s", ty_str[ty->kind]);
}

static int get_blkid(uint32_t fn_id, uint32_t lbl_id) {
    for (Sym *fn = curm->fns; fn; fn = fn->next)
        if (fn->id == fn_id) {
            for (Node *y = fn->labels; y; y = y->goto_next)
                if (y->label == lbl_id) return fn->blks[y->blk_idx].blk_id;
        }
    return 0;
}

// Function whose body is being printed; RLabel refs (labels are
// function-scoped) resolve their blk id through it.
static Sym *dump_curf;

static void print_label(Con *c, char *sym, char *dot) {
    *dot = '\0';
    uint32_t fn_id = intern(sym, strlen(sym));
    uint32_t lbl_id = intern(dot + 2, strlen(dot + 2));

    if (c->bits.i) fprintf(out_file, "getelementptr (i8, ptr ");
    fprintf(out_file, "blockaddress(@");
    print_ident(fn_id);
    fprintf(out_file, ", %%blk%d)", get_blkid(fn_id, lbl_id));
    if (c->bits.i) fprintf(out_file, ", i64 %" PRIi64 ")", c->bits.i);
}

static void printcon(Con *c, Type *ty) {
    if (c->type == CBits) {
        if (is_flonum(ty)) {
            // The 64-bit hex form is the legacy single-bit-pattern literal and
            // LLVM only accepts it for a type of that width: printed for an
            // x86_fp80 it is "floating point constant does not have type
            // 'x86_fp80'". A value that reached here as 64 bits came from an
            // integer or from a narrower type, so for a wider one the value is
            // spelled as a decimal literal, which LLVM reads in the type it is
            // used with. Zero and the non-finite values stay exact.
            if (ty->kind == TY_LDOUBLE || ty->kind == TY_F128) {
                double d;
                memcpy(&d, &c->bits.i, sizeof(d));
                if (c->bits.i == 0) {
                    fprintf(out_file, "%s",
                            T.ldouble_is_fp80 && ty->kind == TY_LDOUBLE ? "0xK00000000000000000000"
                                                                        : "0xL0000000000000000000000000000000");
                } else if (d != d || d == (double)INFINITY || d == -(double)INFINITY) {
                    // NaN and the infinities, in the 80-bit pattern: sign and
                    // exponent first, then the significand.
                    uint16_t se = (uint16_t)(d != d ? 0x7fff : (d > 0 ? 0x7fff : 0xffff));
                    uint64_t m = d != d ? (uint64_t)0xc000000000000000 : 0;
                    fprintf(out_file, "0xK%04x%08x%08x", se, (unsigned)(m >> 32), (unsigned)m);
                } else {
                    fprintf(out_file, "%.17e", d);
                }
            } else {
                fprintf(out_file, "0x%016" PRIx64, c->bits.i);
            }
        } else {
            // Constants reach here already normalised to their declared
            // type and width, so the stored value is printed as-is. The
            // type is int64_t, which is not `long` on ILP32 targets
            // (rv32: long is 32 bits), so use the int64 format macro
            // rather than "%ld".
            fprintf(out_file, "%" PRIi64, c->bits.i);
        }
    } else if (c->type == CBits128) {
        if (is_fpval(ty)) {
            // The type name is prepended by the caller. Legacy LLVM
            // literals: half = 0xH + 4 digits; float/double = the double
            // bit pattern in 16 digits (F32 constants are stored as their
            // exact binary64 widening); x86_fp80 = 0xK + 20 digits
            // (mantissa then sign|exp); fp128 = 0xL + low 64 bits first.
            switch (ty->kind) {
                case TY_F16:
                    fprintf(out_file, "0xH%04x", (unsigned)c->bits.i128.limb[0]);
                    break;
                case TY_F32:
                case TY_F64:
                    fprintf(out_file, "0x%08x%08x", (unsigned)c->bits.i128.limb[1], (unsigned)c->bits.i128.limb[0]);
                    break;
                case TY_LDOUBLE:
                    if (T.ldouble_is_fp80)
                        // 80 bits big-endian: sign|exp first, then mantissa
                        fprintf(out_file, "0xK%04x%08x%08x", (unsigned)c->bits.i128.limb[2],
                                (unsigned)c->bits.i128.limb[1], (unsigned)c->bits.i128.limb[0]);
                    else
                        fprintf(out_file, "0xL%08x%08x%08x%08x", (unsigned)c->bits.f128.limb[1],
                                (unsigned)c->bits.f128.limb[0], (unsigned)c->bits.f128.limb[3],
                                (unsigned)c->bits.f128.limb[2]);
                    break;
                default:
                    // LLVM legacy fp128 literal: low 64 bits first
                    fprintf(out_file, "0xL%08x%08x%08x%08x", (unsigned)c->bits.f128.limb[1],
                            (unsigned)c->bits.f128.limb[0], (unsigned)c->bits.f128.limb[3],
                            (unsigned)c->bits.f128.limb[2]);
                    break;
            }
        } else {
            // iN constants: LLVM rejects hex i128, so print decimal
            char buf[48];
            int128_to_str(c->bits.i128, ty->is_unsigned ? UNSIGNED : SIGNED, 10, buf, sizeof(buf));
            fprintf(out_file, "%s", buf);
        }
    } else if (c->type == CAddr) {
        if (c->sym) {
            char *sym = strdup(str(c->sym));
            char *dot = strchr(sym, '.');
            if (dot && dot[1] == '.') {
                print_label(c, sym, dot);
                return;
            }
        }
        if (c->bits.i) {
            fprintf(out_file, "getelementptr (i8, ptr @");
            print_ident(c->sym);
            fprintf(out_file, ", i64 %" PRIi64 ")", c->bits.i);
        } else if (c->sym) {
            fprintf(out_file, "@");
            print_ident(c->sym);
        } else {
            fprintf(out_file, "null");
        }
    }
}

static void print_operand(Ref r);

static void print_operand(Ref r);

// An immediate is read in the type the instruction is spelled with, not in
// whatever type the constant happens to carry: a zero held as a double, used
// in an x86_fp80 compare, printed `0x0000000000000000` and LLVM refused the
// module ("floating point constant does not have type 'x86_fp80'").
static void print_operand_as(Ref r, Type *ty) {
    if (r.type == RCon && is_flonum(ty))
        printcon(&curm->con[r.val], ty);
    else
        print_operand(r);
}

static void print_operand(Ref r) {
    if (r.type == RCon) {
        printcon(&curm->con[r.val], r.ty);
    } else if (r.type == RInt) {
        if (is_flonum(r.ty)) {
            // An integer immediate used as a floating constant (a compare
            // against 0, say). printcon() spells the literal for the type: the
            // double pattern is not a valid x86_fp80 literal, and printing one
            // is what made `fcmp oeq x86_fp80 %x, 0x0000000000000000` fail.
            double d = (double)r.val;
            uint64_t b;
            memcpy(&b, &d, 8);
            Con c = {.type = CBits, .bits.i = (int64_t)b};
            printcon(&c, r.ty);
        } else if (is_pointer(r.ty) || r.ty->kind == TY_NULLPTR) {
            // A null pointer is the immediate zero typed as a pointer, the
            // same as (void *)0. LLVM wants the literal null for a pointer
            // operand, not the integer 0.
            if (r.val != 0) fatal("non-zero integer immediate with pointer type");
            fprintf(out_file, "null");
        } else if ((r.ty->kind & TY_BITINT) == (TY_BITINT | 1) && (r.val == 0 || r.val == 1)) {
            // A 1-bit value: LLVM writes these true/false, which is also
            // how clang spells the is_zero_undef immarg.
            fprintf(out_file, r.val ? "true" : "false");
        } else {
            fprintf(out_file, "%d", r.val);
        }
    } else if (r.type == RGlb) {
        fprintf(out_file, "@");
        print_sym_name((uint32_t)r.val);
    } else if (r.type == RLabel) {
        fprintf(out_file, "blockaddress(@");
        print_sym_name(dump_curf->id);
        fprintf(out_file, ", %%blk%d)", dump_curf->blks[r.val].blk_id);
    } else {
        fprintf(out_file, "%%tmp%d", r.val);
    }
}

// A block with no instructions and no successors has no terminator to print,
// and LLVM rejects that. It is reachable only in the sense that branches name
// it, so it never runs: an explicit unreachable says so.
// Whether the block has no terminator to print: either nothing at all, or
// instructions that the generator left without a branch. LLVM requires every
// block to end in a terminator, so these get an explicit unreachable.
// static bool blk_has_no_terminator(Blk *b) {
//     if (b->succ1 || b->succ || b->narg) return false;
//     return b->jmp.type == IR_NOP || b->jmp.type == IR_JMP;
// }

void dump_blk(Blk *b) {
    // A label definition is `blkN:` with no %; % marks a reference.
    int indent = fprintf(out_file, "blk%d:", b->blk_id);
    if (b->num_pred) {
        fprintf(out_file, "%*.s; preds = ", 48 - indent, "");
        for (uint32_t i = 0; i < b->num_pred; i++) {
            fprintf(out_file, "%%blk%d", b->pred[i]->blk_id);
            if (i < b->num_pred - 1) fprintf(out_file, ", ");
        }
    }
    fprintf(out_file, "\n");
    Phi *p = b->phi;
    while (p) {
        fprintf(out_file, "  %%tmp%d = phi ", p->result.val);
        print_type(p->result.ty);
        for (int i = p->num_arg - 1; i >= 0; i--) {
            fprintf(out_file, " [ ");
            print_operand(p->arg[i]);
            fprintf(out_file, ", %%blk%d ]", p->blk[i]->blk_id);
            if (i) fprintf(out_file, ", ");
        }
        fprintf(out_file, "\n");
        p = p->next;
    }

    Ir *ir = b->head;
    while (ir) {
        fprintf(out_file, "  ");
        if (!refeq(ir->dst, R)) fprintf(out_file, "%%tmp%d = ", ir->dst.val);

        switch (ir->op) {
            // memmory
            case IR_ALLOCA:
                fprintf(out_file, "alloca ");
                print_type(ir->dst.ty->base);
                if (ir->narg == 2) {
                    fprintf(out_file, ", ");
                    print_type(ir->args[0].ty);
                    fprintf(out_file, " ");
                    print_operand(ir->args[0]);
                }
                fprintf(out_file, ", align ");
                print_operand(ir->args[ir->narg - 1]);
                fprintf(out_file, "\n");
                break;
            case IR_LORD: {
                // The address operand is not always a pointer: a record
                // value is a first-class IR value here, so loading one has a
                // record type and no base to inspect.
                Type *at = ir->args[0].ty->kind == TY_PTR ? ir->args[0].ty : NULL;
                fprintf(out_file, "load ");
                if (at && (at->base->qual & Q_ATOMIC)) fprintf(out_file, "atomic ");
                if (at && (at->base->qual & Q_VOLATILE)) fprintf(out_file, "volatile ");
                print_type(ir->dst.ty);
                fprintf(out_file, ", ptr ");
                print_operand(ir->args[0]);
                if (at && (at->base->qual & Q_ATOMIC)) fprintf(out_file, " %s", mem_order_str[ir->mem_order]);
                fprintf(out_file, ", align ");
                print_operand(ir->args[1]);
                fprintf(out_file, "\n");
                break;
            }
            case IR_STR: {
                fprintf(out_file, "store ");
                Type *dt = ir->args[1].ty->kind == TY_PTR ? ir->args[1].ty : NULL;
                if (dt && (dt->base->qual & Q_ATOMIC)) fprintf(out_file, "atomic ");
                if (dt && (dt->base->qual & Q_VOLATILE)) fprintf(out_file, "volatile ");
                print_type(ir->args[0].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[0]);
                fprintf(out_file, ", ptr ");
                print_operand(ir->args[1]);
                if (ir->args[1].ty->base->qual & Q_ATOMIC) fprintf(out_file, " %s", mem_order_str[ir->mem_order]);
                fprintf(out_file, ", align ");
                print_operand(ir->args[2]);
                fprintf(out_file, "\n");
                break;
            }
            case IR_FENCE:
                fprintf(out_file, "fence");
                if (ir->is_signal) fprintf(out_file, " syncscope(\"singlethread\")");
                fprintf(out_file, " %s\n", mem_order_str[ir->mem_order]);
                break;
            case IR_GEP:
                fprintf(out_file, "getelementptr ");
                print_type(ir->args[0].ty->base);
                fprintf(out_file, ", ptr ");
                print_operand(ir->args[0]);
                for (uint32_t i = 1; i < ir->narg; i++) {
                    fprintf(out_file, ", ");
                    print_type(ir->args[i].ty);
                    fprintf(out_file, " ");
                    print_operand(ir->args[i]);
                }
                fprintf(out_file, "\n");
                break;
            case IR_EXTRACTVAL:
                // The field taken is this dst's ty. The aggregate it comes
                // from is either an aggregate type already -- the { iN, i1 }
                // an overflow builtin returns -- or a bare compare type,
                // which is how a cmpxchg carries one; the latter implies
                // the { T, i1 } the instruction produces.
                fprintf(out_file, "extractvalue ");
                if (is_record(ir->args[0].ty)) {
                    print_type(ir->args[0].ty);
                } else {
                    fprintf(out_file, "{ ");
                    print_type(ir->args[0].ty);
                    fprintf(out_file, ", i1 }");
                }
                fprintf(out_file, " ");
                print_operand(ir->args[0]);
                fprintf(out_file, ", ");
                print_operand(ir->args[1]);
                fprintf(out_file, "\n");
                break;
            case IR_CMPXCHG:
                fprintf(out_file, "cmpxchg ");
                if (ir->is_weak) fprintf(out_file, "weak ");
                if (ir->args[0].ty->base->qual & Q_VOLATILE) fprintf(out_file, "volatile ");
                fprintf(out_file, "ptr ");
                print_operand(ir->args[0]);
                fprintf(out_file, ", ");
                print_type(ir->args[1].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[1]);

                fprintf(out_file, ", ");
                print_type(ir->args[2].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[2]);
                fprintf(out_file, " %s %s", mem_order_str[ir->mem_order], mem_order_str[ir->mem_order1]);
                fprintf(out_file, ", align %d\n", ir->args[1].ty->align);
                break;

            case IR_ATOMICRMW:
                fprintf(out_file, "atomicrmw ");
                if (ir->args[1].ty->base->qual & Q_VOLATILE) fprintf(out_file, "volatile ");
                fprintf(out_file, "%s ", atomicrmw_op[ir->args[0].val]);
                fprintf(out_file, "ptr ");
                print_operand(ir->args[1]);
                fprintf(out_file, ", ");
                print_type(ir->args[2].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[2]);

                fprintf(out_file, " %s", mem_order_str[ir->mem_order]);
                fprintf(out_file, ", align %d\n", ir->args[2].ty->align);
                break;

            case IR_MEMCPY:
                fprintf(out_file, "call void @llvm.memcpy.p0.p0.i64(ptr ");
                print_operand(ir->args[0]);
                fprintf(out_file, ", ptr ");
                print_operand(ir->args[1]);
                fprintf(out_file, ", i64 ");
                print_operand(ir->args[2]);
                fprintf(out_file, ", i1 false)\n");
                break;
            case IR_MEMSET:
                fprintf(out_file, "call void @llvm.memset.p0.i64(ptr ");
                print_operand(ir->args[0]);
                fprintf(out_file, ", i8 ");
                print_operand(ir->args[1]);
                fprintf(out_file, ", i64 ");
                print_operand(ir->args[2]);
                fprintf(out_file, ", i1 false)\n");
                break;
            case IR_TLSADDR:
                fprintf(out_file, "call ptr @llvm.threadlocal.address.p0(ptr ");
                print_operand(ir->args[0]);
                fprintf(out_file, ")\n");
                break;
            case IR_SELECT:
                // select i1 <cond>, <ty> <t>, <ty> <f>
                fprintf(out_file, "select ");
                print_type(ir->args[0].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[0]);
                fprintf(out_file, ", ");
                print_type(ir->args[1].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[1]);
                fprintf(out_file, ", ");
                print_type(ir->args[2].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[2]);
                fprintf(out_file, "\n");
                break;
            case IR_ASM: {
                // call [<ty>] asm [sideeffect] "<template>", "<constraints>"(<args>)
                // An asm goto is the same call with `callbr` for its opcode:
                // the labels its `to label` list names are printed by the
                // terminator that follows, since that is where they belong.
                fprintf(out_file, ir->asm_flags & ASM_GOTO ? "callbr " : "call ");
                if (refeq(ir->dst, R))
                    fprintf(out_file, "void");
                else
                    print_type(ir->dst.ty);
                // `sideeffect` is what says the statement may not be dropped
                // or moved. A template with no output has it whether or not
                // the statement said `volatile`; the parser settled that.
                fprintf(out_file, " asm ");
                if (ir->asm_flags & (ASM_VOLATILE | ASM_GOTO)) fprintf(out_file, "sideeffect ");
                fprintf(out_file, "\"");
                for (char *p = ir->asm_tmpl; *p; p++) fprintf(out_file, "%s", escape_char_to_string(*p));
                fprintf(out_file, "\", \"");
                for (char *p = ir->asm_cons; *p; p++) fprintf(out_file, "%s", escape_char_to_string(*p));
                fprintf(out_file, "\"(");
                for (uint32_t i = 0; i < ir->narg; i++) {
                    if (i) fprintf(out_file, ", ");
                    // An indirect constraint takes an address, and LLVM
                    // insists on knowing what it points at: `ptr` alone is
                    // rejected ("operand for indirect constraint must have
                    // elementtype attribute").
                    if (ir->asm_ind && ir->asm_ind[i] && ir->args[i].ty && ir->args[i].ty->kind == TY_PTR) {
                        fprintf(out_file, "ptr elementtype(");
                        print_type(ir->args[i].ty->base);
                        fprintf(out_file, ") ");
                    } else {
                        print_type(ir->args[i].ty);
                        fprintf(out_file, " ");
                    }
                    print_operand(ir->args[i]);
                }
                fprintf(out_file, ")\n");
                break;
            }
            case IR_CALL:
                fprintf(out_file, "call ");
                if (refeq(ir->dst, R))
                    fprintf(out_file, "void");
                else
                    print_type(ir->dst.ty);
                fprintf(out_file, " ");
                // The explicit callee type is required only for variadic
                // calls (LangRef: the fnty "is only required if the
                // signature specifies a varargs type"); it drives
                // variadic ABI lowering, e.g. rv64 passes variadic FP
                // arguments in integer registers only when the type is
                // spelled out.
                Type *fty = ir->args[0].ty;
                if (fty->kind == TY_PTR) fty = fty->base;
                if (fty->is_variadic) {
                    // The type has to be the one the declaration of the
                    // callee spells, or LLVM rejects the call, so it goes
                    // through the same lowering dump_fn applies: a record
                    // parameter arrives as one value per piece rather than as
                    // the record (cpython's _PyCompile_Error takes a
                    // _Py_SourceLocation before its "..."). Types only: an
                    // attribute here is "argument attributes invalid in
                    // function type", so print_param_type is asked for the
                    // bare form.
                    bool first = true;
                    fprintf(out_file, "(");
                    if (ir->is_sret && fty->ret) {
                        fprintf(out_file, "ptr");
                        first = false;
                    }
                    for (Type *p = fty->params; p; p = p->next) {
                        int cntt = abi_param_count(p);
                        for (int k = 0; k < cntt; k++) {
                            if (!first) fprintf(out_file, ", ");
                            print_param_type(p, k, true);
                            first = false;
                        }
                    }
                    if (!first) fprintf(out_file, ", ");
                    fprintf(out_file, "...) ");
                }
                print_operand(ir->args[0]);
                fprintf(out_file, "(");
                // A pointer at the head of the list carries the result of a
                // memory-class aggregate; a pointer to one further along is a
                // copy the callee may not write back. Both are spelled out,
                // exactly as the definition does.
                // The first argument is the result pointer when the callee returns
                // an aggregate in memory and the function itself returns nothing.
                // The generator marks whether this call passes a hidden result
                // pointer; only then is the leading pointer an sret.
                // Recorded when the call was built: printing happens after all
                // generation, so a flag set during generation would have been
                // overwritten many times over by then.
                bool sret = ir->is_sret && ir->narg > 1 && ir->args[1].ty->kind == TY_PTR;
                for (uint32_t i = 1; i < ir->narg; i++) {
                    Type *at = ir->args[i].ty;
                    if (sret && i == 1) {
                        fprintf(out_file, "ptr noalias sret(");
                        print_type(at->base);
                        fprintf(out_file, ") align %d ", at->base->align);
                    } else if (is_byval(ir, i)) {
                        // Only the operand the generator marked is a copy: a
                        // pointer to a record that the program passed itself
                        // is an ordinary argument, and reading its pointee as
                        // a copy would be wrong.
                        fprintf(out_file, "ptr byval(");
                        print_type(at->base);
                        fprintf(out_file, ") align %d ", at->base->align);
                    } else {
                        print_type(at);
                        fprintf(out_file, " ");
                    }
                    print_operand(ir->args[i]);
                    if (i < ir->narg - 1) fprintf(out_file, ", ");
                }
                fprintf(out_file, ")\n");
                break;
            case IR_SP_SAVE:
                fprintf(out_file, "call ptr @llvm.stacksave.p0()\n");
                break;
            case IR_SP_RESTORE:
                fprintf(out_file, "call void @llvm.stackrestore.p0(ptr ");
                print_operand(ir->args[0]);
                fprintf(out_file, ")\n");
                break;
            // conversion
            case IR_EXT:
            case IR_TRUNC:
            case IR_FPTOINT:
            case IR_INTTOFP:
            case IR_PTRTOINT:
            case IR_INTTOPTR: {
                Type *ty = ir->args[0].ty;
                int idx = ty->is_unsigned;
                if (is_flonum(ty)) {
                    if (!is_flonum(ir->dst.ty))
                        idx = ir->dst.ty->is_unsigned;
                    else
                        idx = 2;
                }
                fprintf(out_file, "%s ", op_str[ir->op][idx]);
                print_type(ir->args[0].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[0]);
                fprintf(out_file, " to ");
                print_type(ir->dst.ty);
                fprintf(out_file, "\n");
                break;
            }
            case IR_BITCAST:
                fprintf(out_file, "bitcast ");
                print_type(ir->args[0].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[0]);
                fprintf(out_file, " to ");
                print_type(ir->dst.ty);
                fprintf(out_file, "\n");
                break;
            // fneg
            case IR_NEG:
                fprintf(out_file, "fneg ");
                print_type(ir->args[0].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[0]);
                fprintf(out_file, "\n");
                break;
            // arithmetic
            case IR_ADD:
            case IR_SUB:
            case IR_MUL:
            case IR_DIV:
            case IR_REM:
            case IR_AND:
            case IR_OR:
            case IR_XOR:
            case IR_SHL:
            case IR_SHR:
            case IR_CMP_EQ:
            case IR_CMP_NE:
            case IR_CMP_LE:
            case IR_CMP_LT:
            case IR_CMP_ONE:
            case IR_CMP_UNO: {
                int idx = ir->args[0].ty->is_unsigned;
                if (is_flonum(ir->args[0].ty)) idx = 2;
                fprintf(out_file, "%s ", op_str[ir->op][idx]);
                print_type(ir->args[0].ty);
                fprintf(out_file, " ");
                print_operand(ir->args[0]);
                fprintf(out_file, ", ");
                print_operand_as(ir->args[1], ir->args[0].ty);
                fprintf(out_file, "\n");
                break;
            }
            default:
                fatal("unknown ir op kind %d", ir->op);
        }
        ir = ir->next;
    }

    fprintf(out_file, "  ");
    switch (b->jmp.type) {
        case IR_RET:
            fprintf(out_file, "ret ");
            if (!refeq(b->jmp.arg, R)) {
                print_type(b->jmp.arg.ty);
                fprintf(out_file, " ");
                print_operand(b->jmp.arg);
            } else {
                fprintf(out_file, "void");
            }
            fprintf(out_file, "\n");
            break;
        case IR_CALLBR:
            // The other half of an asm goto: the block control falls through
            // to, then the labels the template may jump to, in the order the
            // constraint string named them -- which is the order the template
            // numbers them in.
            fprintf(out_file, "        to label %%blk%d [", b->succ1->blk_id);
            for (uint32_t i = 0; i < b->narg; i++) {
                if (i) fprintf(out_file, ", ");
                fprintf(out_file, "label %%blk%d", b->succ[i]->blk_id);
            }
            fprintf(out_file, "]\n");
            break;
        case IR_JMP:
            fprintf(out_file, "br label %%blk%d\n", b->succ1->blk_id);
            break;
        case IR_JNZ:
            fprintf(out_file, "br i1 ");
            print_operand(b->jmp.arg);
            fprintf(out_file, ", label %%blk%d, label %%blk%d\n", b->succ1->blk_id, b->succ2->blk_id);
            break;
        case IR_SWITCH:
            fprintf(out_file, "switch ");
            print_type(b->jmp.arg.ty);
            fprintf(out_file, " ");
            print_operand(b->jmp.arg);
            // The bracketed case list is part of the syntax even when it is
            // empty. A `case` range whose bounds are reversed (6.6.2)
            // contributes no cases, and `switch i32 %x, label %blk` without
            // the brackets is a parse error for LLVM ("expected '[' with
            // switch table").
            fprintf(out_file, ", label %%blk%d [\n", b->succ1->blk_id);
            for (uint32_t i = 0; i < b->narg; i++) {
                fprintf(out_file, "    ");
                print_type(b->jmp.args[i].ty);
                fprintf(out_file, " ");
                print_operand(b->jmp.args[i]);
                fprintf(out_file, ", label %%blk%d\n", b->succ[i]->blk_id);
            }
            fprintf(out_file, "  ]\n");
            break;
        case IR_INDIRECTBR:
            fprintf(out_file, "indirectbr ");
            print_type(b->jmp.arg.ty);
            fprintf(out_file, " ");
            print_operand(b->jmp.arg);
            fprintf(out_file, ", [");
            for (uint32_t i = 0; i < b->narg; i++) {
                if (i) fprintf(out_file, ", ");
                fprintf(out_file, "label %%blk%d", b->succ[i]->blk_id);
            }
            fprintf(out_file, "]\n");
            break;
        default:
            break;
    }
}

static void dump_init(Initializer *init, Type *ty);
static void print_init_ty(Initializer *init, Type *ty);
static Member *union_canon_member(Type *ty);

// A record's braces. A packed record is written `<{ ... }>`: its body carries
// an explicit `[N x i8]` element for every gap, which only reproduces the C
// offsets if LLVM adds no padding of its own -- spelled with plain braces, the
// `double` of `struct { char c; double i; } __attribute__((packed))` moves from
// offset 1 to 8, and an initializer written against the type fills the wrong
// bytes. Types and constants are spelled the same way, as clang does.
// True when a member owns an element of the record's image. A bit-field is
// addressed through the access unit it lives in, which is that unit's element;
// a zero-width one has no bits at all and no unit of its own -- the layout
// gives it the offset of the member that follows, and that member is the one
// the element belongs to (6.7.2.1p12: a zero-width field forces the next field
// to a unit boundary, it does not occupy one).
static bool member_has_element(Member *m) { return !m->is_bitfield || m->bit_width > 0; }

static void record_open(Type *ty) { fprintf(out_file, ty->layout_packed ? "<{ " : "{ "); }

static void record_close(Type *ty) { fprintf(out_file, ty->layout_packed ? " }>" : " }"); }

void dump_type(Type *ty) {
    fprintf(out_file, "%%");
    print_ident(ty->uid);
    fprintf(out_file, " = type ");
    record_open(ty);
    Member *mem = ty->members;
    if (ty->kind == TY_STRUCT) {
        int pos = 0;
        bool first = true;
        for (; mem; mem = mem->next) {
            int off = mem->offset;
            Type *memty = mem->is_bitfield ? mem->unit_ty : mem->ty;
            // The bytes the member's bits occupy -- for a bit-field that is
            // not its access unit (5 + 29 bits is five bytes, not eight), and
            // a field straddling the cursor is still owed the bytes beyond it.
            int end =
                member_has_element(mem)
                    ? (off * 8 + (mem->is_bitfield ? mem->bit_offset + mem->bit_width : mem->ty->size * 8) + 7) / 8
                    : pos;
            if (end <= pos) continue;
            if (!first) fprintf(out_file, ", ");
            first = false;
            if (pos < off) {
                fprintf(out_file, "[%d x i8], ", off - pos);
                pos = off;
            }
            if (pos > off) {
                // The tail of a straddling unit, one byte at a time.
                for (int i = pos; i < end; i++) fprintf(out_file, "%si8", i > pos ? ", " : "");
                pos = end;
            } else {
                print_type(memty);
                // The element is the access unit, which reaches past the bits
                // for a field whose width is not a whole number of bytes. The
                // value walk advances by the unit, so this one has to as well.
                pos = off + memty->size;
            }
        }
        if (pos < ty->size) {
            if (!first) fprintf(out_file, ", ");
            fprintf(out_file, "[%d x i8]", ty->size - pos);
        }
    } else if (ty->kind == TY_UNION) {
        // A union with no members (`union blank_uni1 { };`, which C/0186/0076
        // writes) has no canonical member; its image is empty, the same `{ }`
        // the struct branch above prints for an empty record.
        mem = union_canon_member(ty);
        if (mem) {
            print_type(mem->ty);
            if (mem->ty->size < ty->size) fprintf(out_file, ", [%d x i8]", ty->size - mem->ty->size);
        }
    }
    record_close(ty);
    fprintf(out_file, "\n");
}

// The union's canonical element: the member with the largest
// alignment (as in clang); the union's LLVM type is that member plus
// padding. Initializers of other members use anonymous member-typed
// element types so that any member's value is a valid constant
// (type-punning).
static Member *union_canon_member(Type *ty) {
    // Only a named member can be it: an unnamed bit-field takes no initializer
    // and holds no value of its own, and picking one made the initializer look
    // like it wrote a non-canonical member (see init_needs_inline).
    Member *m = NULL;
    for (Member *x = ty->members; x; x = x->next)
        if (x->name && (!m || x->align > m->align)) m = x;
    // A union of nothing but unnamed bit-fields has no named member at all;
    // the first one is then the only thing to spell.
    return m ? m : ty->members;
}

// The anonymous member-typed element type: "{ <mem-ty> [, pad] }".
static void print_union_elem_ty(Type *ty, Member *mem, Initializer *child) {
    record_open(ty);
    // The member's type as its own value is spelled: a member that is itself a
    // union punned one level down is written `{ i8, [7 x i8] }`, not by name.
    print_init_ty(child, mem->ty);
    if (mem->ty->size < ty->size) fprintf(out_file, ", [%d x i8]", ty->size - mem->ty->size);
    record_close(ty);
}

// A scalar union member value, cast through pointers as needed.
static void print_union_con(Con *c, Type *mem_ty) {
    if (c->type == CAddr && mem_ty->kind != TY_PTR) {
        fprintf(out_file, "ptrtoint (ptr ");
        printcon(c, mem_ty);
        fprintf(out_file, " to i%d)", mem_ty->size * 8);
        return;
    }
    if (c->type == CBits && mem_ty->kind == TY_PTR) {
        fprintf(out_file, "inttoptr (i%d ", mem_ty->size * 8);
        printcon(c, mem_ty);
        fprintf(out_file, " to ptr)");
        return;
    }
    printcon(c, mem_ty);
}

// The union element value: the member-typed value plus padding.
static void dump_union_elem(Type *ty, Member *mem, Initializer *child) {
    record_open(ty);
    if (mem->ty->kind == TY_STRUCT || mem->ty->kind == TY_UNION || mem->ty->kind == TY_ARRAY) {
        // A member that is itself an aggregate carries an aggregate value; the
        // scalar printer below would spell it `%union.X 0`, which LLVM refuses
        // ("integer/byte constant must have ...").
        dump_init(child, mem->ty);
    } else {
        print_type(mem->ty);
        fprintf(out_file, " ");
        // An unnamed bit-field member takes no initializer, so there may be no
        // value here at all; the bytes it covers are zero. Handing the null
        // Con to print_union_con() is what killed the compiler.
        if (child && child->val)
            print_union_con(child->val, mem->ty);
        else
            printcon(&(Con){0, CBits, 0, {0}}, mem->ty);
    }
    if (mem->ty->size < ty->size) fprintf(out_file, ", [%d x i8] zeroinitializer", ty->size - mem->ty->size);
    record_close(ty);
}

// True when what is written for this initializer is the member-typed
// (type-punning) form rather than the type's own image. It decides how the
// *containing* type has to be spelled, so dump_init(), print_init_ty() and
// print_union_elem_ty() all have to agree on it -- a union inside a union
// punned one level down makes the level above it inline as well.
static bool init_is_punned(Initializer *init, Type *ty) {
    if (!init) return false;
    if (ty->kind == TY_UNION) {
        Member *mem = init->mem ? init->mem : union_default_member(ty);
        // A union with no members has nothing to punned-write.
        if (!mem) return false;
        if (mem != union_canon_member(ty)) return true;
        return init_is_punned(init->child[mem->idx], mem->ty);
    }
    if (ty->kind == TY_STRUCT) {
        for (Member *m = ty->members; m; m = m->next)
            if (!m->is_bitfield && init_is_punned(init->child[m->idx], m->ty)) return true;
        return false;
    }
    if (ty->kind == TY_ARRAY) {
        for (int i = 0; i < ty->len; i++)
            if (init_is_punned(init->child[i], ty->base)) return true;
        return false;
    }
    return false;
}

// Whether an initializer needs its type spelled out. A union initialized
// through a member other than the canonical one is written *as that member* --
// clang's type-punning form, `{ %struct.anon }` for an `int64_t` union
// initialized through an 8-byte struct member -- and LLVM then requires the
// type containing it to be spelled out the same way. `%struct.obj = type
// { %union.u, ptr }` with an initializer that writes the union's other member
// is what LLVM reports as "element 0 of struct initializer doesn't match
// struct element type": cpython's `struct _object` has exactly that shape, its
// first member a union of an `int64_t` refcount and a struct of three smaller
// fields.
static bool init_needs_inline(Initializer *init, Type *ty) {
    if (!init || !init->is_inited) return false;

    if (ty->kind == TY_UNION) {
        Member *mem = init->mem ? init->mem : union_default_member(ty);
        Member *canon = union_canon_member(ty);
        if (!mem) return false;
        if (mem != canon) return true;
        // The member may itself be an aggregate whose value takes the inline
        // form -- a union of a union, the inner one initialized through a
        // member that is not *its* canonical one. The containing type has to
        // be spelled the same way the value is.
        return init_needs_inline(init->child[mem->idx], mem->ty);
    }
    if (ty->kind == TY_STRUCT) {
        for (Member *m = ty->members; m; m = m->next) {
            // A bit-field has no element of its own (see dump_init).
            if (m->is_bitfield) continue;
            if (init_needs_inline(init->child[m->idx], m->ty)) return true;
        }
        return false;
    }
    if (ty->kind == TY_ARRAY) {
        for (int i = 0; i < ty->len; i++)
            if (init_needs_inline(init->child[i], ty->base)) return true;
        return false;
    }
    return false;
}

// The type a `dump_init()` value is written with. Everything that needs no
// type-punning keeps its name, so the IR of every other initializer is left
// exactly as it was; only a type that contains a punned union is spelled out,
// and it is spelled out the way `dump_init()` writes its elements -- the same
// walk, in the same order, or LLVM rejects the initializer as belonging to
// another type.
static void print_init_ty(Initializer *init, Type *ty) {
    if (!init_needs_inline(init, ty)) {
        print_type(ty);
        return;
    }

    if (ty->kind == TY_UNION) {
        Member *canon = union_canon_member(ty);
        if (!canon) {
            print_type(ty);
            return;
        }
        Member *mem = (init->is_inited && init->mem) ? init->mem : canon;
        // The same test dump_init() makes for the value: a canonical member
        // whose own initializer is written punned (a union inside it) makes
        // this union inline as well, or the type and the value disagree.
        if (mem == canon && !init_is_punned(init->child[mem->idx], mem->ty))
            print_type(ty);
        else
            print_union_elem_ty(ty, mem, init->child[mem->idx]);
        return;
    }

    if (ty->kind == TY_STRUCT) {
        record_open(ty);
        int pos = 0;
        bool first = true;
        for (Member *m = ty->members; m;) {
            int off = m->offset;
            int end = member_has_element(m)
                          ? (off * 8 + (m->is_bitfield ? m->bit_offset + m->bit_width : m->ty->size * 8) + 7) / 8
                          : pos;
            if (end <= pos) {
                m = m->next;
                continue;
            }
            if (!first) fprintf(out_file, ", ");
            first = false;
            if (pos < off) {
                fprintf(out_file, "[%d x i8], ", off - pos);
                pos = off;
            }
            if (pos > off) {
                for (int i = pos; i < end; i++) fprintf(out_file, "%si8", i > pos ? ", " : "");
                pos = end;
            } else if (m->is_bitfield) {
                print_type(m->unit_ty);
                pos = off + m->unit_ty->size;
            } else {
                print_init_ty(init->child[m->idx], m->ty);
                pos = off + m->ty->size;
            }
            m = m->next;
        }
        if (pos < ty->size) {
            if (!first) fprintf(out_file, ", ");
            fprintf(out_file, "[%d x i8]", ty->size - pos);
        }
        record_close(ty);
        return;
    }

    if (ty->kind == TY_ARRAY) {
        // An array has no padding between its elements, so a packed struct of
        // per-element element types has its layout.
        fprintf(out_file, "<{ ");
        for (int i = 0; i < ty->len; i++) {
            if (i) fprintf(out_file, ", ");
            print_init_ty(init->child[i], ty->base);
        }
        fprintf(out_file, " }>");
        return;
    }

    print_type(ty);
}

// The bits of one element of a record's image. An element is a byte range
// `[pos, pos + size)`, and a bit-field may straddle two of them: the element
// boundaries come from the type, where each field that starts a new access
// unit contributes that unit's type, and the smallest unit covering a 12-bit
// field is two bytes, so a following 8-bit field begins inside it and ends in
// the next element. Every field that overlaps the range therefore contributes
// the part of its value that falls in it, at its own distance from the
// element's first bit.
static int64_t bitfield_image(Type *ty, Initializer *init, int pos, int size) {
    int lo = pos * 8, hi = (pos + size) * 8;
    int64_t val = 0;
    for (Member *m = ty->members; m; m = m->next) {
        if (!m->is_bitfield) continue;
        int start = m->offset * 8 + m->bit_offset;
        int end = start + m->bit_width;
        int from = MAX(start, lo);
        int to = MIN(end, hi);
        if (from >= to) continue;
        Con *c = init->child[m->idx] ? init->child[m->idx]->val : NULL;
        uint64_t bits = c ? (uint64_t)c->bits.i : 0;
        uint64_t mask = (to - from >= 64) ? ~(uint64_t)0 : ((uint64_t)1 << (to - from)) - 1;
        val |= (int64_t)(((bits >> (from - start)) & mask) << (from - lo));
    }
    return val;
}

static void dump_init(Initializer *init, Type *ty) {
    if (ty->kind == TY_UNION) {
        if (!init || !init->is_inited) {
            print_type(ty);
            fprintf(out_file, " zeroinitializer");
            return;
        }
        Member *mem = init->mem ? init->mem : union_default_member(ty);
        if (!mem) {
            // An empty union: nothing to initialize, and the image is empty.
            print_type(ty);
            fprintf(out_file, " zeroinitializer");
            return;
        }
        Member *canon = union_canon_member(ty);
        Initializer *child = init->child[mem->idx];
        if (mem != canon || init_is_punned(child, mem->ty)) {
            // Anonymous member-typed form (type-punning, as in clang) -- and
            // also when the member is the canonical one but its own value is
            // written punned, because then the types have to match too.
            print_union_elem_ty(ty, mem, child);
        } else {
            print_type(ty);
        }
        fprintf(out_file, " ");
        dump_union_elem(ty, mem, child);
        return;
    }

    if (ty->kind == TY_ARRAY && init_needs_inline(init, ty)) {
        // The packed form, element by element, each with the type it was
        // written as: `dump_init()` writes them in the order and the shape
        // `init_needs_inline()` saw.
        fprintf(out_file, "<{ ");
        for (int i = 0; i < ty->len; i++) {
            if (i) fprintf(out_file, ", ");
            print_init_ty(init->child[i], ty->base);
        }
        fprintf(out_file, " }> <{ ");
        for (int i = 0; i < ty->len; i++) {
            if (i) fprintf(out_file, ", ");
            dump_init(init->child[i], ty->base);
        }
        fprintf(out_file, " }>");
        return;
    }

    print_init_ty(init, ty);
    fprintf(out_file, " ");
    if (ty->kind == TY_ARRAY) {
        if (!init || !init->is_inited) {
            fprintf(out_file, "zeroinitializer");
            return;
        }
        // `c"..."` is an [N x i8] constant, so it needs an integer element:
        // a one-byte struct is not one. libswscale's `FormatEntry` is a struct
        // of one byte, and writing its array as a string gave LLVM
        // "'[227 x i8]' but expected '[227 x %struct.FormatEntry]'".
        if (is_integer(ty->base) && ty->base->size == 1) {
            fprintf(out_file, "c\"");
            for (int i = 0; i < ty->len; i++) {
                if (!init->child[i]->val)
                    fprintf(out_file, "\\00");
                else
                    fprintf(out_file, "%s", escape_char_to_string(init->child[i]->val->bits.i));
            }
            fprintf(out_file, "\"");
        } else {
            fprintf(out_file, "[");
            for (int i = 0; i < ty->len; i++) {
                if (i) fprintf(out_file, ", ");
                dump_init(init->child[i], ty->base);
            }
            fprintf(out_file, "]");
        }
        return;
    }
    if (ty->kind == TY_STRUCT) {
        if (!init || !init->is_inited) {
            fprintf(out_file, "zeroinitializer");
            return;
        }
        record_open(ty);
        Member *mem = ty->members;
        // The walk `dump_type` makes, and it has to be that one: the elements
        // written here are the elements written there, in the same order, or
        // LLVM rejects the initializer as belonging to another type. A
        // bit-field that begins inside the access unit of an earlier member
        // has no element of its own there -- its bits are part of that
        // member's value -- so it can have none here either. Grouping
        // bit-fields by equal `offset` instead, as this used to, invented an
        // element per field: libpng's read_chunks table came out with eight
        // values for a five-element struct, and the padding between them was
        // a negative length, `[-1 x i8]`.
        int pos = 0;
        bool first = true;
        while (mem) {
            int off = mem->offset;
            int end =
                member_has_element(mem)
                    ? (off * 8 + (mem->is_bitfield ? mem->bit_offset + mem->bit_width : mem->ty->size * 8) + 7) / 8
                    : pos;
            if (end <= pos) {
                mem = mem->next;
                continue;
            }
            if (!first) fprintf(out_file, ", ");
            first = false;
            if (pos < off) {
                fprintf(out_file, "[%d x i8] zeroinitializer, ", off - pos);
                pos = off;
            }
            if (pos > off) {
                // The tail of a straddling unit: one byte element per byte,
                // each carrying the bits that fall in it.
                for (int i = pos; i < end; i++) {
                    if (i > pos) fprintf(out_file, ", ");
                    int64_t b = bitfield_image(ty, init, i, 1);
                    fprintf(out_file, "i8 ");
                    printcon(&(Con){0, CBits, 0, {b}}, T.ty_char);
                }
                pos = end;
                mem = mem->next;
                continue;
            }
            if (mem->is_bitfield) {
                int64_t val = bitfield_image(ty, init, pos, mem->unit_ty->size);
                print_type(mem->unit_ty);
                fprintf(out_file, " ");
                printcon(&(Con){0, CBits, 0, {val}}, mem->unit_ty);
                pos += mem->unit_ty->size;
                mem = mem->next;
            } else {
                dump_init(init->child[mem->idx], mem->ty);
                pos += mem->ty->size;
                mem = mem->next;
            }
        }
        if (pos < ty->size) {
            if (!first) fprintf(out_file, ", ");
            fprintf(out_file, "[%d x i8] zeroinitializer", ty->size - pos);
        }
        record_close(ty);
        return;
    }
    if (!init || !init->is_inited) {
        // zeroinitializer works for any type; the bare "0" was rejected
        // for pointer globals (tentative definitions)
        fprintf(out_file, "zeroinitializer");
        return;
    }
    // nullptr_t is pointer-sized and pointer-valued, so it takes the same
    // spelling as a pointer.
    bool is_ptr_init = init->ty->kind == TY_PTR || init->ty->kind == TY_NULLPTR;
    if (is_ptr_init && init->val->type == CBits) {
        // A zero value is the null pointer, whose LLVM literal is simply
        // ; only a non-zero integer needs inttoptr.
        if (init->val->bits.i == 0) {
            fprintf(out_file, "null");
            return;
        }
        fprintf(out_file, "inttoptr (i%d ", T.ty_voidptr->size * 8);
        printcon(init->val, init->ty);
        fprintf(out_file, " to ptr)");
        return;
    }
    if (!is_ptr_init && init->val->type == CAddr) {
        fprintf(out_file, "ptrtoint (ptr ");
        printcon(init->val, init->ty);
        fprintf(out_file, " to i%d)", T.ty_voidptr->size * 8);
        return;
    }
    printcon(init->val, init->ty);
}

static void dump_str(Sym *data) {
    char *p = str(data->init_data);
    int len = data->ty->len;
    fprintf(out_file, "private unnamed_addr constant ");
    print_type(data->ty);
    if (len == 1) {
        fprintf(out_file, " zeroinitializer");
    } else {
        if (data->ty->base->size == 1) {
            fprintf(out_file, " c\"");
            for (int i = 0; i < len; i++) fprintf(out_file, "%s", escape_char_to_string(p[i]));
            fprintf(out_file, "\"");
        } else if (data->ty->base->size == 2) {
            uint16_t *buf = (uint16_t *)p;
            fprintf(out_file, " [");
            for (int i = 0; i < data->ty->len; i++) {
                if (i) fprintf(out_file, ", ");
                fprintf(out_file, "i16 %d", buf[i]);
            }
            fprintf(out_file, "]");
        } else if (data->ty->base->size == 4) {
            uint32_t *buf = (uint32_t *)p;
            fprintf(out_file, " [");
            for (int i = 0; i < data->ty->len; i++) {
                if (i) fprintf(out_file, ", ");
                fprintf(out_file, "i32 %d", buf[i]);
            }
            fprintf(out_file, "]");
        }
    }
    // The storage alignment: the declared one, raised for a big array
    // (see object_align). data->align itself is what _Alignof reports.
    fprintf(out_file, ", align %d\n", object_align(data->ty, data->align));
    return;
}

static const char *sclass_name[] = {
    [SC_NONE] = "dso_local",     [SC_EXTERN] = "external",     [SC_STATIC] = "internal",
    [SC_CONSTEXPR] = "internal", [SC_THREAD] = "thread_local",
};

void dump_data(Sym *data) {
    if (already_emitted(data)) return;
    if (data->is_dead) return;
    fprintf(out_file, "@");
    print_sym_name(data->id);
    fprintf(out_file, " = ");
    if (data->is_str) {
        dump_str(data);
        return;
    }

    int sclass = data->sclass;
    bool is_tls = sclass & SC_THREAD;
    sclass &= ~(SC_THREAD);

    if (!data->is_defined && !data->sclass && opt_fcommon) fprintf(out_file, "common ");
    if (data->sclass || !opt_fpic) fprintf(out_file, "%s ", sclass_name[sclass]);
    if (is_tls) fprintf(out_file, "thread_local ");
    fprintf(out_file, "global ");

    if (sclass & SC_EXTERN)
        print_type(data->ty);
    else
        dump_init(data->init, data->ty);

    // The storage alignment: the declared one, raised for a big array
    // (see object_align). data->align itself is what _Alignof reports.
    fprintf(out_file, ", align %d\n", object_align(data->ty, data->align));
}

// Whether the ABI lowering decides this type's shape: a record, or a scalar
// the target hands over by reference.
static bool is_agg(Type *ty) {
    if (!ty) return false;
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) return true;
    return T.scalar_by_ref && T.scalar_by_ref(ty);
}

// A register-class aggregate result comes back as its pieces; a memory-class
// one is written through a pointer the caller supplies, and the function
// itself returns nothing.
static void print_ret_type(Type *ty) {
    if (abi_lowering() && is_agg(ty)) {
        // A value the ABI returns in a register, though an argument of the
        // same type goes in memory.
        Type *scalar = T.agg_ret_value ? T.agg_ret_value(ty) : NULL;
        if (scalar) {
            print_type(scalar);
            return;
        }
        AggClass c;
        T.classify_aggregate(ty, &c);
        if (c.npiece == 0) {
            fprintf(out_file, "void");
            return;
        }
        // One piece has no record at all: the function hands the bare value
        // back. A pair needs a record, and it has to be the very type irgen
        // builds the value with, or the signature and the body would spell
        // the same thing two different ways.
        if (c.npiece == 1) {
            // A lone piece is normally the bare value, but an ABI that keeps
            // a homogeneous aggregate an array even at one element (AAPCS64
            // spells struct { float } as [1 x float]) says otherwise.
            // An AAPCS64 homogeneous aggregate keeps its own type as the
            // return type -- the array form is how it is passed, not how it
            // is returned -- so only the other ABIs need the bare piece.
            if (c.is_hfa && T.agg_always_array) {
                print_type(ty);
                return;
            }
            print_type(c.piece[0].ty);
            return;
        }
        // A homogeneous aggregate is returned as itself; anything else is
        // returned as the record of its pieces.
        if (c.is_hfa && T.agg_always_array) {
            print_type(ty);
            return;
        }
        if (T.pieces_type) {
            Type *rec = T.pieces_type(ty);
            if (rec) {
                print_type(rec);
                return;
            }
        }
    }
    // A returned scalar carries its mark before the type: signext i8. A
    // memory-class result is void by the time it gets here, and has none.
    char *ext = ext_attr(ty);
    if (ext) fprintf(out_file, "%s ", ext);
    print_type(ty);
}

// How many IR parameters one C parameter becomes: a memory-class aggregate
// travels as a single byval pointer, a register-class one is flattened into
// one parameter per piece, everything else stays as it is.
static int abi_param_count(Type *ty) {
    if (!abi_lowering() || !is_agg(ty)) return 1;
    AggClass c;
    T.classify_aggregate(ty, &c);
    return agg_param_slots(ty, &c);
}

static void print_param_type(Type *ty, int i, bool bare) {
    if (abi_lowering() && is_agg(ty)) {
        AggClass c;
        T.classify_aggregate(ty, &c);
        if (c.npiece == 0) {
            if (bare) {
                // A copy is a pointer whatever the spelling, and a call's
                // type list has no room for byval or align: clang writes a
                // bare `ptr` there and the attribute goes on the operand.
                fprintf(out_file, "ptr");
                return;
            }
            // SysV has the callee copy the argument, which the IR spells
            // byval; AAPCS64 and RISC-V have the caller copy it and pass a
            // plain pointer, so byval there would ask for a second copy.
            if (T.agg_byval_param) {
                fprintf(out_file, "ptr byval(");
                print_type(ty);
                fprintf(out_file, ") align %d", ty->align);
            } else {
                fprintf(out_file, "ptr align %d", ty->align);
            }
            return;
        }
        // The parameter arrives as one value of the shape the target chose:
        // an array of the repeated element type, or the bare piece when there
        // is only one. A record of pieces is the other case, and then there is
        // one parameter per piece -- the two have to agree with
        // abi_param_count, which is asked the same question.
        if (agg_is_per_piece(&c)) {
            print_type(c.piece[i].ty);
            return;
        }
        Type *pshape = agg_param_shape_type(ty, &c);
        if (pshape) {
            print_type(pshape);
            return;
        }
        print_type(ty);
        return;
    }
    print_type(ty);
    // A scalar parameter carries its mark after the type: i8 signext %0. In
    // a call's type list it has none -- there it is an error -- and it is
    // the operand that carries it.
    if (bare) return;
    char *ext = ext_attr(ty);
    if (ext) fprintf(out_file, " %s", ext);
}

void dump_fn(Sym *fn) {
    if (already_emitted(fn)) return;
    if (fn->is_dead) return;
    dump_curf = fn;
    // An inline definition (6.7.5p8) is not an external definition, so the
    // module declares the function and leaves the definition to whichever
    // translation unit holds the external one.
    bool defined = fn->is_defined && !fn->is_inline_def;
    if (!defined) {
        fprintf(out_file, "declare ");
    } else {
        fprintf(out_file, "define ");
        if (fn->sclass & SC_STATIC)
            fprintf(out_file, "internal ");
        else if (!opt_fpic)
            fprintf(out_file, "dso_local ");
    }

    print_ret_type(fn->ty->ret);
    fprintf(out_file, " @");
    print_sym_name(fn->id);
    fprintf(out_file, "(");

    uint32_t pi = 0;
    if (fn->abi_sret) {
        fprintf(out_file, "ptr noalias sret(");
        print_type(fn->ty->ret);
        fprintf(out_file, ") align %d", fn->ty->ret->align);
        if (defined) fprintf(out_file, " %%tmp%d", pi);
        pi++;
    }
    for (Type *param = fn->ty->params; param; param = param->next) {
        int cntt = abi_param_count(param);
        for (int k = 0; k < cntt; k++) {
            if (pi) fprintf(out_file, ", ");
            print_param_type(param, k, false);
            if (defined) fprintf(out_file, " %%tmp%d", pi);
            pi++;
        }
    }
    // The separator belongs between the last parameter and the ellipsis,
    // so a definition with no named parameter (a bare "...") has none.
    if (fn->ty->is_variadic) fprintf(out_file, "%s...", pi ? ", " : "");
    fprintf(out_file, ")");
    if (!defined) {
        fprintf(out_file, "\n\n");
        return;
    }
    if (T.llvm_features) fprintf(out_file, " #0");
    fprintf(out_file, " {\n");
    Blk *curb = fn->start;
    while (curb) {
        dump_blk(curb);
        curb = curb->next;
        if (curb) fprintf(out_file, "\n");
    }
    fprintf(out_file, "}\n\n");
}

// The name a symbol is emitted under: the asm label when it has one,
// otherwise the C identifier.


// The same name as the interned id that stands for it: the symbol's own id, or
// the asm name interned once. The two lists below compare these rather than the
// strings -- doing it with strcmp cost 12% of the front end's instructions on
// tccgen, the same "compare an id, not a string" that find_ident needed.
static uint32_t emitted_id(Sym *sym) {
    if (sym->asm_name) return intern(sym->asm_name, strlen(sym->asm_name));
    return sym->id;
}

// Set of names already emitted in this module.
//
// Two different C identifiers can denote one object-file symbol: glibc's
// <stdlib.h> redirects both strtoq and strtoll to the same asm name
// __isoc23_strtoll. Emitting one declaration per C identifier would
// declare the same symbol twice, which LLVM rejects with "invalid
// redefinition of function". Deduplicate on the emitted name; the first
// declaration for a name wins, which is what an alias means.
// Whether this symbol is what the module defines under that name, as opposed
// to a bare declaration. Functions answer with their body, the same way the
// rest of the compiler does.
static bool is_definition(Sym *sym) { return sym->is_function ? sym->body != NULL : !(sym->sclass & SC_EXTERN); }

// The names this module defines, collected before anything is emitted. One
// name can carry two symbols -- a block-scope `extern int a;` declaration and
// the file-scope `int a;` definition -- and the declaration may come first in
// the list. A global may be declared or defined in a module, not both, so a
// declaration of a name this module defines is not printed at all: letting it
// stand in for the definition left the unit defining nothing and the link
// failed with `undefined reference to 'a'` (32 tests).
// Membership sets over interned ids. The two lists they replace -- the names
// this module defines, and the ones it has emitted -- were scanned from end to
// end for every symbol, which is quadratic and was 8% of the front end on
// tccgen. Open addressing, because the ids already carry the interning table's
// hash in their low bits, and a `used` flag rather than a zero test, because an
// interned id can be zero (bucket zero, first string in it).
typedef struct {
    uint32_t id;
    bool used;
} IdSet;

typedef struct {
    IdSet *slots;
    size_t cap;
    size_t len;
} IdSetRef;

static void idset_reset(IdSetRef *s) {
    s->cap = 64;
    s->len = 0;
    s->slots = vnew(s->cap, sizeof(IdSet));  // vnew zeroes: used is false
}

static bool idset_has(IdSetRef *s, uint32_t id) {
    for (size_t h = id & (s->cap - 1); s->slots[h].used; h = (h + 1) & (s->cap - 1))
        if (s->slots[h].id == id) return true;
    return false;
}

static void idset_add(IdSetRef *s, uint32_t id) {
    if (idset_has(s, id)) return;
    if (s->len * 2 >= s->cap) {
        IdSet *old = s->slots;
        size_t old_cap = s->cap;
        s->cap *= 2;
        s->slots = vnew(s->cap, sizeof(IdSet));
        s->len = 0;
        for (size_t i = 0; i < old_cap; i++)
            if (old[i].used) idset_add(s, old[i].id);
    }
    size_t h = id & (s->cap - 1);
    while (s->slots[h].used) h = (h + 1) & (s->cap - 1);
    s->slots[h].id = id;
    s->slots[h].used = true;
    s->len++;
}

static IdSetRef defined_set;
static IdSetRef emitted_set;

static bool name_is_defined(uint32_t id) {
    return idset_has(&defined_set, id);
}

static void record_defined_name(uint32_t id) {
    if (name_is_defined(id)) return;
    idset_add(&defined_set, id);
}

static bool already_emitted(Sym *sym) {
    uint32_t id = emitted_id(sym);
    if (!is_definition(sym) && name_is_defined(id)) return true;
    if (idset_has(&emitted_set, id)) return true;
    idset_add(&emitted_set, id);
    return false;
}

void dump_module(Module *md, FILE *out) {
    // Shape types are built per module: a cache keyed by shape must not
    // outlive the module it was built for.
    if (T.classify_publish) T.classify_publish();
    out_file = out;
    curm = md;
    idset_reset(&emitted_set);
    SrcFile **files = get_input_files();
    fprintf(out_file, "; ModuleID = '%s'\n", files[0]->name);
    fprintf(out_file, "source_filename = \"%s\"\n", files[0]->name);
    fprintf(out_file, "target datalayout = \"%s\"\n", T.datalayout);
    fprintf(out_file, "target triple = \"%s\"\n\n", T.triple);
    // File-scope asm statements: LLVM carries them as module-level assembly,
    // which is emitted before the globals and in source order, the way clang
    // emits it. There are no operands to bind them -- outside a function there
    // is no register allocation -- so each is one directive.
    for (int i = 0; i < md->num_masm; i++) {
        fprintf(out_file, "module asm \"");
        for (char *p = md->masm[i]; *p; p++) fprintf(out_file, "%s", escape_char_to_string(*p));
        fprintf(out_file, "\"\n");
    }
    if (md->num_masm) fprintf(out_file, "\n");
    if (T.llvm_features) fprintf(out_file, "attributes #0 = { \"target-features\"=%s }\n\n", T.llvm_features);
    if (T.llvm_abi)
        fprintf(out_file, "!llvm.module.flags = !{!0}\n!0 = !{i32 1, !\"target-abi\", !%s}\n\n", T.llvm_abi);
    fprintf(out_file, "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)\n");
    fprintf(out_file, "declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)\n");
    fprintf(out_file, "declare ptr @llvm.threadlocal.address.p0(ptr)\n");
    // llvm.bswap.iN is not declared here: it is an overloaded intrinsic,
    // so relying on "declared on first use" lets LLVM pick the overload
    // from the call itself.
    if (curm->has_vla) {
        fprintf(out_file, "declare ptr @llvm.stacksave.p0()\n");
        fprintf(out_file, "declare void @llvm.stackrestore.p0(ptr)\n");
    }
    fprintf(out_file, "\n");

    for (Type *ty = md->tys; ty; ty = ty->next) dump_type(ty);
    if (md->tys) fprintf(out_file, "\n");

    idset_reset(&defined_set);
    for (Sym *var = md->data; var; var = var->next)
        if (is_definition(var)) record_defined_name(emitted_id(var));
    for (Sym *fn = md->fns; fn; fn = fn->next)
        if (is_definition(fn)) record_defined_name(emitted_id(fn));

    for (Sym *var = md->data; var; var = var->next) dump_data(var);
    if (md->data) fprintf(out_file, "\n");

    for (Sym *fn = md->fns; fn; fn = fn->next) dump_fn(fn);

    // `__attribute__((constructor))` and `((destructor))`: the pointers the
    // platform runs at startup and at shutdown, which LLVM carries as
    // appending arrays of { priority, function, data }. A lower priority runs
    // first, so the entries are sorted; the default is 65535.
    dump_init_array("llvm.global_ctors", true);
    dump_init_array("llvm.global_dtors", false);
    oflush();
}
