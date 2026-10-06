# cxx 对 N3685（ISO/IEC 9899:202y 工作草案）的实现对照清单

- **基线**：`dev` 分支 commit `10add85`（§1.1–§1.3 已由此提交纳入），
  工作区另含 §1.4 与 P7 的改动（`src/dumpir.c`、`src/parser.c`、`Makefile`、`test/c2y.sh`）
- **对照基准**：`doc/n3685.txt` / `doc/n3685.pdf` = **ISO/IEC 9899:202y (C2y) 工作草案 N3685**
  - 前言列出 2024-01 / 2024-06 / 2024-09-10 / 2025-02 / 2025-08 五个会期整合的全部提案，共 **79 项**
  - 草案含 **C23 全文 + C2y 增量**，清单按这两层组织，但统一以 N3685 的条款号为准
- **方法**：从四个维度建立清单，全部**行为实测**（`cxx -w -c` / `-S` / 编译+链接+运行），
  源码位置与测试覆盖作交叉证据

| 维度 | 来源 | 条目数 |
|---|---|---|
| 前言提案 | Abstract 页 i–iii | 79 |
| 文法 | Annex A（`n3685.txt:37320–38465`） | 全覆盖比对 |
| 关键字 | 6.4.2 表 + Table 6.1 替代拼写 | 54 + 5 |
| 标准头文件 | 7.1.2 清单 | 33 |

- **规模**：五轮探针共 **183 项**判定 + 关键字/头文件两项全量自动比对
- **证据分级**：`实测` = 编译/运行验证；`源码` = 代码位置确认；`覆盖` = 现有测试是否保护

> **结论速览**
>
> 语言核心覆盖度**很高**：C2y 的 54 个关键字里 cxx 实现 50 个，缺的 4 个正是明确不做计划的
> `_Complex` 与 `_Decimal32/64/128`；Table 6.1 的 5 个替代拼写全有；33 个标准头 30 个可包含。
> 本轮实测 183 项中，**真实缺口只有 5 个**（§2、§3），其余差异都能归入「有意不支持」「宿主库版本」
> 或「已自证的探针缺陷」。
>
> 但本轮**发现并修复了 4 个真实缺陷**，其中两个是静默算错/拒绝合法代码，此前 `make test`
> 全绿完全掩盖了它们（§10）。**最大且未修的缺口是 `<math.h>`：头文件能包含，但 `NAN`、
> `INFINITY`、`HUGE_VAL`、`isnan`、`fpclassify` 等全部宏因缺少 math 内建而不可用。**
>
> 这四个缺陷的类别已固化为 `test/c2y.sh` 并纳入 `make test`（P7），
> 与 `test/conformance.sh`、`test/fold.c`、`test/control.c` 共同构成回归网。

---

## 0. 基线

| 套件 | 结果 |
|---|---|
| `make`（clang 23.1.2, `-Wall -Wextra -Werror -std=c23`） | ✅ 通过 |
| `make test`（52 个测试程序 + 4 个脚本） | ✅ 全绿；`conformance.sh` 73 passed, 3 known gap |
| `make test-arm64`（qemu） | ✅ 51 passed, 0 failed |
| `make test-rv64`（qemu） | ✅ 51 passed, 0 failed |
| `make test-rv32`（bare-metal） | ✅ 51 passed, 0 failed, 1 skipped |

编译器自述：`__STDC_VERSION__ = 202311L`、`__STDC_HOSTED__ = 1`、`__STDC_NO_COMPLEX__ = 1`；
定义 `__GNUC__`，**未**定义 `__clang__`（§3.5）。

---

## 1. 本轮发现并修复的缺陷（4 项）

### 1.1 `&&` / `||` 常量折叠极性反了 —— 静默算错

```c
int f(int x) { return 1 && x; }    /* cxx 曾返回 !x；应为 !!x */
```

`src/opt_ast.c` 的 `fold_logical()` 在左操作数是常量、右操作数不是时返回 `new_lognot(rhs)`。
但 6.5.14 / 6.5.15 规定结果是**与右操作数真值相同**的 int（0 或 1），所以应为 `!!x`。实测
走反的例子（左 cxx / 右 clang）：

```
0 || p                           0 / 1        p 为 void*
true && (q == 0)                 0 / 1        q 为 void*
('A'=='A') && ("xy"[1]=='y')     0 / 1
1 && half                        0 / 1        half 为 double 0.5
```

**为何长期未暴露**：两侧都是常量时走的是另一条分支（直接把合取结果折叠成常量），
只有「常量 && 变量」才进入这条路径；而测试里的 `&&` 两侧几乎总是一起出现。

**修复**：折叠为 `x != 0`（而不是简单的双重否定 `!!x`）。两者语义相同，但比较形式是
6.5.14 / 6.5.15 的字面表述，且与 parser 对**写出来的** `x != 0` 构建的树完全一致
（`ND_NE(LVTOR(x), IMCAST(ND_NUM 0):T)`，零常量经操作数自身类型转换——这正是指针得
`null`、浮点得 `0.0` 的原因），因此折叠后与源码形式不可区分。IR 从 4 条降到 2 条：

```
1 && x  →  icmp ne i32 %x, 0   +  zext i1 to i32     （clang -O0 逐字节相同）
!!x     →  icmp eq i32 %x, 0   +  zext
           icmp eq i32 %t, 0   +  zext
```

`test/fold.c` 增 18 项回归（int / double / 指针 / 常量左侧死代码 / 右结合链）。
（指针操作数目前多一条可折叠的 `inttoptr i32 0 to ptr`，与源码形式 `p != 0` 的既有
行为一致，见 `cxx-c2y-plan.md` T1。）

