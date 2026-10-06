#!/usr/bin/env bash
# 生成 clang 的参考 IR：内建函数的权威契约。
#
# 用途：
#   1. 固化「C 内建 → LLVM 内建名/签名」的映射，尤其溢出内建的多值返回
#      `{ iN, i1 }` 与 extractvalue 结构（cxx 目前还没有多值返回表示）。
#   2. 后期脱离 LLVM 自行发射 IR → 汇编时，作为语义与类型信息的参考基准。
#   3. 与 cxx 输出的 IR 逐行对照（见 doc/builtin-redesign.md §7）。
#
# 用法：bash tools/builtin-ref-ir.sh [输出目录]
#   默认 /tmp/builtin-ref；每个片段产出 <名字>.c / <名字>.ll。
set -u

OUT=${1:-/tmp/builtin-ref}
CC=${CC:-clang}
rm -rf "$OUT"; mkdir -p "$OUT"

if ! command -v "$CC" > /dev/null 2>&1; then
    echo "找不到 C 编译器 '$CC'（可用 CC=... 覆盖）" >&2
    exit 1
fi

emit() {
    local name=$1 src=$2
    printf '%s\n' "$src" > "$OUT/$name.c"
    if ! "$CC" -std=c23 -O0 -S -emit-llvm -o "$OUT/$name.ll" "$OUT/$name.c" 2> "$OUT/$name.err"; then
        echo "### $name —— 编译失败："
        sed 's/^/    /' "$OUT/$name.err" | head -5
        return
    fi
    echo "=============================================================="
    echo "### $name"
    echo "--- 源码 ---"
    cat "$OUT/$name.c"
    echo "--- 函数体 ---"
    # 丢掉属性组/元数据/调试行，只留可读的 IR
    grep -vE '^\s*;|^\s*$|^attributes|^!|^target |^source_filename|^$' "$OUT/$name.ll" |
        awk '/^define/{p=1} p' | head -40
    echo "--- declare ---"
    grep -E '^declare' "$OUT/$name.ll" | grep -v 'llvm\.lifetime' | head -12
    echo
}

echo "############ 1. 位运算家族：返回类型与操作数类型无关 ############"
emit bswap 'unsigned short f16(unsigned short x){ return __builtin_bswap16(x); }
            unsigned int   f32(unsigned int x){ return __builtin_bswap32(x); }
            unsigned long  f64(unsigned long x){ return __builtin_bswap64(x); }'

emit count 'int a(unsigned int x){ return __builtin_clz(x); }
            int b(unsigned int x){ return __builtin_ctz(x); }
            int c(unsigned int x){ return __builtin_popcount(x); }
            int d(unsigned long x){ return __builtin_clzl(x); }
            int e(unsigned long x){ return __builtin_ctzl(x); }
            int f(unsigned long x){ return __builtin_popcountl(x); }'

emit count_ll 'int a(unsigned long long x){ return __builtin_clzll(x); }
               int b(unsigned long long x){ return __builtin_ctzll(x); }
               int c(unsigned long long x){ return __builtin_popcountll(x); }'

emit misc 'int a(int x){ return __builtin_ffs(x); }
           int b(long x){ return __builtin_ffsl(x); }
           int c(int x){ return __builtin_parity(x); }
           int d(int x){ return __builtin_clrsb(x); }'

echo "############ 2. 溢出内建：注意 { iN, i1 } 多值返回与 extractvalue ############"
emit ovf_add_s 'int f(int a, int b, int *r){ return __builtin_add_overflow(a, b, r); }'
emit ovf_add_u 'int f(unsigned a, unsigned b, unsigned *r){ return __builtin_add_overflow(a, b, r); }'
emit ovf_narrow 'int f(signed char a, signed char b, signed char *r){ return __builtin_add_overflow(a, b, r); }
                 int g(short a, short b, short *r){ return __builtin_mul_overflow(a, b, r); }'
emit ovf_wide 'int f(__int128 a, __int128 b, __int128 *r){ return __builtin_sub_overflow(a, b, r); }'
emit ovf_long 'int f(long a, long b, long *r){ return __builtin_add_overflow(a, b, r); }
               int g(unsigned long a, unsigned long b, unsigned long *r){ return __builtin_mul_overflow(a, b, r); }'

echo "############ 3. 前端折叠程度：哪些内建在 IR 里根本不出现 ############"
emit constfold 'unsigned int a(void){ return __builtin_bswap32(0x11223344u); }
                int b(void){ return __builtin_clz(1u); }
                int c(void){ return __builtin_popcount(0xFFu); }
                int d(void){ int r; return __builtin_add_overflow(2147483647, 1, &r); }'

echo "############ 4. B 类对照：只能由编译器实现的内建 ############"
emit special 'int a(void){ return __builtin_constant_p(1+2); }
              int b(void){ return __builtin_types_compatible_p(int, int); }
              void *c(unsigned long n){ return __builtin_alloca(n); }
              void *d(unsigned long n){ return __builtin_alloca_with_align(n, 64); }'

echo "############ 5. 内建结果进入变参位置：默认实参提升 ############"
emit promote 'int printf(const char *, ...);
              int g(unsigned short x){ return printf("%u", __builtin_bswap16(x)); }
              int h(signed char x){ return printf("%d", __builtin_popcount(x)); }'

echo
echo "产物目录：$OUT"
ls -1 "$OUT"/*.ll 2>/dev/null | wc -l | sed 's/^/生成 .ll 文件数：/'
