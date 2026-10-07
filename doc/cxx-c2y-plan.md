# cxx 修订计划（C2y 收口）

> 依据：`doc/n3685-conformance.md`（当前版，基于 commit `0924007` + 本轮改动）。
> 本文取代旧的 A/B/C/D/E/F 批次计划 —— 那六批**已全部完成**，存档见 §5。
> 内建框架的设计见 `doc/builtin-redesign.md`。

---

## 0. 范围

**在计划内**：N3685 语言语义补全、标准库头可用性、内建族、诊断体系、扩展开关。

**明确不做（长期）**：`_Complex` / `_Imaginary` / 复数字面量 / `++ --` 与运算符用于复数；
十进制浮点 `_Decimal32/64/128`。共 9 个提案 + 2 个头（`<complex.h>` `<tgmath.h>`）。
相关位置应保留「有意不支持」注释，验收中排除。

**原则**：不改既有的正确行为去迁就外部头文件。典型案例是 `_FloatN` 保持关键字身份
（H.5.1），而用 GNU 兼容宏让 glibc 走对分支。

---

## 1. 工作项

### P1 ★ math 内建族 —— 一次修复解锁 `<math.h>`

| | |
|---|---|
| **现状** | `<math.h>` 能包含，但 `NAN` `INFINITY` `HUGE_VAL{,F,L}` `isnan` `isinf` `isfinite` `signbit` `fpclassify` `isnormal` `isgreater` `isless` `isunordered` 全部报 `implicit declaration`。cxx 的 32 个内建里没有任何 math 内建 |
| **证据** | `bash doc/c2ycov3.sh`（7 项 FAIL）；`doc/c2ycov2.sh` 的 N3364 GAP |
| **根因** | glibc 把这些宏展开成 `__builtin_nanf` / `__builtin_huge_val*` / `__builtin_inff` / `__builtin_fpclassify` / `__builtin_isgreater` / `__builtin_signbit` 等，cxx 一个都没有 |
| **范围** | ①常量族：`nan` `nanf` `nanl` `nans` `nansf` `nansl` `huge_val` `huge_valf` `huge_vall` `inf` `inff` `infl`（折叠为对应格式的浮点常量）<br>②分类/比较族：`fpclassify` `isgreater` `isgreaterequal` `isless` `islessequal` `islessgreater` `isunordered` `signbit` `isnan` `isinf` `isfinite` `isnormal` `iszero` |
| **不做** | `__builtin_sqrt`/`fabs`/`sin`… 这类数学**运算**内建（涉及 libm 符号与融合语义），单独排期 |
| **同时解决** | N3303（HUGE_VAL 措辞）、N3364（SNAN 初始化）两个提案 |
| **验收** | `NAN != NAN`、`INFINITY > 1e308`、`HUGE_VAL*`、`isnan/isinf/isfinite/signbit/fpclassify/isnormal/isgreater` 的**运行结果与 clang 逐字一致**；`doc/c2ycov3.sh` 的 7 项 FAIL 转 PASS；新增 `test/math.c` 并把 `c2ycov3` 里 math 那一段纳入 `test/c2y.sh` |
| **验收的更正** | 那 7 项里 **4 项是 cxx 的真缺口**（P1 修掉），**3 项是探针自己的期望写错了**——见文末「探针账目的更正」 |
| **框架** | 用 `doc/builtin-redesign.md` 的声明式内建表 + 统一折叠路径；常量族走 `fold_builtin_call`，分类族走 `builtin_fn[]` 的 IR 映射 |

**分三步，进展如下**（`test/c2y.sh` 里各有对应条目）：

| 步 | 内容 | 状态 |
|---|---|---|
| 1 | **常量产出族**：`huge_val{,f,l}`、`inf{,f,l}`、`nan{,f,l}` | ✅ 已完成。在 `parse_builtin_fn` 里直接折叠为 `ND_NUM`，irgen 与折叠器完全不经手，因此整个特性落在一个文件里；`inf` 与 `huge_val` 是同一值的两个名字，只多一行表项。实测位型与 clang 逐位相同（`0x7ff8…` 静默 NaN、`0x7ff0…` +Inf、`0xK7fffc0…` x87） |
| 2 | **比较族**：`isgreater`/`isgreaterequal`/`isless`/`islessequal`/`islessgreater`/`isunordered` | ✅ 已完成。前四个恰好是一个 C 运算符，parser 直接改写成该运算符，每个操作数只求值一次；`islessgreater` 与 `isunordered` 没有对应运算符（7.12.18.6 把前者定义为 `(x) < (y) || (x) > (y)`），改写会让每个操作数出现在两个位置——共享节点被 irgen 生成两遍而块计数只算一遍——所以给 IR 补了 `one`/`uno` 两个 `fcmp` 谓词，由 irgen 发射。10 组操作数组合的语义与 clang 逐字节相同 |
| 3 | **分类族**：`isnan`、`isinf`/`__builtin_isinf_sign`、`isfinite`、`isnormal`、`fpclassify`、`signbit` | ✅ 已完成。同一个理由全部走 irgen：操作数求值一次存入 `Ref`，`Ref` 才是被复用的东西。`isfinite` 用两条 `one` 比较（对无穷因相等而为假、对 NaN 因无序而为假，一次判掉两类）；`isnormal` 再加「|x| ≥ 最小正规数」；`signbit` 用 `copysign(1.0, x) < 0`——7.12.4.8 脚注 281 要求它对 **NaN 也**给出符号，任何算术技巧都做不到，而 `copysign` 就是 ISO/IEC 60559 的符号位操作。24 组取值（含 ±0、±NaN、±inf、次正规、float/long double）与 clang 逐行相同 |

**途中发现 P1c**（见下）：分类测试是本仓库唯一对次正规数分类的地方，它暴露了 x87 次正规常量的问题。

### P2 `_Atomic` 聚合体的泛型原子函数生成非法 IR —— ✅ 读写已完成，RMW 拆到 P2b

| | |
|---|---|
| **现状** | `atomic_load` / `atomic_store` 已可用（含成员访问、`atomic_init`）。`atomic_exchange` 与 `fetch_*` 仍错，见 P2b |
| **根因（不是原子特有的）** | `ND_LVTOR` 对**记录类型**不是恒等变换：它发射了一次真正的结构体加载，交回一个**值** Ref。成员访问随后把这个值当成 GEP 的基址。`atomic_load()` 在 parser 里被降级成 `(temp = *object, temp)`，正好是这个形状，所以整块聚合体的原子读全部失败 |
| **最小复现** | `struct S { int a; }; int g1(void) { struct S t; return (0, t).a; }` —— 与原子毫无关系。`(0, t)` 不是左值（6.5.18 脚注 115），`add_type` 因此给逗号的结果套上左值转换；`fold_ast` 又把逗号改写成它的右操作数，于是成员访问的基址变成了一个「记录值」节点 |
| **对照** | `t.a`（成员是左值）走另一条分支，得到 `getelementptr i8, ptr %slot, i32 0` ✓ |
| **修复** | `irgen.c` 的 `ND_LVTOR`：`TY_STRUCT`/`TY_UNION` 时直接返回地址——与 `ND_VAR` 分支里已有的那句「记录值一律用地址表示」保持一致。原子分支在其之前，整块原子读仍走 `load atomic iN` |
| **验证** | `atomic_load`/`atomic_store` 的运行时值与 clang 相同；`test/c2y.sh` 两条 `runit`（原子聚合体 + 无原子的逗号最小例） |

### P2b 整块聚合体的 `atomic_exchange` / `fetch_*` / `atomic_compare_exchange_*` —— ✅ 已完成（两半）

| | |
|---|---|
| **前半（exchange，早前落地）** | 曾能编译、**运行时段错误**：操作数按**地址**传递，`atomic_exchange(p, x)` 发射 `atomicrmw xchg ptr %obj, ptr %slot_of_x`，LLVM 交换的是**指针值**而不是聚合体的字节。现在按聚合体大小读成 `iN` 再参与 `atomicrmw`，结果经槽位交回 |
| **后半（CAS，本轮落地）** | `atomic_compare_exchange_strong/weak` 也一样：比较值曾是**整个记录**（`load %struct.S`），新值是**它的地址**，于是 `cmpxchg ptr, %struct.S, ptr` ——cxx 自己的校验器就拒绝（`compare value and new value type do not match`），clang 那侧是 `cmpxchg ptr, i64, i64`。现在两个操作数都按聚合体大小读成整数，失败回写把整数存进记录类型的 `*expected`（type punning），逐字节与 clang 相同 |
| **实现** | `irgen.c`：新增 `atomic_agg_bits(node, ty)`（1/2/4/8 字节检查 + `bitint[size*8][1]`），原先三处重复的检查一并走它；`ND_CAS` 增加记录分支用它取 `old_val`/`new_val`。`fetch_*` 不需要改：聚合体在 parser 就被拒（与 clang 同址同因） |
| **验收** | `test/c2y.sh`：`P2b`（`runit`）+ 新增 `P2c`（`runit`：成功/失败回写、weak、8 字节单成员记录、`_Bool` 结果）与 `bad`（16 字节聚合体被拒）。探针里 1/2/4/8 字节、`union`、显式 order、weak 重试循环、全局聚合体全部与 clang 逐行同 IR、同退出码 |
| **已知措辞分歧** | `atomic_fetch_add(&v, 聚合体)` 两家都拒，cxx 的句子是 clang 的**前缀**（clang 后面还缀了实际类型 `('_Atomic(struct S) *' invalid)`） |

### P1c x87 80 位 `long double` 的次正规常量 —— ✅ 已完成

| | |
|---|---|
| **严重性** | **静默给出错值**；且不止影响折叠，**字面量本身**就是错的 |
| **症状**（修复前） | `0x1p-16383L` → `0x0p+0`；`0x1p-16400L` → `-0x8p+16365`；`LDBL_MIN/2` → 0；`LDBL_MIN/4` → `-inf`。而 `LDBL_MIN*0.5` 却是对的（乘法与除法走了不同路径） |
| **根因** | `round116_to_target()` 构造的规范形式「隐式位在 112、指数域 ≥ 1」只对**最小次正规仍在 binary128 正规范围内**的目标成立——binary16/32/64 都满足（它们的最小数远高于 2^-16382）。x87 例外：最小次正规是 2^-16445，于是 `E128 = vexp - (p-1) + msb_t + 16383 = msb_t - 62` 变成负数，写进指数域时**绕进了符号位** |
| **修复** | `E128 < 1` 时改写二进制 128 的**次正规**形式：指数域置 0，有效位右移 `1 - E128` 位（移出的位必为 0）。`fp128_decompose_norm()` 早已会正确读回该形式（`exp_unbiased = msb - 16494`），缺的只是生产端 |
| **范围** | 仅 x87。`0x1p-1023`、`DBL_MIN/2`、`0x1p-127f`、`FLT_MIN/2f` 从未出错 |
| **发现途径** | P1 步三的分类测试——它是本仓库唯一对次正规数调用 `fpclassify` 的地方（`ld subn` 一行 cxx 给 `FP_ZERO`、clang 给 `FP_SUBNORMAL`） |
| **验证** | `doc/sub.sh` 10 行全部与 clang 相同；分类测试 **25 行与 clang 完全相同**；`test/ldouble.c` 增 11 条断言（含 `0x1p-16445L != 0` 与 `0x1p-16446L == 0` 边界）；`test/c2y.sh` 的 `P1c` 条目转为 `runit` |
| **教训** | 该缺陷**与本轮改动无关**，是既有问题。`gap()` 只编译不运行，而错常量照样编译通过——所以加了 `rungap()` 登记「只在运行期可见」的缺口 |

### P1b 信号 NaN（`SNAN*` / `__builtin_nans*`）—— ✅ 已完成

| | |
|---|---|
| **原状** | `fp128_to_fp16_bits`/`_fp32_bits`/`_fp64_bits`/`_fp80_bits` 都以 `if (fp128_is_nan(v)) return <规范化静默 NaN>;` 开头，任何信号 NaN 都到不了输出 |
| **实现** | 新增 `fp128_is_signaling_nan()`（判据是有效位第 111 位、即 limb[3] 的 bit 15 为 0）。四个转换器在 NaN 分支里分流：静默的仍旧走原来的规范值，**信号的保留 quiet 位状态并把 payload 的前导位搬过去**（binary128 有 111 位 payload，binary16/32/64 与 x87 分别放得下 9/22/51/62 位），payload 为 0 时强制为 1——否则会变成无穷。`int128_shr` 做移位，payload 就是 `fp128_get_m()` 去掉指数/符号/quiet 位后的值 |
| **模式的选择** | `FP128_SNAN` 的 payload 在**最高 payload 位**（limb[3] 的 bit 14），于是「保留前导位」正好把它搬到每种格式的最高 payload 位：binary64 得 `0x7FF4000000000000`、binary32 得 `0x7FA00000`、x87 得 `0xA000000000000000`——**与 gcc/clang 逐位相同**（三家同一份程序打印同一串十六进制，`doc/` 未另建探针，写在 `test/c2y.sh` 里） |
| **内建族** | `BUILTIN_NANSF/NANS/NANSL` 三个 kind 加回 `cxx.h` 与 `builtin_defs[]`，`parse_math_const()` 里折叠成 `FP128_SNAN`（`is_snan` 标志）。**踩坑**：只加 kind 与表项不够，`parse_builtin_fn()` 的分派 `switch` 也要加上这三个 case，否则调用落到普通标识符路径、报成 `implicit declaration of function ‘__builtin_nans’` |
| **glibc 不给 `SNAN*`** | 实测 `double d = SNAN;` 在 gcc / clang / cxx 下**都是** `use of undeclared identifier`：glibc 的 `SNAN/SNANF/SNANL` 只在 `__GLIBC_USE (IEC_60559_BFP_EXT)` 下定义，而 `-std=c23` 下三家都拿不到。所以断言写在 `__builtin_nans*` 上，这与计划里「`SNAN*` / `__builtin_nans*`」的验收一致 |
| **验收** | `test/c2y.sh` 的 `runit`：`__builtin_nans("")`/`nansf` 的位型分别是 `0x7FF4000000000000`/`0x7FA00000`，`NAN` 仍是 `0x7FF8000000000000` 且两者不同，x87 的 `nansl` 在 `__LDBL_MANT_DIG__ == 64` 下单独断言。**`test/c2y.sh` 现为 74 passed / 0 known gap** |
| **为何撤回而不将就** | 让 `__builtin_nans` 返回静默 NaN 会**静默出错**——请求信号 NaN 的代码通常紧接着就要检测它。宁可让 `__builtin_nans("")` 报未声明 |
| **已就位的基础** | `FP128_SNAN = {{0, 0, 0, 0x7FFF4000u}}` 已加入 `src/support/fp128.{c,h}`：Fp128 内部就是 binary128 存储，符号与阶码字段在各格式中同位，payload 放在静默位之下、binary16 的 10 位有效位之内，因此**同一个位型在五种目标格式下都是信号 NaN** |
| **动作** | 给库加 `fp128_is_signaling_nan()`，让四个转换器在 NaN 分支里区分两种形态并保留 payload（全零 payload 要强制非零，否则变成无穷）。改完后把三个表项加回来 |
| **风险** | 四个转换器被浮点字面量路径共用；改动只影响 NaN 输入，且静默 NaN 仍映射到原来的规范值，因此既有 `0.0/0.0` 用例不受影响 |
| **验收** | `__builtin_nans("")` 的位型与 clang 一致（`+snan(...)`）；`__builtin_nansf`/`__builtin_nansl` 同理；`test/c2y.sh` 的 `P1b` 条目转 PASS；浮点字面量与折叠用例不回归 |

### P2 `_Atomic` 聚合体的泛型原子函数生成非法 IR

| | |
|---|---|
| **现状** | `atomic_load` / `atomic_store` / `atomic_exchange` 作用于 `_Atomic struct S` 时，cxx 报 `error: '%tmpN' defined with type '%struct.S' but expected 'ptr'`。`_Atomic int` 正常；普通结构体的返回值成员访问也正常 |
| **最小复现** | `bash doc/minbug.sh` 的 B 组；见 `doc/n3685-conformance.md` §2.1 |
| **根因** | 原子读被降级成标量 `load atomic i32`，随后成员访问把这个**结构体值**当成指针做 `getelementptr` |
| **验收** | 上述三种函数对 `_Atomic struct`（含含数组/浮点成员的形状）编译通过且运行值与 clang 一致；`test/atomic.c` 增聚合体用例 |

### I1 `inline`：内联定义与外部定义的区分（6.7.5p8 / p5 / p3）—— ✅ 已完成

| | |
|---|---|
| **原状** | `inline` 只被记进 `funcspec`，**没有参与发射**：每个 `inline` 定义都当作外部定义发出去。那是 C++ 的规则，不是 C 的 |
| **判据（p8）** | 「若一个 translation unit 里该函数的**所有文件作用域声明**都带 `inline` 且都不带 `extern`，则该 unit 里的定义是**内联定义**」。内联定义**不提供外部定义**，于是这个 unit 的目标文件里只有未定义引用——单靠它链接会失败，除非另一个 unit 提供外部定义 |
| **实现** | 每个文件作用域声明都更新 `Sym::all_decls_inline`（`note_inline_decl()`：缺 `inline` 或带 `extern` 就清掉），**在 `parse()` 收尾统一判定**——因为 `inline int f(void){…} extern int f(void);` 里靠后的 `extern` 会把先前的内联定义变成外部定义（实测 gcc/clang 正是如此）。判成内联定义后置 `Sym::is_inline_def`，`irgen.c` 与 `dumpir.c` 都不再发函数体（`dump_fn()` 打 `declare`），调用点照旧引用外部符号 |
| **p5** | 「带 `inline` 的外部链接声明必须在同一 unit 内定义」是约束：gcc 报 `inline function ‘f’ declared but never defined`（**没有 `-W` 组名**，`-w` 可抑制），clang 沉默。cxx 跟 gcc（措辞与列号逐字相同） |
| **p3 的一半** | 「内联定义不得定义可修改的 static/thread 对象」：gcc 与 clang **都报**（措辞不同），clang 的组名是 `-Wstatic-local-in-inline`；cxx 用 clang 的措辞、gcc 的列（指向声明的名字）。`static const` 不报（实测两家都不报） |
| **p3 的另一半** | 「内联定义不得引用内部链接的标识符」：gcc 报、clang 沉默，cxx 跟 clang（不报），理由记在这里 |
| **实测矩阵** | 12 个形状（`inline` 定义、`extern inline`、定义后 `extern` 声明、无 `inline` 的声明、`static inline`、仅声明、取地址、被同 unit 另一个函数调用、可修改 static 局部、`static const` 局部……）跑 gcc / clang / cxx：**符号是否定义**与**单文件能否链接**逐项一致 |
| **两文件惯用法** | `use.c`（内联定义 + 调用）配 `def.c`（`extern inline` 定义）：三家都能链接并打印 `42` |
| **测试** | `test/conformance.sh` 增 6 条：内联定义不定义符号（**必须链接失败**）、另一 unit 的 `extern inline` 补齐、定义后的 `extern` 声明转为外部定义、无 `inline` 的声明同理、`static inline` 在本 unit 内定义、p5/p3 两条诊断与 `-Wno-static-local-in-inline`。该套件 **85 passed / 0 gap** |

### A1 不定长全局数组：翻译单元结束时按一个元素补全（6.9.2p2）—— ✅ 已完成