### 1.2 短路右操作数内含 `?:`、`&&`、`||` 时生成非法 LLVM IR

```c
int main(void) { int a = 0; return (!a && (a ? 0 : 1)) ? 0 : 1; }
```
```
error: invalid LLVM IR input: PHI node entries do not match predecessors!
  %tmp14 = phi i32 [ 0, %blk2 ], [ %tmp13, %blk3 ]
label %blk3
```

`src/irgen.c` 的 `gen_logand` / `gen_logor` 把合并块的 PHI 前驱写成自己为右操作数创建的
`t_blk` / `f_blk`。但右操作数若**自身开新块**（嵌套条件表达式或短路运算符），实际汇入
合并块的是它结束时的那个块。实测命中：`x && (y && z)`、`a || (b ? c : d)`、
`a && (b ? 1 : 0)`；不命中：`!a && (a == 0)`（右操作数不产生块）。

**修复**：在右操作数生成之后捕获 `curb` 作为 `rhs_end`，PHI 引用它。`test/control.c`
增 14 项回归。

### 1.3 `-lm` 链接顺序错误 —— libm 完全不可用

```
$ cxx -w -lm -o a a.c
/usr/bin/ld: a.c:(.text+0x12): undefined reference to `sqrt'
```

`src/main.c` 把 `-l` / `-Wl,` 收进 `input_paths`，回放时排在**目标文件之前**：
`cc -o out -lm tmp.o`。`ld` 只在「已有未定义符号」时才从静态库取成员，所以 `-lm` 成了
空操作。**修复**：改收进 `ld_extra_args`（`run_linker` 在目标文件之后追加）。
`test/conformance.sh` 增端到端回归。

### 1.4 反转的 `case` 范围：不诊断 6.6.2，且随即生成非法 LLVM IR

```c
int f(int c) { switch (c) { case 9 ... 1: return 1; default: return 0; } }
```
```
$ cxx -w -S -o /dev/null r.c
/tmp/cxx-XXXX:22:1: error: expected '[' with switch table
```

6.6.2 规定「第一个常量表达式的值应小于或等于第二个」——这是**约束**，必须诊断。cxx 静默
接受，随后 `src/dumpir.c` 在 case 表为空时**连方括号一起省略**，输出
`switch i32 %x, label %blk3`；LLVM 的 switch 语法要求即使分支表为空也必须写 `[ ]`。

**修复两处**：`dumpir.c` 恒定输出方括号对；`parser.c` 对 `val2 < val1` 发出
`warning: empty case range specified`（与 clang 同措辞）。gcc 与 clang 都只警告并继续
（该 case 什么也不匹配），所以是警告而非错误。

**为何长期未暴露**：既有 `test/ir.sh` 没有「空分支表的 switch」用例；而只跑到 IR 就返回的
路径（`-emit-llvm`）不经过 clang 汇编器，错误只在完整流水线上出现 —— 本轮的探针
`rej "case range with the bounds reversed"` 恰好用了完整流水线，所以它「通过」了，
但通过的原因是下游报错，而不是 cxx 自己诊断。这正是 `test/c2y.sh` 要固定的那类假通过。

---

## 2. 未修复的真实缺陷（2 项）
### 2.1 `_Atomic` 聚合体的泛型原子函数生成非法 IR

```c
#include <stdatomic.h>
struct S { int a; };
int f(void) { _Atomic struct S v; return atomic_load(&v).a; }
```
```
error: '%tmp7' defined with type '%struct.S = type { i32 }' but expected 'ptr'
```

`atomic_load` / `atomic_store` / `atomic_exchange` 对 `_Atomic struct` 全部触发；
`_Atomic int` 正常；普通结构体返回值的成员访问（`mk().a`、复合字面量）也正常。
IR 显示原子读被降级成标量 `load atomic i32`，随后成员访问把**结构体值**当成指针做
`getelementptr`。`test/atomic.c` 未覆盖整块聚合体的原子读写。

### 2.2 N3652 复合类型：数组类型不能由后续声明补全

```c
int a[];
int a[10];
int main(void) { return sizeof(a) == 40 ? 0 : 1; }   /* clang: 40；cxx 报错 */
```
```
error: invalid application of ‘sizeof’ to incomplete type
```

这是 **C89 起就成立**的规则（6.9.2 tentative definition + 6.2.7 复合类型），非 C2y 新增。
`static int b[]; static int b[4] = {...};` 可以（补全发生在同一族声明内），但两条独立声明
「先不完整、后完整」不行。结构体/函数类型的复合正常。
（J.2 把「由不完整 VLA 形成复合类型」列为 UB，与普通数组无关。）

---

## 3. 编译器缺口（合法 C2y 程序被拒）

### 3.1 ★ 最大缺口：`<math.h>` 的宏全部不可用

`<math.h>` 可以包含，但 glibc 把这些宏展开成 cxx **完全不存在**的 builtin：

| 宏 | 依赖的 builtin | 状态 |
|---|---|---|
| `NAN` / `NANF`… | `__builtin_nanf` / `__builtin_nan` / `__builtin_nans*` | ❌ |
| `INFINITY` | `__builtin_inff` / `__builtin_inf` | ❌ |
| `HUGE_VAL` `HUGE_VALF` `HUGE_VALL`（N3303） | `__builtin_huge_val{,f,l}` | ❌ |
| `isnan` `isinf` `isfinite` `signbit` | `__builtin_isnan` … `__builtin_signbit` | ❌ |
| `fpclassify` `isnormal` | `__builtin_fpclassify` | ❌ |
| `isgreater` `isless` `isunordered` … | `__builtin_isgreater` … | ❌ |

**进展（P1 三步全部落地，见 `cxx-c2y-plan.md` P1）**：常量产出族
（`__builtin_huge_val{,f,l}`、`__builtin_inf{,f,l}`、`__builtin_nan{,f,l}`）、
比较族（`isgreater` 等六个）、分类族（`isnan`、`isinf`、`__builtin_isinf_sign`、
`isfinite`、`isnormal`、`fpclassify`、`signbit`）均已实现。因此上表**全部七行已转绿**，
N3303 也随之落地。实测：

- 常量位型与 clang 逐位相同（`0x7ff8…` 静默 NaN、`0x7ff0…` +Inf、`0xK7fffc0…` x87）
- 六个比较宏在 10 组操作数组合（含 ±NaN/±inf/±0）上与 clang **输出逐字节相同**
- 分类族在 24 组取值（含 ±0、±NaN、±inf、次正规、float/long double）上与 clang 逐行相同
- **副作用求值次数**：39 项计数与 clang 完全一致（`doc/effects.sh`），
  含「`0 && f()` 不调用 f」「`1 || f()` 不调用 f」「`sizeof`/`_Generic`/`typeof`
  不求值」「每个比较宏的操作数恰好求值一次」

仍未做的只剩 **P1b 信号 NaN**（`SNAN*`，受库层限制，见下）与
**P1c x87 次正规常量**（本轮新发现的既有缺陷，见 §2.3）。

cxx 原有内建 32 个：`alloca`、`alloca_with_align`、`constant_p`、
`types_compatible_p`、`bswap16/32/64`、`clz/ctz/popcount`（+`l`/`ll`）、
`va_start/va_arg/va_end/va_copy`、`ffs/parity/clrsb`（+`l`/`ll`）、
`add/sub/mul_overflow`；P1 三步共加了 22 个。

**同样缺失的另一族**（真实代码与系统头常用）：

`__builtin_expect`、`__builtin_unreachable`、`__builtin_trap`、`__builtin_offsetof`、
`__builtin_object_size`、`__builtin_memcpy`、`__builtin_strlen`、`__builtin_abs`。

**最小实现面**：`nan`/`nans`/`huge_val`/`inf` 四族（折叠为对应格式的浮点常量）+ 
`fpclassify`/`isgreater`…/`signbit`（比较与分类），约 10 个内建即可让上表全部转绿。

### 3.2 N3348：泛型关联中的 `[*]` 未定长数组类型

```c
_Generic(int[3][2], int[3][*]: 1, int[2][*]: 0)     /* 6.5.2.1 EXAMPLE 3 */
```
```
error: [*] used outside of function prototype
```

6.7.7.1¶4 把 `[*]` 的允许位置从「函数原型作用域」扩展到「**泛型关联的 type-name**」。
**clang 23 同样拒绝**（错误措辞为 `star modifier used outside of function prototype`），
属整个生态尚未落地的 C2y 新特性；cxx 与 clang 行为一致。

### 3.3 N3366 / 7.26 `<stdmchar.h>` 完全未实现

草案 7.1.2 的标准头清单**包含** `<stdmchar.h>`，7.26.14 规定
`__STDC_VERSION_STDMCHAR_H__` 的值形如 `202ymmL` —— 它是 **C2y 头**，不是 C23 头。
按章节内容（transcoding、indivisible unit of work、取代 7.32/7.33 的转换工具）与提案名
对应，**N3366「Restartable Functions for Efficient Character Conversions」的落地形态就是
这个头**，因此 §6 提案表里 N3366 记为整项未实现，而不只是一个头文件缺失。

cxx 自带 7 个头（`float.h` `stdalign.h` `stdarg.h` `stdatomic.h` `stdbool.h` `stddef.h`
`stdnoreturn.h`），不含此头；宿主 glibc 也没有。这是最大的一块库缺口。

### 3.4 驱动缺口

| 项 | 实测 |
|---|---|
| `-pthread` | `fatal error: unknown argument` —— 使用线程的程序无法编译 |
| `-l` / `-Wl,` 位置 | 已修，见 §1.3 |

### 3.5 一致性提示：`_FloatN` 是关键字，但未声明 `__STDC_IEC_60559_TYPES__`

实测 `_Float16/32/64/128` 是关键字，而 `__STDC_IEC_60559_TYPES__`、`__STDC_IEC_60559_BFP__`、
`__STDC_IEC_559__` **都未定义**。H.5.1 把这些标识符的关键字身份**以 `__STDC_IEC_60559_TYPES__`
为条件**。目前不与 glibc 的 `typedef float _Float32;` 冲突，是因为驱动改为定义 `__GNUC__`
（实测 `__GNUC__` defined、`__clang__` **not defined**），glibc 走了 GCC 分支 —— 即现状靠
兼容宏绕开，而不是靠宏与关键字自洽。建议明确二者关系。

---

## 4. 范围声明：有意不支持（不计为缺陷）

按项目决定，以下**不在实现计划内**：

- **复数**：`_Complex`、`_Imaginary`（N3274 已从 C2y 关键字表删除，cxx 正确地不认识它）、
  复数字面量 `2.0i`（N3298 / N3500）、`++`/`--` 用于复数（N3259）、复数运算符（N3460）、
  `creal`/`cimag` 措辞（N3598）
  → 连带 `<complex.h>`、`<tgmath.h>` 不可用
- **十进制浮点**：`_Decimal32` / `_Decimal64` / `_Decimal128`（含 N3291 术语、N3305 `WANT_`
  宏、N3492 舍入错误条件）

共涉及 **8 个提案 + 2 个头文件**。

> 合规性说明：7.1.2 脚注 225 明确 `<complex.h>`、`<stdatomic.h>`、`<threads.h>` 是
> **条件特性**，实现可以不支持；cxx 已自述 `__STDC_NO_COMPLEX__ = 1`。因此这不是缺陷。

---

## 5. 覆盖清单（实测通过的部分）

### 5.1 词法元素（6.4）

| 特性 | 条款 | 状态 |
|---|---|---|
| `0o17` / `0O17` 前缀八进制字面量 | 6.4.5.2 `prefixed-octal-literal`（N3353） | ✅ |
| `017` 无前缀八进制仍可用（已过时） | 6.4.5.2 | ✅ |
| `\o{101}` 八进制转义 | 6.4.5.5（N3353） | ✅ |
| `\x{41}` 带花括号十六进制转义 | 6.4.5.5（N3192） | ✅ |
| `\u{41}` / `\U{00000041}` 带花括号 UCN | 6.4.4 | ✅ |
| `\u{1F600}` 多字节 UCN 字符串 | 6.4.4 | ✅ |
| `wb` / `WB` / `uwb` / `UWB` 位精确整数后缀 | 6.4.5.2 `bit-precise-int-suffix` | ✅ |
| 后缀选取最小的 `_BitInt(N)` 宽度（`2025wb` → 2 字节） | 6.4.5.2 | ✅ |
| 数字分隔符 `1'000'000` / `0xFF'FF` / `0b1010'1010` | 6.4.5.2 | ✅ |
| `true` / `false` / `nullptr` 作 `predefined-constant` | 6.4.5.6（N3239） | ✅ |
| `_Float16/32/64/128` 类型说明符 + `f16/f32/f64/f128` 后缀 | H.5.1 | ✅ |
| `u8'x'` `u8""` `u""` `U""` `L""` | 6.4.5.5 / 6.4.6 | ✅ |
| `char8_t` 由 `<uchar.h>` 提供（**不是关键字**） | 6.4.2 未列，7.32 | ✅ |
| `::` 记号（属性前缀 / pp 参数前缀） | 6.4.7 | ✅ |

### 5.2 表达式与运算符（6.5）

| 特性 | 条款 | 状态 |
|---|---|---|
| `case 1 ... 9` 范围 | 6.6.2 / 6.8.2（N3370） | ✅ |
| `_Countof` 作用于数组对象 | 6.5.4.1（N3369→N3469） | ✅ |
| `_Countof(type-name)` 形式 | 6.5.4.1 | ✅ |
| `_Lengthof` 已不再是关键字（正确移除） | 6.4.2 | ✅ |
| `_Generic` 的**类型操作数** | 6.5.2.1（N3260） | ✅ |
| `_Generic` 数组操作数按 6.5.2.1¶2 退化为指针 | 6.5.2.1 | ✅ |
| `alignof(int[])` 不完整数组类型 | 6.5.4.1（N3273） | ✅ |
| `&a[3]` 单元素越界取址 | 6.5.3.2（N3517） | ✅ |
| 非左值数组成员下标（`mk().a[1]`、复合字面量） | 6.5.3.2（N3517） | ✅ |
| `bool` ← 浮点值 | 6.3.1.2（N3323） | ✅ |
| 复合字面量带存储类 `(static struct S){...}` | 6.5.3.6 | ✅ |
| `constexpr` 复合字面量 | 6.5.3.6 | ✅ |
| `sizeof` / `_Alignof` / `_Countof` / `typeof` / `typeof_unqual` | 6.5.4.1 | ✅ |

### 5.3 声明与语句（6.7 / 6.8）

| 特性 | 条款 | 状态 |
|---|---|---|
| 枚举固定底层类型 `enum E : unsigned char` | 6.7.3.3 | ✅ |
| `alignas` 进入 `specifier-qualifier-list`（枚举与结构体成员） | 6.7.3.2 | ✅ |
| 不完整结构体成员访问被拒绝 | 6.5.2.3（N3532） | ✅ |
| `_Atomic ( type-name )` | 6.7.3.5 | ✅ |
| `_Atomic int` 对齐不再被抬高 | 6.2.8（N3312） | ✅ |
| `[[...]];` 属性声明 | 6.7.1 `attribute-declaration` | ✅ |
| 属性位于标签 / 语句 / 结构体成员 / 枚举常量 / 指针声明符 / 直接声明符 / 结构体 tag / case 标签 / 函数定义 | Annex A 各处 | ✅ |
| `[[gnu::...]]` 厂商前缀属性 | 6.7.13.2 `attribute-prefixed-token` | ✅ |
| `if (int x = f(); ...)` / `switch (int x = f(); x)` | 6.8.5.1 `simple-declaration`（N3356） | ✅ |
| 具名 `break` / `continue` 到循环标签 | 6.8.7.1（N3355） | ✅ |
| 具名 `break` 到 `switch` 标签 | 6.8.7.1 | ✅ |
| 空初始化 `{}` 与匿名成员初始化 | 6.7.11（N3451） | ✅ |
| `static_assert` 无消息 / 结构体内 | 6.7.12 | ✅ |
| `register` 分类与「不得取址」约束 | 6.7.2（N3544） | ✅ |
| `_Thread_local` / `thread_local` / `__thread` | 6.7.2 | ✅ |
| `_Noreturn` / `noreturn`（后者需 `<stdnoreturn.h>`） | 6.7.5 | ✅ |
| `auto` 类型推导 | 6.7.2 / 6.7.9 | ✅ |
| `main` 的合法形式（`int main(void)` / `int main(int, char**)` / 落空返回 0） | 5.2.2.3.2（N3623） | ✅ |
| 块作用域函数声明只能是 `extern` | 6.7.2 | ✅ 拒绝 |
| 数组参数的 `static` / `const` 限定 | 6.7.7.1 | ✅ |

### 5.4 预处理（6.10）

`#elifdef` / `#elifndef` · `#warning` · `__COUNTER__`（N3457）· `#if` 中的 `'` 分隔符（N3505）·
`__has_include` / `__has_include_next` / `__has_c_attribute` / `__has_attribute` / `__has_builtin` ·
`__VA_OPT__` · `#` / `##` · `_Pragma` · `#embed` 的 `limit` / `prefix` / `suffix` / `if_empty` ·
`__has_embed` 与 `__STDC_EMBED_FOUND__`(=1) / `EMPTY__`(=2) / `NOT_FOUND__`(=0) ·
`-embed-dir` · `#pragma STDC FENV_ACCESS` / `FENV_ROUND`（含 C2y 的 `FE_TONEARESTFROMZERO`、
`FE_DYNAMIC`）/ `FP_CONTRACT`

