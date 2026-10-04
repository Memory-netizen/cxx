#include "cxx.h"

static Module *curm;
static Sym *curf;
static Blk *curb;
static Blk dummy;
static Blk *tail;
static Blk *unreach = &(Blk){};
static int tmp_id;
static Blk *brk_blk;
static Blk *cont_blk;
static int atomic_order;  // Memory order of the next atomic load/store

// node->mem_order is stored +1 so that 0 can mean "unspecified" for
// nodes that never went through the atomic builtins (seq_cst default).
static int node_mem_order(Node *node) { return node->mem_order ? node->mem_order - 1 : MEM_ORDER_SEQ_CST; }

static int node_mem_order1(Node *node) { return node->mem_order1 ? node->mem_order1 - 1 : MEM_ORDER_SEQ_CST; }

static bool is_atomic_ptr(Ref addr) {
    return addr.ty && is_pointer(addr.ty) && addr.ty->base && (addr.ty->base->qual & Q_ATOMIC);
}

static Ref gen_stmt(Node *node);
static Ref gen_expr(Node *node);
static Ref gen_cond(Node *node);
static Ref gen_logand(Node *node);
static Ref gen_logor(Node *node);

static Ir *new_ins(IrKind op, Ref dst, Ref *args, uint32_t narg) {
    Ir *new = emalloc(sizeof(Ir) + narg * sizeof(Ref));
    new->op = op;
    new->dst = dst;
    new->narg = narg;
    new->mem_order = MEM_ORDER_SEQ_CST;
    new->mem_order1 = MEM_ORDER_SEQ_CST;
    new->is_weak = 0;
    new->is_signal = 0;
    if (narg > 0 && args) memcpy(new->args, args, narg * sizeof(Ref));

    new->prev = curb->tail;
    new->next = NULL;
    if (curb->head)
        curb->tail = curb->tail->next = new;
    else
        curb->head = curb->tail = new;

    return new;
}

// All blocks of the current function live in one array (curf->blks,
// counted by the parser); label blocks occupy [0, num_lbl), everything
// else takes the remaining slots in generation order.
static int blk_used;

static Blk *new_blk(void) {
    // The parser's count is an upper bound; exceeding it means a
    // construct was added to irgen without updating the parser.
    assert(blk_used < curf->num_blk);
    Blk *b = &curf->blks[blk_used++];
    b->pred = vnew(2, sizeof(Blk *));
    b->blk_no = blk_used - 1;
    return b;
}

static void insert_blk(Blk *b) {
    b->blk_id = tmp_id++;
    tail = tail->next = b;
}

static void add_pred(Blk *bp, Blk *b) {
    if (!b || bp == curf->end || bp == unreach) {
        return;
    }
    for (uint32_t i = 0; i < b->num_pred; i++) {
        if (b->pred[i] == bp) return;
    }
    b->pred = vgrow(b->pred, b->num_pred + 1);
    b->pred[b->num_pred++] = bp;
}

static Phi *new_phi(Ref res) {
    Phi *new = emalloc(sizeof(Phi));
    new->result = res;
    new->arg = vnew(0, sizeof(Ref));
    new->blk = vnew(0, sizeof(Blk *));
    new->num_arg = 0;
    new->next = NULL;
    return new;
}

static void add_phi_arg(Phi *phi, Blk *blk, Ref arg) {
    phi->num_arg++;
    phi->arg = vgrow(phi->arg, phi->num_arg);
    phi->blk = vgrow(phi->blk, phi->num_arg);
    phi->arg[phi->num_arg - 1] = arg;
    phi->blk[phi->num_arg - 1] = blk;
}

static void insert_phi(Blk *blk, Phi *phi) {
    phi->next = blk->phi;
    blk->phi = phi;
}

// Reinterpret the bits of val as `to` (same-size first-class types).
static Ref bitcast(Ref val, Type *to) {
    Ref dst = TMP(tmp_id++, to);
    new_ins(IR_BITCAST, dst, (Ref[]){val}, 1);
    return dst;
}

// True for the type kinds that the IR lowers to `ptr`. nullptr_t is a
// distinct scalar type (C23 6.2.5: different from all pointer and
// arithmetic types, with the single value nullptr), not a pointer type
// -- so this must NOT be used for anything semantic. It exists only to
// pick the right null constant when comparing an IR `ptr` value.
static bool is_ir_pointer(Type *ty) { return is_pointer(ty) || ty->kind == TY_NULLPTR; }

static Ref cast(Ref val, Type *src_ty, Type *target_ty) {
    if (target_ty->kind == TY_BOOL) {
        Ref tmp = TMP(tmp_id++, bitint[1][1]);
        Ref zr = INT(0);
        // The null constant must match the operand's IR type: comparing
        // an IR `ptr` (pointer or nullptr_t) against an integer 0 is
        // invalid IR. nullptr_t is not a pointer type, but it is
        // lowered to `ptr`, so it takes the same null constant.
        if (is_ir_pointer(src_ty))
            zr = NULLPTR;
        else
            zr.ty = src_ty;
        new_ins(IR_CMP_NE, tmp, (Ref[]){val, zr}, 2);

        Ref dst = TMP(tmp_id++, target_ty);
        new_ins(IR_EXT, dst, (Ref[]){tmp}, 1);
        return dst;
    }
    // 6.3.2.4: only a null pointer constant or a nullptr_t may be
    // converted to nullptr_t, so the result is always the null pointer
    // value. Emit it directly -- there is nothing to convert at run
    // time. Falling through to the by-width case below would instead
    // emit an invalid `sext i32 0 to ptr`, because nullptr_t is not
    // TY_PTR and is_pointer() is therefore false for it.
    if (target_ty->kind == TY_NULLPTR) {
        if (src_ty->kind == TY_NULLPTR) return val;  // no-op
        Ref n = NULLPTR;
        n.ty = target_ty;
        return n;
    }
    if (is_pointer(src_ty) && is_integer(target_ty)) {
        Ref dst = TMP(tmp_id++, target_ty);
        new_ins(IR_PTRTOINT, dst, (Ref[]){val}, 1);
        return dst;
    }
    if (is_integer(src_ty) && is_pointer(target_ty)) {
        Ref dst = TMP(tmp_id++, target_ty);
        new_ins(IR_INTTOPTR, dst, (Ref[]){val}, 1);
        return dst;
    }
    if (is_flonum(src_ty) && is_integer(target_ty)) {
        Ref dst = TMP(tmp_id++, target_ty);
        new_ins(IR_FPTOINT, dst, (Ref[]){val}, 1);
        return dst;
    }
    if (is_integer(src_ty) && is_flonum(target_ty)) {
        Ref dst = TMP(tmp_id++, target_ty);
        new_ins(IR_INTTOFP, dst, (Ref[]){val}, 1);
        return dst;
    }

    if (target_ty->kind == TY_VOID) return val;
    if (src_ty->kind == TY_VLA && is_pointer(target_ty)) {
        val.ty = target_ty;
        return val;
    }
    if (target_ty->size == src_ty->size) {
        if (is_flonum(src_ty) && is_flonum(target_ty)) {
            int rs = float_rank(src_ty), rt = float_rank(target_ty);
            if (rs != rt) {
                // Same storage size but different IR types, e.g.
                // x86_fp80 <-> fp128 on amd64.
                Ref dst = TMP(tmp_id++, target_ty);
                new_ins(rt > rs ? IR_EXT : IR_TRUNC, dst, (Ref[]){val}, 1);
                return dst;
            }
        }
        // _BitInt types of different widths share the same byte size but
        // are different IR types (i3 vs i4, ...): convert by bit width.
        int ws = (src_ty->kind & TY_BITINT) ? bitint_width(src_ty) : src_ty->size * 8;
        int wt = (target_ty->kind & TY_BITINT) ? bitint_width(target_ty) : target_ty->size * 8;
        if (ws != wt) {
            Ref dst = TMP(tmp_id++, target_ty);
            new_ins(wt > ws ? IR_EXT : IR_TRUNC, dst, (Ref[]){val}, 1);
            return dst;
        }
        val.ty = target_ty;
        return val;
    }
    Ref dst = TMP(tmp_id++, target_ty);
    if (target_ty->size > src_ty->size)
        new_ins(IR_EXT, dst, (Ref[]){val}, 1);
    else
        new_ins(IR_TRUNC, dst, (Ref[]){val}, 1);

    return dst;
}