| | |
|---|---|
| **需求** | `int arr[];`（暂定定义，整份 TU 里都没给出长度）在 TU 结束时补成长度 1 并警告 |
| **原状** | 补全完全没做：`@arr = dso_local global [-1 x i32] zeroinitializer` 直接进 LLVM，clang 报 `error: expected number in address space`（与本轮之前几处「发出去的模块 LLVM 拒收」同类） |
| **参照实测** | gcc：`warning: array ‘arr’ assumed to have one element`（无 `-W` 组名）；clang：`warning: tentative array definition assumed to have one element [-Wtentative-definition-array]`。两家都按 **1 个元素**分配（`arr:` 之后 4 字节），列号都指向声明的名字 |
| **实现** | `complete_tentative_arrays()` 在 `parse()` 收尾（`check_unused_statics()` 之前，好让警告顺序与 gcc 一致）：遍历文件作用域的**定义**（跳过 `extern` 声明），对 `TY_ARRAY` 且 `len < 0` 的符号 `sym->ty = array_of(sym->ty->base, 1)`。用 clang 的措辞与组名 `-Wtentative-definition-array`（新组位 `1u << 17`），列号与两家一致（声明名） |
| **顺序** | 只对**定义**补全：`int arr[]; int arr[10];` 由 P4 的复合类型机制拿到 10（不警告），`int arr[] = {1,2,3};` 由初始化器拿到 3（不警告），`extern int arr[];` 是声明、不补全 |
| **顺带修掉的三个同类硬失败** | 都是 `[-1 x T]` 进 LLVM、clang 报 `expected number in address space`：<br>① `extern int arr[];` —— 声明没有长度可补，`print_type()` 现在对未知长度打 `[0 x T]`，与 clang 的 `@arr = external global [0 x i32]` 逐字相同；<br>② 块作用域的 `int arr[];` / `static int arr[];` —— 暂定规则只属于文件作用域，块内定义必须给长度或初始化器，现在按 clang 的措辞报错（gcc 是 `array size missing in ‘arr’` / `storage size of ‘arr’ isn’t known`，裁定一致）；<br>③ `struct S arr[];`（元素类型不完整，6.7.6.2p1）—— 现在报 `array has incomplete element type`（gcc/clang 都报，只是带类型名） |
| **验收** | IR 与 clang 逐字相同：`int arr[];` → `@arr = dso_local global [1 x i32] zeroinitializer, align 4`；`extern int arr[];` → `@arr = external global [0 x i32], align 4`。行为证据：`def.c` 里 `int arr[];` + `use.c` 里 `extern int arr[1];` 三个编译器都能链接并运行（说明确实只分配了 1 个元素）。`test/conformance.sh` 增 6 条（含「只有暂定定义才警告」：`int done[] = {…}`、`int sized[4]`、`int late[]; int late[10];` 都不警告，`-Wno-tentative-definition-array` 可关），该套件 **91 passed / 0 gap** |
| **已知差异** | 一个 TU 里同时有 `sizeof arr` 错误和这条警告时，gcc/clang 两条都报，cxx 在第一处错误就停下（`error()` 直接退出，是本仓库一贯的错误模型，不是本轮引入的） |

### N1 fold 期的窄化警告（6.3.1.3 常量转换）—— ✅ 已完成

| | |
|---|---|
| **需求** | 折叠常量时对「值的改变」发警告：大整数塞进 `char`、`double` 塞进 `float` 等 |
| **落点** | `src/opt_ast.c` 的 `fold_cast()`：常量转换正是在这里被折叠，`node->kind == ND_IMCAST`（cxx 早就有隐式/显式两种 cast 节点）保证只报隐式转换。**但全局初始化器不走 `fold_ast()`**（它只走函数体），所以 `eval_gvar_data()` 里对纯量初始化器的表达式补一次 `fold_node()`——parser.c 里本来就有同样的注释（折叠内建参数时） |
| **四组** | `-Wconstant-conversion`（clang 名，**默认开**）：整数常量转成装不下的整数类型。判据是「值能否落在目标类型的范围内」，负值转无符号时**按该类型的有符号范围判**——这条让 `unsigned u = -1;` / `unsigned char c = -1;` 保持沉默（两家都沉默），而 `unsigned char c = -300;` 报警（两家都报）。`-Wliteral-conversion`（clang 名，**默认开**）：浮点常量转整数，丢小数部分或越界。`-Wfloat-conversion`（gcc 名，**默认关**，两家都不是默认开）：`double → float` 这类浮点之间的舍入改变了值。`-Wimplicit-const-int-float-conversion`（clang 名，**默认开**，第四组见下）：**常量整数**转浮点，目标格式装不下它 |
| **第四组（评审后拆出）** | 原先「整数超出目标有效位」与 `double → float` 共用默认关的 `-Wfloat-conversion`。实测 clang 把前者单独放在**默认开**的 `-Wimplicit-const-int-float-conversion` 里（`-Wall`/`-Wextra` 无关，`-Wno-float-conversion` **也关不掉它**），gcc 两处都沉默；于是照 clang 拆开：`1u << 23`，不在 `WG_OFF_DEFAULT` 里。实测同一个文件 9 个形状：clang 默认 3 条（`16777217 → float`、`9007199254740993 → double`、负值那条），cxx 现在也是默认 3 条且逐行同位置；`-Wno-implicit-const-int-float-conversion` 归零，`-Wno-float-conversion` 仍是 3（与 clang 相同——两个组独立）；`-Wfloat-conversion` 4 条（多出 `float f = 1.1;` 那条 `double → float`）；`long double ld = 18446744073709551615ULL;` 三家都沉默（x87 尾数 64 位，正好装得下），而同一常量给 `double` 报（53 位装不下） |
| **实测矩阵** | `doc/narrow.sh`：14 个形状 × 三家。**逐行一致的有 11 行**（char/uchar/short/块作用域/赋值/实参/返回值 7 行三家都报；显式转换、`unsigned -1` 惯用法、正好装得下 3 行三家都沉默） |
| **两处刻意的跟随** | `int i = 3000000000;`（`long → int`）：clang 报、gcc 的 `-Woverflow` 不报 → cxx 跟 clang ✓；`int i = 1.5;`（浮点转整数丢小数）：clang 默认报、gcc 要 `-Wconversion` → cxx 跟 clang ✓ |
| **消息与取值** | 措辞用 clang 的（cxx 一贯把 `'` 换成 `‘ ’`），取值打印踩过一个坑：最初两边都走宿主 `%g`，于是 `float f = 1.1;` 印成「from ‘1.1’ to ‘1.1’」——同一个值。现在按各自类型的位数打印（float 9 位、其余 17 位），与 gcc 的取值形状对齐：`changes value from ‘1.1000000000000001’ to ‘1.10000002’` |
| **验收** | `test/conformance.sh`：一个文件里 6 条警告（5 条 constant-conversion + 1 条 literal-conversion）且 `-Wno-*` 各自只剩对方那些、`-w` 归零；浮点那组默认 **1** 条（第四组那条）、`-Wno-float-conversion` 仍 1、`-Wno-implicit-const-int-float-conversion` 0、`-Wfloat-conversion` 3 条，并且断言消息里含 `1.10000002` 与 `16777216`（就是上面那个打印坑的回归）。本轮又加两条：note 里必须出现 `the nearest representable value is 2147483647`，以及三个方向的饱和（`long l = -1e30;` → `-9223372036854775808`、`unsigned u = -1e10;` → `0`、`short s = 1e10;` → `32767`；三个各自一个文件——拒绝是终止性的，同一个文件里只能看到第一条）。该套件 **100 passed / 0 gap** |
| **越界浮点→整数（评审后的决定）** | **静态初始化器直接拒绝**：`int i = 1e20;`、`int i = (int)1e20;`、`unsigned u = 1e20;` 都报 `conversion of out of range value from ‘double’ to ‘int’ is undefined`（clang 也是拒绝，只是它给的是内部消息 `cannot compile this static initializer yet`；gcc 接受并把值饱和到 `INT_MAX`）。函数体内同样的转换只**警告**（clang 的 `-Wliteral-conversion` 默认行为）。判据共用 `fits_target()`：整数→整数的窄化允许「负值转无符号按有符号范围判」，浮点→整数不允许（6.3.1.4 是有定义性缺口，不是模运算）。实现上这个决定放在 `fold_cast()` 里、由 `in_static_init`（`eval_gvar_data()` 折叠静态初始化器表达式时置位）分流——原先想放一个折叠前的遍历，但那条路径上看不到转换节点 |
| **note 说明该选什么值** | 拒绝若只说「未定义」，读者还得自己算。所以错误与警告后面都跟一条 note，给出目标类型的区间**和最接近的可表示值**：`‘int’ holds -2147483648 to 2147483647; the nearest representable value is 2147483647`（`conv_int_range_str()` 按目标类型的宽度与符号算区间，含 `__int128` 无符号那档；`conv_oor_note()` 再按方向饱和——负值给下界、无符号目标遇到负值给 0、超出 `Int128` 本身的值按符号给那一端，**NaN 不给**，它离哪个值都不近）。措辞在评审后从「choose a value or a type that can hold it」改成直接给出那个值：gcc 对同一处报的 `changes value from ‘1.0e+20’ to ‘2147483647’` 也是同一个饱和值。note 与它的警告同生共死：`-Wno-literal-conversion` 或 `-w` 下两者都不出现，不留孤立 note |
| **字面量超出自身类型（评审后的决定）** | 解析期警告：`double d = 1e400;`、`float f = 1e40f;`、`long double x = 1e5000L;`、`0x1p+20000` 都报 `magnitude of floating-point constant too large for type ‘double’`（gcc `-Woverflow`、clang `-Wliteral-range`，新组位 `1u << 21`）。位置在 `new_num_node()` 而不是词法器：词法器会连死分支里的字面量一起过，`#if 0` 里的 `1e400` 不该报警（实测确实沉默）。判据是「数值型字面量折出来是 ±inf」——`<math.h>` 的无穷是标识符不是字面量，所以 inf 只可能来自溢出 |

### S1 `-Wsign-compare`：关系运算两侧符号性不同 —— ✅ 已完成

| | |
|---|---|
| **需求** | 在 `<` `>` `<=` `>=` 节点**加上类型之后、算术转换之前**检查两侧符号性，混合符号时警告 |
| **落点** | `src/type.c` 的 `add_type()`：关系分支里 `usual_arith_conv()` 之前插入 `warn_sign_compare()`。位置是全部要点——`unsigned char < int` 提升后是 `int` vs `int` 要沉默，`char < unsigned` 提升后是 `int` vs `unsigned` 要报，所以 helper 自己先做 6.3.1.1 的整型提升（`size < 4 → int`）。`>` / `>=` 在 cxx 里可能被换过操作数，helper 换回来按书写顺序报 |
| **消息与组** | gcc 的措辞 `comparison of integer expressions of different signedness: ‘int’ and ‘unsigned int’`，组名 `-Wsign-compare`（`1u << 22`）。**默认关**：两个参考实现在 C 里都只在 `-Wextra` 下开这一组，cxx 跟参考实现；`-Wsign-compare` 开、`-Wno-sign-compare` / `-w` 关（默认沉默已实测） |
| **抑制规则（都来自 gcc 实测）** | `_Bool` 操作数不报（值只能是 0/1）；带符号一侧是**非负常量**不报（`i < 5`）；无符号一侧是**字面量 0** 不报（`i < 0u`）；浮点、指针操作数不报。`i < 1u` 仍报（无符号常量 1 不是 0，带符号那一侧也不是常量） |
| **实测十行（`mismatches: 0`）** | `int`/`unsigned` 的四种关系全 warn；`unsigned < unsigned`、`int < long`、`unsigned char < int`、`_Bool < unsigned`、`int < 0u`、浮点混合、指针混合全沉默；`char < unsigned`、`sizeof(*p) < int`、`int < 1u` warn —— 与 gcc `-Wextra` 逐行一致 |
| **等值比较（评审后放开）** | `==` / `!=` 一起报：gcc 的该组实测覆盖等值，cxx 的等值比较也走同一条类型化路径，所以只是去掉 helper 里的 kind 判断。探针的第 4、5 行就是这两条，三家一致 |
| **测试固化** | `doc/signcmp.sh`：17 行形状 × 三家，带 `ok/MISMATCH` 判定与旗标一节（默认沉默、`-Wsign-compare` 开、`-Wno-` 与 `-w` 关，以及 gcc/clang 的 `-Wextra` 计数）；`test/conformance.sh` 增一条带旗标的检查（2 条警告、默认 0、`-Wno` 0） |
| **验证** | `make test` exit 0（conformance 98/0、c2y 74/0、0 FAILED）、arm64 与 rv64 各 51 passed、`doc/probes.sh` 与 `doc/fall.sh` 不变。**rv32 本轮未重跑**（改动是公共代码，之前 51 passed / 1 skipped） |

### G1 `__attribute__((cleanup(f)))`（GNU 变量属性）—— ✅ 已完成

| | |
|---|---|
| **需求** | 对象离开作用域时调用 handler，并把它**自己的地址**传进去。不是标准 C（`__has_c_attribute(cleanup)` 仍是 0，只有 GNU 名字空间有它），但 C 代码里用得很多（glibc 与内核风格的清理栈） |
| **语义实测**（`doc/cleanup.sh`，9 个形状、三家**逐字节**同 stdout 同退出码） | ①同一块内按**声明逆序**（`b(2) a(1)`）；②内层块先于外层（`b(2) c(3) a(1)`）；③**每个出口都跑**：`break`、`continue`、`goto`、`switch` 里的块、函数体末尾；④`for` 初始化里声明的对象在**循环结束时**销毁一次（`break` 出去也汇到那里，不重复）；⑤`return` **先取值再跑 handler**（`h(7) f=7`，handler 把对象改成 100 也不影响返回值）；⑥`[[gnu::cleanup]]` 与 `__attribute__((cleanup))` 等价；⑦记录类型（`struct S`）与变长数组（handler 形参 `int (*)[3]` 对 `int a[n]`）都可用；⑧形参上的属性被忽略（两家都不跑） |
| **误用**（10 个形状，比「拒绝 / 警告 / 沉默」的类别） | 文件作用域、块作用域 static、**形参**、`typedef`、函数声明上：两家都只警告并忽略，cxx 用 clang 的措辞 `‘cleanup’ attribute only applies to local variables`；实参不是函数、`&h`、形参类型不兼容、形参不是恰好一个、没有实参：两家都拒绝，cxx 逐条跟 clang 的措辞与严重程度（`‘cleanup’ function ‘h’ parameter has type ‘char *’ which is incompatible with type ‘int *’` 等） |
| **实现** | `attr.c` 增 `cleanup`（GNU 名字空间，`ATTR_DECL`）；`Sym::cleanup_attr` 暂存属性；`Scope` 挂一份声明序的 `Cleanup{var, fn}` 表；`cleanup_handler()` 只在**能跑 handler 的位置**（块作用域自动对象）解析实参并校验——实参必须是函数名（`&h` 与函数指针都拒，与两家一致），形参按**普通调用的兼容规则**判定（`const int *`/`void *` 收，`char *` 拒，VLA 对象跳过类型比较）；`cleanup_call()` 建 `f(&var)` 并走 `check_asop`/`new_imcast`；`cleanup_scope_chain()` 把一个作用域的 handler 逆序串成一条链，`cleanup_leaving(from, to)` 收集一次跳转真正离开的那些作用域 |
| **跳转怎么带上 handler** | 新增 `Node::unwind`（在联合体之外，与 `label_ring`/`label_body` 同级）：`return`/`break`/`continue`/`goto` 各带一条调用链，irgen 在**跳之前**生成它。`return` 的链在结果写回之后才跑（`ret_jump()`，四条出口共用）。`goto` 的落点要等标签解析完才知道，所以解析期先用一张 `(node, scope)` 边表记下 goto 与标签各自的作用域，`resolve_goto_labels()` 里再算 |
| **不重复跑** | 落点作用域自己的 handler 由「它在哪里结束」负责（块末尾、`for` 之后），跳转只跑**离开**的那些——所以 `break` 不跑 `for` 作用域的 handler，`continue` 一个都不跑 |
| **验收** | `test/c2y.sh` 两条：一条把七个语义形状编进一个程序，断言输出与 **gcc/clang 逐字节相同**（期望串就是从两家跑出来的——我最初手写的那版把 `a(7)` 的位置写错了，被这条断言抓住）；一条把十种误用按类别断言。`test/conformance.sh` 又加两条：**位置**（文件作用域、块 static、形参、原型形参、typedef 声明符前后、函数声明共七处各警告一次，`-Wno-attributes`/`-w` 归零）与**四种拼写**（`__attribute__`/`[[gnu::…]]` × 声明符前后，四种都要跑 handler 且自身不产生诊断）。`doc/cleanup.sh` 已接进 `doc/probes.sh`：**19 passed / 0 mismatch** |

**`Type` 联合体的审计（`len` / `is_static` / `is_star` ↔ `vla_len` / `vla_cnt`）**

VLA 的 `len` 就是 `vla_len` 指针的低 32 位（见缺陷 1），所以把**每一个读 `len` 的地方**都按「能不能拿到 VLA 类型」过了一遍：

| 读取点 | 能否拿到 VLA | 结论 |
|---|---|---|
| `type.c` `is_compatible()` 数组分支 | **能** | 缺陷 1，已修（VLA 一律兼容） |
| `dumpast.c` 类型打印 | 能 | 早已有 `TY_VLA → "?"` 分支；实测 `-ast-dump` 打的是 `int[n]`（走 token 那条路） |
| `parser.c` 初始化器机器（`new_initializer` / `array_initializer2` / 指示符 / 逐元素填充） | 不能 | VLA 带初始化器被拒（三家一致；措辞不同，见 §3.5.2） |
| `parser.c` `constexpr_elem()` | 不能 | `constexpr` VLA 被拒（三家一致） |
| `dumpir.c` 全局数据发射（4 处循环 + `len`） | 不能 | 静态存储期不可能是 VLA |
| `parser.c` `_Countof` / `sizeof` / 下标 / 参数退化 | 能 | 各自有 VLA 分支；1-D 与 2-D（`sizeof`、`_Countof`、下标、逐维除法）实测与两家同值 |
| `is_static` / `is_star` | **能**（见 G2） | 写有保护，但 `func_param()` 把数组形参降级成指针时**读**了 `arr->is_star` / `arr->is_static`，而 `int a[2][m]` 的外层也是 VLA——报出 `‘[*]’ not allowed in other than function prototype scope`。已修（只对固定数组读） |
| `vla_len` / `vla_cnt` | 只在 VLA 分支里读（`sizeof` / `_Countof` / `vla_of`） | 无风险 |

结论：**没有第二处**。顺手在 `cxx.h` 联合体旁补上**读的**危险（原来只写了「不能给 VLA 写 `is_static`/`is_star`」的写危险）。

**顺带修掉的缺陷**（都不是 cleanup 特有的，是量它的过程中撞出来的）

1. **变长数组的 `len` 与 `vla_len` 共用存储 → 诊断随运行变化（既有，严重）**。`Type` 的 `len`、`is_static`、
   `is_star` 与 `vla_len`、`vla_cnt` 在同一个联合体里，所以 VLA 的 `len` 就是**长度表达式指针的低 32 位**；
   `is_compatible()` 的数组分支却拿 `len` 比长度，于是 `int (*)[3]` 是否接受 `&vla` **每次运行都可能不同**。
   实测（同一份源码连编 20 次）：普通调用 `hv(&a)` ok=11/bad=9、赋值 ok=13/bad=7、比较 20/0，
   同一现象也打在 cleanup 的 handler 校验上（ok=9/bad=11）。修法：任一操作数是 VLA 就直接视为兼容
   （两家参考实现同样接受 `int (*)[3]` ← `&vla`），固定长度仍严格比较。修后四种形状各 20/20，
   且 `int (*)[4]` 对 `int a[3]`、`char (*)[3]` 对 `int a[n]` 仍被拒。
   `test/c2y.sh` 增一条**连编 5 次**的断言——非确定性缺陷不连编几次是抓不住的。
2. **C23 拼写的 `[[gnu::X]]` 声明属性被当成未知属性**（既有）：`declspecs()` 里只有 `a->is_gnu`（即
   `__attribute__` 拼写）才进声明属性表，`[[gnu::unused]]`/`[[gnu::cleanup(h)]]` 落到
   「unknown attribute ignored」。改为按**名字空间**判断（GNU 名字空间 + `ATTR_DECL`），两种拼写等价。