> `#embed "f" gnu::offset(1)` 被拒 —— **与 clang 23 一致**，6.10.4.1¶4 把非标准参数完全
> 交给实现，拒绝是合规的。

### 5.5 常量表达式（6.6）

N3447 / N3459 / N3558 所要求的各类整数常量表达式全部可用：算术、`sizeof` / `_Alignof` /
`_Countof`、枚举常量、转换、`_BitInt` 与 `_FloatN` 操作数、地址常量、字符串字面量下标、
`const` 对象读取、已知常量大小对象。`static_assert` 失败与 `case 9 ... 1` 逆序范围
都正确报错。

### 5.6 关键字（6.4.2 + Table 6.1）

**自动比对结果**（脚本 `kw.sh`：从 `n3685.txt:5003-5015` 取草案关键字表，与
`src/lexer.c` 的关键字表求差集）：

| | 数量 |
|---|---|
| 草案 6.4.2 关键字 | 54 |
| cxx 关键字表 | 67 |
| **草案有、cxx 缺** | **4**：`_Complex` `_Decimal32` `_Decimal64` `_Decimal128` |
| cxx 有、草案没有（Annex H + GNU 扩展） | `_Float16/32/64/128`；`asm` `__asm` `__asm__` `__attribute__` `__extension__` `__restrict` `__restrict__` `__thread` |
| Table 6.1 替代拼写（5 个） | 全部具备：`_Alignas` `_Alignof` `_Bool` `_Static_assert` `_Thread_local` |

