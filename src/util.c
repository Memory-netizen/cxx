#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POOL_SIZE (32 * 1024 * 1024)  // 32MB
#define BIG_THRESHOLD (128 * 1024)    // 128KB
#define ALIGNMENT _Alignof(max_align_t)
#define ALIGN_UP(value, align) (((value) + (align) - 1) & ~((align) - 1))
#define HEAD_SIZE ALIGN_UP(sizeof(void *), ALIGNMENT)
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

static void **pool;
static size_t free_len;

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

    if (free_len < n) {
        void **new_pool = malloc(POOL_SIZE);
        if (!new_pool) {
            fprintf(stderr, "emalloc: out of memory\n");
            exit(1);
        }
        new_pool[0] = pool;
        pool = new_pool;
        free_len = POOL_SIZE - HEAD_SIZE;
    }

    void *p = (char *)pool + HEAD_SIZE + (free_len - n);
    free_len -= n;
    return p;
}

typedef struct Vec Vec;
struct Vec {
    size_t esz;
    size_t cap;
    union {
        long long ll;
        long double ld;
        void *ptr;
        void (*fp)(void);
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

char *format(char *s, ...) {
    va_list ap;
    int n;
    char *p;

    va_start(ap, s);
    n = vsnprintf(NULL, 0, s, ap);
    va_end(ap);
    p = emalloc(n + 1);
    va_start(ap, s);
    vsnprintf(p, n + 1, s, ap);
    va_end(ap);
    return p;
}

#define IBits 12
#define IMask ((1 << IBits) - 1)

typedef struct Bucket Bucket;
struct Bucket {
    uint32_t nstr;
    uint32_t *len;
    char **str;
};
static Bucket itbl[IMask + 1];

static uint32_t fnv_hash_32(const unsigned char *s, int len) {
    uint32_t hash = 0x811c9dc5;
    for (int i = 0; i < len; i++) {
        hash ^= (uint32_t)s[i];
        hash *= 0x01000193;
    }
    return hash;
}

uint32_t intern(char *s, int len) {
    Bucket *b;
    uint32_t h;
    uint32_t i, n;

    h = fnv_hash_32((const unsigned char *)s, len) & IMask;
    b = &itbl[h];
    n = b->nstr;

    for (i = 0; i < n; i++)
        if (memcmp(s, b->str[i], len) == 0) return h | (i << IBits);

    if (n == 1 << (32 - IBits)) {
        fprintf(stderr, "interning table overflow\n");
        exit(1);
    }
    if (n == 0) {
        b->str = vnew(1, sizeof b->str[0]);
        b->len = vnew(1, sizeof b->len[0]);
    } else if ((n & (n - 1)) == 0) {
        b->str = vgrow(b->str, n + n);
        b->len = vgrow(b->len, n + n);
    }
    b->len[n] = len;
    b->str[n] = emalloc(len + 1);
    b->nstr = n + 1;
    memcpy(b->str[n], s, len);
    b->str[n][len] = '\0';
    return h | (n << IBits);
}

char *str(uint32_t id) {
    assert(id >> IBits < itbl[id & IMask].nstr);
    return itbl[id & IMask].str[id >> IBits];
}

uint32_t str_len(uint32_t id) {
    assert(id >> IBits < itbl[id & IMask].nstr);
    return itbl[id & IMask].len[id >> IBits];
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
    free_len = 0;
    memset(itbl, 0, sizeof(itbl));
}

char *escape_char_to_string(char c) {
    static char buffer[8];
    /* if (isprint(c)) {
         buffer[0] = c;
         buffer[1] = '\0';
     } else*/
    {
        sprintf(buffer, "\\%02x", (unsigned char)c);
    }
    return buffer;
}