static Ref convert(Node *lhs, Type *target_ty) {
    Ref lr = gen_expr(lhs);
    if (lhs->ty->kind == TY_FUNC) return lr;
    if (lhs->ty->kind == TY_ARRAY) {
        Ref dst = TMP(tmp_id++, target_ty);
        new_ins(IR_GEP, dst, (Ref[]){lr, LONG(0)}, 2);
        return dst;
    }
    return cast(lr, lhs->ty, target_ty);
}

static Ref gen_addr(Node *node) {
    switch (node->kind) {
        case ND_VAR:
            gen_expr(node->var_init);
            if (node->var->is_local) {
                return SLOT(node->var->vreg, pointer_to(node->ty, 0));
            } else if (node->var->sclass & SC_THREAD) {
                Ref dst = TMP(tmp_id++, pointer_to(node->ty, 0));
                Ref ops[] = {GLB(node->var->id, pointer_to(node->ty, 0))};
                new_ins(IR_TLSADDR, dst, ops, 1);
                return dst;
            } else {  // Global variable
                return GLB(node->var->id, pointer_to(node->ty, 0));
            }
        case ND_DEREF:
            return gen_expr(node->lhs);
        case ND_SUBACCESS: {
            Ref addr = gen_addr(node->lhs);
            addr.ty = pointer_to(node->ty, 0);
            Ref idx = gen_expr(node->rhs);
            Ref gep_ops[] = {addr, idx};
            Ref dst = TMP(tmp_id++, pointer_to(node->ty, 0));
            new_ins(IR_GEP, dst, gep_ops, 2);
            return dst;
        }
        case ND_MEMBER: {
            Ref addr = gen_expr(node->lhs);
            if (node->lhs->ty->kind == TY_UNION) {
                addr.ty = pointer_to(node->member->ty, 0);
                return addr;
            }

            // Byte-offset addressing: the member may overlap another
            // member's access unit (bit-fields), so the LLVM element
            // layout cannot express its address.
            Ref a8 = addr;
            a8.ty = pointer_to(T.ty_char, 0);
            Ref gep_ops[] = {a8, INT(node->member->offset)};
            Ref dst = TMP(tmp_id++, pointer_to(node->ty, 0));
            new_ins(IR_GEP, dst, gep_ops, 2);
            return dst;
        }
        default:
            break;
    }
    error(node->tok, "not a lvalue");
    return R;
}

static Ref load(Ref addr, Type *type, int align, Member *mem) {
    if (mem && mem->is_bitfield) {
        Type *ty = mem->unit_ty;
        Ref dst = TMP(tmp_id++, ty);
        new_ins(IR_LORD, dst, (Ref[]){addr, INT(align)}, 2);
        Ref shl = TMP(tmp_id++, ty);
        new_ins(IR_SHL, shl, (Ref[]){dst, INT(ty->size * 8 - mem->bit_width - mem->bit_offset)}, 2);
        Ref shr = TMP(tmp_id++, ty);
        new_ins(IR_SHR, shr, (Ref[]){shl, INT(ty->size * 8 - mem->bit_width)}, 2);
        return cast(shr, ty, type);
    } else if (type->kind == TY_VLA) {
        return addr;
    } else {
        Ref dst = TMP(tmp_id++, type);
        Ir *ins = new_ins(IR_LORD, dst, (Ref[]){addr, INT(align)}, 2);
        if (is_atomic_ptr(addr)) ins->mem_order = atomic_order;
        return dst;
    }
}

static void store(Ref val, Ref addr, int align, Member *mem) {
    if (mem && mem->is_bitfield) {
        Type *ty = mem->unit_ty;
        // a. load entire memmory unit
        Ref old = load(addr, ty, align, NULL);

        int width = mem->bit_width;
        int boff = mem->bit_offset;
        int total_bits = ty->size * 8;

        // b. clear mask
        int64_t mask = ((1ULL << width) - 1) << boff;
        uint64_t clear_mask_val = ~mask;
        clear_mask_val &= (1ULL << total_bits) - 1;
        Ref clear_mask = INT(clear_mask_val);

        // c. clear old value
        Ref old_cleared = TMP(tmp_id++, ty);
        new_ins(IR_AND, old_cleared, (Ref[]){old, clear_mask}, 2);

        // d. trunc new vlaue
        Ref trunc = cast(val, val.ty, ty);

        // e. shiht new value
        Ref dst_shifted;
        if (boff > 0) {
            dst_shifted = TMP(tmp_id++, ty);
            new_ins(IR_SHL, dst_shifted, (Ref[]){trunc, INT(boff)}, 2);
        } else {
            dst_shifted = trunc;
        }

        // f. merge value
        Ref new_val = TMP(tmp_id++, ty);
        new_ins(IR_OR, new_val, (Ref[]){old_cleared, dst_shifted}, 2);

        // g. store
        new_ins(IR_STR, R, (Ref[]){new_val, addr, INT(align)}, 3);
    } else {
        Ir *ins = new_ins(IR_STR, R, (Ref[]){val, addr, INT(align)}, 3);
        if (is_atomic_ptr(addr)) ins->mem_order = atomic_order;
    }
}

// Effective alignment of an lvalue: a variable's declared alignment,
// a packed member's (or packed record's) 1, otherwise the type alignment.
static int lvalue_align(Node *node) {
    if (node->kind == ND_VAR) return node->var->align;
    if (node->kind == ND_MEMBER) {
        if (node->member->is_packed) return 1;
        for (Node *n = node; n->kind == ND_MEMBER; n = n->lhs)
            if (n->lhs->ty->is_packed) return 1;
        int align = node->member->align;
        // A bit-field may start at a byte offset not aligned to its
        // declared type (the unit is shared with other fields).
        if (node->member->is_bitfield && node->member->offset % align) return 1;
        return align;
    }
    return node->ty->align;
}

// A call to a builtin -> a direct IR_CALL to the matching LLVM intrinsic,
// built the same way a normal call builds its callee and argument list.
//
// The name goes through a reserved identifier rather than a Sym: intrinsic
// names are not C identifiers and no declaration may be emitted for them.
// Every intrinsic name starts with "llvm.", which a C identifier cannot
// contain, so it can never collide with a user function.
//
// The symbol is deliberately absent from the module's function list, so
// LLVM auto-declares it on first use and derives the overload from the
// call site -- which is what keeps the signature right.
//
// (opt_ast.c folds a constant argument before irgen sees it, so this only
// runs for a runtime value.)
// Emit a call to an LLVM intrinsic: one argument, one result, the width
// taken from the operand. This is the shared emission for every builtin
// whose table row names an intrinsic, so adding one of those needs no new
// code here.
//
// The name goes through a reserved identifier rather than a Sym: intrinsic
// names are not C identifiers and no declaration may be emitted for them.
// Every intrinsic name starts with "llvm.", which a C identifier cannot
// contain, so it can never collide with a user function. The symbol stays
// out of the module's function list, so LLVM auto-declares it on first use
// and derives the overload from the call site.
static Ref gen_intrinsic_call(Node *node, BuiltinDef *d, int kind) {
    // The intrinsic's width is the builtin's *parameter* type, not the
    // argument's: a narrow argument arrives already converted to that
    // parameter type, and the clz family's parameter is wider than its
    // result, so the intrinsic computes wide and truncates below.
    Type *param_ty = builtin_type(kind)->params;
    uint32_t width = param_ty->size * 8;
    Ref val = gen_expr(node->args);
    char *name = format(d->intrinsic, width);
    uint32_t id = intern(name, strlen(name));
    register_asm_name(id, name);

    // The result comes back at the parameter's width; a narrower declared
    // result (int, for clz and friends) is obtained by truncating.
    bool truncate = node->ty->size < param_ty->size;
    Type *call_ty = truncate ? param_ty : node->ty;
    Ref dst = TMP(tmp_id++, call_ty);
    // The callee's ty must be the *function* type, not a pointer to it:
    // the IR_CALL printer reads ir->args[0].ty->is_variadic directly.
    Ref fn = GLB(id, func_type(call_ty));

    // clz/ctz take a second, immediate argument (is_zero_undef). It belongs
    // to the builtin rather than to the call site, so the table carries it.
    // The immediate is i1, which ty_str prints for bool in memory (i8), so
    // it uses the canonical 1-bit type instead.
    Ref ops[3] = {fn, val, R};
    uint32_t n = 2;
    if (d->extra_arg >= 0) {
        ops[2].type = RInt;
        ops[2].val = d->extra_arg;
        ops[2].ty = bitint[1][1];
        n = 3;
    }
    new_ins(IR_CALL, dst, ops, n);

    if (!truncate) return dst;
    Ref out = TMP(tmp_id++, node->ty);
    new_ins(IR_TRUNC, out, (Ref[]){dst}, 1);
    return out;
}