**即：除 4 个明确不做计划的关键字外，C2y 关键字表 100% 覆盖。**

### 5.7 标准头文件（7.1.2，共 33 个）

判定方式：`#include <X>` + 空 `main`，用 cxx 编译。

| 可用（30） |
|---|
| `assert.h` `ctype.h` `errno.h` `fenv.h` `float.h` `inttypes.h` `iso646.h` `limits.h` `locale.h` `math.h`※ `setjmp.h` `signal.h` `stdalign.h` `stdarg.h` `stdatomic.h` `stdbit.h` `stdbool.h` `stdckdint.h` `stdcountof.h` `stddef.h` `stdint.h` `stdio.h` `stdlib.h` `stdnoreturn.h` `string.h` `threads.h` `time.h` `uchar.h` `wchar.h` `wctype.h` |

| 不可用（3） | 原因 |
|---|---|
| `complex.h` `tgmath.h` | 复数**有意不支持**（§4） |
| `stdmchar.h` | **真实缺口**（§3.3） |

※ `<math.h>` 能包含但功能不可用 —— 见 §3.1。

**不属于 cxx 实现的**（由宿主或 clang 23 内置目录提供）：`stdbit.h` `stdckdint.h`
`stdcountof.h` `uchar.h`（glibc / clang 资源目录）。cxx 自带头只有 7 个。