3. **形参在类型前写的声明属性被丢弃**（既有）：`func_param()` 调 `declspecs(..., NULL)`，属性没有接收者，
   于是 `__attribute__((cleanup(h))) int x` 与 `[[deprecated]] int x` 都无声无息。现在把这份列表挂到
   **该形参自己的类型副本**上（`copy_type` + `ty_prepend_attrs`），与写在类型后面的那种写法一致。
4. **`gen_stmt()` 没有 `ND_COMMA` 分支**：解析器用逗号把「一条语句 + 它之后必须跑的代码」串起来
   （作用域 handler、VLA 的栈恢复），而 `gen_stmt` 的默认路径按表达式生成它。cleanup 挂在 `for` 之后时，
   链里含 `ND_DECL`（`for` 初始化的声明），直接 `gen_expr: unknown node kind 74`。补一个语句级分支，
   两半都按语句生成。
5. **声明符之后的 `[[gnu::X]]` 全被当成类型属性**（既有）：`int x [[gnu::cleanup(h)]] = 1;` 报
   「attribute 'gnu::cleanup' ignored, because it cannot be applied to a type」而 handler **不跑**，
   而 `int x __attribute__((cleanup(h))) = 1;` 正常。判据不是拼写也不是名字空间，而是**声明符的种类**——
   实测六种组合：
   - **对象声明符**之后是**声明**属性位置：`int x [[gnu::cleanup(h)]]` 两家都跑 handler、
     `int x [[gnu::unused]]` 两家都沉默、`int x [[gnu::noreturn]]` 两家都警告（cxx 报
     「‘noreturn’ can only appear on functions」，与它自己 `__attribute__` 拼写的既有行为一致）、
     `int x [[gnu::aligned(16)]]` 三家都生效（实测 `offsetof` 都是 16）。
   - **函数声明符**之后是**类型**属性位置：`void g(void) [[gnu::noreturn]]`、`[[gnu::cleanup(h)]]`、
     `[[gnu::unused]]` 两家都报「cannot be applied to a type」——这正是 `test/error.sh` 里
     `post-declarator type attribute warning` 那条既有夹具断言的行为，所以分流之后它原样通过。
   实现：`apply_postdecl_attrs()` 的静默分支加 `ty->kind != TY_FUNC`，两处 `attr_decl_apply(ty->attrs, …)`
   把 `gnu_only` 传成 `ty->kind == TY_FUNC`（函数只看 `__attribute__` 拼写，对象两种拼写等价）。
6. **`sym_attr_flags()` 的 `gnu_only` 按拼写过滤**（既有）：`gnu_only && !a->is_gnu` 让 C23 拼写的
   GNU 属性在后置位置永远读不到，`int x [[gnu::cleanup(h)]]` 于是不销毁对象。改成按名字空间判断
   （`gnu_only` 的意思是「GNU 名字空间」，不是「`__attribute__` 拼写」）；`attr_decl_apply()` 保留
   按拼写的判据（它的后置调用只服务函数声明符）。
7. **typedef 前面的声明属性被无声丢弃**（既有）：`__attribute__((cleanup(h))) typedef int T;`
   两家都警告（gcc「attribute ignored」/ clang「only applies to local variables」），cxx 什么都不说。
   两条 typedef 路径各自对 `cleanup` 警告一次，声明符**前后**两种位置都算（实测七处位置各警告一次）。
8. **只被 handler 用到的对象被误报 unused variable**（本轮引入的）：`__attribute__((cleanup(h))) int a = 1;`
   除了属性没有任何提及，而两家都沉默——handler 调用**就是**这次使用。登记 cleanup 时置
   `is_referenced`；conformance 那条拼写断言同时要求「四种拼写都不产生任何诊断」。

### G2 变长数组形参：名字的作用域与边界的求值 —— ✅ 已完成

| | |
|---|---|
| **缺口（三家实测）** | `static int f(int n, int a[n])` 报 `use of undeclared identifier ‘n’`，gcc/clang 都通过；`int a[2][m]` 多报一条 `‘[*]’ not allowed in other than function prototype scope` |
| **指令（评审给出）** | 解析函数参数时进入**函数原型作用域**，声明过的形参压进去，离开时自然丢弃；定义处现有的重新把参数压入 |
| **实现** | `func_param()` 在参数表前后 `enter_scope()` / `leave_scope()`（6.2.1p7 正是这个作用域），每个具名形参在**降级之后**建符号并压进该作用域——所以后面的边界能用它，而**原型**里的这份符号谁也不认领（不会给任何函数留下栈槽）。定义处新增两件事：①**认领**这些符号（`ParamSym` 边表按参数类型查），让它成为函数真正的形参，边界表达式因此仍指向同一个对象；②把原型作用域登记的**边界表达式**移交给函数作用域，函数体开头发射它们——这正是 6.9.1p9 要求的位置：每次调用一次、在形参都拿到值之后。原型没有函数体，边界随作用域一起丢弃，也正是 6.7.6.2p5「原型作用域里的非常量大小视同 `[*]`」的意思 |
| **一次性的状态** | 三个全局量（`proto_scope` / `proto_locals` / `param_syms`）必须**一次用完就清**：`()` 与 `(void)` 两条形参表路径各自清一次，定义处认领时清一次。否则一个 `(void)` 的 `main` 会认领上一个函数的原型留下的边界，报 `use of undefined value '%tmp0'`（实测踩到） |
| **同一区域另外两个缺陷** | ①`func_param()` 读 VLA 的 `is_star`/`is_static`（与 `vla_len`/`vla_cnt` 共用存储）→ 上面审计表的更正；②函数体开头只发射了边界**表达式**（`vla_expr[i]->rhs`）而没发射对隐藏计数器的**赋值**，于是形参的内层维度是未初始化的：`int a[2][m]` 里 `a[1][3]` 按 2 而不是 4 步进（实测得到 6 而不是 9）。两处都修 |
| **顺序陷阱** | `locals` 在函数体解析完会 `reverse_list`，而 irgen 把它的**前 nparam 项**当形参读。所以「先认领形参、再把声明符自己的符号接在下面」——我先写反了，`-ast-dump` 里 `params:` 显示成 `unsigned long` / `void *`（计数器与栈指针）才看出来 |
| **验收** | `test/c2y.sh` 三条（边界命名前面的形参并逐维取值、原型在前定义在后、`[*]` 只在原型合法）；`doc/vlaparam.sh` 接进 `doc/probes.sh`：**13 passed / 0 mismatch**（含 `static` 边界、`sizeof`/`_Countof` 内层维度、函数指针形参里嵌套的 VLA 形参、VLA 局部与形参并存、全局变量作边界） |

### G3 跳转不得跳过 variably modified 标识符的初始化 —— ✅ 已完成

| | |
|---|---|
| **依据** | 6.8.6.1p1「A goto statement shall not jump from outside the scope of an identifier having a variably modified type to inside the scope of that identifier」，以及同一件事在 switch 上的 **6.8.5.3p2**（case/default 标签落在这种标识符的作用域里时，switch 的整个 secondary block 都要在那个作用域内） |
| **实测（`doc/vmgoto.sh`，17 形状）** | 两家对「该拒/该放」的判定完全一致，**17 行全部 ok**：`goto` 跳进声明 VLA 的块、同一块里向前跳过声明、跳过**指向 VLA 的指针**、跳过 **VLA typedef**、向后跳越 VLA —— 全部拒绝；VLA 声明之后才跳、向后跳回声明之前、跳进已经进入的作用域、跳到别的块里的标签、跳越**固定长度**数组、VLA 在 switch 之外 —— 全部通过 |
| **判据** | 记下每个「variably modified 标识符」的（作用域、序号）与每个跳转点/标签点的（作用域、序号），然后：标签在该标识符作用域内 **且** 跳转点没走过它的声明（跳转点在作用域之外，或虽在同一作用域但序号在前）**且** 标签在声明之后 → 报错。序号是**严格**比较——它是「这一点之前有多少条这种声明」，所以声明本身的 seq 与该声明之前的跳转点相同（这一点我第一版写成 `>=`，三处漏报都是它） |
| **措辞** | 跟 clang：`cannot jump from this goto statement to its label` / `cannot jump from switch statement to this case label` + note `jump bypasses initialization of variable length array …`（typedef 那条 clang 用 `VLA typedef` ✓ 照抄）；note 里**多给了标识符名**（clang 不给、gcc 在另一条 note 里给），是刻意的增补 |
| **switch 侧的实现要点** | case/default 标签也要进那张（作用域、序号）表——我先只记了 `goto` 与普通标签，于是 switch 那条是靠后端 `Instruction does not dominate all uses!` 「碰巧」拒绝的：判定对了、理由不对 |
| **验收** | `test/c2y.sh` 八条（六个该拒的形状 + 措辞 + 一组合法跳转必须仍然编译运行，期望值与 gcc/clang 相同）；`doc/vmgoto.sh` 接进 `doc/probes.sh`：**17 passed / 0 mismatch** |

### G4 内联汇编（GNU `asm`）—— ✅ 已完成

| | |
|---|---|
| **依据** | ISO C 没有任何形式的 `asm`（草案全文没有这个关键字），所以整件事都是 GNU 扩展：基本 asm、扩展 asm（输出/输入/破坏列表）与 `asm goto`。两家参考实现的行为一致，差别只在措辞 |
| **实现范围** | 三种形式 + 三个限定符（`volatile` / `inline` / `goto`，任意顺序，`__asm__` 一类下划线拼写由预处理器折进关键字）；块作用域与文件作用域；用在语句表达式、循环、switch 体内均可 |
| **模板改写** | GCC 的 `%0` / `%[name]` / `%l1` / `%l[name]` / `%b0` 要变成 LLVM 的 `$0` / `${N:l}` / `${N:b}`，`%%` 变成一个 `%`，而**字面 `$` 要写成 `$$`**（`$` 是 LLVM 的操作数标记）。`%` 后面既不是数字、`[`、`%%`，也不是「修饰字母+操作数」时**原样透传**：两家都把 `%eax` 交给汇编器 |
| **编号** | 一条约束串就是一个编号空间，顺序与 GCC 的操作数编号一致（输出、输入、标签）。校验过的事实：**LLVM 的 `$N` 就是约束串里的第 N 项**（clang 的 `%N`→`$N` 是直接代换，实测含内存输出混排、`asm goto` 标签序号）。`+` 操作数在 GCC 里只算一个编号，在 LLVM 里必须拆成输出 + 一个输入：寄存器用匹配约束 `"0"`，内存用第二次 `"*m"`，且**排在所有编号输入之后**（否则 `%N` 的序号会整体错位）。标签的 GCC 编号 = 操作数个数（所以有一个输入时 `%l1` 才是第一个标签），LLVM 位置再加 `+` 输入的个数 |
| **约束转换** | 每目标一张表（`src/*/target.c` 的 `asm_cons`）：把 GCC 的固定寄存器字母与内存字母翻成 LLVM 的拼法，其余原样。x86：`a`→`{ax}`、`b`→`{bx}`、`c`→`{cx}`、`d`→`{dx}`、`S`→`{si}`、`D`→`{di}`、`t`→`{st}`、`u`→`{st(1)}`；四个目标共有 `m`/`o`/`V`→`*m`/`*o`/`*V`（**间接**操作数）、输出位 `X`→`*X`、`g`→`*imr`；arm64 另有 `Q`→`*Q`、`p`(输入)→`r`，RISC-V 有 `A`→`*A`。表是实测 clang 的转换结果得到的 |
| **间接操作数** | 内存约束的操作数交出去的是**地址**，而且 LLVM 强制要求 `ptr elementtype(T)`——只写 `ptr` 会被拒（`Operand for indirect constraint must have elementtype attribute`，实测）。元素类型就是操作数的类型；数组作为内存操作数时是**数组本身**而不是它退化出的指针（clang 亦如此）。`"m"` 打在非左值上时给它开一个临时（gcc 接受 `"m"(x+1)`，clang 拒绝，cxx 跟 gcc） |
| **多输出** | LLVM 的 inline asm 只有一个返回值，多个寄存器输出合成一个**匿名记录** `{ i32, i32 }`（`print_type` 本就把无 uid 的记录内联展开），随后逐个 `extractvalue` 再存回对象 |
| **`asm goto`** | 是 `callbr`：asm 指令本身在一个块里，块的终结指令是 `to label %fall [label %a, ...]`；标签的约束是 `!i`，排在破坏列表**之前**（clang 同）。寄存器输出写回对象的 store 必须落在 **fallthrough 块**里——callbr 必须是所在块的终结指令（否则 LLVM 报 `expected 'to' in callbr`）。标签与普通 `goto` 共用同一套解析/检查，所以 6.8.6.1p1 的 VLA 检查对 asm goto 自动生效（措辞跟 clang：`cannot jump from this asm goto statement to one of its possible targets`）；其目标**不**运行沿途的 cleanup（跳转在模板里，落空的那条路还要用它们） |
| **文件作用域** | `asm("...")` 变成 LLVM 的 `module asm`，按源码顺序打在所有 global 之前；带限定符或带操作数一律拒绝（clang：`meaningless 'volatile' on asm outside function`；gcc 连 `(` 都解析不到） |
| **目标相关的破坏列表** | amd64 每条 asm 默认追加 `~{dirflag},~{fpsr},~{flags}`（clang 对 x86 正是如此：模板改不了标志位而 asm 可能改），其余三个目标没有 |
| **踩到的坑** | ①`::` 在 cxx 的词法里**是一个 token**（属性里的作用域名 `[[gnu::const]]`），在 asm 段之间却是**两个冒号**：第一版按单冒号解析，`:::` 与 `::::` 全错（`expected ')' before ':'`）。②破坏列表最初和操作数拼进同一个字符串，于是 clobber 落在**最前面**（LLVM：`output constraint occurs after input, clobber or label constraint`）——只有「既带 clobber 又带操作数」的形状能暴露它，第一版探针没有这种形状。③`+r` 的输入半边要的是**值**：输出操作数在 parse 期保持左值（模板要写它），第一版把地址传给了 `"0"`。④asm goto 的 store 位置（见上）。⑤位域：`"m"(s.bf)` 两家都拒（cxx 跟，措辞用 gcc 的 `cannot take address of bit-field`）；`"=r"(s.bf)` clang 接受、gcc 生成非法汇编，cxx 跟 clang（经访问单元读改写，邻居位域不受影响） |
| **实测** | `doc/asm.sh`：**68 passed / 0 mismatch**，三家的判定（接受/拒绝）与运行退出码逐行一致；含 6 个基本形状、33 个操作数/模板/目标形状、5 个 asm goto 形状、3 个文件作用域形状、17 个该拒形状。另有 1 行**信息行**：位域上的 `=r` 输出，gcc 自己生成非法汇编（`movl $5, %al`）、clang 得 53、cxx 得 53——两家参考互不一致，cxx 跟 clang |
| **验收** | `test/conformance.sh` +9（4 个 `ok`、4 个 `bad`、1 个「操作数语义 + asm goto 真的跳」的运行检查），该套件 **102 → 111 passed / 0 gap**；`test/ir.sh` +7（IR 形状：`call i32 asm "movl $1, $0", "=r,r"`、内存操作数的 `elementtype`、无输出即 `sideeffect`、两个输出走 `{ i32, i32 }`、`callbr`、标签终结指令、`module asm`）；`doc/asm.sh` 接进 `doc/probes.sh` |
| **跨目标** | 四个目标（amd64 / arm64 / rv64 / rv32）的 asm IR 都被 clang 后端接收并**汇编通过**（`cxx -target <t> -S` 直通 clang 汇编器）；x86 的默认 flag clobber 只出现在 amd64 |
| **有意偏差** | ①**聚合体操作数**：寄存器约束下一块（单 piece）的记录按 ABI 的 piece 类型走，多 piece 与内存类的记录拒绝（`‘asm’ operand of aggregate type is not supported`）——两家都不拒，属已知缺口。②`%` 后跟未知字母（`%eax`、`%b` 后无操作数）原样透传（gcc 同；clang 对后者报 `invalid % escape`）。③`%=` 不做替换（gcc 此处也不替换、交给汇编器；clang 报错）。④`-pedantic` 下不报「ISO C 不允许 asm」（clang 报 `-Wlanguage-extension-token`，gcc 沉默）。⑤`asm inline` 被接受但丢弃（LLVM IR 无处安放） |

### R1 自举第一轮：cxx 编译自己的源码 —— ✅ 已完成

| | |
|---|---|
| **起因** | 现有回归网全是「构造出来的小程序」，对「合法 C 让编译器**崩掉**」和「发出去的模块 LLVM 不收」这两类缺陷覆盖很薄（前几轮各撞到一次）。这一轮把**真实代码**做成验收面：`doc/selfhost.sh` 用 `-cc1` 逐个编译 `src/**/*.c`（21 个编译单元），再把打印出的模块交给 clang。三个判定：`ok` / `IR bad` / `crash` |
| **初次记分** | **1 ok / 2 IR 非法 / 18 崩溃**（18 个是段错误，而驱动把它们折成了**静默 exit 1**） |
| **缺陷 1（崩溃）** | `check_anon_mem()`：匿名 struct/union 成员递归检查完自己的成员后**没有 `continue`**，于是又走到「普通成员」分支，把 `mem2->name`（NULL）当 token 传给 `get_struct_member()`，后者在 `mem->name->id == tok->id` 解引用空指针。gdb 确认现场：`mem2->name == 0`、`mem2->ty->kind == TY_UNION`。三个最小形状（含 cxx.h 里 `struct Node` 那种「匿名 union 套匿名 struct」）都崩；修复是那个 `continue` 与「无名成员跳过」两处 |
| **缺陷 2（崩溃）** | 同一族的另一处：`struct_designator()` 搜匿名成员时把**成员节点**而不是**该成员的成员表**交给 `get_struct_member()`。后者会一路走过给定表的末尾，于是「匿名成员之后的兄弟」也被当成它里面的名字——`.tail = 5` 报 `struct has no such member`。同一处还只认 `TY_STRUCT` 的匿名成员，漏了匿名 union（改用 `is_record()`） |
| **缺陷 3（被拒）** | **链式指示符** `.in.a = 1, .in.b = 2` 与 `[0].a = 1, [1].b = 2`：`designation()` 在指示符之后调用 `struct_initializer2()` / `array_initializer2()` 续填同一个花括号，而这两个函数把「前导逗号」当成「列表开头」，于是把逗号当表达式解析（`expected expression before ‘,’`）。修法：给两个函数一个 `comma` 参数（列表开头 false、指示符续填 true），并且返回的仍是「下一个元素之前那个 token」——逗号留给拥有列表的调用者去消费。匿名成员走的就是这条续填路径，所以它与缺陷 1/2 是同一片代码 |
| **缺陷 4（缺特性）** | `__PRETTY_FUNCTION__` 未定义。cxx 定义 `__GNUC__`（B1），于是 glibc 的 `assert.h` 走 `__extension__ __PRETTY_FUNCTION__` 分支——这一条挡住了 `src/irgen.c`、`src/main.c`、`src/util.c`。实现为 `__func__` 的又一个名字：**gcc 在 C 里的读法**（gcc 给 `add`，clang 给 `int add(int, char *)`）；跟 clang 就需要一套 C 声明符打印器，收益只是 assert 消息更好看，因此跟 gcc（差异记入 §3.5.2）。`-pedantic` 措辞照 gcc 逐字：`ISO C does not support ‘__PRETTY_FUNCTION__’ predefined identifier` |
| **缺陷 5（掩盖者）** | 子进程被信号杀死时 `run_subprocess()` 只做 `exit(1)`，用户**看不到任何诊断**——上面三个崩溃就是这么静默的。现在报 `cxx: internal compiler error: <tool> killed by signal N`；普通诊断（子进程正常退出且已打印）不受影响 |
| **记分** | 本轮 **16 ok / 5 IR 非法 / 0 崩溃**（21 个编译单元全部能过 cc1）；剩下 5 个是同一类非法 IR——聚合体参与 `?:` 时 PHI 的操作数类型不一致——由 R2 修掉，现为 **21 / 0 / 0** |
| **验收** | `doc/selfhost.sh` 接进 `doc/probes.sh`（`EXPECT_OK` 是下界：低于它就失败，涨上去要求同步抬高）；`test/conformance.sh` **111 → 116 passed / 0 gap**（匿名成员与指示符的一键运行检查、`designator` 该拒形状、缺逗号该拒形状、`__PRETTY_FUNCTION__` + glibc `assert` 实编、`-pedantic` 措辞）；`test/driver.sh` +2（替身 clang 自杀必须报 ICE；普通诊断**不得**报 ICE） |
| **顺带记录** | 探针还量出：`-fsyntax-only` 这个常用驱动开关 cxx 不认（两家参考都有），记入 §3.5.1 候选 |

