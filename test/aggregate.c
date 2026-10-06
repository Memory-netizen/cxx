/* Aggregate ABI matrix: records of every size class and member mix, through
 * an ordinary call and through va_arg, plus pointers to records (which are
 * ordinary arguments, not ABI-lowered copies).
 *
 * The values stay small because assert() takes ints. */

#include <stdarg.h>

#include "test.h"

/* ---- 1 字节 ---- */
struct S1 {
    char a;
};
union U1 {
    char a;
};
/* ---- 2 字节 ---- */
struct S2 {
    char a, b;
};
struct S2s {
    short a;
};
/* ---- 4 字节 ---- */
struct S4i {
    int a;
};
struct S4f {
    float a;
};
struct S4c {
    char a, b, c, d;
};
/* ---- 8 字节 ---- */
struct S8i {
    int a, b;
};
struct S8l {
    long a;
};
struct S8d {
    double a;
};
struct S8f {
    float a, b;
};
/* ---- 12 字节 ---- */
struct S12 {
    int a, b, c;
};
/* ---- 16 字节 ---- */
struct S16l {
    long a, b;
};
struct S16d {
    double a, b;
};
struct S16m {
    int a;
    double b;
};
struct S16m2 {
    double a;
    int b;
};
struct S16f {
    float a, b, c, d;
};
/* ---- 24 / 32 字节（内存类） ---- */
struct S24 {
    long a, b, c;
};
struct S32 {
    long a, b, c, d;
};
/* ---- 联合体 ---- */
union U4 {
    int a;
    char b[4];
};
union U8i {
    int a;
    long b;
};
union U8d {
    double a;
    long b;
};
union U16 {
    double a[2];
    long b[2];
};
union U32 {
    double a[4];
    char b[32];
};

/* ================= 普通调用：按值传参 ================= */
static int t1(struct S1 v) { return v.a; }
static int t2(struct S2 v) { return v.a * 1 + v.b * 2; }
static int t2s(struct S2s v) { return v.a; }
static int t4i(struct S4i v) { return v.a; }
static int t4f(struct S4f v) { return (int)v.a; }
static int t4c(struct S4c v) { return v.a + v.b * 2 + v.c * 3 + v.d * 4; }
static int t8i(struct S8i v) { return v.a * 1 + v.b * 2; }
static int t8l(struct S8l v) { return (int)v.a; }
static int t8d(struct S8d v) { return (int)v.a; }
static int t8f(struct S8f v) { return (int)v.a * 1 + (int)v.b * 2; }
static int t12(struct S12 v) { return v.a * 1 + v.b * 2 + v.c * 4; }
static int t16l(struct S16l v) { return (int)v.a * 1 + (int)v.b * 2; }
static int t16d(struct S16d v) { return (int)v.a * 1 + (int)v.b * 2; }
static int t16m(struct S16m v) { return v.a * 1 + (int)v.b * 2; }
static int t16m2(struct S16m2 v) { return (int)v.a * 1 + v.b * 2; }
static int t16f(struct S16f v) { return (int)v.a * 1 + (int)v.b * 2 + (int)v.c * 4 + (int)v.d * 8; }
static int t24(struct S24 v) { return (int)v.a * 1 + (int)v.b * 2 + (int)v.c * 4; }
static int t32(struct S32 v) { return (int)v.a * 1 + (int)v.b * 2 + (int)v.c * 4 + (int)v.d * 8; }
static int tu1(union U1 v) { return v.a; }
static int tu4(union U4 v) { return v.a & 7; }
static int tu8i(union U8i v) { return (int)v.b & 7; }
static int tu8d(union U8d v) { return (int)v.a; }
static int tu16(union U16 v) { return (int)v.b[0] * 1 + (int)v.b[1] * 2; }
static int tu32(union U32 v) { return (int)v.a[0] * 1 + (int)v.a[1] * 2; }

/* ================= 可变参数：同上的类型，走 va_arg ================= */
static int va_one(int n, ...) {
    va_list ap;
    va_start(ap, n);
    struct S1 v = va_arg(ap, struct S1);
    va_end(ap);
    return v.a;
}
static int va_two(int n, ...) {
    va_list ap;
    va_start(ap, n);
    struct S2 v = va_arg(ap, struct S2);
    va_end(ap);
    return v.a * 1 + v.b * 2;
}
static int va_s8i(int n, ...) {
    va_list ap;
    va_start(ap, n);
    struct S8i v = va_arg(ap, struct S8i);
    va_end(ap);
    return v.a * 1 + v.b * 2;
}
static int va_s8d(int n, ...) {
    va_list ap;
    va_start(ap, n);
    struct S8d v = va_arg(ap, struct S8d);
    va_end(ap);
    return (int)v.a;
}
static int va_s16m(int n, ...) {
    va_list ap;
    va_start(ap, n);
    struct S16m v = va_arg(ap, struct S16m);
    va_end(ap);
    return v.a * 1 + (int)v.b * 2;
}
static int va_s32(int n, ...) {
    va_list ap;
    va_start(ap, n);
    struct S32 v = va_arg(ap, struct S32);
    va_end(ap);
    return (int)v.a * 1 + (int)v.b * 2 + (int)v.c * 4 + (int)v.d * 8;
}
/* 聚合体之后还有普通实参：验证游标没有被多走或少走 */
static int va_mix(int n, ...) {
    va_list ap;
    va_start(ap, n);
    struct S8i v = va_arg(ap, struct S8i);
    int k = va_arg(ap, int);
    struct S16d d = va_arg(ap, struct S16d);
    va_end(ap);
    return v.a * 1 + v.b * 2 + k * 4 + (int)d.a * 8 + (int)d.b * 16;
}


