/* Freestanding mini-libc for running the cxx test suite on bare-metal
 * rv32 (qemu-system-riscv32 -M virt -bios none -kernel).
 *
 * Provides the small libc subset the tests use (printf/sprintf with %d,
 * %u, %x, %c, %s, %-Ns, %lld, %f, %.Nf; strcmp/strncmp/strlen/
 * memcmp/memcpy/memset; exit), the fixture functions from test/common,
 * and the entry point. The verdict goes to the SiFive test device at
 * 0x100000: 0x5555 = pass, 0x3333 | (code << 16) = fail with that exit
 * code. Diagnostics go to the UART at 0x10000000.
 *
 * Compiled freestanding for rv32 (ilp32d); %f formatting uses the
 * vendored fp128 library so decimal rounding is exact.
 */
#include <stdarg.h>
#include <stdint.h>

#include "../src/support/fp128.h"

extern char __bss_start[], __bss_end[];

/* ---------- output sinks ---------- */

typedef struct {
  void (*put)(char c);
} Sink;

static void uart_putc(char c) {
  volatile char *uart = (volatile char *)0x10000000;
  *uart = c;
}

static char *bufp;
static int bufcount;

static void buf_putc(char c) {
  *bufp++ = c;
  bufcount++;
}

static Sink sink_uart = {uart_putc};
static Sink sink_buf = {buf_putc};

/* ---------- memory functions ---------- */

void *memcpy(void *dst, const void *src, long n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  for (long i = 0; i < n; i++)
    d[i] = s[i];
  return dst;
}

void *memset(void *s, int c, long n) {
  unsigned char *d = (unsigned char *)s;
  for (long i = 0; i < n; i++)
    d[i] = (unsigned char)c;
  return s;
}

long strlen(char *s) {
  long n = 0;
  while (*s++)
    n++;
  return n;
}

int strcmp(char *p, char *q) {
  while (*p && *p == *q) {
    p++;
    q++;
  }
  return (unsigned char)*p - (unsigned char)*q;
}

int strncmp(const char *p, const char *q, long n) {
  for (long i = 0; i < n; i++) {
    if (p[i] != q[i])
      return (unsigned char)p[i] - (unsigned char)q[i];
    if (!p[i])
      return 0;
  }
  return 0;
}

int memcmp(char *p, char *q, long n) {
  for (long i = 0; i < n; i++) {
    if (p[i] != q[i])
      return (unsigned char)p[i] - (unsigned char)q[i];
  }
  return 0;
}

/* ---------- exit ---------- */

void exit(int n) {
  volatile uint32_t *test_dev = (volatile uint32_t *)0x100000;
  *test_dev = (n == 0) ? 0x5555u : (0x3333u | ((uint32_t)n << 16));
  for (;;)
    ;
}

/* ---------- formatting ---------- */

static void emit(Sink *s, const char *str, int len) {
  for (int i = 0; i < len; i++)
    s->put(str[i]);
}

static void emit_str(Sink *s, const char *str) { emit(s, str, (int)strlen((char *)str)); }

static void emit_uint(Sink *s, uint64_t v) {
  char b[24];
  int n = 0;
  do {
    b[n++] = (char)('0' + (int)(v % 10));
    v /= 10;
  } while (v);
  while (n > 0)
    s->put(b[--n]);
}

static void emit_hex(Sink *s, uint32_t v, int upper) {
  const char *h = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  char b[8];
  int n = 0;
  do {
    b[n++] = h[v & 0xF];
    v >>= 4;
  } while (v);
  while (n > 0)
    s->put(b[--n]);
}

/* Exact double -> decimal with `prec` fraction digits, rounding half
 * away from zero, via the fp128 library. */
static void emit_fp(Sink *s, double d, int prec) {
  union {
    double d;
    uint64_t u;
  } u;
  u.d = d;
  Fp128 v = fp128_from_fp64(u.u);
  if (fp128_is_nan(v)) {
    emit_str(s, "nan");
    return;
  }
  if (fp128_is_inf(v)) {
    if (fp128_is_negative(v))
      s->put('-');
    emit_str(s, "inf");
    return;
  }
  int neg = fp128_is_negative(v);
  v = fp128_abs(v);
  Fp128 ten = fp128_from_int128((Int128){{10, 0, 0, 0}}, UNSIGNED);
  Fp128 scale = fp128_from_int128((Int128){{1, 0, 0, 0}}, UNSIGNED);
  for (int i = 0; i < prec; i++)
    scale = fp128_mul(scale, ten);
  Fp128 two = fp128_from_int128((Int128){{2, 0, 0, 0}}, UNSIGNED);
  v = fp128_add(fp128_mul(v, scale), fp128_div(FP128_ONE, two));
  bool ok;
  Int128 iv = fp128_to_int128(v, SIGNED, &ok);
  if (!ok) {
    emit_str(s, "inf");
    return;
  }
  char dbuf[64];
  int n = int128_to_str(iv, UNSIGNED, 10, dbuf, sizeof(dbuf));
  if (neg)
    s->put('-');
  if (n <= prec) {
    emit_str(s, "0.");
    for (int i = 0; i < prec - n; i++)
      s->put('0');
    emit(s, dbuf, n);
  } else {
    emit(s, dbuf, n - prec);
    s->put('.');
    emit(s, dbuf + n - prec, prec);
  }
}