### R2 自举第二轮：cxx 能编译并链接自己 —— ✅ 已完成（下一步落在 cxx2 的关键字错编）

| | |
|---|---|
| **起因** | R1 记分板停在 16 ok + 5 个 IR 非法，那 5 个是同一类：**聚合体参与 `?:` 时 PHI 的操作数类型不一致** |
| **缺陷 1（非法 IR）** | `gen_cond()` 给 PHI 的类型是**记录类型**（`node->ty`），而两个分支交上来的是**地址**（cxx 里记录值就是它的地址），LLVM 于是拒收：`'%tmp6' defined with type 'ptr' but expected '%struct.anon.1 = type { [4 x i32] }'`。修法：结果为记录时 PHI 的类型取 `pointer_to(记录)`。一处修复同时解掉 5 个单元（`int128.c`、`fp128.c`、`parser.c`、`irgen.c`、`opt_ast.c`），记分板 **21 ok / 0 / 0** |
| **缺陷 2（链接）** | **文件作用域复合字面量的对象没有链接**（6.5.2.5p5），却被发成外部符号 `.compoundliteral`：两个单元各有一个就 `multiple definition`（cxx 自己的 `src/type.c` 与 `src/irgen.c` 正好各有一个）。修法：文件作用域复合字面量给内部链接 |
| **缺陷 3（链接）** | **tentative definition 跟在 `extern` 声明之后不算定义**：符号的存储类是**第一条声明**的，而 `dump_data` 只看 `SC_EXTERN` 决定发「初始化器」还是「光类型」，于是 `extern Target T; Target T;` 发出 `@T = external global`，链接报 `undefined reference to T`。修法：tentative definition（文件作用域、无初始化器、无 `extern`）清掉 `SC_EXTERN` |
| **缺陷 4（链接）** | 同一处的另一半：**带初始化器的定义**也要清 `SC_EXTERN`（`extern const Fp128 FP128_ONE;` 之后 `const Fp128 FP128_ONE = ...` 被发成声明，链接报 `undefined reference to FP128_ONE`；`Type *bitint[129][2] = {...}` 同理） |
| **里程碑** | 三个链接缺陷修完后：`doc/selfbuild.sh` 报 **compiled 21, linked, cxx2 hello=1 itself=no**——cxx 编译出的 21 个目标文件被宿主 `cc` 链接成一个**能跑的编译器** `cxx2`（`hello=1` 表示它正确编译并运行了一个返回 1 的程序） |
| **cxx2 的第一处错编（下一轮）** | `cxx2` 读自己的 `parser.c` 失败，现象是**多字符运算符全部不被识别**（`1 && 1` → `expected ‘;’ before ‘&&’`，`#if defined X && Y` → `missing binary operator`）。用 `-dump-tokens` 一比就分开了：**cxx2 的 lexer 完全正确**（`and_and` / `equal_equal` 都对），错在**关键字到记号种类的映射**——`static` 被当成 `_Thread_local`（报 `function definition declared ‘thread_local’`）或 `extern`（报 `block scope identifier … cannot have an initializer`）。而 `parser.c` 的 `op_table[TK_NKIND][2]` 正是「函数内 static + 指定初始化器 + 二维」的形状，所以先是运算符表读成 0。结论：**第一处要修的 codegen 缺陷在「关键字/驻留字符串」这条路径上** |
| **验收** | `doc/selfhost.sh` 的 `EXPECT_OK` 由 16 抬到 **21**（回退即失败）；新增 `doc/selfbuild.sh`（两阶段：编译全部单元 → 宿主链接 → cxx2 编译 hello 与 parser.c），接进 `doc/probes.sh`，一行报出「走到哪一步」；`test/conformance.sh` **116 → 118 passed / 0 gap**（两个跨单元链接检查：`extern` 之后的定义、两个单元各一个复合字面量不许撞名） |

### R3 自举完成：cxx 造出的编译器与它自己等价 —— ✅ 已完成

| | |
|---|---|
| **起因** | R2 末尾停在「cxx2 能编译 hello，但读不了自己的 `parser.c`，现象是所有多字符运算符都不被识别」 |
| **缺陷（真凶）** | C2y 6.5.3.2 让 `E1[E2]` 直接指定 `*(E1 + E2)` 会指定的那个元素，所以下标要按指针宽度参与地址计算。但**数组下标那条路**（`ND_SUBACCESS`）把下标原样交给了 LLVM 的 GEP，而**窄于指针的 GEP 下标会被 LLVM 符号扩展**（`movsbq`，见 `clang -S` 的实测）。于是 `unsigned char` 下标 200 变成 **-56 个元素**，访问落到数组之前。cxx 自己的 `op_table[tok->kind]` 正是这个形状：`Token.kind` 是 `uint8_t`，而所有「有意思」的 kind（多字符运算符、关键字）都 ≥ 128——`sc_table[tok->kind]` 同理。这就是 cxx2 读不了 `&&`、`==`、`static`，也读不了任何系统头的原因。修法：`ND_SUBACCESS` 里按操作数的符号性把下标扩到 `long`（与同义的指针加法路径 `ND_PTRADD` 一致），见 `src/irgen.c` |
| **顺带修掉的同类缺陷** | 同一个原因让 `unsigned int` 下标 ≥ 2³¹ 也会算错（i32 下标被符号扩展成负偏移）——现在两者都按符号性扩展，arm64/rv64/rv32 三个目标一并通过 |
| **里程碑** | `doc/bootstrap.sh` 报 **cxx2 → cxx3 → cxx4 全部能跑，达到不动点**：cxx 编译出 cxx2，cxx2 编译出 cxx3，cxx3 编译出 cxx4，每一代都能编译并运行程序、也能编译 cxx 自己的 `parser.c` |
| **保真度** | 把编译器自己的 `include/` 放到它旁边（见下面的坑）后，**第二阶段的 conformance 119/0、c2y 101/0，与第一阶段逐项一致**——`bash doc/bootstrap.sh ./cxx --suites` 可复现 |
| **逐字节不动点** | **`cxx2 = cxx3 = cxx4` 二进制完全相同**（sha256 `b07d2723…`，各 805,168 字节）：21 个目标文件两两一致，`gen2` 与 `gen3` 为 21 个单元生成的 IR 也逐字节相同；用同一个编译器重复构建同样得到同一份字节。第一阶段（clang 造的 `./cxx`，1,102,320 字节）不同，那是 clang 的代码生成，属预期——两阶段的行为由上面的套件证明等价。探针最后一行报出这条结论，字节不同即失败 |
| **踩到的坑（记录）** | 一开始第二阶段「52 项失败」看着像错编，其实是**安装位置**问题：`add_default_include_paths(argv[0])` 按可执行文件位置找 `include/`，放在 `/tmp` 的第二阶段于是退回 clang 的资源目录头文件，而那些头用了 cxx 尚未实现的 `__has_feature` / `__has_extension`（`#if !defined(__STDDEF_H) \|\| __has_feature(modules)` 里的 `0 ( modules )` 被当成函数调用）。探针里用符号链接把 `include/` 放到每一代旁边；`__has_feature`、`__has_extension` 仍列为待办 |
| **验收** | 新增探针 `doc/bootstrap.sh`（默认一行「走到哪一步」，`--suites` 加跑两个套件做保真度对照），接进 `doc/probes.sh`；`test/conformance.sh` 118 → **119 passed / 0 gap**（新增「窄的无符号下标要取到那个元素」运行断言：`unsigned char i = 200; tbl[i]`） |

### R4 真实开源项目：第一轮 —— ✅ 探针与三处修复落地，四个缺陷待修

| | |
|---|---|
| **目标** | 拿真实代码库试 cxx，把「哪个文件编不过、为什么」变成可复现的数字，并修掉挡路的缺陷 |
| **探针** | `doc/realworld.sh`：源码放在 `$RW`（默认 `~/rw`，不存在就报 not present，任何机器都能跑）。每个项目**用它自己的构建系统取编译命令**（`make -n` 抽 `-c` 行），只把编译器换成 cxx；需要 configure 的项目先用宿主 cc 跑一遍，好让特性探测诚实。输出每项目 `N/M ok` 与失败诊断的 top 两类 |
| **已修（1）** | **`const` 作用于尚未完成的结构体时，副本永远停在 `size = -1`**。`type_qual()` 加限定符时复制类型，而 C 允许随后才给出结构体定义（6.7.2.3p4）；完成后更新的是原类型，副本没跟上，于是 `const S *p; ... struct S {...}; p->x` 报 `no member named ‘x’`。修法：副本挂到 origin 的链表上，完成时 `complete_copies()` 把形状传下去（`src/type.c`、`src/parser.c`）。这一处让 **zlib 的 trees.c 通过** |
| **已修（2）** | **`__builtin_expect` 缺失**。lua 有 22 处、git/cpython 同样大量使用。加进内建表：`llvm.expect.iN`，第二个操作数取自调用点（`intrinsic_args = 2` 且 `extra_arg = -1` 的新情形），行为与 clang 一致 |
| **已修（3）** | **`offsetof` 不是整数常量表达式**。cxx 自己的 `<stddef.h>` 把它写成地址常量 `&((T*)0)->m`，于是 `char pad[offsetof(S, m)]` 被判成 VLA（`field has variably modified type`），而 7.19p3 要求它是整数常量表达式；clang 的 `<stddef.h>` 用 `__builtin_offsetof`，cxx 根本不认。修法：实现 `__builtin_offsetof`（解析点即折成常量，支持 `.m` 与 `[常量]` 链），并把自带 `<stddef.h>` 改成同样的拼法 |
| **记分（本轮结束）** | **zlib 15/15** 全过；lua 28/35；libpng 15/18；git/cpython/sqlite 见下 |
| **待修（1，已定性）** | **转发 `va_list` 的 codegen 是错的**：`int sum(int n, va_list ap) { ... va_arg(ap, int) ... }` 编译通过但**运行时段错误**，同样的程序 gcc/clang 正常。cxx 现在对非变参函数里的 `va_arg`/`va_copy` 报错（比标准严），放宽之前必须先修好这条路径——这正是 `vprintf` 型函数的形状，真实代码里到处都是 |
| **待修（2，已定性）** | **lua 的 `setobj`/`setsvalue`/`l_addi` 宏没有被展开**：`lobject.h:118` 的 `setobj` 是无条件定义的，但 `lapi.c:623` 的 `setobj2n(...)` 展开后内层 `setobj` 留在解析器里成了函数调用。需要按 lua 头文件做最小复现（预处理器的嵌套展开或重扫描问题） |
| **待修（3，已定性）** | **函数指针转换被误判**：`lua_CFunction f = cast_Lfunc(dlsym(lib, sym));`（`cast_Lfunc(p) = cast(lua_CFunction, cast_func(p))`）报 `incompatible types when initializing`，gcc/clang 接受 |
| **待修（4，已定性）** | **libpng 两处**：`PNG_IMAGE_SAMPLE_CHANNELS` 宏没展开（与 lua 同类），以及内联汇编报 `expected number in address space`；git 侧 60 个单元全挂在 `expected ‘X’ after top level declarator`，看起来是探针抽 flag 不完整（git 需要生成的 `command-list.h`/`version-def.h`，`make -n` 不生成它们），下一轮先修探针再谈 git |
| **验收** | `test/conformance.sh` 119 → **120 passed / 0 gap**（新增「`__builtin_expect` 与 `offsetof` 当常量用」运行断言）；`doc/realworld.sh` 独立探针，`bash doc/realworld.sh ./cxx` 复现上表 |

### R5 真实开源项目：第二轮 —— ✅ 八处修复，sqlite 只剩一个被丢掉的静态数组定义

| | |
|---|---|
| **目标** | 把上一轮定性的四个缺陷修掉，并顺着 sqlite 的编译一路推进 |
| **已修（1）** | **转发 `va_list` 的 codegen**：`va_list ap` 作为参数会被调整成指针，而 `va_list_addr()` 没做左值转换，irgen 拿到的是**指针变量槽的地址**而不是指针值，于是展开按 `__va_list_tag` 读了那个槽——编译通过、运行段错误（gcc/clang 正常）。修法：操作数是指针时先 `lvalue_convert`。顺带把上一轮退回的「非变参函数里禁止 `va_arg`/`va_copy`」诊断去掉：7.16.1.1 只要求 va_list，`vprintf` 型函数本来就该允许 |
| **已修（2）** | **对象式宏别名不重扫描**：`#define setobj2n setobj` 是 lua/libpng 给函数式宏起别名的方式，而 cxx 把宏体当独立列表展开，于是内层 `setobj` 后面看不到调用点的 `(`，`setobj(L,a,b)` 成了隐式函数声明。修法：在当前位置跟随「宏体只是一个名字」的别名链（最多 16 步、沿途禁用，`#define A B`/`#define B A` 因此会停），链尾是函数式宏且后面跟着 `(` 时就在原地展开 |
| **已修（3）** | **`"\\u00"` 被当成通用字符名**：转义后的反斜杠不能再起头 UCN，cxx 的对扫描把第二个反斜杠读了进去——这一处正是 sqlite 的第 214604 行 `jsonAppendRawNZ(pOut, "\\u00", 4)`。修法：非 UCN 的转义按两字符整对跳过 |
| **已修（4）** | **`_Float32x` / `_Float64x` / `_Float128x`**：glibc 的 `<stdlib.h>` 用它们声明 `strtof32x` 等，没有这三个拼法就卡在系统头里。三者与 `double` / `long double` / `_Float128` 表示相同，按同样的表示接进声明符即可 |
| **已修（5）** | **`__sync_synchronize()`**：GCC 的全屏障，cxx 已有 C11 fence 的降级路径，映射成 `seq_cst` 的 `ND_FENCE` |
| **已修（6）** | **GCC 的 `__atomic_*` 家族**：与 C11 内建同形同名不同拼法，做成别名表（`__atomic_store_n` → `__atomic_store` 等）；`__atomic_compare_exchange_n` 的 `weak` 标志是参数而不是名字的一部分，单独给它一个 kind，读出该标志后按 strong 处理（strong 满足 weak 的一切）。另外这些 GCC 拼法取的是**普通对象**的地址，只有 C11 内建要求 `_Atomic`，所以检查要认识拼法 |
| **已修（7）** | **`__int128` / `__int128_t` / `__uint128_t`**：cxx 用 `_BitInt(128)` 建模 128 位整数（乘法、移位、比较与 gcc/clang 逐位一致），缺的只是名字，于是在三个 64 位目标的预定义里补上这组拼法 |
| **已修（8）** | **`{ "abc" }` 的花括号不是初始化器列表**：只有当字符串初始化的就是数组本身（元素类型相符）时才能拆括号，否则 `const char *a[] = { "so" };` 会被当成"用字符串初始化指针数组"而拒绝——两个元素时又正常，所以这个 bug 只咬单元素的那种写法，sqlite 的 `azEndings[]` 正是它。顺带把 `[[fallthrough]]` 的"必须紧邻标签"检查去掉：gcc 和 clang 都接受后跟语句、夹着用户标签、以及位于 switch 末尾（sqlite 三种都写了），cxx 原有的更严行为连 c2y 套件里的断言都是错的 |
| **sqlite 现状** | 250k 行的 amalgamation **解析与 IR 生成全过（8 秒）**，只差 LLVM 校验器一处：`@aSyscall` 有引用没有定义。已排除预处理器（cxx 与 clang 的 `-E` 输出都保留该定义且提及次数相同），是 cxx 把那个文件作用域 `static` 数组定义丢掉了；使用者所在的函数都在输出里，所以不是"没被引用"这么简单，下一轮从 `check_unused_statics` 的可达性记录查起 |
| **记分（本轮结束）** | zlib **15/15**；lua 31/35（别名修复 +3）；libpng 15/18；sqlite 0/1 但只差一处；tinycc 与 git 仍卡在探针的取 flag 环节（tinycc 需要先 configure，git 需要先生成 `command-list.h`，探针已加 `prepare` 但 git 仍需确认） |
| **验收** | `test/conformance.sh` **120 passed / 0 gap**（新增「转发 va_list 到 helper」运行断言）；`test/c2y.sh` **101 passed / 0 gap**（fallthrough 那条断言改成与 gcc/clang 一致）；`doc/realworld.sh` 报出上表 |

### R6 真实开源项目：第三轮 —— ✅ **sqlite 编译并运行**

| | |
|---|---|
| **里程碑** | 250k 行的 SQLite amalgamation 用 cxx 编译通过（约 5 秒），由宿主链接成程序后**跑出与 clang 版本逐字相同的结果**：`2 5 two,three` / `sqlite 3.53.4`，退出码同为 0。端到端程序（建表、插入、绑定参数查询、`group_concat`）见 `tests/sqlite_use.c` |
| **缺陷（1）可达性走查漏掉「解析期已标记」的符号** | `is_reachable` 在解析期就会被置 1（被发出的初始化器所引用的名字，见 `live_init`），而工作队列只在**根**循环里压入符号，于是这些名字的引用从未被跟随。sqlite 的 `aSyscall` 恰好只被这类函数引用 → 被判为未使用丢掉定义，而使用点仍在模块里 → LLVM 报 `use of undefined value '@aSyscall'`。修法：把解析期已标记的符号也种进队列（`work` 相应放大一倍） |
| **缺陷（2）引用没有用「发出时使用的名字」** | glibc 用 `__asm__` 把 `fcntl` 重定向到 `fcntl64`，于是声明发成 `@"fcntl64"` 而初始化器里的引用仍写 `@fcntl` → `use of undefined value '@fcntl'`。`print_ident()`（常量/初始化器用的打印器）不认识 asm 名，只有 `print_sym_name()` 认识；修法是让它先查 asm 名表 |
| **验收** | `test/conformance.sh` **120 → 122 passed / 0 gap**，两条新断言都经过「去掉修复即复现」的验证：<br>• 「引用要用发出时的名字」——`int alias(void) __asm__("real"); int (*p)(void) = alias;`；<br>• 「已扎根名字的引用要活着」——块作用域 static 初始化器扎根的 `chain[]` 必须把 `inner[]` 带活（去掉种子循环即复现 `use of undefined value '@inner'`） |
| **记分** | sqlite **1/1**；zlib **15/15**；lua 31/35；libpng 15/18；git 44/60；tinycc 0/11；cpython 未下载 |

### R7 真实开源项目：第四轮 —— ✅ 限定副本的完成（git 的十二个单元）

| | |
|---|---|
| **缺陷** | `struct V { volatile struct V *next; };` 里，`volatile` 会给**正在定义中**的 V 做一份限定副本，而副本要等 V 布局完成才能拿到成员。R3 加的 `complete_copies()` 用 `size >= 0` 判断「副本是完成之后做的」，可**正在定义中的结构体 size 是 0，不是 -1**（-1 只标前向声明），于是恰好把最需要的那类副本跳过了：`head->next->next` 报 `no member named 'next'`。git 的 `list.h` 正是这个形状（`volatile struct volatile_list_head *next, *prev`），它挡住了 git 的十二个编译单元。修法：无条件把标签的形状同步给副本（已有形状的副本重写一次是空操作） |
| **定位过程** | 用**差分谓词**（cxx 拒绝 ∧ clang 接受）对 delta debugging 收紧条件，才从「未声明标签」「不完整结构体」这些两家都会拒绝的岔路上摆脱出来，最终得到两行复现 |
| **验收** | `test/conformance.sh` **122 → 123 passed / 0 gap**（新增「自身结构体的限定副本里的成员」运行断言）；`doc/realworld.sh` 记分见下 |
| **记分** | git **44/60 → 52/60**；sqlite **1/1**；zlib 15/15；lua 31/35；libpng 15/18；tinycc 0/11；cpython 未下载 |