// Address of a field of the va_list record. The expansion only moves
// within the object, so clang's byte-offset addressing is what the ABI
// expects.
static Ref va_field_addr(Ref ap, Type *rec, int idx) {
    int off = 0, i = 0;
    for (Member *m = rec->members; m; m = m->next, i++)
        if (i == idx) {
            off = m->offset;
            break;
        }
    Ref base = ap;
    base.ty = pointer_to(T.ty_char, 0);
    Ref dst = TMP(tmp_id++, base.ty);
    new_ins(IR_GEP, dst, (Ref[]){base, INT(off)}, 2);
    return dst;
}

// __builtin_va_arg. The shape -- test, two candidate addresses, join -- is
// the same for every ABI; what the target supplies through T.va_arg_ops is
// which field holds the cursor, how much room is left, and how far the
// cursor moves.
static Ref gen_va_arg(Node *node) {
    VaArgOps *ops = T.va_arg_ops(node->ty);
    Type *addr_ty = node->lhs->ty;  // pointer to the va_list object
    Ref ap = gen_expr(node->lhs);
    Type *want = node->ty;

    if (ops->kind == VA_MEM_LINEAR) {
        // The va_list is one pointer walking the argument area: read it,
        // align it for this type, take the value, then advance.
        Type *aptr_ty = is_ir_pointer(ap.ty) && ap.ty->base ? ap.ty->base : T.ty_voidptr;
        Ref cursor = load(ap, aptr_ty, aptr_ty->align, NULL);

        int step = ops->mem_step;
        int align = want->align > step ? want->align : step;
        Ref aligned = cursor;
        if (align > step) {
            // Types wider than a slot keep their natural alignment, which
            // cxx models with a round-down mask.
            Ref c8 = cursor;
            c8.ty = pointer_to(T.ty_char, 0);
            Ref bits = TMP(tmp_id++, T.ty_ulong);
            new_ins(IR_PTRTOINT, bits, (Ref[]){c8}, 1);
            Ref mask = TMP(tmp_id++, T.ty_ulong);
            new_ins(IR_AND, mask, (Ref[]){bits, LONG(-align)}, 2);
            Ref back = TMP(tmp_id++, c8.ty);
            new_ins(IR_INTTOPTR, back, (Ref[]){mask}, 1);
            aligned = back;
        }

        int size = want->size;
        int taken = (size + align - 1) / align * align;
        Ref next = aligned;
        Ref n8 = aligned;
        n8.ty = pointer_to(T.ty_char, 0);
        Ref adv = TMP(tmp_id++, n8.ty);
        new_ins(IR_GEP, adv, (Ref[]){n8, INT(taken)}, 2);
        next = adv;
        store(next, ap, aptr_ty->align, NULL);

        int la = want->align;
        if (la > step) la = step;
        return load(aligned, want, la, NULL);
    }

    if (ops->kind != VA_MEM_REGS) fatal("unknown va_arg kind %d", ops->kind);

    Type *rec = addr_ty->base;
    if (rec->kind != TY_STRUCT) fatal("va_arg: va_list is not a struct");

    Blk *blk_reg = new_blk();
    Blk *blk_mem = new_blk();
    Blk *blk_join = new_blk();

    Type *off_ty = ops->offset_ty;
    if (!off_ty) fatal("va_arg: the target gave no offset type");
    Ref off_addr = va_field_addr(ap, rec, ops->offset_field);
    Ref off = load(off_addr, off_ty, off_ty->align, NULL);
    // Temp ids must increase in the order LLVM first sees them, so an
    // instruction may not be numbered below an operand it uses; each branch
    // therefore allocates its result after the values feeding it.
    bool neg = ops->offset_negative;
    Ref cond;
    if (neg) {
        // The offset counts *down* and turns negative once the named
        // registers are used up, so the register save area holds the
        // argument only while the offset is negative and stays negative
        // after the step. LLVM's icmp here is signed, and the two tests
        // cannot be collapsed into one unsigned comparison.
        Ref in_regs = TMP(tmp_id++, bitint[1][1]);
        new_ins(IR_CMP_LT, in_regs, (Ref[]){off, INT(0)}, 2);
        Ref next = TMP(tmp_id++, off_ty);
        new_ins(IR_ADD, next, (Ref[]){off, INT(ops->reg_step)}, 2);
        Ref still = TMP(tmp_id++, bitint[1][1]);
        new_ins(IR_CMP_LE, still, (Ref[]){next, INT(0)}, 2);
        cond = TMP(tmp_id++, bitint[1][1]);
        new_ins(IR_AND, cond, (Ref[]){in_regs, still}, 2);
    } else {
        // The offset counts up from zero, so "still has room" is simply
        // being within the bound that leaves space for one more slot.
        cond = TMP(tmp_id++, bitint[1][1]);
        new_ins(IR_CMP_LE, cond, (Ref[]){off, INT(ops->offset_bound)}, 2);
    }
    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = blk_reg;
    curb->succ2 = blk_mem;
    add_pred(curb, blk_reg);
    add_pred(curb, blk_mem);

    // Register save area: the argument sits at the running offset, which
    // then advances by one step.
    curb = blk_reg;
    insert_blk(curb);
    Ref reg = load(va_field_addr(ap, rec, ops->reg_field), T.ty_voidptr, 8, NULL);
    Ref reg8 = reg;
    reg8.ty = pointer_to(T.ty_char, 0);
    Ref off64 = cast(off, off_ty, T.ty_long);
    Ref addr_reg = TMP(tmp_id++, reg8.ty);
    new_ins(IR_GEP, addr_reg, (Ref[]){reg8, off64}, 2);
    // The cursor advances one slot. The condition already computed this
    // value for the descending form, but it is scoped to that branch, so it
    // is recomputed here; the duplicate folds away and keeps the numbering
    // inside this block trivially increasing.
    Ref next_reg = TMP(tmp_id++, off_ty);
    new_ins(IR_ADD, next_reg, (Ref[]){off, INT(ops->reg_step)}, 2);
    store(next_reg, off_addr, off_ty->align, NULL);
    // The address above uses the offset as the ABI defines it: for the
    // descending form that is the position of this argument, and the
    // stored value is where the next one starts.
    curb->jmp.type = IR_JMP;
    curb->succ1 = blk_join;
    add_pred(curb, blk_join);

    // Overflow area: read there and advance that cursor instead.
    curb = blk_mem;
    insert_blk(curb);
    Ref over_addr = va_field_addr(ap, rec, ops->mem_field);
    Ref over = load(over_addr, T.ty_voidptr, 8, NULL);
    Ref addr_mem = over;
    if (ops->mem_align > 8) {
        // A type more aligned than the stack's slot granularity: raise the
        // cursor to that alignment before reading, as clang does.
        Ref bits = TMP(tmp_id++, T.ty_ulong);
        new_ins(IR_PTRTOINT, bits, (Ref[]){over}, 1);
        Ref mask = TMP(tmp_id++, T.ty_ulong);
        new_ins(IR_AND, mask, (Ref[]){bits, LONG(-ops->mem_align)}, 2);
        Ref up = TMP(tmp_id++, T.ty_voidptr);
        new_ins(IR_INTTOPTR, up, (Ref[]){mask}, 1);
        addr_mem = up;
    }
    Ref over8 = addr_mem;
    over8.ty = pointer_to(T.ty_char, 0);
    Ref next_mem = TMP(tmp_id++, over8.ty);
    new_ins(IR_GEP, next_mem, (Ref[]){over8, INT(ops->mem_step)}, 2);
    next_mem.ty = T.ty_voidptr;
    store(next_mem, over_addr, 8, NULL);
    curb->jmp.type = IR_JMP;
    curb->succ1 = blk_join;
    add_pred(curb, blk_join);

    // Join: both candidates are plain addresses by now.
    curb = blk_join;
    insert_blk(curb);
    Ref addr = TMP(tmp_id++, T.ty_voidptr);
    Phi *phi = new_phi(addr);
    add_phi_arg(phi, blk_reg, addr_reg);
    add_phi_arg(phi, blk_mem, addr_mem);
    insert_phi(curb, phi);

    // An aggregate value is represented by its address, so the candidate
    // already *is* the result: loading it would ask LLVM for a by-value
    // first-class struct, which its load instruction cannot produce.
    if (want->kind == TY_STRUCT || want->kind == TY_UNION) {
        addr.ty = pointer_to(want, 0);
        return addr;
    }

    int la = want->align;
    if (la > 8) la = 8;
    return load(addr, want, la, NULL);
}