static void vformat(Sink *s, const char *fmt, va_list ap) {
  for (const char *p = fmt; *p; p++) {
    if (*p != '%') {
      s->put(*p);
      continue;
    }
    p++;
    int width = 0;
    int left = 0;
    if (*p == '-') {
      left = 1;
      p++;
    }
    while (*p >= '0' && *p <= '9') {
      width = width * 10 + (*p - '0');
      p++;
    }
    int prec = 6;
    if (*p == '.') {
      prec = 0;
      p++;
      while (*p >= '0' && *p <= '9') {
        prec = prec * 10 + (*p - '0');
        p++;
      }
    }
    int isll = 0;
    if (p[0] == 'l' && p[1] == 'l') {
      isll = 1;
      p += 2;
    }
    char c = *p;
    if (c == '%') {
      s->put('%');
      continue;
    }
    if (c == 's') {
      const char *str = va_arg(ap, const char *);
      int len = (int)strlen((char *)str);
      if (width > len && !left)
        for (int i = 0; i < width - len; i++)
          s->put(' ');
      emit(s, str, len);
      if (width > len && left)
        for (int i = 0; i < width - len; i++)
          s->put(' ');
      continue;
    }
    if (c == 'f') {
      emit_fp(s, va_arg(ap, double), prec);
      continue;
    }
    if (c == 'd' || c == 'i') {
      int64_t v = isll ? va_arg(ap, long long) : va_arg(ap, int);
      if (v < 0) {
        s->put('-');
        v = -v;
      }
      emit_uint(s, (uint64_t)v);
      continue;
    }
    if (c == 'u') {
      uint64_t v = isll ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
      emit_uint(s, v);
      continue;
    }
    if (c == 'x' || c == 'X') {
      emit_hex(s, va_arg(ap, unsigned int), c == 'X');
      continue;
    }
    if (c == 'c') {
      s->put((char)va_arg(ap, int));
      continue;
    }
    /* unknown: print literally */
    s->put('%');
    s->put(c);
  }
}

int printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vformat(&sink_uart, fmt, ap);
  va_end(ap);
  return 0;
}

int sprintf(char *buf, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  bufp = buf;
  bufcount = 0;
  vformat(&sink_buf, fmt, ap);
  va_end(ap);
  *bufp = '\0';
  return bufcount;
}

/* ---------- assert ---------- */

void assert(int expected, int actual, char *code) {
  if (expected == actual) {
    printf("%s => %d\n", code, actual);
  } else {
    printf("%s => %d expected but got %d\n", code, expected, actual);
    exit(1);
  }
}

/* ---------- fixture functions (mirror test/common) ---------- */

int ext1 = 5;
int *ext2 = &ext1;
int ext3 = 7;
int ext_fn1(int x) { return x; }
int ext_fn2(int x) { return x; }

int false_fn() { return 512; }
int true_fn() { return 513; }
int char_fn() { return (2 << 8) + 3; }
int short_fn() { return (2 << 16) + 5; }

int uchar_fn() { return (2 << 10) - 1 - 4; }
int ushort_fn() { return (2 << 20) - 1 - 7; }

int schar_fn() { return (2 << 10) - 1 - 4; }
int sshort_fn() { return (2 << 20) - 1 - 7; }

int add_all(int n, ...) {
  va_list ap;
  va_start(ap, n);

  int sum = 0;
  for (int i = 0; i < n; i++)
    sum += va_arg(ap, int);
  return sum;
}

float add_float(float x, float y) { return x + y; }

double add_double(double x, double y) { return x + y; }

/* ---------- entry ---------- */

__asm__(".section .text.start\n"
        ".globl _start\n"
        "_start:\n"
        "  la sp, _stack_top\n"
        /* qemu resets mstatus.FS to Off, which makes every FP
         * instruction trap as illegal; enable F/D state (FS=Dirty)
         * before any C code runs. */
        "  csrr t0, mstatus\n"
        "  li t1, 0x6000\n"
        "  or t0, t0, t1\n"
        "  csrw mstatus, t0\n"
        "  la t0, __bss_start\n"
        "  la t1, __bss_end\n"
        "1:\n"
        "  bgeu t0, t1, 2f\n"
        "  sw zero, 0(t0)\n"
        "  addi t0, t0, 4\n"
        "  j 1b\n"
        "2:\n"
        "  call main\n"
        "  li a0, 0x5555\n"
        "  li a1, 0x100000\n"
        "  sw a0, 0(a1)\n"
        "  j .\n");