### R8 真实开源项目：第五轮 —— ✅ cpython 与 tinycc 进入记分（十五处修复）

| | |
|---|---|
| **探针重修** | 三处让真实项目「报不出数」的问题：① 每个编译单元必须在**它自己构建目录**里编译——递归 make 以 `Entering directory` 出现在输出里，tinycc 的 `lib/` 命令只有在 `lib/` 里才成立（`-I..`、`-B..` 都是相对它自己的）；② 配方文本是 **shell 引用过的**（`-DTCC_GITHASH="\"2026-10-03 mob@43c7708\""` 是一个词），切词改用 `shlex`——正则会把引号切坏，`xargs` 的引用规则又和 shell 不同；③ 已构建完的树 `make -n` 什么都不打印：非 autoconf 树用 `-B` 要配方（`-W` 不经 `MAKEFLAGS` 传给子 make，tinycc 的 `lib/` 因此只报 11 个单元），有 `config.status` 的树用 `-W`（假装每个源文件刚被改过）——**cpython 不能上 `-B`**：它连带重建 `config.status`，而这棵树的 `configure` 被重新生成过，一跑就 exit 2。解析按「目录 + 源文件」去重，`N/M` 从此是**翻译单元数**；`RW_ONLY` / `RW_CAP` / `RW_CLUSTER` 用于分项目、限步数、看完整诊断聚类 |
| **缺陷（1）命令行 `-D` 只有第一个生效** | `cmd_define_macro()` 把 `#define NAME 1` 的长度按 `len + 9` 记账，可这个名字后面追加了 `" 1"` 两个字符，实际是 `len + 11` → 下一个 `-D` 从上一条**行尾之内**开始写，覆盖掉前一条的尾巴。于是 `-DNDEBUG -DPy_BUILD_CORE` 里 cpython 的 `Py_BUILD_CORE` 消失，`Include/internal/pycore_*.h` 立刻 `#error`。修法：三处（`#include` / `#define` / `#undef`）一律用 `sprintf` 的返回值累加——手算的常数里 `#include` 还**多算**了 1，会在缓冲区里留一个 NUL，把命令行在那里截断 |
| **缺陷（2）宏实参里的宏名不会在宏体里被调用** | `subst()` 把实参预展开后给**每一个**结果 token 打上 `noexpand`。但「已展开」的染色本该只属于**展开产生**的 token：实参只是**提到**一个函数式宏名（后面没有 `(`，所以没展开），宏体才是决定要不要调用它的地方。libpng 的 `PNG_IMAGE_PIXEL_(PNG_IMAGE_SAMPLE_CHANNELS, fmt)` 正是这个形状。修法：按标准实现染色——实参预展开期间被禁用的名字记进 `paint`，只有名字命中它的 token 才打 `noexpand` |
| **缺陷（3）位域常量初始化的字节图** | `dump_init()` 按「offset 相同」给位域分组，`dump_type()` 却按「起点落在前一个元素的访问单元里就没有自己的元素」跳过——两个走法对不上，libpng 的 `read_chunks` 给 5 元素的结构体写出 8 个值，中间的填充还成了**负长度** `[-1 x i8]`。修法：初始化和类型用同一次走法；元素的值由**所有与它字节区间相交**的位域拼出来（12 位域件的访问单元是 2 字节，紧跟的 8 位域件起点落在它里面、终点在下一个元素里，必须跨元素拆开写）。验收：字节图与 gcc/clang **逐字节相同**（`0d 00 fd 19`） |
| **缺陷（4）指针型 `va_list` 被读成了值** | rv64/rv32 的 `va_list` 就是 `void *`，而线性游标策略需要的是**指针变量本身的地址**（读出游标、取值、把推进后的游标写回），`va_list_addr()` 却对它做了 `lvalue_convert`。于是 `ap.ty->base` 是 `void`，IR 里出现 `load void, ptr %p, align 1`——不是 IR。修法：只有**数组**型 va_list 在已经退化成指针时才读它（amd64 转发 va_list 的形状），指针型保留左值。**这是 `99d6bf0` 上就存在的缺陷**：`make test-rv64` 改动前是 `COMPILE-FAIL aggregate` |
| **缺陷（5）带 `-D` 时诊断的文件名** | 命令行令牌被并进主文件令牌流的前面，`preprocess2()` 却用**第一个**令牌的文件当显示名，于是带 `-D` 时文件里每条诊断都报成 `<command line>:118:21`（行号其实是对的）。修法：把真正的输入文件传给 `preprocess2()` |
| **缺陷（6）`__extension__` 之后不再识别 cast** | `unary()` 里 `__extension__` 消费掉自己后递归进 `unary()`，而 cast 是在**上一层** `cast()` 里识别的。lua 的 `#define cast_func(p) (__extension__ (voidf)(p))` 因此把 `(voidf)(p)` 读成普通括号表达式。修法：递归进 `cast()` |
| **缺陷（7）参数上的限定符破坏了函数类型兼容** | 6.7.6.3p15：判定兼容时，形参声明中的限定类型一律取**未限定**版本。tinycc 的 `tcc.h` 用 `extern float strtof(const char *, char **);` 重新声明 glibc 的 `strtof(const char *restrict, char **restrict)`，cxx 判为 `redeclared as conflicting type`——**它的三十个源文件全倒在这一条上**。修法：比较 TY_FUNC 形参时两侧都走 `type_unqual()` |
| **缺陷（8）粘贴出来的宏名没有和后面的 `(` 一起重扫** | 6.10.3.4p1 要求替换列表**连同调用之后的令牌**一起重扫，这才是「替换结果末尾是函数式宏名、而 `(` 来自源文件」时它成其为调用的原因。cxx 只单独展开宏体，看不到那个 `(`：tinycc 的 `#define ELFW(type) ELF##64##_##type` 配上 `ELFW(ST_BIND)(sym->st_info)`，`ELF64_ST_BIND` 就留在那里当隐式函数声明。修法：宏体展开完，若末尾是未被禁用/未染色的函数式宏名、且输入的下一 token 是 `(`，就把输入的实参表**拷到**替换列表末尾并推进输入游标。残留：`#define A ELFW(ST_INFO)` 这种**对象式宏**的别名链还要多一层（见下） |
| **缺陷（9）合法的不完整类型 `extern` 声明被拒** | 6.9.2p2：带 `extern` 且无初始化器的声明**不是定义**，只有定义才要求完整类型。两家都接受 `extern struct S x;`（S 永不完整）；cxx 报 `variable 'x' has incomplete type`。cpython 的每个 `PyAPI_DATA` 对象、git 头文件里的记录声明都是这个形状。修法：文件作用域看 `var->sclass & SC_EXTERN`（初始化器会清掉它、试探性定义在上面也清掉了），块作用域看 `is_extern` |
| **缺陷（10）generic `__atomic_load` / `__atomic_store` 缺失** | GCC 的非 `_n` 形式**取值的地址**而不是传值：`__atomic_load(ptr, ret, order)`、`__atomic_store(ptr, val, order)`，且取**普通对象**的地址（`_Atomic` 检查要放宽）。cpython 的 `pyatomic_gcc.h` 对每个没有 `_n` 形式的宽度都写这两个，**322 条「隐式声明」全是它**。修法：两个新 kind（不是别名，参数不同）+ 各自的实参改写；`is_gcc_atomic_spelling()` 收下这两个 kind |
| **缺陷（11）类型化拼写的溢出内建缺失** | `__builtin_{u,s}{add,sub,mul}[l,ll]_overflow` 是同一套三操作数运算的逐类型拼写（宽度本来就取操作数的类型）。cpython 自带的 mimalloc 调 `__builtin_umull_overflow`，那 13 个单元的「隐式声明」全是它。修法：18 个名字进别名表 |
| **缺陷（12）`__builtin_assume_aligned` 缺失** | `(ptr, align[, offset])` 是「指针 + 给优化器的对齐承诺」。参数按 gcc 的要求读（两个常数、对齐是 2 的幂），值就是那个指针——承诺不影响任何结果。**有意分歧**：承诺没有传进 IR（cxx 没有对应的 `llvm.assume`），丢的是优化信息不是语义 |
| **缺陷（13）`__builtin_unreachable` 缺失** | cpython 的 `Py_UNREACHABLE()` 展开成它。**有意分歧**：cxx 把它降成 `(void)0`——IR 里那个 `unreachable` 终止符的 opcode（`IR_HLT`）声明了却没实现，所以走不到这里的路径在 IR 里仍然是可达的。到达该点是 UB，所以**结果**不受影响，丢的是优化信息。改成真终止符是后续工作 |
| **缺陷（14）包在宏里的 `_Pragma` 会漏到语法分析** | `scan_pragma_op()` 只在 `preprocess2()` 的**每个片段**内扫，某些路径（cpython 的 `_Py_COMP_DIAG_PUSH` 在 `Modules/*` 的包含链上）会把展开结果复制到片段之外，`_Pragma` 于是到了分析器手里变成「隐式声明函数 `_Pragma`」。修法：`preprocess()` 在 `convert_keywords()` 之前**再扫一遍整条流**——`_Pragma` 是运算符而不是函数，留在最后的只能是漏网的，多扫一次不会有别的效果 |
| **缺陷（15）包含路径顺序让 glibc 的 `<limits.h>` 读不到** | cxx 的默认搜索顺序是「自带 include → clang 资源目录 → **gcc 目录** → 系统目录」。clang 自带的 `<limits.h>` 会先 `#define _GCC_LIMITS_H_` 再 `#include_next <limits.h>`，**正是为了让 gcc 那份让开**；gcc 目录排在下一个，`include_next` 找到的偏偏就是它，它按约定让开，链就断在那里，glibc 的 `<limits.h>` 与 `<bits/posix1_lim.h>` 从未被读到——`SSIZE_MAX` 因此不存在（cpython 的 `pyport.h` 要它，60 单元样本里 22 个单元倒在这上面）。修法：把 `add_gcc_include_paths()` 移到系统目录**之后**（clang 自己根本不加 gcc 目录，放最后既是兜底也修好了 `include_next` 链） |
| **验收** | `test/conformance.sh` **123 → 136 passed / 0 gap**，十三条新断言：一串无值 `-D` 每个都定义、诊断报文件名而不是命令行、实参里的宏名在宏体里被调用（libpng 形状）、位域常量记录字节图（值与字节图都比）、参数限定符不破坏兼容、粘贴出的宏名被源文件调用、`__extension__` 之后的 cast、不完整类型的 `extern` 声明、generic `__atomic` 读写、类型化溢出内建、`assume_aligned` + `unreachable`、宏里的 `_Pragma`、`SSIZE_MAX`。rv64 的 va_arg 由 `test/aggregate.c` 覆盖：`make test-rv64` **0 → 51 passed**。包含顺序改动后**全绿复跑**：c2y 101/0、arm64/rv64/rv32 各 51（rv32 +1 skipped）、`doc/probes.sh` 全部基线、`doc/bootstrap.sh` 仍是 cxx2 = cxx3 = cxx4 逐字节相同 |
| **记分（去重后的全量翻译单元）** | lua **31/35 → 35/35**；libpng **15/18 → 18/18**；zlib 15/15；sqlite 1/1；tinycc **0 → 10/21**（21 个单元，含 `lib/`）；git **52/60 → 508/567**；cpython **未下载 → 321/385**（已配置并宿主编译一次，生成头齐全） |
| **剩余阻塞项（按诊断聚类，本轮末次全量探针）** | **git 59**：`clar-decls.h`／`clar.suite` 缺失 31（`t/unit-tests` 的生成头没建，因为宿主编译因缺 libcurl 停在 `help.o`——探针的 `prepare` 已把这件事报到 stderr，不是 cxx 缺陷）、`curl/curl.h` 等系统头缺失 6、`incompatible types when passing argument` 13、`__builtin_va_end` 用在非可变参函数里 5、一次 `signal 11`（cxx 自身崩溃，未定位）；**cpython 64**：`cannot compile inline asm` 20、`Expected '}'` 12、`unknown token in expression` 8、`incompatible types when initializing` 7、`element 0 of struct initializer doesn't match struct element type` 6、`incompatible types when passing argument` 5、`incompatible types when assigning` 5、`__builtin_va_end` 用在非可变参函数里 4、`void type only allowed for function results` 4、`implicit declaration` 3、缺系统头（lzma/gdbm/bzlib/winsock2）若干；**tinycc 11**：`ELFW` 的对象式别名链 2（缺陷 8 的残留：宏体是**对象式**宏时，重扫的 `(` 还要多跨一层）、`__builtin_va_end` 用在非可变参函数里 1、`no member named 'gp_offset' in 'struct __va_list_tag'` 1（x86-64 的 va_list 与 `-target` 不匹配）、`expected ';' after top level declarator`（`dsohandle.c`）1，其余为参数类型不兼容 |

### P3 `<stdmchar.h>`（7.26 / N3366）

| | |
|---|---|
| **现状** | 头文件不存在。cxx 自带 7 个头，不含它；宿主 glibc 也没有 |
| **性质** | **N3366 的落地形态就是这个头**，属唯一整项未实现的提案 |
| **状态** | ✅ **两步都已完成**（见下） |
| **建议** | 分两步：①先提供头与类型/枚举（`stdc_mcerr`、`mbstate_t` 等），让 `#include <stdmchar.h>` 可用；②再补函数。函数体可先用 C 写、经 cxx 自身编译 |
| **验收** | `#include <stdmchar.h>` 通过；`__STDC_VERSION_STDMCHAR_H__` 按 7.26.14 定义；`doc/c2ycov.sh` 的该头 FAIL 转 PASS |
| **① 已完成** | `include/stdmchar.h`：7.26.14 的版本宏、7.26.15/7.26.19 的 `enum stdc_mcerr` + 同名 typedef（四个枚举名与取值逐字照抄草案）、Table 7.8 的五个 `STDC_*_MAX`、以及 7.26.16 要求的 `WCHAR_WIDTH` 与六个编码宏（`MB_UTF8`/`WCHAR_UTF32` 非零——窄执行字符集是 UTF-8、宽的是 32 位 UTF-32，其余为 0；每个都 `#ifndef` 保护，宿主头将来定义了就让它）。类型来自 `<stddef.h>`/`<uchar.h>`/`<wchar.h>` |
| **① 版本宏取值** | 头自己的版本宏跟「引入它的那一版标准」，不跟语言模式：实测 gcc `-std=c2y` 把 `__STDC_VERSION__` 报成 `202500L`，而它已有的头仍是 `202311L`，所以新头取 `202500L` |
| **① 验收** | `test/c2y.sh` 的 `gap` 转成 `runit`（枚举/宏/类型三组 `_Static_assert` + 运行检查）；`doc/c2ycov.sh` 的 `include <stdmchar.h>` 与 `doc/c2ycov2.sh` 的 `7.26 <stdmchar.h>` 双双 FAIL→PASS（c2ycov2 由 32 passed/4 library gap 变为 **33 passed/3 gap**） |
| **② 已完成** | 7.26.2 / 7.26.3 的函数族共 **50 个**（`mc`/`mwc`/`c8`/`c16`/`c32` 五个来源 × 五个目标 × 单/多单元两族），函数名与草案逐字一致：`grep -oE 'stdc_[a-z0-9]+nrto[a-z0-9]+' doc/n3685.txt` 得到 50 个名字，与头里的 50 个宏调用一一对应 |
| **② 函数体放哪** | 标准要求它们有外部链接；cxx 没有自己的运行库，所以是头里的 `static inline` 定义。对**包含该头**的程序（它本来也只能这样拿到声明）与外部定义等价；自己声明而不包含头的程序链接不上——这条偏差写进了头注释 |
| **② 编码状态机** | 不需要：7.26.16 要求这个头把 7.33.1 的编码宏放进作用域，而本实现声明的就是 `MB_UTF8` / `WCHAR_UTF32`，于是 `mc` 与 `c8` 同为 UTF-8、`mwc` 与 `c32` 同为 UTF-32，三种编码都是**无状态**的。未完成的序列因此按 7.26.2 报 `incomplete_input` 而不吞进 `mbstate_t`——标准只要求「不消耗、不修改」，`mbsinit` 也始终为真 |
| **② 共用内核** | 三个 static inline 助手：按编码取/放一个码元（值按无符号解释，7.26.1 NOTE 1）、解码出一个码点（UTF-8 的过长形式/代理区/越界、UTF-16 的孤立代理、UTF-32 的越界都判 `invalid`；不足则 `incomplete_input`）、算出编码长度并写出。两个宏据此生成 25 + 25 个函数；多单元族按 7.26.3 para 6 的七步「反复调用单单元族」 |
| **② 验收** | `test/c2y.sh` 的 `runit`：往返（c8→c16→c32→c8 恒等，含 1/2/3/4 字节四种长度）、`mc`/`mwc` 与声明的编码一致、计数模式（`output` 为空时只数）、三种失败码各自「什么都不动」的规则、空输入与空指针输入。**同一份头用 clang 编也通过**（`clang -std=c23 -I include`），说明语义不是靠 cxx 的宽松实现撑起来的 |

**顺带修掉的缺陷：`?:` 的分支自己开块时，合并 PHI 指错了前驱**

写 7.26 的编码内核时撞上的（`stdc_mc_len()` 那行三点嵌套 `?:`）：`int f(int a, int b) { return a ? 1 : b ? 2 : 3; }`
——任一支里嵌套 `?:` 即可，`&&`/`||` 同理——让 cxx 生成**非法 IR**，clang 报
`PHI node entries do not match predecessors!`，编译直接失败。

根因在 `gen_cond()`：它把「跳进合并块」和「合并 PHI 的前驱」都记成**进入分支时**的那个块
（`t_blk`/`f_blk`），可分支里的 `gen_expr` 可能自己开块（嵌套 `?:`、`&&`、`||`、语句表达式），
控制流实际是从**分支结束时**的块离开的。`gen_logand()` 早先已经按后一种写法修过（那次留了注释
「the PHI below must name that block」），`gen_cond()` 漏了同一处；现在照同一模式补上：分支生成完
保存 `t_end`/`f_end`，跳转与 PHI 都用它。

`test/c2y.sh` 增一条断言，把三种嵌套形状（else 支、then 支、括号）连同取值一起跑出来。这与 P2 的
`_Atomic` 聚合体是**同一类**缺陷：不是诊断不对，而是**发出去的模块 LLVM 拒收**，而当时全套件是绿的
——现有回归网对「IR 合法性」的覆盖仍然偏薄。

### P4 复合类型：数组不能由后续声明补全（N3652）—— ✅ 已完成

| | |
|---|---|
| **现状** | `int a[]; int a[10];` → `invalid application of 'sizeof' to incomplete type`。clang 得 40。这是 **C89 起**的规则（6.9.2 tentative definition + 6.2.7），非 C2y 新增 |
| **已可用的** | 同一族声明内的补全（`static int b[]; static int b[4] = {...};`）、结构体与函数类型的复合 |
| **验收** | 上述两行能取 `sizeof`；`doc/c2ycov.sh` 与 `c2ycov2.sh` 的 3 项 FAIL 转 PASS；`test/decl.c` 增用例 |

### P5 特性宏与实现对齐 —— ✅ 已完成