### 5.8 预定义特性宏

| 宏 | 实测 |
|---|---|
| `__STDC_VERSION__` | `202311L`（见下） |
| `__STDC_HOSTED__` | `1` |
| `__STDC_NO_COMPLEX__` | `1` ✅ 与「复数不做」一致 |
| `__STDC_NO_THREADS__` / `__STDC_NO_ATOMICS__` / `__STDC_NO_VLA__` | 未定义 ✅（三者都支持） |
| `__STDC_IEC_60559_TYPES__` / `_BFP__` / `_DFP__` / `__STDC_IEC_559__` | 未定义 ⚠️ 见 §3.5 |
| `__STDC_NO_DECIMAL_FLOAT__` / `_DECIMAL128_FLOAT__` | 未定义 —— 十进制不支持时**应当**定义为 1，建议补 |
| `__STDC_EMBED_FOUND__` / `EMPTY__` / `NOT_FOUND__` | 1 / 2 / 0 ✅ |
| `__GNUC__` / `__clang__` | defined / **not** defined |

> `__STDC_VERSION__` 仍是 C23 的 `202311L`。N3685 是 C2y 草案，其值形如 `202ymmL`，
> 具体取值要等定稿；在实现已提供 C2y 特性的情况下，把这个值留在 C23 是可辩护的，
> 但若要对外声明 C2y 支持则应更新。

