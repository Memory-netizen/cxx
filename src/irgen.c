#include "cxx.h"

int cur_reg = 1;

// decode operand to str
static void fmt_operand(char *buf, int encoded) {
    if (encoded > 0)
        sprintf(buf, "%%%d", encoded);
    else
        sprintf(buf, "%d", -encoded);
}

static int gen_expr(Node *node) {
    if (!node) return 0;

    char lhs_str[32], rhs_str[32];
    switch (node->kind) {
        case ND_NUM:
            return -node->val;
        case ND_VAR: {
            int reg = cur_reg++;
            printf("  %%%d = load i32, ptr %%%d, align 4\n", reg, node->var->vreg);
            return reg;
        }
        case ND_AS:
            int reg = gen_expr(node->rhs);
            fmt_operand(rhs_str, reg);
            printf("  store i32 %s, ptr %%%d, align 4\n", rhs_str, node->lhs->var->vreg);
            return reg;
        default:
            break;
    }

    int lr = gen_expr(node->lhs);
    int rr = gen_expr(node->rhs);
    if (node->kind == ND_COMMA) return rr;

    int reg = cur_reg++;

    fmt_operand(lhs_str, lr);
    fmt_operand(rhs_str, rr);

    switch (node->kind) {
        // unary arithmetic operation
        case ND_PLUS:
            printf("  %%%d = add nsw i32 0, %s\n", reg, lhs_str);
            break;
        case ND_NEG:
            printf("  %%%d = sub nsw i32 0, %s\n", reg, lhs_str);
            break;
        case ND_INVERT:
            printf("  %%%d = xor i32 -1, %s\n", reg, lhs_str);
            break;
        case ND_NOT:
            printf("  %%%d = icmp eq i32 %s, 0\n", reg, lhs_str);
            int zext_reg = cur_reg++;
            printf("  %%%d = zext i1 %%%d to i32\n", zext_reg, reg);
            return zext_reg;
        // binary arithmetic operation
        case ND_ADD:
            printf("  %%%d = add nsw i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;
        case ND_SUB:
            printf("  %%%d = sub nsw i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;
        case ND_MUL:
            printf("  %%%d = mul nsw i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;
        case ND_DIV:
            printf("  %%%d = sdiv i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;
        case ND_MOD:
            printf("  %%%d = srem i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;

        // shift operation
        case ND_LEFT:
            printf("  %%%d = shl nsw i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;
        case ND_RIGHT:
            printf("  %%%d = ashr i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;

        // bit operation
        case ND_BAND:
            printf("  %%%d = and i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;
        case ND_BOR:
            printf("  %%%d = or i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;
        case ND_XOR:
            printf("  %%%d = xor i32 %s, %s\n", reg, lhs_str, rhs_str);
            break;

        // Comparison operations：icmp return i1，zext to i32
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_GT:
        case ND_LE:
        case ND_GE: {
            static char *cmp_str[] = {
                [ND_EQ] = "eq", [ND_NE] = "ne", [ND_LT] = "slt", [ND_GT] = "sgt", [ND_LE] = "sle", [ND_GE] = "sge",
            };
            printf("  %%%d = icmp %s i32 %s, %s\n", reg, cmp_str[node->kind], lhs_str, rhs_str);
            int zext_reg = cur_reg++;
            printf("  %%%d = zext i1 %%%d to i32\n", zext_reg, reg);
            return zext_reg;
        }

        default:
            fprintf(stderr, "gen_expr: unknown node kind %d\n", node->kind);
            exit(1);
    }

    return reg;
}

static int gen_stmt(Node *node) {
    if (node->kind == ND_EXPR_STMT) return gen_expr(node->lhs);
    exit(1);
}

void irgen(Function *prog) {
    printf("define i32 @main() {\n");
    printf("entry:\n");

    for (Obj *var = prog->locals; var; var = var->next) {
        var->vreg = cur_reg++;
        printf("  %%%d = alloca i32, align 4\n", var->vreg);
    }

    int ret_val = 0;
    for (Node *n = prog->body; n; n = n->next) {
        ret_val = gen_stmt(n);
    }

    if (ret_val > 0)
        printf("  ret i32 %%%d\n", ret_val);
    else
        printf("  ret i32 %d\n", -ret_val);
    printf("}\n");
}
