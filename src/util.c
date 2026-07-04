#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POOL_SIZE (32 * 1024 * 1024)  // 32MB
#define BIG_THRESHOLD (128 * 1024)    // 128KB
#define ALIGNMENT 16
#define ALIGN_UP(value, align) (((value) + (align) - 1) & ~((align) - 1))
#define HEAD_SIZE ALIGN_UP(sizeof(void *), ALIGNMENT)
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

static void **pool;
static size_t len;

typedef struct big_block {
    void *ptr;
    struct big_block *next;
} big_block;
static big_block *big_blocks;

void *emalloc(size_t n) {
    if (n == 0) return NULL;
    n = ALIGN_UP(n, ALIGNMENT);
    if (n >= BIG_THRESHOLD) {
        void *p = malloc(n);
        if (!p) {
            fprintf(stderr, "emalloc: out of memory\n");
            exit(1);
        }
        big_block *b = emalloc(sizeof(big_block));
        b->ptr = p;
        b->next = big_blocks;
        big_blocks = b;
        return p;
    }

    if (len < n) {
        void **new_pool = malloc(POOL_SIZE);
        if (!new_pool) {
            fprintf(stderr, "emalloc: out of memory\n");
            exit(1);
        }
        new_pool[0] = pool;
        pool = new_pool;
        len = POOL_SIZE - HEAD_SIZE;
    }

    void *p = (char *)pool + HEAD_SIZE + (len - n);
    len -= n;
    return p;
}

void freeall(void) {
    while (big_blocks) {
        big_block *b = big_blocks;
        big_blocks = b->next;
        free(b->ptr);
    }
    while (pool) {
        void **pp = pool[0];
        free(pool);
        pool = pp;
    }
    len = 0;
}

typedef struct Vec Vec;
struct Vec {
    size_t esz;
    size_t cap;
    union {
        long long ll;
        long double ld;
        void *ptr;
    } data[];
};

void *vnew(size_t len, size_t esz) {
    size_t cap = 2;
    while (cap < len) cap *= 2;
    Vec *v = emalloc(sizeof(Vec) + esz * cap);
    v->cap = cap;
    v->esz = esz;
    return v->data;
}

void *vgrow(void *data, size_t len) {
    if (!data) return NULL;
    Vec *v = container_of(data, Vec, data);
    if (v->cap >= len) return data;

    void *new_data = vnew(len, v->esz);
    memcpy(new_data, data, v->esz * v->cap);
    return new_data;
}