---

## 6. 前言提案逐项状态

`✅` 已实现 · `❌` 未实现 · `n/a` 与本实现无关（库措辞/编辑性修改/有意不支持）

### 2024-01

| 提案 | 状态 | 说明 |
|---|---|---|
| N3192 Sequential hexdigits | ✅ | `\x{41}` |
| （编辑性：Annex K 脚注） | n/a | |

### 2024-06

| 提案 | 状态 | 说明 |
|---|---|---|
| N3064 Writing to multibyte character files | n/a | 库 |
| N3232 Round-trip rounding | ✅ | `FE_TONEARESTFROMZERO` 编译通过 |
| N3233 printf rounding | n/a | 库 |
| N3239 Some constants are literally literals, v2 | ✅ | `typeof` 各常量形式、`true`/`false`/`nullptr` 字面量 |
| N3242 "correctly rounded" | n/a | 措辞 |
| N3244 Slay Some Earthly Demons I | ✅ | 6.3.2.1 / 6.7.2 / 6.7 / 6.7.5 / 6.7.6 约束实测符合 |
| N3247 fopen `p` | n/a | 库 |
| N3254 Accessing byte arrays, v4 | ✅ | 字节数组访问语义 |
| N3259 `++`/`--` on complex | n/a | 复数（§4） |
| N3260 `_Generic` with a type operand | ✅ | |
| N3273 `alignof` of an incomplete array type | ✅ | |
| N3274 Remove imaginary types | ✅ | 正确不认识 `_Imaginary` |

### 2024-09 / 10

| 提案 | 状态 | 说明 |
|---|---|---|
| N3272 strftime | n/a | 库 |
| N3286 FP exception for macro replacements | n/a | 库措辞 |
| N3287 / N3324 / N3340–N3346 / N3347 / N3484 | n/a | Slay Some Earthly Demons 系列，措辞与去重 |
| N3291 / N3298 / N3305 | n/a | 十进制 / 复数（§4） |
| N3303 HUGE_VAL corrections | ❌ | 受 §3.1 阻塞（`__builtin_huge_val*` 缺失） |
| N3312 Relax atomic alignment requirements | ✅ | `_Alignof(_Atomic T) == _Alignof(T)` |
| N3322 zero-length ops on null pointers | ❌ | 宿主 glibc 无 `free_sized` 等，见 §7 |
| N3323 How do you add one to something? | ✅ | `bool` ← 浮点 |
| N3326 strnlen / wcsnlen | ✅ | 宿主 glibc 提供 |
| N3349 abs without UB | ✅ | |
| N3353 Obsolete octal + new escapes | ✅ | `0o17`、`\o{101}`、`\u{...}` |
| N3355 Named / labeled loops | ✅ | |
| N3356 `if` declarations | ✅ | 亦含 `switch` 的 `simple-declaration` |
| N3364 SNAN initialization | ❌ | 受 §3.1 阻塞（`__builtin_nanf` 缺失） |
| N3366 Restartable char conversions | ❌ | **即 `<stdmchar.h>`，整项未实现**（§3.3） |
| N3367 More modern bit utilities | ✅ | `<stdbit.h>` 功能实测正确 |
| N3369 `_Lengthof` operator | ✅ | 以 C2y 名 `_Countof` 落地 |
| N3370 Case ranges | ✅ | |
| N3461 range error definition | n/a | 库 |

### 2025-02

| 提案 | 状态 | 说明 |
|---|---|---|
| N3363 `<stdarg.h>` wording | ✅ | `va_start` 单参形式、裸 `...` 形参 |
| N3401 SIGFPE and I/O / N3405 / N3409–N3411 / N3418 / N3478 / N3481 / N3482 / N3492 / N3496 | n/a | 库措辞与错误条件 |
| N3447 Chasing Ghost I — Constant Expressions | ✅ | |
| N3448 Chasing Ghosts II — Allocated Storage | n/a | 库/UB 措辞 |
| N3451 Anonymous struct/union initialization | ✅ | |
| N3452 Complex literals warning | n/a | 复数（§4） |
| N3459 Integer and arithmetic constant expressions | ✅ | |
| N3460 Complex operators | n/a | 复数（§4） |
| N3466 Null pointers in the library | n/a | 库 |
| N3469 Big array size survey | ✅ | `_Countof` + `<stdcountof.h>` 的 `countof()` |
| N3505 Preprocessor integer expressions, II | ✅ | |

### 2025-08

| 提案 | 状态 | 说明 |
|---|---|---|
| N3348 Multi-dim arrays in generic selection | ❌ | `[*]` 型名被拒；**clang 23 同样拒绝**（§3.2） |
| N3457 `__COUNTER__` | ✅ | |
| N3500 / N3536 / N3537 / N3598 | n/a | 复数（§4） |
| N3511 Remove "category" | n/a | 措辞 |
| N3517 Array subscripting without decay | ✅ | 越界取址、非左值数组下标 |
| N3525 `static_assert` without UB | ✅ | |
| N3532 Member access of an incomplete struct | ✅ | 正确拒绝 |
| N3535 frexp and double-double | n/a | 库 |
| N3544 Classification of `register` | ✅ | |
| N3558 Constant expressions / known constant size | ✅ | |
| N3563 Representation of pointers and `nullptr_t` | ✅ | 比较、转换、`_Generic` 选中 `nullptr_t` |
| N3577 Rename `uimaxabs` to `umaxabs` | ❌ | 宿主 glibc 无该符号（§7） |
| N3623 Slay Some Earthly Demons XIV: Definition of Main | ✅ | 合法形式通过；非标准形参型别按 J.2 属 UB，不要求诊断（cxx 不诊断，与标准一致） |
| N3652 Composite types v1.3 | ❌ | 数组复合类型不支持（§2.2） |