// Identify the builtin and dispatch. Emission lives in the callee so that
// adding a builtin is a table row plus, at most, one emission routine.
static Ref gen_builtin_call(Node *node, int kind) {
    BuiltinDef *d = builtin_def(kind);
    if (!d) fatal("unknown builtin kind %d in irgen", kind);

    // A builtin described by an intrinsic name needs no per-builtin code.
    if (d->intrinsic) return gen_intrinsic_call(node, d, kind);

    fatal("no IR lowering for builtin ‘%s’", d->name);
    return R;  // unreachable: fatal() exits
}

static Ref gen_expr(Node *node) {
    if (!node) return R;
    Ref dst;
    switch (node->kind) {
        case ND_FENCE: {
            // LLVM has no monotonic fence (clang drops relaxed fences
            // entirely); skip emission for relaxed.
            int order = node_mem_order(node);
            if (order != MEM_ORDER_RELAXED) {
                Ir *ins = new_ins(IR_FENCE, R, NULL, 0);
                ins->mem_order = order;
                ins->is_signal = node->is_signal;
            }
            return R;
        }
        case ND_SP_SAVE:
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_SP_SAVE, dst, NULL, 0);
            return dst;
        case ND_SP_RESTORE:
            dst = gen_expr(node->lhs);
            new_ins(IR_SP_RESTORE, R, (Ref[]){dst}, 1);
            return R;
        // va_start/va_end map onto real LLVM intrinsics: the backend
        // expands them against the target's va_list layout, so no ABI
        // knowledge is needed here. The operand is the address of the
        // va_list object, which is also the pointer the intrinsic takes.
        case ND_VA_ARG:
            return gen_va_arg(node);
        case ND_VA_COPY: {
            // llvm.va_copy is a real intrinsic; the backend expands the
            // copy for the target's va_list layout.
            const char *name = "llvm.va_copy.p0";
            uint32_t id = intern((char *)name, strlen(name));
            register_asm_name(id, (char *)name);

            Type *fty = func_type(T.ty_void);
            fty->params = T.ty_voidptr;
            fty->nparam = 1;
            Ref dst = gen_expr(node->lhs);
            Ref src = gen_expr(node->rhs);
            dst.ty = T.ty_voidptr;
            src.ty = T.ty_voidptr;
            new_ins(IR_CALL, R, (Ref[]){GLB(id, fty), dst, src}, 3);
            return R;
        }
        case ND_VA_START:
        case ND_VA_END: {
            const char *name = node->kind == ND_VA_START ? "llvm.va_start.p0" : "llvm.va_end.p0";
            uint32_t id = intern((char *)name, strlen(name));
            register_asm_name(id, (char *)name);

            Type *fty = func_type(T.ty_void);
            Ref aptr = gen_expr(node->lhs);
            aptr.ty = T.ty_voidptr;
            fty->params = T.ty_voidptr;
            fty->nparam = 1;
            new_ins(IR_CALL, R, (Ref[]){GLB(id, fty), aptr}, 2);
            return R;
        }
        case ND_NOP:
            return R;
        case ND_NULLPTR:
            return NULLPTR;
        case ND_NUM:
            // parse() must hand irgen a fully typed AST: a constant with
            // no type means the front end failed to propagate one, and
            // silently treating it as an int here would mask a parser bug
            // and emit the wrong constant format. Assert instead.
            if (!node->ty) fatal("ND_NUM reached irgen without a type");
            if (is_fpval(node->ty)) {
                Con c = {.type = CBits128};
                switch (node->ty->kind) {
                    case TY_F16:
                        c.bits.i128.limb[0] = fp128_to_fp16_bits(node->fpval);
                        break;
                    case TY_LDOUBLE:
                        if (T.ldouble_is_fp80) {
                            uint64_t m;
                            uint16_t se;
                            fp128_to_fp80_bits(node->fpval, &m, &se);
                            c.bits.i128.limb[0] = (uint32_t)m;
                            c.bits.i128.limb[1] = (uint32_t)(m >> 32);
                            c.bits.i128.limb[2] = se;
                        } else {
                            c.bits.f128 = node->fpval;
                        }
                        break;
                    default: {
                        // F32/F64/F128. The legacy LLVM literal for float
                        // and double is the double bit pattern (16 hex
                        // digits); a binary32 value widened to binary64 is
                        // exact, so F32 can share the F64 path.
                        uint64_t b = fp128_to_fp64_bits(node->fpval);
                        if (node->ty->kind == TY_F128) {
                            c.bits.f128 = node->fpval;
                        } else {
                            c.bits.i128.limb[0] = (uint32_t)b;
                            c.bits.i128.limb[1] = (uint32_t)(b >> 32);
                        }
                        break;
                    }
                }
                dst = newcon(&c, curm);
                dst.ty = node->ty;
                return dst;
            }
            if (is_bitint128(node->ty)) {
                Con c = {.type = CBits128, .bits.i128 = node->ival};
                dst = newcon(&c, curm);
                dst.ty = node->ty;
                return dst;
            }
            if (node->ty->kind == TY_FLOAT) return FLOAT((int64_t)fp128_to_fp64_bits(node->fpval));
            if (node->ty->kind == TY_DOUBLE) return DOUBLE((int64_t)fp128_to_fp64_bits(node->fpval));
            if (node->ty->size == 1)
                dst = BOOL(int128_to_i64(node->ival));
            else if (node->ty->size == 4)
                dst = INT(int128_to_i64(node->ival));
            else
                dst = LONG(int128_to_i64(node->ival));
            dst.ty = node->ty;
            return dst;
        case ND_STMT_EXPR:
            for (Node *n = node->body; n; n = n->next) {
                Ref tmp = gen_stmt(n);
                if (n->kind != ND_SP_RESTORE) dst = tmp;
            }
            return dst;
        case ND_LABEL_VAL:
            dst.type = RLabel;
            dst.ty = node->ty;
            // Labels are function-scoped: the index into fn->blks
            // suffices. Global/static &&label initializers take the
            // Con CAddr path ("fn..label"), not Ref.
            dst.val = node->target->blk_idx;
            return dst;
        case ND_LVTOR: {
            int align = lvalue_align(node->lhs);
            Ref addr = gen_expr(node->lhs);
            atomic_order = node_mem_order(node->lhs);
            if (is_record(node->ty) && is_atomic_ptr(addr)) {
                // Whole access to an _Atomic aggregate reads the bit
                // pattern through an integer of the same size (clang
                // does the same; consumers store it type-punned).
                int sz = node->ty->size;
                if (sz != 1 && sz != 2 && sz != 4 && sz != 8)
                    error(node->tok,
                          "atomic aggregate larger than 8 bytes or of non-power-of-two size is not supported");
                return load(addr, bitint[sz * 8][1], align, NULL);
            }
            return load(addr, node->ty, align, node->lhs->member);
        }
        case ND_VAR:
        case ND_MEMBER:
        case ND_SUBACCESS: {
            Ref addr = gen_addr(node);
            // An lvalue is left as its address; the load happens when the
            // value is needed, which the front end marks with ND_LVTOR.
            // A member of an rvalue aggregate -- f().b, where f returns a
            // record -- is never marked that way, because it is not an
            // lvalue to begin with, so the load has to happen here.
            if (node->is_lvalue || is_array(node->ty)) return addr;
            if (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION) return addr;
            atomic_order = node_mem_order(node);
            return load(addr, node->ty, lvalue_align(node), node->kind == ND_MEMBER ? node->member : NULL);
        }
        case ND_ADDR:
            return gen_addr(node->lhs);
        case ND_DEREF:
            return gen_expr(node->lhs);
        case ND_IMCAST:
        case ND_EXCAST:
            return convert(node->lhs, node->ty);
        case ND_MEMZERO: {
            // node->lhs is the ND_VAR node of the variable being
            // zero-initialized; its type carries the region size.
            Ref addr = gen_expr(node->lhs);
            Ref ops[] = {addr, INT(0), INT(node->lhs->ty->size)};
            new_ins(IR_MEMSET, R, ops, 3);
            return R;
        }
        case ND_INIT:
        case ND_AS: {
            Ref addr = gen_expr(node->lhs);
            int align = lvalue_align(node->lhs);

            if (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION) {
                Ref src_addr = gen_expr(node->rhs);
                // The rhs record conversion may wrap in ND_IMCAST and
                // rewrite the Ref ty (hiding or adding the atomic
                // qualifier): normalize to the true source type.
                Type *src_ty = node->rhs->ty;
                if (node->rhs->kind == ND_IMCAST) src_ty = node->rhs->lhs->ty;
                src_addr.ty = pointer_to(src_ty, 0);
                if (is_atomic_ptr(addr) || is_atomic_ptr(src_addr)) {
                    // Whole access to an _Atomic aggregate: move the bit
                    // pattern through a same-size integer. The load/store
                    // are atomic iff their respective object is atomic.
                    int sz = node->ty->size;
                    if (sz != 1 && sz != 2 && sz != 4 && sz != 8)
                        error(node->tok,
                              "atomic aggregate larger than 8 bytes or of non-power-of-two size is not supported");
                    atomic_order = node_mem_order(node);
                    Ref src = load(src_addr, bitint[sz * 8][1], align, NULL);
                    atomic_order = node_mem_order(node);
                    store(src, addr, align, NULL);
                    return addr;
                }
                Ref ops[] = {addr, src_addr, INT(node->ty->size)};
                new_ins(IR_MEMCPY, R, ops, 3);
                return addr;
            }

            Ref dst = gen_expr(node->rhs);
            if (node->ty->kind == TY_VLA) {
                node->lhs->var->vreg = dst.val;
            } else {
                atomic_order = node_mem_order(node);
                store(dst, addr, align, node->lhs->member);
            }
            return dst;
        }
        case ND_PREINC:
        case ND_PREDEC:
        case ND_POSTINC:
        case ND_POSTDEC: {
            int ir_op = is_pointer(node->ty) ? IR_GEP : IR_ADD;
            Ref addr = gen_expr(node->lhs);
            int align = lvalue_align(node->lhs);
            atomic_order = node_mem_order(node);
            Ref lr = load(addr, node->ty, align, node->lhs->member);
            int addend = (node->kind == ND_PREINC || node->kind == ND_POSTINC) ? 1 : -1;
            union {
                double f64;
                uint64_t bits;
            } u = {addend};
            Ref rr;
            if (is_fpval(node->ty)) {
                Con c = {.type = CBits128};
                switch (node->ty->kind) {
                    case TY_F16:
                        c.bits.i128.limb[0] = addend > 0 ? 0x3C00 : 0xBC00;  // half ±1.0
                        break;
                    case TY_F32:
                    case TY_F64:
                        // binary64 bit pattern of ±1.0 (an f32 ±1.0 widens
                        // exactly to f64, matching the constant encoding)
                        c.bits.i128.limb[1] = addend > 0 ? 0x3FF00000u : 0xBFF00000u;
                        break;
                    case TY_LDOUBLE:
                        if (T.ldouble_is_fp80) {
                            // x87 ±1.0: se = 0x3FFF, mantissa = 1.0 (int bit)
                            c.bits.i128.limb[2] = addend > 0 ? 0x3FFF : 0xBFFF;
                            c.bits.i128.limb[1] = 0x80000000u;
                        } else {
                            c.bits.f128 = addend > 0 ? FP128_ONE : fp128_neg(FP128_ONE);
                        }
                        break;
                    default:
                        c.bits.f128 = addend > 0 ? FP128_ONE : fp128_neg(FP128_ONE);
                        break;
                }
                rr = newcon(&c, curm);
                rr.ty = node->ty;
            } else if (is_flonum(node->ty)) {
                rr = DOUBLE(u.bits);
                rr.ty = node->ty;
            } else if (is_pointer(node->ty)) {
                // GEP index must be pointer-sized: T.ty_long (i64 on
                // LP64, i32 on ILP32)
                rr = LONG(addend);
            } else {
                rr = INT(addend);
                rr.ty = node->ty;
            }
            dst = TMP(tmp_id++, node->ty);
            new_ins(ir_op, dst, (Ref[]){lr, rr}, 2);
            store(dst, addr, align, node->lhs->member);
            if (node->kind == ND_PREINC || node->kind == ND_PREDEC)
                return dst;
            else
                return lr;
        }
        case ND_PTRAS: {
            Ref addr = gen_expr(node->lhs);
            int align = lvalue_align(node->lhs);
            atomic_order = node_mem_order(node);
            Ref lr = load(addr, node->ty, align, node->lhs->member);
            Ref rr = gen_expr(node->rhs);
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_GEP, dst, (Ref[]){lr, rr}, 2);
            atomic_order = node_mem_order(node);
            store(dst, addr, align, node->lhs->member);
            return dst;
        }
        case ND_ADDAS:
        case ND_SUBAS:
        case ND_MULAS:
        case ND_DIVAS:
        case ND_MODAS:
        case ND_ANDAS:
        case ND_ORAS:
        case ND_XORAS:
        case ND_LEFTAS:
        case ND_RIGHTAS: {
            Ref addr = gen_expr(node->lhs);
            int align = lvalue_align(node->lhs);
            atomic_order = node_mem_order(node);
            Ref lr = load(addr, node->ty, align, node->lhs->member);
            lr = cast(lr, node->ty, node->compute_ty);
            Ref rr = gen_expr(node->rhs);
            rr = cast(rr, node->rhs->ty, node->compute_ty);
            static int bin_op[] = {
                [ND_ADDAS] = IR_ADD, [ND_SUBAS] = IR_SUB,  [ND_MULAS] = IR_MUL,   [ND_DIVAS] = IR_DIV,
                [ND_MODAS] = IR_REM, [ND_LEFTAS] = IR_SHL, [ND_RIGHTAS] = IR_SHR, [ND_ANDAS] = IR_AND,
                [ND_ORAS] = IR_OR,   [ND_XORAS] = IR_XOR,
            };
            Ref res = TMP(tmp_id++, node->compute_ty);
            new_ins(bin_op[node->kind], res, (Ref[]){lr, rr}, 2);
            dst = cast(res, node->compute_ty, node->ty);
            atomic_order = node_mem_order(node);
            store(dst, addr, align, node->lhs->member);
            return dst;
        }
        case ND_LOGOR:
            return gen_logor(node);
        case ND_LOGAND:
            return gen_logand(node);
        case ND_COND:
            return gen_cond(node);
        case ND_FUNCALL: {
            int bkind = builtin_kind_of(node->func);
            if (bkind) return gen_builtin_call(node, bkind);

            int nargs = node->narg;

            // A record comes back by value, but every other part of the
            // compiler represents a record value by its address. The slot
            // that materializes it is allocated before the arguments so
            // that temp ids, and hence the IR's numbering, stay in the
            // order the instructions are emitted.
            bool is_record = node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION;
            Ref slot = R;
            if (is_record) {
                slot = TMP(tmp_id++, pointer_to(node->ty, 0));
                new_ins(IR_ALLOCA, slot, (Ref[]){INT(node->ty->align)}, 1);
            }

            Ref *call_ops = emalloc((nargs + 1) * sizeof(Ref));
            call_ops[0] = gen_expr(node->func);

            int idx = 1;
            for (Node *arg = node->args; arg; arg = arg->next) {
                Ref a = gen_expr(arg);
                // A record argument is passed by value. Whether the
                // operand already is that value depends on its shape: the
                // front end wraps a record lvalue in ND_LVTOR, which yields
                // the value, while a compound literal or the slot a
                // record-returning call was materialized in is an address.
                // load() cannot be used to read one: for an aggregate it
                // hands the address back, that being how a record value is
                // represented everywhere else.
                if (arg->ty->kind == TY_STRUCT || arg->ty->kind == TY_UNION) {
                    if (a.ty && a.ty->kind == TY_PTR) {
                        Ref val = TMP(tmp_id++, arg->ty);
                        new_ins(IR_LORD, val, (Ref[]){a, INT(arg->ty->align)}, 2);
                        a = val;
                    }
                }
                call_ops[idx++] = a;
            }

            if (node->ty->kind == TY_VOID) {
                new_ins(IR_CALL, R, call_ops, nargs + 1);
                return R;
            }

            // Materialize the returned record in the slot allocated above
            // and hand out its address, so an assignment copies from it and
            // a member access reads through it.
            if (is_record) {
                Ref val = TMP(tmp_id++, node->ty);
                new_ins(IR_CALL, val, call_ops, nargs + 1);
                store(val, slot, node->ty->align, NULL);
                return slot;
            }

            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_CALL, dst, call_ops, nargs + 1);
            return dst;
        }
        case ND_CAS: {
            Ref addr1 = gen_expr(node->lhs);
            Ref addr2 = gen_expr(node->rhs);
            Type *t = node->rhs->ty->base;
            Ref old_val = load(addr2, t, t->align, NULL);
            Ref new_val = gen_expr(node->desired);
            // LLVM cmpxchg takes integer/pointer operands only: floating
            // values compare by their bit pattern in an unsigned _BitInt
            // of the same width (clang does the same). The failure
            // writeback stores the integer into the float-typed *expected
            // (type punning), matching clang's IR.
            Type *cmp_ty = t;
            if (is_flonum(t)) {
                cmp_ty = bitint[t->size * 8][1];
                old_val = bitcast(old_val, cmp_ty);
                new_val = bitcast(new_val, cmp_ty);
            }
            Ref args[] = {addr1, old_val, new_val};
            // The ty carried by the cmpxchg dst is the compare type; the
            // result itself is { T, i1 }, which dumpir derives from it.
            Ref res = TMP(tmp_id++, cmp_ty);
            Ir *ins = new_ins(IR_CMPXCHG, res, args, 3);
            ins->mem_order = node_mem_order(node);
            ins->mem_order1 = node_mem_order1(node);
            ins->is_weak = node->is_weak;
            Ref val = TMP(tmp_id++, cmp_ty);
            Ref success = TMP(tmp_id++, bitint[1][1]);
            new_ins(IR_EXTRACTVAL, val, (Ref[]){res, INT(0)}, 2);
            new_ins(IR_EXTRACTVAL, success, (Ref[]){res, INT(1)}, 2);
            Blk *f_blk = new_blk();
            Blk *m_blk = new_blk();
            // IR_JNZ branches to succ1 on true: success goes straight to
            // the merge block, failure falls through to write the actual
            // value into *expected (C11 7.17.7.4p2).
            curb->jmp.type = IR_JNZ;
            curb->jmp.arg = success;
            curb->succ1 = m_blk;
            curb->succ2 = f_blk;
            add_pred(curb, curb->succ1);
            add_pred(curb, curb->succ2);

            curb = f_blk;
            insert_blk(curb);
            store(val, addr2, node->rhs->ty->base->align, NULL);
            curb->jmp.type = IR_JMP;
            curb->succ1 = m_blk;
            add_pred(curb, curb->succ1);
            curb = m_blk;
            insert_blk(curb);
            return cast(success, bitint[1][1], node->ty);
        }
        case ND_ATOMICRMW: {
            Ref addr = gen_expr(node->lhs);
            Ref new_val = gen_expr(node->desired);
            Ref args[] = {INT(node->armw_op), addr, new_val};
            // LLVM's atomicrmw add/sub on a pointer pointee yields the
            // raw integer result; convert it back to the pointer.
            bool ptr_addsub = (node->armw_op == A_ADD || node->armw_op == A_SUB) && is_pointer(node->ty);
            Ref res = TMP(tmp_id++, ptr_addsub ? T.ty_long : node->ty);
            Ir *ins = new_ins(IR_ATOMICRMW, res, args, 3);
            ins->mem_order = node_mem_order(node);
            return ptr_addsub ? cast(res, T.ty_long, node->ty) : res;
        }
        case ND_ALLOCA: {
            // Emitted at the call site, like clang: the memory lives
            // until the function returns (not scope-bound).
            Ref size = gen_expr(node->lhs);
            Ref dst = TMP(tmp_id++, pointer_to(node->base_ty ?: T.ty_char, 0));
            new_ins(IR_ALLOCA, dst, (Ref[]){size, INT(int128_to_i64(node->rhs->ival))}, 2);
            return dst;
        }
        default:
            break;
    }

    Ref lr = gen_expr(node->lhs);
    if (node->kind == ND_PLUS) return lr;

    // unary arithmetic operation
    switch (node->kind) {
        case ND_NEG:
            dst = TMP(tmp_id++, node->ty);
            if (is_flonum(node->ty)) {
                new_ins(IR_NEG, dst, (Ref[]){lr}, 1);
                return dst;
            }
            Ref zr = node->ty->size == 8 ? LONG(0) : INT(0);
            zr.ty = node->ty;  // rv32: T.ty_long is 32-bit; match the node
            new_ins(IR_SUB, dst, (Ref[]){zr, lr}, 2);
            return dst;
        case ND_INVERT:
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_XOR, dst, (Ref[]){lr, INT(-1)}, 2);
            return dst;
        case ND_NOT: {
            Ref tmp = TMP(tmp_id++, bitint[1][1]);
            Ref zr = INT(0);
            // Match the operand's IR type: an IR `ptr` must be compared
            // against a null constant, not an integer 0. nullptr_t is a
            // distinct scalar type but is lowered to `ptr`.
            if (is_ir_pointer(node->lhs->ty))
                zr = NULLPTR;
            else
                zr.ty = node->lhs->ty;
            new_ins(IR_CMP_EQ, tmp, (Ref[]){lr, zr}, 2);

            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_EXT, dst, (Ref[]){tmp}, 1);
            return dst;
        }
        default:
            break;
    }

    Ref rr = gen_expr(node->rhs);
    if (node->kind == ND_COMMA) return rr;

    switch (node->kind) {
        case ND_PTRADD: {
            Type *ty;
            if (node->ty->base->kind == TY_VLA) {
                ty = node->ty->base;
                while (ty->kind == TY_VLA) ty = ty->base;
                ty = pointer_to(ty, 0);
                lr.ty = ty;
            } else {
                ty = node->ty;
            }
            dst = TMP(tmp_id++, ty);
            new_ins(IR_GEP, dst, (Ref[]){lr, rr}, 2);
            return dst;
        }
        // binary and bit arithmetic operation
        case ND_ADD:
        case ND_SUB:
        case ND_MUL:
        case ND_DIV:
        case ND_MOD:
        case ND_LEFT:
        case ND_RIGHT:
        case ND_BAND:
        case ND_BOR:
        case ND_XOR: {
            static int bin_op[] = {
                [ND_ADD] = IR_ADD,  [ND_SUB] = IR_SUB,   [ND_MUL] = IR_MUL,  [ND_DIV] = IR_DIV, [ND_MOD] = IR_REM,
                [ND_LEFT] = IR_SHL, [ND_RIGHT] = IR_SHR, [ND_BAND] = IR_AND, [ND_BOR] = IR_OR,  [ND_XOR] = IR_XOR,
            };
            if (node->kind == ND_LEFT || node->kind == ND_RIGHT) {
                int width = is_bitint128(lr.ty) ? bitint_width(lr.ty) : lr.ty->size * 8;
                rr = cast(rr, rr.ty, bitint[width][1]);
            }
            dst = TMP(tmp_id++, node->ty);
            new_ins(bin_op[node->kind], dst, (Ref[]){lr, rr}, 2);
            return dst;
        }
        // Comparison operations：icmp return i1，zext to i32
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
        case ND_GT:
        case ND_GE: {
            static int cmp_op[] = {
                [ND_EQ] = IR_CMP_EQ,
                [ND_NE] = IR_CMP_NE,
                [ND_LT] = IR_CMP_LT,
                [ND_LE] = IR_CMP_LE,
            };
            // Canonicalize GT/GE to the swapped LT/LE forms (a > b ==
            // b < a) so CSE and later IR passes only see the 4 basic
            // comparisons.
            Ref tmp = TMP(tmp_id++, bitint[1][1]);
            if (node->kind == ND_GT || node->kind == ND_GE)
                new_ins(node->kind == ND_GT ? IR_CMP_LT : IR_CMP_LE, tmp, (Ref[]){rr, lr}, 2);
            else
                new_ins(cmp_op[node->kind], tmp, (Ref[]){lr, rr}, 2);

            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_EXT, dst, (Ref[]){tmp}, 1);
            return dst;
        }
        default:
            fatal("gen_expr: unknown node kind %d\n", node->kind);
    }
    return R;
}