| 项 | 结论 |
|---|---|
| `__STDC_IEC_60559_TYPES__` | **已定义**为 `202311L`（5 张目标宏表）。`_Float16/32/64/128` 是关键字，而 H.5.1 正是以该宏为条件——此前这条依据不存在，只靠 GNU 兼容宏恰好让 glibc 没写出 `typedef float _Float32;`。实测 `/usr/include` 下**没有任何头文件**引用该宏，所以定义它零风险（四目标全套件绿） |
| `__STDC_IEC_60559_BFP__` | **有意不定义**：Annex F 合规需要 fenv 与浮点异常，cxx 不建模 |
| `__STDC_IEC_60559_DFP__` | **有意不定义**——按 6.10.8，这就是「不支持十进制浮点」的表达方式 |
| `__STDC_IEC_60559_COMPLEX__` | 不定义，且 `__STDC_NO_COMPLEX__` 已定义为 1（标准要求二者互斥） |
| `__STDC_VERSION__` | 保持 `202311L`；若对外声明 C2y 支持再更新 |

> **更正（本计划原写错）**：原表里要求「把 `__STDC_NO_DECIMAL_FLOAT__` /
> `__STDC_NO_DECIMAL128_FLOAT__` 定义为 1」——**这两个宏在 N3685 里根本不存在**。
> 6.10.8 只列 `__STDC_NO_ATOMICS__`、`__STDC_NO_COMPLEX__`、`__STDC_NO_THREADS__`、
> `__STDC_NO_VLA__` 四个；十进制不支持是由 `__STDC_IEC_60559_DFP__` 的**缺席**表达的。
> 实测 gcc 与 clang 也都不定义任何十进制 "NO" 宏。已按标准改。

### P6 驱动：`-pthread` —— ✅ 已完成

`-pthread` 是**两个层面**的一个开关，两者都需要：链接器要看到线程库，预处理器要看到
`_REENTRANT`（POSIX 专为「使用线程的程序」保留）。链接标志走 `ld_extra_args`——与
`-l`/`-Wl,` 同一个位置、同一个理由（必须在目标文件之后）。

验证：`<threads.h>` 的 `thrd_create`/`thrd_join` 与 `<pthread.h>` 的
`pthread_create`/`pthread_join` 两个真实多线程程序，cxx 与 clang 的输出与退出码相同；
`#ifdef _REENTRANT` 在 `-pthread` 下为真；`-lpthread` 仍然可用。`test/c2y.sh` 增一条端到端回归。

### P7 `test/c2y.sh` 回归网 —— ✅ 已完成

把本轮五轮探针收敛成 `test/c2y.sh` 纳入 `make test`，最小集：

1. 常量左侧的 `&&` / `||`（§5 修复的折叠极性）
2. 右操作数为嵌套 `?:`/`&&`/`||` 的短路（§5 修复的 PHI 前驱）
3. `NAN` / `INFINITY` / `HUGE_VAL` 可用
4. `_Atomic struct` 的 `atomic_load`
5. `int a[]; int a[10];`
6. `-lm` 端到端（已在 `test/conformance.sh`）
7. `<stdmchar.h>` 可包含

依据见 `doc/n3685-conformance.md` §10 的 6 条覆盖空洞。

### D1 警告分组用位 flags —— ✅ 已完成

| | |
|---|---|
| **设计** | `uint32_t opt_wgroups` 取代原先「全开或全关」的 `bool opt_nowarn`；`warning(int group, Token*, ...)` 按组判定。**每个组默认开启**——这正是改造前 cxx 的行为（除了 `-w` 没有任何控制手段），所以这是纯粹的能力增加、不破坏既有输出；`-w` 仍然全局静默，`-Wall`/`-Wextra` 显式全开，`-W<组>` 开、`-Wno-<组>` 关 |
| **组表**（`main.c`，驱动器接口） | `deprecated-declarations`、`unused-result`（`[[nodiscard]]`）、`attributes`、`invalid-noreturn`、`builtin-macro-redefined`、`cpp`、`atomic-memory-ordering`、`extra-tokens`、`unused-variable`、`implicit-function-declaration`，外加 7 个别名（见下） |
| **组名经 gcc/clang 实测校正** | `doc/wgroups.sh` 为**每一条** cxx 诊断构造等价情形，跑 gcc 与 clang 并读取 `[-W...]` 标签。**三处当初是猜错的**：noreturn 返回（cxx 曾用 `return-type`，clang 是 `invalid-noreturn`、gcc 根本不报）、重定义/取消定义内建宏（曾归 `cpp`，clang 是 `builtin-macro-redefined`）、指令后多余记号（曾归 `cpp`，clang 是 `extra-tokens`、gcc 是 `endif-labels`） |
| **别名** | 两家对同一诊断常取不同名字，因此**一个组可以有多个名字**（表就是 {名字, 位} 的扁平列表，天然支持）：`invalid-memory-model`↔`atomic-memory-ordering`、`unknown-attributes`/`ignored-attributes`↔`attributes`、`extra-tokens`↔`endif-labels`。已实测 `-Wno-unknown-attributes` 确实消掉属性警告 |
| **未知组** | `fatal error: unknown warning group: -Wno-bogus-option`（对齐 gcc/clang）。此前 `-W*` 被一律「接受并丢弃」，`test/driver.sh:110` 的 `-Wall` 与 `:590` 的 `-Wl,` 依赖这条路径，改造后前者走组表、后者仍由 `-l` 分支提前消费 |
| **注意** | `-Werror` 必须在组解析**之前**放行，否则会被读成名为 `error` 的组而报错——实现时踩过一次 |
| **覆盖的 19 个诊断点** | `parser.c` 12 个（deprecated ×2、nodiscard、noreturn、属性 ×4、原子内存序 ×3、空 case 范围）· `preprocess.c` 7 个（全部 `WG_CPP`） |
| **验证** | `doc/warn.sh`：`-Wno-deprecated-declarations` 从「警告数仍为 1」变为 **0**，再 `-Wdeprecated-declarations` 能恢复；`-Wno-attributes` 关掉属性警告而 `-Wno-deprecated-declarations` **不影响**它（组间独立）；三种未知组拼写均被拒；`-Wall`/`-Wextra`/`-Werror`/`-w` 不受影响 |
| **测试** | `test/conformance.sh` 的两条 `known gap` 转为 3 条真实断言，**该套件从 73 passed/3 gap 变为 76 passed/1 gap**（剩下的是 D3 的 `-pedantic`） |

### D3 `-pedantic` 与 `$` 标识符 —— ✅ 已完成

| | |
|---|---|
| **选项** | `-pedantic`、`-pedantic-errors`、`-Wpedantic`（同一模式的 `-W` 拼写），以及 `-Wno-pedantic` 关闭 |
| **`$` 标识符** | `is_ident1`/`is_ident2` 原先无条件接受 `$`；现在默认接受（GNU 扩展，与 gcc/clang 一致）、`-pedantic` 下**拒绝** |
| **GNU 扩展点** | 新增 `pedantic(tok, fmt, ...)`：非 pedantic 下完全静默，`-pedantic` 下告警，`-pedantic-errors` 下报错。**这不是警告组**——`-pedantic` 是模式而非分组，与 gcc/clang 的处理一致 |
| **已接线** | 五个 `// [GNU]` 标注点全部接线：statement expression、`?:` 缺中项、computed goto（`goto *ptr`）、`__FUNCTION__`、labels-as-values（`&&label`）。前四个与 gcc **逐字逐列一致**（`doc/ped.sh` 打印两家原文对照）：`1:25 braced-groups`、`2:29 middle term of a ‘?:’`、`3:20 ‘goto *expr;’`、`4:31 ‘__FUNCTION__’ predefined identifier` |
| **口径** | labels-as-values 沿用第一轮定下的 "ISO C forbids taking the address of a label"——gcc 那句是 "taking the address of a label is non-standard"、位置也不在 `&&` 上，不再追 |
| **`-w` 与模式** | `-w -pedantic` / `-w -pedantic-errors` 都必须静默：实测 gcc 与 clang 四种组合全 0，而 cxx 原先在 `-w` 下照样报；`pedantic()` 现在先看 `opt_nowarn` |
| **`,##__VA_ARGS__` 不接** | 三家不一致：gcc 在 `-std=gnu23` 下完全沉默（严格模式直接报语法错），clang 用的是**不属于 pedantic 的** `-Wgnu-zero-variadic-macro-arguments`，报在宏定义处。cxx 保持现状（接受且沉默），理由记在这里 |
| **实现踩坑** | `-Wno-pedantic` 一度写成开启模式：进到该分支时 `no-` 前缀已被剥掉，`off` 标志才是依据 |
| **测试** | `test/conformance.sh` 的最后一条 `known gap` 转为 3 条真实断言（`$` 被拒、GNU 构造告警、非 pedantic 下静默）——**该套件现为 79 passed / 0 known gap**，登记的缺口全部清零 |

### D2 可增补的警告（依据 GCC 手册 Warning-Options 整理）

> 这张表是**候选清单**，不是待办清单：其中已成组的见 E1–E4、D1、N1、S1
> （`unused-const-variable`、`static-local-in-inline`、`constant-conversion`、
> `literal-conversion`、`float-conversion`、`literal-range`、`sign-compare`），
> 表内其余行仍按需要取用。

来源：<https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html>。只列**常用且实现简单**的，
按「是否需要数据流分析」筛掉 `-Wuninitialized` / `-Wmaybe-uninitialized` /
`-Warray-bounds` / `-Wstringop-overflow` 一类。

**A. `-Wall` 内、cxx 实现代价很小**（优先做）

> **已完成两条**（`-Wshift-count-negative` / `-Wshift-count-overflow`）。挂点在 **`fold_node()`**
> ——不是解析器。理由：`fold_ast` 在解析**之后**才跑，所以在 `binexpr()` 里只能看到字面量；
> `x << (32 + 1)`、`x << (1 << 5)`、`x << (64 - 0)` 折完才是常量，那时才可判。而折叠之后
> `node->rhs` 是常量、`node->lhs` 仍可以不是——正是 `x << 33` 违反 6.5.7 的情形。
>
> 折叠器里**本来就有这条检查**（`if (sh < 0 || sh >= width) return NULL;`，拒绝折叠越界移位），
> 只是静默拒绝、没有诊断；现在给它接上诊断，并**删掉解析器里的副本**以免报两次。
> 附带好处：折叠把 `-1` 变成了字面量，解析器版本里那个 `ND_NEG` 特例不再需要。
>
> 实测与 gcc/clang **逐行一致**：12 行用例命中第 1/3/4/7/8/11/12/14 行
> （4 条字面量 + 3 条折叠后才成常量），三家完全相同；两个组互相独立，`-w` 全部抑制。
>
> **教训**：放在解析器里的第一版会漏掉 5 条溢出中的 3 条。

| 选项 | 判据 | 代价 |
|---|---|---|
| `-Wchar-subscripts` | `ND_ARRAY_REF` 的下标表达式类型是 `char` | 极小 |
| `-Wduplicate-decl-specifier` | 声明说明符里重复的 `const`/`volatile`/`restrict`/`_Atomic` | 极小 |
| `-Wsizeof-pointer-div` | `ND_DIV` 两侧呈 `sizeof(p) / sizeof(*p)`、`p` 是指针 | 极小 |
| `-Wshift-count-negative` / `-Wshift-count-overflow` | 常量移位计数为负 / ≥ 位宽（**默认开启**） | ✅ **已完成** |
| `-Wunknown-pragmas` | 预处理器遇到 cxx 不认识的 `#pragma` | 极小 |
| `-Wunused-value` | 表达式语句的值未被使用 | 小 |
| `-Wunused-function` / `-Wunused-const-variable` | **调用图分析**（见下），不是按名字查引用 | ✅ **已完成**（E4） |
| `-Wunused-label` | 声明的标签从未被 `goto` | 小 |

**`-Wunused-function` 必须是调用图分析，不能按名字判「有没有被引用」**

`doc/uf.sh` 实测（clang/cxx 对照）：

```
static int never_used(void);          真正没人用
static int a(void){return b();}       与 b 互相引用
static int b(void){return a();}
static int used(void);                main 调用
static int via_extern(void);          只被非 static 的 exported() 调用
static int addr_taken(void);          地址被取
int (*fp)(void) = addr_taken;
int main(void){ return used() + exported(); }
```

| | 发射到 IR（`-O0`） | 警告 |
|---|---|---|
| clang | `addr_taken` `exported` `main` `used` `via_extern` | 只报 `never_used` |
| cxx | **全部 8 个**（多出 `a` `b` `never_used`） | 无（组都不存在） |

clang **不报 `a`/`b`**——两个 static 互相引用，按名字查会有引用、按可达性则是死的。这正是必须做
调用图的理由。gcc 同样只报 1 条。

**E4 实现（已完成）**：不必事后遍历 `ND_FUNCALL` 找边——**边在解析时就能拿到**。标识符解析成
`ND_VAR` 的那一点（`primary()`）是唯一知道「谁在引用」的地方，所以调用图在那里建：

- 每条边写进**正在解析的那个符号**：函数体里的名字归 `cur_fn`（`Sym::refs[]` / `num_refs`，这就是
  保留下来供后续优化使用的调用图），文件作用域初始化器里的名字归**那个对象**（新增 `cur_init`）
- 静态函数指针表没人用，表里装的函数也活不下来——实测 clang 同样把它删掉
- 两者都不是（文件作用域的常量表达式，如 `sizeof x`）→ 当作**根**：名字在源码里出现，保守留下

以**外部可见的定义为根**（函数要有函数体、对象不能只是声明），沿边做一次可达标记。剩下内部链接的
定义就是死代码：报 `-Wunused-function` / `-Wunused-variable` / `-Wunused-const-variable`，并且
**不打进 IR**（`irgen.c` 与 `dumpir.c` 跳过 `Sym::is_dead`）。诊断按源序输出（`globals` 是倒序链表，
先读进数组再倒着走）。

两个刻意的取舍：
- **普通标签不影响**：`case 1: y=1; l: case 2:` clang 不报（gcc 报），cxx 跟 clang——E2 里已经用过同一条
- **`static inline` 不报**：gcc 对未用的 `static inline` 沉默、clang 会报，而头文件里成片的
  `static inline` 正是这种写法，所以跟 gcc

**E4 实测**（`doc/uf.sh`，`test/c2y.sh` 增两条断言）。一个文件里放上互相调用的 `a`/`b`、只被死函数
调用的 `only_dead`、只被死函数写过的 `kept`：

| | gcc | clang | cxx |
|---|---|---|---|
| 警告 | 2（`never_called`、`dead`） | 3（多 `kept`，但组是 `-Wunused-but-set-global`） | **6**（5 个函数 + 1 个对象） |
| 发射到 IR | — | `main`、`called` | `main`、`called`（**与 clang 相同**） |

也就是说：cxx 的**发射**与 clang 一致，而**诊断**比两家都彻底——`a`/`b` 互相引用、`only_dead` 只被
`dead` 调用、`kept` 只被 `dead` 写，按名字都「有引用」，按可达性都是死的。clang 自己这两件事也不
一致：它把 `target`/`table` 从 IR 里删掉，却不为 `target` 报警告（名字被提到过）。

**块作用域 static** 是同一张图上的另一种情况：它属于某个函数，所以既不是根，也不随函数体一起
被删——实测 clang 对未使用的块作用域 static **照报照发**（`unused variable 'q'` + `@f.q`），
只有文件作用域的才即报即删。cxx 与两家一致：报 `unused variable ‘q’`（名字取声明 token 的拼写，
不是 IR 里的 `f.q`），`@f.q` 留在输出里。

**E4 后半（本轮落地）：未求值操作数里的名字不再算「使用」**

先把 clang 的边界实测出来（`doc/uf.sh` 里那条 `sizeof_only` 加一份 13 形状的对照；`d` = 默认旗标、
`W` = `-Wall -Wextra`、`e` = 是否活到输出里）：

| 形状 | gcc | clang | cxx（改前 → 改后） |
|---|---|---|---|
| `static int x; int a[sizeof x];` | 沉默 · 保留 | `W`+1 · **删** | 沉默·保留 → **报 + 删** |
| `enum { E = sizeof x };` | 沉默 · 保留 | `W`+1 · **删** | 沉默·保留 → **报 + 删** |
| `_Static_assert(sizeof x == 4, "")` | 沉默 · 保留 | `W`+1 · **删** | 沉默·保留 → **报 + 删** |
| `static int x; int g = sizeof x;` | 沉默 · 保留 | `W`+1 · **删** | 沉默·保留 → **报 + 删** |
| `int f(void) { return sizeof x; }` | 沉默 · 保留 | `W`+1 · **删** | 沉默·保留 → **报 + 删** |
| `static int f(void){…} (sizeof &f) != 0` | 沉默 · 保留 | **沉默** · **删** | 沉默·保留 → **报 + 删** |
| `typeof(x) y = 1;` / `typeof_unqual` | 沉默 · 保留 | `W`+1 · **删** | 沉默·保留 → **报 + 删** |
| `_Countof(x)`、`__alignof__(x)` | 沉默 · 保留 | `W`+1 · **删** | 沉默·保留 → **报 + 删** |
| `static int *p = &x;`（有求值使用） | 沉默 · 保留 | 沉默 · 保留 | 沉默·保留（不变）✓ |
| `int f(void) { static int q; return sizeof q; }` | 沉默 · 保留 | 沉默 · 保留 | 沉默·保留（不变）✓ |

读出来的边界是两句话：**发射看「有没有被求值代码到达」**（`sizeof &f` 那条 clang 连警告都不给，
照样把函数删掉；`static int *p = &x;` 里 `&x` 是真求值，两家都留），而**警告**另有一条
「地址被取过就不报」的豁免。所以 cxx 收紧的是同一件事：6.5.3.4p2 的**未求值操作数**。

实现放在解析器里那两处「只知道类型、不需要值」的地方：

- `parser.c` 新增 `uneval_operand` 标志 + `parked[]`/`num_parked`/`park_ref()`/`unpark_refs()`：
  标志升起期间，`primary()` 本来要加的图边（或「根」标记）**先停放**而不是立刻生效
- `sizeof`/`_Countof`/`__alignof__` 与 `typeof`/`typeof_unqual` 在解析操作数时升起标志，
  拿到类型后决定：类型是 **VLA** 就 `unpark_refs()` 补上（6.5.3.4p2：变长操作数是要求值的，
  它的长度表达式真的会跑），否则丢掉
- **块作用域 static 不参与停放**：两家都不删它、clang 对 `sizeof q` 也沉默，停放只会让 cxx
  去报一个它照样发射的对象
- `Sym::is_referenced`（E3 的「名字从未解析」）不受影响：那是关于源码文本的问题，不是关于运行的问题

**「不许删活代码」的不变量有专门的断言**：`static int n; int f(void){ return sizeof(int[++n]); }`
必须是**报也不报、删也不删、链接得上、跑得出对的值**（`test/c2y.sh`：`runit` + 链接运行；
`typeof(int[++n]) v;` 同样一条）。`n` 在 IR 里出现 4 次，与 clang 相同。

**E4 后半之二：`_Generic` 的三个未求值位置（同一条规则的补齐）**

6.5.2.1p3 和 6.5.3.4p2 是同一句话的两个落点：控制操作数不求值、未被选中的关联表达式不求值。
实测七个形状（`d` = 默认旗标、`W` = `-Wall -Wextra`、`e` = 是否活到输出）：

| 形状 | gcc | clang | cxx |
|---|---|---|---|
| `_Generic(x, int: 1, default: 0)`（控制操作数提到对象） | 留·沉默 | **删 + `W`** | **删 + 报** ✓ |
| `_Generic(f(), int: 1, default: 0)`（控制操作数提到函数） | 留·沉默 | **删 + `W`** | **删 + 报** ✓ |
| `_Generic(0, double: f(), default: 0)`（未选中关联） | 留·沉默 | **删**·沉默 | **删 + 报** ✓ |
| `_Generic(0, int: 1, default: g())`（被丢弃的 default） | 留·沉默 | **删**·沉默 | **删 + 报** ✓ |
| `_Generic(0, double: y, default: 0)`（未选中关联读对象） | 留·沉默 | **删**·沉默 | **删 + 报** ✓ |
| `_Generic(0, int: f(), default: 0)`（**选中**的关联） | 留 | 留 | 留 ✓ |
| `_Generic(int[++n], int *: 1, default: 0)`（变长类型名） | 留 | 留 | 留 ✓ |