**汇总**：79 项中，编译器行为可观测的 **30 项**里 ✅ 27 / ❌ 3；其余 49 项为库措辞、
编辑性修改或有意不支持。

---

## 7. 宿主 C 库缺口（**不是**编译器缺陷）

| 项 | 症状 | 判定依据 |
|---|---|---|
| `free_sized` / `free_aligned_sized`（N3322） | `implicit declaration` | glibc 2.39 的 `<stdlib.h>` 无声明、`libc.so.6` 无符号；**clang 23 同样失败** |
| `memalignment`（N3322） | 同上 | 同上 |
| `umaxabs`（N3577） | 同上 | 同上 |

cxx 不自带这些头，用的是宿主的；宿主版本不含新符号时，与编译器实现度无关。

---

## 8. 探针自证：实测确认「**不是**缺口」的项

记录在此以免重复调查：

| 怀疑 | 实测结论 |
|---|---|
| `noreturn` 关键字缺失 | 需 `#include <stdnoreturn.h>`（C23 起该头定义宏）；包含后通过 |
| `char8_t` 未内置 | 6.4.2 **未**把 `char8_t` 列为关键字，它是 `<uchar.h>` 的 typedef（7.32）；包含后通过 |
| `u8'x'` 不支持 | 只是探针漏了头文件 |
| `_Generic(a, int[3]: …, int*: …)` 选错 | 6.5.2.1¶2 规定操作数经受**数组到指针转换**，选 `int*` 是**正确**的（clang 相同） |
| `1 = 2` 被拒 | 正确（左值要求） |
| 块作用域 `static int f(void);` 被拒 | 正确（6.7.2 只允许 `extern`） |
| `[[maybe_unused]] 1 + 1;` 被拒 | **clang 23 报完全相同的错误**（属性不能用于语句） |
| 参数上的 `alignas(8)` 被拒 | 同上（clang：`'alignas' attribute cannot be applied to a function parameter`） |
| `struct T {…} [[gnu::aligned(8)]];` 被接受 | cxx 比 clang 宽松（clang 拒绝），不影响正确性 |
| `#embed "f" gnu::offset(1)` 被拒 | clang 23 同样拒绝（§5.4） |
| `[[deprecated]] struct T {…};` 被接受 | clang 拒绝，cxx 更宽松，不影响正确性 |
| `_BitInt(>64)` 在 arm64 的对齐是 16 而非 8 | LLVM `105dd60c` 起 AArch64 改为 16，cxx 已对齐 clang 23 |
| `_Float32` 变参提升为 `double` | gcc 与 clang 本就意见相反；**cxx 与 clang 一致** |

---

## 9. 对本文档 v1 的更正

| v1 的结论 | 更正 | 依据 |
|---|---|---|
| 「`<stdmchar.h>` 不属于 C23，也不在 N3685 范围内」 | **错误**。它在 N3685 的 7.1.2 头文件清单里，正文章节为 **7.26**，`__STDC_VERSION_STDMCHAR_H__` 形如 `202ymmL` | `n3685.txt:16334`（清单）、`:30162`（7.26）、`:30255`（版本宏） |
| 「`__extension__` 未支持（`grep` 零命中）」 | 已支持，且在关键字表中 | `src/lexer.c` 关键字表；`test/conformance.sh` 有断言 |
| P0-1 `__va_area__` / P0-2 `_FloatN` 冲突 / P0-3 `#include_next` 自循环 / P1-1 `nullptr_t` | 四条**均已修复**，`test/conformance.sh` 有回归网 | `make test` 全绿 |
| 「结构体 ABI lowering 完全没做」 | 已实现（四个目标），本文档不再重复该历史 | `test/aggregate.c`、跨目标互链 |
| 「变参：除调用外均未完成」 | 已完成（`va_start`/`va_arg`/`va_end`/`va_copy`、聚合体与宽类型变参） | `test/varargs.c`、`test/aggregate.c` |
| 「cxx 已定义 `__STDC_IEC_60559_TYPES__`」 | **当前未定义**；且 `__GNUC__` 已定义、`__clang__` 未定义 | 实测 `#ifdef` |
| 「`make test` 全绿掩盖了全部 P0」 | 仍然成立，但掩盖的对象换了一批（§10） | |

---

## 10. 为什么全绿套件仍会掩盖缺口

本轮 3 个已修缺陷 + 2 个未修缺陷全部**穿过了** `make test` 全绿：

| # | 空洞 | 证据 |
|---|---|---|
| 1 | **`&&` 两侧同时是常量时不触发折叠缺陷** | `test/control.c` 的 `&&` 用例两侧都是常量或都是变量，恰好避开 `1 && x` 这条路径；`test/fold.c` 原先也没有「常量左侧 + 变量右侧」的组合 |
| 2 | **短路右操作数为嵌套 `?:`/`&&`/`||` 无覆盖** | `test/control.c:123-130` 的用例右操作数都是 `5`、`(2-2)` 这类不产生基本块的表达式 |
| 3 | **`-lm` 从无测试** | `test/conformance.sh` 曾测 `<math.h>` 能被包含，但不测链接；`test/common` 由 clang 编译，libm 依赖落在宿主工具链上 |
| 4 | **`<math.h>` 的宏无覆盖** | 全部 `test/*.c` 只做「能包含」判定，没有一处使用 `NAN`/`INFINITY`/`isnan` |
| 5 | **`_Atomic` 聚合体的整块 load/store 无覆盖** | `test/atomic.c` 覆盖标量原子类型；`test/aggregate.c` 覆盖普通聚合体；两者不相交 |
| 6 | **数组复合类型（跨声明补全）无覆盖** | `test/decl.c` 用单条声明完成数组定义 |
| 7 | **`-emit-llvm` 路径不经过 clang 汇编器** | 空 `case` 范围的非法 IR 只在完整流水线上暴露；探针用完整流水线时「通过」的原因是下游报错，不是 cxx 诊断（§1.4） |