static Ref gen_cond(Node *node) {
    Blk *t_blk = new_blk();
    Blk *f_blk = new_blk();
    Blk *m_blk = new_blk();

    // cond
    Ref tmp = gen_expr(node->cond);
    Ref cond = TMP(tmp_id++, bitint[1][1]);
    Ref zr = (is_pointer(tmp.ty) || tmp.ty->kind == TY_NULLPTR) ? NULLPTR : INT(0);
    if (zr.type == RInt) zr.ty = tmp.ty;
    new_ins(IR_CMP_NE, cond, (Ref[]){tmp, zr}, 2);
    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = t_blk;
    curb->succ2 = f_blk;
    add_pred(curb, curb->succ1);
    add_pred(curb, curb->succ2);

    // then
    curb = t_blk;
    insert_blk(curb);
    Ref true_r = gen_expr(node->then);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;
    add_pred(curb, curb->succ1);

    // else
    curb = f_blk;
    insert_blk(curb);
    Ref false_r = gen_expr(node->els);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;
    add_pred(curb, curb->succ1);

    curb = m_blk;
    insert_blk(curb);

    if (node->ty->kind != TY_VOID) {
        Ref result = TMP(tmp_id++, node->ty);
        Phi *phi = new_phi(result);
        add_phi_arg(phi, t_blk, true_r);
        add_phi_arg(phi, f_blk, false_r);
        insert_phi(curb, phi);
        return result;
    }
    return R;
}