实现沿用同一套停放机制：控制操作数整段升起标志（取得类型后再决定），每个类型关联的**表达式**
单独升起（选中的 `unpark_refs`、未选中的丢掉），`default` 因为决定要等到列表末尾，所以记住
`def_mark` 到最后再结账（此时它的条目一定是表尾——中间那些关联各自已经结清）。关联的**类型名**
不升起标志：变长边界属于外层作用域，仍会被发射。cxx 比 clang 多出来的那几行警告是它一贯的
默认开 unused 组，发射面与 clang **逐行一致**。

**顺带量出并修掉的三个缺陷**

- **文件作用域复合字面量（本轮 E4 自己引入的）**：`int bound = sizeof((struct S){&x});` ——
  复合字面量是**照旧发射**的匿名全局，它的初始化器里 `&x` 被停放丢掉，于是 `x` 判死删除，而模块里
  还留着 `@.compoundliteral = global %struct.S { ptr @x }`，cxx 自己的校验器拒收
  （`use of undefined value '@x'`）。clang 链接正常（它把两者都删了）。修法：新增 `live_init` ——
  **会被发射的初始化器（块作用域 static、静态存储期的复合字面量）里的名字直接成为根**，因为它
  在装入时就跑；解析这种初始化器时同时把停放标志落下（里面的东西不是未求值的）。
  优先级是「最内层说了算」：这个初始化器里再套一个 `sizeof x`，那个 `x` 仍然被丢掉（内层 sizeof
  照样要折叠）。
- **块作用域 static 的初始化器（既有，比 E4 早）**：`static int dead(void) { static struct S s = {&x}; … }`
  —— `s` 的初始化器引用 `x`，而这条边记在**死函数** `dead` 名下；`s` 本身因为是块作用域 static
  照旧发射，`x` 却被删掉，同样是 `use of undefined value`。`live_init` 一并修掉。
- **6.5.2.1p2/p3（`_Generic` 自己的语义，既有）**：cxx 把**类型名**形式的控制类型也做了数组/函数
  退化，还会**求值**类型名里的变长边界。实测三家：

  | 形状 | gcc | clang | cxx 改前 | cxx 改后 |
  |---|---|---|---|---|
  | `_Generic(int[3], int *: 1, default: 9)` | 9 | 9 | **1** | 9 ✓ |
  | `_Generic(int (void), int (*)(void): 1, default: 2)` | 2 | 2 | **1** | 2 ✓ |
  | `_Generic(a, int *: 5, default: 0)`（表达式形式，必须退化） | 5 | 5 | 5 ✓ | 5 ✓ |
  | `_Generic(int[++n], …) + n * 10` | 9 | 9 | **1**（退化且 `n` 变 1） | 9 ✓ |

  p2 说转换只属于「赋值表达式操作数」，类型名就是它写下的那个类型；p3 明说类型名里的 size
  expression 不求值。修法是：类型名形式不退化（`tyname` 分流），并在控制操作数与关联类型名
  解析处把 `scope->vla_num` 存下再恢复——sizeof 会**接管**变长边界的表达式，`_Generic` 则按
  标准把它们丢掉。变长类型名提到的名字仍保留（`unpark_refs`）：这里什么都不发射，但 clang 也不报，
  cxx 跟参考实现。

**探针的一个假阳性（方法论）**：E4 的发射检查用 `@[A-Za-z0-9_.]*NAME([^A-Za-z0-9_]|$)`，
而 `@llvm.memcpy.p0.p0.i64` 里的 `memcpy` 恰好能以 `y` 结尾匹配 `NAME=y`，于是「cxx 还在发射」
是探针的错。改成 `@([A-Za-z0-9_]+\.)*NAME` 后 13 个形状的 cxx 列与结论一致（结论本身另有
`test/c2y.sh` 的子串断言与 `doc/uf.sh` 的 clang 对照为证）。

**残留（记在 3.5.2）**：那份「地址被取过」的警告豁免 cxx 没有跟（只被 `sizeof &f` 提到的函数，
clang 沉默但照删，cxx 报 `unused function` 并照删——cxx 更吵，但它说的是实话：这个函数确实不发射）；
以及未求值位置里的名字 cxx 一律按自己的默认开 unused 组报出来，而 clang 只在 `-Wall` 下报
`-Wunneeded-internal-declaration`——发射一致，诊断更彻底，这是 E4 一贯的取舍。

**顺带修掉的缺陷：没有名字的记录被 `str(0)` 命名**

`make test` 在这一轮红过一次：`empty case range compiles, links and runs` 报 LLVM 的
`expected '=' after name`，指向 `%struct./tmp/cxx-c2y-G7B7Kt/range.c = type { i32, i32, ptr, ptr }`。
链子是：目标自建的 va_list 记录是 `static Type elem`（`id` 为 0），`insert_ty()` 用 `str(ty->id)`
命名，而 `str(0)` 是**预处理最先 intern 的那个字符串**——它可能是宏名（正常时就是
`%struct.__INT_FAST8_TYPE__`），也可能是文件路径；路径带 `/`，LLVM 的标识符不接受，于是同一个
程序有时编得过、有时编不过。修法两半：

- amd64 / arm64 的 va_list 记录直接取 clang 的名字：`%struct.__va_list_tag` / `%struct.__va_list`
  （实测与 clang 逐字一致；rv64/rv32 的 va_list 是裸指针，没有这个问题）
- 其余没有名字的记录（ABI 聚合等）编成 `struct.anon.N`，不再读 `str(0)`

`test/ir.sh` 增一条断言钉住它。
| `-Wparentheses`（比较链） | 关系/相等节点的操作数本身是关系节点（`x<=y<=z`） | 小 |
| `-Wswitch-unreachable` / `-Wswitch-bool` / `-Wswitch-outside-range` | switch 的几种形状（**默认开启**） | 小～中 |

**B. `-Wall` 内、需要一点结构分析**

| 选项 | 判据 |
|---|---|
| `-Wreturn-type` | 「控制流到达非 void 函数末尾」——cxx 目前**完全没有**这条；手册说明它在 C 里属于 `-Wall` |
| `-Wswitch` | switch 的索引是枚举类型却缺 `case`（有 `default` 则豁免） |
| `-Wmissing-braces` | 聚合初始化未完全加括号 |
| `-Wsequence-point` | `a = a++`、`a[i++] = i` 一类 |

**C. `-Wextra` 内**（用 `WG_EXTRA_ONLY` 一类标记，`-Wall` 不开）

| 选项 | 判据 | 代价 |
|---|---|---|
| `-Wempty-body` | `if (x);` / `while (x);` 空体 | 极小 |
| `-Wmissing-field-initializers` | 聚合初始化只给了一部分成员 | 小 |
| `-Woverride-init` | 同一成员被初始化两次 | 小 |
| `-Wunused-parameter` | 形参从未被引用——E3 刻意跳过的那类，现在补上只需给形参加 token | 小 |
| `-Wimplicit-fallthrough=3` | 即 E2 | 中 |

**D. 由本次核对发现的 D1 更正**

- `-Wignored-attributes` 与 `-Wattributes` 是**两个不同的组**。手册原文：前者在「编译器决定丢弃
  属性」时报警，后者管「属性未知 / 用错位置」。D1 把 `ignored-attributes` 当作 `attributes` 的
  别名合并了，应当拆开。
- `-Wunused-but-set-variable` 与 `-Wunused-variable` 也是两个独立选项（都在 `-Wall` 内）。
  D1/E3 目前把「只写不读」归到了 `unused-variable`，应当拆出 `unused-but-set-variable`。

**E. `-w` 与具体组的优先级（实测，非文档推断）**

| 命令行 | gcc | clang | cxx |
|---|---|---|---|
| `-w -Wunused-variable` | 0 | 0 | 0 |
| `-Wunused-variable -w` | 0 | 0 | 0 |
| `-Wall -Wno-unused-variable` | 0 | 0 | 0 |
| `-Wno-unused-variable -Wall` | 0 | **1** | **1** |

`-w` 在**两种顺序**下都压过具名组——手册里「更具体的优先」说的是 `-Wall`/`-Wextra` 与具名组
之间，`-w` 是全局开关，不适用该规则。cxx 现有实现与两家**一致**，无需改动。
而 `-Wall` 与 `-Wno-<组>` 的交互 **gcc 与 clang 本身分歧**（gcc 顺序无关，clang 后写者赢）；
cxx 当前跟 clang，作为已知选择记录。

### D4 `-funsigned-char` / `-fsigned-char` —— ✅ 已完成

普通 `char` 是否有符号是**实现定义**的（6.2.5），而由 ABI 定：x86-64 有符号，ARM 与 RISC-V
的 ABI 无符号。cxx 原本把这件事硬编码在每个目标的 `ty_char_` 里，所以开关只需覆盖那**一个
共享 Type 对象**——所有对 `char` 的使用都走它。`signed char` / `unsigned char` 是各自独立的
类型，不受影响。

实测（含 `-funsigned-char -fsigned-char` 的先后覆盖）：

```
                     char  signed  unsigned  (char)-1==255  c<0
默认（amd64）        -1    -1      255       0              1
-fsigned-char        -1    -1      255       0              1
-funsigned-char      255   -1      255       1              0        ← 与 clang 逐字相同
```

开关必须贯穿整个编译器而不只是一个表达式，因此另测了「数组传给函数、结构体成员、char 算术」
三种形态：默认 `1 -1 1`、`-funsigned-char` `0 255 0`，与 clang 相同。

### E3 的重要更正 —— 标记落在错误节点上（已修）

上一轮我报告 E3 完成时，`is_referenced` 被加在了 `primary()` 的**字符串字面量**分支上——
因为锚点 `return new_var_node(var, tok);` 在文件里**恰好只有一处**，而它是字符串字面量分支，
不是标识符分支（标识符分支写作 `node = new_var_node(sc->var, tok);`）。

后果：`is_referenced` 对真正的变量**从未置位**，每个局部变量都被报成未使用。上一轮我看到
「6 条警告」并解读为「`used()` 的局部被正确排除」是**读错了证据**——6 条里正含那条误报。

发现方式：D4 的测试恰好把数组当函数实参、又用了结构体成员，于是暴露出 `buf`/`s` 明明被用
却被报告。这正是「一个更简单的用例看起来是对的」的典型。

修正后实测：

| 用例 | cxx | clang |
|---|---|---|
| 每个局部都被使用（含实参、成员、下标、取地址） | **0** | 0 |
| 恰好一个未使用 | **1**（`dead`） | 1 |

`test/c2y.sh` 增两条断言（含**反例**：真正未使用的必须报）。注意两条断言最初都误带了 `-w`，
而 `-w` 抑制一切、后面的 `-Wunused-variable` 无法再打开（gcc 同样），导致其中一条是**空洞通过**。

### E 诊断补强（E1/E2/E3）

| ID | 项 | 状态 |
|---|---|---|
| E3 | 未使用局部变量 | ✅ **已完成**（见下） |
| E4 | 未使用 static 函数/对象（调用图 + 不发射） | ✅ **已完成**（两半，见下） |
| I1 | `inline` 的内联定义 / 外部定义区分（6.7.5p8） | ✅ **已完成**（见下） |
| A1 | 不定长全局数组按一个元素补全（6.9.2p2） | ✅ **已完成**（见下） |
| N1 | fold 期的窄化警告（6.3.1.3） | ✅ **已完成** |
| E2 | 隐式 switch 穿透 + `[[fallthrough]]` 位置 | ✅ **已完成**（两半，见下） |
| E1 | `[[deprecated]]` 用于**类型** | ✅ **已完成**（见下） |

**E1/E2 实测**（`doc/e12.sh`，构造等价情形跑 gcc 与 clang 读 `[-W...]` 标签）。
这张表更正过一次：`e12.sh` 原先按「每个编译器一个 std 参数」传参，多出来的 `-std=`/`-W`
参数被**静默丢弃**，于是 gcc 那一列其实是在没有 `-Wimplicit-fallthrough` 的情况下测出来的
——表里「gcc 不报」是探针的错，不是 gcc 的行为。修好传参后重测：

| 情形 | cxx | gcc | clang |
|---|---|---|---|
| `struct [[deprecated]] S` 全局使用 | **0** | 1 · `deprecated-declarations` | 1 · `deprecated-declarations` |
| 同上，局部使用 | **0** | 1 · 同上 | 1 · 同上 |
| `typedef int [[deprecated]] myint` | 1 | 1 · `attributes` | **0** |
| `enum [[deprecated]] E` | **0** | 0 | 1 · `deprecated-declarations` |
| switch 穿透，默认 | 0 | 0 | 0 |
| switch 穿透，`-Wall` | 0 | 0 | 0 |
| switch 穿透，`-Wextra` | 0 | **1** · `implicit-fallthrough=` | 0 |
| switch 穿透，`-Wimplicit-fallthrough` | **1** | 1 · 同上 | 1 · `implicit-fallthrough` |
| switch 穿透 + `[[fallthrough]]` | 0 ✓ | 0 ✓ | 0 ✓ |
| `break`，无穿透 | 0 ✓ | 0 ✓ | 0 ✓ |

结论：E1 是**漏报**——两种 struct 用法 cxx 都不报而 gcc/clang 都报（早先记的「typedef 多报 2 条」
是旧树的结果；现在 cxx 1 条与 gcc 一致，clang 反而不报），所以要先对齐使用点判定再加诊断。
E2 的开关位置：gcc 把它放在 `-Wextra` 里，clang 只认自己的 `-Wimplicit-fallthrough`，cxx 跟 clang。

**E1 实现（已完成）**：判据是「这个名字被当成类型用」，挂点在**解析类型说明符**的两处——
`record_decl()` / `enum_decl()` 里**复用已有 tag** 的那条路径（定义走的是另一条，所以定义本身不报），
以及 `declspecs()` 里解析 typedef 名的那条。诊断点与 gcc/clang **逐字一致**：打的是 **tag 名**、
位置打在 tag 上（`struct S v;` → `2:8: warning: ‘S’ is deprecated`），共用一个小助手
`check_deprecated_ty(ty, tok)`。

两种**不算用途**的情形按实测排除：类型自身的定义（`struct [[deprecated]] S { … };`）与裸重声明
（`struct S;`）——两者 gcc/clang 都是 0 条，所以那两条路径调用助手前先看后面的 token 是不是 `;`。

**E1 实测**（`doc/e12.sh`，`test/c2y.sh` 增一条按行号断言的用例）：

| 用法 | cxx | gcc | clang |
|---|---|---|---|
| 全局对象 `struct S v;` | 1 ✓ | 1 | 1 |
| 局部对象 / 指针 / 形参 / 返回类型 / 成员 | 1 ✓ | 1 | 1 |
| `sizeof(struct S)` | 1 ✓ | 1 | 1 |
| `enum E w;` | 1 ✓ | 1 | 1 |
| 定义自身、裸重声明 | 0 ✓ | 0 | 0 |
| `typedef int [[deprecated]] myint;` 之后再用 | 1 | 1 | **0** |
| 只用被弃用 enum 的枚举常量 | 0 | 0 | **1** |

最后两行是 gcc 和 clang 自己就不一致的地方，cxx 跟 gcc——与 `-pedantic` 的措辞来源一致。

**D1 更正（由 E2 的测量逼出）**：原先 `-Wall` 被实现成「等于所有组」，这**表达不了**
`-Wimplicit-fallthrough` 这类不在 `-Wall` 里的诊断。现在组表多一列语义——`WG_OFF_DEFAULT`
标记的组初始为关，`-Wall`/`-Wextra` **不会**打开它们，只有自己的 `-W<名字>` 才行。
`implicit-fallthrough` 已按此登记，实测 `-Wall`/`-Wextra`/`-Wno-implicit-fallthrough` 均被接受，
且原有默认开启的组在 `-Wall` 下仍然开启（行为不变）。

**E2 实现**：判据是「上一个语句能否落进这个 label」，也就是「上一个语句能否正常结束」。
解析器按顺序读一个 switch 体，所以**一个全局位**就够：每条语句在解析结束时写下自己的答案，
`case`/`default` 读它并清零——清零正是让「一串 label」不被算成连续穿透的原因（`case 1: case 2:`
是一个入口）。位的写法分散在各语句的解析处：

- 跳转（`return`/`break`/`continue`/`goto`，含 GNU 计算 goto）→ 不落穿
- `if`：无 `else` 时「条件为假」本身就是一条通路；有 `else` 时取两支的或
- `while (1)` / `for (;;)` / `do ... while (1)`：常量真条件不落出，**除非它自己有 `break`**。
  `break` 属于最内层的循环或 switch，所以每个循环体和 switch 体压一个栈帧，`break` 只填最内层；
  否则 `while (1) { switch (y) { case 5: break; } }` 会被判成能落出
- `switch`：没有 `default`（值可能匹配不上）、有 `break` 离开它、或本体最后一条语句落穿时才落穿；
  只有「有 `default` 且每个 label 都以跳转结束」的内层 switch 不落穿
- 表达式语句 → 落穿；但**调用声明为 noreturn 的函数**不落穿（`_Noreturn`/`[[noreturn]]`/
  `__attribute__((noreturn))` 都汇到 `Sym::funcspec`）
- 声明、`_Static_assert`、空块 → 落穿
- 普通标签（`l:`）→ 当作新入口清零。这是照 clang 的行为做的（`case 1: y=1; l: case 2:` 两边都不报），
  gcc 会报
- `[[fallthrough]];`（两种拼写）→ 清零，这正是该属性的用途

常量条件只认字面量（`while (1)`、`for (;;)`）：`fold_ast` 在解析之后才跑，所以 `while (1 + 0)`
在这里认不出来。这与 `-Wshift-count-*` 当初放在解析器里漏掉一半的情况同源，但那一次的结论
（挪到 `fold_node`）在这里用不上——穿透判定要的是**语句顺序**，那是解析器独有的信息。

**E2 实测**（`doc/fall.sh`，50 个形状各自一个文件、三个编译器同跑；`test/c2y.sh` 增两条断言）。
两个参考实现对其中二十来个形状的答案并不一致——gcc 的检查跑得晚、以「语句」为对象，clang 跑在
自己的 CFG 上——所以目标不是「对齐某一个」，而是把每个形状的答案和理由写下来：

| 形状 | cxx | gcc | clang | 说明 |
|---|---|---|---|---|
| `case 1: y=1; case 2:` | 1 | 1 | 1 | 警告的本体 |
| 以 `return` / `break` / `goto` 结尾 | 0 | 0 | 0 | 跳转不落穿 |
| `case 1: ; case 2:`、`case 1: {} case 2:` | 1 | 0 | 1 | 空语句/空块仍算落穿（= clang） |
| `case 1: y=1; case 2: ;` | 1 | 0 | 1 | 落入空 label：gcc 不报（= clang） |
| `if (c) return; else return;` | 0 | 0 | 0 | 两支都离开 |
| `if (c) ; else return 3;` | 1 | 1 | 1 | 空 then 分支是一条通路 |
| `while (1) {}`、`for (;;) {}` | 0 | 0 | 0 | 常量真循环不落出 |
| `while (1) { break; }` | 1 | 0 | 1 | break 会落出（= clang，gcc 漏报） |
| `while (1) { switch (y) { case 5: break; } }` | 0 | 0 | 0 | 那个 break 属于内层 switch |
| `switch (y) { default: return 3; }` | 0 | 0 | 0 | 内层 switch 每条路都离开 |
| `switch (y) { case 5: return 1; }` | 1 | 1 | 0 | 无 default，值可能匹配不上（= gcc） |
| `noret();`（声明为 noreturn） | 0 | 0 | 0 | 调用不返回 |
| `y=1; /* fall through */ case 2:` | 1 | 0 | 1 | 注释不是注解：gcc 默认级别读注释（= clang） |
| `y=1; l: case 2:` | 0 | 1 | 0 | 普通标签通向 label（= clang） |
| `goto out; y = 1; case 2:` | 1 | 1 | 0 | 不做不可达分析（= gcc） |
| `case 1: { y=1; case 2: } case 3:` | 2 | 1 | 2 | 两次穿透都报，gcc 只报前一处（= clang） |

