#define ASSERT(x, y) assert(x, y, #y)

void assert(int expected, int actual, char *code);
int printf(const char *fmt, ...);
int sprintf(const char *buf, char *fmt, ...);
int strcmp(char *p, char *q);
int strncmp(const char *p, const char *q, long n);
int memcmp(char *p, char *q, long n);
void exit(int n);
long strlen(char *s);
void *memcpy(void *dest, void *src, long n);