static Ref gen_logor(Node *node) {
    Blk *f_blk = new_blk();
    Blk *m_blk = new_blk();

    // lhs
    Ref lr = gen_expr(node->lhs);
    Ref cond = TMP(tmp_id++, bitint[1][1]);
    Ref zr = INT(0);
    zr.ty = lr.ty;
    new_ins(IR_CMP_NE, cond, (Ref[]){lr, zr}, 2);
    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = m_blk;
    curb->succ2 = f_blk;
    add_pred(curb, curb->succ1);
    add_pred(curb, curb->succ2);
    Blk *sel = curb;

    // rhs
    curb = f_blk;
    insert_blk(curb);
    Ref rr = gen_expr(node->rhs);
    Ref res_r = TMP(tmp_id++, bitint[1][1]);
    zr.ty = rr.ty;
    new_ins(IR_CMP_NE, res_r, (Ref[]){rr, zr}, 2);
    Ref r_ext = TMP(tmp_id++, T.ty_int);
    new_ins(IR_EXT, r_ext, (Ref[]){res_r}, 1);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;
    add_pred(curb, curb->succ1);

    curb = m_blk;
    insert_blk(curb);

    Ref result = TMP(tmp_id++, T.ty_int);
    Phi *phi = new_phi(result);
    add_phi_arg(phi, sel, INT(1));
    add_phi_arg(phi, f_blk, r_ext);
    insert_phi(curb, phi);
    return result;
}

