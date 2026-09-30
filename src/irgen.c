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

static Ref gen_builtin_fn(Node *node) {
    if (node->func->lhs->var->id == intern("__builtin_alloca", 16)) {
        Ref size = gen_expr(node->args);
        Ref dst = TMP(tmp_id++, pointer_to(T.ty_char, 0));
        new_ins(IR_ALLOCA, dst, (Ref[]){size, INT(16)}, 2);
        return dst;
    }
    if (node->func->lhs->var->id == intern("__builtin_alloca_with_align", 27)) {
        Ref size = gen_expr(node->args);
        int align = (int)int128_to_i64(node->args->next->ival);
        Type *base_ty = node->base_ty ?: T.ty_char;
        Ref dst = TMP(tmp_id++, pointer_to(base_ty, 0));
        new_ins(IR_ALLOCA, dst, (Ref[]){size, INT(align / 8)}, 2);
        return dst;
    }
    return R;
}

static Ref cast(Ref val, Type *src_ty, Type *target_ty) {
    if (target_ty->kind == TY_BOOL) {
        Ref tmp = TMP(tmp_id++, bitint[1][1]);
        Ref zr = INT(0);
        zr.ty = src_ty;
        new_ins(IR_CMP_NE, tmp, (Ref[]){val, zr}, 2);

        Ref dst = TMP(tmp_id++, target_ty);
        new_ins(IR_EXT, dst, (Ref[]){tmp}, 1);
        return dst;
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
        case ND_MEMBER: {
            Ref addr = gen_expr(node->lhs);
            if (node->lhs->ty->kind == TY_UNION) {
                addr.ty = pointer_to(node->member->ty, 0);
                return addr;
            }

            int pos = 0;
            int idx = 0;
            int mem_off = node->member->offset;
            Member *cur = node->lhs->ty->members;

            while (cur->offset != mem_off) {
                pos += cur->unit_ty->size;
                idx++;
                int off = cur->offset;
                do {
                    cur = cur->next;
                } while (cur->offset == off);
                if (pos < cur->offset) {
                    pos = cur->offset;
                    idx++;
                }
            }
            Ref gep_ops[] = {addr, INT(0), INT(idx)};
            Ref dst = TMP(tmp_id++, pointer_to(node->ty, 0));
            new_ins(IR_GEP, dst, gep_ops, 3);
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

static Ref gen_expr(Node *node) {
    if (!node) return R;
    Ref dst;
    switch (node->kind) {
        case ND_SP_SAVE:
            dst = TMP(tmp_id++, node->ty);
            new_ins(IR_SP_SAVE, dst, NULL, 0);
            return dst;
        case ND_SP_RESTORE:
            dst = gen_expr(node->lhs);
            new_ins(IR_SP_RESTORE, R, (Ref[]){dst}, 1);
            return R;
        case ND_NOP:
            return R;
        case ND_NULLPTR:
            return NULLPTR;
        case ND_NUM:
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
            int align = node->ty->align;
            if (node->lhs->kind == ND_VAR) align = node->lhs->var->align;
            Ref addr = gen_expr(node->lhs);
            atomic_order = node_mem_order(node->lhs);
            return load(addr, node->ty, align, node->lhs->member);
        }
        case ND_VAR:
        case ND_MEMBER:
            return gen_addr(node);
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
            int align = node->ty->align;
            if (node->lhs->kind == ND_VAR) align = node->lhs->var->align;

            if (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION) {
                Ref src = gen_expr(node->rhs);
                Ref ops[] = {addr, src, INT(node->ty->size)};
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
            int align = node->ty->align;
            if (node->lhs->kind == ND_VAR) align = node->lhs->var->align;
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
            int align = node->ty->align;
            if (node->lhs->kind == ND_VAR) align = node->lhs->var->align;
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
            int align = node->ty->align;
            if (node->lhs->kind == ND_VAR) align = node->lhs->var->align;
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
            if (node->func->kind == ND_IMCAST && is_builtin_fn(node->func->lhs->var->id)) return gen_builtin_fn(node);
            int nargs = node->narg;
            Ref *call_ops = emalloc((nargs + 1) * sizeof(Ref));
            call_ops[0] = gen_expr(node->func);

            int idx = 1;
            for (Node *arg = node->args; arg; arg = arg->next) call_ops[idx++] = gen_expr(arg);

            if (node->ty->kind == TY_VOID)
                dst = R;
            else
                dst = TMP(tmp_id++, node->ty);
            new_ins(IR_CALL, dst, call_ops, nargs + 1);
            return dst;
        }
        case ND_CAS: {
            Ref addr1 = gen_expr(node->lhs);
            Ref addr2 = gen_expr(node->rhs);
            Ref old_val = load(addr2, node->rhs->ty->base, node->rhs->ty->base->align, NULL);
            Ref new_val = gen_expr(node->desired);
            Ref args[] = {addr1, old_val, new_val};
            // The ty carried by the cmpxchg dst is the value type T; the
            // result itself is { T, i1 }, which dumpir derives from it.
            Ref res = TMP(tmp_id++, node->rhs->ty->base);
            Ir *ins = new_ins(IR_CMPXCHG, res, args, 3);
            ins->mem_order = node_mem_order(node);
            ins->mem_order1 = node_mem_order1(node);
            ins->is_weak = node->is_weak;
            Ref val = TMP(tmp_id++, node->rhs->ty->base);
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
            Ref res = TMP(tmp_id++, node->ty);
            Ir *ins = new_ins(IR_ATOMICRMW, res, args, 3);
            ins->mem_order = node_mem_order(node);
            return res;
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
    Ref zr = INT(0);
    zr.ty = tmp.ty;
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