**已落地（P7）**：`test/c2y.sh` 已建立并纳入 `make test` —— 覆盖 C2y 词法/表达式/声明/
预处理/常量表达式，加上「每个缺陷类别一条端到端断言」，并把 5 个未修缺口登记为
`known gap`（缺口一旦补上就报 `GAP CLOSED` 强制提升该条目）。

---

## 11. 验证方法（如何重跑本清单）

```bash
cd ~/cxx && make -j8                     # 先确认基线可构建
bash test/conformance.sh ./cxx           # 73 passed, 3 known gap
bash test/c2y.sh ./cxx                   # 48 passed, 5 known gap（P7 的 C2y 回归网）

# 本清单的探针全部归档在 doc/ 下，可直接重跑
bash doc/probes.sh ./cxx      # 一次跑完下面四轮 + 关键字比对
bash doc/c2ycov.sh ./cxx      # 111 项：词法/表达式/声明/预处理/常量/头文件/特性宏
bash doc/c2ycov2.sh           #  37 项：复合类型、存储类、库面、特性宏
bash doc/c2ycov3.sh           #  19 项：math 内建、属性、参数约束
bash doc/c2ycov4.sh           # 与 clang 逐条对照（无 pass/fail）
bash doc/c2ycov5.sh           #  16 项：字面量类型、nullptr_t、下标、原子
bash doc/kw.sh                # 草案关键字表 vs cxx 关键字表（全量自动比对）
bash doc/minbug.sh            # §2 两个非法 IR 的最小复现
bash doc/driver.sh            # 警告分组 / -pedantic / -funsigned-char 现状
bash doc/suite.sh             # 四个目标的全套件

# 交叉目标
make test-arm64 && make test-rv64 && make test-rv32
```

判定约定：探针里 `rej` 打印 `PASS` 表示**被正确拒绝**；`GAP` 表示已知缺口。
凡与 clang 有分歧的项都会用 `clang -std=c2y` 对拍（`c2ycov4.sh`）。

> 归档脚本里对编译器/仓库路径使用绝对路径 `/home/memory/cxx`（本次会话的 checkout 位置），
> 换环境时改这两行即可：`c2ycov2/3/4/5.sh` 顶部的 `C=`、`c2ycov4.sh` 顶部的 `CC=`、
> `kw.sh` 的 `cd`。

---

## 12. 改动清单与建议顺序

本轮改动（**未提交**，等 review）：

```
 src/opt_ast.c       | 50 ++++++++++++++++++++++++---   §1.1 折叠为 x != 0
 src/irgen.c         | 14 ++++++++++--                   §1.2 短路 PHI 前驱
 src/main.c          | 16 ++++++++++----                 §1.3 -l / -Wl, 链接顺序
 src/dumpir.c        | 11 +++++----                    §1.4 switch 空分支表必须写 [ ]
 src/parser.c        |  7 ++++++                       §1.4 空 case 范围的诊断
 test/fold.c         | 34 ++++++++++++++++++++++++       18 项回归
 test/control.c      | 19 +++++++++++++++                14 项回归
 test/conformance.sh | 18 +++++++++++++++                -lm 端到端回归
 test/c2y.sh         | 新增（P7）                        C2y 回归网，纳入 make test
 Makefile            |  1 +                              make test 调用 test/c2y.sh
```

> `doc/` 下的旧探针脚本（`probe*.sh`、`verify_*.sh`、`evidence*.sh`、`reverify*.sh`
> 共 17 个）已删除 —— 它们验证的结论要么已修复要么已撤销，本文档不再引用。
> 计划文档也已重写，见 `doc/cxx-c2y-plan.md`。

建议的下一步优先级：

| 序 | 项 | 理由 |
|---|---|---|
| ① | §3.1 math 内建族 | 一次修复解锁 `<math.h>` 的全部宏；同时解决 N3303、N3364 两个提案 |
| ② | §2.1 `_Atomic` 聚合体 | 生成非法 IR，属硬失败 |
| ③ | §3.3 `<stdmchar.h>` | 唯一整项未实现的提案（N3366） |
| ④ | §2.2 数组复合类型 | C89 起的老规则，实现面小 |
| ⑤ | §3.5 特性宏一致性 + `__STDC_NO_DECIMAL_FLOAT__` | 声明与实现对齐 |
| ⑥ | §3.4 `-pthread` | 驱动层，工作量小 |
| ⑦ | §10 的 `test/c2y.sh` | 把本轮回归网固化 |

§3.2（N3348）可穿插，但受 clang 生态进度制约，优先级最低。

**上述工作项的完整定义（范围、依赖、验收标准、风险）见 `doc/cxx-c2y-plan.md`。**

---

*本文档基于 N3685 全文（`doc/n3685.txt`，57194 行）逐节比对；所有条款引用均可在该文件中定位。*