static Ref gen_logand(Node *node) {
    Blk *t_blk = new_blk();
    Blk *m_blk = new_blk();
    // lhs
    Ref lr = gen_expr(node->lhs);
    Ref cond = TMP(tmp_id++, bitint[1][1]);
    Ref zr = INT(0);
    zr.ty = lr.ty;
    new_ins(IR_CMP_NE, cond, (Ref[]){lr, zr}, 2);

    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = t_blk;
    curb->succ2 = m_blk;
    add_pred(curb, curb->succ1);
    add_pred(curb, curb->succ2);
    Blk *sel = curb;

    // rhs
    curb = t_blk;
    insert_blk(curb);
    Ref rr = gen_expr(node->rhs);
    Ref res_r = TMP(tmp_id++, bitint[1][1]);
    zr.ty = rr.ty;
    new_ins(IR_CMP_NE, res_r, (Ref[]){rr, zr}, 2);
    Ref r_ext = TMP(tmp_id++, T.ty_int);
    new_ins(IR_EXT, r_ext, (Ref[]){res_r}, 1);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;
    add_pred(curb, curb->succ1);

    curb = m_blk;
    insert_blk(curb);
    Ref result = TMP(tmp_id++, T.ty_int);
    Phi *phi = new_phi(result);
    add_phi_arg(phi, t_blk, r_ext);
    add_phi_arg(phi, sel, INT(0));
    insert_phi(curb, phi);
    return result;
}

static void gen_if(Node *node) {
    Blk *t_blk = new_blk();
    Blk *f_blk = node->els ? new_blk() : NULL;
    Blk *m_blk = new_blk();

    // cond
    Ref tmp = gen_stmt(node->cond);
    Ref cond = TMP(tmp_id++, bitint[1][1]);
    Ref zr = INT(0);
    zr.ty = tmp.ty;
    new_ins(IR_CMP_NE, cond, (Ref[]){tmp, zr}, 2);

    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = t_blk;
    curb->succ2 = f_blk ? f_blk : m_blk;
    add_pred(curb, curb->succ1);
    add_pred(curb, curb->succ2);

    // then
    curb = t_blk;
    insert_blk(curb);
    gen_stmt(node->then);
    curb->jmp.type = IR_JMP;
    curb->succ1 = m_blk;
    add_pred(curb, curb->succ1);

    // else
    if (f_blk) {
        curb = f_blk;
        insert_blk(curb);
        gen_stmt(node->els);
        curb->jmp.type = IR_JMP;
        curb->succ1 = m_blk;
        add_pred(curb, curb->succ1);
    }
    curb = m_blk;
    insert_blk(curb);
}

static void gen_for(Node *node) {
    Blk *cond_blk = new_blk();
    Blk *body_blk = new_blk();
    Blk *incr_blk = new_blk();
    Blk *merge_blk = new_blk();

    Blk *brk = brk_blk;
    Blk *cont = cont_blk;

    node->brk_blk = brk_blk = merge_blk;
    node->cont_blk = cont_blk = incr_blk;

    // init
    gen_stmt(node->init);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;
    add_pred(curb, curb->succ1);

    // cond
    curb = cond_blk;
    insert_blk(curb);
    if (node->cond) {
        Ref tmp = gen_expr(node->cond);
        Ref cond = TMP(tmp_id++, bitint[1][1]);
        Ref zr = INT(0);
        zr.ty = tmp.ty;
        new_ins(IR_CMP_NE, cond, (Ref[]){tmp, zr}, 2);

        curb->jmp.type = IR_JNZ;
        curb->jmp.arg = cond;
        curb->succ1 = body_blk;
        curb->succ2 = merge_blk;
        add_pred(curb, curb->succ1);
        add_pred(curb, curb->succ2);
    } else {
        curb->jmp.type = IR_JMP;
        curb->succ1 = body_blk;
        add_pred(curb, curb->succ1);
    }

    // body
    curb = body_blk;
    insert_blk(curb);
    gen_stmt(node->body);
    curb->jmp.type = IR_JMP;
    curb->succ1 = incr_blk;
    add_pred(curb, curb->succ1);

    // incr
    curb = incr_blk;
    insert_blk(curb);
    gen_expr(node->inc);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;
    add_pred(curb, curb->succ1);

    curb = merge_blk;
    insert_blk(curb);

    brk_blk = brk;
    cont_blk = cont;
}

static void gen_while(Node *node) {
    Blk *cond_blk = new_blk();
    Blk *body_blk = new_blk();
    Blk *merge_blk = new_blk();

    Blk *brk = brk_blk;
    Blk *cont = cont_blk;
    node->brk_blk = brk_blk = merge_blk;
    node->cont_blk = cont_blk = cond_blk;

    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;
    add_pred(curb, curb->succ1);

    // cond
    curb = cond_blk;
    insert_blk(curb);
    Ref tmp = gen_expr(node->cond);
    Ref cond = TMP(tmp_id++, bitint[1][1]);
    Ref zr = INT(0);
    zr.ty = tmp.ty;
    new_ins(IR_CMP_NE, cond, (Ref[]){tmp, zr}, 2);

    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = body_blk;
    curb->succ2 = merge_blk;
    add_pred(curb, curb->succ1);
    add_pred(curb, curb->succ2);

    // body
    curb = body_blk;
    insert_blk(curb);
    gen_stmt(node->body);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;
    add_pred(curb, curb->succ1);

    curb = merge_blk;
    insert_blk(curb);

    brk_blk = brk;
    cont_blk = cont;
}

static void gen_do(Node *node) {
    Blk *body_blk = new_blk();
    Blk *cond_blk = new_blk();
    Blk *merge_blk = new_blk();

    Blk *brk = brk_blk;
    Blk *cont = cont_blk;
    node->brk_blk = brk_blk = merge_blk;
    node->cont_blk = cont_blk = cond_blk;

    curb->jmp.type = IR_JMP;
    curb->succ1 = body_blk;
    add_pred(curb, curb->succ1);

    // body
    curb = body_blk;
    insert_blk(curb);
    gen_stmt(node->body);
    curb->jmp.type = IR_JMP;
    curb->succ1 = cond_blk;
    add_pred(curb, curb->succ1);

    // cond
    Ref zr = INT(0);
    curb = cond_blk;
    insert_blk(curb);
    Ref tmp = gen_expr(node->cond);
    Ref cond = TMP(tmp_id++, bitint[1][1]);
    zr.ty = tmp.ty;
    new_ins(IR_CMP_NE, cond, (Ref[]){tmp, zr}, 2);

    curb->jmp.type = IR_JNZ;
    curb->jmp.arg = cond;
    curb->succ1 = body_blk;
    curb->succ2 = merge_blk;
    add_pred(curb, curb->succ1);
    add_pred(curb, curb->succ2);

    curb = merge_blk;
    insert_blk(curb);

    brk_blk = brk;
    cont_blk = cont;
}