合计 50 个形状：gcc 报 26 次、clang 32 次、cxx 34 次；**每个形状 cxx 都至少与一个参考实现一致**
（`doc/fall.sh` 每行都标了 `= gcc` 或 `= clang`），差异集中在 gcc 只认语句、clang 只认 CFG 的
那些角落。开关行为照 clang：默认关、`-Wall`/`-Wextra` 不打开、`-Wno-` 可关、`-Werror` 变错误、
未知名报错。

**E2 后半（已落地）**：`[[fallthrough]];` 后面不是 `case`/`default` 时的诊断。实测 gcc 报
warning「attribute 'fallthrough' not preceding a case label or default label」，clang 报
**error**「fallthrough annotation does not directly precede switch label」——严重程度不一致，取舍定的是
**跟 clang 的 error**（约束就是约束，措辞也逐字用 clang 的），落点在 `stmt()` 处理语句属性处
（`has_fallthrough` 那一段）：注解必须是空语句，之后紧跟的 token 不是 `case`/`default` 就报错。
四个形状实测都对：错位（后面跟语句）报错、位置正当通过、注解落在 switch 末尾报错、
**注解后面跟普通标签**报错（这一条 gcc 沉默、clang 报，cxx 跟 clang）。

**上一轮为什么退回来、这一轮为什么不用退**：上一轮 `make test` 变红，唯一失败来自
`test/error.sh:251-257` 那三条夹具；复盘发现根因不是夹具的前提，而是**检查点的位置**——上一版把
位置检查放在了「空语句 / 在 switch 内」这两条约束之前，于是那三条断言原有措辞的用例先撞上了新
诊断。这一版把检查放在**语句解析完成之后**（`*rest` 指向注解后面那个 token），于是
`fallthrough annotation is outside switch statement` 与 `only applies to non-empty statement`
两条旧约束仍然先发，`test/error.sh` **一字未改**就继续通过（`make test` exit 0 实测）。
`test/c2y.sh` 增一条断言：错位、switch 末尾、普通标签三种都报且**退出码非 0**（error 不是 warning），
位置正当的一条必须不报。

**E3 实现**：判据是「名字从未解析成 `ND_VAR`」，所以两半是——在标识符解析处置
`Sym::is_referenced`，在函数体解析完后遍历 `var->locals` 报告未引用者。`Sym` 原先不带声明
token（而 `warning()` 需要一个），因此加了 `Token *tok` 并在两处块作用域声明点记录。

刻意**不**报告的三类，都写进了注释：
- **形参**：那是 gcc 单独的 `-Wunused-parameter`，不在 `-Wall` 里。实现上让形参不带 token，
  遍历时按 `!v->tok` 跳过——两端都加了注释说明这个耦合
- **编译器自己的临时量**：它们与用户局部变量共用 `locals` 链，同样因无 token 而跳过
- `[[maybe_unused]]` / `__attribute__((unused))` / `static` / `extern`

**E3 实测**（9 个函数的最小文件，`doc/e3.sh`）：`-w` → 0 条、默认 → 6 条、
`-Wno-unused-variable` → 0 条、`-Wunused-variable`/`-Wall` → 6 条、`-Werror` → 变错误并停在第一条。
报出的名字正是该报的六个（`used()` 的局部被引用了所以不报，`maybe`/`attr`/形参都被正确排除）。

**已知偏差**：`int a; a = 1;`（只写不读）现在也按 `unused-variable` 报告；gcc/clang 用的是
`-Wunused-but-set-variable`（同样在 `-Wall` 内，所以默认行为一致，只是组名不够精确）。

### T1 指针与 0 比较的 IR 整洁化 —— ✅ 已完成

`cast()` 的「整数 → 指针」分支原先无条件发 `inttoptr`，于是每个 `p == 0`、`p != 0`、`p = 0`、
`p = NULL` 前面都多一条常量指令：

```
改前   %tmp6 = inttoptr i32 0 to ptr
       %tmp7 = icmp ne ptr %p, %tmp6
改后   %tmp6 = icmp ne ptr %p, null          ← 与 clang 相同
```

依据是 6.3.2.3：值为 0 的整数常量表达式是**空指针常量**，其转换结果就是空指针，而不是
「由整数构造的指针」。判定用 `val.type == RInt && val.val == 0`——只有常量取这种形式，所以
判定是精确的；`void *f(long n) { return (void *)n; }` 仍正确发 `inttoptr`（实测）。

`test/ir.sh` 增两条断言：空指针常量对照 `null`、非常量整数仍走 `inttoptr`（后者防止修过头）。

**未覆盖**：`void *f(void) { return 0; }` 的 `ret` 仍经过一个槽位（`store ptr null` + `load`），
clang 直接 `ret ptr null`。那是 cxx 对每个表达式都先落槽的既有结构，与本项无关。

---

## 2. 依赖与建议顺序

```
P1（独立，收益最高）
P2（独立，硬失败）
P3（独立，工作量最大，可分两步）
P4（独立，实现面小）
P5（独立，改动最小）
P6（独立，一行级）
P7（应尽早，把本轮的教训固化）
D1 ──┬── D3
     └── E1/E2/E3
D4（独立）
T1（独立，可选）
```

**建议顺序**：P7 → P1 → P2 → P4 → P5 → P6 → D1 → D3/E → P3 → D4 → T1。

理由：
- **P7 最先**：先有回归网，后面每一项才有保护；本轮 3 个缺陷全是穿过全绿套件进来的。
- **P1 收益最高**：一次解锁 `<math.h>` 的全部宏，同时清掉 N3303 / N3364 两个提案。
- **P2 是硬失败**（生成非法 IR），且与 P1 无关，紧随其后。
- **P4 / P5 / P6 都很小**，穿插即可。
- **D1 是 D3 与 E 组的前置**，且属行为破坏性变更，适合在语言缺口清完后单独排期。
- **P3 工作量最大**（一个新头 + 函数族），放在最后单独排。

---

## 2.5 探针账目的更正（P3 / P1b 收尾时一并核对）

四个覆盖率探针最后剩下的 FAIL 全部逐个复核过：**没有一项是 cxx 的行为问题**，七个条目是探针自己
写错了期望值或少了 include。逐项实测（`clang -std=c2y` / `gcc -std=c2y` / `cxx` 三家同跑）：

| 探针条目 | 期望 | 实测 | 结论 |
|---|---|---|---|
| c2ycov2 `_Noreturn and noreturn` | 接受 | 三家都**拒绝** | C23 里 `noreturn` 是 `<stdnoreturn.h>` 的宏、不是关键字；条目补上 include |
| c2ycov3 `math functions link` | 链接并运行 | clang 也要 `-lm` 才链接得上 | 条目加 `-lm`（宿主没把 libm 并进 libc） |
| c2ycov3 `attribute on an expression statement` | 接受 | clang **报错**（"cannot be applied to a statement"）、gcc 只警告并接受 | cxx 与 clang **逐字相同**；条目改成期望诊断 |
| c2ycov3 `alignas in a parameter declaration` | 接受 | 三家都拒绝（措辞各异） | 条目改成期望诊断 |
| c2ycov5 `_Generic over literal types` | 接受并运行 | 少了 `<uchar.h>` 时 cxx 与 clang **都**拒绝（`char8_t` 是那个头的 typedef） | 条目补上 include |
| c2ycov5 `a literal is not an lvalue` | 接受 | 三家都拒绝 | 条目名就是要求本身；改成 `rej` |
| c2ycov5 `6.7.2 block-scope function declaration …` | 接受 | 三家都拒绝 | 改成 `rej` |
| c2ycov `case range with the bounds reversed` | 拒绝 | gcc/clang 都只**警告**并继续编译 | 条目改成「必须有诊断」而不是「必须被拒」 |

修完之后：**c2ycov 109 passed / 2 failed**（剩下两项是 `include <complex.h>` 与 `include <tgmath.h>`，
即项目有意不做的 `_Complex` 非目标，探针自己的 `rej "_Complex is not implemented"` 是 PASS 的）、
**c2ycov2 34 / 0 / 3 library gap**（3 项是宿主没有的 C2y 库函数）、**c2ycov3 19 / 0**、**c2ycov5 14 / 0**。
教训与 `doc/e12.sh` 那次同类：**探针的期望值也是代码，要按参考实现实测后再写**，
否则「cxx 的缺口」和「探针的笔误」在报表上长得一模一样。

---

## 3. 风险与待确认

| 项 | 风险 / 待确认 |
|---|---|
| P1 | 常量族要按 `_Float16/32/64/128` 与 `long double`（x86_fp80）各自的格式产出正确位型；SNAN 的静默/信号语义要按 7.12 与 Annex F 核对。`__builtin_fpclassify` 的 5 个实参顺序须按 glibc 的实际展开确认 |
| P1 | `nan("payload")` 的 payload 解析：glibc 传的是字符串字面量，cxx 需在折叠期解析 n-char-sequence（725.2.6 / Annex A.5.1） |
| P3 | 是否需要 cxx 自己解析多字节编码状态机，还是只做 UTF-8/UTF-16/UTF-32 与窄/宽执行字符集之间的转换，需要明确；`stdc_mcerr` 的枚举名要与草案逐字一致 |
| D1 | 把 `-W*` 从「一律接受」改成「未知即报错」会破坏现有脚本。**先清点** `Makefile` 与 `test/*.sh` 用到的全部 `-W*` |
| P5 | 若选择「定义 `__STDC_IEC_60559_TYPES__`」，需确认 glibc 的 `bits/floatn-common.h` 在 `__GNUC__` 已定义的前提下不会走进新分支（`__GNUC_PREREQ` 组合条件） |
| P2 | `_Atomic` 聚合体的 `load atomic i32` 是「按整块大小当整数读」还是「逐成员读」的语义选择，要与 clang 的实际 IR 核对后定 |
| T1 | 指针目标选零常量的分支要确认不会影响 `nullptr_t`（`TY_NULLPTR` 已有独立分支）与函数指针 |

---

## 3.5 待办事项（本轮核对）

复核过一遍所有「未做 / 待定」的记录，按性质分三类。**前两类是可以直接开工的活**，第三类是有意不做。

### 3.5.1 有明确下一步的

**空。** 原先挂在这里的两条 N1 都已按评审意见落地（note 给出最接近的可表示值；
常量整数→浮点的精度丢失拆成默认开的 `-Wimplicit-const-int-float-conversion`），见 §1 N1。
连同 E2、P2b、E4 三项，上一轮列出的「有明确下一步」的活已经全部做完。

### 3.5.2 已记录、属于既有缺口的

| 项 | 内容 | 位置 |
|---|---|---|
| cleanup | 独立属性声明 `[[gnu::cleanup(h)]];`：gcc 报「attribute ignored」、clang 报「only applies to local variables」、cxx 沉默——两家自己都不一致，**有意不跟** | §1 G1 |
| cleanup | VLA 带初始化器时 cxx 的措辞是「incompatible types when initializing」，两家是「variable-sized object may not be initialized」；三家都拒绝，只差措辞 | §1 G1 |
| 既有 | `int x __attribute__((noreturn));`：两家都只**警告**（gcc「attribute ignored」/ clang「only applies to function types」），cxx 报 **error**（`‘noreturn’ can only appear on functions`）。严重程度分歧，早于本轮 | §1 G1 缺陷 5 |
| E4 | 未求值位置（`sizeof`、`_Generic` 控制操作数/未选中关联）里的名字 cxx 按默认开的 unused 组报出，clang 只在 `-Wall` 下报 `-Wunneeded-internal-declaration`：发射一致，诊断更彻底 | §1 E4 末 |
| E4 | 「地址被取过就不报」那条 clang 的警告豁免 cxx 没跟：只被 `sizeof &f` 提到的函数 clang 沉默、cxx 报 `unused function`（两侧都不发射） | §1 E4 末 |
| P2b | `atomic_fetch_*` 拒绝聚合体时的措辞比 clang 短（clang 后缀实际类型） | §1 P2b |
| P1 | `nan("payload")` 的 n-char-sequence 解析（头文件只传 `""`） | §1 P1 末 |
| P3 | 转码函数是头里的 `static inline`，不包含该头的程序链接不上（cxx 没有自带运行库） | §1 P3 |
| I1 | 6.7.5p3 的另一半（内联定义引用内部链接标识符）：gcc 报、clang 沉默，cxx 跟 clang | §1 I1 |
| D3 | `, ## __VA_ARGS__` 的 pedantic 措辞分歧（有意不接） | §1 D3 |
| asm | 聚合体操作数（多 piece 记录、内存类记录）在寄存器约束下被拒，两家都不拒 | §1 G4 |
| asm | `-pedantic` 下不报 `asm` 是 ISO C 之外的扩展（clang 报 `-Wlanguage-extension-token`、gcc 沉默） | §1 G4 |
| asm | 位域上的 `=r` 输出：gcc 自崩于后端、clang 正确，cxx 跟 clang | §1 G4 |
| R1 | `__PRETTY_FUNCTION__` 在 C 里的值：gcc 给函数名（与 `__func__` 同）、clang 给 `int f(int)` 签名；cxx 跟 gcc（跟 clang 需要一套 C 声明符打印器） | §1 R1 |

### 3.5.3 有意不做（长期）

`_Complex` / `_Imaginary` / 复数字面量、十进制浮点 `_Decimal32/64/128`，以及 `<complex.h>` `<tgmath.h>`
两个头——这正是 `c2ycov` 剩下的 2 项 FAIL。另有 `c2ycov2` 的 3 项 **library gap**：宿主没有的 C2y 库函数
（`free_sized`/`free_aligned_sized`、`memalignment`、`umaxabs`），不是编译器的事。

---

## 4. 交付方式与验收总纲

- 所有改动**只落在工作区，不做任何 `git commit`**，由项目方 review 后决定提交。
- 每个工作项完成后提供：改动文件清单 + 关键 diff 摘要 + 验收命令与实测输出。
- 每个工作项保持**可独立 review 的最小改动面**；跨后端项（如涉及 ABI）按后端拆分。
- 每项改动后跑：

```bash
make -j8 && make test
make test-arm64 && make test-rv64 && make test-rv32
bash doc/probes.sh ./cxx        # 四轮探针 + 关键字全量比对
```

- 涉及 IR 的项用 `clang -std=c2y -O0 -S -emit-llvm` 逐条对照（`doc/c2ycov4.sh` 是模板）。

---

## 5. 已完成的批次（存档）

旧计划的 A/B/C/D/E/F 六批均已落地，此表保留以免重提：

| 批次 | 内容 | 现状证据 |
|---|---|---|
| A1–A4 | 变参函数定义、结构体/联合体按值传参与返回、四个目标的 ABI lowering、解除测试回避 | `test/varargs.c`、`test/aggregate.c`、跨目标互链 |
| B1–B1c | GNU 兼容宏（定义 `__GNUC__`），解锁 `<stdio.h>`/`<stdlib.h>`/`<math.h>`/`<wchar.h>` | `test/conformance.sh` 头文件段；实测 `__GNUC__` defined、`__clang__` not defined |
| B2 | `#include_next` 自循环 | `test/conformance.sh` 的 `limits.h` 宏断言 |
| B3 | `<stddef.h>` 补 `nullptr_t` | `test/conformance.sh` 4 项 |
| B4/B4b | `__builtin_{add,sub,mul}_overflow`、`__typeof` 无括号形式 | `<stdckdint.h>` / `<stdbit.h>` 端到端 |
| B5 | `#embed prefix()` 多 token 重词法化 | 结论：三家都拒绝 `prefix(0x)`，诊断时机差异，非缺陷 |
| C1–C4 | `_Countof` / 具名循环（功能 + 测试）/ `_Lengthof` / `defer` | `test/countof.c`、`test/control.c`；`_Lengthof` 已确认不在 C2y |
| D2 | `__extension__` | 已在关键字表中，`test/conformance.sh` 有断言 |
| F1/F2 | `test/conformance.sh` 纳入 `make test`；`test/stdhdr.c`、`rv32_common.c` 的覆盖空洞 | `make test` 现含 4 个脚本 |

**本轮新增的三项修复**（同批未提交改动）：

| 项 | 文件 | 说明 |
|---|---|---|
| `&&` / `||` 折叠极性 | `src/opt_ast.c` | `1 && x` 曾折叠为 `!x`；现折叠为 `x != 0`（见下） |
| 短路 PHI 前驱 | `src/irgen.c` | 右操作数自身开块时生成非法 IR |
| `-l` / `-Wl,` 链接顺序 | `src/main.c` | `-lm` 排在目标文件之前，libm 完全不可用 |
| 空 `case` 范围 | `src/dumpir.c`、`src/parser.c` | 分支表为空时省略方括号 → 非法 IR；同时补上 6.6.2 的诊断（P7 期间发现） |
| C2y 回归网 | `test/c2y.sh`、`Makefile` | P7 |
| math 常量产出族 | `src/cxx.h`、`src/parser.c`、`src/support/fp128.{c,h}` | P1 步一 |
| math 比较族 | `src/cxx.h`、`src/dumpir.c`、`src/irgen.c`、`src/parser.c`、`src/type.c` | P1 步二（IR 增加 `one`/`uno` 两个 `fcmp` 谓词；`usual_arith_conv` 从 type.c 导出） |
| math 分类族 | `src/cxx.h`、`src/parser.c`、`src/irgen.c`、`src/support/fp128.{c,h}` | P1 步三（`fp_const` 由 ND_NUM 的浮点常量分支抽出，两条路径共用一套格式编码） |
| 副作用求值次数验证 | `doc/effects.sh`、`test/c2y.sh` | 39 项计数与 clang 逐一对照；`c2y.sh` 固化 28 项断言 |

关于折叠形式：`1 && x` 与 `0 || x` 折叠成 `x != 0`（而非 `!!x`）。两者语义相同，
但比较形式是 6.5.14/6.5.15 的字面表述，与 parser 对写出来的 `x != 0` 构建的树
（`ND_NE(LVTOR(x), IMCAST(ND_NUM 0):T)`）完全一致，因此折叠后与源码形式不可区分；
IR 从 4 条降到 2 条（`icmp/fcmp ne` + `zext`），与 `clang -O0` 逐字节相同。
指针操作数多一条可折叠的 `inttoptr i32 0 to ptr`（见 T1）。

---

## 6. 相关文档

| 文档 | 内容 |
|---|---|
| `doc/n3685-conformance.md` | N3685 实现对照清单：四个维度的实测覆盖、缺口、探针自证 |
| `doc/builtin-redesign.md` | 声明式内建框架的设计与实施记录（P1 的基础） |
| `doc/cfg.txt` | C2y 文法（不含扩展） |
| `doc/token.txt` | C2y 记号与关键字文法 |
| `doc/c2ycov*.sh`、`doc/probes.sh` | 覆盖探针与统一runner |
| `doc/minbug.sh` | 两个非法 IR 缺陷的最小复现 |
| `doc/driver.sh` | 驱动与诊断项的现状检查 |
| `doc/suite.sh` | 四个目标的全套件 |
| `doc/asm.sh` | GNU asm 语句的三家对照（判决 + 运行结果 + 一条参考实现互相矛盾的信息行） |
| `doc/realworld.sh` | 真实项目编译记分（`$RW` 下的 lua/zlib/libpng/sqlite/tinycc/git/cpython，各项目自己的 flag，报 N/M 与 top 诊断） |
| `doc/selfhost.sh`、`doc/selfbuild.sh`、`doc/bootstrap.sh` | 自举记分板（cc1 + LLVM 收不收）、两阶段自举（编译 → 链接 → cxx2 能做什么）、三代自举链（cxx2 → cxx3 → cxx4 不动点，`--suites` 加跑保真度对照） |
