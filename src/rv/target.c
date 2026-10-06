#include "cxx.h"

// RISC-V passes an aggregate of at most two XLEN words in registers and any
// larger one in memory. Which of the two register shapes it takes was read
// off clang, for lp64d, ilp32d and ilp32:
//
//   * a struct of at most two leaves, at least one of them float or double,
//     travels as those leaves. Each keeps its own type, and so reaches its own
//     register file: struct { int; double } is an i32 and a double, and
//     struct { float; float } is two floats. A third leaf does not fit and
//     drops the struct back to the integer coercion below -- struct { int;
//     int; float } is twelve bytes that still travel as [2 x i64].
//
//   * every other aggregate -- no floating point, too many leaves, or a union
//     -- is coerced to integers: one integer as wide as the aggregate when it
//     is no wider than XLEN or is as aligned as it is big, otherwise two XLEN
//     integers, and memory beyond 2*XLEN. The width is the aggregate's own,
//     with no rounding up, which is why a three-byte struct is an i24.
//
// The alignment clause is not decoration. struct { long long } and
// struct { char[8] } are both eight bytes on rv32, and clang coerces the
// first -- whose alignment equals its size, so it behaves like one scalar --
// to a single i64 while splitting the second into [2 x i32].
//
// A variadic call uses the integer coercion for everything. The callee has no
// prototype to read the argument against, so the base ABI applies and nothing
// travels in the floating-point registers: struct { float; float } reaches a
// va_arg as one i64, not as two floats.

// The leaves of a composite, in memory order, up to a bound. Only the first
// two are kept: past that the answer is the integer coercion either way, so
// the rest only have to be counted.
#define RV_MAX_LEAVES 2

typedef struct {
    int n;        // leaves in total, whether kept or not
    bool fp;      // at least one leaf is float or double
    bool opaque;  // holds a union, whose members overlap, or a type no
                  // register holds on its own; either forces the coercion
    struct {
        int off;
        Type *ty;
    } leaf[RV_MAX_LEAVES];
} RvLeaves;

static void rv_collect(Type *ty, int off, RvLeaves *acc) {
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        // A union's members all start at the same offset, so there is no
        // sequence of leaves to hand to the registers; the ABI coerces it
        // whole, as an integer of its size.
        if (ty->kind == TY_UNION) {
            acc->opaque = true;
            return;
        }
        for (Member *m = ty->members; m; m = m->next) rv_collect(m->ty, off + m->offset, acc);
        return;
    }
    if (ty->kind == TY_ARRAY) {
        if (ty->len <= 0) {
            acc->opaque = true;
            return;
        }
        for (int i = 0; i < ty->len; i++) rv_collect(ty->base, off + i * ty->base->size, acc);
        return;
    }
    if (acc->n < RV_MAX_LEAVES) {
        acc->leaf[acc->n].off = off;
        acc->leaf[acc->n].ty = ty;
    }
    acc->n++;
    if (ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE) acc->fp = true;
}

// An integer type `bytes` wide that prints as iN. The widths are not all
// powers of two -- a three-byte struct coerces to i24 -- so this starts from
// the _BitInt table, which already carries every width, and then corrects the
// two fields the coercion does not share with _BitInt: _BitInt(24) is stored
// in four bytes and so claims four-byte alignment, while the aggregate it
// stands for may promise only one.
static Type *rv_int_type(int bytes, int align) {
    Type *ty = copy_type(bitint[bytes * 8][0]);
    ty->size = bytes;
    ty->align = align;
    return ty;
}

// `int_only` drops the floating-point leaf rule, which is what a variadic
// call needs.
static void rv_classify(Type *agg, AggClass *out, bool int_only) {
    out->npiece = 0;
    out->size = agg ? agg->size : 0;
    out->shape_array = false;
    out->is_hfa = false;
    if (!agg || (agg->kind != TY_STRUCT && agg->kind != TY_UNION)) return;
    int xlen = T.ty_long->size;
    int sz = agg->size;
    if (sz <= 0) return;

    RvLeaves lv = {0};
    rv_collect(agg, 0, &lv);

    if (!int_only && agg->kind == TY_STRUCT && !lv.opaque && lv.n > 0 && lv.n <= RV_MAX_LEAVES && lv.fp) {
        bool ok = true;
        for (int i = 0; i < lv.n; i++) {
            Type *lt = lv.leaf[i].ty;
            bool fp = lt->kind == TY_FLOAT || lt->kind == TY_DOUBLE;
            // A soft-float ABI has no FP registers to send a float to, and an
            // integer leaf has to fit the one register it would take.
            if (fp && T.agg_no_fp) ok = false;
            if (!fp && lt->size > xlen) ok = false;
            if (!ok) break;
        }
        if (ok) {
            for (int i = 0; i < lv.n; i++) {
                out->piece[i].off = lv.leaf[i].off;
                out->piece[i].size = lv.leaf[i].ty->size;
                out->piece[i].ty = lv.leaf[i].ty;
            }
            out->npiece = lv.n;
            return;
        }
    }

    if (sz > 2 * xlen) return;

    if (sz <= xlen || agg->align >= sz) {
        out->piece[0].off = 0;
        out->piece[0].size = sz;
        out->piece[0].ty = rv_int_type(sz, agg->align);
        out->npiece = 1;
        return;
    }

    // Split at the XLEN boundary. Alignment is what the object actually
    // promises: a packed struct that reaches into the second word is still
    // only byte-aligned, and the pieces are read out of it as such.
    int align = MIN(xlen, agg->align);
    out->piece[0].off = 0;
    out->piece[0].size = xlen;
    out->piece[0].ty = rv_int_type(xlen, align);
    out->piece[1].off = xlen;
    out->piece[1].size = sz - xlen;
    out->piece[1].ty = rv_int_type(xlen, align);
    out->npiece = 2;
    out->shape_array = true;
}

void rv_classify_aggregate(Type *agg, AggClass *out) { rv_classify(agg, out, false); }

void rv_classify_variadic(Type *agg, AggClass *out) { rv_classify(agg, out, true); }

Type *rv_pieces_type(Type *agg) {
    if (!agg || (agg->kind != TY_STRUCT && agg->kind != TY_UNION)) return NULL;
    AggClass c;
    rv_classify_aggregate(agg, &c);
    return agg_shape_type(&c);
}

int rv_param_slots(Type *ty) {
    if (!ty || (ty->kind != TY_STRUCT && ty->kind != TY_UNION)) return 1;
    AggClass c;
    rv_classify_aggregate(ty, &c);
    return agg_param_slots(ty, &c);
}