static void gen_switch(Node *n) {
    Blk *merge_blk = new_blk();
    int i = 0;
    for (Node *y = n->case_next; y; y = y->case_next) ++i;
    curb->narg = i;

    Blk *brk = brk_blk;
    n->brk_blk = brk_blk = merge_blk;

    Ref cond = gen_stmt(n->cond);
    curb->jmp.type = IR_SWITCH;
    curb->jmp.arg = cond;

    curb->jmp.args = emalloc(i * sizeof(Ref));
    curb->succ = emalloc(i * sizeof(Blk *));
    if (n->default_case)
        curb->succ1 = &curf->blks[n->default_case->blk_idx];
    else
        curb->succ1 = merge_blk;
    add_pred(curb, curb->succ1);

    Node *y = n->case_next;
    for (int j = 0; j < i; ++j) {
        // Case values must match the switch operand type: LONG stamps
        // T.ty_long, which is 32-bit on ILP32 targets while an 8-byte
        // cond is long long / _BitInt(64) (i64) there.
        Ref c = cond.ty->size == 8 ? LONG(int128_to_i64(y->ival)) : INT(int128_to_i64(y->ival));
        c.ty = cond.ty;
        curb->jmp.args[j] = c;
        curb->succ[j] = &curf->blks[y->blk_idx];
        add_pred(curb, curb->succ[j]);
        y = y->case_next;
    }

    curb = unreach;
    gen_stmt(n->body);

    curb->jmp.type = IR_JMP;
    curb->succ1 = merge_blk;
    add_pred(curb, curb->succ1);
    curb = merge_blk;
    insert_blk(curb);
    brk_blk = brk;
}

static Ref gen_label(Node *n) {
    Blk *b = &curf->blks[n->blk_idx];
    curb->jmp.type = IR_JMP;
    curb->succ1 = b;
    add_pred(curb, b);
    curb = b;
    insert_blk(curb);
    return gen_stmt(n->label_body);
}

static Ref gen_case(Node *n) {
    Blk *b = &curf->blks[n->blk_idx];
    curb->jmp.type = IR_JMP;
    curb->succ1 = b;
    add_pred(curb, b);
    curb = b;
    insert_blk(curb);
    return gen_stmt(n->label_body);
}

static void gen_goto(Node *n) {
    curb->jmp.type = IR_JMP;
    curb->succ1 = &curf->blks[n->target->blk_idx];
    add_pred(curb, curb->succ1);
    curb = unreach;
}

static void gen_indirectgoto(Node *n) {
    curb->jmp.type = IR_INDIRECTBR;
    curb->jmp.arg = gen_expr(n->lhs);
    curb->narg = curf->num_indirectbr;
    curb->succ = emalloc(curb->narg * sizeof(Blk *));
    for (int i = 0; i < curf->num_indirectbr; i++) {
        curb->succ[i] = curf->indirectbr[i];
        add_pred(curb, curb->succ[i]);
    }
    curb = unreach;
}

// The statement a named break/continue targets: the label run before
// the loop. label_body is attached to the run's head only, so ring
// members resolve through label_ring to the head.
static Node *named_loop_body(Node *target) {
    while (!target->label_body) target = target->label_ring;
    return target->label_body;
}

static void gen_break(Node *n) {
    curb->jmp.type = IR_JMP;
    curb->succ1 = n->target ? named_loop_body(n->target)->brk_blk : brk_blk;
    add_pred(curb, curb->succ1);
    curb = unreach;
}

static void gen_continue(Node *n) {
    curb->jmp.type = IR_JMP;
    curb->succ1 = n->target ? named_loop_body(n->target)->cont_blk : cont_blk;
    add_pred(curb, curb->succ1);
    curb = unreach;
}

static void gen_ret(Node *n) {
    Ref result = gen_expr(n->lhs);
    if (!refeq(result, R)) {
        Type *ty = curf->ty;
        store(result, SLOT(ty->nparam + 1, pointer_to(ty->ret, 0)), ty->ret->align, NULL);
    }

    curb->jmp.type = IR_JMP;
    curb->succ1 = curf->end;
    add_pred(curb, curb->succ1);
    curb = unreach;
}

static Ref gen_stmt(Node *node) {
    if (!node) return R;
    Ref reg;
    switch (node->kind) {
        case ND_IF:
            gen_if(node);
            break;
        case ND_FOR:
            gen_for(node);
            break;
        case ND_WHILE:
            gen_while(node);
            break;
        case ND_DO:
            gen_do(node);
            break;
        case ND_SWITCH:
            gen_switch(node);
            break;
        case ND_CASE:
            return gen_case(node);
        case ND_GOTO:
            gen_goto(node);
            break;
        case ND_GOTO_EXPR:
            gen_indirectgoto(node);
            break;
        case ND_BREAK:
            gen_break(node);
            break;
        case ND_CONTINUE:
            gen_continue(node);
            break;
        case ND_LABEL:
            return gen_label(node);
        case ND_DECL:
        case ND_COMP_STMT:
            for (Node *n = node->body; n; n = n->next) reg = gen_stmt(n);
            return reg;
        case ND_RETURN:
            gen_ret(node);
            break;
        case ND_EXPR_STMT:
            return gen_expr(node->lhs);
        default:
            return gen_expr(node);
    }
    return R;
}

Module *irgen(Module *md) {
    curm = md;
    for (Sym *fn = md->fns; fn; fn = fn->next) {
        if (!fn->is_defined) continue;

        curf = fn;
        uint32_t nparam = tmp_id = fn->ty->nparam;
        tail = &dummy;
        // The parser counted every block (labels at parse time, the
        // rest per construct); allocate the array once and fill it in
        // generation order. Label blocks take [0, num_lbl).
        fn->blks = emalloc(fn->num_blk * sizeof(Blk));
        for (int i = 0; i < fn->num_blk; i++) fn->blks[i].pred = vnew(2, sizeof(Blk *));
        blk_used = fn->num_lbl;
        fn->start = new_blk();
        fn->end = new_blk();
        int num_indirectgoto = 0;
        for (Node *y = fn->labels; y; y = y->goto_next)
            if (y->is_addr) num_indirectgoto++;
        fn->num_indirectbr = num_indirectgoto;
        fn->indirectbr = emalloc(num_indirectgoto * sizeof(Blk *));
        for (Node *y = fn->labels; num_indirectgoto && y; y = y->goto_next) {
            if (!y->is_addr) continue;
            fn->indirectbr[--num_indirectgoto] = &fn->blks[y->blk_idx];
        }
        brk_blk = cont_blk = NULL;

        curb = fn->start;
        insert_blk(curb);

        Type *ty = fn->ty->ret;
        bool is_valid = ty->kind != TY_VOID;
        // Entry
        if (is_valid) new_ins(IR_ALLOCA, TMP(tmp_id++, pointer_to(ty, 0)), (Ref[]){INT(ty->align)}, 1);

        for (Sym *var = fn->locals; var; var = var->next) {
            if (var->ty->kind == TY_VLA) continue;
            new_ins(IR_ALLOCA, TMP(var->vreg = tmp_id++, pointer_to(var->ty, 0)), (Ref[]){INT(var->align)}, 1);
        }

        // The C spec defines a special rule for the main function.
        //  Reaching the end of the main function is equivalent to returning 0,
        //  even though the behavior is undefined for the other functions.
        uint32_t main_id = 0;
        if (main_id == 0) main_id = intern("main", 4);
        if (curf->id == main_id) store(INT(0), TMP(nparam + 1, pointer_to(ty, 0)), ty->align, NULL);

        Sym *var = fn->locals;
        for (uint32_t i = 0; i < nparam; ++i, var = var->next)
            store(TMP(i, var->ty), TMP(var->vreg, pointer_to(var->ty, 0)), var->align, NULL);

        // Body
        gen_stmt(fn->body);

        // End
        curb->jmp.type = IR_JMP;
        curb = curb->succ1 = fn->end;
        insert_blk(curb);

        Ref ret_val = R;
        if (is_valid) ret_val = load(SLOT(nparam + 1, pointer_to(ty, 0)), ty, ty->align, NULL);
        curb->jmp.type = IR_RET;
        curb->jmp.arg = ret_val;

        // The parser's count is an upper bound: folding, dead-code
        // elimination and block merging only remove blocks.
        assert(blk_used <= fn->num_blk);
    }
    return md;
}
