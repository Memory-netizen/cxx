#include <stdio.h>
#include <stdlib.h>

#define POOL_SIZE (32 * 1024 * 1024)  // 32MB
#define ALIGNMENT 8
#define HEAD_SIZE (((sizeof(void *) + ALIGNMENT - 1) / ALIGNMENT) * ALIGNMENT)

static void **pool;
static size_t len;

typedef struct big_block {
    void *ptr;
    struct big_block *next;
} big_block;
static big_block *big_blocks;

void *emalloc(size_t n) {
    if (n == 0) return NULL;
    n = (n + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    if (n > POOL_SIZE - HEAD_SIZE) {
        void *p = malloc(n);
        big_block *b = malloc(sizeof(big_block));
        if (!p || !b) {
            fprintf(stderr, "emalloc: out of memory\n");
            exit(1);
        }
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
    while (pool) {
        void **pp = pool[0];
        free(pool);
        pool = pp;
    }
    while (big_blocks) {
        big_block *b = big_blocks;
        big_blocks = b->next;
        free(b->ptr);
        free(b);
    }
    len = 0;
}