/* ============ 真的传一个指向结构体的指针（不是 ABI 降级） ============ */
static int p_pair(struct S8i *p) { return p->a * 1 + p->b * 2; }
static int p_dbl(struct S16d *p) { return (int)p->a * 1 + (int)p->b * 2; }
static int p_big(struct S32 *p) { return (int)p->a * 1 + (int)p->d * 8; }
static int p_void(void *p) {
    struct S8i *q = p;
    return q->a * 3 + q->b * 5;
}
/* 结构体里含指向结构体的指针 */
struct HasPtr {
    struct S8i *p;
    int k;
};
static int hp(struct HasPtr v) { return v.p->a * 1 + v.p->b * 2 + v.k * 4; }
static int va_hasptr(int n, ...) {
    va_list ap;
    va_start(ap, n);
    struct HasPtr v = va_arg(ap, struct HasPtr);
    va_end(ap);
    return v.p->a * 1 + v.p->b * 2 + v.k * 4;
}

int main() {
    struct S8i s8 = {1, 2};
    struct S16d s16d = {3.0, 4.0};
    struct S32 s32 = {1, 2, 3, 4};
    struct HasPtr h = {&s8, 5};

    /* --- 普通调用 --- */
    ASSERT(7, t1((struct S1){7}));
    ASSERT(1 + 4, t2((struct S2){1, 2}));
    ASSERT(9, t2s((struct S2s){9}));
    ASSERT(11, t4i((struct S4i){11}));
    ASSERT(12, t4f((struct S4f){12.9f}));
    ASSERT(1 + 4 + 9 + 16, t4c((struct S4c){1, 2, 3, 4}));
    ASSERT(1 + 4, t8i((struct S8i){1, 2}));
    ASSERT(13, t8l((struct S8l){13}));
    ASSERT(14, t8d((struct S8d){14.9}));
    ASSERT(1 + 4, t8f((struct S8f){1.9f, 2.9f}));
    ASSERT(1 + 4 + 12, t12((struct S12){1, 2, 3}));
    ASSERT(1 + 4, t16l((struct S16l){1, 2}));
    ASSERT(1 + 4, t16d((struct S16d){1.9, 2.9}));
    ASSERT(1 + 4, t16m((struct S16m){1, 2.9}));
    ASSERT(1 + 4, t16m2((struct S16m2){1.9, 2}));
    ASSERT(1 + 4 + 12 + 32, t16f((struct S16f){1.9f, 2.9f, 3.9f, 4.9f}));
    ASSERT(1 + 4 + 12, t24((struct S24){1, 2, 3}));
    ASSERT(1 + 4 + 12 + 32, t32((struct S32){1, 2, 3, 4}));
    ASSERT(6, tu1((union U1){6}));
    ASSERT(5, tu4((union U4){5}));
    ASSERT(3, tu8i((union U8i){.b = 3}));
    ASSERT(15, tu8d((union U8d){15.9}));
    ASSERT(1 + 4, tu16((union U16){.b = {1, 2}}));
    ASSERT(1 + 4, tu32((union U32){.a = {1.0, 2.0}}));

    /* --- 可变参数 --- */
    ASSERT(7, va_one(1, (struct S1){7}));
    ASSERT(1 + 4, va_two(1, (struct S2){1, 2}));
    ASSERT(1 + 4, va_s8i(1, (struct S8i){1, 2}));
    ASSERT(14, va_s8d(1, (struct S8d){14.9}));
    ASSERT(1 + 4, va_s16m(1, (struct S16m){1, 2.9}));
    ASSERT(1 + 4 + 12 + 32, va_s32(1, (struct S32){1, 2, 3, 4}));
    ASSERT(1 + 4 + 3 * 4 + 5 * 8 + 6 * 16,
           va_mix(3, (struct S8i){1, 2}, 3, (struct S16d){5.9, 6.9}));


    /* --- 指向结构体的指针 --- */
    ASSERT(1 + 4, p_pair(&s8));
    ASSERT(3 + 8, p_dbl(&s16d));
    ASSERT(1 + 32, p_big(&s32));
    ASSERT(1 * 3 + 2 * 5, p_void(&s8));
    ASSERT(1 + 4 + 20, hp(h));
    ASSERT(1 + 4 + 20, va_hasptr(1, h));

    printf("OK\n");
    return 0;
}
