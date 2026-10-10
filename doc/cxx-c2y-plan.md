# cxx 修订计划（C2y 收口）

> 依据：**N3685 草案本身**（`doc/n3685.txt`），外加两样自动化对照：`doc/c2ycov.sh`
> （一致性覆盖，取代早期手工清单）与 §1 的 P-工作项（提案清单）。早期的
> `doc/n3685-conformance.md` 是基于 commit `0924007` 的**手工快照**，其“未修复”项
> 经复测**全部已修**（`_Atomic` 聚合体、N3652 数组补全、`<math.h>` 宏、`-pthread`、
> `__STDC_IEC_60559_TYPES__`），内容已并入本文与 `c2ycov.sh`，故删除（历史见 git）。
> 本文取代旧的 A/B/C/D/E/F 批次计划 —— 那六批**已全部完成**，存档见 §5。
> 内建框架的设计见 `doc/builtin-redesign.md`（框架已按 P1 落地，其余仍为设计稿）。

---

## 0. 范围

**在计划内**：N3685 语言语义补全、标准库头可用性、内建族、诊断体系、扩展开关。

**明确不做（长期）**：`_Complex` / `_Imaginary` / 复数字面量 / `++ --` 与运算符用于复数；
十进制浮点 `_Decimal32/64/128`。共 9 个提案 + 2 个头（`<complex.h>` `<tgmath.h>`）。
相关位置应保留「有意不支持」注释，验收中排除。

**也不支持（已删除/已改变语义的旧形式，见 §R56、§R57）**：两种写法 cxx **不实现**，
而且 C23 已经不再需要它们——探针直接**过滤**，不计入失败：

| 写法 | 状态 |
|---|---|
| 调用一个声明为 `f()`（无原型）的函数 | **弃用于所有 C 版本**，C23 起 `()` 等同 `(void)`，传参在两家邨也是错误 |
| K&R（旧式）函数定义 `int f(a) int a; { }` | C23 **删除**：草案的 `function-definition` 产生式已经没有 `declaration-list` |

过滤判定不靠正则，而是让参考编译器说话：clang 用同一条 `-Wdeprecated-non-prototype`
同时命中这两种写法，而 `(void)` 原型不受影响（实测三样本：两个被过滤、一个干净）。

**原则**：不改既有的正确行为去迁就外部头文件。典型案例是 `_FloatN` 保持关键字身份
（H.5.1），而用 GNU 兼容宏让 glibc 走对分支。

**当前阶段：debug——只修已有功能的 bug，不扩功能**。
测试暴露出“未支持的功能”时，**先屏蔽并记为缺口**，不当场实现；
debug 阶段结束后再统一计划是否扩充。探针把它们算进 `gap` 桶（与“超出范围”分开），
于是“失败”只剩下**本该能用的东西真出了问题**。

**仍未闭合的两项（原手工清单的遗留，2025 复测）**：

| 项 | 状态 |
|---|---|
| N3366 `7.26 <stdmchar.h>` | cxx 未提供该头，系统也无此头 —— **本机无法对照**，故不列入缺口表（既非已支持也非已证缺口） |
| N3348 泛型关联里的 `[*]` | 未实现；`doc/c2ycov.sh` 覆盖此项，列入 §1 待办 |

**缺口表（已知未支持，已屏蔽）**：

| 缺口 | 见证 | 备注 |
|---|---|---|
| **SSE/MMX 内联函数族**（`__builtin_ia32_*`）及 `__SSE2__` | Fujitsu `C/0159`；`§R63` | 试过定义 `__SSE2__`，结果 cpython 从 381/385 掉到 **149/385**（头文件的 SIMD 分支全部散架），已撤回 |
| **`__sync_*` 未实现的形式**：`*_and_fetch`、`__sync_{bool,val}_compare_and_swap`、`nand` | Fujitsu `C/0044_0001`；`§R68` | 已有 `fetch_and_*`、`lock_test_and_set`、`lock_release`、`synchronize`；`_and_fetch` 要把操作数加回去（指针还要按元素缩放），CAS 两形式按值接旧值，nand 没有 `A_*` 撠码 |
| **十进制浮点** `_Decimal32/64/128` | §0 长期不做 | 已在上面“明确不做”之列 |
| 复数（`_Complex`/`_Imaginary`/虚数后缀） | §0 长期不做 | 探针已按 `noproto` 桶过滤 |

此前已落地的扩展（`__fp16` §R67、`__label__` §R70、`#pragma pack` §R65、`alias` 属性 §R69）**保留**；
本阶段不再新增。


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
| **最小复现** | `bash doc/minbug.sh` 的 B 组（原手工清单 §2.1，已并入本节） |
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

### T2 以 cxx 为宿主编出的 tcc 不能字节级自我复现 —— ✅ 已修（根因：`指针 - unsigned`，见 R39）

R37 量到：cxx 编的 tcc 通过 `test1`/`test2`/`test3`（三级自举）与边界检查运行，
但 `tccb`（`-b` 自编译的两个可执行文件必须逐字节相等）失败，而同一目标在 gcc 宿主树里通过。
归因：同一份 `tcc.c`、同一个运行库，gcc 编的 tcc 与 cxx 编的 tcc 输出相差 369528 字节；
`int main(){return 42;}` 这样的输入也差 32 字节（入口点偏移不同）。**R38 缩小到的范围（已确证）**：

| 环节 | 结果 |
|---|---|
| 预处理（`-E`） | **逐字节相同**（1045025 字节）——不是预处理差异 |
| 目标文件（`-c`，无链接） | **逐字节相同**（706370 字节）；小输入也相同 —— **代码生成一致** |
| 链接产物 | 差 32 字节，入口点 `0x401b50` vs `0x401b30`；节表/程序头数目相同，`_start` 等代码**逐字节相同**，只是整体平移 |
| 平移的来源 | `.eh_frame` 段**大小不同**（gcc 侧 `0x24`、cxx 侧 `0x0c`）——少了一个 FDE，后续节因此整体后移 `0x18`（`.text` 对齐后易位 `0x20`） |
| 两棵树的 `libtcc1.a` | 9 个成员**逐字节相同**；`lib/*.o` 大小也一致 —— **不是运行库差异** |
| 同一棵树内（cxx 编的 tcc 与它编出的 tcc） | 对同一输入产出的**目标文件相同**，链接产物不同 —— 差异在**链接阶段** |

结论：tcc 的**代码生成对自己怎么被编译不敏感**（目标文件一致），但它的**链接器**
在 cxx 编出来时会丢一个 `.eh_frame` 条目（或合并得不同），于是产物整体平移、`tccb` 的 `cmp` 不再相等。

**下一步**：用 `readelf --debug-dump=frames` 对比两个链接产物，看是哪个函数的 FDE 没了（应该在 `_start`/crt
一带），再回到 `tccelf.c` 的 `.eh_frame` 合并与重定位那几行，看 cxx 编译它时哪一个表达式行为不同。
实验脉绝对不要放 `/tmp`（这台 WSL 会清），用 `~/t2/`。

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

### R9 真实开源项目：第六轮 —— ✅ 三处语言/驱动缺陷与三处预处理器缺陷（六处修复）

| | |
|---|---|
| **缺陷（1）非可变参函数里的 `va_end`** | 7.16.1.1p1 只要求 `va_start` 出现在可变参函数里；`va_end`（以及 `va_arg`/`va_copy`）关掉的 `va_list` 完全可以是**别处传进来的**——`vprintf` 那个形状，cpython 的 `object_vacall()`、git 的包装函数、tinycc 的辅助函数都这么写，两家也都接受。cxx 对三者一律要求可变参，于是这些文件全被拒。修法：该检查只留给 `va_start` |
| **缺陷（2）内联汇编的方言备选** | cpython 的 `pycore_pystate.h` 写 `__asm__("{movq %%rsp, %0\|mov %0, rsp}" : "=r"(result))`——GCC 的**方言备选**语法，LLVM 的拼法是 `$(att$\|intel$)`。cxx 原样发出，LLVM 的汇编分析器在 `{` 后等 `}`，报 `Expected '}'`，clang 再报 `cannot compile inline asm`（cpython 全量 32 条里 20+12 都是它）。修法：**扩展** asm（写了冒号的）且在**有方言的目标**（x86）上才改写 `{`→`$(`、`\|`→`$\|`、`}`→`$)`；`%{`/`%\|`/`%}` 是这三个字符的字面写法，任何目标都认。逐例与 clang 对照过：基本 asm 不改写，aarch64 不改写，`:::` 这种无操作数的扩展 asm 照样改写 |
| **缺陷（3）丢弃限定符：该警告的报了 error** | 6.5.16.1p1 要求「左指针指向的类型带齐右指针指向类型的所有限定符」，所以 `void *p = const char *q;` 是约束违例——但两家**只警告并照常编译**（gcc `-Wdiscarded-qualifiers`、clang `-Wincompatible-pointer-types-discards-qualifiers`），cxx 直接拒编：git 的 `bloom.c`（`free(e)`）、tinycc 的 `tccrun.c`、五个 cpython 单元都倒在这上面。修法：新警告组 `WG_DISCARDED_QUALIFIERS`（gcc 的名字，默认开，`-w`/`-Wno-` 都能关），指向类型兼容或 `void *`↔对象指针时只警告。同一处还补上 GNU 的**函数指针↔`void *`**（两家沉默接受）：tinycc 写 `tcc_add_symbol(s, name, _tcc_backtrace)`，函数指示符给 `const void *` |
| **缺陷（4）替换列表没有和后面的 `(` 一起重扫** | 6.10.3.4p1 要求替换列表**连同调用之后的令牌**一起重扫——替换结果末尾是函数式宏名、而 `(` 来自源文件时，它才成其为调用。R8 的做法是把输入的实参表**拷进**替换列表，于是那对实参落在了外层宏「已展开」的窗口里：`ELFW(ST_INFO)(a, ELFW(ST_TYPE)(b))` 里的内层 `ELFW` 被外层 `ELFW` 的窗口挡住，不展开。修法：把名字从输出里**取回来**，在窗口**之外**用输入的实参表展开（`rescan_call()`）；对象式宏那一支同样调用它，于是 `#define WRAP ELFW(ST_TYPE)` 配 `WRAP(b)`（R8 记录的残留）也一并修好 |
| **缺陷（5）包含守卫的判定过宽** | 一个文件算「有守卫」的前提是**守卫一直管到文件末尾**。cxx 原来只要求「某个 `#endif` 之后就是 EOF」，于是任何以条件块结尾的头都会被登记成有守卫——tinycc 的 `tcc.h` 正是这样：`#endif /* _TCC_H */` 之后还有一段按 `USING_GLOBALS` 选 `TCC_SET_STATE` 的代码。第二次包含时整个文件被跳过，那段代码不再求值，`TCC_SET_STATE(fn)` 停在 `(tcc_enter_state(s1),fn)`，`tcc.c` 于是报 `use of undeclared identifier 's1'`。修法：只有**关闭守卫自身 `#ifndef`** 的那个 `#endif` 才算（比对 `cond_incl->if_tok` 与文件首指令的 `#`） |
| **缺陷（6）`__attribute`（一对下划线）** | 两家都接受的老拼写，tinycc 的 `lib/dsohandle.c` 写 `void *__dso_handle __attribute((visibility("hidden"))) = &__dso_handle;`。cxx 的关键字表里只有 `__attribute__`。修法：补上 |
| **验收** | `test/conformance.sh` **136 → 143 passed / 0 gap**，七条新断言：转发的 `va_list` 上的 `va_end`、asm 方言备选、丢弃限定符的转换、`-Wdiscarded-qualifiers` 组名、替换列表与后续令牌一起重扫（含对象式别名）、守卫 `#endif` 之后的代码、`__attribute`。全套复跑：c2y 101/0、arm64/rv64/rv32 各 51（rv32 +1 skipped）、`doc/probes.sh` 全部基线、`doc/bootstrap.sh` 仍是 cxx2 = cxx3 = cxx4 逐字节相同 |
| **记分（去重后的全量翻译单元）** | tinycc **10/21 → 17/21**；git **508/567 → 519/567**；cpython **321/385 → 353/385**；lua 35/35、libpng 18/18、zlib 15/15、sqlite 1/1 不变 |
| **剩余阻塞项（本轮末次全量探针）** | **tinycc 4**（全在 `lib/`）：`__atomic_compare_exchange`（generic 形式，与已实现的 `_n` 参数不同）1、`no member named 'gp_offset' in 'struct __va_list_tag'` 1（x86-64 的 va_list 布局与该文件手写的假设不一致）、`__builtin_frame_address` 2（缺这个内建，`bt-log.c` 与 `bcheck.c`）；**git 48**：`clar-decls.h`／`clar.suite` 缺失 30（`t/unit-tests` 的生成头没建，宿主编译因缺 libcurl 停在 `help.o`，不是 cxx 缺陷）、`incompatible types when passing argument` 7、`storage class specifier is not allowed in this context` 1（`kwset.c`）等；**cpython 32**：`element 0 of struct initializer doesn't match struct element type` 6、`void type only allowed for function results` 4、`unknown token in expression`、`incompatible types when initializing/assigning` 等 |

### R10 真实开源项目：第七轮 —— ✅ **tinycc 全部编过**（五处修复）

| | |
|---|---|
| **缺陷（1）`__builtin_frame_address` / `__builtin_return_address` 缺失** | 两者都是「取第 n 层的帧地址 / 返回地址」，一行表项即可：`llvm.frameaddress.p0` / `llvm.returnaddress.p0`，参数 i32、结果 `void *`。它们的层号是 LLVM 的 **immarg**：原来的做法会把一个变量当立即数发出去，LLVM 回一句 `immarg operand has non-immediate parameter` —— 一条关于 IR 而不是关于程序的报错。所以 `fold_builtin_call()` 里补上「必须是常量整数」的检查，措辞与 clang 一致（`argument to '__builtin_frame_address' must be a constant integer`）。tinycc 的 `lib/bt-log.c`、`lib/bcheck.c` 就是调用者 |
| **缺陷（2）GNU 的 `void *` 算术** | GNU C 给 `void *` 的加减一个**字节**步长（元素大小按 1 算），而 GEP 的元素类型不能是 `void`：LLVM 答 `void type only allowed for function results`。tinycc 的 `__bound_ptr_add()` 写 `return p + offset;`（p 是 `void *`），cpython 也有同样的形状（4 个单元）。修法：`gep_step_type()` 把 `void *` 换成 `char *` 再发 GEP，三处指针算术（`p + n`、`p++/--p`、`p += n`）都走它 |
| **缺陷（3）`va_list` 的字段没有名字** | x86-64 的 `__va_list_tag` 与 AArch64 的 `__va_list` 是编译器内建类型，但 psABI/ABI 给它们的字段起了名字（`gp_offset`/`fp_offset`/`overflow_arg_area`/`reg_save_area`，arm64 是 `__stack`/`__gr_top`/`__vr_top`/`__gr_offs`/`__vr_offs`），clang 的内建类型也带这些名字。cxx 造的记录成员一律无名，于是任何按名字走这个记录的代码都不成立——tinycc 的 `lib/va_list.c` 正是在这些字段上实现 `__va_arg`。修法：`member_name_token()` 造一个只有 interned id 的成员名（成员名只被比较，没有别处读它的文本） |
| **缺陷（4）generic `__atomic_compare_exchange`** | 与 `_n` 形式只差一处：**desired 也是取地址**（`*desired`），weak 标志与两个内存序的位置完全相同。补上 kind、表项与那句解引用。**过程中撞出一个 cxx 崩溃**：新建的 `ND_DEREF` 节点没有类型，紧接着的 `lvalue_convert()` 读 `ty->qual` → SIGSEGV（探针在 git 上也记到过一次 `signal 11`，未定位）。修法是紧跟一句 `add_type()`。验收：成功/失败两条路径（失败时把当前值写回 `expected`）与 gcc/clang 逐字相同 |
| **缺陷（5）诊断里的记录名用了 IR 的名字** | 记录的类型名有两套：`uid` 是 **IR** 打印的名字，`id` 是程序写的 tag。诊断一直用 `str(uid)`，而 `uid` 为 0 的正是编译器自己造的记录（ABI 的 va_list、聚合体 shape）——`str(0)` 根本不是字符串，而是 intern 表第 0 号槽里碰巧放着的那个：于是成员查找报出 `no member named 'gp_offset' in '__INT_FAST8_TYPE__'`。修法：`record_diag_name()` 先看 `is_anon`（给 gcc 的 `<anonymous>`），再看 tag，再看 uid |
| **验收** | `test/conformance.sh` **143 → 150 passed / 0 gap**，七条新断言：`void *` 算术（运行）、帧/返回地址（运行）、非常量层号被诊断、`va_list` 字段名与布局、generic `__atomic_compare_exchange`（含失败回写）、记录在诊断里的名字（tagged/union/anonymous）、内建记录在诊断里的名字。全套复跑：c2y 101/0、arm64/rv64/rv32 各 51（rv32 +1 skipped）、`doc/probes.sh` 全部基线、`doc/bootstrap.sh` 仍是 cxx2 = cxx3 = cxx4 逐字节相同 |
| **记分（去重后的全量翻译单元）** | **tinycc 17/21 → 21/21**（该构建编译的每一个翻译单元都过了）；cpython **353/385 → 358/385**；git 519/567（不变）；lua 35/35、libpng 18/18、zlib 15/15、sqlite 1/1 不变 |
| **剩余阻塞项（本轮末次全量探针）** | **git 48**：`clar-decls.h`／`clar.suite` 缺失 30（`t/unit-tests` 的生成头没建，宿主编译因缺 libcurl 停在 `help.o`，**不是 cxx 缺陷**）、`incompatible types when passing argument` 7（`daemon.c`、`connect.c`、`tr2_dst.c` 等）、`storage class specifier is not allowed in this context` 1（`kwset.c`）、`'noreturn' can only appear on functions` 1（`usage.c`，两家只警告、cxx 报 error——R8 §3.5.2 已记的严重程度分歧）、`incompatible types when assigning` 1（`source-packed.c`）；**cpython 27**：`element 0 of struct initializer doesn't match struct element type` 6、`implicit declaration` 2、其余为 `unknown token in expression`／`incompatible types when initializing/assigning` 等；**tinycc 0** |

### R11 真实开源项目：第八轮 —— ✅ 七处修复，git 的非环境阻塞项清零

| | |
|---|---|
| **缺陷（1）透明联合（`transparent_union`）没有实现** | glibc 在 `_GNU_SOURCE` 下把 `connect()`/`bind()`/`accept()`/`sendto()` 的地址参数声明成一个**透明联合**（`union { const struct sockaddr *__sockaddr__; … } __attribute__((__transparent_union__))`），调用处传的是**成员**而不是联合本身。cxx 既不认这个属性（`__has_attribute` 也是假），也就按普通联合比较，于是每个 socket 调用都成了 `incompatible types when passing argument`——git 的 7 处、以及这类代码的全部。修法：属性入表；`fncall()` 里参数类型是透明联合时，取**第一个可赋值的成员**作为实参类型（`is_assignable()` 从 `check_asop()` 里分出来，使这个判断可以不带诊断地问）；成员一个都不匹配时仍按联合报错。gcc 还要求成员「传递方式一致」并在声明处警告 `union cannot be made transparent`，cxx 直接按字面接受属性（记在文档里） |
| **缺陷（2）`-E` 输出会把两个记号粘成一个** | 6.10.3.3 要求预处理输出重新分词后仍是同一串记号，而 `-E` 只用 `is_leadingws` 决定要不要空格——宏替换后**实参的第一个记号**带的是它在源文件里的前导空白，跟它现在跟在谁后面无关。`#define __SOCKADDR_COMMON(prefix) sa_family_t prefix;` 按 `__SOCKADDR_COMMON (sa_family)` 调用，输出就是 `sa_family_tsa_family;`（这恰好挡住了本轮用来定位崩溃的自包含复现）。修法：`needs_space()` —— 两侧都是标识符/数字字符要空格，`.` 与数字相邻要空格，省略号第三个点要空格，两个标点会不会并成更长的标点则交给**词法器自己的 `read_punct()`**（把它从 `static` 提出来，避免第二份标点表）。四种情形与 clang 逐字对照一致 |
| **缺陷（3）cxx 崩溃：不完整参数类型的诊断解引用了空名字** | git 的 `log-tree.c` 一路走到 `func_param()` 的 `paramty->size < 0` 分支，`paramty->name` 是空（`void show_reflog_message(…, struct date_mode, …)` 里的参数没有名字），`paramty->name->len` 直接 SIGSEGV——探针里那条 `signal 11` 就是它。**顺带发现真正的问题**：6.7.6.3p4 只要求**定义**的参数类型完整，**声明**允许不完整（git 的 `reflog-walk.h` 就是这么写的），而 cxx 在参数解析处一律报错。修法：诊断先按无名参数拼名字；这条检查移到 `external_declaration()` 的函数定义分支，与 gcc 的位置一致 |
| **缺陷（4）参数上的 `register` 被拒** | 6.7.6.3p2 说参数唯一允许的存储类是 `register`，git 的 `kwset.c` 写 `register struct tree const *tree`。cxx 给 `declspecs()` 传了空的 `sclass`，于是任何存储类都在参数处报 `storage class specifier is not allowed in this context`。修法：传真的 `SClass`，只放行 `SC_REG`，其余按名字报错 |
| **缺陷（5）`noreturn` 的严重程度与位置** | 两处：`static __attribute__((noreturn)) report_fn usage_routine = …`（`report_fn` 是**函数指针** typedef，git 的 `usage.c`）被 cxx 报 error，两家都接受——它修饰的是指针指向的函数类型；`__attribute__((noreturn)) int x` 被 cxx 报 error，两家只**警告**（`-Wattributes` / `-Wignored-attributes`）。修法：函数指针上接受；其余降为 `WG_ATTRIBUTES` 警告，并且**真的清掉该标志**（留着会把对象标成 noreturn，穿过它的调用会被当成不返回） |
| **缺陷（6）枚举与它的整数类型不兼容** | 6.7.2.2p4 把枚举的整数类型交给实现选择，gcc/clang 的选择是「枚举值全非负 → 无符号类型」。cxx 的 `enum_set_underlying()` 只在值**超出** `int` 时才选无符号（注释却写着自己跟 clang 一致），于是 `(enum E)-1 < 0` 为真、`enum E` 与 `unsigned` 不兼容。git 的 `odb/source-packed.c` 正是把一个 `int (…, unsigned)` 函数赋给 `int (…, enum odb_write_object_flags)` 成员。修法：`is_unsigned = !negative`（与自身的 64 位分支一致）；`is_compatible()` 里再加一条「枚举与其等价整数类型兼容」，且必须放在**同类判断之前**（kind 不同会先返回 false） |
| **验收** | `test/conformance.sh` **150 → 156 passed / 0 gap**，六条新断言：透明联合参数（真跑一次 `connect()`）、`-E` 输出重新分词（并与自身再编译）、不完整参数类型（声明通过／定义报错）、`register` 参数、函数指针上的 `noreturn` 与非函数对象上的警告、枚举与其整数类型。全套复跑：c2y 101/0、arm64/rv64/rv32 各 51（rv32 +1 skipped）、`doc/probes.sh` 全部基线、`doc/bootstrap.sh` 仍是 cxx2 = cxx3 = cxx4 逐字节相同（枚举符号性这一改没有动摇自举） |
| **记分（去重后的全量翻译单元）** | git **519/567 → 522/567**，且**非环境阻塞项清零**：剩下的 45 全是缺生成文件/缺库（`clar-decls.h` 30、`curl/curl.h` 6 及其余同类）；cpython 358/385（本轮无净增，其 27 处仍是结构体初始化元素 6、`implicit declaration` 2 与若干 `unknown token in expression`／`incompatible types when initializing/assigning`）；tinycc 21/21、lua 35/35、libpng 18/18、zlib 15/15、sqlite 1/1 不变 |
| **剩余阻塞项** | **cpython 27**：`element 0 of struct initializer doesn't match struct element type` 6、`unknown token in expression`、`incompatible types when initializing/assigning`、`implicit declaration` 2 等；**git 45**：全部为环境（缺 `clar-decls.h`/`clar.suite` 生成头 30、缺 libcurl 6、其余同类）；**tinycc 0** |

### R12 真实开源项目：第九轮 —— cpython 的常量初始化器（一处修复，10 个单元）

| | |
|---|---|
| **缺陷：联合体的常量初始化器与它所在的具名类型不一致** | cxx 输出的是 LLVM IR 文本，类型按 `uid` 命名（`%struct._object = type { %union.anon.0, ptr }`，而 `%union.anon.0 = type { i64 }` 取的是**规范成员**——对齐最大的那个，与 clang 一致）。但初始化器里，若被初始化的成员**不是**规范成员，cxx 按 clang 的「类型双关」写法把它写成那个成员：`{ %struct.anon }` ✗——而包含它的类型仍然印成具名的 `%struct._object` ✗，于是同一个初始化器的**类型与值对不上**，LLVM 报 `element 0 of struct initializer doesn't match struct element type`。cpython 的 `struct _object` 正是这个形状：第一个成员是一个联合体，一半是 `int64_t ob_refcnt_full`（规范成员），另一半是一个由 `uint32_t ob_refcnt` + `uint16_t ob_overflow` + `uint16_t ob_flags` 组成的匿名结构体，而 `_PyObject_HEAD_INIT` 初始化的是后者。**10 个单元**倒在这上面 |
| **修法** | 照 clang 的办法：**需要双关时把类型整个写开**。新增 `init_needs_inline()`（一个初始化器里是否有联合体被非规范成员初始化，递归看结构体成员、数组元素）与 `print_init_ty()`（与 `dump_init()` 完全相同的遍历顺序，联合体按被选中的成员印成 `{ <成员类型> [, 填充] }`，结构体印成字面量形式）；`dump_init()` 只改一行：类型由 `print_type()` 改为 `print_init_ty()`。**不需要写开的类型一律走原路**，所以其余 IR 逐字节不变（自举不动点仍成立，见验收）。数组同理：任一元素需要写开时，整个数组按 clang 的 packed struct 形式 `<{ T0, T1, ... }>` 写出（数组元素之间没有填充，二者布局相同），每个元素用它自己的类型 |
| **缺陷（2）：属性参数表被行首令牌截断** | `attr_entry()` 扫描 `name(...)` 的参数表时，把**位于行首**的令牌当成属性结束（`if (tok->kind == TK_EOF || tok->is_sol) error(start, "expected ')'")`）——一条 C 里没有的规则。curl 的 `typecheck-gcc.h` 正是把参数字符串写在下一行：`static void __attribute__((__warning__(\n"curl_easy_setopt expects a long argument for this option"))) id(void) { ... }`，于是**每一个 `curl_easy_setopt()` 调用**都报 `expected ')'`（git 的 7 个单元：`http.c`、`http-walker.c`、`http-push.c`、`remote-curl.c`、`imap-send.c`、`http-fetch.c`、`help.c`）。括号深度本身已经会在配对的 `)` 处收尾，缺一个 `)` 仍然在 EOF 处报出来，所以三处 `is_sol` 判断都可以去掉（GNU 与 C23 两种拼写各一处，外加 `attr_list_*` 的收尾判断） |
| **与 clang 的对照** | 同一个源文件：clang 出 `@x = dso_local global { { %struct.anon }, ptr } { { %struct.anon } { %struct.anon { i32 -1073741824, i16 0, i16 0 } }, ptr null }`，cxx 出 `@x = dso_local global { { %struct.anon }, ptr } { { %struct.anon } { %struct.anon { i32 3221225472, i16 0, i16 0 } }, ptr null }`（`3221225472` = `0xC0000000`，clang 把它印成有符号的 `-1073741824`，同一个 32 位模式），LLVM 两者都收 ✓。修复前 cxx 出的是 `%struct.obj { { %struct.anon } { %struct.anon { ... } }, ptr null }` ✗ |
| **验收** | `test/conformance.sh` **156 → 158 passed / 0 gap**：两条新断言分别覆盖「具名结构体里的双关联合体」（三种初始化写法 + 运行时取值校验）与「数组里逐元素双关」（含 `sizeof` 布局校验）。全套复跑：c2y 101/0、arm64/rv64/rv32 各 51（rv32 +1 skipped）、`doc/probes.sh` 全部基线、`doc/bootstrap.sh` 仍是 **cxx2 = cxx3 = cxx4 逐字节相同**——这一条特别说明「不需要写开的类型 IR 不变」成立 |
| **记分（`doc/realworld.sh` 全量探针）** | **git 522/567 → 567/567 —— 全部翻译单元编过**（其中 522→560 来自上游装好 libcurl 后宿主构建完成、生成了 `clar-decls.h`/`clar.suite` 等头文件，560→567 是本轮的属性修复）；**cpython 358/385 → 368/385**（本次扫描口径 47 → 37 个失败单元，其中 10 个是初始化器那一类）；tinycc 21/21、lua 35/35、libpng 18/18、zlib 15/15、sqlite 1/1 不变 |
| **剩余阻塞项（cpython 17）** | **两处 cxx 崩溃**（下轮首要目标，已定位到指纹）：`Modules/socketmodule.c` 在 `new_blk()` 的 `assert(blk_used < curf->num_blk)`（irgen.c:70）上 abort——解析器给函数算的块数被 irgen 超了，调用栈是一长串嵌套 `gen_if`；`Python/crossinterp.c` 段错误——`gen_expr` 递归 **2455 层**，链是 `ND_COMMA`，位置指向 `_PyPickle_LoadFromXIData()` 里只有两个指定初始化项的 `struct _unpickle_context ctx = {...}`，某个地方把逗号链做成了远长于源文的形状。**其余为**：缺系统头（`krml/internal/types.h`、`windows.h`、`tcl.h`、`emscripten.h`、`winsock2.h`、`uuid.h`、`lzma.h`、`SystemConfiguration.h`、`optimizer.h`、`pycore_uops.h` 等）；`expected type` 2（`Objects/typeobject.c`、`Python/pythonrun.c`）；`argument is not of expected type '%struct.anon.N = …'` 2（`Python/codegen.c`、`compile.c`）；`expected ';' before '__VERSION__'` 1（`Python/getcompiler.c`）；`implicit declaration of ‘__builtin_shufflevector’` 1（`Python/pystrhex.c`）；`premature end of input` 1（`Python/jit_unwind.c`） |

### R13 真实开源项目：第十轮 —— ✅ 两处崩溃与十三处修复（cpython 的变长数组、调用 ABI 与模式宏）

| | |
|---|---|
| **缺陷（1）变长数组的边界被求值两次** | 解析器把边界表达式存进类型里的计数器（`vla_cnt = <边界>`）**并**把它当作 alloca 的长度表达式再用一次——同一个 AST 节点被 irgen 生成两遍。后果有两层：**语义上** `char a[n++]` 让 `n` 加了两次（两家只加一次：`n=4 sizeof(a)=3`，cxx 出 `n=5 sizeof(a)=4`；`int c[3][k++ + 2]` 出 `k=2 sizeof=36` 而两家是 `k=1 sizeof=24`）；**结构上**，边界里只要有需要开块的东西（`?:`、`&&`、`||`…），irgen 要的块就比解析期 `cnt_blk()` 预留的多，`new_blk()` 的 `assert(blk_used < curf->num_blk)` 直接 abort——**cpython 的 `Modules/socketmodule.c` 就是它**（`_socket_inet_pton_impl` 的 `char packed[Py_MAX(sizeof(struct in_addr), sizeof(struct in6_addr))]`，`Py_MAX` 展开成含 `?:` 的语句表达式）。修法：alloca 的长度改成**读计数器**（沿 `var->ty` 的变长链取每层的 `vla_cnt` 相乘），边界只在声明处生成一次 |
| **缺陷（2）变长 typedef 的计数器没有赋值** | `typedef int T[n];` 只把 `AS(cnt, n)` 压进待发队列，而队列是在**下一条声明**处才发出去的；`sizeof(T)` 走的是「类型名」分支，它先把队列清掉再读 `cnt`，读到的就是未初始化的值（实测 `sizeof(U)` 出 0，两家是 12；`n` 在 typedef 之后再改，cxx 还跟着变，两家固定在 typedef 处取值）。修法：块作用域的变长 typedef 自己把边界语句发出去——gcc 与 clang 都是在 typedef 处求值并捕获大小 |
| **缺陷（3）`typeof(int[++n]) v;` 的边界被丢掉** | `init_decl_list()` 在每个声明符开头 `scope->vla_num = 0`，把**说明符部分**（`typeof`）登记的边界一起丢了；旧代码恰好因为 alloca 重新生成了一遍 `vla_expr[0]` 才「看起来能用」（`n` 只加一次是因为唯一那次求值就在 alloca 里）。改成读计数器后这个巧合消失，`test/c2y.sh` 的 `typeof(int[++n]) v` 立刻段错误。修法：删掉那次清零，声明处的边界语句覆盖「说明符登记的 + 声明符新增的」全部 |
| **缺陷（4）初始化器为每个零元素建一个逗号节点（cpython 段错误）** | `create_lvar_init()` 对数组元素、结构体成员**逐个**建 `ND_COMMA` 节点，无论是否真的写了值；irgen 沿逗号链左端递归，一个节点一层栈帧。`struct _unpickle_context ctx = { .tstate = …, .main = { .filename = … } }` 里的 `char _filename[PATH_MAX+1]`（4097 字节，全零、已由 `ND_MEMZERO` 覆盖）就是 4097 层——`Python/crossinterp.c` 的 `gen_expr` 2455 层递归段错误正是它。修法：空的（`ND_NOP`）元素不进链（连同递归返回的「全空」子树），链长只与**真正写了的元素数**成正比 |
| **缺陷（5）变长调用的调用类型表写了未降级的参数类型** | 变长调用的被调类型必须显式写出来（后端靠它决定实参怎么传），cxx 用的是**源类型**，而被调函数的 `declare` 行是用 **ABI 降级后**的类型印的：cpython 的 `int _PyCompile_Error(struct _PyCompiler *, _Py_SourceLocation, const char *, ...)` 出成 `declare i32 @_PyCompile_Error(ptr, i64, i64, ptr, ...)` 配 `call i32 (ptr, %struct.anon.250, ptr, ...)`，LLVM 报 `argument is not of expected type '%struct.anon.250 = …'`（`Python/codegen.c`、`Python/compile.c`）。修法：调用类型表走 `dump_fn` 同一套降级（每个形参按 `abi_param_count()`/`print_param_type()` 展开）。**但属性不能进类型表**：`signext`/`byval`/`sret` 在调用类型里都是 `argument attributes invalid in function type`（clang 也只写裸类型，属性留在操作数上）——这一点是第一版实现漏掉的，rv64 套件立刻红了 4 个用例（rv64 上连 `int` 形参都带 `signext`），改法是给 `print_param_type()` 加一个「裸类型」开关，`dump_fn` 传 false、调用类型表传 true |
| **缺陷（6）块作用域的函数声明建立了局部符号** | 名字已经可见时（`long PyImport_GetMagicNumber(void);` 写在 `run_pyc_file()` 里，原型来自 `<import.h>`），块作用域声明走的是 `new_lvar()` 分支：于是入口块里多了一个**函数类型**的槽——函数类型有大小和对齐却没有指针目标，IR 出 `alloca (null), align 4`，clang 报 `expected type`（`Python/pythonrun.c`）；顺带 `-Wunused-variable` 还会把函数报成「未使用的变量」（两家都不报）。修法：函数声明**接管外层那个符号**（与文件作用域路径同一做法），并且不再用块作用域声明的存储类去覆盖它（`static long s(void);` 之后再来一条块作用域声明，函数必须仍然是 internal） |
| **缺陷（7）`__VERSION__` 未定义** | gcc 与 clang 都把它定义成字符串字面量，而报告「谁编译的」的程序会读它：cpython 的 `Python/getcompiler.c` 在 `#if defined(__GNUC__)`（cxx 刻意定义了这个宏，好让 glibc 的 `<bits/floatn-common.h>` 不写自己的 `_FloatN` typedef）下写 `"[GCC " __VERSION__ "]"`，宏不存在时字符串合不拢，报 `expected ';' before '__VERSION__'`。修法：五个目标的预定义块各加一行（值取 `"cxx (C2y)"`，不冒充任何一家的版本号） |
| **缺陷（8）`__STRICT_ANSI__` 未定义（`-std=cNN`）** | gcc/clang 给 ISO 模式定义它、给 `-std=gnuNN` 不定义；头文件与项目就靠它判断「GNU 扩展能不能用」。cpython 的 `Py_ARRAY_LENGTH()` 只在它**未定义**时把 `Py_BUILD_ASSERT_EXPR()`（一个逗号表达式）加进数组边界，而逗号表达式不是整数常量表达式，于是常量边界 `slotdefs_dups[…][1 + 10]` 被 cxx 当成变长数组（`Objects/typeobject.c`）。修法：`-std=` 从「忽略」列表里拿出来，非 `gnu` 前缀就 `-D__STRICT_ANSI__=1`，与两家一致 |
| **缺陷（9）三个同族缺陷：常量折叠、文件作用域的变长修改类型、`sizeof(变长对象)`** | ①`((void)sizeof(int), 4)` 这个形状（cpython 的 `Py_ARRAY_LENGTH` 在 GNU 模式下就是它）clang 读作常量 4：`(void)` 把值扔掉，底下的常量没有别的东西可丢——cxx 的逗号折叠只认「左操作数是整型/浮点常量」，现在也认「左操作数是到 void 的显式/隐式转换、其下是整型常量」。② 6.7.6.2p2 不允许文件作用域出现变长修改类型，cxx 却把它当常量，对象**类型为空**地发出去（`@a = dso_local global (null) zeroinitializer`），只有后端报错（`expected type`）；现在按 gcc 的措辞报 `variably modified ‘a’ at file scope`。③ `sizeof`/`_Countof` 一个变长**对象**时，待求值的边界链本来就该是空的，代码却把空指针当左操作数塞进 `ND_COMMA`——一个左操作数为空的节点是编译器别处不该见到的东西，上面①的折叠一走进去就 SIGSEGV（`int f(int n){ int a[n]; return sizeof(a); }`，这条是①的补丁自己撞出来的，随即按「没有链就直接返回长度」修掉，并给折叠补了空操作数判断） |
| **缺陷（10）`__inline` / `__inline__` 是宏而不是关键字** | 两家把它们做成关键字（`#ifdef __inline__` 为假），cxx 写成了预定义宏 `#define __inline inline`——而宏是可以**反向再展开**的：expat 的 `internal.h` 写 `#define inline __inline`，于是 `__inline` 展开成 `inline` 再被展开回 `__inline`，而它现在是一个**普通标识符**，byteswap.h 的 `static __inline __uint16_t` 就没了函数说明符（cpython 的 `Modules/expat/xmltok.c`、`xmlrole.c`）。修法：两个拼写进关键字表（`TK_INLINE`），并把五个目标预定义块里那两行宏删掉 |
| **验收** | `test/conformance.sh` **160 → 171 passed / 0 gap**（十一条新断言）：变长边界只求值一次（三种形状 + 运行时取值）、变长 typedef 在声明处捕获、`sizeof`/`_Countof` 变长对象、部分初始化的超大记录（4097 字节数组，运行时校验零填充）、变长调用前的记录形参、`[[noreturn]]` 在对象上是 error／GNU 拼写是警告／函数指针接受、`-std=c11` 是严格模式而 `-std=gnu11` 不是、文件作用域的变长修改类型被拒、`__inline` 是关键字。全套复跑：c2y 101/0、arm64 51、rv64 51、rv32 51(+1 skipped)、`doc/probes.sh` 全部基线（c2ycov 109/2、selfhost 21/0/0、asm 68/0 …）、`doc/bootstrap.sh` 仍是 **cxx2 = cxx3 = cxx4 逐字节相同**（说明调用类型表、初始化器链与关键字表的改动没有动摇不动点）。`clang-format-21 --dry-run --Werror` 干净。另外 **`make test` 由红转绿**：本轮开始时 `[[noreturn]] int v;` 是**失败**的——R11 把这条诊断从 error 降成了警告，但 `test/error.sh` 仍然要求它报错；本轮把**标准拼写**恢复成 error（GNU 拼写保持警告，函数指针仍接受），与 clang 一致 |
| **记分（`doc/realworld.sh` 全量探针）** | **git 567/567 保持全过**；**cpython 368/385 → 377/385**（本轮修好的 9 个单元：`Modules/socketmodule.c`、`Python/crossinterp.c`（这两个是崩溃）、`Python/codegen.c`、`Python/compile.c`、`Python/pythonrun.c`、`Python/getcompiler.c`、`Objects/typeobject.c`、`Modules/expat/xmltok.c`、`Modules/expat/xmlrole.c`）；lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21 不变 |
| **剩余阻塞项** | **cpython 8**（按性质分两类）。环境类 4 个：`Python/pystrhex.c`（`implicit declaration of function ‘__builtin_shufflevector’`）、`Modules/Hacl_Hash_Blake2s_Simd128.c`、`Modules/Hacl_Hash_Blake2b_Simd256.c`、`Modules/_testcapimodule.c`（`static assertion failed: __extension__ __alignof__(buf) >= 64`）——这四个都要 `vector_size` 向量类型，而它们之所以被启用，是因为这棵树的 `pyconfig.h` 是安装时用宿主 clang 配置出来的（它声明「本编译器有 `__builtin_shufflevector`、有 64 字节对齐的 SIMD」），cxx 本身不支持向量类型。真正的缺陷 4 个：`Modules/posixmodule.c`（`implicit declaration of function ‘__builtin_memset’`）、`Python/jit_unwind.c`（`premature end of input`）、`Modules/_testsinglephase.c`（`array initializer must be an initializer list`），以及（R14 复查后更正）`Modules/_ctypes/_ctypes_test.c` —— 它在 `__GNUC__` 下包含 `<complex.h>`，而 glibc 的 `bits/cmathcalls.h` 用 `_Mdouble_complex_`（`double _Complex`）声明函数，属于本计划 **有意不做** 的 `_Complex` 缺口，不是新缺陷 |

### R79 asm 匹配约束对间接输出 —— ✅ 缺口表最后一项落地，FFmpeg 诊断类别归零

`libavcodec/x86/rnd_template.c` 里的 `"+g"(h)`：GCC 的 `+` 操作数是**一个**，LLVM 的约束串需要**两个**。

- 约束**只名内存**时，输入半边命名同一块内存、传**同一地址**，clang 写作 `"=*m,*m"`；
- 约束**可能落在寄存器**时，clang 写**匹配数字**、传对象的**值**：`"+g"(h)` → `"=*imr,0"`（操作数 `i32`），`"+r"(x)` → `"=r,0"`。

cxx 两种情形都重复转换后的字母，于是 `"+g"(h)` 的输入被标成 indirect 并传了地址 —— LLVM 拒绝
落在非 indirect 约束上的 `elementtype`。**修法**：新增 `asm_cons_mem_only()`（字母全落在 `moV<>` 内）
判定走哪一条；数字那条在 IR 里改传 `asm_read()` 的**值**。修后约束串与 clang **逐字一致**，
`hpeldsp_init.c` 编译通过，FFmpeg 探针的**诊断类别归零**（只剩链接期的 `ld returned 1`）。
验收：`test/conformance.sh` 264/0（新增 `+g`/`+m`/`+r`/显式 `"0"` 四种形式，与两家输出一致）。

**缺口表更新**：§0 的“asm 匹配约束对间接输出”一行**移除**（已实现）。

### R163 `param_sym_of` 改按指针键的哈希表 —— ✅ **release −2.38%**（同类缺陷第三次）

`param_sym_of` 线性扫描一个 `(Type*, Sym*)` 数组，而该数组**只增不减、整个 TU 从不重置**
（`num_param_syms` 无任何清零点）→ 文件级二次复杂度，占 4.25%（20.3M 条指令）。
同类前两次：`find_ident` 的 `sc->vars` 扫描、`dumpir` 的 `emitted`/`defined_names`。

修法：开地址表，键为 `(uintptr_t)ty >> 3`（指针 8 字节对齐）；**用 `used` 标志判空，不用零值判断**
（移位键为 0 是合法键 —— 关键词表那次的坑）；256 槽起、50% 负载翻倍。数组、计数器、`ParamSym` 类型与扫描全删。

**先立裁判**：保留原扫描，每次查找跑两遍逐项对拍、不一致 `fatal()` → 四道闸门零分歧后才删扫描；
删除后闸门仍全绿（含 bootstrap 逐字节相同 + 21/21）。

| 项 | 结果 |
|---|---|
| release A/B | 264,311,120 → **258,024,199（−2.38%）**；**相对本轮起点 −11.26%** |
| 调试剖面 | 477,337,983 → **456,825,089**；`param_sym_of`（4.25%）**离开榜单** |
| 闸门 | **325 / 0**、bootstrap **逐字节相同 + 21/21**、tcctests **106 / 0** |

**流程教训（第三次同类，做法要改）**："精确匹配 + 残留即中止"的删除方式**连续两次中止**
（第一次以为是无名 typedef，实际是带标签的 `typedef struct ParamSym ParamSym;`；第二次残留检查又把该 typedef
当残留）——两次都未写入（安全但白费）。第三次改**宽容式删除**通过：**能删就删，其余交给编译器**；
**残留检查应当是报告，不是中止。**

### R162 记号/节点分配内联（"arena"）—— ✅ **release −3.12%**，`emalloc` 跌出榜首

`emalloc` 的 6.15% 几乎全是调用开销（每次编译 250~300 万次调用、每次约 11 条 + call/ret）。
让两个高频构造器自己做 bump：`arena_alloc_fixed()`（`always_inline`，一个判断 + 三条指令）+
`ALLOC(type)` 宏（`sizeof` 是编译期常量 → 对齐也在编译期，运行时无取整）。池状态由 util.c 导出
（`arena_pool`/`arena_free`），`emalloc` 用同一份。共 12 处：parser 1、lexer 1、preprocess 5、opt_ast 5；
其余分配仍走 `emalloc()`。返回内存与 `emalloc` 一样**清零**（chunk 来自 calloc、每字节只发放一次），
`new_node` 依赖这一点，由 bootstrap 逐字节相同保证。仍有 1 处写法不同未转，保持原样。

**为什么这次内联对、上次错（两次都实测）**：上次把 `emalloc` 内联进**全部 60 处**是 **+0.9%**；
这次只内联进 12 处的高频构造器、且只内联"一判断三指令"的极简快路径 → **−3.12%**。
**内联的收益 = 省下的调用 / 复制的代码，比值取决于站点数。**

| 项 | 结果 |
|---|---|
| release A/B | 272,817,234 → **264,311,120（−3.12%）**；**相对本轮起点 −9.10%** |
| 调试剖面 | 482,813,282 → 476,279,202；`emalloc` 6.15%（第一）→ **跌出前八** |
| 闸门 | **325 / 0**、bootstrap **逐字节相同 + 21/21**、tcctests **106 / 0** |
| 终止性 | 300 文件 × 10 秒 → hangs: 0 |

**流程教训（第二次同类）**：按"期望站点数"写的断言在 `preprocess.c`（实际 5 处而非 3 处）中止了脚本，
留下**半应用的树**。批量改动要么可重入、要么先数清楚；断言只该报告差异，不该中止在中途。

### R161 当前热点（重新测量）—— 头号目标变成"打印 IR"

调试构建重新 profile（符号完整），总量 548,237,295 → **482,813,282（−11.9%）**。self cost 前几名：

| 占比 | 函数 | 说明 |
|---|---|---|
| 6.15% | `util.c:emalloc` | 仍第一（每次编译 250~300 万次调用） |
| 6.00% | `lexer.c:tokenize` | |
| **5.01% + 4.00% + 2.86% + 2.36% + 2.06% + 1.98% + 1.54%** | **printf 家族** | **IR 打印，合计约 19.8%** |
| 4.20% | `parser.c:param_sym_of` | |
| 3.51% / 3.45% / 2.56% | `new_token` / `read_ident` / 内联 ASCII 谓词 | |
| 2.53% | `util.c:fnv_hash_32` | intern 哈希 |
| 2.67% | `__memcpy_avx`（memmove） | 新加整段拷贝（收益的一部分） |

**离开榜单的**：`canonicalize_and_splice` 7.45%、`already_emitted` 8.16%、`convert_keywords` 6.60%、
`decode_utf8` 3.28%、`is_ident2_ascii` 2.58%、`build_line_offsets` 4.06%、`read_punct` 1.69%。

**结论**：`dumpir.c` 自身代码已不在前 20，但**它打印出去的代价**（libc printf 家族）合计约 19.8%，
成为头号目标；词法簇约 19.4%、预处理簇约 4.3%。下一步候选：① IR 打印改自建缓冲写出（量级最大）；
② `emalloc` 的记号/节点 arena；③ `fnv_hash_32`（2.53%）与 `param_sym_of`（4.20%）。

日志：`doc/hotspots.txt`。

### R160 归一化趟改整段 `memmove` —— ✅ **release −4.11%**（本会话单项最大；累计 −6.17%）

用户问：逐字节赋值 vs "找到下一个需处理位置 + 重叠块 `memmove`"，哪个更优？**先量段长再回答**：

| 统计（tccgen 的 124 个输入文件） | 数值 |
|---|---|
| 读入字节 / 精确守卫后仍需重写 | 1,219,680 / **767,160（63%）** |
| 段长 | 均值 **11,363 B**、中位 2,521 B、最小 119 B、最大 268,425 B |

段是长的 → 向量化 `memmove`（约 1/32 条指令/字节）替换掉几千次逐字节迭代 → **决定性更优**。

实现：双指针 `r`/`w`；第一次"需要消失"的字节之后 `w` 落后 `r` → **必须 `memmove` 而非 `memcpy`**
（区域重叠，`memcpy` 契约禁止；glibc 大块实现可能反向拷贝）；段 >= 16 B 走 `memmove`，更短逐字节；
段尾 = `\r` / 需交还吞掉换行的 `\n` / **真的拼接的 `\`**（不拼接的反斜杠不算段尾，否则段被切成字节级）。

| 项 | 结果 |
|---|---|
| release A/B | 284,517,726 → **272,817,234（−4.11%）**；**相对本轮起点 −6.17%** |
| 终止性回归网 | 400 个真实文件 × 10 秒上限 → **hangs: 0** |
| 闸门 | **325 / 0**、bootstrap **逐字节相同 + 21/21**、tcctests **106 / 0** |

**同一个错误犯了第二次**：段扫描遇到不拼接的反斜杠时 `stop == r` → 拷贝 0 字节 → 死循环（首次运行即挂住），
与 §8e 的 `convert_universal_chars` 同类。修法不是加兜底，而是让扫描**跳过不拼接的反斜杠**
（既保证 `stop > r` 不变量、又保持段完整），把不变量写进注释，并新增**有界终止性检查**作为回归网。

### R159 精确守卫（用户提议）—— ✅ **release 实测 −1.18%**；并删掉一个不可达分支

原守卫"文本里任何地方有反斜杠就跑整趟"，对真实 C 源码几乎不过滤（字符串与宏里到处是不拼接的反斜杠）。
按用户提议改为 `needs_newline_fix()`：`memchr` 找 `\r`（有则必定要跑），否则**只遍历反斜杠**检查其后面
是否为 `\n` —— 遍历对象是反斜杠而非每个字节。

| 项 | 结果 |
|---|---|
| **release A/B** | 287,906,292 → **284,517,726（−1.18%）** —— 比 §8ah 我自己的估计（−0.5~0.7%）好一倍 |
| dev 闸门 | **325 / 0**、bootstrap **逐字节相同 + 21/21**、tcctests **106 / 0** |

**用户的第二点**：`p[1] == '\r'` **不可能成立** —— 上面的 `memchr(p, '\r', len)` 已覆盖整个缓冲区，
任何 `\r` 都会先在那儿返回 true，因此反斜杠遍历时缓冲里没有 `\r`。这是**不可达分支**（会让读者误以为
存在这种情形），已删除并把理由写进注释；`\`+CRLF 的拼接仍由 `\r` 那一步捕获（实测 `int a = 1\<CRLF>+ 2;`
得 3 ✓）。

**累计（release，tccgen cc1）**：290,760,902 → 287,906,570（分配器拆分）→ **284,517,726（守卫）＝ −2.15%**。

（小记：本次两个定向用例"失败"又是**我的探针写错**（`s[2]` 应为 `s[1]`、文件作用域的 `int b = a + 1;`
本就要求常量初始化式），编译器两次都对 —— 本会话第三次"探针错、编译器对"。）

### R158 评估：归一化/拼接/行表"合成一趟并与词法穿插"能省多少 —— **约 0.1~0.2%，且单趟化不可行**

先量现状（有符号的调试剖面）：

| 事实 | 数值 |
|---|---|
| `canonicalize_and_splice` | **41,196,902 / 7.68%**（548M 口径），163 个文件各一次 |
| `build_line_offsets` | 自身 2,366 条 + 两次 `memchr`（已向量化）≈ 0.2~0.5M = **0.05~0.1%** |
| `read_file` | 246,746（0.05%）—— 读 1.16 MB 几乎免费 |
| **实际字节数** | **1.16 MB**（124 文件；tccgen.c 仅 268 KB，其余来自 322 处 include） |
| 折合 | 调试 35 条/字节；`-O2` 约 3~5 条/字节 → release 里该趟约 **4.6M = 1.6%** |

**结论一**：行表折进那一趟最多省 **0.1~0.2%**；"只扫一遍"其实**已经达到** —— 它本来就是一次线性扫描
（外加两次 `memchr` 守卫），只是扫的是 1.16 MB **文本**而非 token。

**结论二：与词法主循环穿插不可行**（三条语义理由）：① token 以 `loc` 偏移指向缓冲区、文本很久以后才经
`tok_text()` 读取 → 归一化结果必须**先**作为完整连续数组存在，否则每个消费者都要自己再归一化；
② **拼接改变 token 边界**（`foo\<换行>bar` 是一个标识符）→ 阶段 2 必须早于阶段 3；
③ `\r` 归一化改变字面量内容。这三条都是标准规定的处理阶段顺序，不是实现细节。

**结论三**：还能拿的是把那一趟**批量化**（`memchr` 定位 + 长段向量拷贝，与 §8af 同一手法），
估 **−0.8~1.2%（release）**；压缩后源目标重叠，之后需用 `memmove`（仍向量化）。

**口径警告（本轮第三次测量教训）**：release 剖面读到「`tokenize_file` 自身 80.5M = 27.97%」是**假象** ——
无符号 `-O2` 二进制里 callgrind 把内联的一团代码记在该 `???` 帧；有符号剖面显示它自身只有 **7,664 条**，
归一化是独立的 41.2M 子节点。**调用树只能从有符号构建里读。**

**剩余机会（有符号数据）**：`tokenize` inclusive 138.2M（自身 release 占 **23%**）、
`dumpir.c:dump_module` **132.6M / 24.7%**、`parse` 76.6M / 14.3%。

### R157 大块分配由调用点选择（`emalloc_big`）—— ✅ **release 实测 −0.98%**；并纠正一次假阴性测量

用户提问：① 重排 `emalloc` 内分支能否有可观测提升；② 能否把大块池分配独立、由调用点判断。

**① 不能**：`n >= BIG_THRESHOLD` 与 `free_len < n` 两次比较都还在，两者都高度可预测 → 指令数不变；
有收益的是**把测试从热路径上拿掉**。
**② 成立，已落地**：`emalloc_big()` 承担 `calloc` 那条路；调用点只需三处 —— `vnew()`
（一处覆盖编译器所有 vector）、`vgrow()`（经 `vnew`）、`read_file()` 的 `fstat` 路径。
`BIG_THRESHOLD` 随之移到 `cxx.h`（属于契约）。

| 项 | 结果 |
|---|---|
| **release A/B（`DEBUG=0`）** | 290,760,902 → **287,906,570（−0.98%）**，两次测量相差 14 条 |
| dev 闸门 | **325 / 0**、bootstrap **逐字节相同 + 21/21**、tcctests **106 / 0** |

**两个坑（比改动本身更有价值）**：
1. **调试构建里的断言抵消了被测量的改动**：我先加 `assert(n < BIG_THRESHOLD)` 作审计，
   于是调试剖面里那个测试又回来了（`emalloc` 自身 31.76M → 32.79M），量出 **+0.25% 的假阴性**。
   断言确实抓到一个真实调用点（`tests2/55_lshift_type` 向 `emalloc` 要 ≥128 KB，无害），
   但让测量失去意义 → **规则：把判断从热路径拿掉的改动，必须在 `DEBUG=0`（用户实际配置）里 A/B**；
   调试构建用于**定位**热点，用于**判定**收益会骗人。
2. 断言已移除，理由写进注释：池**能**接受大请求（块按需变大）只是浪费尾部 → "大请求走 `emalloc_big`"
   是**偏好**而非**前提** —— 这也正是只需改三处调用点、无需审计全部调用方的原因。

### R156 第 1 项收尾：`always_inline` + `memchr` 版整段拷贝 —— ✅ **−4.46%**（达成画像预估）

按 §R155 查明的两点各修一处：① 两个 ASCII 谓词加 `__attribute__((always_inline))`
（`-O0` 的构建不内联 `static inline`，`is_ident2_ascii` 原本仍是独立函数 13.82M / 2.58%）→ **离开热点榜**；
② `convert_universal_chars` 用 `memchr('\\')` 定位反斜杠，长段（≥16B）批量 `memcpy`、短段逐字节
（`-O0` 下 `memcpy` 是 libc 调用，短段更亏 —— 这正是上一版 +3.3M 的来源），并保留"扫描为空时无条件消费一字节"。

| 项 | 结果 |
|---|---|
| callgrind | 548,237,295 → **523,805,044**；相对起点 **−42.7%**；**相对第 1 项之前 −4.46%** |
| 热点榜 | `decode_utf8`、`is_ident2_ascii` 双双离开；`read_ident` 17.78M → 16.67M |
| 闸门 | **325 / 0**、bootstrap **逐字节相同 + 21/21**、tcctests **106 / 0** |
| 定向 | UCN 标识符 / 真 UTF-8 标识符 / `"\\u00"` 两字符 / 末尾孤立反斜杠不挂死 / 宽字符串 UCN / `-pedantic` 拒 `$` 均符合预期 |

（一条"没通过"的定向用例其实是**我的测试写错了**：C 里 `"a\\u0041b"` 是「反斜杠 + u0041b」，
`"a\u0041b"` 才是 UCN —— 两种写法都已单独验证，编译器正确。）

### R155 第 1 项：ASCII 快速路径 —— ✅ 落地（**−2.18%**，累计 −41.4%），并记一个自造的死循环

按 §R154 的第 1 项实施：`cxx.h` 加 `is_ident1_ascii`/`is_ident2_ascii`（内联，含 `$` 的 `-pedantic` 规则）；
**`unicode.c` 构造函数自检扩展到这两个谓词**（128 个 ASCII 码点与 `is_ident1/2` 逐一对拍，不一致拒绝启动 ——
这次改动的裁判，且始终开启）；`read_ident` 首字符与循环各加 ASCII 分支（循环 `continue`，跳过
`decode_utf8` 与表）；`convert_universal_chars` 整段拷贝 ASCII 段。

**修掉一个我自己引入的死循环**：整段扫描若立即停下（高位字节，或**末尾孤立反斜杠**），该字节无人消费 →
`while (p < end)` 永不结束（首次运行即挂住）。修法：扫描为空时无条件拷一字节。
**教训**：把 `else` 拆成 `else if` 必须证明"新分支并集 = 原 `else`"，漏掉的那类输入会**挂死**而非报错。
（本轮每个测试编译都加 `timeout`，挂死只损失一次运行。）

| 项 | 结果 |
|---|---|
| callgrind | 548,237,295 → **536,267,833（−2.18%）**；相对起点 **−41.4%** |
| `decode_utf8` | 3.28% → **离开热点榜** |
| 定向用例 | UCN 标识符、真 UTF-8 标识符、`"\\u00"` 两字符、**孤立尾反斜杠不挂死**、宽字符串 UCN、`-pedantic` 拒 `$` |
| 闸门 | **325 / 0**、bootstrap **逐字节相同 + 21/21**、tcctests **106 / 0** |

**差在预期之外的两点（已实测）**：① **`-O0` 下 `static inline` 不内联** —— `is_ident2_ascii` 仍是独立函数
（13.82M / 2.58%，即每字符一次调用）→ 加 `always_inline`；② **`convert_universal_chars` 反而 +3.3M**
（逐字节整段扫描 + 每段 `memcpy` 调用，短段更亏）→ 改成 `memchr('\\')` + 长段批量拷贝。

### R154 词法逐行画像（日志留存）—— 找到两个约 5% 与 1.5~2% 的目标

用户要求测量 `tokenize`/`canonicalize_and_splice` **内部**热点并**保留日志**。
用 callgrind 逐行归因（`DEBUG=1` 即 `-g -O0`），产物：

- `doc/lexer-hotspots.txt`：28,890 行，整个 `src/lexer.c` 的逐行成本 + 调用树（`--tree=both`，未裁剪）；
- `doc/lexer-hotspots-summary.txt`：62 行摘要（含下表与优先级）。

**`canonicalize_and_splice` = 纯逐字节状态机（40.9M / 7.45%）**：拷贝每个字节 15.33M（2.80%），
分支链 `while`/`\r`/`\n`/`\\` 各 ~5.64M（合计 ~22.6M）——**普通字节过四道判断**；
而 `memchr` 守卫仅 2,394 条（0.00%），"无事可做就跳过"这条路已经免费。

**`tokenize` 的开销在它调用的人身上**（自身仅 28.98M）：`read_ident` **50.96M / 9.29%（165,136 次）**、
`convert_universal_chars` 18.86M、`intern` 18.35M、`read_punct` 9.27M、`new_token` 合计 24.06M；
自身行里 `__ctype_b_loc` **被调 325,983 次**。

**最要紧的发现**：`read_ident` 里 `decode_utf8` 与 `is_ident2` **各被调 616,673 次**（= 165,136 个标识符
× 平均 3.7 字符）—— **每个标识符字符都走 UTF-8 解码 + Unicode 表判定**，而源码是 ASCII；
`convert_universal_chars` 同样（`decode_utf8` + `is_ident1` 各 165,136 次）。两处合计约 **5%**。

**下一步（按收益排序）**：① `read_ident`/`convert_universal_chars` 的 ASCII 快速路径（−4~5%）；
② `canonicalize_and_splice` 分支重排（−1.5~2%）；③ ctype 改范围比较（−0.5~1%）；
④ `intern` 的哈希（2.23%）；⑤ `new_token` 的 `emalloc`（1.65%）。

### R153 **74 MB 底噪不存在**：是 `%M` 的测量地板 —— 并给出本机正确测法

按 §R152 的下一步，在 `cleanup()`（`atexit` 注册，覆盖所有退出路径）加暂停钩子，
把 cc1 停住后直接问内核：

| 输入 | 进程自己 `ru_maxrss` / VmHWM | `/usr/bin/time -f %M` |
|---|---|---|
| 一行文件 | **2,604 / 2,992 KB** | 74,232 KB ✗ |
| tccgen.c | **66,112 KB** | 74,224 KB ✗ |
| sqlite3.c | **339,452 KB** | 339,416 KB ✓ |

**`%M` 在本机有约 74 MB 的地板**：真值低于它就抬到 74 MB，高于它才准。

- **"一行文件占 74 MB"不存在** —— 真值约 3 MB；此前 massif（堆峰值 585 KB）、`size`（bss 120 KB）、
  `emalloc` 大请求探针（一次都没有）三方**早就一致指向"没有这 74 MB"**，是我没有相信它们，
  反而去追 `strace` 的虚拟预留（第二个口径错误）。
- **sqlite3 的数字成立**（两法一致）→ `read_file` 的 **−17 MB 是真的**。
- 受影响的是所有 <74 MB 的 `%M` 读数（tccgen 也偏高 12%）→ 需作废重测。
- §R137 的内存分布用分配器自身记账 + massif，**不受影响**。

**正确测法（本机）**：① 进程内 `getrusage(RUSAGE_SELF).ru_maxrss`；
② 外部采样 `/proc/<pid>/status` 的 `VmHWM`（大输入可直接采样）；**不要**用 `%M`。
（暂停钩子已撤回，`make test` 全绿。）

### R152 追查 74 MB 底噪 —— 定位到"不是我们的内存"，并记下两条口径错误

按 §R151 的待查项逐项探测（一行文件、cc1 进程、RSS 74,220 KB、二进制 1.2 MB）：

| 探测 | 结果 | 结论 |
|---|---|---|
| `massif`（默认） | 堆峰值 **585 KB** | 不在 malloc 堆上 |
| `size`/`nm -S` | text 560 KB、data 84 KB、**bss 120 KB** | 无大静态数组 |
| `massif --pages-as-heap` | 5.7 MB —— ✗ **口径错误**：valgrind 接管 `mmap`/`calloc`，客体页记在 valgrind 账上，与原生 RSS 不可比 |
| `strace -e mmap,brk`（原生） | 276 MB/49 次，最大 147/61/29/15 MB，**brk 0 次** —— ✗ **口径错误**：那是 glibc 的 `MAP_NORESERVE` **虚拟**预留，不计入 RSS |
| `emalloc` 内打印 ≥1 MB 请求 + `addr2line` | **一次都没有** | 与 cxx 分配器无关 |

**结论**：该底噪是 glibc/内核侧的驻留（THP 触碰 4 KB 按 2 MB 记账是最常见的一类），
**且不是本轮引入**（分块池前后 tccgen 74,320 → 74,224 KB）。

**下一步（一次定位）**：在 `main` 结束前加暂停钩子（`CXX_PAUSE` 环境变量 + `getchar()`），
运行时读 `/proc/<pid>/smaps_rollup` 与 `smaps`，按 mapping 看 74 MB 落在哪 —— 比 massif/strace 直接。

**两条口径教训**：① valgrind 下 `--pages-as-heap` 的读数不能当原生 RSS；
② `strace` 的 `mmap` 长度是虚拟请求（glibc arena、`MAP_NORESERVE`），与 RSS 无关。

（本轮为诊断临时加的 `emalloc` 打印已撤回，`make test` 重新全绿。）

### R151 `read_file`：利用池的连续性去掉 `vgrow`/`memcpy` —— ✅ 落地（RSS **−17.3 MB**，指令 −0.28%）

用户指出：`emalloc` 是 bump 分配器，读文件循环中间没有别的分配，**每次 4096 的申请地址本来就连着**，
`vgrow`/`memcpy` 都是白付的（且 `vgrow` 把每一份中间缓冲都留在 arena 里）。

实现（`src/lexer.c:read_file`）：

1. **常规文件走 `fstat`**：尺寸已知 → 一次 `emalloc(size + 2)` + 一次 `fread`，**无循环、无增长、无拷贝**；
2. **管道等未知尺寸**：按 4096 步申请并**直接 `fread` 进去**，不再 `memcpy`；唯一破坏连续性的是
   **池块用尽**，所以**每次检查相邻性**（`more == buf + cap` 就原地延长，否则说明跨块 →
   取更大的一块拷贝一次，每个池块至多一次）。

**对用户所提 assert 的一处修正**：跨池块边界时相邻性**合法地**不成立，无条件断言会误报；
改成**始终生效的运行时分支** —— 比只在 debug 生效的 assert 更强（release 也安全，代价一次比较）。

| 项 | 结果 |
|---|---|
| 闸门 | `make test` **325 / 0**、bootstrap **逐字节相同 + 21/21**、tcctests **106 / 0** |
| 文件 vs 管道 | 同一程序两条路径一致（都 exit 13） |
| 峰值 RSS | **sqlite3 356,752 → 339,416 KB（−17.3 MB）**；tccgen 74,368 → 74,224 |
| callgrind | 549,776,615 → **548,237,295（−0.28%）**；相对起点 **−40.1%** |

**累计（tccgen cc1）**：914,592,386 → **548,237,295（−40.1%）**，输出逐字节不变。
待查项不变：一行文件仍占 74 MB（下一步 massif 定位）。

### R150 `emalloc`：内联**负收益已撤回**、分块池保留；`vnew`/`vgrow` 方案评估；发现 74 MB 固定底噪

1. **内联是负收益** ✗：假设「5.78% 主要是 call/ret」，于是把快路径内联（`static inline emalloc` +
   `emalloc_slow` 出线 + `Arena` 状态）→ **+0.9%（549,706,493 → 554,503,678）**。
   十来个指令的函数复制到 60 个调用点，省下的 2 条 call/ret 抵不过代码膨胀。**已撤回成一个函数。**
   这是「先量再判」的又一例：**热点函数的占比不等于调用开销**。
2. **保留分块池**：一次 `calloc(128 MB)` → 64 KB 起、按需翻倍到 8 MB。指令数 **+0.01%（中性）**；
   RSS 无变化（sqlite3 356,752 / tccgen 74,368）；收益是内存形态——去掉 128 MB 池的尾部浪费与虚拟空洞，
   并（按用户先前的观点）**让越界写更容易落在未映射页，保住"垃圾值＝越界写"的信号**。
   闸门：**325 / 0**、bootstrap **逐字节相同 + 21/21**、tcctests **106 / 0**。
3. **`vnew` 改 static**：实测 **8 文件 63 处**调用 → 不可能；头文件 `static inline` 需搬 `Vec` 布局，
   且 `vnew` 不在热点榜、又刚测出多点多内联可能净亏 → **不建议**。
4. **`vgrow(NULL)` 调 `vnew`**：`esz` 在数据指针**前面**的 `Vec` 头里，NULL 读不到 →
   **按现签名无法实现**；改签名牵动 **44 处**；现存 `vgrow(NULL, …)` 是 **0 处** → **不建议**。
5. **新发现（下一步目标）**：一行文件 `int main(void){return 0;}` 的 **cc1 进程 RSS 就有 74,220 KB**
   （二进制仅 1.2 MB，池已排除）→ 前端存在一处**与输入规模无关的 ~74 MB 预分配/触碰**。
   用 massif 量出调用点（`valgrind --tool=massif ./cxx -cc1 -cc1-input /tmp/tiny.c -cc1-output /tmp/tiny.ll`）。

### R149 `already_emitted`/`name_is_defined` 改 id 哈希集合 —— ✅ 落地（**−10.5%**，累计 −39.9%）

优化①把这两处的**字符串**比较换成了 id 比较，但 **O(n) 扫描本身还在**（8.16%）。两张表实际只是
成员集合 → 换成开地址哈希集合。两个必须注意的点：

1. **两个集合要分开**：`defined_set` 在打印前的预扫描里建，`emitted_set` 在打印中累积，**同时存活**
   （第一版用一个共享集合是错的）。
2. **用 `bool used` 判空，不用 `id == 0`** —— 上一轮刚踩过（interned id 可以是 0）。

**顺序按 §R146 的教训**：集合与两张表并存 + 每次查询逐项对拍（不一致即 `fatal()`），
三个真实文件 + 四道闸门全绿后才删数组与扫描、再量收益。

| 项 | 结果 |
|---|---|
| callgrind（tccgen cc1） | 614,002,443 → **549,706,493（−10.5%）**；相对起点 **−39.9%** |
| 热点榜 | `already_emitted`（原 8.16%）离开榜单 |
| 落位核对 | 集合查询 2 处、数组引用 0 处 |
| 闸门 | **325 / 0**、bootstrap **逐字节相同 + 21/21**、tcctests **106 / 0** |

**五轮累计**：914,592,386 → 788,204,173 → 654,694,956 → 614,002,443 → **549,706,493（−39.9%）**，
输出逐字节不变。

剩余热点（550M）：`canonicalize_and_splice` 7.43%、`emalloc` 5.78%（→ 按需分块，兼砍固定内存）、
`tokenize` 5.27%、`vfprintf` 4.40+3.51%（IR 打印）、`param_sym_of` 3.69%。
（过程小插曲：删除脚本把自己要修的两行当成"残留"而中止、未写入 —— 树停在已验证状态；
把重置语句挪到检查之前即通过。检查本身的断言也要写对。）

### R148 更正：`vnew` 是清零的；「垃圾值」＝越界写的症状 —— ✅ 已核实并修掉我写错的注释

用户指出：`vnew` 返回清零内存，若出现垃圾值说明有**隐蔽的越界写入**，正如文档里记录过的
irgen 分配基本块那次。核实结果：

- `emalloc()` **两条路径都是 `calloc`**（大块 `calloc(1, n)`、池 `calloc(1, POOL_SIZE)`）→
  `vnew()` 与 `vgrow()` 增长段都是清零的；
- 我上一轮在 `register_file()` 里写的 `// vnew does not zero` **是错的** ✗，随之而来的显式清零循环
  也是多余的 → 注释改成事实描述、循环删掉；`make test` **325 / 0**、bootstrap **逐字节相同 + 21/21**
  （删掉多余清零本就不改变行为）。

**先例（文档原文，§R46 之后的记录）**：`dump_blk` 的 phi 循环读到**垃圾 Phi 节点** →
「说明有东西写进了尚未分配的池空间，即某处**越界写**」；当时的归零「清掉的是受害区域，
不是那个越界写本身」。**这才是这条更正的价值**：垃圾值不是"没初始化"，是"有人越界写了"。

**带进③的纪律**：token 池按阶段释放之后，若在别处看到莫名其妙的值，第一反应是查越界写，
而不是加清零。（代码里另有几处同样多余的显式清零循环 —— `parser.c` 三处、`preprocess.c` 一处 ——
无害、非本轮改动，按最小改动原则保留。）

### R147 `convert_keywords` 槽表 —— ✅ 落地（**−6.2%**；根因：interned id 可以是 0）

**根因**：`intern()` = `bucket | (index << 12)`，所以**桶为 0 且是该桶首个字符串时 id 就是 0**。
第一版把 `kw_tab[h].id == 0` 当空槽标记 → ① 该关键字自身查不到；② **探测链遇到它提前终止**，
其后挂着的关键字全部查不到 —— 症状就是"关键字漏认"。修法：槽表加显式 `bool used`，
空槽与插入判定都用它。（原 `if (!kw[0].id)` 守卫有同样隐患，只是 `_Alignas` 的 id 恰好非 0。）

**顺序按 §R146 的教训反过来做**：先让槽表与旧线性扫描**并存**，对每个 `TK_IDENT` 同时跑两者、
不一致就 `fatal()` 打印记号文本/id/两个 kind；探针在 tccgen 与关键字密集文件上无一次不一致、
四道闸门全绿之后才删掉线性扫描，然后再量收益。

| 项 | 结果 |
|---|---|
| callgrind（tccgen cc1） | 654,694,956 → **614,002,443（−6.2%）**；相对起点 **−32.9%** |
| 热点榜 | `convert_keywords` 离开榜单 |
| 落位核对 | 线性扫描 0 处、`kw_tab[h].used` 3 处 |
| 闸门 | `make test` **325 / 0**、bootstrap **逐字节相同 + 21/21 对象**、tcctests **106 / 0** |

**四轮优化累计（tccgen cc1 指令数）**：914,592,386 → 788,204,173（`dumpir` 按 id 查重）
→ 654,694,956（ASCII 快速路径 + 词法三趟合一）→ **614,002,443（关键字槽表，−32.9%）**，
输出逐字节不变。

剩余热点与方向：`already_emitted` 8.16%（→ 按 id 的哈希集合）、`canonicalize_and_splice` 6.66%、
`emalloc` 5.17%（`POOL_SIZE` 128 MB 一次 calloc → 按需分块）、`tokenize` 4.72%、
`vfprintf` 3.94%（IR 打印）、`param_sym_of` 3.30%。

### R146 `convert_keywords` 槽表优化 —— ✗ 已撤回（实测 −6.3%，但关键字漏认）

把每个标识符扫 76 行关键词表改成按 id 低位的 256 槽开地址表 + 线性探测（表本身原样保留）：

| 项 | 结果 |
|---|---|
| callgrind（tccgen cc1） | 654,694,956 → **613,765,481（−6.3%）** |
| `make test` | **exit 2** ✗ |
| bootstrap | **cxx2 编过 17、失败 4** ✗（`a type specifier is required for all declarations`） |

失败模式是**漏认**关键字（`static`/`int` 没被识别，于是声明缺类型说明符）而非错认。
原因**未查明**：按阅读，建表与查找用同一套探测规则、同一 `id & 255` 索引。
已备好对照探针（对每个 TK_IDENT 同时跑新查找与旧线性扫描，不一致即 `fatal()` 打印记号文本与两个 kind），
但补丁锚点被上一次失败清理还原掉了，探针没跑起来。

**已撤回**：`src/lexer.c` 回到线性扫描＝已验证的 654,694,956 状态；四道闸门重新全绿
（`make test` 325 / 0、bootstrap 逐字节相同 + 21/21、tcctests 106 / 0）。

**教训（流程）**：把线性查找换成哈希这类改动，**先写与旧实现逐项对拍的探针，再谈收益**。
这次顺序反了 —— 先量收益再验正确性，于是拿了一个漂亮的 −6.3% 和一个坏掉的编译器。

### R145 优化②：ASCII 快速路径 + 词法三趟合一 —— ✅ 已落地（tccgen 相对优化①再 **−16.9%**，相对起点 −28.4%）

**「确定改动是否落位」这一步抓出两个缺陷**，两个都不是测试能发现的：

1. **`return false` 那一版根本没落位** ✗：它锚在 `if (c < 0x80) return (...)` 这行上，而前一次运行
   已经把那两行删掉了，于是断言失败、什么都没写 —— 结果树上**没有任何 ASCII 快速路径**，
   `in_range` 仍是 7.84%。
2. **融合把两个调用点都改名了** ✗✗：原来 `canonicalize_newline(p); remove_backslash_newline(p);`
   两行，被同一个 sed 都换成了 `canonicalize_and_splice(p);` → **融合函数被调用两次**。
   它是幂等的，所以测试全绿、输出不变，只有指令数露馅：那一项 103M 指令（占 13.26%）。

**用户的两处纠正都要记账**：
- 「`is_ident1`/`is_ident2` 之前已经为 ASCII 走了快速路径」—— 对，所以真正该写的不是
  「重新判断 A-Z/a-z」，而是 **`if (c < 0x80) return false;`**：ASCII 中所有可能为真的答案
  都在前面返回过了（表的 ASCII 区间实测就是 `A-Z`/`a-z` 与 `0-9`/`A-Z`/`_`/`a-z`），
  于是 ASCII 只需被挡在二分查找之外 —— 一条分支，零比较。
  我先写的冗余版本（重新比较区间）能拿到 −8.8%，删掉后掉回 −1.3%，**用户的形式又拿回全部并更多**。
- 「不需要单独为 ASCII 建表」—— 对：省掉惰性 init、额外状态与第二个真相源。保留的是
  `__attribute__((constructor))` 自检：128 个 ASCII 码点的快速路径与 `in_range` 逐一对拍，
  不一致即拒绝启动（这样即使重新生成表也安全）。

**验收**（tccgen cc1 指令数）：

| 阶段 | 指令数 | 相对起点 |
|---|---|---|
| 起点 | 914,592,386 | — |
| 优化①（`dumpir` 按 id 查重） | 788,204,173 | −13.8% |
| 优化②（ASCII 快速路径 + 三趟合一，修复后） | **654,694,956** | **−28.4%** |

`make test` **325 / 0**、bootstrap **逐字节相同 + 21/21 对象**、tcctests **106 / 0**。
热点榜：`canonicalize_and_splice` 与 `unicode.c:in_range` **双双消失**；剩下
`already_emitted` 7.65%（O(n) 扫描 → 按 id 哈希）、`convert_keywords` 6.60%（线性扫表 → 分桶）。

**教训（写进流程）**：脚本化改动必须**验证落位** —— 断言要通过、grep 要看到目标文本、
指令数要与预期方向一致。这一轮两个缺陷分别属于「补丁没应用」与「补丁应用过头」，
而四道闸门对两者都是绿的。

### R144 优化①：`dumpir` 的按名查重改成按 id —— ✅ 已落地（tccgen 指令数 **−13.8%**）

**工具上的更正**（用户指出）：valgrind/callgrind 在这台 WSL 上**完全可用** —— 我此前"跑不了"的结论错了，
因为我 profile 的是会 `fork+exec` 的驱动进程。正确调用是直接 profile cc1：

```
valgrind --tool=callgrind ./cxx -cc1 -cc1-input <src> -cc1-output /tmp/x.ll
```

这也让「按结构插桩」那套易错手段可以退休（§R142 里 tag 不清零的错误就是这么来的）。

**发现**：tccgen 的 cc1 里 **`__strcmp_avx2` 占 12.28% 指令**，来源是 `dumpir.c` 的两处按名线性查重：

```c
static bool already_emitted(Sym *sym) {
    char *name = emitted_name(sym);
    ...
    for (int i = 0; i < num_emitted; i++) if (!strcmp(emitted[i].name, name)) return true;
static bool name_is_defined(char *name) {
    for (int i = 0; i < num_defined_names; i++) if (!strcmp(defined_names[i].name, name)) return true;
```

`emitted_name()` 返回的正是 `str(sym->id)`（interned 字符串）或 `asm_name` —— **又是「在已哈希去重的
符号上做全量字符串比较」**，与 §R131 的 `find_ident` 同病。我上一轮把 `dumpir` 判成"调试路径、
不在编译路径上"是错的：`-c`/`-S` 每次都要打印 IR。

**改法**：新增 `emitted_id(sym)`（asm 名 intern 一次，否则直接用 `sym->id`），两张表从
`{char *name}` 改成 `{uint32_t id}`，`name_is_defined`/`record_defined_name` 收 id，
`emitted_name()` 随之退休（无调用者，留着会触发 `-Wunused-function`）。

**验收**：

| 项 | 结果 |
|---|---|
| **callgrind 指令数（tccgen cc1）** | 914,592,386 → **788,204,173（−13.8%）** |
| 热点榜 | `__strcmp_avx2`（12.28%）**从榜上消失**；`already_emitted` 6.74% → 6.36%（只剩 O(n) 循环本身） |
| `make test` | exit 0 —— conformance **325 / 0**、c2y **101 / 0** |
| bootstrap | **cxx2 = cxx3 = cxx4 逐字节相同**，21/21 目标文件（**IR 文本一字未改**） |
| tcctests | **106 ok / 0 failed** |

**改后的热点榜**（tccgen cc1，788M 指令）与下一步目标：

| 占比 | 函数 | 可做的方向 |
|---|---|---|
| 7.74% | `unicode.c:in_range` | 区间查表 → 二分/跳表 |
| 6.36% | `dumpir.c:already_emitted` | 剩下的 O(n) 扫描 → 按 id 的哈希集合 |
| 5.49% | `lexer.c:convert_keywords` | 关键词表 → 按 id 命中（表里已有 id） |
| 5.46% + 5.42% + 4.06% | `remove_backslash_newline` / `canonicalize_newline` / `build_line_offsets` | 三趟逐字符扫描 → 合成一趟 |
| 4.03% | `util.c:emalloc` | `POOL_SIZE` 128 MB 一次 calloc → 按需分块 |
| 3.68% / 3.07% / 2.57% | `tokenize` / `vfprintf`（打印 IR）/ `param_sym_of` | 分别在词法、IR 打印、参数查找 |

### R143 查 dumptok/dumpast（用户提醒）—— 顺带把③的改动面大幅缩小，并修正我两处错误

**先答问题**：`dumptok.c` 与 `dumpast.c` 在 slice 3 里都已转换（`tok->file` → `file_of(tok->file_uid)`），
构建、残留检查与四道闸门都过了。但用户提醒得有道理 —— 真正要问的是**它们相对于「释放 token 池」
这一点在流水线的什么位置**。

**流水线（main.c 行号）**：

| 行 | 动作 | 需要 token？ |
|---|---|---|
| 1014 | `tokenize_file` | — |
| 1017 | `dump_raw_tokens`（`-dump-raw-tokens`） | 是 |
| 1019 | `preprocess` | — |
| 1021 | `dump_tokens`（`-dump-tokens`） | 是 |
| 1030 | `-E` → `print_tokens` → return | 是 |
| 1035/1037 | `filter_tokens` / `join_adjacent_string_literals` | — |
| 1039 | `parse` | — |
| **1041** | **`dump_ast`（`-ast-dump`）** | **是**（`dumpast.c` 读 3 处 token 文本） |
| **1043** | **`fold_ast`** | **是** |
| 1050 | `-fsyntax-only` → return | — |
| 1052 | `irgen` | **否**（`irgen.c`/`dumpir.c` 各 0 处） |

**token 读者的完整人口（这次含所有文件）**：`parser.c` 111、**`opt_ast.c` 17**、`preprocess.c` 13、
`dumpast.c` 3；`dumptok.c`/`irgen.c`/`dumpir.c`/`lexer.c`/`main.c` 各 0。
**它们全部在 1043 之前。**

**由此得到三条结论**：

1. **`fold_ast` 定义在 `opt_ast.c:905`**（我此前说「在 type.c」是错的）。它一边改写节点一边
   `node->tok = x->tok` 保留位置，并用 `node->tok` 报 17 处诊断 —— 也就是说**折叠器本身**是
   「跨过释放点」的主要嫌疑，而它在释放点**之前**跑完。
2. **把释放点定在 `fold_ast` 之后、`irgen` 之前，就没有任何 token 读者在释放之后运行** ——
   irgen 与 dumpir 一处都不读 token。所以 **③ 不需要转换那 ~35–40 处诊断**：§R138 把改动面估成
   「约 40 处要改 Loc」是基于「它们跨过释放点」的错误前提。**③ 的实际工作量 = 给 token 单独开一个池 +
   在 `fold_ast` 之后释放 + 验证**，比原计划小一个数量级。
3. **但这是三条硬约束**，必须写进代码注释并尽量加断言：
   - `dump_ast` 必须留在 `fold_ast` **之前**（它依赖 token 池）；
   - `-fsyntax-only` 的返回点在 `fold_ast` 之后，释放必须在返回前完成；
   - `irgen`/`dumpir` 的诊断必须保持不读 token（现状全是 `fatal()`，一旦有人加了带位置的诊断，
     释放后就是悬垂指针）。
   （`Node.tok` 在释放后仍指向已释放的池，但那之后没人读它 —— 这一点要靠上面的约束来守，而不是靠类型。）

**修正后的③（下一轮）**：① 给 token 阶段单独的 arena（`emalloc` 之外的第二套 bump 池，
只由 `new_token`/`copy_token` 等 token 分配使用）；② `fold_ast` 之后释放它；
③ 用「分配器统计 + 峰值 RSS」验证（预期 sqlite3 `-fsyntax-only` 345 → 约 115 MB）；
④ 顺带用 `-dump-tokens`/`-ast-dump` 用例确认两个打印器仍正常。

### R142 ②slice3 落地：`Token.file` → 4 字节 uid + 尾部打包，**`sizeof(Token)` 64 → 56** —— ✅ 已落地；内存效果**未证实**

按 §R140/R141 的修正方案实现：

- `Token.file`（8 字节指针）→ `Token.file_uid`（4 字节），文件表（`file_tab`）从 lexer.c 导出，
  `file_of()` 改为头文件里的 **static inline**（`tok_text()` 是最热的那条路径，不能变成跨 TU 调用）。
- 尾部打包：`{uint16_t lit_suffix | uint8_t enc_prefix}` + `kind` + 3 个 `bool`（6 字节）
  → `uint16_t lit_suffix` + `uint8_t kind` + `uint8_t enc_prefix:4` + 三个 `bool:1`（4 字节）。
  位域读取与原来的普通字段写法相同，**调用点一处未改**。
- 全部写者/读者转换：`tok->file = X` → `tok->file_uid = X->uid`；`tok->file->` → `file_of(tok->file_uid)->`；
  裸 `tok->file` → `file_of(tok->file_uid)`（lexer/preprocess/util/dumpast/dumptok/main 共 6 个文件）。

**验收**：

| 项 | 结果 |
|---|---|
| `sizeof(Token)` | **56**（改前实测 64，−12.5%） |
| `make test` | exit 0 —— conformance **325 / 0**、c2y **101 / 0** |
| bootstrap | **cxx2 = cxx3 = cxx4 逐字节相同**，21/21 目标文件（字段布局改了而输出不变，这是关键证据） |
| tcctests | **106 ok / 0 failed** |
| sqlite3 `-fsyntax-only` 峰值 RSS | **356,772 KB**，改前记录 356,600 KB —— **没有可测到的下降** ✗ |

**为什么峰值没动，以及我的错误**：我按「2,709,107 个 Token × 8 字节 ≈ 21 MB」预期下降，但那个
2,709,107 来自 §R137 的分结构统计，而**那次插桩的 tag 从不清零** —— `mem_tag` 在某个构造器里置位后
一直保留，后续分配都被计入该 tag。所以「Token 137.1 MB / 2,709,107 次」是**上界而非实数**，
真实的 `Token` 数量可能远小于此，8 字节 × 真实数量也就只有几 MB，测不出来。

**因此下一步不是③，而是先把人口数准**：给 `new_token` 加一个只计数的探针（不改 tag 语义），
量出 sqlite3 真实的 Token 数量与字节数，再决定「释放 token 池」（③）的收益到底有多少 ——
§R137 的**阶段**分布是准的（按阶段边界记账，不受 tag 影响）：tokens 230.3 MB / parse 114.9 MB，
所以③的上界（把整个 token 阶段释放掉）仍是 −230 MB 量级，只是 Token 结构本身占多少要重测。

### R141 ②slice2 落地：`SrcFile` 唯一 uid + `Loc` 20 字节 + **全部诊断已走 Loc 路径** —— ✅ 已落地并证明等价

按 §R140 的修正方案实现：

1. `SrcFile` 增 `uint32_t uid` —— `new_file()` 里一个计数器（`register_file()`），**与显示名 id 分开**；
   `new_file` 是每个文件（含 scratch、`<built-in>`、命令行）的唯一入口，所以全部登记；
   表用 `vnew` 分配并**显式清零**（`vnew` 不清零，这是不会构建报错的那类坑）。
2. `Loc` 去掉指针，改为 `{filename, file_uid, loc, len, line_delta}` = **20 字节**（实测 `sizeof(Loc) == 20`）。
3. `loc_of()` 填 `file->uid`，`emit_diag_loc()` 用 `file_of(at.file_uid)` 取回文件。
4. **把 `diag()` / `diag_exit()` 本体改成走 `loc_of` + `emit_diag_loc`** —— 于是全编译器
   **每一条诊断**都经过 Loc 路径，而不是只有少数几个保留位置的调用者经过。这是真正证明
   「Loc 装得下诊断所需的一切」的方式：整个测试套件都在跑这条路径。

**验收（Loc 路径为唯一路径的情况下）**：

| 项 | 结果 |
|---|---|
| `make test` | exit 0 —— conformance **325 / 0**、c2y **101 / 0** |
| bootstrap | **cxx2 = cxx3 = cxx4 逐字节相同**，21/21 目标文件 |
| tcctests | **106 ok / 0 failed** |
| 宏展开错误 | `/tmp/d1.c:2:25: error: expected expression before ')'` + caret，与改前逐字相同 |
| note 对（重定义） | `error: redefinition of 'f'` + `note: previous definition is here`（两处位置都正确） |
| **`#line` 改名** | `#line 100 "other.c"` 后报 `other.c:100:25: …` —— **显示名与物理文件不同的那一类**，正是 `filename` 与 `file_uid` 必须分开的理由 |

下一步（slice 3）：把 `tok->file` 的 28 处读者改为 `file_of(tok->file->uid)` 形态并删掉 `Token.file`
（−4）+ 尾部打包（−2）→ 结构 62 → 56（约 −21 MB）；随后才是③（`Node.tok` → `Loc` + 在 `fold_ast`
之后释放 token 池，−230 MB）。

### R140 ②slice2 撤回 + 一个拦住设计错误的发现：`SrcFile::id` 不是文件的唯一键 —— ✅ 已查实并还原

slice 2 本想把 `Loc` 变成自包含（用文件 **id** 而不是 `SrcFile *`，让 `Loc` 从 24 字节降到 20，
并为后面去掉 `Token.file` 铺路）。**构建失败拦住了它**，查因后发现设计前提是错的：

```c
SrcFile *new_file(char *name, int file_no, char *contents) {
    ...
    file->id = intern(name, strlen(name));   // 显示名的 interned id，不是文件句柄
```

- `SrcFile::id` 是**文件名（显示名）的 interned id**，同名文件共享同一个值，**不能当唯一键**；
- 而且 `#line` 会把显示名换成另一个（`tok->filename = display_name` 而 `tok->file = cur_file`），
  二者本来就可以不同 —— 所以 `Loc` 里**两个字段都必要**（显示名给诊断打印，文件给内容/行表）。

**已还原**：`Loc` 回到 slice 1 的形态（带 `SrcFile *file`，24 字节），`loc_of`/`emit_diag_loc` 回到
`at.file`；构建通过，`make test` exit 0（conformance **325 / 0**、c2y 101 / 0），slice 1 的
`Loc`/`loc_of`/`diag_loc` 仍在位。

**修正后的 slice 2**：给 `SrcFile` 添一个**新的唯一 uid**（`new_file()` 里一个计数器，1 行）+
按 uid 建表 + `Loc.file_uid`；然后逐个改掉 `tok->file` 的 28 处读者（preprocess 14、util 5、
dumptok 5、lexer 2、dumpast 1、main 1），`filename` 保留给 `#line`；最后去掉 `Token.file`
（−4）并把尾部 6 字节打包成 4（−2）—— 结构 62 → 56，**−8 字节 = 约 −21 MB**（token 阶段 137 MB）。
之后才是③（`Node.tok` → `Loc`，`fold_ast` 之后释放 token 池，−230 MB）。

### R139 ① `Token` 压缩：实测推翻我自己的估算 —— ✅ 已定量，需改配方

§R138 把「压缩 `struct Token`」列为低风险第一步，估 −38 MB。**实测结果不同**：

| | 值 |
|---|---|
| `sizeof(Token)` | **64 字节**（`_Alignof` = 8，不是 16） |
| 布局 | `next` 8 + `origin` 8 + 值 union 16 + `file` 8 + `loc`/`len`/`filename`/`line_delta` 16 + 尾部 6 = 62 → **补齐到 64** |
| 尾部小字段 | `{uint16_t lit_suffix \| uint8_t enc_prefix}` 2 + `kind` 1 + 3 个 `bool` 3 = **6 字节** |

**结论：单靠位域打包是无效的。** 尾部 6 字节压到 4 只省 2，结构仍是 62→60，按 8 字节对齐
**照样是 64** —— 要真正变小，必须一次减掉 ≥8 字节。可选的 8 字节只有三处：

| 候选 | 省 | 改动面 | 风险 |
|---|---|---|---|
| `SrcFile *file` → 32 位 `file_no`（`lexer.c` 已有 `input_files[]` 表） | 4（+ 尾部打包 2 → 共 6 ✗ 不够，需再省 2） | `->file` 读取 28 处（preprocess 14、util 5、dumptok 5、lexer 2、dumpast 1、main 1） | 中：`new_file` 造的 scratch/内建文件**未必**进 `input_files[]`，需先核实 |
| `origin` → `Loc` 链 | 8 | 与步骤②③同一批 | 中：宏展开诊断靠它 |
| 值 union（16）移出为旁表 | 16 | 大 | 高 |

也就是说：**①不是零风险的第一步，它要么与②③合并做，要么先做「`file`→`file_no` + 尾部打包」并核实 scratch 文件**。
按实测，正确的顺序是**先②（`Loc`）再③（释放 token 池 + 顺手去掉 `origin` 与 `file` 指针）**——
届时 `Token` 由 64 降到约 48（−25%，约 34 MB），且这 34 MB 是叠加在③的 −230 MB 之上的。

**修订后的执行顺序**：②`Loc` 类型与转换点（纯重构，ASan + 全套闸门守住）→ ③`Node.tok`→`Loc`、
`fold_ast` 之后释放 token 池、同时去掉 `Token.origin` 与 `Token.file` 指针（−230 MB − 34 MB）→
④ `-g` 时再给 IR 加 `loc` 钩子。①降级为③的一部分。

### R138 多池 + `Loc` 抽取方案评估 —— 📋 方向正确，但**改动面比设想集中**，建议分四级落地

用户提议：把分配改成「永不释放池 + 类型池 + 预处理池 + token 池 + node 池」，按阶段批量释放；
把诊断所需字段抽成 `Loc`（文件/行/偏移/长度），各阶段统一使用，`tok`→AST 只浅复制指针；
宏展开时共享 `Loc` 因而 `origin` 可去；IR 阶段是否预留 `loc` 供 `-g`。

**一、实测基础（§R137）**：sqlite3.c `-fsyntax-only` 峰值 356,600 KB，请求 345.3 MB ——
tokens **230.3 MB（66.7%）**、parse/AST **114.9 MB（33.3%）**；按结构 `Node` 147.3 MB、
`Token` 137.1 MB、Type 15.0、Sym 13.9、other 32.0。`-c`：+irgen 66.4 MB（16.1%）= 411.6 MB。

**二、改动面（本轮实测，比预期小很多）**：

| 项 | 数量 | 说明 |
|---|---|---|
| 读取 token 的站点（`->tok`/`.tok`） | **175** | parser.c 111、type.c 31、preprocess.c 13、dumpast.c 3 |
| 诊断调用点 | **494** | parser.c 333、preprocess.c 66、type.c 23、lexer.c 22… |
| **`parse()`/`fold_ast()` 之后仍需 token 的** | **约 35–40** | **type.c 的 31 处 + 少量 parser 尾部**；`irgen.c` 一处都不读 token（它的诊断全是 `fatal()`） |
| `struct Token` | 约 50 字节 | 2,709,107 个 → 137.1 MB |

也就是说：**parser 自己那 333 个诊断和 111 个 token 读取不需要改**（它们都在 token 池存活期间跑），
真正要改成 `Loc` 的只是**跨过释放点的那三四十处**。这使方案从「大手术」变成「中等改动」。

**三、逐项评估**：

| 方案部件 | 收益 | 代价 | 风险 | 结论 |
|---|---|---|---|---|
| **按阶段释放 token 池**（释放点应定在 **`fold_ast` 之后**，不是 `parse` 之后——折叠期 type.c 仍在用 token 报诊断） | fso 峰值 **345 → 约 115 MB（−67%）**；`-c` **412 → 约 181 MB（−56%）** | 约 40 处改 Loc + 释放点接线 | 中等：悬垂指针类 bug，**必须配 ASan 构建**（valgrind 在本机跑不了 cxx，实测过） | **值得做，是最大的一笔** |
| **`Loc` 抽取**（`{file_idx, loc, len}` 12–16 字节，按值传递） | 前提性工作；也是 `-g` 的必需品 | 新类型 + 约 40 处转换 | 低-中：诊断文本/宏展开提示必须逐字复现（conformance 的 `bad` 断言 + driver 的 `__LINE__`/`_Pragma` 用例守住） | **做，但只覆盖跨释放点的部分**；让 parser 继续用 `Token`，别全区铺开 |
| **`origin` 去掉**（2,709,107 × 8 = **22 MB**） | 22 MB + 复制更简单 | Loc 里要带上展开链（每次宏调用一个，共享） | 中：`__LINE__`/`__FILE__` 与「in expansion of」都靠它 | **与 Loc 一起做**，不要单独动 |
| **压缩 `struct Token`**（`origin` 8 + `filename` 4→16 位索引 + `line_delta` 4 + 3 个 bool→位域） | 约 50 → 36 字节 ⇒ **省约 38 MB** | 局部改动，不动生命周期 | **低** | **建议作为第一步**（不碰生命周期就能拿到 ~11% 峰值） |
| **池子拆成 6 个** | 主要是「能否选择性释放」；碎片与对齐几乎为零（请求 345.3 MB vs 峰值 348 MB） | N 个分配器 + 每个分配点要指名池 | 低 | **只需 3 个**：永不释放（Sym/loc/type 的稳态部分）、token、node/type 阶段池。**「预处理池」是多余的** —— 预处理那 93 MB 全是 token 副本，本就在 token 池里 |
| **IR 预留 `loc` 跟 `-g`** | 未来 `-g` 的 DWARF 需要 | —— | —— | **现在不设计**：`-g` 属功能开发，当前阶段是 debug；只要保持 `Loc` 与阶段无关，irgen 以后引用它即可 |

**四、对原方案的改进点**：

1. **释放点写成 `fold_ast` 之后**（原方案说 parse 之后；实测折叠期 type.c 仍以 token 报诊断）。
2. **`Loc` 只覆盖跨释放点的路径**，不要全区替换 —— 333 个 parser 诊断留在 `Token` 上，
   改动量因此从「数百处」降到「三四十处」。
3. **3 个池而不是 6 个**；「预处理池」并入 token 池（实测那 93 MB 就是 token 副本）。
4. **先做零风险的 Token 压缩**（−38 MB），再做生命周期改造（−230 MB）—— 两笔账分开量。
5. **`Loc` 用值类型 + 文件索引**（12–16 字节），不要「指针 + 另一个 24 字节对象」；
   宏展开链每次展开共享一个，正是 `origin` 的替代。
6. **安全网必须是 ASan 构建的 cxx**（`-fsanitize=address`）跑全套 conformance + bootstrap +
   tcctests + 三个交叉目标 —— 这一改动的失败模式正是 use-after-free，valgrind 在本机不可用（实测）。
7. **别把它当性能优化**：前端已在 gcc 持平线（§R135），这一改动的目标是内存；速度只有
   局部性带来的间接收益，量不出来也不该写进收益。

**五、总体判断**：诊断对（token 阶段占 67%），`Loc` 是正确的抽象（也是 `-g` 的前置），
但原方案的改动面被高估了 —— 真正的必改点只有约 40 处 + 一个释放点 + 新类型。
建议分四级：① 压缩 `Token`（低风险，−38 MB）→ ② 引入 `Loc` 并只改跨释放点的诊断
（纯重构，无内存变化，用 ASan + 全套闸门守住）→ ③ 把 `Node.tok` 换成 `Loc` 并**在 `fold_ast`
之后释放 token 池**（−230 MB）→ ④ 需要 `-g` 时再给 IR 加 `loc` 钩子。

### R137 token / AST / IR 三阶段内存分布 —— ✅ 已实测（sqlite3.c）

在唯一的分配 choke point（`util.c` 的 `emalloc`，bump pool）按阶段与结构打点，`-O2`：

**`-fsyntax-only`（峰值 356,600 KB；请求总计 345.3 MB，与峰值近 1:1）**

| 阶段 | MB | 占比 | 次数 | | 结构 | MB | 占比 |
|---|---|---|---|---|---|---|---|
| tokens | **230.3** | **66.7%** | 3,783,074 | | **Node** | **147.3** | 42.7% |
| parse（AST） | **114.9** | **33.3%** | 890,229 | | **Token** | **137.1** | 39.7% |
| | | | | | Type | 15.0 | 4.3% |
| | | | | | **Sym** | **13.9** | **4.0%** |
| | | | | | other（大块） | 32.0 | 9.3% |

**`-c`（含 IR）**：tokens 230.3（56.0%）+ parse 114.9（27.9%）+ **irgen 66.4（16.1%）= 411.6 MB**；
`Type` 由 15.0 涨到 **81.3 MB**（irgen 为每条指令建类型），`Node`/`Token` 不变。

结论：① 内存主因是 **token 阶段（fso 67% / `-c` 56%）与 `Node`（147 MB）**，不是 IR（16%）；
② **`Sym` 只占 4.0%**，与 §R136 一致；③ 正路是**按阶段回收 token 区域** —— 但每个 `Node` 都带
`Token *tok` 供诊断用，所以要么让诊断改读源文件，要么把记号缩成 `{file, loc, len}` 紧凑三元组
（可把 137 MB 砍到约 60 MB）；④ AST 在 irgen 之前不能释放。

### R136 `Sym` 函数专有尾部封装评估 —— ✅ 已定量：结构上成立，收益上不成立（0.4%）

提议：把 `Sym` 里 11 个只有函数用的字段（`funcspec`…`indirectbr`）封装成单独结构，`Sym` 只留指针。
实测（sqlite3.c `-fsyntax-only`，`-O2`，峰值 356,728 KB）：`sizeof(Sym)` = **224 字节**，
其中函数尾部 **80 字节（35%）**；分配 `Sym` **26,360** 个，其中函数 4,935（**18.7%**）；
**全部 `Sym` = 5.9 MB = 峰值的 1.7%**；尾部总开销 2.0 MB；**移出可省 1.5 MB = 0.4%**。
tccgen：7,552 个 `Sym` = 1.6 MB，可省 0.3 MB。

**结论：不做。** 形状对（35% 的字段只有函数用），但人口太小 —— 省不到峰值的 0.4%；
速度论据也弱（5.9 MB 本就在 L3 内，量不出差别）；风险面反而广（11 个字段的读写点遍布
parser/irgen/dumpast，且需在「符号变成函数」时惰性分配 `sym->fn`，漏判即空指针）。

**真正的大头**（估算待确认）：300–500 万 token + 100–200 万 AST 节点，两者都从**永不释放的 arena**
分配，是数百 MB 量级，而 `Sym` 5.9 MB、intern 表约 1 MB、`NameSpace` 约 1.3 MB 都只是零头。
方向应是**按阶段回收**（token 在 `parse()` 之后即死），而不是缩小结构体。
下一步：用正确的 `emalloc` 签名做一次尺寸直方图，拿到确切分布再动。

### R135 全量回归 + 最新对比数据 —— ✅ 无回归；前端与 gcc 持平，端到端多数快过 gcc

**全量闸门**：`make test` exit 0（conformance **325 / 0**、c2y **101 / 0**）；bootstrap
**cxx2 = cxx3 = cxx4 逐字节相同 + 21/21 对象**；tcctests **106 / 0**；
arm64 / rv64 / rv32 各 **51 / 0**（rv32 另 1 skipped）；`doc/realworld.sh`：
lua 35/0、zlib 15/0、libpng 18/0、sqlite 1/0、tinycc 21/0、**git 567/0**、
cpython **381 ok / 4 failed**（3 条 `implicit declaration` + 1 条解析缺口，均在原缺口表内）。
`doc/crash-smoke.sh` 本次是调用方式不对（它要日志文件），报 `no tests found`，与改动无关。

**`doc/speed.sh`（cxx 按 `make DEBUG=0` 发布构建，同机同次运行，3 次取最快）**：

| 负载 | 模式 | gcc | clang | cxx | cxx/gcc |
|---|---|---|---|---|---|
| sqlite3 | fso / full | 0.66 / 6.55 | 1.03 / 2.35 | **0.67 / 3.09** | **1.01 / 0.47** |
| cpython-parser | fso / full | 0.20 / 1.43 | 0.45 / 0.67 | **0.28 / 0.98** | 1.40 / 0.68 |
| cpython-ceval | fso / full | 0.24 / 1.52 | 0.41 / 0.63 | **0.31 / 1.05** | 1.29 / 0.69 |
| cpython-compile | fso / full | 0.15 / 0.25 | 0.25 / 0.28 | **0.21 / 0.51** | 1.40 / 2.04 |
| cpython-pickle | fso / full | 0.19 / 0.67 | 0.31 / 0.45 | **0.26 / 0.74** | 1.36 / 1.10 |
| tinycc-tccgen | fso / full | 0.08 / 0.43 | 0.14 / 0.21 | **0.11 / 0.31** | 1.37 / 0.72 |
| zlib-deflate | fso / full | 0.02 / 0.12 | 0.04 / 0.06 | **0.04 / 0.09** | 2.00 / 0.75 |
| lua-lparser | fso / full | 0.02 / 0.14 | 0.05 / 0.08 | **0.04 / 0.10** | 2.00 / 0.71 |

几何平均：**前端 ≈ 1.4× gcc**（sqlite3 上 1.01×），**端到端 ≈ 0.8×，平均快过 gcc**，
8 个负载 7 个快于 gcc（例外 cpython-compile 2.04×，小文件上 IR 往返固定成本占比高）；
**对 clang 前端全面更快、端到端 4/8 更快**。
sqlite3 的 `-c`：cxx **2.91 s** vs gcc 5.22（快 1.8×）vs clang 2.43（慢 1.20×）——优化开始时是 9.84 s。

**下一步两个明确目标**：① 内存（sqlite3 前端 348 MB vs gcc 84 / clang 125，arena 从不释放）；
② IR 文本往返（端到端里约 0.8–1.1 s/TU，需 cxx 用 LLVM API 建模块）。

### R134 合并两次 clang 调用 —— ✅ 已落地（`-c` 省 12–31%，execve 18 → 12）

实现 §R133 的第 1 条：`-c` 与链接路径改为一次 `clang -x ir … -c -o out.o`（新增 `compile_object()`），
不再 `-S` 出汇编再 `-x assembler -c`；`-S` 与 `-x assembler` 输入的老路径不变。

| 负载 | 两次 clang | 合并 | 变化 |
|---|---|---|---|
| sqlite3.c `-c`（第一次量 / 第二次量） | 3.60 / 0.74 s | **2.83 / 0.72 s** | **−21.3% / −2.7%** |
| tccgen.c `-c` | 0.44 / 0.49 s | **0.31 / 0.40 s** | **−29.5% / −18.3%** |
| lua/lparser.c `-c` | 0.13 s | **0.09 s** | **−30.7%** |

（同机两次运行绝对值差 4×，只报同一次运行内的比值；两次都更快。）
**execve 18 → 12**（`strace -f -e trace=execve`，含驱动自身/`/bin/sh`/PATH 探测）。
**输出**：三个负载目标文件大小相同，`cmp` 差 574/225856 字节，`objdump -d` 显示只在**对齐 NOP 填充**上
不同 —— 同代码、不同对齐。闸门全绿：`make test` exit 0（conformance **325 / 0**、c2y 101 / 0）、
bootstrap **逐字节相同** + 21/21 对象、tcctests **106 / 0**。

管道喂 IR（§8i 第 2 条）**未做**：合并后只剩 cc1 写的 `.ll`，要省掉它需让 cc1 直接写管道
（当前 `-cc1-output` 是路径），按体积估算收益个位数百分比，留待与「LLVM API 建模块」一起评估。

### R133 当前瓶颈评估：IR 往返占 `-c` 的约 65% —— ✅ 已定量（未改代码）

`cxx -### -c sqlite3.c` 的流水线：`cxx -cc1`（前端 → IR 文本）→ `clang -x ir -S`（IR → 汇编）
→ `clang -x assembler -c`（汇编 → 目标文件），**3 个进程、2 个临时文件**。

分段（`-O2`，sqlite3.c，best of 3）：

| 阶段 | 秒 | 占 `-c` |
|---|---|---|
| `-fsyntax-only`（纯前端） | **0.58** | 18% |
| `-S`（前端 + irgen + 打印 IR + clang IR→汇编） | 2.65 | 84% |
| `-c`（再加汇编器） | 3.17 | 100% |
| clang 自己 `-S` / `-c` 同一份 C | 2.20 / 2.02 | — |
| gcc `-c` 同一份 C | 5.16 | — |

结论：① **前端已不是瓶颈**（0.58 s，比 clang 自己的 0.94 s 还快）；
② **IR 往返 ≈ 2.1 s（65%）** —— clang 从 C 直接做同样后端只要 1.26 s，**走文本 IR 多花约 0.8 s**；
③ 多出的汇编器进程 ≈ 0.5 s（`-c` − `-S` = 0.52）；
④ cxx 3.17 s 对 clang 2.02 s（1.57×），而 cxx 前端本快 0.36 s —— 后端不绕圈理论上约 1.7 s，**快过 clang 自己**。

建议（按收益/代价，均未做）：合并「IR→汇编→目标文件」为一次 `clang -x ir … -c`（省一进程一文件往返，
约 0.1–0.3 s，驱动约 10 行）→ 管道喂 IR 省掉 `.ll` 临时文件 → **真正的修法是让 cxx 用 LLVM API 建模块**
（约 0.8–1.1 s/TU，属驱动架构改动，先量后动）→ 前端再挖需重新取 profile。
另记：每 TU 3 进程 2 临时文件，在 `make -j` 并行构建时会被成倍放大。

### R132 `find_ident` 改走哈希桶 —— ✅ 已落地（sqlite3 前端 1.53 → 0.48 s，−68.6%）

§R131 方案 ② 实现：`find_ident()` 不再沿 `sc->vars` 扫作用域全部名字，改为遍历该名字的哈希桶链。
等价性由 `push_namespace()` 保证 —— 每个名字**同时**挂进 `vars` 与 `ht[h]`，都是最新在前，
rehash 的注释也明确保持该顺序；桶链里同名的相对次序与 `vars` 链一致，只是不再经过无关名字。

**A/B（都 `-O2`，同一次运行）**：sqlite3 `-fsyntax-only` 1.53 → **0.48 s（−68.6%）**；
tccgen 0.33 → **0.09 s（−72.7%）**；lua 0.04 → 0.03（−25%）。
闸门：`make test` exit 0（conformance **325 / 0**、c2y 101 / 0）、bootstrap **逐字节相同** + 21/21 对象、
tcctests 106 / 0；另用含 typedef / 内层同名 typedef 遮蔽 / 属性 / `__func__` / 字面量 / 复合字面量的
用例比对 `-S`：**逐字节相同**。

**①（typedef 名快速否定）评估结论：不做。** 桶遍历后 `find_ident` 每次只走 1–3 步（原 378 步），
`is_typename` 已非瓶颈；再加一层集合只会多一处需要撤销逻辑的状态。

**位置（同一次运行 best-of-5 / best-of-3）**：sqlite3 `-fsyntax-only` gcc 0.49 / clang 0.97 / **cxx 0.65**；
tccgen 0.10 / 0.15 / **0.11**；lua 0.02 / 0.05 / **0.04**；`-c`：sqlite3 6.03 / 2.39 / **4.09**，
tccgen 0.52 / 0.28 / **0.43**。
**前端由「比 gcc 慢 8.4×」变为 1.33×，且快过 clang；tccgen 端到端还快过 gcc。**
累计路径：`-O0` 起点 4.58 s → `-O2` 2.44 s → `__func__` 惰性 −42% → 序号化命名 −13% →
属性/内建按 id −0.7/−2.1% → **桶遍历 −68.6%** → 0.48–0.65 s。

下一个目标：**IR 往返**（端到端 4.09 s 中前端仅 0.65 s，其余约 3.4 s 是「打印 IR → clang 重解析 →
汇编」，而 clang 自己做同样后端工作只用约 1.4 s），属驱动架构改动，先量后动；
之后重新取一份前端 profile（`declspecs` 的 32 µs 已随桶遍历消失）。

### R131 `find_ident` 的名字链表扫描：sqlite3 一次编译 7,970 万步 —— ✅ 已定量（修法已定，未改）

按用户指出的热路径（`case TK_IDENT` → `find_typedef` → `find_ident`）加了计数器（`-O2`）：

| | sqlite3.c | tccgen.c |
|---|---|---|
| `is_typename` / `find_typedef` / `find_ident` 调用 | 224,070 / 190,417 / 211,052 | 30,982 / 20,706 / 27,527 |
| `find_ident` 未命中 | 30,901 | 11,906 |
| 作用域层数 / **名字链表步数** | 716,787 / **79,715,113** | 84,087 / **25,365,768** |
| 平均每次调用 | **378 步** | 921 步 |
| 该次总时间 | 2.83 s | 0.42 s |

`find_ident()` 先用哈希桶找一个同名条目，**然后仍从 `sc->vars`（或该条目）顺着整条
作用域名字链线性扫描**；未命中时走完整条链。`is_typename()` 在每个有歧义的标识符处被调用，
绝大多数答案是「不是类型名」。80M 步（指针追逐 + id 比较，多半带 cache miss）
≈ 0.1–0.8 s，即 sqlite3 前端的 **4–28%** —— 这正是 §R125 里 `declspecs` 每次 32 µs 的真身。

**用户方案（类型名单列一张表）可行且有效**，并顺带发现更根本的一处：

1. `is_typename()` 快速否定：维护「当前作用域链中的 typedef 名 id 集合」（作用域退出时撤销，
   需在本层记一份插入过的 id 列表），集合里没有就立即答「不是类型名」，整条链与 vars 扫描全省。
2. **`find_ident()` 的第二段扫描本身多余**：哈希桶 `hnext` 链已含同名全部声明，
   按「最新在前」遍历桶链即可，不必走 `sc->vars`；这一改覆盖所有调用者，收益比 ①更大，
   但要核对 `is_extern` 分支依赖的「最近一次声明」次序（桶链顺序正好满足）。

验收计划：① 与 ② 分别 A/B（`-O2`，同一次运行内对照）+ `make test`（325/0）+ bootstrap 逐字节 +
tcctests 106/0。本节只做定量与定位，未改代码。

### R130 全仓 `strcmp`/`strncmp`/`memcmp` 清点 —— ✅ 已审计：热路径已是 id 比较，余者为冷或必须

按用户提出的「所有 `strcmp`/`strncmp` 都可疑」，清点 `src/` 共 **139 处**：

| 文件 | 处数 | 判定 |
|---|---|---|
| `main.c` | 97 | 命令行解析，每进程一次 —— 冷 |
| `parser.c` | 30 | 约 25 处是 `strcmp(a->info->name, …)`（表行 vs 字面量），按属性出现次数跑 —— 冷；其余是 `"llvm."` 前缀与 asm 操作数表 |
| `attr.c` | 3 | `ns_of()` 的 `"gnu"`/`"clang"`；整类在 §R129 量到 ≈0.7% |
| `util.c` | 4 | **interning 表自己的 `memcmp`（哈希命中必须字节确认）**、日志级别、`Int128` 比较（非字符串） |
| `dumpir.c` | 2 | IR 打印器按名字查表 —— 调试路径 |
| `lexer.c` / `irgen.c` / `preprocess.c` | 1 / 1 / 1 | BOM；`"llvm."` 常量前缀 ×2 |

**热路径核对结果（这是本节的重点）**：关键字比较是 `tok->id == kw[i].id`（整数，`lexer.c`
全文只剩 BOM 一处 `memcmp`）；内建函数自 §R129 起快速失败 + id 表；属性查表自 §R129 起按 id；
作用域/宏/字符串字面量走 hashmap 与 interned id。即**用户指出的方向对，但这一类现在
要么冷、要么必须（哈希确认不能省字节比较）**，与 §R129 的独立测量一致。

可选清洁（收益≈0，未做）：`attrs[]` 行给枚举/指针身份以替换那 ~25 处名字比较；`ns_of()` 传枚举；
`irgen` 的 `"llvm."` 前缀在表初始化时预算 flag。

### R129 `attr_lookup` 按 id 比较 + `is_builtin_fn` 两道字符快速失败 —— ✅ 已落地，收益很小（−0.7% / −2.1%）

用户指出两处可疑点，都改了，也量了：

1. **`attr_lookup()`**：原来对 30 行每一行做 `strlen()` + `strncmp()`；现在每行一个 interned id
   比较（`attr_ids[]` 惰性填充）。`__name__` 归一化**保留**（glibc 头文件就这么写），但只在查表前
   `intern()` 一次 —— 多一个表项，内存不在这一处。
2. **`is_builtin_fn()`**：`s[0] != '_' || (s[1] != '_' && 非大写)` 直接返回，跳过
   30 + 29 + 6 = 65 行的扫描。它在 `primary()` 里对每个表达式里的每个标识符调用。
   已核对三张表 65 个名字**全部**以 `__` / `_大写` 开头（`check_builtin_rows()` 的构建期校验仍在）。

**A/B（都 `-O2`，同一脚本背靠背，取四次最快）**：sqlite3 `-fsyntax-only` 2.77 → 2.75 s（**−0.7%**）；
tccgen 0.46 → 0.45（**−2.1%**）；lua 0.05 → 0.05（0）。
验收：`make test` exit 0（conformance 325 / 0、c2y 101 / 0）、bootstrap **逐字节相同** + 21/21 对象、
tcctests 106 ok / 0 failed —— 改动免费、无回归，但**不解决前端**。

**顺带排除一个嫌疑**：`declspecs` 的 1.32 s self 里**不含** `attr_lookup`（`attr.c` 是独立编译单元，
不会被内联，gprof 把它的时间算在自己名下，而它很小）。`declspecs` 每次 32 µs 另有来源，
下一轮从 `is_typename` / `find_typedef` / 类型说明符处理循环入手。

### R128 序号化命名 + `insert_ty` 提前退出 —— ✅ 已落地（sqlite3 前端再 −13.3%）

§R127b/§R127c 定下的两条一起实现，`src/parser.c` 在惰性 `__func__` 之上再 +39/−7：

1. **`generated_name(prefix, seq)`**：每个前缀一个序号 —— 第一个保持裸名，其后 `.1`、`.2`…
   （与旧 `new_unique_varname()` 的输出**逐字符相同**，但不再遍历 `globals`）。
   `.str` 与 `.compoundliteral` 改走它；`new_unique_varname()` 只留给块内 `static` 局部。
2. **`insert_ty()` 提前退出**：不再数完整条 `types` 链，取链表上**第一个**同 id 的条目
   （链表新的在前，故即最近一次），从其名字后缀读出序号继续（用户提的做法）。
   没有同名者时仍要走到链尾 —— 那种情况本来就没有编号可续，且类型数远小于全局数。

**验收**：`make test` exit 0（conformance **325 / 0**、c2y 101 / 0）；bootstrap
**cxx2 = cxx3 = cxx4 逐字节相同**、21/21 目标文件；`doc/tcctests.sh` 106 ok / 0 failed；
A/B（都 `-O2`，同一次运行）：sqlite3 `-fsyntax-only` 2.48 → **2.15 s（−13.3%）**、
tccgen 0.47 → 0.45（−4.2%）、lua 0.06 → 0.05（−16.6%）、`-c` 各行在噪声内。
**输出逐字节不变**：两个字面量 + `__func__` + 去重的用例，`-S` 输出与改前完全相同。

**累计**（同机、`-O2`）：sqlite3 前端 **4.30 s → 2.15 s（−50%）**，两处改动均不改变输出。
下一步回到 §R125 的清单：`declspecs`（41%，需先在其内部插桩定位）→ IR 往返（sqlite ≈2.7 s）
→ 区域分配器降 349 MB 峰值。

### R127c 匿名记录 / 匿名复合字面量能否也按序号编号？—— ✅ 评估结论：可行，且大半已在代码里

用户提案：匿名 struct/union 与匿名复合字面量永不可能被名字引用 → 不压作用域、建 id 为空的 var、
打印时按序号给名，省掉遍历 / 取名 / 格式化 / 插入；有名字的类型与 static 同名变量仍保留 `.N`。

**逐条对照代码，结论是「提案即现状」加「一处例外」**：

| 类别 | 今天的做法 | 结论 |
|---|---|---|
| 匿名记录（无名 `struct {…}`、编译器自建记录） | `insert_ty()`：`format("%s.anon.%d", kind, ++anon_seq)` | **已经是按序号** |
| 重名类型 | `format("%s.%s.%d", kind, str(ty->id), i)` | 与提案一致（保留 `.N`） |
| 编译器临时量、**块作用域**匿名复合字面量 | `new_lvar(id_anon, …)`，`id_anon = intern("", 0)`，**不 push 作用域** | **已经是 id 为空** |
| **文件作用域 / `static` 复合字面量** | `new_unique_varname(intern(".compoundliteral", 16))`（4429） | 唯一例外 |
| 字符串字面量 | `new_unique_varname(intern(".str", 4))` | 即 §R127b 的 92% |
| 块内 `static` 局部（用户可见名） | `format("%s.%s", cur_fn->id, str(id))` + 查重 | 保留 `.N`（提案亦如此） |

**收益**：`new_unique_varname` 的 0.348 s 中 `.str` 占 1,381/1,503（≈0.32 s），
文件作用域复合字面量等约 120 次（≈0.03 s，1%）。匿名记录/临时量**本来就不走取名路径**，
所以这一侧没有可省的开销；剩下要做的与 §R127b 是同一件事（编译器生成的符号按序号打印），
合计仍是 sqlite3 前端 **≈ −14%**。

**实现必须遵守的三条（都来自代码里的教训）**：
① `insert_ty()` 的注释记录了先例——匿名记录早期两种情况各自计数、都从 `anon.1` 开始，
于是两个无关结构都叫 `%struct.anon.1`，LLVM 以 redefinition 拒绝整个模块
（`libavcodec/jpegxl_parser.c`）→ **每种前缀只能有一个全局序号**；
② 序号分配在**内容去重后的对象**上，相同字面量仍共享一个全局（否则 IR 变大）；
③ 打印形状保持 `.str.<i>` / `.compoundliteral.<i>`，不动符号绑定。
另记一处同类隐患：`insert_ty()` 自己也在对 `types` 做线性扫描，改成每个 id 一个计数即可（同 ①）。

### R127 `__func__` 字符串惰性化 —— ✅ 已落地（sqlite3 前端 −42%）

§R126 定下的修法已实现，`src/parser.c` 净增 37 行 / 删 5 行：

- 9345 起：三个名字（`__func__` / `__FUNCTION__` / `__PRETTY_FUNCTION__`）照旧各推一条命名空间条目，
  但 `->var` 留 NULL；函数名 id 与「本次函数已建的那个字符串」存在两个解析器级变量里
  （嵌套函数保存/恢复，语义与原来一样是**一个对象**）。
- `primary()` 解析到这三个名字之一时按需建：`cur_func_strlit` 缓存保证三个拼写仍解析到同一个 Sym
  （实测 `__func__ == __FUNCTION__ == __PRETTY_FUNCTION__` 为真，与改动前一致）。
- 副作用：不被引用的字符串变量不再出现在 `globals` / `-ast-dump` 里。

**验收**（与 git HEAD 的 parser.c 在**同一次运行内**对比，两者都 `-O2`）：

| 负载 | 模式 | 改前 | 改后 | 变化 |
|---|---|---|---|---|
| sqlite3.c | `-fsyntax-only` | 4.30 s | **2.49 s** | **−42.0%** |
| sqlite3.c | `-c` | 7.90 s | **5.46 s** | **−30.8%** |
| tccgen.c | `-fsyntax-only` | 0.42 s | 0.39 s | −7.1% |
| tccgen.c | `-c` | 0.73 s | 0.66 s | −9.5% |

`make test` exit 0（conformance **325 / 0**、c2y 101 / 0）；bootstrap **cxx2 = cxx3 = cxx4 逐字节相同**、
21/21 目标文件；`doc/tcctests.sh` 106 ok / 0 failed；`__func__`/两个 GNU 拼写/同一性手工用例通过。

### R127b 字符串「内容池分家」vs「名字按序号打印」：分头量了 —— ✅ 已定量

按用户的两条思路分别量（`intern` 分三类计数 + `new_unique_varname` 的扫描步数/耗时，
sqlite3.c `-O2`，详见 `doc/perf.md` §8）：

| | 调用 | 新建 | 桶内探测 | self |
|---|---|---|---|---|
| 标识符 | 514,916 | 24,522 | 1,999,164 | 0.070 s |
| 字面量内容 | 2,145 | 1,301 | 13,889 | 0.001 s |
| `.str.N` 名字 | 1,381 | 1,381 | 9,102 | 0.002 s |
| **`new_unique_varname`** | 1,503 | | **遍历 globals 5,183,142 步** | **0.348 s** |

- **内容池分家：不划算**（反例）。字面量内容只占全表探测的 0.7%、0.001 s；贵的是标识符自己
  （51 万次调用）。改 id 空间 + 两张表一致性不值得。
- **名字按序号打印：值得**，目标正是那 **0.348 s（前端 14%）**：`new_unique_varname` 为每个
  字符串线性遍历 `globals` 找同名者（平均每次 3,448 个条目），再 `format` + `intern`。
  字符串池按解析顺序编号后，唯一性由序号天然保证、无需 format/intern，
  打印时直接给 `.str.<index>`（保留前导点，符号形状与今天一致）。约 20 行，预期 sqlite3 前端再 −14%。

后续顺序：① 名字按序号（−14%，小改动）→ ② `declspecs`（41%，还需先在其内部插桩定位）→
③ IR 往返（sqlite 上约 2.7 s）→ ④ 区域分配器降 349 MB 峰值。

### R126 `__func__` 的字符串是每个函数定义无条件建的 —— ✅ 已定量（修法已定，未改代码）

承接 §R125 的热点：`new_string_literal` self 0.82 s / 5,113 次。**根因已确认**，
且与用户提的假设一致：`src/parser.c:9345-9351` 每个函数定义都会推入 `__func__` /
`__FUNCTION__` / `__PRETTY_FUNCTION__` 三个命名空间条目，**并立刻**
`new_string_literal(var->id, fn_name)` 建出那个字符串字面量，与函数体里是否用它无关。

实测（`-O2`；两个调用点加计数、函数内加计时，再加一个跳过该调用的反事实开关
`CXX_NO_FUNC_STR=1`；背靠背比较）：

| sqlite3.c | 现状 | `__func__` 惰性 |
|---|---|---|
| 真字面量 / 来自 `__func__` 的调用 | 2,503 / **2,610（51%）** | 2,503 / 0 |
| 表中新建条目 | 3,996 | **1,391** |
| `new_string_literal` 内部耗时 | **1.572 s** | **0.260 s（−83%）** |
| **`parse`** | **4.472 s** | **2.565 s（−43%）** |

sqlite3.c 中 `__func__`/`__FUNCTION__` 出现 **0** 次 —— 2,610 次建表纯属白做。
tccgen.c 同形小一号（228/469 来自 `__func__`，新建 412 → 184，内部 0.032 → 0.001 s）。

**修法（~15 行，无语义变化，未实施）**：9347-9351 只推入命名空间条目、`->var` 留 NULL；
解析函数体前把函数名 id 与 `fn_name` 类型存一份；`primary()` 解析到 `__func__` 时按需建。
标准说 `__func__` 行为「如同声明」，惰性不可观测；副作用是不被引用的字符串变量不再出现在
globals 与 `-ast-dump` 里（这本就更合理）。预期：sqlite3.c 前端 parse 阶段 −43%，
折算到 §R125 未经插桩的 2.44 s 前端约 **−0.9 s**（前端对 gcc 由 ~4.8× 降到 ~3×）。

**另一件独立的事**：惰性化之后 `new_string_literal` 仍有 0.26 s / 2,503 次 ≈ 104 µs 每次，
其中 `__func__` 那 2,610 次约 500 µs 每次（几乎每次都走「新建」这条贵路径，函数名互不相同、
去重不命中）。新建一个 Sym 不该要 0.1–0.5 ms —— 这是**下一个**要查的目标，与本次假设无关。

（量测环境提醒：同一台机器上绝对秒数可波动 2×，只有同一次运行内的背靠背比较有效。）

### R125 前端瓶颈定位：parse 85%，其中 declspecs + new_string_literal 占 67% —— ✅ 已定位

工具链的现实（都已试过，记下来省得重试）：`perf` 在这台 WSL2 上是残缺安装（提示装
`linux-tools-standard-WSL2`）且没有 PMU；`valgrind --tool=callgrind` 下 cxx 只跑 ~17 万条指令就
退出；`gdb -p` 被 `yama/ptrace_scope=1` 挡在外面（只能 attach 子进程）。
**能用的是 gprof** —— 两个坑：cxx 驱动会 `fork+exec` 出 `cxx -cc1` 子进程做前端，所以 profile 属于
子进程（用 `GMON_OUT_PREFIX` 让每个进程各写一份 `gmon.out`，取最大的那份）；clang 的 `-pg` 只装
`mcount`，直方图采样只在那份子进程文件里。

分段计时（`main.c` 临时插桩，`CXX_TIME=1`，量完还原；sqlite3.c `-fsyntax-only`，`-O2`）：

| 阶段 | 秒 | 占比 |
|---|---|---|
| tokenize | 0.13–0.17 | ~6% |
| preprocess | 0.13–0.20 | ~7% |
| filter + join | 0.02 | 1% |
| **parse** | **2.08–2.14** | **~85%** |
| fold_ast | 0.018 | 1% |

热点函数（gprof，子进程；self 秒 / 占前端 / 调用次数）：

| 函数 | self | 占比 | 次数 | 每次 |
|---|---|---|---|---|
| **`declspecs`** | **1.32 s** | **41.5%** | 40,842（+2,736 递归） | ~32 µs |
| **`new_string_literal`** | **0.82 s** | **25.8%** | 5,113 | ~160 µs |
| `compound_stmt2` / `init_decl_list` / `stmt` / `primary` | 0.01–0.02 | 29.7 / 25.4 / 20.8 / 13.9%（均含子） | 15k / 8.5k / 48k / 192k | |
| `tokenize` / `preprocess` | 0.07 / 0.01 | 4.3% / 12.4%（含子） | 3,055 / 1 | |

`parse` 自身 inclusive 86.2%，与分段计时互相印证。两个函数的**每次调用成本比它们该有的高
一到两个数量级**，说明里面有随输入规模增长的隐藏工作（哈希链/重哈希、逐字符循环、重复构造类型），
不是「函数写得慢」；`-O2` 内联使得「self」包含被内联的被调者，这是 gprof 的分辨率上限。

**构建配置是最大的一条**：默认 `make` 是 `DEBUG=1`（`-g -O0`），而 `make DEBUG=0` 是
`-O2 -DNDEBUG`，实测前端快 **1.7–2×**、内存不变（sqlite fso 4.17 → 2.44 s，tokenize+preprocess
0.69 → 0.34 s，tccgen 0.40 → 0.32 s，`-c` 5.93 → 5.14 s）。§R124 的基准是拿 `-O0` 二进制测的，
换算到 `-O2` 后「前端比 gcc 慢 8.4×」约为 4–5×。

后续（按性价比）：① 用 `-O2` 构建（零代码改动）→ ② 攻 `declspecs` / `new_string_literal`
（gprof + 函数内插桩，找那 32 µs / 160 µs 花在哪）→ ③ 端到端的 IR 往返（sqlite 上约 2.7 s，
驱动架构改动，先量后动）→ ④ 内存 349 MB（`vnew`/`emalloc` 从不释放，改区域分配器）。
**不要先动**：预处理（~13%）、filter/join（1%）、fold_ast（1%）。
详细数据与脚本见 `doc/perf.md` §5–§6。

### R124 速度与内存定量评估（对 gcc / clang，真实项目源码）—— ✅ 已测

工具与数据：`doc/speed.sh`（可复现的测量脚本）、`doc/perf.md`（报告）、`doc/perf-results.txt`
（原始输出）、`doc/perf-summary.sh`（几何平均等汇总）。负载是 `~/rw` 下真实项目的翻译单元，
命令行取自它们自己的 `make -n`（树已构建时用 `doc/realworld.sh` 的 `-B` / `-W <source>` 回退），
只统一 `-O0 -w` 并去掉 `-g`/`-M*`/`-std=`；cxx 不认识的开关由探针探测后剔除并打印，三家同一份命令行。

| 指标（8 个负载，几何平均） | cxx/gcc | cxx/clang | 最差项 |
|---|---|---|---|
| 前端 `-fsyntax-only` 时间 | **8.41×** | **4.28×** | cpython-compile 14.92× / cpython-pickle 8.65× |
| 前端内存（最大 RSS） | **3.05×** | **1.13×** | zlib-deflate 4.24× |
| 端到端 `-c` 时间 | **2.46×** | **4.34×** | cpython-compile 10.33× |
| 端到端内存 | **1.65×** | **1.08×** | zlib-deflate 2.62× |

关键观察：

1. **瓶颈全在前端，且随规模放大**：sqlite 的 25 万行，前端吞吐 cxx **5.5 万行/秒**、gcc 49 万、clang 29 万；
   小文件只差 3–5 倍，cpython 的重 TU 差 12–15 倍 —— 是「每行都要走」的路径，不是固定开销。
2. **内存是 clang 量级**（cxx/clang 1.13×），对 gcc 3.05×；峰值 sqlite 前端 **349 MB**（gcc 84、clang 125）。
3. **端到端反而接近**（对 gcc 2.46×），因为后端就是 clang 的；把两个模式相减可分离出 **IR 往返的代价**：
   sqlite 上 cxx 的后端部分是 5.26 s，clang 从 IR 到目标文件只用 1.98 s。
4. **固定开销约 40 ms**（最小 TU：cxx 0.05 s / clang 0.03 / gcc 0.01）。
5. **cxx 拒绝 cpython 构建里的 `-fno-strict-overflow` 与 `-fvisibility=hidden`**（两家都接受），
   探针剔除后照测；这两个开关决定「项目能否原样编译」，其中 `-fvisibility=hidden` 值得优先补。

> **构建配置的更正（§R125 量出）**：本节所有数字是拿默认 `make`（`DEBUG=1`，即 `-g -O0`）
> 的二进制度的，而 gcc/clang 是发行版的 `-O2` 构建。用 `make DEBUG=0`（`-O2 -DNDEBUG`）重建后
> 前端快 1.7–2×、内存不变 —— 换算后上表的「前端 8.41×」约为 **4–5×**。比值之外的结论不变。

后续（按性价比，未做）：补 `-fvisibility=*` 一类开关的接受 → 给前端做一次 profile（词法/记号分配、
宏展开复制、`intern` 哈希、349 MB 对应的长期结构）→ 再谈 IR 往返（改成库接口是驱动架构的改变，先量后动）。

### R123 `__has_feature` / `__has_extension`：特性表 + 两个内建宏 —— ✅ 已完成

来源是 §R217 一带记录的待办：cxx 既没有 `__has_feature`，`__has_extension` 也只是个「永远答 0」
的 `#if` 运算符，于是 clang 自己头文件里的 `#if !defined(__STDDEF_H) || __has_feature(modules)`
被读成 `0 ( modules )`，报 `called object ‘0’ is not a function or function pointer`。

**先量两家的词汇表**（`-std=c2y`，这是 cxx 的目标模式；gcc 没有 `__has_feature`，参照只有 clang）：

| 名字 | clang `__has_feature` | clang `__has_extension` | cxx 是否真有该特性 |
|---|---|---|---|
| `c_alignas`、`c_alignof`、`c_atomic`、`c_generic_selections`、`c_static_assert`、`c_thread_local`、`c_fixed_enum`、`c_countof`、`enumerator_attributes` | 1 | 1 | 有 |
| `c_attributes`（`[[…]]`）、`gnu_asm` | **0** | **1** | 有（clang 把这两个归为扩展） |
| `modules`、`statement_expr`、`c_embed`、`c_nullptr`、`c_float16`、`c_complex`、各 `cxx_*`、各 sanitizer 名 | 0 | 0 | cxx 没有（`_Complex` 属长期不做） |

两条语义要点，都是从实测拿到的：**未知名字答 0 且不诊断**（clang 如此）；**参数不是标识符时报错**
（clang：`builtin feature check macro requires a parenthesized identifier`）。

**实现**（`src/preprocess.c`，一行表项 = 一个特性）：

```c
typedef struct { char *name; uint32_t id; bool is_extension; } CFeature;
static CFeature c_features[] = { {"c_alignas", 0, false}, … {"c_attributes", 0, true}, {"gnu_asm", 0, true} };
```

`__has_feature(x)`：命中且**不是**扩展 → 1；`__has_extension(x)`：命中即可（标准特性或扩展）→ 1；
其余 0。两者都注册成**内建宏**（`add_builtin`，与 `__has_builtin` 同路），而不是只在 `#if` 里求值的
运算符 —— 因为 `#ifdef __has_feature` 必须为真，头文件正是用这个来保护自己的用法（`__building_module`
仍走旧的「恒 0」路径，cxx 不建模块）。

顺带把「名字后面没有括号」的诊断改成 clang 的样子：`missing ‘(’ after ‘__has_feature’`
（原来报 `expected ‘(’ before ‘;’`，看不出是哪个宏）。

**验收**：

| 项目 | 结果 |
|---|---|
| 与 clang 对照 | 13 行行为逐行相同：两个 `#ifdef`、表内特性的两种问法、未知名字答 0、`#if` 之外当宏用（`enum { x = __has_feature(c_atomic) }`）、`#define HAS(x) __has_feature(x)` 的间接用法、一行两个、以及原来那行 `!defined(__STDDEF_H) \|\| __has_feature(modules)` |
| 原来的阻塞点 | clang 资源目录的 `stddef.h`（正文就用那个 guard）现在**能编译**；`stdatomic.h`、`immintrin.h`、`stdalign.h` 预处理也都通过 |
| `make test` | **exit 0**；conformance **323 → 325 / 0 gap**（新增两条；既有的「`__has_extension` 恒 0」断言按新行为改写为「`gnu_asm` 应答 1」）、c2y 101 / 0 |
| bootstrap | **cxx2 = cxx3 = cxx4 逐字节相同**，21/21 目标文件 |
| `doc/tcctests.sh` | 106 ok / 0 failed |
| `clang-format-21 --dry-run --Werror` | 通过 |

**加一个特性的做法**：`c_features[]` 加一行，其余不动 —— 但**只有 cxx 真的支持**才可以答 1：
这张表就是「本编译器能力」的声明，答错会让头文件走错分支。

### R122 6.7.6.2p1：数组派生的限定符与 `static` 只许出现在参数的最外层派生 —— ✅ 已修

来源是 §R120 限定符位置审计里**只记录未修**的那一处；§0 的当前阶段是 debug（只修已有功能的
bug，不扩功能），而这一条正是 bug：cxx **接受**两家都拒绝的写法。

| 形状 | 修前 cxx | 修后 cxx | gcc | clang |
|---|---|---|---|---|
| `void f(int a[3][const 5]);` | accept ✗ | **`type qualifier used in non-outermost array type derivation`** ✓ | 拒 | 同句 |
| `void f(int a[3][static 5]);` | accept ✗ | **`‘static’ used in non-outermost array type derivation`** ✓ | 拒 | 同句 |
| `void f(int a[const 3][const 5]);` | accept ✗ | 同第一条 ✓ | 拒 | 拒 |
| `void f(int (*p)[static 5]);` | accept ✗ | 同第二条 ✓ | 拒 | 拒 |
| `void f(int (*p)[const 5]);` | accept ✗ | 同第一条 ✓ | 拒 | 拒 |
| `void f(int a[][const 5]);`、`int a[n][const 5]` | accept ✗ | 同第一条 ✓ | 拒 | 拒 |
| `void f(int a[const 5]);`、`[static 5]`、`[const 3][5]`、`[const static 5]`、`[restrict 5]` | accept ✓ | accept ✓ | 收 | 收 |
| `void f(int (a)[const 5]);`、`int (a[const 5])`（多余括号） | accept ✓ | accept ✓ | 收 | 收 |
| `void f(int *a[const 5]);`（数组**在外**、指针在内） | accept ✓ | accept ✓ | 收 | 收 |
| `void f(int (*p)[5]);`、`[3][*]`、`[static 3][5]` | 不变 | 不变 ✓ | — | — |

**实现**：两个解析器级标志（与既有 `static_init_ctx`、`gcc_atomic_args` 同风格，避免把参数一路
穿过四个声明符函数的二十多个调用点）：

- `param_outermost`：进入**每个参数**的声明符时置位，由该声明符的**第一个**数组派生消费；
- `in_nested_declr`：进入带括号的声明符时置位；若在其中解析到 `*`，就清掉 `param_outermost`
  —— `int (*p)[5]` 里指针在数组**外面**，所以那个数组不是最外层派生。

**两个坑**：

1. **带括号的声明符会被读两遍**（`declarator()` 先试读一遍求形状，再用外层类型重读一遍），
   第一遍就把标志消费掉了，于是 `int (a[const 5])` 被误拒 ✗（两家都收）。修法：试读前保存、
   重读前恢复。
2. **参数表自成一套**：`int (*g(int a[const 5]))[3]` 里内层函数的参数仍是最外层派生，所以
   `func_param()` 对每个参数重置两个标志（两家都收）。

**验收**：`make test` exit 0（conformance **319 → 323 / 0 gap**：3 个 `bad` + 1 个正向，正向覆盖
多余括号、数组套指针、`[3][*]`、嵌套函数声明符里的参数）、c2y 101 / 0；bootstrap **逐字节相同**；
`doc/tcctests.sh` 106 ok / 0 failed；`clang-format-21` 通过；17 个边界形状与两家逐行一致。

### R121 范围指示符 [a ... b]：求值次数、任意维与死初始化器消除 —— ✅ 已修


用户提问：gcc 明确说明 `[2 ... 5] = i++;` 只求值一次，检查 cxx 的求值次数；随后又问
`{[2 ... 5] = i++; [2 ... 5] = 6;}` 里那个 `i++` 是**被抛弃**还是**保留求值**。

#### （一）求值次数：已修

GCC 的规矩（Designated Initializers 一节）：范围指示符覆盖的每个元素拿到**同一个值**，
初始化表达式**只求值一次**。clang 不支持这个扩展（`cannot compile this GNU array range
designator extension yet`），参照只有 gcc。修前实测（`static int i; static int bump(void)
{ return ++i; }`）：

| 形状 | 修前 cxx | gcc | 修后 cxx |
|---|---|---|---|
| `int a[4] = { [0 ... 3] = bump() }` | i=1 ✓ | i=1 | i=1 ✓ |
| `int b[2][2] = { [0 ... 1][0 ... 1] = bump() }` | **i=2** ✗，`b[0][0]=1, b[1][1]=2` ✗ | i=1，`1,1` | **i=1，`1,1`** ✓ |
| `int b[2][2] = { [0 ... 1][1] = bump() }` | **i=2** ✗ | i=1 | i=1 ✓ |
| `struct S { int v; } s[3] = { [0 ... 2].v = bump() }` | **i=3** ✗ | i=1 | i=1 ✓ |
| `int c[2][2][2] = { [0 ... 1][0 ... 1][0 ... 1] = bump() }` | **i=4** ✗ | i=1 | i=1 ✓ |
| `int b[3][2] = { [0 ... 2][0 ... 1] = bump() }` | **i=3** ✗ | i=1 | i=1 ✓ |
| `struct P p[2] = { [0 ... 1] = (struct P){ bump(), 2 } }` | — | — | i=1 ✓ |

**根因**：`designation_range()` 把「只求值一次」实现成「第一个元素自己的 `expr` 非空」。
值一旦落在更深一层（`first->child[1]->expr`、`first->child[mem]…`），`first->expr` 是 NULL，
代码走回退分支**逐个元素重新解析**，副作用与值都重来。

**修法**：把「只求值一次」从「第一个元素的 `expr`」推广成「整棵子树的副作用」。

1. 解析一次到 `first`；
2. 按 `create_lvar_init()` 的发射顺序遍历子树，收集每个 `pre` 槽与每个叶子 `expr`
   （`collect_range_effects()`，上限 16 项，超出即回退到旧路径）；
3. 逐项上提到 `first->pre` 的链上：`pre` 是语句直接搬，叶子换成 `tmp = 值` 加「读 tmp」
   （`share_range_value()`）；
4. `begin+1 … end` 的元素拿到**同形状副本**（`copy_range_init()`：叶子读同一批临时量、
   `pre` 为空）。

实施中抓到并修掉的两个坑（都是**顺序/存活**问题，不是求值次数问题）：

- **副本会丢掉元素原有的 `pre`**：`{[1 ... 5] = 9, [6 ... 10] = elt, [4 ... 7] = elt + 1}`
  里第三个范围整片重写元素 6 的 Initializer，把第二个范围挂在它 `pre` 上的临时量 store
  一起丢了，于是 `s[8 ... 10]` 读到未初始化的临时量得 0（`conformance.sh` 既有的
  「range designator's single evaluation」用例当场抓住）。修法：副本保留目标元素原有的
  `pre`（那是在它被初始化处运行的更早的效果），只丢它自己的值。
- **顺序必须提在第一个被覆盖元素的位置**，不能提到范围所在数组的 `pre`：否则
  `int a[6] = { [0] = x++, [2 ... 4] = x++ };` 会先算范围 —— 那是新 bug。已用 gcc 逐例对照
  （元素在范围前/后、两个范围、范围后单元素覆盖、嵌套花括号行内范围、静态/文件作用域的常量
  范围、单元素范围、复合字面量）确认顺序与取值一致。

静态初始化器（`static_init_ctx`）不受影响：那里全是常量，本来就没有临时量。

#### （二）被**完全**覆盖的范围初始化器：已修（求值次数 + 死初始化器消除）

用户问的那一行：`int a[10] = { [2 ... 5] = i++; [2 ... 5] = 6; }` —— 最终值两边都是 `6`，
但那个 `i++` 是否被求值，修前三家不同：

| 形状 | 修前 cxx | gcc | clang | 修后 cxx |
|---|---|---|---|---|
| 普通 `{ [0] = i++, [0] = 6 }`（元素被完全覆盖） | i=0（抛弃） | i=0 | i=0 | i=0 ✓ |
| 普通 `{ [0] = 6, [0] = i++ }` | i=1 | i=1 | i=1 | i=1 ✓ |
| **范围** `{ [2 ... 5] = i++, [2 ... 5] = 6 }` | **i=1（保留求值）** ✗ | i=0（抛弃） | i=0 | **i=0** ✓ |
| **范围** `{ [2 ... 5] = i++, [3] = 6 }`（部分存活） | i=1 ✓ | i=1 | 不支持 | i=1 ✓ |
| **范围** `{ [0 ... 3] = i++, [0 ... 3] = i++ }` | **i=2** ✗ | i=1 | 不支持 | **i=1** ✓ |
| **范围** `{ [0 ... 1].v = i++, [0 ... 1].v = 7 }` | **i=1** ✗ | i=0 | 不支持 | **i=0** ✓ |

规律：**一个初始化器当且仅当它至少有一个目标元素存活时才求值**（死初始化器消除）。普通指定
初始化器三条路径都满足；cxx 自己也满足（后面的设计符**替换**那个元素的 Initializer，副作用随之
消失）；只有**范围**路径不满足 —— 范围把临时量 store 挂在「第一个被覆盖元素」的 `pre` 上，
后面的设计符即便把该范围**所有**元素都覆盖掉，那个 store 仍在（§（一）为了修 `s[8 ... 10]`
还特意保留 `pre`，正是这一点让它更明显）。

**修法**：在对象初始化器解析完成后（`initializer()` 里 `initializer2()` 之后）做一次死 store
消除。范围建的临时量都是匿名 `Sym`，只有范围机制会读它们，且读法就是 `ND_VAR` 及其
`ND_LVTOR`/`ND_IMCAST`/`ND_EXCAST` 外壳 —— 所以「这棵树里还有谁读这个临时量」是一个**窄**判定：

1. `pre_stmts()` 把一条 `pre` 链摊平成有序语句表（超过 16 条就不动，宁可不剪）；
2. `init_reads()` 在整棵树里找该临时量的读法（数组按元素、结构体/联合按成员；深度超过 8 层即
   判定「有人在读」，保守保留）；
3. 没人读的 store（连同它的副作用表达式）从链上摘掉并重建，重复几趟以收拾「只喂给另一个被摘
   store」的那些。

**踩到的坑**：第一版把「不认识的表达式形状」当成「可能读」✗，于是 `= 6`（一个 `ND_NUM`）被
判成读，剪枝从不触发。正确判据相反：匿名临时量**不可能**出现在程序自己写的表达式里，只有范围
机制放置读法，所以不认识的形状就是**不读**。

#### （三）更多维：`[a ... b][c ... d] = expr` 到任意深度

用户的追问：gcc 是否支持 `[2 ... 5][1 ... 3] = assign_expr`，甚至更多维？——**支持，任意深度**。
实测（`static int n; int f(void){ return ++n; }`，值写进所有元素）：gcc 在 2D…8D 全部 `n=1`，
且每个元素都拿到同一个值；`= assign_expr` 也照收（赋值表达式、逗号表达式、条件表达式、复合字面量
都测过）。

修完前两节后 cxx 只跟到 4D —— 5D 又退回「逐元素重解析」。根因在收集器的**计数**：范围把同一棵
子树交给它覆盖的每个元素，各元素的槽位地址却不相同，于是同一批副作用被按**元素个数**重复收集
（实测各层计数 1, 3, 5, 9, 17, 33 …），5D 就撞上 `RANGE_EFFECTS_MAX`。两处修正：

1. **同一槽位只算一次**（`effect_seen()`）——只解决「同一槽被多元素共享」这一半；
2. **读「本层或上层已经收下的临时量」的叶子不再上提**（`effect_defines()`）——这才是指数增长的
   来源：每一层都把下一层的读数再包一层临时量。判据必须看**共享的**计数：第一版把计数按子树切片
   传下去 ✗，叶子只能看到自己那一片，判据恒假 ✗，于是仍旧 1,3,5,9,17,33；改成调用方与被调方
   共用一个计数后，各层稳定为常数。

**踩到的坑（与上一条同源）**：第一版的「纯读」判据把任何 `ND_VAR` 都当成「不必上提」✗ —— 可
复合字面量 `(struct P){ bump(), 2 }` 也是匿名变量，于是它被当成纯读共享给每个元素，
`bump()` 每个元素跑一次（`range_order.sh` 的复合字面量一行当场抓到，且 5D 之下的计数探针全绿，
只有这一行变红）。正确判据是「读的是本遍历已经收下的临时量」——复合字面量的变量是在别处初始化
的，它的副作用照旧要上提。

最终：**2D … 8D 全部与 gcc 一致**（`n=1`，各元素取值相同），复合字面量、成员设计符、混合
单下标/范围、赋值/逗号/条件表达式值、嵌套花括号列表、范围后跟设计符等 12 种形状逐例一致。

#### （四）验收

| 项目 | 结果 |
|---|---|
| `make test` | **exit 0**；conformance **319 / 0 gap**（两条断言覆盖：二维/行+列/成员/三维/五维/复合字面量/部分覆盖，以及完全覆盖不求值）、c2y 101 / 0 |
| bootstrap | **cxx2 = cxx3 = cxx4 逐字节相同**，21/21 目标文件 |
| `doc/tcctests.sh` | 106 ok / 0 failed（含 tinycc `tests2/90_struct-init.c`） |
| `clang-format-21 --dry-run --Werror` | 通过 |
| 与 gcc 逐例对照 | 求值次数 8 例（1D…8D）、形状 12 例、顺序/覆盖 10 例、死初始化器 7 例，取值与副作用全部一致 |

### R120 语法文档改用 N3685 附录 A 的拼写 + 限定符位置审计（含一处新缺陷）—— ✅
 + 限定符位置审计（含一处新缺陷）—— ✅

#### （一）`doc/token.txt` 与 `doc/cfg.txt` 重写为标准拼写

记号不变（`::=`、`|`、终结符加引号），**产生式一条不多一条不少**，只把终结符/非终结符的拼写换成
N3685 附录 A 的形式（`TransUnit`→`translation-unit`、`DeclSpecs`→`declaration-specifiers`、
`DirDeclr`→`direct-declarator`、`AsExp`→`assignment-expression`、`CondExp`→`conditional-expression`、
`PrimExp`→`primary-expression`、`CompStmt`→`compound-statement`、`Ident`→`identifier`、
`strlit`→`string-literal`、`intlit`→`integer-literal`、`charlit`→`character-literal`、
`predef_constant`→`predefined-constant`、`ucn`→`universal-character-name` …）。C23 改了名的
两处照新名走：**`character-constant`→`character-literal`**、**`integer-constant`→`integer-literal`**
（`floating-constant`→`floating-literal` 同理）。

| 项 | 结果 |
|---|---|
| `token.txt` | 47 条产生式 → **47 条**（纯改名） |
| `cfg.txt` | 73 条 → **76 条**：+3 条是把原先**悬空**的非终结符补上定义 —— `constant-range-expression`（6.6.2）、`simple-declaration`（6.7.1）、`struct-or-union`（6.7.3.2）。原先 `Num`/`Str`/`UnaryOP` 三处引用也没有定义，按标准名接到 `constant`/`string-literal`/`unary-operator` |
| 笔误 | 标点表里 `'|='`（单引号）改为 `"|="` |
| 一致性 | `cfg.txt` 用到的名字现在**全部**有定义（或定义在 `token.txt` 里）：`uses but does not define: (none)` |
| 终结符 | 编码前缀/后缀补上引号（`("u8" | "u" | "U" | "L")`、`("ll" | "LL")`、`("wb" | "WB")`），与文件里其它终结符一致 |

#### （二）限定符位置审计：还有没有「指针不接受 `_Atomic`」那类遗漏？

方法同当初发现该缺陷时一样：把**标准允许某个 token 出现的每个位置**都写成探针，cxx / gcc / clang
三家一起过一遍（`-fsyntax-only`），只看三家不一致的行。共 45 个探针位置。

**结论：`_Atomic` 那一类遗漏已经没有了。** 九个位置全部接受，与两家一致：

| 位置 | cxx |
|---|---|
| `int * _Atomic p`、`int ** _Atomic p`、`int * _Atomic * p`、`int * _Atomic const p` | 接受 ✓ |
| 抽象声明符 `sizeof(int (* _Atomic)(void))`、`sizeof(int (* _Atomic)[3])` | 接受 ✓ |
| 原型参数 `void f(int a[_Atomic 5])`、`void f(int a[_Atomic static 5])` | 接受 ✓ |
| `struct S { _Atomic int a; }`、`typedef _Atomic int ai`、`_Atomic(T) *p`、`_Atomic struct S s` | 接受 ✓ |

同一轮探针抓到**一处新缺陷**（方向相反：标准禁止而 cxx 静默接受）：

| 缺陷 | 现象 | 依据 | 处置 |
|---|---|---|---|
| `restrict` 用在函数指针上 | `int (* restrict p)(void);` cxx 接受，**clang 与 gcc 都拒绝** | **6.7.4.1p2**：只有「所指类型是对象类型的指针」才可 restrict 限定，函数类型不是（标准自己的 `memcpy` 写 `void * restrict`，可见 `void` 按对象类型读） | **本轮已修**：`pointers()` 里加一条诊断 `pointer to function type may not be ‘restrict’ qualified`；`void * restrict` 仍旧接受（探针固化） |

**仍然保留、本轮不动的一处**（早前已记录的缺口，本轮换了个方向看清了它）：

| 缺口 | 现象 | 依据 |
|---|---|---|
| 非最外层数组派生的限定符与 `static` | `void f(int a[3][static 5]);`、`void f(int a[3][const 5]);`、`void f(int a[const 3][const 5]);` cxx **接受**，clang/gcc 都拒绝（clang：`type qualifier used in non-outermost array type derivation`） | **6.7.6.2p1**：「可选的类型限定符与关键字 `static` **只应**出现在带数组类型的函数参数的声明中，且**只在最外层**数组类型派生里」。cxx 现在只分 `is_param`（非参数已经在报 `… outside of function prototype`），缺的是「最外层」这一维；修法：把 `outermost` 从 `func_param()` 一路传进 `declarator()`/`decl_suffix()`/`array_dimensions()`，遇到带括号的嵌套声明符就清掉 |

**两处「不一致但 cxx 更标准」/「按设计不做」**，记录以免当成缺陷：

- `[[maybe_unused]] case 1:` —— cxx 接受，**clang 拒绝**（`attribute cannot be applied to a statement`）。附录 A 的 `label` 产生式带 `attribute-specifier-sequenceopt`，cxx 是对的，clang 还没跟上。
- `_Complex double x;`、`_Decimal32 x;` —— 计划 §0「明确不做（长期）」的两族。副作用值得知道：这两个拼写不是关键字，于是被当成标识符，诊断是 `a type specifier is required for all declarations`，看不出「本编译器不支持复数」。若要改善，只需把 `_Complex`/`_Imaginary`/`_Decimal32/64/128` 收进关键字表并报「not supported」（不改语法）。

#### （三）验收

| 项目 | 结果 |
|---|---|
| `make test` | **exit 0**；conformance **317 / 0 gap**（+2：函数指针 restrict 被拒、`void * restrict` 仍接受）、c2y 101 / 0 |
| bootstrap | **cxx2 = cxx3 = cxx4 逐字节相同**，21/21 目标文件 |
| `doc/tcctests.sh` | 106 ok / 0 failed |
| `clang-format-21 --dry-run --Werror` | 通过 |
| 语法文档 | `token.txt` 47 条、`cfg.txt` 76 条，两份都无悬空非终结符、无遗留旧拼写 |

### R119 A 类内建声明化收尾：6 行转声明式、两处既有缺陷、表可表达任意固定原型 —— ✅
：6 行转声明式、两处既有缺陷、表可表达任意固定原型 —— ✅

按 `doc/builtin-redesign.md` 把 A 类做完：把**能由固定 C 函数原型表达却仍留在 B 类**的 6 行搬走 ——
`__builtin_unreachable`、`__sync_synchronize`、`__builtin_memcpy`、`__builtin_memmove`、
`__builtin_memset`、`__builtin_memcmp`。表 census：**86 行 = 30 DECL + 56 SPECIAL**。

| 项 | 内容 |
|---|---|
| 表 | 新增「具名形参表」选择子 `BT_MEMCPY_ARGS`/`BT_MEMSET_ARGS`/`BT_MEMCMP_ARGS`（memcpy 一族形参类型不同：`void *`、`const void *`、`size_t`）。用选择子而不是给 `BuiltinDef` 加字段，保住「一行 = 一个内建」，其余各行位置初始化不动 |
| `void (void)` 两行 | irgen 按 id 降级：`__builtin_unreachable` 仍是 void 空操作；`__sync_synchronize` 发 `fence seq_cst` |
| memcpy 一族 | irgen 的 `gen_libcall()` 发库调用（`intrinsic` 里不是 `llvm.*` 的名字即它代表的库函数）；`declare_builtin()` 顺带把库函数声明进模块 —— 少了声明 LLVM 会拒绝该引用。`parse_mem_builtin()`（44 行）与其 4 个 case 删除 |
| B 类理由 | `builtin-redesign.md` §2.2 由 5 行扩成**逐族 11 行**（覆盖全部 56 行），判据补一条：结果须可为运行期值、实参在求值前不被折叠 |

**顺带抓到的两个既有缺陷**（不是本次引入，但都被本次暴露）：

| 缺陷 | 现象 | 修法 |
|---|---|---|
| 取内建地址产出 LLVM 拒绝的模块 | `&__builtin_bswap32` 一直「能编译」，模块里却引用了没有任何声明引入的符号：`use of undefined value '@__builtin_bswap32'` | 前端拒绝（`builtin functions must be directly called`，与 clang 同句）；§3.2 的「获得取址能力」一句据此更正 |
| 任何 `void *` 转换都误报 `discards qualifiers` | `type.c` 的 `is_assignable()` 里 `!agree` 一项对 void 对恒真：`char *`→`void *` 也报、`memcpy(dst, src, n)` 全报 | 只有**目标真的丢掉限定符**时才报；七个用例与 clang 逐点一致 |

**验证**：`make test` exit 0（conformance **315 / 0 gap**，c2y 101 / 0）；**bootstrap 逐字节相同**
（cxx2 = cxx3 = cxx4、21/21 目标文件）；`tcctests` 106 ok / 0 failed；跨目标 arm64 / rv64 / rv32
各 51 / 0。新增断言 11 条：`test/ir.sh` 4（库声明、库调用、memcmp 的 int、fence）、
`test/conformance.sh` 7（arity、const 源可编译可运行、四个限定符用例、取址被拒）。
两份拆分评估存档在 `doc/parser-split-eval.md`。

### R118 阶段解耦：用户可见检查全部前移到前端，`-fsyntax-only` 在折叠之后、irgen 之前返回 —— ✅
，`-fsyntax-only` 在折叠之后、irgen 之前返回 —— ✅

用户给定性：**irgen 设计上不该再对程序提要求，它只负责 IR 生成**；`-fsyntax-only` 应在折叠完成后、
irgen 之前返回 —— 这既是时间优化，也是阶段解耦。按此实施。

#### 三条检查的去向

| 原位置 | 去向 | 做法 |
|---|---|---|
| `irgen.c` `atomic_agg_bits`：`atomic aggregate larger than 8 bytes …` | **type.c `check_atomic_aggregate()`**，在“访问被写出来”的三处调用 | 内建的对象（`atomic_object()`）、对 `_Atomic` 聚合的赋值（`assign()` 的 `ND_AS`）、对它的读（`lvalue_convert()`）—— 正好覆盖此前探到的 6 个形状 |
| `irgen.c` `asm_ir_ty`：`‘asm’ operand of aggregate type is not supported` | 整函数搬到 **type.c `asm_operand_ir_type()`**；parser 在 asm 操作数循环里拒绝，irgen 仍用它取 piece 类型 | 一份实现两处用，前后端不会各写一份判断而漂移 |
| `irgen.c` `gen_addr` 的 `not a lvalue` | **改成 `fatal()`**（内部断言） | 14 个形状探针里没有一个能到达它；它本就不是“用户要求”，而是编译器自检 |
| `abi_lowering()` / `abi_lowered()` | irgen.c → **type.c**（`dumpir.c` 里那份重复的 `abi_lowering` 同时删掉） | 前端要用同一个答案，三处合成一份 |

结果：**`src/irgen.c` 与 `src/dumpir.c` 里已无任何 `error(`/`warning(`**，只剩 `fatal()`（编译器自检）——
“irgen 只专注 IR 生成”这条边界现在是可以被 `grep` 验证的事实。

#### `-fsyntax-only` 的位置

`cc1()` 里改为在 `fold_ast(prog)` 之后、`irgen(prog)` 之前返回（原来在 irgen 之后）。
之所以放在**折叠之后**而不是 parse 之后：那批警告（`-Wconstant-conversion`、`-Wfloat-conversion`、
`-Wliteral-conversion`、移位计数）出自折叠那趟，`-fsyntax-only -Wall -Werror` 不该因此安静下来。

#### 实测：两个模式现在报同一批前端诊断

| 用例 | `-fsyntax-only` | 普通编译 |
|---|---|---|
| C11 拼法、16 字节 `_Atomic` 聚合 | 报（前端） | 报（前端） |
| GCC 拼法 `__atomic_load`、16 字节 | 报（前端） | 报（前端） |
| asm 寄存器操作数是 16 字节记录 | 报（前端） | 报（前端） |
| asm 寄存器操作数是 8 字节记录 | 接受 | 接受（无假阳性） |
| 元素类型不完整 | 报（前端） | 报（前端） |
| `-Wshift-count-negative` 的警告 | **照报**（证明折叠那趟仍在跑） | 报 |
| `asm("" : "=i"(x))` —— LLVM 后端的约束检查 | 不报 | 报（`could not allocate output register for constraint 'i'`） |

最后一行是现在**唯一**落在前端之后的诊断：它由 LLVM 后端发出，`-fsyntax-only` 不走后端。
这条差异记在 §R116，属于结构性的（cxx 不重做后端的约束校验）。

#### 时间（sqlite3.c，9.5 MB，空载，三次取最好）

| 模式 | 时间 |
|---|---|
| `-E`（只预处理） | 0.70 s |
| **`-fsyntax-only`（预处理 + parse + 折叠）** | **2.41 s** |
| `-S -o /dev/null`（全链） | 4.57 s |

顺带更正 §R116 里那张阶段耗时表：它是在机器满载时测的，绝对数值偏大三倍左右；空载口径下结论不变
（parse 占前端大头，`fold_ast` + `irgen` 合起来约 1.5–2%，跳过后端才是大头）。

#### 验收

| 项目 | 结果 |
|---|---|
| `test/conformance.sh` | 306 → **309 / 0 gap**（+3：`-fsyntax-only` 报宽原子访问、报 asm 寄存器操作数、仍出折叠警告） |
| `make test` | **exit 0**（driver / error / ir / conformance / c2y） |
| bootstrap | cxx2 = cxx3 = cxx4 **逐字节相同**，21 个目标文件亦相同 |
| `grep -c 'error(\|warning(' src/irgen.c src/dumpir.c` | **0 / 0** |
| 原子 IR（`test/ir.sh` 的 4 条） | 不变：四种 GCC 拼法仍发 `load atomic`/`store atomic` |

### R117 真缺陷：GCC 拼法的原子 load/store 被降级成普通访问 —— ✅ 已修，>8 字节改为拒绝（缺口记录）

从 §R116 的“三条 irgen 检查能不能前移”探出来的：`atomic_object()` 对 GCC 拼法（`__atomic_load`、
`__atomic_load_n`、`__atomic_store`、`__atomic_store_n`）放行**普通对象**的地址，但降级原子性的判断
`is_atomic_ptr(addr)` 只看**pointee 有没有 `Q_ATOMIC`** —— 于是这些内建编出来的就是普通读写：

| 源码 | 修前 cxx 的 IR | gcc | clang |
|---|---|---|---|
| `__atomic_load_n(p, __ATOMIC_SEQ_CST)`（`long *p`） | `load i64, ptr %p` ✗ | `movq` + `lock`/栅栏 | `load atomic` |
| `__atomic_store_n(p, v, __ATOMIC_RELAXED)` | `store i64` ✗ | `xchgq` | `store atomic` |
| `__atomic_load(p, r, order)` / `__atomic_store(p, r, order)` | 同上，普通 copy ✗ | 同 | 同 |
| 同一对象经 C11 拼法（`_Atomic long *`） | `load atomic i64 … seq_cst` ✓ | — | — |

也就是说：**程序要求的原子性被静默丢掉了**（数据竞争），而 C11 拼法那条路是对的 —— 这正是最坏的一类
缺陷（错代码而不是报错）。根因是 parse 把 `mem_order` 标在节点上，而 `irgen.c` 的 `load()`/`store()`
只用 `is_atomic_ptr(addr)` 决定要不要发原子指令（`irgen.c:389`、`488`）。

**修法（`src/parser.c` 的 `atomic_object()`，一处）**：GCC 拼法取的是普通对象的地址，它要求的访问同样是原子的，
于是把这个 pointee 读成 C11 拼法本会要求的 `_Atomic` 类型 —— 套一层 `ND_EXCAST`（cast 自己的类型
`add_type()` 不会覆盖），指向 `type_qual(base, Q_ATOMIC)` 的指针。这样下面所有东西一次对齐：
原子降级、整块访问的宽度上限、对齐。

#### 连带的两件事

1. **>8 字节的整块原子访问现在被拒绝**（两个拼法一致）：`atomic aggregate larger than 8 bytes … is not supported`。
   gcc 走 `__atomic_load_16`（libatomic），clang 也接受；cxx 不调 libatomic，**拒绝而不是静默非原子** ——
   按 §0 记为缺口（新增断言把它钉住）。
2. **原来那条 conformance 断言其实固化了错误行为**：它用 16 字节的 `struct { int a; double d; }` 走
   `__atomic_load(&s, &t, …)`，修前靠“普通 copy”通过。已改成 8 字节的记录（值语义照测），
   另加一条“宽记录必须被拒”的断言。

#### 实测

| 项目 | 之前 | 现在 |
|---|---|---|
| `test/ir.sh` | — | **+4 条**：GCC 四种拼法的 IR 里必须出现 `load atomic`/`store atomic`（含 8 字节记录的整块访问） |
| `test/conformance.sh` | 305 / 0 gap | **306 / 0 gap**（改 1 条、加 1 条） |
| `make test` | exit 0 | exit 0（driver / error / ir / conformance / c2y 全绿） |

### R116 新功能：`-fsyntax-only` —— ✅ 与两家退出码/产物一致，多报 irgen 类诊断（已记录）

用户要求补上这个驱动选项。它此前是 §R115 里记下的一处驱动缺口（gcc/clang 都有，cxx 回
`unknown argument`），也是 `doc/crash-smoke.sh` 原本想用却用不了的选项。

#### 语义怎么定

cxx 只有一趟前端（`parse → fold_ast → irgen → dump_module`），没有独立代码生成阶段可跳。
所以这里的“syntax only”取**“跑完整趟前端、什么都不写”**：不写 `.ll`/`.s`/`.o`、不链接、`-o` 忽略。
这样它报的诊断是**普通编译的父集**，而退出码与产物与两家完全一致 —— 先用脚本把两家的行为量出来再对齐：

| 用例 | cxx | gcc | clang |
|---|---|---|---|
| `-fsyntax-only` 好文件 | rc 0，无产物 | rc 0，无产物 | rc 0，无产物 |
| `-fsyntax-only` 语法错 | rc 1，无产物 | rc 1，无产物 | rc 1，无产物 |
| `-fsyntax-only` 语义错（不完整元素类型） | rc 1 | rc 1 | rc 1 |
| `-fsyntax-only -o out.o` | rc 0，**不产生 out.o** | 同 | 同 |
| `-fsyntax-only -c` / `-S` | rc 0（静默忽略） | rc 0（静默） | rc 0 + “argument unused” 警告 |
| `-fsyntax-only -E` | **`-E` 生效**（预处理到 stdout） | 同 | 同（另给警告） |
| 多个输入 / 无输入 | rc 0 / rc 1 | 同 | 同 |

实现（三处，`src/main.c`）：解析 `-fsyntax-only` 存标志；驱动的 `.c` 分支在 `-E`/`-M` 之后、
`-S` 之前插一段“只跑 cc1、不给输出路径”；非 C 输入（`.s`/`.S`/`.o`/`.a`/`.so`）直接跳过（不进链接参数表，
所以不会链接）。`cc1()` 在 `irgen()` 之后、`open_outfile()` 之前 `return`。

#### 前端时间分布（临时插桩实测，插桩已移除；`grep -c TEMP-TIME src/main.c` = 0）

**注**：下表是机器满载时（全量套件 + 崩溃全扫并行，load ≈ 10）测的，绝对值偏大 3 倍左右；
空载口径与更正后的结论见 **§R118**。

| 输入 | 预处理 | parse | fold_ast | irgen | cc1 合计 | 驱动（`-fsyntax-only`） | 驱动（`-S`） |
|---|---|---|---|---|---|---|---|
| `sqlite3.c`（9.5 MB） | 1038 ms | **7764 ms** | 38 ms | 135 ms | 8976 ms | 8.91 s | 11.64 s |
| `src/parser.c`（9.4 k 行） | 181 ms | **209 ms** | 7 ms | 27 ms | 424 ms | 0.85 s | 0.87 s |
| `src/irgen.c` | 117 ms | **155 ms** | 1 ms | 12 ms | 286 ms | 0.33 s | — |
| 小程序 | 12 ms | 1 ms | 0 ms | 0 ms | 13 ms | — | — |

读法：**parse 占前端 50–88%**，`fold_ast` 与 `irgen` 合起来只有 1.5–8%；`-fsyntax-only` 相对 `-S`
省下的 2.7 s（sqlite3.c，约 23%）全部来自跳过后端。

#### 「三条 irgen 错误能不能前移到 parse，好让语法检查更快」—— 能移，但不划算

- `not a lvalue`（`irgen.c:301`）：用 14 个形状探过（`&(x+1)`、`asm` 输出操作数写成右值、`(x=1)=2`、
  非左值的成员/数组元素、`va_start` 取右值…），**全部被 parse 自己拦下**（`lvalue required as unary ‘&’
  operand` / `lvalue required in ‘asm’ statement` / `lvalue required as ‘=’ operand`），没找到能到达
  irgen 那一条的输入 —— 它是**安全网**，性质上更接近内部断言，不是“用户可见错误”。
- `atomic aggregate larger than 8 bytes`：**能前移**，但要覆盖 6 个形状（`atomic_load`、`atomic_store`、
  `atomic_exchange`、`atomic_compare_exchange` 四个内建，加上直接读写 `_Atomic` 对象的两种普通访问 ——
  后两种“这是原子访问”的判断在 irgen），前移等于把这条判断抄一份。
- `‘asm’ operand of aggregate type is not supported`：**能干净地前移**（parse 已经有操作数类型与
  `is_indirect`），而且这条是 cxx 有意比两家严格的拒绝（见 §R117 末段）。

但即使三条都前移，收益就是上表里 `fold`+`irgen` 的 1.5–8%；而**停在 parse 之后还会丢掉 `fold_ast` 的警告**
（`-Wconstant-conversion`/`-Wfloat-conversion`/`-Wliteral-conversion`/移位计数 —— 实测其调用栈是
`check_shift_count ← fold_node ← fold_ast`），也就是 `-fsyntax-only -Wall -Werror` 会安静下来，
对做代码检查的人是倒退。**结论：保持现状**（跑完前端、不写文件）；要提速应该提在正确的地方 ——
跳过后端与不落盘，那才是大头。

因为 `parse()` 之后还有一趟**会报错**的前端。实测（`-fsyntax-only` 与 `-c` 各跑一遍）：

| 用例 | `-fsyntax-only` | `-c` | 检查在哪 |
|---|---|---|---|
| `_Atomic struct S v;`（`struct S` 是 16 字节）`atomic_load(&v)` | **报** `atomic aggregate larger than 8 bytes or of non-power-of-two size is not supported` | 同 | `irgen.c:312` —— 只有 `irgen()` 会报 |
| `struct S; struct S a[3];`（元素类型不完整） | 报 `array has incomplete element type` | 同 | `parse()` |
| `(void)0 = x` | 报 `lvalue required as ‘=’ operand` | 同 | `parse()` |

放在 `parse()` 之后就会漏掉第一类：构建脚本的探测**通过**、真正的编译随后失败 —— 对“只做语法检查”的
用途来说这是最坏的失败模式。代价是 `-fsyntax-only` 不比普通编译省前端时间，它省的是后端
（跳过 `clang -S`、汇编器与链接器）和文件写入。

#### 一处方向相反的差异（初稿写反了，实测更正）

| 用例 | cxx `-fsyntax-only` | cxx 普通编译 | gcc | clang |
|---|---|---|---|---|
| `void f(void){ int x=1; asm("" : "=i"(x)); }` | **不报** | 报 `could not allocate output register for constraint 'i'`（**LLVM 后端**发的） | 静默接受 | 报 `invalid output constraint '=i' in asm`（sema） |

这条 asm 约束检查 cxx 交给 LLVM 后端，`-fsyntax-only` 不走后端，所以报不出来；clang 在自己的 sema 里查，
所以它的 `-fsyntax-only` 也能报。本节初稿把两个方向写反了，实测后更正 —— 这属于结构差异（cxx 没有独立
sema 检查器去重做后端的约束校验），不改代码，记录在案。

#### 实测

| 项目 | 之前 | 现在 |
|---|---|---|
| `test/conformance.sh` | 301 / 0 gap | **305 / 0 gap**（+4 条：无 `main` 也能过、`-o` 被忽略且不产生文件、报错仍然报、`-E` 优先） |
| `doc/crash-smoke.sh` | 用 `-S -o /dev/null` 代替 | 改回 **`-fsyntax-only`**（就是它文档里写的那个选项），并修好 `stride 1` 选不出样本的 bug（`NR % 1 == 1` 恒为假） |
| `make test` | exit 0 | exit 0（c2y 101/0） |
| c2ycov / 2 / 3 / 5 | 109 / 34 / 19 / 16 | 109 / 34 / 19 / 16 |

#### 边界情形（都实测过）

| 用例 | 结果 |
|---|---|
| `-fsyntax-only a.s` / `a.S` / `a.o` | rc 0，**什么都不做**（不汇编、不预处理、不进链接参数表）—— 与 gcc 一致 |
| `-fsyntax-only -dump-tokens` / `-ast-dump` | 转储照常打印（这两个开关本来就在驱动里提前返回） |
| `-fsyntax-only -MD -MF x.d` | 写依赖文件、不编译 —— 与 gcc 一致 |
| `-Werror` 下的警告 | 仍按非零退出（警告机制未改） |
| 全语料崩溃全扫（37,190 个测试，`-fsyntax-only`，`-j8`） | **0 崩溃** —— 这是修好工具之后第一次真正跑满全语料（此前几次读数因为 `-fsyntax-only` 不存在而什么都没测） |

#### 全量那次的读数说明

用最终二进制重跑全量得 **17,637 / 191**（上一次 17,638 / 189）。差的两个经复核都是**负载下的偶发**，
不是编译器行为变化：

- `C/0134/0134_0136.c` 那次被记成 `compile`：单独重编 **5/5 通过**，把 `C/0134`、`C/0185`、`C/0140`
  三个目录整目录重跑探针得 **311/312、0 失败**（当时机器 load ≈ 10，探针与我的 c2ycov/conformance 并行跑）。
- `C/0185/0185_0123.c` 那次被记成 `output`：这个文件本轮早些时候也来回跳过，重跑三次其输出与 clang **逐字节相同**。

所以真实成绩仍是 **17,638 / 189**；这两例的偶发性来自探针在高负载下的编译/运行失败，值得以后把
探针的并发调低一点再复跑（`FJ_JOBS`）。

`-fsyntax-only` 现在可用于构建脚本的探测；`make`/autoconf/cmake 那类“只做语法检查”的试探不会再因为
`unknown argument` 而误判 cxx 不可用。

#### 附：8,783 个「参考实现编不过」到底是什么（用户提问引出的一次排查）

`doc/fujitsu.sh` 的 `try()` 是**编译并链接**（`clang -w -o bin f.c -lm`），失败即记 `reffail`。实测：

| 分类 | 数量 | 依据 |
|---|---|---|
| 多文件测试的单个文件 | **8,708（99.1%）** | 分布在 **1,714 个**目录里，每个目录有 `CMakeLists.txt` + `llvm_multisource(...)`，一组约 5 个 `.c` 共享一个 `.reference_output`；探针一次只编一个文件，没有 `main` 的那个链接失败（抽样 219 个中 **196 个确实没有 `main`**） |
| 单文件、clang 自己也编不过 | **75** | 抽样 439 个里 436 个败在**链接**、只有 3 个是编译错误。这 75 个中：约 49 个是 `-Wincompatible-pointer-types`（clang 23 把这类指针不匹配从警告升为错误，它们是写给宽松编译器的老测试）、13 个缺头文件、2 个未声明或缺失的库函数（`memcpy`、`gets`）、`rsize_t`/`RSIZE_MAX`/`errno_t`（C11 Annex K）与 `TMP_MAX_S` 各 1 |

**结论：这一类不是负向测试。** 整个套件没有“必须被拒绝”的约定：每个测试都是“跑起来、与
`*.reference_output` 比输出”（共 29,479 个 `reference_output`）。所以这 37,190 个测试**不能**回答
“cxx 是否正确拒绝了错误的程序”这个问题。

**但它顺手提供了一份负向素材**：把那 75 个单文件测试交给 cxx（探针从不问它们），结果 **69 拒 / 6 收** ——
38 条 `incompatible types when passing argument`、11 条 `incompatible types when assigning`、9 条缺头文件、
2 条 `implicit declaration of function ‘memcpy’`、`TMP_MAX_S`/`rsize_t`/`RSIZE_MAX`/`errno_t` 各 1；
cxx 收下的 6 个全是 clang 因 **OpenMP 头缺失**（4 个）或**链接失败**（2 个）才失败的，不是语义错误。
也就是说那 49 个指针不匹配的约束违规，**两家都不放过**。

负向覆盖目前靠我们自己的断言：`test/conformance.sh` 里 `bad`/`pedantic`/`-pedantic-errors` 那 20 余条，
以及 `doc/c2ycov*.sh` 的 `rej`。要再扩，最省事的素材就是上面这 75 个文件。

### R115 用户提问引出的两个真缺陷：数组长度的类型检查、逗号不算常量表达式 —— ✅ 编译侧再收两类
用户问：“`array_dimensions` 解析完 `len` 的表达式之后似乎没有判断他是整数类型？”——**确认属实**，
而且顺着这条线还查出相邻的第二个缺陷（逗号表达式被当作常量表达式）。两个都修好，各留断言。

#### 1）数组长度没有类型检查（6.7.7.3p1）

N3685 **6.7.7.3p1**（C2y 里 `6.7.6.2p1` 的新编号）：

> If they delimit an expression, called the **array length expression**, the expression **shall have an
> integer type**. If the expression is a constant expression, it shall have **a value greater than zero**.

`array_dimensions()` 里 `len = assign(&tok, tok)` 之后确实什么都没查。实测（`-S -o /dev/null`）：

| 用例 | 修前 cxx | gcc | clang | 说明 |
|---|---|---|---|---|
| `int a[1.5];` | reject | reject | reject | **碰巧**被拒：`double` 不是整型常量表达式，诊断来自 `eval_ice()`，消息也是“expression is not an integer constant expression” |
| `int f(int n){ int a[n*1.0]; }` | **accept** ✗ | reject | reject | 变长数组这条路上**完全没有检查** |
| `void f(int n, int a[n*1.0]);` | **accept** ✗ | reject | reject | 形参同样 |
| `int a[-1];` | **accept** ✗ | reject | reject | 更糟：`-1` 正好是 `array_of()` 里“长度未指定”的哨兵，`int a[-1]` 静默变成 `int a[]`，`sizeof(a)` 事后报“incomplete type” |
| `int a[0];` | accept | accept | accept | 三家都当 GNU 扩展接受；`-pedantic-errors` 三家都拒 |

修法（`src/parser.c` 的 `array_dimensions()`，一处覆盖声明、形参、类型名三条路）：

- 解析完 `len` 后 `add_type(len)` 并检查 `is_integer(len->ty)`，不是就报
  `size of array has non-integer type ‘double’`（非算术类型则不带类型名）。
- 常量分支改成先取 `n = eval_ice(len)`：`n < 0` 报 `size of array is negative`（不再让 `-1` 变成“长度未指定”），
  `n == 0` 走 `pedantic()`（GNU 扩展，`-pedantic` 警告、`-pedantic-errors` 致命——与 gcc/clang 逐项一致）。

修后 18 个“必须拒/必须收”的探针与 gcc、clang **完全一致**；另有 13 个合法形状
（`(int)1.5`、枚举、`sizeof`、函数调用 VLA、`short`/`unsigned` VLA、`[const 3]`、`[static n]`、`[*]`、
多维 VLA、`_BitInt` 长度、`6/2`）全部照常接受。

#### 2）逗号表达式被当成常量表达式（6.6p3，探针时顺带发现）

N3685 **6.6p3**：“Constant expressions shall not contain assignment, increment, decrement, function-call,
or comma operators, except when they are contained within a subexpression that is not evaluated.”

cxx 的 `fold_node()` 会把 `(1, 3)` 折成 `3`（`src/opt_ast.c` 的 `ND_COMMA` 分支），折叠之后它到处都“像常量”：

| 用例 | 修前 cxx | gcc | clang |
|---|---|---|---|
| `_Static_assert((1, 3), "x");` | accept | **reject** | accept |
| `enum { A = (1, 3) };` | accept | **reject** | accept |
| `case (1, 3):` | accept | **reject** | accept |
| `struct s { int a : (1, 3); };` | accept | **reject** | accept |
| `static int x = (1, 3);` | accept | **reject** | accept |
| 文件作用域 `int a[(1, 3)];` | accept | **reject**（“variably modified at file scope”） | accept |
| `__builtin_constant_p((1, 3))` | 1 | **0** | 1 |
| 块作用域 `int a[(1, 3)];`（允许非常量长度） | accept | accept | accept |

修法三处，都只针对**用户写出来的**逗号（解析器内部为原子内建/`sizeof` VLA 造的 `ND_COMMA` 不走这些路）：

1. `eval2()` / `eval_int128()` / `eval_fp128()` 的 `ND_COMMA` 分支不再求值，直接报
   `expression is not an integer constant expression`（三种求值器都要，因为常量表达式按类型走不同的求值器）。
2. `is_const_expr()` 在 `fold_node()` **之前**先看根节点是不是 `ND_COMMA` —— 折叠正是抹掉它的那一步。
   这样文件作用域的 `int a[(1, 3)]` 按“非常量长度 ⇒ 可变长类型”处理，随即撞上 6.7.7.3p2
   （文件作用域不得有可变长类型）而报错；块作用域仍照常接受（VLA）。
3. `opt_ast.c` 的 `ND_COMMA` 折叠在 `in_static_init` 时提前返回，不再折掉逗号：
   静态初始化器要的是常量表达式（6.7.9p4），折掉了就查不出来。其它位置照旧折叠——
   `((void)sizeof(int), 4)` 仍要折成常量 `4`（cpython 的 `Py_ARRAY_LENGTH` 形状，早前一轮的断言）。

guarded 之后 `static int x = (1, 3);` 与 gcc 一致地拒绝，7 个逗号用例全部与 gcc 同侧。这是
**gcc/clang 分歧**（clang 一律接受并折叠），按标准字面（6.6p3）站 gcc，已记入 `doc/fj-divergences.md`。

#### 实测汇总

| 项目 | 之前 | 现在 |
|---|---|---|
| `test/conformance.sh` | 285 / 0 gap | **301 / 0 gap**（+16 条断言：数组长度 9 条、逗号 7 条） |
| `make test` | exit 0 | exit 0（c2y 101/0） |
| c2ycov / 2 / 3 / 5 | 109 / 34 / 19 / 16 | 109 / 34 / 19 / 16 |
| bootstrap | cxx2=cxx3=cxx4 | cxx2=cxx3=cxx4 **逐字节相同**，21 个目标文件亦相同 |
| `doc/tcctests.sh` | 106 ok / 0 | 106 ok / 0 |
| 全量复跑（37,190，`-j7`） | 17,638 / 189 | **17,638 / 189** —— 逐测试比对与上一轮**完全一致**（没有一个测试改类），即这两处收紧**没有**让任何原本能编的测试变得不能编 |

#### 顺带修好一个“自己不生效”的工具

`doc/crash-smoke.sh` 一直用 `-fsyntax-only` 编译每个样本，而 **cxx 没有这个选项**：它对每个文件都回
`unknown argument` 并 exit 1，脚本把这当成“没崩溃”，于是此前几次“0 崩溃”的读数**什么都没测**。
改用 `-S -o /dev/null`（同样走完整前端）后重测：**3,100 个样本，0 崩溃**。
`-fsyntax-only` 当时是驱动接口上的一个缺口（gcc/clang 都有），**已在 §R116 实现**；在那之前
`-S -o /dev/null` 是替代品，`doc/crash-smoke.sh` 现在用回了这个选项本身。

#### 同一函数里另外两条约束（本轮只记录，未改）

为了确认这条约束的边界，把 6.7.7.3p1 的其余约束也做成探针，又发现两条**同一函数**里缺的检查。
这两条 **gcc 与 clang 都拒**（不是分歧），但影响面小（都只涉及不常见的声明形式，野生代码不用它们），
按“最小可复核的改动”原则留作下一轮的清单：

| 用例 | cxx | gcc | clang | 约束与缺在哪里 |
|---|---|---|---|---|
| `struct S; void f(struct S a[3]);`（形参的元素类型不完整） | **accept** ✗ | reject | reject | 6.7.7.3p1：“The element type shall not be an incomplete or function type.” cxx 只在**声明**层查（`parser.c` 两处报 `array has incomplete element type`），形参声明走 `func_param()`，把那两处绕过去了 |
| `void f(int a[3][static 5]);` | **accept** ✗ | reject | reject | 同条：“The optional type qualifiers and the keyword `static` shall appear only in a declaration of a function parameter with an array type, and then only in the **outermost** array type derivation.” cxx 的检查只分 `is_param`，没有区分“最外层” |
| `void f(int a[3][const 5]);` | **accept** ✗ | reject | reject | 同上，限定符那一半 |

修法方向已经明确：前者把元素完整性检查放进 `array_dimensions()`（或 `func_param()` 的类型调整之前），
后者给 `decl_suffix()` 增加“是否最外层”的实参传给 `array_dimensions()`。两个都是诊断补全，不动代码生成。

另外确认**没有**问题的相邻形状（三家一致）：`void f(struct S *a)`、`extern` 声明的不完整类型、
`int a[static 3][5]`、`int a[const 3]`、`int a[static 3]`（非形参）、`int a[*]`（原型外）、
`sizeof(int[const 3])` —— 该拒的拒、该收的收。

### R114 七个真缺陷（§1c 运行期 + §1b 编译期）—— ✅ `plain` 21 → 2（那 2 个是 clang 侧分歧）

上一轮留下的“下一步”是 `__builtin_constant_p`；按它做下去时顺手把 `plain` 那 21 个逐个跑通了，
一共修好 7 处**真缺陷**（6 处在预处理/常量求值，1 处是 SysV AMD64 的 `va_arg`）。全部先写出最小复现、
与 `gcc`+`clang` 两家对齐后再动代码，并在 `test/conformance.sh` 里各留一条断言（278 → **285**）。

#### 1）`__builtin_constant_p`：折叠后剩下的是“转换节点”（§R113 的待办）

上一轮按“字符串字面量 / 空指针常量”补的两个判定没命中，原因查清了：**折叠后的形状是转换节点本身**。
把 `NodeKind` 的枚举按序号算出来（`python3` 解析 `src/cxx.h`）之后对照运行时看到的数字：
`""` → **43** = `ND_IMCAST`（数组到指针的隐式转换），`NULL` → **44** = `ND_EXCAST`（`(void *)0` 的显式转换），
`sizeof(a)` → 78 = `ND_NUM`（本来就对）。修法是**先剥掉 `ND_IMCAST`/`ND_EXCAST`/`ND_LVTOR` 再判定**
（“常量的转换还是常量”）。实测 23 项探针（`NULL`、`(void*)0`、`(int*)0`、`""`、`"abc"`、`(int)3.0`、
`(long)a`、`(int)(a+1)`、`cp`、`&ci`、`ci`、`g`、`E`、`(int)E`、`sizeof(a)`、`(size_t)sizeof(a)`、`'x'`、
`1?2:3`、`(int)(1?2:3)` …）中，cxx 与 **gcc 完全一致**；与 clang 只差 `ci` 一项（即已记录的第 13 例）。

#### 2）宏重扫描：未被替换的名字要**永久**不可再展开（`C/0048/0048_0087.c`）

`cxx-ng` 里最后一个。最小复现（`gcc` 与 `clang` 输出逐字相同）：

```c
#define f1(a) a*g1
#define g1    f1
#define f2(a) a*g2
#define g2(a) f2(a)
#define x(a) # a
#define y(a) x(a)
y(f1(2)(9))   /* 参考： "2*f1(9)"   cxx 原来： "2* 9*f1" */
y(f2(2)(9))   /* 参考： "2*9*g2"    cxx 原来： "2* 9*g2" */
```

两处缺陷叠在一起：

- **涂色要跨出窗口**。6.10.4.1p2：“These nonreplaced macro name preprocessing tokens are **no longer
  available for further replacement** even if they are later (re)examined …”。cxx 原来只有一层全局
  “正在替换”栈，出栈就忘了；`f1` 正是出栈后被 `rescan_call` 重新当作调用展开的。修法：`expand_macro`
  里因 `is_disabled` 而**原样拷出**的名字，给副本打上 `noexpand`（永久）。
- **展开的首 token 继承名字的空白**。`rescan_call` 里少了两处调用点都有的
  `prev->next->is_leadingws = macro_name->is_leadingws;`：`f2` 的 `9` 因此带上了 f2 体内参数前的空格。
  这是 `2*9*g2` 与 `2* 9*g2` 的差别，与涂色无关。

`f1` 与 `f2` 的差别本身是标准里那条“嵌套替换”子句的边界：`g2(9)` 的实参表来自**文件**，
没有被 f2 这次替换涂色（Prosser 算法里是与右括号的 hide set 取交），所以内层 `f2` **要**展开；
而 `g1` 是对象宏，`f1` 直接来自被替换的替换表，**不**展开。

#### 3）SysV AMD64 `va_arg`：两处 ABI 缺陷（`C/0080`、`C/0081` 共 6 个文件）

| 缺陷 | 现象 | 原因 | 修法 |
|---|---|---|---|
| `va_arg_fp.mem_step` 写成 16 | 固定形参占满 8 个 SSE 寄存器后，第 2 个 variadic `double` 起全部错位（`C/0081/0081_0011.c` 的 `…,9,11,12,10`） | 16 是**寄存器保存区**里 XMM 槽的间距；**栈**上一个 double 只占 8 字节 | `mem_step = 8` |
| `va_arg_mem16.offset_bound = 0` 想表达“永远在溢出区” | 函数**没有具名整型形参**时 `gp_offset == 0`，无符号的 `offset <= 0` 成立 → `long double` 从寄存器保存区读（`C/0080/0080_0013.c` 读出第 5 个具名实参、`C/0081/0081_0006.c` 读出 NaN） | 这个测试表达不了“永不”，注释里“gp_offset 至少是 8”也不成立 | 改成 `VA_MEM_OVERFLOW`（根本没有寄存器分支），并给这条路径补上 `mem_align` 的**向上**对齐（long double 是 16 字节对齐） |
| `VA_MEM_OVERFLOW` 直接返回地址 | 改完上面一条，`long double` 读出来是 1e-4937 之类的垃圾 | 该分支原来只服务**空记录**（值即地址）；标量必须就地取载 | 记录仍返回地址，标量按 `min(align,8)` 取载，与 `VA_MEM_REGS` 的汇合点一致 |

修完后 5 个自写探针（`double` 形参、`long double` 形参、`float` 形参、`int` 形参、混合 8 具名 + 5 变参）
与 clang **逐字一致**；受影响的 8 个测试文件全部转为 SAME。

#### 4）`#if` 要在 `intmax_t`/`uintmax_t` 里求值（`C/0125/0125_0026.c`、`0125_0028.c`）

6.10.1p4：`#if` 里“all signed integer types and all unsigned integer types act as if they have the same
representation as, respectively, `intmax_t` and `uintmax_t`”。cxx 把 `#if` 交给**普通常量表达式解析器**求值，
于是按字面量**写出来的类型**做常规算术转换：`1l > -1u` 成了 `long` 对 `unsigned int`——而 `long` 能表示所有
`unsigned int`，两边都变有符号 → 答 1，gcc/clang/标准都答 0。修法：`eval_const_tokens` 里把每个整型字面量
**重新定型**为 64 位（各目标的 `intmax_t`/`uintmax_t` 都是 64 位）并按自身有无符号决定 signed/unsigned。
8 项混合符号探针现在与 clang **全同**。

#### 5）`(-1u)/2` 在 `#if` 里算出 `2^64-1`（同一个文件）

第 4 条修好后仍有一处：`#if (-1u)/2 == 0x7FFFFFFFFFFFFFFF` 两边不一致。原因是 `eval_int128()` 的
**中间值**没有按自己的类型收窄：`-1u` 是 128 位全 1，除以 2 得 128 位的 `2^127-1`，最后才截到 64 位 → `2^64-1`。
修法：`eval_int128_wide()` 保留原实现，外面包一层 `eval_int128()`，按 `bitint_width()` 或 `size*8`
把每个子表达式的值 `int128_normalize` 回它自己的宽度（`_BitInt` 与非 `_BitInt` 两种宽度来源都照顾到）。
这同时修好了“`int` 溢出后再参与 64 位运算”的一类隐式错误。

#### 6）字符串化：换行也算空白；杂散反斜杠不转义（`C/0125/0125_0020.c`、`0125_0008.c`）

- 词法器把行首 token 记成 `is_sol`，**不设** `is_leadingws`，而 `join_tokens()` 只看后者，
  于是 `x(f( 123\n456 ))` 字符化成 `"123456"`；参考是 `"123 456"`。改为两个标志都看。
- 换行同时还要区分“实参自己的换行”与“形参在体内的位置”：替换进参数位置的**首** token 现在连同
  `is_sol` 一起继承形参（`y(G(\n9))` 是 `"q 9"`，`y(F(\n9))` 配 `q*p` 是 `"q*9"`）。
- `#define a(x) \n`（`\` 不是合法的预处理记号）参考实现在字符化时**不转义**这个反斜杠，
  拼出来的字面量 `"\n"` 因此含一个**换行符**（长度 1）；cxx 原来把它转义成 `\\n`（长度 2）。
  改为按 token 拼**拼写**（只对字符串/字符字面量内部的 `\` 与 `"` 转义），再交给第 6 阶段
  （`convert_str_literal`）解码——`test/conformance.sh` 里 5 种字符串化场景现在与两家全同。

#### 7）`__LINE__` 在对象宏里答的是定义行（`C/0054/0054_0102.c`）

`__LINE__`/`__FILE__` 通过 token 的 `origin` 回溯到**最外层调用**。对象宏那条路把 `origin` 盖在
**展开之后**，而 `__LINE__` 是在展开**进行中**读它的——于是答了 `#define` 所在的行（该测试因此 72 行全错）。
修法：对象宏的体先 `copy_body()` 成带 `origin` 的副本再展开（顺带也不再往共享的宏体上写字）。实测
`L=7 / M=8 / FL()=9`（`__LINE__` 直接、经一层对象宏、经函数宏）与 gcc/clang 全同。

#### 实测汇总

| 项目 | 之前 | 现在 |
|---|---|---|
| `plain`（两家都不打 OK/NG、逐个案看） | 21 | **2**（`0055_0670`/`0676`，见下） |
| `test/conformance.sh` | 278 / 0 gap | **285 / 0 gap** |
| `make test` | exit 0 | exit 0（c2y 101/0） |
| c2ycov / 2 / 3 / 5 | 109 / 34 / 19 / 14 | 109 / 34 / 19 / **16** |
| `-E` 往返（`C/0048`+`0125`+`0054`+`0081` 里能跑起来的 530 个） | — | **529/530**：预处理后再编译的输出与直接编译**逐字相同**（唯一差异是测试自己用 `%p` 打地址） |
| `doc/tcctests.sh`（tinycc 自检程序） | 106 ok / 0 | 106 ok / 0 |
| `doc/bootstrap.sh`（自举链） | cxx2=cxx3=cxx4 | cxx2=cxx3=cxx4 **逐字节相同**，21 个目标文件也逐字节相同 |
| `doc/selfhost.sh` | 21 ok / 0 | 21 ok / 0 无效 IR / 0 崩溃 |

`-E` 往返这一项是这轮新加的检查：`is_sol`/`origin` 的改动影响的是**打印器**（`print_tokens`、`needs_space`），
普通编译看不见它；拿真实文件走“`-E` → 再编译 → 运行”与“直接编译 → 运行”对比，530 个里 529 个逐字相同。
脚本落在 RT-DIFF 0048_0158 (rc 0 vs 0)
        33c33
        < ***-33**** N   G ****:9e1babf
        ---
        > ***-33**** N   G ****:d4486eaf（用法：RT-DIFF 0048_0158 (rc 0 vs 0)
        33c33
        < ***-33**** N   G ****:858902cf
        ---
        > ***-33**** N   G ****:adb3ca3f， 换语料），与 crash-smoke: 930 of the corpus's tests compiled with -fsyntax-only, 0 crashed 一样是新增工具。
脚本落在 `doc/eround.sh`（用法：`bash doc/eround.sh`，`SRC=<目录>` 换语料），与 `doc/crash-smoke.sh` 一样是新增工具。

#### 全量复跑（37,190 个测试，`-j7`；§R105 之后第一次）

| | §R105（之前） | 本次（§R114） |
|---|---|---|
| 通过 | 17,612 / 17,833 | **17,638 / 17,833** |
| 失败 | 215 | **189** |
| `compile` | 176 | **171** |
| `output` | 38 | **18** |
| `exit` | 1 | **0** |
| `timeout` | 0 | 0 |

`doc/fj-worklist.md` 已用这次结果重新生成（`bash doc/fj-triage.sh`）：

- **§1b 真缺陷：只剩 145 条政策类**（`implicit declaration of function`，§0/§R81 已记录的政策），
  原来的 5 条“已修复（结果文件是旧的）”消失 —— 编译侧真缺陷 **0**。
- **§2 真缺陷：`cxx-ng` 0、`plain` 0** —— 运行期真缺陷 **0**。剩下的 18 个 `output` = 6 个“参考自己报 NG”
  （`C/0054` 那 6 个，cxx 全对）+ 3 个已记录分歧 + 9 个 §1c 缺口（属性/`HAS_SUBNORM`/`DECIMAL_STR`/目标宏）。
- §1a 缺口 22 条、§1c 缺口 9 条、§3 缺口三类（274 个 `()`、73 个 `__sync_*`、51 个 SSE/MMX 内建），
  按 §0 只记录。

`doc/fj-triage.sh` 这轮补了两处，都是“分类本身出错”的修正：

1. `output` 分类现在会查 `doc/fj-divergences.md`：已记录的测试归新的 `divergence` 类，不再计成 `cxx-ng`
   （`C/0178/0178_0035.c` 因此从“真缺陷 1”变成“已记录分歧”）；同时把该文件里那一行的 `C/0178/0178_0035`
   补上 `.c` —— 提取分歧清单的正则是 `C/<目录>/<文件>.c`，少了后缀就一直没被认出来。
2. 计数列的 `grep -c … || echo 0` 在计数为 0 时会打印两行（`0` 与 `0`），改用 `awk` 计数；
   这一轮 `plain` 归零，正好把它暴露出来。

`doc/c2ycov5.sh` 里 `rej()` 从来没定义过（`bash: rej: command not found`），两条“应当报错”的探针
**静默什么都没做**，汇总仍显示干净——已补上，两条都通过。这类“探针自己不生效”的坑记在这里备查。

#### 剩下 2 个：clang 侧分歧，已记入 `doc/fj-divergences.md`

`C/0055/0055_0670.c` / `0055_0676.c` 用非法内存序调 `__atomic_store`（7.17.7.1p2 约束，两家都只警告）。
**clang 把整条语句丢掉**（`foo1` 编成空函数），gcc 照常存储；测试自带的非 GNU 分支期望输出是
`C/0055/0055_0670.c` / `0055_0676.c` 用非法内存序调 `__atomic_store`（7.17.7.1p2 约束，两家都只警告）。
**clang 把整条语句丢掉**（`foo1` 编成空函数），gcc 照常存储；测试自带的非 GNU 分支期望输出是
`0x7f 0x7e 0x7e`（即**存储发生**）。cxx 与 gcc、与测试自身的期望一致 → 记为参考分歧，不改。

#### 方法学备注（本轮踩到的一个测量陷阱）

在 DSH 的 pwsh 命令行里，`$?`、`$x`、`$cc` 这类 `$` 变量会在 bash 看到之前就被展开掉
（`/bin/false >/dev/null 2>&1; echo "rc=$?"` 打出来的却是 `rc=0`）。这一轮里有几次“退出码”读数因此是假的
——一度以为“cxx 报了错却仍返回 0”，写成脚本文件重新测量后确认：**cxx 对所有错误路径都返回非零**
（`--bogus-flag`、缺文件、`-c`/`-S`/`-fsyntax-only`/整链、`#error`、语法错误、直接跑 `-cc1` 全都非零，
好文件返回 0）。结论：**退出码只能在脚本文件里测**（`if cmd; then … else … fi`），命令行上只采信输出对比。
本轮所有实质结论都来自输出对比，未受影响。

### R113 `__builtin_constant_p`：用户定性为真缺陷 —— ⚠️ 两处判定已补，尚未命中折叠后的形状

用户指出：`__builtin_constant_p` **已实现**，所以 `C/0178/0178_0035` 是**真缺陷**（编译期常量判定），
不是缺口；并且判定规则是**不能明确判断为常量就视为非常量**（例：未初始化的 `const int ci;`）。
已把 `__builtin_constant_p` 从 triage 的缺口判定里移出。

#### 13 个检查里的实测对照

| # | 表达式 | gcc | clang | cxx（现在） | 判定 |
|---|---|---|---|---|---|
| 1–2 | 常量、常量除法 | 1 | 1 | 1 | ✓ |
| **3** | `NULL` | 1 | 1 | **0** | **真缺陷**（待修） |
| **4** | `""` | 1 | 1 | **0** | **真缺陷**（待修） |
| 5–12 | `cp`、`&ci`、`cc`、`a=2`、`func()`、`func`、`&func`、`sizeof(a)` | 0…0…1 | 0…0…1 | 0…0…1 | ✓ |
| 13 | `sizeof(a)==sizeof(cc) ? ci : 0` | **0** | 1 | 0 | **分歧，标准/规则在 cxx 这边**（已入 `doc/fj-divergences.md`） |

#### 已补的两处（方向对，尚未命中）

`is_const_expr()` 新增：字符串字面量（`ND_VAR` 且 `var->is_str`）与空指针常量（`ND_NULLPTR`
或 `is_nullptr(ty)`）。**但实测未生效**：加临时 trace 看到，`fold_node()` 之后 `""` 是**节点种类 43**、
`NULL` 是**44**（`sizeof(a)` 是 78 = `ND_NUM`，这一个本来就对）。下一步很具体：
把 `ND_ADDR`/`ND_NULLPTR` 等候选的**数值在运行时打出来**（上一次尝试因脚本环境没打出来），
再在 `is_const_expr()` 里接受对应形状（字符串字面量的地址、常量 0 转成指针）。

（现状：零个回归。`test/conformance.sh` 278/0；c2ycov 109/0；c2y 101/0；`make test` exit 0；
trace 已移除，`grep -c CXX_TRACE src/*.c` 全 0。）

**已在 §R114 补完**：折叠后剩下的是**转换节点**（`""` → `ND_IMCAST`、`NULL` → `ND_EXCAST`），
判定前先剥掉转换即可，第 3、4 两例现在与两家一致；第 13 例按用户规则维持 0 并记为分歧。

### R112 用户定性：函数级属性与优化相关一律列为缺口 —— ✅ 并修好 `_Alignas` 的一个真缺陷

#### 定性（按用户的口径）

| 测试 | 用到的东西 | 类别 |
|---|---|---|
| `C/0077/0077_0031`、`0077_0034` | `__attribute__((always_inline))` + `__builtin_return_address` | **缺口（函数级属性 / 内联）** |
| `C/0059/0059_0087` | `__attribute__((weakref("__target")))` | **缺口（属性）** |
| `C/0178/0178_0035` | `__builtin_constant_p` | **缺口（编译期常量判定）** |
| `C/0044/0044_0003` | `__SSE__` / `__SSE2__` / `__AVX__` 等目标特性宏 | **缺口（代码生成阶段）** |

已写进 `doc/fj-triage.sh` 的 `feature_of()`，日后自动归入工作清单 §1a，类名里标明“优化阶段”。

（背景：`__builtin_return_address(0)` 的实现本身是对的 —— cxx 发的就是 `llvm.returnaddress.p0`；
差的是 `always_inline` 没有转发成 LLVM 的 `alwaysinline` 属性，于是 AlwaysInliner 不动手，
内层函数取到的是它自己的调用点。按用户决定，这一块留给优化阶段。）

#### 同期修好的真缺陷：`_Alignas(0)` 不应抹掉先前的对齐（`C/0057/0057_0005`，`exit` 判定）

```c
long _Alignas(16) _Alignas(0) g_b;      /* 原本只得到 align 8，应为 16 */
assert((long)&l_b % 16 == 0);           /* 断言失败，程序 abort */
```

**6.7.5p5**：“An alignment specification of zero has no effect.”；**p6**：多个规定取**最严**。
cxx 用的是**赋值**，后面的 0 把前面的 16 覆盖了。修法：取最大、0 不参与；
非 2 的幂仍报错（与两家一致）。

| 验收 | 结果 |
|---|---|
| 对齐形状（全局/局部、两种 `_Alignas` 顺序） | 三家逐值一致（均 16 对齐） |
| `_Alignas(类型名)` 与非 2 的幂 | 前者接受；后者三家均报错 |
| `C/0057/0057_0005.c` | 不再 abort，rc=0（与参考一致） |
| `C/0057 + C/0013` | 750 of 755 passed |
| `test/conformance.sh` | **278 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

### R110/R111 修好：两个真缺陷（位域镜像推进、赋值表达式的值）—— ✅ 编译侧真缺陷归零，`0095`/`0129` 整目录全通过

#### R110：三处遍历的推进规则不一致（`C/0202/0202_0087`）

`INT64T mem64:64; … INT64T mem53:53; …` 的结构体，每个位域的**访问单位是 8 字节**，但 53 位只占 7 字节。
**值**的遍历按单位推进，而 R91 修过的两个**类型**遍历按“位所占字节”推进 —— 类型里因此多出一个 `[1 x i8]` 空隙，
元素数与初始化器对不上，LLVM 报 `initializer with struct type has wrong # elements`。

**修法**：统一到“**非跨元素按单位、跨元素按尾巴**”（与值的遍历一致）。

#### R111：赋值表达式的值必须是“赋值后左操作数的值”（`C/0095/0095_0005`、`C/0129/0129_0160`）

```c
struct t { signed int b05:2; signed long long b00:8; … } x, *y = &x;
y->b00 = y->b05 = … = 2;      /* b05 = 2 存进 2 位有符号位域后就是 -2 */
if (y->b00 == -2) OK          /* 两家参考都是 OK */
```

**6.5.16p3**：“An assignment expression has the value of the left operand **after the assignment**.”
对位域而言就是**存进去的那个值**（按位宽截断、按符号性扩展）。cxx 把右操作数原样返回，
于是链式赋值一路传下去的是 2，而不是 -2。

**修法**：`ND_AS` 在左操作数是位域时，用与 `load()` 相同的两次移位对**手中的值**截断/扩展
（不额外读内存，避免给 volatile 位域多一次访问）。

| 验收 | 结果 |
|---|---|
| `C/0202/0202_0087.c` | 编译并与参考输出一致 |
| `C/0095/0095_0005.c`、`C/0129/0129_0160.c` | 均**与参考逐字一致** |
| 链式赋值最小用例（含 `int v = (y->b05 = 3);`） | 三家逐值一致（`b00=-2 …`、`value of the assignment=-1`） |
| 位域家族 R83–R91 与 `C/0013` | 全部保持正确 |
| `C/0013 0095 0129` 目录 | **832 of 832 passed, 0 failed** |
| `test/conformance.sh` | **278 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

#### 剩余运行期真缺陷（下一批）

`cxx-ng` 原 8 个，本轮消掉 2 个（`0095_0005`、`0129_0160`），剩：
`C/0044/0044_0003`、`C/0048/0048_0087`、`C/0077/0077_0031`、`C/0077/0077_0034`、`C/0059/0059_0087`、`C/0178/0178_0035`，
以及 `exit` 的 `C/0057/0057_0005`。

### R109 现存缺陷的逐类定性（跑测试 + 查标准）与一个真缺陷的修好 —— ✅ `##` placemarker

用户要求：对现有缺陷跑测试、分类、遇到分歧记录，并给出**严格符合标准的做法**供参考。
下表逐类列出：现象、标准依据、两家参考行为、判定与建议。

| 类（数量） | 现象 / 最小复现 | 标准（N3685） | gcc / clang | 判定与严格做法 |
|---|---|---|---|---|
| `(cond ? a : b)` 作左值（1，`C/0044/0044_0007`） | `(a == 5 ? obj1 : obj2).array[3] = a;` | **6.5.16 脚注 111**：“A conditional expression does not yield an lvalue.”；**6.5.16p3**：“An assignment expression … is not an lvalue.” | 两家**接受**（含 `-pedantic-errors`） | **分歧，标准在 cxx 这边**：保持拒绝（已记入 `doc/fj-divergences.md`） |
| 条件表达式的指针操作数嵌套限定符（4，`C/0202/0097/0100/0103/0106`） | `p = i ? pa : cia;`（`int **` 与 `const int **`） | **6.5.16p3 约束**：“both operands are pointers to qualified or unqualified versions of compatible types”；**6.2.7p11**：“Two qualified types are compatible if and only if they are identically qualified versions of compatible types” —— `int *` 与 `const int *` 不是同限定符的兼容类型，**约束不满足** | **gcc 报错**；clang 接受 + `warning: pointer type mismatch` | **分歧，标准在 cxx 这边**（与 gcc 同侧）：保持拒绝，已记入分歧表 |
| `##` 与空实参（3，`C/0202/0202_0287`） | `#define t(x,y,z) x ## y ## z`；`t(,2,3)` 应为 `23`，`t(,,)` 应为空 | **6.10.5.4**：空实参“replaced by a **placemarker** preprocessing token”；**6.10.5.5**：“Placemarker preprocessing tokens are handled specially” | 两家均按 placemarker 处理 | **真缺陷，已修**：`subst()` 用列表头区分“真的以 `##` 开头”与“空实参造成的假象”；八种粘贴形态与两家逐值一致 |
| 巨型位域的初始化器 IR（1，`C/0202/0202_0087`） | `INT64T mem64:64; …` 的结构体全局初始化 | 标准无争议（初始化器必须合法） | 两家正常 | **真缺陷**：LLVM 报 `initializer with struct type has wrong # elements`，属 cxx 的位域镜像拼写（待修） |
| 146 个 `implicit declaration` | 调用未声明的函数 | C2y 不再容许旧形式（§0/§R81 已记录） | 探针参考（`clang -std=c17`）接受并警告 | **政策类**，只记录；已写进分歧表 |

#### 已修的那一个（`##`）

```c
#define t(x,y,z) x ## y ## z
t(,2,3)   /* 23 */      t(,,)   /* 空 */
```

cxx 没有 placemarker 概念，只要 `##` 前面的参数展开为空就报
`'##' cannot appear at start of macro expansion`。修法：`subst()` 记下**替换列表的头**，
只有列表真的以 `##` 开头时才报错；否则视为“左侧是 placemarker”——右侧自成一体，
两个 placemarker 相粘则什么也不产生。

| 验收 | 结果 |
|---|---|
| `t(1,2,3) t(,2,3) t(1,,3) t(1,2,) t(,,3) t(,2,) t(1,,) t(,,)0` | 三家逐值一致（`123 23 13 12 3 2 1 0`） |
| 真正以 `##` 开头的替换列表 | cxx 仍拒绝（与两家同侧） |
| `C/0202/0202_0287.c` | **编译并运行** |
| `test/conformance.sh` | **278 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

### R108 按标准判定：`(a == 5 ? obj1 : obj2).array[3] = a;` 的左值性 —— ✅ **cxx 正确**，记为分歧

用户要求“根据标准本身判断”。cxx 的目标草案（`doc/n3685.txt`）写得很明确：

| 依据 | 原文 |
|---|---|
| **6.5.16 脚注 111** | “A conditional expression does not yield an lvalue.” |
| **6.5.16p3** | “An assignment expression has the value of the left operand after the assignment, but is **not an lvalue**.” |
| 6.5.2.3p7 例 1 | “`f().x` is a valid postfix expression but is **not an lvalue**.” |

因此 `(a == 5 ? obj1 : obj2)` **不是左值**，`.array[3]` 也就不是可修改左值，`= a` 属**约束违反**：
cxx 报 `lvalue required as ‘=’ operand` 是对的。同一条规则也判第 95 行 `(obj1 = obj2).array[3] = a;` 无效。

实测到的参考行为（这是它们宽容，不是 cxx 错）：

| | `-std=c11` / `c17` / `c23` / `c2y` | 加 `-pedantic-errors` |
|---|---|---|
| gcc | 接受 | **接受** |
| clang | 接受 | **接受** |
| cxx | 拒绝 | — |

两家在**所有标准模式、连 `-pedantic-errors` 都不报**地接受它（实现的是 C++ 的“两个同类型左值 → 结果为左值”）；
这个测试靠的就是这份宽容。

#### 机制：分歧列表

新增 `doc/fj-divergences.md`：每条写明**标准依据**与**实测到的参考行为**。
`doc/fj-triage.sh` 读它，把列入的测试从“真缺陷”移到新的 **§1a2 记录的分歧**一节。
`C/0044/0044_0007.c` 已归入该节，工作清单里真缺陷的编译拒绝从 6 类降到 5 类。

（若以后决定跟随参考实现支持这个扩展，改动在 `modifiable_lvalue()`：当条件表达式的两个分支都是同类型左值时将其视为左值（并在内部用条件选择实现存储）；
目前保持严格按标准。）

### R107 修好：三个崩溃（空联合体、符号地址求值）—— ✅ 全量里已无 SIGSEGV

用户给出的原则：**无论输入如何，编译器本身不应该崩溃；最少要报告错误之后退出**。
已写进 §0，与“缺口只记录”并列。

#### 崩溃一：空联合体（`C/0186/0186_0076`、`0077`）

```c
union blank_uni1 {
};
```

`dump_type()` 的联合体分支取“对齐最大的成员”作为镜像，而空联合体没有成员 —— `union_canon_member()`
返回 NULL，随后 `print_type(mem->ty)` 解引用空指针（空**结构体**走另一条路，不会崩）。

修法：类型打印器与初始化器路径共 **7 处**加守卫（`init_is_punned`、`init_needs_inline`、`print_init_ty`、
`dump_init`、`create_lvar_init`、`is_fully_initialized`、`eval_gvar_data`），对“没有成员”给出“无需写入”的答案；
类型仍打成 `{ }`（与空结构体一致）。我自己的对照用例（嵌套、数组、局部、包在结构体里并带初始化器）
在第一版修法后仍崩，才把剩下的点找齐。

#### 崩溃二：符号地址求值时写空指针（`C/0202/0202_0140`）

```c
static test_t msg;
static int len = ((char *) &(msg.end)) - ((char *) &msg);
```

`eval()` 的契约是 `eval2(node, NULL)`——“只要数值，不关心哪个对象”——而 `eval_rval()` 在 ND_VAR
分支写 `*sym`：遇到 `&msg.end` 就向空指针写入。修法：`sym` 为空时用一个局部哑元。
偏移量正是这个差值所需（`msg.end` 在 `msg` 之后 4 字节），所以这个守卫不仅安全，结果也对。

| 验收 | 结果 |
|---|---|
| `C/0186/0186_0076`、`0077`、`C/0202/0202_0140` | 均**编译通过**，且输出与参考**逐字一致** |
| 空联合体在各种位置（嵌套、数组、局部、文件作用域、带初始化器） | 三家逐值一致（`1 0 4 2 5 0`） |
| `C/0186`、`C/0202` 目录重跑 | 见下方输出 |
| `test/conformance.sh` | **278 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

#### 不崩溃这件事，现在有了常驻检查

用户的要求是绝对的（“无论输入如何”），而探针只能告诉我们它正在数的那些文件。新增
`doc/crash-smoke.sh`：按步长抽取结果文件里的测试（**不分判定类别**），逐个用
`-fsyntax-only` 跑一遍，把信号退出（≥ 128）或输出里的 `internal compiler error` 计为崩溃，
**并以崩溃数作为自己的退出码**，可以直接当门禁。

| 范围 | 结果 |
|---|---|
| 176 个 `compile` 失败（triage 逐个重跑） | **0 崩溃** |
| 全语料抽样（步长 25，1,488 个文件，含 `reffail`/`skip`/`noproto` 各类） | **0 崩溃** |
| 全语料抽样（步长 40，930 个文件） | **0 崩溃** |

抽样不是全量：若要彻底，可以用 `bash doc/crash-smoke.sh <results> 1`（全部 37,190 个，`-fsyntax-only`
下约 30–40 分钟）。

### R106 缺口与真缺陷的更细分类（用户指出的三处）—— ✅ 已自动化进 `doc/fj-triage.sh`

用户指出：`C/0059/0059_0052.c` 与 `C/0125/0125_0014.c` 用的是**尚未支持的预处理特性**（归缺口）；
`C/0057/0057_0025.c` 涉及**未实现的 `atomic_signal_fence`**（归缺口）；`C/0057/0057_0075.c`
的 `FLT_HAS_SUBNORM` 在标准里**明确标为过时**。逐个核对后确认，并把判定写进了 triage。

#### 现在的分类（自动生成在 `doc/fj-worklist.md` §1a/§1b）

**§1a 缺口（只记录）—— 9 类 20 个文件**

| 数量 | 特性 | 代表文件 | 依据 |
|---|---|---|---|
| 5 | `#pragma redefine_extname` | `C/0059/0059_0054.c` | 指令未实现（编译过但链接时符号名未重命名） |
| 4 | 参考实现（`clang -std=c23`）同样拒绝 | `C/0048/0048_0049.c` | 三家都拒绝，无需修 |
| 3 | `#ident` | `C/0125/0125_0014.c` | 遗留指令，clang 接受并忽略 |
| 2 | 字符串里反斜杠接空白再接换行（GNU 扩展） | `C/0059/0059_0081.c` | 标准下不是接续，clang 作扩展接受 |
| 2 | `atomic_thread_fence` / `atomic_signal_fence` | `C/0057/0057_0024.c` | 内置未实现 |
| 1 | `*_HAS_SUBNORM`（**标准已标为过时**） | `C/0057/0057_0075.c` | C23 将这组宏列为 obsolescent |
| 1 | `float.h` 的 `*_DECIMAL_DIG` | `C/0057/0057_0076.c` | 宏未提供 |
| 1 | `_Complex` | `C/0057/0057_0079.c` | 未支持（§0 已声明） |
| 1 | `__attribute__((weak))` | `C/0181/0181_0235/0181_0235_0000.c` | 属性未生效 |

**§1b 真缺陷 —— 156 个**

| 数量 | 诊断 | 性质 |
|---|---|---|
| 146 | `implicit declaration of function ‘X’` | 政策类（C23 已删除的旧形式，§0/§R81） |
| 4 | `incompatible types when assigning` | 真缺陷（C/0202） |
| 3 | **SIGSEGV** | 真缺陷（C/0186×2、C/0202×1） |
| 1 | `lvalue required as ‘=’ operand` | 真缺陷 |
| 1 | `initializer with struct type has wrong # elements` | 真缺陷（位域初始化的 IR 拼写） |
| 1 | `'##' cannot appear at start of macro expansion` | 真缵陷（宏展开） |

#### 实现方式

`doc/fj-triage.sh` 新增 `feature_of()`：**读源文件**看它需要什么特性（`redefine_extname`、`#ident`、
`atomic_*_fence`、`*_HAS_SUBNORM`、`*_DECIMAL_DIG`、`_Complex`、`__attribute__((weak))`、反斜杠接空白的行接续），
都不命中时再用 `clang -w -std=c23 -c` 试一次：它也拒绝则归入“参考实现同样拒绝”，否则才算真缺陷。
分类结果写进工作清单的 §1a/§1b，下一次全量后自动更新。

### R105 第三次全量 + 工作清单分类升级 —— ✅ **17,612 通过，真失败 330 → 215**

#### 三次全量对比

| 判定 | R82 | R95 | **R105** |
|---|---|---|---|
| `ok` | 17,127 | 17,498 | **17,612** |
| `compile` | 347 | 285 | **176** |
| `output` | 350 | 43 | **38** |
| `exit` | 1 | 1 | 1 |
| `timeout` | 0 | 1 | **0** |
| 真失败合计 | **698** | **330** | **215** |

其中 `compile` 176 个里 **146 个是政策类**（`implicit declaration`，C23 已删除的旧形式，只记录）
—— **真正需要修的编译缺陷只剩 30 个**。日志 `~/cxxwork/logs/fj-full.log`，逐例结果 `fj-full3.results`。

#### 工作清单现在自带分类

`doc/fj-triage.sh` 新增一步：对每个 `output` 差异**用两家各编译运行一遍**，数它们自己打的 `OK`/`NG`，
然后写进工作清单：

| 类别 | 数量 | 读法 |
|---|---|---|
| `ref-ng` | 6 | 参考实现自己报 NG、cxx 全 OK —— **cxx 正确，只记录** |
| `cxx-ng` | **8** | cxx 报 NG、参考全 OK —— **真缺陷** |
| `plain` | 24 | 两家都不打 OK/NG，打印普通数值 |

（这一步在小集合上先验证过，结果与手工分析逐个对得上；期间踩了两个坑：`xargs -I{}` 的参数位置、
以及反引号在**双引号内需转、单引号内不需**——后者一度把报告打成 2 行，已修复并复测。）

#### 这一阶段累计修好的真缺陷（R83–R105）

| 缺陷 | 规模 |
|---|---|
| 位域/联合体初始化与布局（含未命名成员、跨元素单位、类型双关内联形式） | C/0013 **341 → 676/676** |
| `x86_fp80` 常量拼写 | 61 |
| 块作用域 `extern` 声明顶掉同名定义 | 32+ |
| 指针下层限定符（clang 口径：警告并接受） | 5 |
| 类型之后的存储类 + 块内 typedef 多声明符 | 4 |
| `#line` 参数先宏展开 | 4 |
| 纯标签声明在块内新建标签（6.7.2.3p7） | 5 |
| 指针数组被当成字符数组 | 3 |
| 省略花括号穿过“数组的数组” | （自查发现） |
| `is_compatible` 对未命名成员的空指针解引用（崩溃） | 8 |

断言数：`test/conformance.sh` 267 → **278**，全部通过；c2ycov 109/0；c2y 101/0；`make test` exit 0。

#### 下一阶段的工作清单（`doc/fj-worklist.md`，自动生成）

- **真缺陷**：30 个编译拒绝（已按诊断聚类：3 个崩溃、3 个 `#ident`、3 个链接、4 个 `incompatible types when assigning`、…），
  8 个 `cxx-ng` 运行期缺陷，24 个 `plain` 值差异，1 个 `exit`；
- **只记录**：146 个 `implicit declaration`（政策类，§R81）、6 个 `ref-ng`（参考实现自己不符合测试预期）、
  398 个已声明缺口、7,459 个 no-prototype/K&R、2,717 个 OpenMP、8,783 个参考实现也编不过。

### R104 修好：省略花括号穿过“数组的数组” —— ✅ 回应 §R103 末尾发现的旧缺陷

```c
struct T { char c[2][4]; } t = {"abc", "def"};   /* c[1] 原本是 0 */
char g[2][4] = {"abc", "def"};                   /* 文件作用域，一直正常 */
struct U u = {{"abc", "def"}};                   /* 有内层花括号，也正常 */
```

6.7.9p20 允许省略内层聚合体的花括号，列表因此**继续进入该成员的剩余元素**。
成员的初始化器带着**自己的花括号**到达字符串分支，而它只读一个字符串就返回；逗号与 `"def"` 随后在
**结构体**层面被丢掉。文件作用域同形走的是列表路径，所以一直正常。

**修法**：字符串分支在**无花括号且元素类型仍是数组**时，继续消耗逗号后的字符串，
直到数组元素用完或遇到非字符串。一维成员不动（它的逗号属于**下一个成员**）——
这一点用对照例验证过（`struct P { char a[4]; char b[4]; } p = {"ab","cd"}` 仍为 `[ab][cd]`）。

| 验收 | 结果 |
|---|---|
| 三种形状（成员省略花括号 / 文件作用域 / 有内层花括号） | 三家**逐值一致**（`[abc][def]`×3） |
| 对照例（一维成员 + 二维成员） | 三家一致（`[ab][cd]`、`[ab][cd][ef]`） |
| `C/0030 0053 0054` | **1035 of 1044 passed**（7 failed） |
| `test/conformance.sh` | 277 → **278 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

（这个缺陷是用自己的对照矩阵找出的，而不是套件报出来的 —— 正是目标里“更隐秘的 bug”那一类。）

### R103 修好：指针数组不是字符数组 —— ✅ `C/0030` **91/91 全部通过**

```c
struct tag { char *s[2]; } s = {"abc", "def"};
```

`s[0]`、`s[1]` 是 `char *`，各自用字符串字面量初始化 —— 普通的指针初始化。cxx 看到“数组前面是字符串”，
就当成字符数组初始化：`infer_strtype()` 得 `char[4]`，数组的叶子类型是 `char *`，两者不兼容，于是
报 `array of inappropriate type initialized from string constant`（clang 正常编译）。

**修法**：把原来报错的那个判定提到**进入分支之前**：字符串与数组元素类型不相容时，
就不走“字符串初始化数组”这条路，而是交给下面的列表路径逐个元素处理。

| 验收 | 结果 |
|---|---|
| `C/0030/0030_0067.c` | 编译并输出与参考一致（`abc|def|`） |
| **`C/0030` 目录** | **91 of 91 passed, 0 failed** |
| 各种形状（指针数组、结构内指针数组、二维字符数组、`int a[3]="abc"`） | 与 clang **逐个一致**（该报错的两例两家都报） |
| `test/conformance.sh` | 276 → **277 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

#### 本轮又发现一个（已最小化，下一轮）

在做上面的对照矩阵时顺手发现：

```c
struct t2 { char c[2][4]; } t = {"abc", "def"};
printf("%s\n", t.c[1]);      /* 参考输出 def，cxx 输出空 */
```

二维字符数组成员用**花括号串列**初始化时，第二个字符串没有落下去。与本节的修改无关
（新条件对 `char[2][4]` 为真，路径与修前一致），属旧有缺陷。最小复现 `~/cxxwork/repro/preparse/strarr.c` 的 `t2`。

### R102 修好：块内的“纯标签声明”建新标签 —— ✅ `C/0053` 那一簇 5 个文件全部与参考一致

对受影响的 19 个目录做了一次**定向重跑**（比全量快得多），结果显示 R96–R101 已经消掉了
链接类、`x86_fp80`、`#line`、声明符类以及工作清单里的 3 个崩溃。存留的 69 个失败里最大一簇是 `C/0053` 的 5 个输出差异。

#### 根因（6.7.2.3p7）

```c
struct stag { int a; };          /* 文件作用域，大小 4 */
int main(void) {
    struct stag st;              /* 外层类型 */
    struct stag;                 /* 只有标签：在**当前作用域**新建一个不完整标签，并隐藏外层 */
    struct stag *p;              /* 指向**内层**那个 */
    struct stag { char a; };     /* 把它补全，大小 1 */
    sizeof(*p) == 1              /* C/0053/0440 的 TEST2 */
}
```

**“`struct-or-union identifier ;`”形式的声明在块内为当前作用域建新标签**（类型不完整，直到本块把它补全）。
cxx 直接沿着外层找到了那个标签，于是 `p` 指向外层类型（大小 4），TEST2 报 NG 而 clang 报 OK。

**修法**：`find_tag(tag, false)`（只查当前作用域）为空、且在块内、且是纯标签声明时，
新建一个不完整的同名标签并推入当前作用域。

| 验收 | 结果 |
|---|---|
| `C/0053/0053_0440/0441/0442/0444/0445.c` | **全部与参考输出一致** |
| 最小形状（`taghide.c`） | 三家逐值一致（`4 1 1`） |
| `test/conformance.sh` | 275 → **276 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

#### 工作清单分类上的一个收获

对 34 个 `output` 差异逐个在两家下跑一遍、数它们自己打的 `OK`/`NG`，得到三类：

| 类别 | 数量 | 读法 |
|---|---|---|
| **cxx 全 OK、参考有 NG** | 6 | **cxx 正确，参考实现自己不符合测试预期**（如 `0054_0018/0019/0020/0021/0023/0027` 的 `sizeof` 布局）—— 属参考分歧，**只记录** |
| cxx 有 NG、参考全 OK | 10 | **cxx 真缺陷**（本轮修掉其中的 5 个） |
| 两家都不打 OK/NG | 17 | 打印普通数值，逐个看 |

这一分类已写进本节，下一次全量后应并入 `doc/fj-triage.sh` 的输出。

### R101 修好：`#line` 的参数先宏展开 —— ✅ 第三次成功（前两次的原因已查清）

`#line int1`（`#define int1 200`）在 6.10.4 下应先对**整行**宏展开再读行号，clang 接受。
前两次失败的真正原因是 **`expand_macro()` 的契约**：

```c
static Token *expand_macro(Token *dst, Token *list) { … return dst; }   /* 返回的是**链尾** */
```

它是一个**累加器**：写穿目标（`dst = dst->next = …`）并返回构造出的链尾，惯用写法是传哑头、
然后以 `dummy.next` 作链首（预处理器里收集参数那段就是 `for (Token *t = dummy2.next; …)`）。

| 尝试 | 写法 | 结果 |
|---|---|---|
| 1 | `expand_macro(NULL, args)` | SIGSEGV（目标不能为 NULL） |
| 2 | 哑头 + **取返回值当链首** | 单 token 行恰巧对；`#line 300 "renamed.c"` 取到链尾的字符串，报“需要正整数” |
| 3 | 哑头 + **`dummy.next` 作链首**，数值与文件名均取自展开链 | **成功** |

| 验收 | 结果 |
|---|---|
| `C/0048/0048_0085.c` | 判定 **`ok`** |
| `C/0048` | 228 → **229 of 231 passed** |
| 行号与文件名都是宏的用例 | 与 clang **逐字一致**（`42 42 from_macro.c 300`） |
| `test/conformance.sh` | **275 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

### R100 修好：块作用域的 typedef 声明可以有多个声明符 —— ✅ `expected expression` 类清零

R99 已经把 `declspecs()` 修到接受“类型之后的存储类”，但 `C/0053/0053_0416.c` 仍报错：
**块作用域**走到 `SC_TYPEDEF` 那条支路时，它只取一个声明符就停下（非 typedef 的情形走 `declaration()`，能走逗号列表），
于是 `struct { int i; char c; } typedef *stptype, sttype;` 里的逗号被当成语句结束。改成循环后，每个声明符都登记为 typedef。

| 验收 | 结果 |
|---|---|
| `C/0053/0053_0416.c` | **编译通过**（判定 `ok`） |
| `C/0053` | 736 → **738 of 743 passed** |
| 最小形状（struct/union/enum 三种 + 多声明符） | 与 clang 一致（均编译） |
| `test/conformance.sh` | **274 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

#### 同一轮里 `#line` 的两次失败（**均已回退**，类仍开着）

`#line int1`（`#define int1 200`）应先对整行宏展开再读行号（6.10.4，clang 接受）。两次尝试都失败，记录在此：

| 尝试 | 写法 | 结果 |
|---|---|---|
| 1 | `tok = expand_macro(NULL, args)` | **SIGSEGV**：`expand_macro()` 写穿目标（`dst = dst->next = …`），目标不能为 NULL |
| 2 | 传哑头 `Token dummy2 = {}`，且数值与文件名都取自展开列表 | 不再崩溃，但第二条 `#line 300 "renamed.c"` 仍报“需要正整数”—— 展开列表的尾部/续行与原始行的关系还没搞对 |

回退时我的还原脚本切错了花括号，随后用 `git checkout -- src/preprocess.c` 恢复（该文件在版本库里没有其他未提交改动，
只有我这两次坏编辑），并复测：构建通过、套件全绿、`C/0053` 738、`C/0048` 228。

### R99 声明说明符可以任意交错（部分落地）；`#line` 宏尝试崩溃，**已回退** —— ⚠️ C/0048 226 → 228

#### （一）落地：`declspecs()` 接受写在类型**之后**的存储类

6.7.1 的语法是右递归的，说明符可以任意交错，所以 `struct { int i; char c; } typedef *p, q;` 是合法的（clang 接受）。
`declspecs()` 的循环条件只看 `is_typename()`，而存储类关键字不算 typename —— 于是类型后的 `typedef` 被当成了**声明符名**。
加上 `|| sc_table[tok->kind]` 后，`C/0048` 从 226 → **228 of 231**。

#### （二）还差一步：块作用域的语句判定不认这种写法

`C/0053/0053_0416.c:16` 仍报 `expected expression before ‘,’`—— 该行是在**函数内**，
语句层的“这是不是一个声明”判定没把 `类型 + 存储类` 算进去，于是整行走了表达式语句。
最小复现 `~/cxxwork/repro/preparse/scafter2.c`（声明部分 clang 接受；下一行的错误是我测试文件自己写错，已核对）。

#### （三）`#line` 的宏尝试：崩溃，已回退

`#line int1`（`#define int1 200`）在 6.10.4 下应先宏展开再读行号，clang 接受。我改成
`tok = expand_macro(NULL, args)` 后，该文件从“报错”变成 **SIGSEGV**（`expand_macro()` 的契约不是 dst 可为 NULL），
于是**回退**，代码回到本轮开始的状态。类仍开着（4 个），下一次要按 `expand_macro()` 的实际接口来写。

| 验收 | 结果 |
|---|---|
| `C/0048` | 226 → **228 of 231 passed** |
| `C/0053` | 736 of 743（不变） |
| `test/conformance.sh` | **274 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

### R98 修好：指针下层的限定符是诊断，不是拒绝 —— ✅ `C/0137` **348/348 全部通过**

工作清单第三类（5 个 `incompatible types when assigning`）的代表 `C/0137/0137_0517.c`：

```c
volatile long long int **a;
long long int *b;
a = &b;                       /* 限定符在**一层指针之下** */
```

| | 行为 |
|---|---|
| gcc | **报错**（`assignment to ‘volatile long long int **’ …`） |
| clang | **警告并接受**（`assigning to 'volatile long long **' from 'long long **' discards qualifiers'`） |
| cxx（修前） | 报错 `incompatible types when assigning` ✗ |

探针以 clang 为基准，且 6.5.16.1p1 只要求“指向**有限定符或无限定符版本**的兼容类型”，
所以 cxx 取 clang 的答案：**诊断保留，但不拒绝**。

**修法**：新增 `agrees_ignoring_qualifiers()`（递归忽略**每一层**指针下的限定符），
并让 `is_assignable()` 的指针分支用它；原有的 `discards qualifiers` 警告在两种方向都会发出。
旧断言“指针下层的限定符陷阱”（断言**拒绝**）已按新行为更新，保留“必须有诊断”这一点。

| 验收 | 结果 |
|---|---|
| `C/0137/0137_0517.c` | 编译并运行，输出 `kaimk2050-ok`（与参考一致） |
| **`C/0137` 目录** | **348 of 348 passed, 0 failed** |
| 两个限定符形状（`volatile` 加入、`const` 丢弃） | 均**警告并接受**（与 clang 同口径） |
| `test/conformance.sh` | 274 passed / 0 gap（断言更新后仍全绿） |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

### R97 修好：块作用域 `extern` 声明顶掉了同名定义 —— ✅ 工作清单里 32 个链接失败归零

工作清单第二大类（`undefined reference to ‘a’`，32 个）的代表 `C/0053/0053_0653.c`：

```c
int f(void) { extern int a; a = 10; return a; }   /* 块作用域的 extern */
int a;                                            /* 文件作用域的定义 */
```

**同一个名字下有两个符号**：块作用域的 `extern` 声明（`SC_EXTERN`）与文件作用域的暂定定义。
模块列表里**声明在前**，而发射器的 `already_emitted()` 按名字去重时记下了它的名字 ——
于是**定义被丢弃**，单元里什么也没定义，链接报 `undefined reference to ‘a’`。

修法（两步，第二步是必需的）：

1. 发射前先收集本模块**定义**的名字（`is_definition()`：函数看 `body`，对象看 `SC_EXTERN`）；
2. 凡是已被定义的名字，**其声明一律不打印** —— LLVM 不允许一个全局名字既声明又定义（先打声明再补定义会得到
   `redefinition of global '@a'`，第一版修法就是这么错的）；定义本身就是声明。

| 验收 | 结果 |
|---|---|
| `C/0053/0053_0653.c` | 链接并运行，输出 `TEST OK`（与参考一致） |
| `C/0053` 目录 | 720 → **736 of 743 passed** |
| 三个最小形状（文件作用域 extern、无 extern、块作用域 extern） | 均链接并运行 |
| `test/conformance.sh` | 273 → **274 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

### R95 全量重跑（第二次）与 R96 最大的真缺陷：`x86_fp80` 常量 —— ✅ 17,498 通过（+371），失败 698 → **330**

#### 全量结果（`~/cxxwork/logs/fj-full.log`，逐例结果 `fj-full2.results`）

| 判定 | R82 基线 | 现在 |
|---|---|---|
| `ok` | 17,127 | **17,498** |
| `compile` | 347 | **285** |
| `output` | 350 | **43** |
| `exit` | 1 | 1 |
| 真失败合计 | **698** | **330** |
| `timeout` | 0 | 1 |
| （范围外）`skipped` / `noproto` / `reffail` / `gap` | 2,717 / 7,459 / 8,783 / 398 | 同前 |

工作清单已用 `doc/fj-triage.sh` 从新结果重生成为 `doc/fj-worklist.md`（`compile` 285 个按诊断聚类）。

#### R96：61 个 `floating point constant does not have type 'x86_fp80'`

现场（`C/0048/0048_0156.c`，文件里只有 `long double lda;`）：

```llvm
%tmp162 = load x86_fp80, ptr %tmp21, align 16
%tmp163 = fcmp oeq x86_fp80 %tmp162, 0x0000000000000000   ; ✗ double 位型
```

**两个打印点都不看类型**：

1. `printcon()` 的 `CBits` + 浮点分支一律 `0x%016lx`（只有 `CBits128` 那条路才按类型分派）；
2. `print_operand()` 的 `RInt` + 浮点分支（注释里就写着“e.g. fcmp with 0”）同样一律打 double 位型 —— **这一行就是元凶**。

LLVM 按**指令类型**读立即数，所以字面量必须按该类型拼写。修法：

- `printcon()` 的 `CBits` 浮点分支按类型分派，宽类型（`long double` / `fp128`）改用**十进制字面量**（LLVM 按使用处类型读取），零与非有限值保留精确拼写；
- `print_operand()` 的那一分支改为**调用 `printcon()`**（一个真源）；
- 新增 `print_operand_as()`：指令第二操作数是立即数时按**指令类型**打印。

| 验收 | 结果 |
|---|---|
| `C/0048/0048_0156.c` | 从 `floating point constant does not have type 'x86_fp80'` 到**编译通过** |
| `C/0048` 目录 | 221 → **226 of 231 passed** |
| `long double` 与常量比较的端到端用例 | 与 gcc/clang **逐值一致** |
| `test/conformance.sh` | 272 → **273 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

#### 接下来的工作清单（`doc/fj-worklist.md`）

| 数量 | 诊断 | 性质 |
|---|---|---|
| 146 | `implicit declaration of function ‘X’` | **政策类**（C23 已删除的旧形式），只记录 |
| 32 | `link: undefined reference to ‘a’` | 待查 |
| 5 | `incompatible types when assigning` | 待查 |
| 4 | `#line directive requires a positive integer argument` | 待查 |
| 4 | `expected expression before ‘X’` | 待查 |
| … | （其余见清单） | |

以及早前已知但本次未动的：`int m2:20` 之后成员丢值、下一次全量跑会确认 R93/R96 的净效果。

### R93/R94 结构体重复定义的崩溃（修好）与一条被纠正的改动 —— ✅ 崩溃已修，诊断改动**已回退**

#### （一）真缺陷：比较两个定义时解引用了无名成员的名字

```c
struct L2 { unsigned char :0; unsigned char :0; unsigned char m3; };
struct L2 { unsigned char :0; unsigned char :0; unsigned char m3; };
```

`is_compatible()`（`type.c:673`）比较成员时直接读 `m1->name->id`，而**未命名位域没有名字**（匿名的
struct/union 成员同理）—— 空指针解引用，SIGSEGV（栈：`record_decl` → `is_compatible`）。两家参考实现都能正常给出判断，
cxx 则崩在比较里。

**修法**：名字缺省时用 0 作为“无名”（标识符的 interned id 不会是 0），两个无名成员相等、无名与有名不等。

#### （二）一条被用户纠正的改动：相同的重复定义在 c2y 是**合法**的

我一度把“同一作用域内重复定义”改成一律报错，依据是我用 **`-std=c17`** 测出的两家行为：

| 形状 | `-std=c17` | `-std=c23` / `c2y` |
|---|---|---|
| **完全相同**的重复定义（struct / union / enum） | gcc 报错、clang 报错 | **gcc 接受、clang 接受** |
| **冲突**的重复定义 | 报错 | 报错 |

C23 起允许同一个结构体/联合体/枚举以**相同的定义**再定义一次，只有冲突的才是错误。
cxx 的目标是 **N3685（c2y）**，所以原来的宽容是对的，那条改动**已回退**。实测证据：它还把 `make test`
弄红了，回退后又绿。保留的是崩溃修复与**冲突定义仍报错**；新增断言把 c2y 语义固定下来。

| 验收 | 结果 |
|---|---|
| 相同的重复定义（struct/union/enum） | 三家在 `-std=c23` 下**均接受** |
| 冲突的重复定义 | 三家**均报错** |
| `test/conformance.sh` | 271 → **272 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0；C/0013 **676/676** |

### R91 修好：跨元素边界的位域 —— ✅ **C/0013 全部 676 个用例通过**

R90 回退的原因很快查清了：实验版用 **访问单位的大小** 算元素末尾，而单位可以比位宽得多：

```c
struct S { unsigned char m1:5; unsigned long m2:29; unsigned char m3; };
```

`m2` 的位占字节 0–4，但 5+29=34 位使 `get_unit_ty` 选中 **8 字节**的单位，
于是“尾巴”把 `m3` 所在的字节 5 也吞掉了 —— 20 个形状因此丢值（`m3` 读出 0）。

**修法**：元素的末尾取**该成员的位实际占到的字节**：
`ceil((bit_offset + width) / 8)`，而不是单位的大小。三处遍历统一改为该判据。

| 验收 | 结果 |
|---|---|
| **C/0013（676 个用例）** | **676 of 676 passed，0 failed** |
| 最小用例 | `straddle.c`（跨边界位域数组、宽单位位域、匿名宽位域、局部与全局）与两家逐值一致 |
| `test/conformance.sh` | 270 → **271 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

#### 八轮累计（R82–R91，C/0013 位域/联合体家族）

| | R82 全量时 | 现在 |
|---|---|---|
| `ok` | 341 | **676** |
| `output` 差异 | 303 | **0** |
| `compile`（崩溃） | 32 | **0** |

#### 一个误报（已更正）：那个“局部初始化器 bug”是我自己测试写错了

```c
struct S { unsigned char m1:5; unsigned long m2:29; unsigned char m3; };
struct S loc = { 2, 2000, 9 };      /* 局部 */
```

| | `loc.m3` |
|---|---|
| gcc / clang | **9** |
| cxx | **140724603453449** |

但那是**测试自己的错**：`printf("%lu …", …, loc.m3)` 里 `loc.m3` 是 `unsigned char`，没有转型，
按 32 位传入却按 64 位读取——未定义行为（两家参考实现恰好打印出 9）。加上转型后三家一致：

```
gcc    1 1000 7 | 2 2000 9
clang  1 1000 7 | 2 2000 9
cxx    1 1000 7 | 2 2000 9
```

局部路径（`create_lvar_init`）本来就是对的；断言已把局部情形收回。

（以下是当时的记录，保留作为“先怀疑自己的测试”的例子）

全局镜像已经修好（本节上面的 676/676），但**局部变量的运行时初始化**（`create_lvar_init`）
有它自己的遍历，同一形状仍错。最小复现 `~/cxxwork/repro/preparse/strloc.c`。

#### 下一步：全量重跑

家族已收尾，应重跑一次全量（`FJ_LIMIT=0`，约 75 分钟，
`FJ_RESULTS=~/cxxwork/logs/fj-full2.results`），取得新的总数，并用 `doc/fj-triage.sh`
重新生成工作清单（预计 698 个失败里的 372 个已消）。剩下已知的：
58 个 `x86_fp80`、`int m2:20` 之后丢值、结构体重定义时崩溃。

### R90 剩下的 5 个差异定性完成；修法试了一版，**实测更差，已回退** —— ⚠️ 结论保留，代码回到 R89

C/0013 剩下的 5 个单行值差异是**同一种形状**：

```c
struct D { unsigned long m1:3; unsigned long :3; unsigned long m3:3; };
struct D d1[2] = {{ 1, 2 },{ 3, 4 }};
```

`m3` 从位 6 开始、跨到位 8，所以它的访问单位是**两字节**；而它的**字节偏移**（第 0 字节，首位所在）已在游标之后（第 1 字节，`m1` 单位的末尾），
三处遍历都把它当“在前一个成员的单位里”而**跳过**。结果是镜像只有一个字节：

| | `%struct.D` | 第二个元素的值 |
|---|---|---|
| clang | `{ i8, i8, [6 x i8] }` | `{ i8 3, i8 1, … }`（`m3` = 4） |
| cxx | `{ i8, [7 x i8] }` ✗ | `{ i8 3, … }`（`m3` = **0**） ✗ |

最小复现 `~/cxxwork/repro/preparse/arr2.c`（`d1`/`d2` 两例与两家不一致）。

#### 试过的修法：把跨元素的单位按逐字节 `i8` 展开

三处遍历的跳过条件从 `off < pos` 改为“单位末尾在游标之后才算有元素”，
跨边界的单位只补出尾巴那几个字节（一字节一个 `i8`，与 clang 同形）。

| 指标 | 修前（R89） | 试修后 |
|---|---|---|
| `arr2.c` 的 `d1`/`d2` | 与两家不一致 | **一致** ✅ |
| `%struct.D` | `{ i8, [7 x i8] }` | **`{ i8, i8, [6 x i8] }`** ✅（与 clang 同） |
| **C/0013 通过数** | **671** | **656** ✗（净亏 15） |

—— 目标形状确实修好了，但另有十五个形状受损，于是**回退**，
C/0013 回到 671、`test/conformance.sh` 回到 270/0（均已复测确认）。

#### 下一步（窄化后再试）

“单位跨元素”这个事实成立、最小复现也在，需要的是**只在真正跨边界时**才动那条路径：
当前实现把所有 `off < pos` 且单位延到游标之后的情形都当成“跨边界”，而其中不少是
**同一字节内的另一个单位**（如前例的 `:3`），它们本该继续被跳过。判据应是“该成员的位**越过了当前元素的末尾**”，
而不是“单位尾巴在游标之后”。

### R89 修好：类型打印器与值打印器全面一致 —— ✅ C/0013 通过数 667 → **671**，编译失败 **0**

R88 给 `dump_init()` 的联合体分支加了 `init_is_punned()` 判断，但 **`print_init_ty()` 的联合体分支漏了**（它还用旧的
`mem == canon`），于是（`struct HOLD { int tag; union OUT o; }`）：

| | 类型 | 值 |
|---|---|---|
| clang | `{ i32, [4 x i8], { { i8, [7 x i8] } } }` | `… { { i8, [7 x i8] } } { … }` |
| cxx（修前） | `{ i32, [4 x i8], %union.OUT }` ✗ | `… { { i8, [7 x i8] } } { … }` ✗ |

LLVM：`element 2 of struct initializer doesn't match struct element type`。

**修法**：`print_init_ty()` 的联合体分支用与 `dump_init()` 完全相同的条件
（`mem == canon && !init_is_punned(child, mem->ty)` 才写规范拼法）。两个打印器自此在所有形状上一致。

| 验收 | 结果 |
|---|---|
| **C/0013（676 个用例）** | `ok` 667 → **671**；`compile` **4 → 0**（八个文件全部编译通过）；`output` 5 |
| 嵌套用例 | `nestun.c` / `holdpunned.c`（联合体套联合体、结构体包类型双关联合体、局部与全局）与 gcc/clang **逐值一致** |
| `test/conformance.sh` | 269 → **270 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

#### 八轮累计（R82–R89，同一个位域/联合体家族）

| C/0013（676 个用例） | R82 全量时 | 现在 |
|---|---|---|
| `ok` | 341 | **671** |
| `output` 差异 | 303 | **5** |
| `compile`（崩溃） | 32 | **0** |

#### 剩下的

| 形状 | 现状 |
|---|---|
| C/0013 的 5 个单行值差异（0013_0611/0616/0630/0633/0645） | 如 `8` vs `0`、`6` vs `2`、`4` vs `0`—— 又是一种初始值落错成员的形状，待缩小 |
| `int m2:20` 之后的成员丢值（§R85） | 仍在 |
| 结构体重定义时崩溃（§R85） | 仍在 |
| 全量重跑 | 本阶段结束前应重跑一次，以取得新的总数 |

### R88 修好：类型双关的判断统一到一个谓词 —— ✅ C/0013 通过数 663 → 667，编译失败 8 → 4

上一轮的线索是“类型前缀是规范拼法、值是内联拼法”。用 trace 发现：
**`dump_init()` 的联合体分支根本不调用 `init_needs_inline()`**，它自己用 `mem != canon` 判断；而成员的**值**
是由 `dump_init(child, mem->ty)` 写的，同样的判断在**下一层**做一次。两层意见不一致时 LLVM 报
`element 0 of struct initializer doesn't match struct element type`。

**修法**：新增递归谓词 `init_is_punned()`（联合体选中非规范成员、或成员自己又是这样），
三个打印器（`dump_init`、`print_init_ty`、`print_union_elem_ty`）都用它；并让 `print_union_elem_ty`
用**初始化器感知的** `print_init_ty(child, mem->ty)` 而不是 `print_type()`。

| 验收 | 结果 |
|---|---|
| **C/0013** | `ok` 663 → **667**；`compile` **8 → 4**（`0013_0598/0602/0603/0604` 现已编译）；`output` 5 |
| 最小用例 | `usub.c`（三行）编译通过；类型与值都是内联拼法 |
| 回归集 | 全部保持正确 |
| `test/conformance.sh` | 268 → **269 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

#### 剩下的（下一轮）

| 形状 | 现状 |
|---|---|
| `C/0013` 的 4 个文件（0013_0665/0669/0670/0671） | 仍报 `element 0 of struct initializer …`（另一形状，待缩小） |
| **结构体里含类型双关联合体** | 本轮新发现：`struct HOLD { int tag; union OUT o; }`（OUT 同上）报 **`element 2 of struct initializer …`**—— 类型与值的**元素走法不一致**（填充元素的位置）；最小复现 `~/cxxwork/repro/preparse/nestun.c` |

### R87 联合体的“默认成员”与类型双关内联形式 —— ⚠️ 三处改动合乎规范，最后 8 个文件仍失败

目标是 §R86 剩下的 8 个文件（`element 0 of struct initializer …`）。本轮做了三件事，
都是规范上应该的（回归集全部保持正确、套件全绿），但**没有移动那 8 个文件**：

1. 新增 `union_default_member()`（`type.c` + `cxx.h`）：初始化器未命名成员时取**第一个具名成员**（6.7.9p9）。
   原来有**五处**（`dumpir.c` ×2、`parser.c` ×3）回退到 `ty->members`，即那个未命名位域；
2. `union_initializer2()`（无花括号路径）同样改用该助手；
3. `init_needs_inline()` 的联合体分支不再提前返回：选中的成员若本身是聚合体，
   它自己的初始化器也可能需要内联形式（联合体套联合体），那么包含它的类型也要一致。

| 验收 | 结果 |
|---|---|
| 回归集（以未命名位域开头、联合体位域、宽单位位域） | 全部编译并与两家逐值一致（无回归） |
| `test/conformance.sh` | **268 passed / 0 gap**（不变） |
| C/0013 | `ok` 663 不变；8 个 `compile` 失败依旧（报错**列号变了**，说明输出确实变了） |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

#### 下一步已缩到一个点

IR 现场（`0013_0602.c`）：

```llvm
%union.FUJW00HU00 = type { %union.SUB_UHB00HW00 }
@fujw00hu00_0 = global %union.FUJW00HU00 { { i8, [7 x i8] } { i8 1, [7 x i8] zeroinitializer } }, align 8
```

**类型前缀是规范拼法，而值是内联（类型双关）拼法**，两者必须一致。类型前缀由
`dump_data → dump_init → print_init_ty → init_needs_inline` 选定，而它在这个**嵌套**情形返回了 false：下一步是看
`init->is_inited` / `init->mem` 在嵌套子初始化器上的传递（`init_needs_inline` 开头就是
`if (!init || !init->is_inited) return false;`）。

复现：`~/cxxwork/repro/preparse/usub.c`（简化版反而正确，说明缺的就是嵌套那一层）与 `0013_0602.c`。

### R86 修好：花括号路径只跳一个未命名成员 —— ✅ C/0013 通过数 **501 → 663**

上一轮的线索是“`{ 1 }` 走的不是 `struct_initializer2`”。果然：`initializer2()` 把**花括号**的
struct 初始化发给 `struct_initializer1()`，而它有同样的“只跳一步”错误（R83b 只修了无花括号那条路）：

```c
if (mem && !mem->name && !is_record(mem->ty)) mem = mem->next;   /* 只跳一个 */
```

两个未命名成员在前时，值被写进**第二个**，真正的成员保持 0。改成 `while`，并把联合体“初始化第一个**具名**成员”（6.7.9p9）一并对齐。

随后连带修好三个联合体边角（均由此处变得可达）：

1. `union_initializer1()` 跳完可能走到列表尾（全部成员未命名）—— 保留首成员，不置 NULL；
2. `dump_union_elem()` 拿到**没有值的成员**（未命名位域）时传了 NULL Con 给 `print_union_con()` —— 改为打印该类型的零；
3. 同一函数的聚合分支漏了 **`TY_UNION`**，于是把“成员本身是联合体”当标量打成 `%union.X 0`，LLVM 报
   `integer/byte constant must have …`；
4. `union_canon_member()`（类型双关的那个成员）会选中**未命名位域**，现只在具名成员里选。

| 验收 | 结果 |
|---|---|
| **C/0013（676 个用例）** | `ok` 501 → **663**；`output` 差异 173 → **5**；`compile` 2 → 8（下述） |
| 回归集 | 以未命名位域开头的全家族（含三连 `:0`、联合体首成员为未命名位域、局部变量）与两家逐值一致 |
| `test/conformance.sh` | 267 → **268 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

四轮累计（R83–R86）在这一族上：`ok` **341 → 663**，`output` **303 → 5**。

#### 剩下的（下一轮）

| 形状 | 现状 |
|---|---|
| `C/0013` 的 8 个文件（0013_0598/0602/0603/0604/0665/0669/0670/0671） | **已从崩溃变成有诊断的失败**：`element 0 of struct initializer …`—— 即类型双关的**内联形式**（`init_needs_inline` 与发射的判断不一致） |
| 5 个输出差异 | 待样本化 |
| `int m2:20` 之后的成员丢值 | 仍在（§R85） |
| 结构体重定义时崩溃 | 仍在（§R85） |

### R85 修好：位域单位落在自身对齐之外 —— ✅ C/0013 通过数 425 → 501

`unsigned long m2:29` 这一族（剩余 249 个输出差异的主体）。IR 一目了然：

```llvm
%struct.E = type { i8, i32, i8, [2 x i8] }        ; 非 packed
@e = global %struct.E { i8 1, i32 2, i8 3, ... }
```

**LLVM 把元素放在它自己对齐要求的偏移上**：那个 `i32` 会落在字节 4；而 cxx 的元素就是访问单位、
放在**位所在的地方**（字节 1）。初始化器写在一处、读取读另一处，于是 `e.m2` 读出 `2 << 24`。

**修法**：位域单位落在非自身对齐处时置 `lowered`，即设 `layout_packed`——正是 R64
为“C 布局 ≠ LLVM 自然布局”建的机制：`<{ … }>` 的元素就在写它们的地方。

| | 修前 | 修后 |
|---|---|---|
| `%struct.E` | `{ i8, i32, i8, [2 x i8] }` | `<{ i8, i32, i8, [2 x i8] }>` |
| `e.m2` | `33554432` | **`2`** |

| 验收 | 结果 |
|---|---|
| **C/0013（676 个用例）** | `ok` 425 → **501**；`output` 249 → **173**；`compile` 2 不变 |
| 最小用例 | `unsigned long m2:29` / `unsigned long :29` / `unsigned short m2:9` / `int m2:20`，全局与局部都与两家逐值一致 |
| 旧用例 | R83/R84 的复现集全部保持正确 |
| `test/conformance.sh` | 266 → **267 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

三轮累计（R83+R84+R85）在这一族上：`ok` **341 → 501**，`output` **303 → 173**，`compile` **32 → 2**。

#### 剩下的（下一轮，已精确到一步）

| 形状 | 现状与线索 |
|---|---|
| **以两个连续未命名位域开头**（`struct { unsigned char :0; unsigned char :0; unsigned char m3; } x = { 1 };`） | 仍得 0；局部与全局**都错**，而指定初始化 `.m3 = 1` 正确 → **顺序遍历未赋值**。临时 trace（已撤）显示成员表为 `[:0, :0, m3]`（非记录），但 `struct_initializer2()` 循环体**根本没进去** —— 下一步是找 `{ 1 }` 走的哪条路 |
| `C/0013/0013_0598.c`、`0013_0665.c` | 仍崩溃（待缩小） |
| **重定义结构体时崩溃** | 本轮撞见：同一个 `struct L2` 定义两次时 cxx **SIGSEGV**，而应当报 `redefinition of struct or union` —— 另一个真缺陷 |
| **20 位的 `int` 位域之后的成员丢值** | 本轮新发现：`struct N { unsigned char m1; int m2:20; unsigned char m3; } n = { 1, 70000, 3 };` 的 `m3`，两家给 3、cxx 给 **0**（`m2` 自己正确） |

### R84 修好：联合体的位域成员缺少访问单位 —— ✅ 崩溃族 32 → 2

R83 留下的头号目标。最小复现：`union U { unsigned long long m:3; } u = { 1 };` → SIGSEGV。

`layout_struct()` 对联合体是这么写的：

```c
if (is_union) { offset = MAX(offset, mem->ty->size); continue; }
```

—— 每个成员从位 0 开始，所以不走下面的偏移计算；但**这个 `continue` 跳过了唯一设置
`mem->unit_ty` 的分支**，于是联合体里的位域成员带着 `unit_ty == NULL` 进入读、写与镜像发射。
结构体路径不受影响（它会设单位），所以同一声明写在结构体里一直正常。

**修法**：联合体分支里补上位域的处理（`offset = 0`、`bit_offset = 0`、
`unit_ty = get_unit_ty(min_bytes_for_bits(width), is_unsigned)`），尺寸仍用声明类型的大小。

| 验收 | 结果 |
|---|---|
| **C/0013（676 个用例）** | `ok` 401 → **425**；`compile`（崩溃）**32 → 2**；`output` 243 → 249（那 6 个从“编不过”变成“跑得出但输出不同”） |
| 崩溃集 | `bf5/c1/c2/c3/c5/c7/bfuv` 全部编译通过；运行值与 gcc/clang **逐值一致**（`5 3 171 -15`） |
| `sizeof` 基线 | 9 种形状三家一致（`U1 8 U2 4 U3 1 U4 8 \| S1 8 S2 4 S3 1 S4 8 \| U5 8 S5 8`） |
| `test/conformance.sh` | 265 → **266 passed / 0 gap** |
| 其余 | c2ycov 109/0；`make test` exit 0 |

#### 这一族剩下的（下一轮）

| 形状 | 现状 |
|---|---|
| `C/0013/0013_0598.c`、`0013_0665.c` | 仍崩溃（另一形状，待缩小） |
| **宽单位的具名/匿名位域**（`unsigned long m2:29`、`unsigned long :29`） | 镜像对、**读取错位**（`m2` 读出 `2<<24`）—— 剩下 249 个输出差异的主体 |
| 连续两个 `:0` 后的成员 | `struct T { uchar m1; uchar :0; uchar :0; uchar m3; } t = {1,2};` 仍得 `1 0` |

### R83 修好：零宽位域不拥有元素 —— ✅ C/0013 的输出差异 303 → 243

从 §R82 的工作清单头号目标入手。定位过程：

1. 最小复现（三行）：`struct { unsigned char m1; unsigned char :0; unsigned char m3; } x = { 1, 2 };`
   —— 参考输出 `1 2`，cxx 输出 `1 0`。
2. 局部变量（运行时初始化）**正确**，只有**全局静态数据**错—— 所以 layout 对，问题在“初始化器 → 字节”。
3. 渐进判别：连指定初始化 `.m3 = 2` 也丢，而 `{ 1 }` 三家一致 →— 值没进到镜像里。
   临时 trace（已撤）给出关键事实：零宽位域的 `offset` 与**其后成员相同**（都是 1），
   而 layout 本身是对的（`sizeof`/`offsetof` 三家一致）。

**根因**：发射阶段把零宽位域当成一个**元素**，推进了游标，紧接着真正住在那个字节的成员
就因 `off < pos` 被**跳过**，值和字节一起丢。同一原因让**类型**多出一个元素：`struct { unsigned char m1; unsigned char :0; };`
会发成 `{ i8, i8 }`，而对象只有 1 字节。

**修法**：`member_has_element()`（`!m->is_bitfield || m->bit_width > 0`），在三个遍历成员找元素的地方
都加上：`dump_type()`、`print_init_ty()`、取值镜像那个循环。同时把 `struct_initializer2()` 的“跳过无名成员”
从一次改成一个 `while`（连续两个 `:0` 时只跳一次会把值写进第二个零宽位域）。

| 验收 | 结果 |
|---|---|
| **C/0013（676 个用例）** | `ok` 341 → **401**；`output` 差异 303 → **243**；`compile`（崩溃）32 不变 |
| 最小用例 | `A`/`A2`（含指定初始化）、尾部零宽、三者类型都与两家一致；`@s` 的 IR 与 clang 同形 |
| `test/conformance.sh` | 264 → **265 passed / 0 gap** |
| 其余 | c2ycov 109/0；c2y 101/0；`make test` exit 0 |

#### 这一族剩下的（下一轮的目标）

| 形状 | 现状 |
|---|---|
| **联合体 + 位域成员** | 仍崩溃（最小：`union U { unsigned long long m:3; } u = {1};`）—— C/0013 的 32 个 `compile` 失败就是它 |
| 连续两个 `:0` 后的成员 | `struct T { uchar m1; uchar :0; uchar :0; uchar m3; } t = {1,2};` 仍得 `1 0` |
| 单位 4 字节的具名位域 | `struct E { uchar m1; unsigned long m2:29; uchar m3; } e = {1,2,3};` 的 `m2` 读出 2<<24（镜像对、读取错位） |

三者都已有最小复现，记在 `~/cxxwork/repro/preparse/bf*.c`。

### R82 回到 debug：compiler-test-suite **全量**（37 190 个用例）—— ✅ 找到抽样完全碰不到的 bug

运行：`FJ_LIMIT=0 FJ_JOBS=8 FJ_RESULTS=~/cxxwork/logs/fj-full.results bash doc/fujitsu.sh ./cxx`，
日志 `~/cxxwork/logs/fj-full.log`，约 75 分钟（比抽样估算的 33 分钟慢：全集里“完整流程”的用例占比更高）。

#### 结果（各类相加正好 37 190）

| verdict | 数量 | 性质 |
|---|---|---|
| `ok` | 17 127 | 通过 |
| `reffail` / `refcrash` / `reftimeout` | 8 783 / 5 / 3 | 参考实现（clang）也编不出或跑不了 |
| `noproto` | 7 459 | 无原型/K&R 与虚数后缀（已屏蔽） |
| `skip` | 2 717 | OpenMP（已屏蔽） |
| `gap` | 398 | 功能缺口：274 `()` 无原型 + 73 `__sync_*` + 51 SSE/MMX（只记录） |
| **`output`** | **350** | **真缺陷**：输出与参考不一致 |
| **`compile`** | **347** | **真缺陷**：cxx 拒绝编译 |
| **`exit`** | **1** | **真缺陷**：退出码不一致 |
| `timeout` | 0 | 无 |

工作清单由新探针 **`doc/fj-triage.sh`** 自动生成到 `doc/fj-worklist.md`：
它把 `compile` 失败逐个重跑 cxx 取诊断并聚类，把 `exit`/`output` 按目录归并，把缺口单列。

#### 编译被拒的前几类

| 数量 | 诊断 | 性质 |
|---|---|---|
| **69** | `cxx killed by signal 11` | **崩溃**（本轮已最小化，见下） |
| **58** | `floating point constant does not have type 'x86_fp80'` | 非法 IR：长双精度常量发错 |
| 146 | `implicit declaration of function ‘X’` | **政策类**：C23 删除了隐式声明，与 `()`/K&R 同一性质（cxx 不管 `-std=` 都按 C23 判） |
| 42 | （原本计作“无错误行”） | 实为**链接失败**：探针的 compile 一步同时链接，链接器不输出 `error:` |
| 5 / 4 / 4 / 3 / 3 / 2 / 2 | 其余小类 | `incompatible types when assigning`、`#line` 非正整数、`expected expression before`、`invalid preprocessor directive`、字符串初始化数组类型不符、非常量等 |

#### 本轮已最小化的崩溃：联合体里的位域

```c
union U { unsigned long long m:3; } u = {1};
int main(void) { return (int)u.m - 1; }
```

触发条件（逐条测过）：

| 写法 | cxx |
|---|---|
| **联合体 + 位域成员**（局部、全局、带/不带初始化器、单个或多个位域、`int` 或 `unsigned long long`） | **崩溃** |
| 结构体 + 位域（`struct S { int m:3; } s = {1};`） | 正常 |
| 联合体 + 普通成员 | 正常 |
| 两家参考实现 | 全部接受 |

见证文件 `C/0013/0013_0520.c`（联合体里不同类型的位域 + 未命名位域 + **零宽位域**）。下一轮从这里入手。

#### 一个根因解释了 698 个失败里的 **372 个**

工作清单的运行期一节显示：350 个 `output` 失败里 **303 个在 `C/0013`**，而 69 个崩溃里的证人
`C/0013/0013_0520.c` 也在同一目录。`C/0013` 全目录 676 个 `.c`，**每一个都含位域**。抽一个输出失败的看：

```c
struct ASHB00JB00HB00 {
    unsigned char m1;
    unsigned char   :0;      /* 零宽位域 */
    unsigned char m3;
} x = { 1, 2 };
```

| | 输出 |
|---|---|
| 参考实现 | `1 2` |
| cxx | **`1 0`** |

—— **未命名位域（含零宽位域）在 cxx 里占了一个初始化器槽位**，第二个初始值被写进了
没有名字的位域，`m3` 因此保持 0。6.7.9p9 说未命名成员**不参与初始化**。

同一族还包含崩溃的那些形状（联合体 + 位域）。两者合计 **372 / 698**，是下一轮的第一个目标。

#### 新探针与两个改动

- `doc/fj-triage.sh`：从 `FJ_RESULTS` 的逐例结果生成 `doc/fj-worklist.md`。
- `doc/fujitsu.sh`：新增 `FJ_RESULTS=<file>`（逐例结果原本在 mktemp 目录里，退出时连同目录一起删掉）；
  另让每个用例在**工作目录**里运行——全量跑完后仓库根多出了 `test1.txt` … `test5.txt`，
  那是写文件的用例写在了探针所在的源码树里（已清理）。

### R81 “完全不支持 `f()` 无原型声明”的覆盖面 —— 实测：现代项目上**近乎零**

该策略（§0、§R56/R57）拒绝的是**两种写法**：
① 对一个**无原型声明**（`int f();`）的函数**带实参调用**；② **K&R 式定义**。
C23 把 `()` 归于 `(void)`、删除了 K&R 定义，两家参考实现在 C23 下同样报错；cxx 的差异在于它**在所有 C 版本下都这么判**。

#### 方法：让参考编译器当裁判（`doc/noproto.sh`）

`f()` 的**空调用**是完全合法的代码，正则无法区分；clang 有一个开关同时命中这两种写法。
新增的探针 `doc/noproto.sh` 把每个翻译单元按**项目自己的命令**（取自 `make -n`，含它自己的 `-I/-D/-include/-std=`）交给
`clang -Werror=deprecated-non-prototype`，拒绝的单元就是策略在该项目里的覆益。

**两道校验**（都是必要的，第一版就是靠它们拯回来的）：

1. **自造样本**：`int f(); f(1);` → `noproto`、K&R 定义 → `kr`、正确原型 → `clean`。
   （提醒：`-Wno-everything` 会**连这个诊断一并关掉**，`-Werror=` 只改严重度、不重新启用；第一版就是这么错的。）
2. **与探针交叉验证**：同一目录下，探针的过滤器与普查结果一致（`C/0006`：独立过滤器测得 0 个真无原型，普查也是 0）；
   另用**真有这两种写法的目录**做阳性对照：`C/0023` 107 单元 → 拒绝 6（全 K&R），`C/0048` 314 单元 → 拒绝 **70**（K&R 36 + 调用形式 34）。

#### 结果：三类代码，三个数量级

| 项目 | 单元 | 被策略拒绝 | 备注 |
|---|---|---|---|
| git 2.47 | 567 | **0** | 全清 |
| FFmpeg | 2 119 | **0** | 1 个单元因其他原因失败 |
| cpython | 207 | **0** | 全清 |
| curl 8.10 | 170 | **0** | 全清 |
| libpng | 78 | **0** | 32 个单元需配置头 |
| redis 7.4 | 151 | **0** | 77 个单元需配置头 |
| lua | 34 | **0** | |
| tinycc | 21 | **0** | |
| zlib | 17 | **0** | |
| **busybox**（整棵树，688 个 `.c`） | 688 | **2** | `shell/ash_test/recho.c`（K&R，**测试目录**）、`shell/random.c`（调用形式） |
| **Fujitsu 测试集**（抽样 1 432 个文件，覆盖全部 203 个目录） | 1 432 | **95 = 6.6%** | K&R **78** + 调用形式 **17** |

→ 当代维护中的大型 C 项目（上表前九行，**3 181 个能干净编译的单元**）里**一个也没有**；
这些项目在 cxx 下的实际构建结果独立印证了这一点（：`doc/realworld.sh` 的失败面里没有一例这两种诊断）。
它们集中在：（a）**编译器测试集**（刻意测试这些旧形式），（b）老项目里的**测试/示例目录**。

#### 一个副产品：探针的 `noproto` 桶里有三种东西

上述抽样顺便量出：探针标为 `noproto` 的用例里，真正的无原型/K&R 只占一部分，
另一大类是 **`_Complex` 虚数后缀**（§0 的“有意不支持”，如 `C/0006` 的 12 个文件）。
两者都不计入失败，但它们的**性质不同**：前者是政策选择，后者是明确不做。本轮把它们分开计数了。

#### 本轮没测到的

| 项 | 原因 |
|---|---|
| busybox 的**构建范围**（默认配置 170 个对象） | 命令提取在本轮未收敛；上表的 2 是**整棵树**扫描，且 688 个单元里 655 个因缺 `libbb.h` 等原因停在更早的阶段，所以 **2 是下界** |
| nginx | `./configure` 需要 PCRE，本机未安装 |
| 内核 | 未下载（其阻塞点是驱动层选项与自我标识，与本策略无关） |
| redis/busybox/libpng 的部分单元 | 需配置头或子目录 flag，本轮算作“其他失败”，不计入两边 |

**结论**：完全不支持这两种写法，对**当代维护中的真实项目几乎零代价**（实测 3 181 个单元 0 命中）；
代价集中在**旧代码与测试集**（测试集样本 6.6%），且那里的主体是 **K&R 定义**（78 对 17）——
而 K&R 定义在 C23 里已被**删除**，两家参考实现也不再接受。cxx 与他们的差别只在于
**不管 `-std=` 是什么都按 C23 判**；若要支持 C17 语义，需要的不是语法能力，而是**对 `-std=` 的应用**。

### R80 文档整理 + 全量跑测试集的时间估算 —— ✅

#### （一）doc 下的 md：三份变两份

| 文件 | 处置 | 依据 |
|---|---|---|
| `doc/n3685-conformance.md` | **删除**（已并入本文与 `c2ycov.sh`） | 基于 commit `0924007` 的手工快照，其“未修复”项**逐条复测均已修**：`_Atomic` 聚合体、N3652 数组补全、`<math.h>` 宏（它称之为“最大缺口”）、`-pthread`、`__STDC_IEC_60559_TYPES__`；活的清单就是 §1 的 P-工作项，自动化对照是 `c2ycov.sh`。文件在 git 里，删除不丢历史 |
| `doc/builtin-redesign.md` | **保留**，更新状态行 | 它仍是内建框架的设计依据（P1 引用）；但“未实现”已过时 —— §3 的声明式内建表与统一折叠已按 P1 落地 |
| `doc/cxx-c2y-plan.md` | **保留**，更新首部依据与 4 处引用 | 它是唯一的计划与轮次记录（§0 范围 + §1 P-工作项 + 79 个轮次节） |

遗留两项已写入 §0：`<stdmchar.h>`（N3366，本机无此头，**无法对照**）、
N3348（泛型关联里的 `[*]`，由 `c2ycov.sh` 覆盖）。

#### （二）全量跑 compiler-test-suite 的时间估算

测得底座：8 逻辑核（i5-10300H，7 GB）。探针每例的工作 = 参考实现编译+链接+运行、
cxx 编译+链接+运行、加一次 clang 预过滤，最后比对 stdout 与退出码。**三个样本实测**：

| 样本 | 用例数 | 构成 | 挂钟 | CPU | 挂钟/例 | CPU/例 |
|---|---|---|---|---|---|---|
| 5 个目录，**串行** | 173 | 153 无原型类 + 3 缺口 | 51.6 s | 38.6 s | 0.298 s | 0.223 s |
| 标准深样本 `FJ_PER_DIR=3` `-j8` | 556 | 61 OMP + 100 无原型 + 14 缺口 + 5 参考编不出 | 29.6 s | 167.0 s | **0.053 s** | **0.300 s** |
| 两个最大目录 `0104 0055` `-j8` | 1888 | 2 OMP + 314 无原型 + 72 缺口 | 102.5 s | 575.3 s | **0.054 s** | **0.305 s** |

两个 `-j8` 样本（构成差异很大）在每例成本上**相差 2%**，并行效率两次都是 **5.6×**。

**测得数据直接外推**：全集 **37 190** 个 `.c`（203 个目录，中位数 58，最大 991）。

| 口径 | 估算 |
|---|---|
| 挂钟 @ `-j8` | 37 190 × 0.0535 s ≈ 1 990 s ≈ **33 分钟** |
| CPU 总量 | 37 190 × 0.30 s ≈ 11 157 s ≈ **3.1 CPU·小时** |
| @ `-j1`（串行） | ≈ 3.1 小时 |

注意：上述计时**是在另一个 8 并发构建（realworld）同时跑的情况下取的**，空闲机器上只会更快；
且两个很不同的样本给出同一个每例成本，所以 **30–40 分钟是稳健区间**。风险在尾部：若全集里超时用例（`FJ_TIMEOUT=20`）比样本多，
或出现大量“两家都慢”的用例，尾部会拉长；三个样本里未见超时。

实跑建议：

```bash
cd ~/cxx
FJ_LIMIT=0 FJ_JOBS=8 bash doc/fujitsu.sh ./cxx > ~/cxxwork/logs/fj-full.log 2>&1 &
# 分批（可中断续跑，DIRS 优先于 LIMIT）：
FJ_LIMIT=0 FJ_DIRS="0104 0055 0053" FJ_JOBS=8 bash doc/fujitsu.sh ./cxx
```

全量跑的价值在**失败面**：两个最大目录里 1 500 个可判定用例有 **139 个失败**（已知缺口 72），
那是现有样本（每目录 3 个，失败 0）看不到的。

### R78 修好：匿名记录的命名 —— ✅ FFmpeg 的 bug 类别归零，debug 阶段结束

最后一个非法 IR：`libavcodec/jpegxl_parser.c` 里两个**不同的匿名结构体**都叫 `%struct.anon.1`，
LLVM 报 `redefinition of type`。

**定位手段**：同样用 `FF_MAKE_ARGS=V=1` 的日志（注意不要用“最近的 CC 行”推断文件，
`-j` 下输出交错；要用“最近一条 `cxx … -c …` 命令”），拿到命令行后用 `-cc1 -cc1-output` 导出 IR：

```llvm
13: %struct.anon.1 = type { i32, i32 }
99: %struct.anon.1 = type { %struct.FFJXLMetadata, %struct.JXLFrame }
```

**根因**（用一次临时 trace 确认，已撤）：匿名记录有**两条进入 `insert_ty()` 的路**，各自计数、
**各自从 `anon.1` 开始**：

- 无标签的 `struct { … }` 在 `parser.c:7493` 被赋 `ty->id = intern("anon")`，走 `struct.anon.<同 id 计数>`；
- 编译器自己造的记录（目标的 va_list、参数聚合体）或尚未取得 id 的副本，`ty->id == 0`，走 `struct.anon.<同 id 计数 + 2>`。

于是一个走第二条路、一个走第一条路的两个结构体都叫 `anon.1`。trace 里同一个名字确实被算出了**两次**。

**修法**：两条路合并到**一个全局单调序列**（`static int anon_seq`），名字与表内容无关、
按构造唯一。修后 `jpegxl_parser.c` 用它自己的构建命令编译通过，IR 里的匿名类型名不再重复。

#### debug 阶段结束：探针里只剩缺口

| 探针 | 结果 |
|---|---|
| **FFmpeg** | 目标文件 **2262**，崩溃 **0**，错误类别 **1**——且那一类是**已记录的缺口**（asm 匹配约束，§0），**不再有 bug** |
| **深样本（`FJ_PER_DIR=3`）** | **376 / 376，0 failed**；超出范围 61 OpenMP + 100 无原型/K&R；缺口 14（`()` 无原型 7 + SSE/MMX 7） |
| **验收** | `test/conformance.sh` **263 passed / 0 gap**（本轮新增：三层嵌套的匿名结构体、匿名联合体与其成员）；`doc/c2ycov.sh` 109/0；c2y 101/0；`make test` exit 0 |
| **全量** | **全部与基线相同**：realworld cpython **381/385**（四例均为已记录的缺口：SIMD 内联函数与 `<complex.h>`）、git 567/567、libpng 18/18、lua 35/35、zlib 15/15、sqlite 1/1、tinycc 21/21；tests2 **106 / 0**；跨目标 arm64 51/0、rv64 51/0、rv32 51/0（+1 skipped）；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。 |

R71–R78 八轮共修好：指针调用的参数个数崩溃、`1 - 1` 当空指针常量被拒、
多维数组元素限定符、声明符开头的属性、asm 内存操作数是数组（含输出侧）、局部对象槽位对齐、
按值传递的聚合体被当值存、`c"…"` 误用于一字节结构体数组、匿名记录命名撞车。
期间以下项按政策归入**缺口**（§0 缺口表），留待 debug 阶段结束后统一计划：
`()` 无原型（C17 语义）、SSE/MMX intrinsics、`_Complex`/`_Decimal*`、asm 匹配约束对间接输出。

### R77 修好：`c"…"` 只能表示 `[N x i8]` —— ✅ FFmpeg 的三类非法 IR 全消

按 §R76 的计划，先把探针改成能看命令行（`FF_MAKE_ARGS`，`V=1`）。
第二次 `make -k` **只重建失败的目标**，所以很快；日志里三个错误全部指向**同一个文件**
`libswscale/utils.c`，而且两个错误在**同一行 IR**（99:1 与 99:63）——果然同源。

拿到确切命令行（带全部 `-D`）后复现，再用 `-cc1 -cc1-output` 保下 IR：

```llvm
%struct.FormatEntry = type { i8 }
@format_entries = internal global [227 x %struct.FormatEntry] c"\00\00\00\00..."   ; 字符串形式
```

**根因**：`dump_init()` 的数组分支只要“元素大小为 1”就用 `c"…"` 简写（`dumpir.c:1155`）。
但 `c"…"` **就是 `[N x i8]`**：元素是一字节的**结构体**时，类型是 `[227 x %struct.FormatEntry]`，两者不匹配——
LLVM 于是报两次（`constant expression type mismatch` 与 `redefinition of type`）。

**修法**：条件加上“元素是整型类型”：`is_integer(ty->base) && ty->base->size == 1`。
字节数组依然用简写（实测 IR：`@bytes = … c"\05\06\07\08"`），一字节结构体/联合体数组改为逐元素（`[%struct.One { i8 1 }, …]`）。

| | |
|---|---|
| **FFmpeg** | 目标文件 **2259 → 2261**，崩溃 **0**，错误类别 **4 → 2**（两个 `constant expression type mismatch` 全消，均出自 `libswscale/utils.c`）。剩下：1 个 `redefinition of type`（**非法 IR**）与 1 个 `Elementtype`（缺口） |
| **验收** | `test/conformance.sh` 261 → **262 passed / 0 gap**（新增：一字节结构体数组、一字节联合体数组、字节数组三者并存，含运行值）；最小用例与 gcc/clang 一致；`libswscale/utils.c` 用它自己的构建命令编译通过；`doc/c2ycov.sh` 109/0；c2y 101/0；`make test` exit 0 |
| **全量** | **与基线相同**：realworld cpython **381/385**、libpng 18/18、git 567/567、lua 35/35、zlib 15/15、sqlite 1/1、tinycc 21/21；深样本（`FJ_PER_DIR=3`）**376 / 376，0 failed**（缺口 14）；tests2 **106 / 0**；跨目标 arm64 51/0、rv64 51/0、rv32 51/0（+1 skipped）；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。 |

**下一轮目标**：最后一个非法 IR。同样用 `V=1` 的日志定位到了：

```
CC      libavcodec/vp9recon.o
/tmp/cxx-JzMAZ9:99:1: error: redefinition of type
   99 | %struct.anon.1 = type { %struct.FFJXLMetadata, %struct.JXLFrame }
```

两个**不同的匿名结构体**被编成了同一个 LLVM 名字 `%struct.anon.1`，LLVM 视为重定义。
修法方向：匿名类型的编号必须对**每个不同类型**唯一（而不是每个作用域/每次遇到重新计数）。

### R76 修好：按值传递的聚合体被“存”而不是“拷贝” —— ✅ `ffmpeg_sched.c` 的 5 类非法 IR 全消

上一轮定位到单个文件的非法 IR，本轮拿下了。IR 现场：

```llvm
%tmp104 = getelementptr %struct.SchedulerNode, ptr %tmp101, i64 %tmp103   ; 节点的地址
%tmp105 = alloca %struct.SchedulerNode, align 4
store %struct.SchedulerNode %tmp104, ptr %tmp105, align 4                 ; 把地址当记录值存
```

LLVM 拒绝：`'%tmp104' defined with type 'ptr' but expected '%struct.SchedulerNode'`。

**根因**：cxx 的 IR 里**聚合体的值就是它的地址**，所以记录从不走 `IR_STORE`——
它的拷贝都是 `IR_MEMCPY`（va_arg 那两处就是）。唯一漏掉的是“给值一个家”的那个分支：
**按值传递聚合体参数**时（`irgen.c:1876`），它调用 `store(a, hv, …)`，而 `a` 是地址。

**修法**：改成 `IR_MEMCPY`（与旁边的写法一致，两侧都转成 `char *`），并顺手把那个 home 的 alloca 也换成
`object_align()`（R75 的同一条规则：16 字节及以上的聚合体在 x86-64 上 16 对齐）。

| | |
|---|---|
| **FFmpeg** | 目标文件 **2257 → 2259**，崩溃 **0**，错误类别 **6 → 4**（两个 `'%tmp…' defined with type 'ptr' but expected …` 全消，均出自这一处）。剩下：1 个 `redefinition of type`、2 个 `constant expression type mismatch`（均为**非法 IR**），以及 1 个 `Elementtype`（缺口） |
| **验收** | `fftools/ffmpeg_sched.c` **单独编译通过**；`test/conformance.sh` 260 → **261 passed / 0 gap**（新增：成员左值、返回值、复合字面量三种聚合体实参，加两个实参同时传）；最小用例与 gcc/clang 输出一致；`doc/c2ycov.sh` 109/0；c2y 101/0；`make test` exit 0 |
| **全量** | **与基线相同**：realworld cpython **381/385**、libpng 18/18、git 567/567、lua 35/35、zlib 15/15、sqlite 1/1、tinycc 21/21；深样本（`FJ_PER_DIR=3`）**376 / 376，0 failed**（缺口 14）；tests2 **106 / 0**；跨目标 arm64 51/0、rv64 51/0、rv32 51/0（+1 skipped）；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。 |

**下一轮目标**：剩下三类非法 IR（1 个 `redefinition of type`、2 个 `constant expression type mismatch`）。
它们**无法单独编译复现**：涉及的文件（`libswscale/utils.c`、带 `FormatEntry` 表的那几个）
脱离 FFmpeg 的构建参数就先报缺少配置宏或头文件，而 `make -k` 的日志只有 `CC xxx.o`、
没有实际命令行。所以下一步是**用 `V=1` 重跑 FFmpeg 探针**（或从探针里取出它自己的编译命令），
先把那两个文件的确切命令行拿到，再定位。

### R75 修好：数组作 asm 内存操作数 + 局部对象的槽位对齐 —— ✅ 两个 bug，同一个程序里相遇

两者是在验证第一个时撞出第二个的。

#### （一）`"+m"(state)`：输出侧没有数组修正

ffmpeg `libavutil/utils.c:105`：

```c
uint16_t state[14];
__asm__ volatile ("fstenv %0 \n\t" : "+m" (state) : : "memory");
```

`"+m"` 既是**输出**又是**间接（内存）**操作数。`ND_ASM` 的循环里，**输入侧**已经知道“内存操作数命名的是对象，
数组就是数组本身而不是它退化成的指针”（保留数组节点，传的是数组地址），**输出侧没有**，
于是左值检查拿到的是退化后的 `ND_IMCAST`（不是左值）而报 `lvalue required in ‘asm’ statement`。

**修法**：把那个修正移到循环开头，两半共用。修后 `libavutil/utils.c` 编译通过。

#### （二）局部对象的槽位用了声明对齐（R63 漏掉的第三条路径）

验证第一个时合成的用例（`uint16_t state[14]` 加 `movaps` 读写 `double a[2]`）在 cxx 下崩溃，而分开写都正常。IR 里看到原因：

```llvm
%tmp4 = alloca [2 x double], align 8      ; 应为 16
```

R63 给“对象对齐”建了 `object_align()`（16 字节及以上的数组在 x86-64 上是 16 对齐），
应用在**全局对象发射**与 **`__builtin_alloca`**；局部变量的存储是**第三处**：

```c
for (Sym *var = fn->locals; var; var = var->next)
    new_ins(IR_ALLOCA, …, (Ref[]){INT(var->align)}, 1);   /* 声明对齐 */
```

`var->align` 是声明对齐，也是 `_Alignof` 该报的值（`double[2]` 为 8），必须保持；而**槽位**需要 16。
改成 `object_align(var->ty, var->align)` 后，`alloca … align 16`，那个 R63 时期**连编都编不过**的用例
现在编译并运行正常（三家输出一致）。

| | |
|---|---|
| **FFmpeg** | 目标文件 **2256 → 2257**，崩溃 **0**；`libavutil/utils.c` 编译通过。剩下的全是**非法 IR**：2 个 `defined with type ‘ptr’ but expected …`、1 个 `redefinition of type`、2 个 `constant expression type mismatch`；另有 1 个 `Elementtype`（`libavcodec/x86/hpeldsp_init.c`，已核实仍在，归缺口） |
| **验收** | `test/conformance.sh` 259 → **260 passed / 0 gap**（新增：数组作 `"+m"` 操作数与局部数组上的 `movaps`，一个用例同时盖住两个 bug）；`libavutil/utils.c` 编译通过；`doc/c2ycov.sh` 109/0；c2y 101/0；`make test` exit 0 |
| **全量** | **与基线相同**：realworld cpython **381/385**、libpng 18/18、git 567/567、lua 35/35、zlib 15/15、sqlite 1/1、tinycc 21/21；深样本（`FJ_PER_DIR=3`）**376 / 376，0 failed**（缺口 14）；tests2 **106 / 0**；跨目标 arm64 51/0、rv64 51/0、rv32 51/0（+1 skipped）；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。 |

**下一轮目标**：剩下的**非法 IR**。已定位到单个文件：`fftools/ffmpeg_sched.c` 单独编译即可复现
`'%tmp104' defined with type 'ptr' but expected '%struct.SchedulerNode'`，而日志里那 5 类（两个 `defined with type`、
`redefinition of type`、两个 `constant expression type mismatch`）很可能同源。
（另：`libswscale/utils.c` 单独编译时先报 `FF_ALLOC_TYPED_ARRAY` 未声明，那是缺少 FFmpeg 配置宏的假象，与 cxx 无关。）

### R74 修好：属性可以写在声明符列表中间 —— ✅ FFmpeg 的两个解析错误消失

```c
int i, ret, av_unused(version), nb_curves;   /* av_unused = __attribute__((unused)) */
```

GCC 允许属性出现在**声明符的开头**，不只是它之后；FFmpeg 就靠这一点（`vf_curves.c:591`），
`ripemd.c:111` 是同一形状（`uint32_t a, b, …, av_unused t;`）。cxx 在声明符**之后**（`apply_postdecl_attrs`）
和声明**之前**（`declspecs`/`decl_attrs`）都能读属性，就是不能在**声明符开头**，于是报
`expected identifier or ‘(’`。

**修法**：两条声明路径各自的声明符循环，在**每轮开头**接受属性并接到该声明符的 `ty->attrs`
上——下游的 `apply_postdecl_attrs()`、`attr_decl_apply()`、`sym_attr_flags()` 都从那里读，所以不需要别的改动。

> 一次白跑：第一版只改了文件作用域那条路径（`declaration()`），而 FFmpeg 的两行都在**函数体内**，
> 走的是 `init_decl_list()`（`parser.c:5126`）——修完第二条才生效。两条都留着，两种位置都合法。

| | |
|---|---|
| **FFmpeg** | 目标文件 **2254 → 2256**，崩溃 **0**，错误类别 **9 → 7**（`expected identifier or ‘(’` 两例消失）。剩下的是：2 个 `defined with type ‘ptr’ but expected …`、1 个 `redefinition of type`、2 个 `constant expression type mismatch`（以上四类是 **非法 IR**）、1 个 `lvalue required in ‘asm’ statement`（`libavutil/utils.c:105`，asm 的内存操作数是数组 `uint16_t state[14]`，两家都接受）、1 个 `Elementtype`（已归缺口） |
| **验收** | 两个复现与 gcc/clang 输出一致；**`ripemd.c` 编译通过**；`vf_curves.c` 越过该错（再往后是 `NULL_IF_CONFIG_SMALL`，**三家都报**——单独编译缺少 FFmpeg 配置宏的假象）；`test/conformance.sh` 258/0；`doc/c2ycov.sh` 109/0；c2y 101/0；`make test` exit 0；`doc/probes.sh` 全部基线 |
| **全量** | **与基线相同**：realworld cpython **381/385**、libpng 18/18、git 567/567、lua 35/35、zlib 15/15、sqlite 1/1、tinycc 21/21；**深样本（`FJ_PER_DIR=3`）仍 376 / 376，0 failed**（缺口 14）；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。 |

### R73 修好：多维数组的元素限定符；并把 FFmpeg 剩余错误分类定案 —— ⚠️ debug 阶段尚未结束

为了收尾 debug 阶段，重跑了 FFmpeg 探针（自 §R60 后没再跑）：**0 崩溃**，以下是它报出的每一类

#### （一）修好的：`pointee_unqual()` 只剥一层数组

6.7.3p9 说“写在数组类型上的限定符属于它的元素”。**数组的数组在每一层都是数组类型**，
所以 `const int16_t a[3][3][8]` 的限定符落在最内层的 `int16_t` 上；只剥一层只能去掉 `int16_t[3][8]`
的限定符（而它本来就没有）。这就是 ffmpeg `vf_colorspace.c:327`：被调者取
`const int16_t yuv2yuv_coeffs[3][3][8]`，而调用方传的是 `int16_t[3][3][8]` 成员。

先前的写法故意只剥一层，因为“`int **` 与 `const int **` 不相容”——那条规则依然成立：
**递归只沿数组元素走，不穿过指针层**。修后实测：多维数组的 const 被接受（三家一致），
`int **` → `const int **` 依然被拒绝（gcc/cxx；clang 只警告，已有记录的严重度差异）。

#### （二）FFmpeg 剩余错误的分类（本轮定案，尚未修）

| 类别 | 数量 | 真相 | 定性 |
|---|---|---|---|
| `expected identifier or ‘(’` | 2 | **GCC 属性写在声明符列表中间**：`int i, ret, av_unused(version), nb_curves;`（`vf_curves.c:591`）、`uint32_t …, av_unused t;`（`ripemd.c:111`），`av_unused` 展开为 `__attribute__((unused))` | **bug**（解析） |
| `‘%tmp7’ defined with type ‘ptr’ but expected ‘%union.SyncQueueFrame’` | 2 | 发射出的 IR 类型不一致（`ffmpeg_sched.c` 与另一处） | **bug**（非法 IR） |
| `redefinition of type` | 1 | 同一个名字发了两个不同的 LLVM 类型 | **bug**（非法 IR） |
| `constant expression type mismatch: [227 x i8] vs [227 x %struct.FormatEntry]` | 2 | 初始化器的类型与声明的不一致（同 R64 那一族的另一面） | **bug**（非法 IR） |
| `lvalue required in ‘asm’ statement` | 1 | `libavutil/utils.c:105`，`asm` 操作数是数组 | **bug**（诊断不对） |
| `Elementtype attribute can only be applied for indirect constraints` | 1（同一文件共 8 行） | `libavcodec/x86/hpeldsp_init.c`：匹配约束 `"0"` 对一个**间接输出**时，cxx 把它展成被匹配者的约束并按**地址**传值（clang 传的是值 `i32 %9`，约束保留为 `0`） | **缺口**（asm 匹配约束不完整） |

所以 **debug 阶段尚未结束**：上表前五行都是待修的 bug（其中三行是**非法 IR**）。
第六行按政策归入缺口，已记入 §0 的缺口表。

| | |
|---|---|
| **FFmpeg** | 目标文件 **2252 → 2254**，崩溃 **0**，错误类别 10 → **9**（`incompatible types when passing argument` 两例全消） |
| **深样本** | 仍 **376 / 376，0 failed**（缺口 14） |
| **验收** | `test/conformance.sh` **258 passed / 0 gap**；`doc/c2ycov.sh` 109/0；c2y 101/0；`make test` exit 0；`doc/probes.sh` 全部基线（自举逐字节相同） |
| **全量** | **与基线相同**：realworld cpython **381/385**、libpng 18/18、git 567/567、lua 35/35、zlib 15/15、sqlite 1/1、tinycc 21/21；tests2 **106 通过 / 0 失败**；跨目标 arm64 51/0、rv64 51/0、rv32 51/0（+1 skipped）。 |

### R72 debug 第一轮：一个崩溃 + 一个误报错 —— ✅ 两个都修好

按 §R71 的政策，本轮只修已有功能的 bug。上一轮扫描给出的两个目标都落定了：

#### （一）通过函数指针调用时的参数个数诊断：空指针解引用 → SIGSEGV

最小复现（三行）：

```c
int (*mpfp)();
int main(void) { return (*mpfp)(0); }
```

`fncall()` 的三处参数个数诊断都读 `ty->name->len`，而通过指针调用时所指的函数类型**没有名字**
（`ty->name == NULL`）——于是解引用空指针。其中两处（`too few`）在现有测试里碰不到，
一处（`too many`）就是 Fujitsu `C/0048_0001`：

```c
int mpfff(), (*mpfp)(), ii;
ii = (*mpfp)(i);
```

**修法**：新增 `error_call_args()`，只在类型有名字时命名，否则用 clang 的措辞
（`too many arguments to function call; expected 0`）。修后四个复现都变成正常诊断。

> **顺带发现**：`C/0048_0001` 在崩溃修好后仍报错，但那是**已记录的政策类**（`()` 的 C17 语义，§R56/R57）。
> 探针原来的 clang 过滤只命中“声明为 `f()` 的函数被调用”，**不命中函数指针这一形态**；
> 现已把 `(*name)()` 这个声明符形态归入同一缺口类（命中 7 个）。

#### （二）`1 - 1` 就是空指针常量，却被当成类型不匹配

Fujitsu `C/0150_0002`：`int fp0(int *);` 对应 `fp0(1-1)`。按 6.3.2.3p3，“值为 0 的整型常量表达式”
**就是空指针常量**——gcc 静默接受，clang 警告接受（“expression which evaluates to zero treated as a null
pointer constant”），cxx 报错。根因：`is_null_constant()`（`type.c:271`）只认**字面量零**
（一个 `ND_NUM`），而语法分析阶段碰到的是**尚未折叠**的表达式。

影响面不止调用：`int *p = 1 - 1;`、`= 0 + 0;`、`= 2 - 2;` 同样被拒。

**修法**：在 `check_asop()`（那个汇总“incompatible types when …”的函数）里，当目标是指针时先
`fold_node(src)` 一次，折叠结果是一个值为 0 的整型 `ND_NUM` 就放行。

> **一个坑**：`fold_node()` 返回的是**它自己的节点**，读 `src->ival` 会读到未折叠表达式的值。
> 另外 cxx 没有 `-Wint-conversion` 组，按本阶段“只修 bug”的原则取 **gcc 的静默接受**，而不新增警告。

**两个真实不匹配依然报错**（三家一致）：`int *p = a;`（非常量）、`long n = p;`（指针转整）。

| | |
|---|---|
| **深样本（`FJ_PER_DIR=3`）** | **376 / 376**，**0 failed**（修前 377/378 并有 1 个真失败）；超出范围 61 OpenMP + 100 无原型/K&R，缺口 14（`()` 无原型 7 + SSE/MMX 7），参考实现编不出 5 |
| **验收** | `test/conformance.sh` 255 → **258 passed / 0 gap**（新增：指针调用的参数个数必须是诊断而非崩溃、常量表达式形式的空指针常量必须接受、非常量整数转指针必须仍拒绝）；`doc/c2ycov.sh` 109/0；c2y 101/0；`make test` exit 0；`C/0150_0002` 跑到 `END`，与参考一致 |
| **全量** | **与基线相同**：realworld cpython **381/385**、libpng 18/18、git 567/567、lua 35/35、zlib 15/15、sqlite 1/1、tinycc 21/21；tests2 **106 通过 / 0 失败**；跨目标 arm64 51/0、rv64 51/0、rv32 51/0（+1 skipped）；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。 |

### R71 阶段转向：debug——只修 bug，未支持功能先屏蔽记缺口 —— ✅ 政策与分类已落地

按定调把阶段重点改为**修正已有功能的 bug**：测试暴露出“未支持的功能”时，
**先屏蔽并记为缺口**，不当场实现；debug 阶段结束后再统一计划是否扩充。

#### （一）探针多了一个 `gap` 桶

`doc/fujitsu.sh` 的 `gap_feature()` 按缺口表把测试归类，与“超出范围”（OpenMP、K&R/无原型、复数）
分开计数，并在汇总里**按特性列出**。于是“失败”只剩下**本该能用的东西真出了问题**。
`FJ_GAPS=0` 可关掉过滤，直接看列表。

**深样本（`FJ_PER_DIR=3`）的新账目**：

```
fujitsu: 377 of 378 passed, 1 failed
   (out of scope: 61 OpenMP, 105 no-prototype/K&R; gaps: 7; unbuildable by the reference: 5)
   gaps (a feature cxx does not have; section 0 of the plan):
          7  SSE/MMX intrinsics
```

（原来是 380/384；现在 377/378，因为 7 个 `__SSE2__` 测试从“失败”移到了“缺口”。）

#### （二）一次全样本扫描：真正的 bug 只剩一个崩溃

用一个不套 clang 过滤的脚本把深样本里**所有与参考不一致的编译**列了出来，命中的类别只有三个：

| 命中 | 真相 |
|---|---|
| `a type specifier is required for all declarations`（多个） | **全是 K&R 定义**（`int sub(p, idx)` 等）——已在 §0 的政策表里，**不是新缺口** |
| `too many arguments to function ‘X’; expected 0`（多个） | 同一类（`f()` 无原型）——已屏蔽 |
| **`cxx killed by signal 11`** —— `C/0048_0001.c` | **真正的 bug**：参考实现接受这个文件，而 cxx 内部错误 |

也就是说，除了已知缺口，深样本里只剩**一个崩溃**——它是下一轮的直接目标。

（扫描脚本本身的缺点：它没有套用 clang 的 `-Wdeprecated-non-prototype` 过滤，
所以 K&R 那一类会冒出来；下次直接用 `doc/fujitsu.sh` 的分类。）

| | |
|---|---|
| **验收** | `bash -n doc/fujitsu.sh` 通过；深样本新账目如上；方案已写入 §0（阶段政策 + 缺口表） |
| **代码** | 本轮未改编译器（只改探针与文档）；因此各套件保持上一轮的结果 |

### R70 `__label__`（GNU 的块内局部标签）—— ✅ 深样本里唯一的编译失败消失

`C/0059_0002` 用 `__label__ test004_1, …;`，而且是写在一个**语句表达式**（`({ … })`）里的宏，
同一个宏在一个函数里展开多次——这正是 `__label__` 的用途：名字**作用域限于所在块**，
所以多个块可以各自定义同名标签。实测（gcc/clang 一致）：

| 形态 | 输出 |
|---|---|
| `{ __label__ L; goto L; … L: … }` | `*OK*` |
| 两个语句表达式各自 `__label__ a, done;` | `1 9` |
| 同一块内同名标签两次 | **报错**（redefinition of label） |
| 块外 `goto L` 指向块内的 `__label__` 标签 | **报错**（undeclared label） |

**实现**：cxx 用一个**函数级的 id 列表**解析标签（重名检查、goto 匹配、块分配都在上面），
所以 `__label__` 声明的名字**在解析时换成一个自己的 id**：带上声明顺序后缀，
标签定义、`goto`、`&&label` 三处都用同一个查找，下游全部机制照旧运行在**已经互不相同**的 id 上。
条目在“声明它的那个 scope 仍在当前链上”时有效，块外查不到——这就是作用域规则。

> **一次白跑：**第一版借用了 `new_unique_varname()`，但它**只在 `globals` 里查重**，
> 查不到就**原样返回 id**——而标签永远不在 `globals` 里，于是等于没改写，
> 宏的第二次展开仍报 `redefinition of label ‘test004_1’`。改成自己的计数器后就对了。

| | |
|---|---|
| **深样本（`FJ_PER_DIR=3`，384 个测试）** | 379 → **380 / 384**（修前 379）；剩下的都是 `#error __SSE2__`（§R63 的已知缺口）与超出范围项，**再无编译失败** |
| **验收** | `test/conformance.sh` 252 → **255 passed / 0 gap**（新增：块内与语句表达式里的 `__label__`、同块重名必须报错、块外 goto 必须报错）；`C/0059_0002` 输出与参考逐字一致 |
| **全量** | **与基线相同**：realworld cpython **381/385**、libpng 18/18、git 567/567、lua 35/35、zlib 15/15、sqlite 1/1、tinycc 21/21；tests2 **106 通过 / 0 失败**；跨目标 arm64 51/0、rv64 51/0、rv32 51/0（+1 skipped）；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。 |

### R69 更深的 Fujitsu 样本（每目录 3 个）挖出的三个缺口 —— ✅ 全部修好

样本此前**每个目录只取一个文件**（204 / 37190），所以上一轮新加了 `FJ_PER_DIR` 旋钮。
`FJ_PER_DIR=3`（384 个测试，376 通过）当场暴露三个新类别：

| 测试 | 诊断 | 真因 |
|---|---|---|
| `C/0108_0001` | 链接失败 `undefined reference to \`foo_impl'` | **`__attribute__((alias("foo")))` 被接受后丢弃** |
| `C/0044_0001` | `expected ‘)’ before ‘,’` | `__sync_lock_*` 上的**多余实参** |
| `C/0077_0002` | `implicit declaration of function ‘__alignof’` | GNU 的**旧拼写 `__alignof`** |

#### （一）`__attribute__((alias("target")))`

gcc 与 clang 都把被声明的名字当作**同一翻译单元内已定义实体的另一个名字**（两家都输出 `OK OK OK`）；
cxx 接受了属性但丢弃了它，于是调用指向一个没人定义的符号。

cxx **已经有这个概念**：一个符号的 C 标识符与它发射时用的名字可以不同——就是
`__asm__("name")` 占的位置（`set_asm_name`）。所以实现就是把属性的参数放进同一个槽位：对别名的调用变成对目标的引用，
而打印器“一个目标文件名只发一条”的规则保证模块里只剩一个定义。

> **一次白跑：**第一版把属性只从 `attrs`（声明说明符那一列）里找，而
> `void f(void) __attribute__((alias("foo")));` 的属性在**声明符之后**，落在 `ty->attrs` 上——两个列表现在都查。

#### （二）`__sync_lock_*` 上的多余实参

`__sync_lock_test_and_set(&a, b, &dummy)`——gcc 和 clang **都接受并忽略**第三个实参（实测），
而 `__sync_fetch_and_add(&a, 3, &d)` **两家都拒绝**（实测）；cxx 现在按同一规则：只在 `__sync_lock_test_and_set`
与 `__sync_lock_release` 上跳过多余实参。

#### （三）`__alignof`

GNU 的旧拼写。词法表里有 `_Alignof`、`alignof`，而 `__alignof__` 靠“剥双下划线”能命中——
**`__alignof` （尾部只有一个下划线）命中不了**，于是被当成普通标识符、进而报隐式函数声明。加一行即可。

| | |
|---|---|
| **深样本（`FJ_PER_DIR=3`，384 个测试）** | 376 → **379 / 384**（修前 376）；剩下的里面只有一个编译失败：`C/0059_0002` 的 **`__label__`**（GNU 的局部标签声明，待做），其余是 `#error __SSE2__` 与超出范围项 |
| **验收** | `test/conformance.sh` 248 → **252 passed / 0 gap**（新增：`alias` \uff08函数与对象各一）、`__sync_lock_*` 的多余实参必须接受、fetch 族必须仍拒绝、`__alignof`）；`C/0044_0001`、`C/0077_0002`、`C/0108_0001` 输出均与参考一致 |
| **全量** | **与基线相同**：realworld cpython **381/385**、libpng 18/18、git 567/567、lua 35/35、zlib 15/15、sqlite 1/1、tinycc 21/21；tests2 **106 通过 / 0 失败**；跨目标 arm64 51/0、rv64 51/0、rv32 51/0（+1 skipped）；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。 |

### R68 GCC 的旧式 `__sync_*` 原子内建 —— ✅ `C/0044` 输出 `OK`

上一轮定位到的 `C/0044`：`__sync_lock_test_and_set` 报 `implicit declaration of function`（连跑三次稳定）。
cxx 已有 `__atomic_*` 一族与 **`__sync_synchronize`**，缺的是旧式的其余成员。

**关键差别在于参数**：`__sync_*` 把**内存序写在名字里**，而 `__atomic_*` 把它作为最后一个参数。
所以实现是“映射到现有操作码 + 跳过那个参数”，而不是新建一套语义：

| 写法 | 映射到 | 隐含序 |
|---|---|---|
| `__sync_lock_test_and_set(p, v)` | `ATOMIC_EXCHANGE` | acquire |
| `__sync_fetch_and_add/sub/and/or/xor(p, v)` | `ATOMIC_FETCH_*` | seq_cst（全屏障） |
| `__sync_lock_release(p)` | 自己的行（写 0） | release |
| `__sync_synchronize()` | 已有 | seq_cst |

**未实现的部分与原因**（写在表旁边）：`*_and_fetch` 返回**新值**，而
`ND_ATOMICRMW` 给的是旧值，要把操作数加回去（对指针还要按元素尺寸缩放）；两个
compare-and-swap 形式按值接旧值；nand 没有对应的 `A_*` 操作码。

> 测量在先：整个 Fujitsu 套件里 `__sync_*` 出现 16 种共 200+次，但 **`C/0044` 只用三种**
> （lock_test_and_set / lock_release / synchronize），而三个真实项目（git/cpython/libpng）只用 `__sync_synchronize`——
> 所以先把这三种加 fetch 族做完，其余作为可选后续。

| | |
|---|---|
| **验收** | 子集运行值 `1 7 0 1 4 3 11 8 9` 与 gcc/clang **逐字一致**；`C/0044` 输出 `OK`；`test/conformance.sh` 247 → **248 passed / 0 gap**；`doc/c2ycov.sh` 109/0；c2y 101/0；`make test` exit 0 |
| **全量** | **与基线相同**：realworld cpython **381/385**、libpng 18/18、git 567/567、lua 35/35、zlib 15/15、sqlite 1/1、tinycc 21/21；tests2 **106 通过 / 0 失败**；跨目标 arm64 51/0、rv64 51/0、rv32 51/0（+1 skipped）；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。嵌套用例（`__sync_*` 套 `__atomic_*`、`__sync_*` 套 `__sync_*`）与两家一致：`1 11 11 11 0 13`——这是本轮自己发现并修掉的隐患留下的回归点。 |

### R67 `__fp16`（ACLE 的半精度拼写）—— ✅ `C/0194` 整目录 50/50

`C/0194` 的 `void test(__fp16 *restrict a, bool *restrict b, int n)` 报 `a type specifier is required for all
declarations`。量出来的三家行为：

| | gcc | clang | cxx 修前 | cxx 修后 |
|---|---|---|---|---|
| `__fp16` 是否存在 | **没有**（建议 `__bf16`） | 每个目标都有 | 没有 | 有 |
| `sizeof(__fp16)` / 运算 | — | `2`、`1.5+2.5=4` | — | **`2`、`4`** |
| 按值做参数/返回值（x86-64、riscv64） | — | **报错**：`parameters cannot have __fp16 type; did you forget * ?` | — | 接受（宽松，已记录） |
| 指针/变量用法（`C/0194` 的形状） | — | 接受 | 报错 | **接受** |
| aarch64 | — | 完全接受，IR 与 `_Float16` 同为 `half` | — | 同 |

**实现**：`src/lexer.c` 的关键字表里加一行 `{"__fp16", 0, TK_F16}`——表中已有
`__asm`/`__attribute`/`__inline`/`__restrict`/`__thread` 这类 GNU 拼写的先例。

**两处有意的差异**（写在源码注释里）：

1. clang 把 `__fp16` 当**独立类型**（`__builtin_types_compatible_p(__fp16, _Float16)` 为 **0**、
   `_Generic` 走 `default`），而 cxx 是 `_Float16` 的另一种拼写。两者在**表示、运算与 ABI** 上一致
   （对照 clang 的 IR：三个目标的参数/返回类型都是 `half`）；要做成独立类型得把新 `Type` 種类
   贯穿每一个转换、提升与 ABI 位置，而换来的只有上面两个可观察差异。
2. clang 在没有半精度参数的目标上禁止 `__fp16` 按值作参数或返回值，cxx 接受。这是**宽松**方向的差异。

| | |
|---|---|
| **验收** | `test/conformance.sh` 246 → **247 passed / 0 gap**（新增：`__fp16` 的全局变量、`restrict` 指针参数、运算与 `sizeof`）；`C/0194` 整目录 **50/50**；`doc/c2ycov.sh` 109/0；c2y 101/0；`make test` exit 0 |
| **全量** | **与基线相同**：realworld cpython **381/385**、libpng 18/18、git 567/567、lua 35/35、zlib 15/15、sqlite 1/1、tinycc 21/21；tests2 **106 通过 / 0 失败**；跨目标 arm64 51/0、rv64 51/0、rv32 51/0（+1 skipped）；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。 |

### R66 `-Wpointer-sign` 覆盖 `char` 的两个变体 —— ✅ `C/0168` 现在与 clang 输出一致

上一轮剩下的 `C/0168`：`strcpy(scharp, "cd")`，而 `scharp` 是 `signed char *`。量出的对照：

| 实参类型 | gcc | clang | cxx 修前 | cxx 修后 |
|---|---|---|---|---|
| `char *` | ok | ok | ok | ok |
| **`signed char *`** | ok（不响） | **warning** | **error** | **warning** |
| `unsigned char *` | ok（不响） | warning | warning | warning |
| `double *` 给 `int *`（真实不匹配） | error | error | error | **error**（不受影响） |

**根因**：`sign_only_difference()`（`src/type.c:849`）要求两侧 `is_unsigned` 不同，
而 x86-64 上 `char` 与 `signed char` **符号相同**，于是 `-Wpointer-sign` 不作声，转换落到后面的
`incompatible types when passing argument`。`unsigned char` 那一侧因为符号确实不同而一直正常。

**修法**：把“一侧是普通 `char`、另一侧是 `signed char`/`unsigned char`”也算作该警告的范围（尺寸相同已由前面的
`a->size != b->size` 过滤）。clang 对这一情形的措辞是“converts between pointers to integer types where one is of the unique
type 'char'”，也归入 `-Wpointer-sign`；gcc 完全不响。**cxx 主要模仿 clang**，所以取警告。

| | |
|---|---|
| **验收** | `test/conformance.sh` 243 → **246 passed / 0 gap**（新增：三种 `char` 变体的运行值、警告必须出现、`double *`→`int *` 必须仍被拒绝）；`doc/c2ycov.sh` 109/0；c2y 101/0；`make test` exit 0；`C/0168` 输出与 clang 一致（`Not memalias`） |
| **Fujitsu 样本** | 130 → **131 / 134** |
| **全量** | **与基线相同**：realworld cpython **381/385**、libpng 18/18、git 567/567、lua 35/35、zlib 15/15、sqlite 1/1、tinycc 21/21；tests2 **106 通过 / 0 失败**；跨目标 arm64 51/0、rv64 51/0、rv32 51/0（+1 skipped）；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。 |

### R65 `#pragma pack` 与成员级 packed 也要到达 IR —— ✅ 修好（R64 的直接后续）

R64 修好了 `__attribute__((packed))`，但同一类型的另两种写法仍在错：

| 写法 | 布局（三家一致） | cxx 修前的 IR 类型 |
|---|---|---|
| `#pragma pack(1)` + `{ char c; double i; }` | size 9、align 1 | `{ i8, double }`（double 在偏移 8） |
| `#pragma pack(2)` 同上 | size 10 | `{ i8, [1 x i8], double }`（double 在偏移 8） |
| `{ char c; int i __attribute__((packed)); }` | size 5 | `{ i8, i32 }`（i 在偏移 4） |

布局一直是对的（`layout_struct` 的 `packed_layout`、成员 cap 7135、最终对齐 7180 都看了 `cur_pack`），
**差的只是 IR 拼写**：`is_packed` 只由属性置位，而它又是**布局的输入**（每个成员对齐到 1），
不能顺手拿来表示“C 布局 ≠ LLVM 自然布局”。

**修法**：新增独立字段 `Type.layout_packed`，在 `layout_struct` 里定下来：

```c
    if (cur_pack > 0 && natural_align > cur_pack && !attr_align) lowered = true;
    ty->layout_packed = ty->is_packed || lowered;
    for (Member *m = ty->members; !ty->layout_packed && m; m = m->next)
        if (m->is_packed) ty->layout_packed = true;
```

其中 `lowered` 在两处置位：成员对齐被 cap 压低（7135），以及**记录自身**的对齐被压低（它同时决定尾部填充：
`pack(4)` 下 `struct { double d; char c; }` 是 12 字节，而 LLVM 的类型会把 9 向上圆到它自己的 8 得到 16）；
显式 `aligned(N)` 是**抬高**而非降低，不算。打印器改读 `layout_packed`，复合类型拷贝时一并拷贝。

**验证**（运行值与类型拼写都对照两家）：

| 用例 | gcc | clang | cxx |
|---|---|---|---|
| `pack(1)`/`pack(2)`/`pack(4)`/packed 成员 四种形状的运行值 | `1 2 3 4 5 6 7 8 9 10 12` | 同 | **同** |
| clang 的类型拼写 | — | `P1 = <{ i8, double }>`、`MP = <{ i8, i32 }>` | **逐字一致** |
| `pack(8)`（未降低任何东西） | — | `{ i8, double }` | `{ i8, [7 x i8], double }`（显式填充，布局相同） |

> 一次操作失误值得记：修改脚本在失败前已经写了前三个文件，重跑又把字段插了一遍（`cxx.h` 里出现两份
> `layout_packed`）。当场发现并去重，之后把“打补丁”与“验证”拆成两个脚本。

| | |
|---|---|
| **验收** | `test/conformance.sh` 242 → **243 passed / 0 gap**（新增：pack(1)/pack(2)/pack(4)/packed 成员四种形状的运行值与 `sizeof`）；`doc/c2ycov.sh` 109/0 |
| **全量** | **与基线相同**：realworld cpython **381/385**、libpng 18/18、git 567/567、lua 35/35、zlib 15/15、sqlite 1/1、tinycc 21/21；tests2 **106 通过 / 0 失败**；跨目标 arm64 51/0、rv64 51/0、rv32 51/0（+1 skipped）；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**；Fujitsu 保持 130/134（该套件用属性写 packed，这一修是为了库代码里的 `#pragma pack`）。 |

### R64 packed 记录必须发成 `<{ }>` —— ✅ 修好，`C/0091` 的输出错误消失

上一轮剩下的失败里，`C/0091` 是最严重的一类：**cxx 打印 `NG`，两家参考实现打印 `OK`**——错误的代码生成。
那个程序只有一个动作：`struct __attribute__((packed)) A { char c; double i; }`（`i` 在偏移 1），
`struct A st = { 1, 2 };`，然后 `st.i = st.i + 1.0;`。

**最小复现**（`~/cxxwork/repro/preparse/pk1.c`）：

| 用例 | gcc | clang | cxx 修前 | cxx 修后 |
|---|---|---|---|---|
| `st = { 1, 2 }` 后读 `st.c st.i sizeof` | `1 2 9` | `1 2 9` | **`1 0 9`** | **`1 2 9`** |
| 赋值形式（`st.c = 1; st.i = 2;`） | `1 2` | `1 2` | `1 2`（本来就对） | `1 2` |
| `char c; long i;` 的同一形状 | `1 2` | `1 2` | **乱码** | **`1 2`** |

**根因**：cxx 的类型体用**显式 `[N x i8]` 填充**给每个成员安上它的 C 偏移，
但记录本身用普通花括号发出，于是 **LLVM 又按自然布局加了一层填充**：

```
cxx 修前：%struct.A = type { i8, double }                  → double 在偏移 8
clang   ：%struct.A = type <{ i8, double }>                → double 在偏移 1
```

**访问一直是对的**（它们走字节 `getelementptr`：`getelementptr i8, ptr @st, i32 1` + `load double … align 1`），
所以只有**初始化器**出错——它是按类型写的，而那个类型的布局不是 C 的。
这也解释了为什么赋值形式（无初始化器）一直正常。

**修法**：`record_open()` / `record_close()` 一对小助手，按 `ty->is_packed` 在 `<{ ` 与 `{ ` 之间切换，
统一用于**六处**：类型定义（`dump_type`，与 `TY_UNION` 共用花括号）、
初始化器里的类型（`print_init_ty`）、值（`dump_init`），以及 union 型别名成员的**匿名形式**（`print_union_elem_ty` 与 `dump_union_elem`，
按 union 自身的 packedness）。类型与常量必须用同一种拼写，否则 LLVM 拒绝初始化器属于另一个类型。

修后的 IR 与 clang 逐字一致：

```
%struct.A = type <{ i8, double }>
@st = dso_local global %struct.A <{ i8 1, double 0x4000000000000000 }>, align 1
```

| | |
|---|---|
| **验收** | `test/conformance.sh` 241 → **242 passed / 0 gap**（新增：packed 结构（`double`/`long`/数组成员各一种）、packed union 的打穿初始化、按值返回的 packed 结构，加 `offsetof`）；`C/0091` 输出 `OK` |
| **Fujitsu 样本** | 129 → **130 / 134** |
| **全量** | **与基线相同**：realworld cpython **381/385**、libpng 18/18、git 567/567、lua 35/35、zlib 15/15、sqlite 1/1、tinycc 21/21；tests2 **106 通过 / 0 失败**；跨目标 arm64 51/0、rv64 51/0、rv32 51/0（+1 skipped）；`doc/probes.sh` 全部基线（c2ycov 109/0），**cxx2 = cxx3 = cxx4 逐字节相同**。 |

### R63 x86-64：数组 ≥ 16 字节的对齐修好；SIMD 预定义试过后撤回 —— ✅ Fujitsu 128 → **129/134**

上一轮剩下 6 个失败里，`C/0163` 是最严重的一类：**cxx 的产物运行时段错误**（退出 139），
而参考实现打印 `6 2`。那个程序用 `movaps` 读写三个 `double[]` 全局对象。

#### （一）修好的：x86-64 psABI 的“数组 ≥ 16 字节即 16 字节对齐”

`clang` 给 `double[2]` 全局发 `.p2align 4`（16），cxx 发 `.p2align 3`（8），而 `movaps` 需要 16——就是崩溃的原因。
量出的规则（gcc/clang 一致；用 `clang -target` 验证了它是 **x86-64 专属**，aarch64/rv64/rv32 都用元素对齐）：

| 对象 | 结果 |
|---|---|
| `double[2]`、`double[3]`、`float[4]`、`int[4]`、`short[8]`、`char[16]` | 16 字节对齐 |
| `double[1]`、`float[3]`、`int[3]`、`struct { double a, b; }` | 元素/本身对齐（8、4、4、8） |
| `_Alignof(struct { char buf[16]; char c; })` | **1**（size **17**），与 gcc/clang 一致 |

三层区分，前两层我都先做错了一次，都是被现有探针当场抓住的：

1. **对象对齐 ≠ 类型对齐**。第一版放进 `array_of()`，立刻把聚合体布局带坏：
   `struct C { char buf[16]; char c; }` 从 **17/1** 变成 **32/16**。改到对象侧后三家一致。
2. **存储对齐 ≠ 报告对齐**。第二版把提升写进 `var->align`，`__alignof__(plain)` 就从 8 变成 16——
   **`doc/c2ycov.sh` 的“sizeof / alignof / _Countof / typeof”当场抓住**（109/0 → 108/1）。实测两家：`double[2]` 全局存在 16 对齐的地址上，
   但 `__alignof__` 报 **8**；只有显式 `_Alignas(32)` 才报 32。
3. **长度可能晚到**。`double init[] = { 1.0, 2.0 };` 在声明时 `size` 为 0；对齐改到发射点才算后，这个问题自然消失。

最终：`object_align()` 住在 `type.c`（声明在 `cxx.h`），由目标字段 `T.array_align16` 开关（仅 amd64），
只在**两个分配存储的地方**生效：全局发射（`dumpir.c` 的两处 `align`）与局部的 alloca；`var->align` 始终是声明对齐。

#### （二）试过并**撤回**的：x86-64 的 SIMD 预定义

起因是 `C/0159` 的 `#error Need macro __HPC_ACE__ or __SSE2__`：gcc/clang 都定义 `__MMX__`/`__SSE__`/`__SSE2__`/`__SSE_MATH__`/`__SSE2_MATH__`，
cxx 一个都没有。我按 clang 的集合补上后，`C/0159` 越过了 `#error`（改卡在 clang 的 `<mmintrin.h>`：
`__builtin_ia32_emms`）——但全量扫描立刻报警：

| | 基线 | 定义了 `__SSE2__` 之后 |
|---|---|---|
| cpython | 381 / 385 | **149 / 385**（**235** 个 `implicit declaration`） |
| libpng | 18 / 18 | 17 / 18 |

机制很直接：这些宏告诉每个头文件“机器有 SSE2、编译器能降下配套的内联函数”，
而 cxx 没有 `__builtin_ia32_*` 族，于是头文件的 SIMD 分支全部散架。

> **结论：撤回。**声称一个自己降不了的特性是错的答案；不声称才是一个没有那些内联函数的编译器该做的事——
> 这与“**cxx 最终身份始终不是 gcc 或者 clang，不应该假冒身份**”同一条道理。
> 代价是 `C/0159` 回到失败（它本来就是“检查 SSE2 代码能不能编”的用例，而这个编译器不编 SSE2）。
> 要真正解决，得先有 `__builtin_ia32_*`，那是另一块工作，已记为**已知缺口**。

#### （三）顺带发现

`asm("..." : "=m"(r) : "m"(a))`（**内存约束**）cxx 目前编不了，而 gcc/clang 很常见地接受；
这也是无法用它直接验证栈上对齐的原因。

| | |
|---|---|
| **Fujitsu 样本** | 128 → **129 / 134**（`C/0163` 的崩溃消失；`C/0159` 因预定义撤回依然失败，它从未通过） |
| **验收** | `test/conformance.sh` 239 → **241 passed / 0 gap**（新增：16 字节数组全局上的 `movaps`、聚合体布局必须保持 `16/8 16/4 17/1 24/8`）；`doc/c2ycov.sh` **109/0**（它抓住了对齐那一步的回退） |
| **跨目标** | arm64 51/0、rv64 51/0、rv32 51/0（+1 skipped）——该规则不适用于它们，实测确认 |
| **全量** | **与基线相同**：realworld 回到 cpython **381/385**、libpng 18/18、git 567/567、lua 35/35、zlib 15/15、sqlite 1/1、tinycc 21/21；tests2 **106 通过 / 0 失败**；`doc/probes.sh` 全部基线（c2ycov 109/0），**cxx2 = cxx3 = cxx4 逐字节相同**。SIMD 预定义那一步曾把 cpython 打到 **149/385**（已撤回）。 |

### R62 修好：定义优先登记 —— ✅ Fujitsu 125 → **128/134**，三个 `undefined reference` 消失

根因（§R61 已测）：块作用域声明与文件作用域定义是**两个 `Sym`**，而
`dumpir.c:1514` 的 `already_emitted()` 按**目标文件名**去重、**先到者胜**（它本是为了合并
glibc 的 `strtoq`/`strtoll` → `__isoc23_strtoll` 这类 asm 别名）。`md->fns` 是按**源码声明顺序**建的，块声明在前，
于是打印器输出了 `declare i32 @f(i32)`（或 `@x = external global i32`）并把定义跳过，链接报
`undefined reference`。

**修法**（`src/parser.c` 的收尾登记段）：把 `md->fns` 与 `md->data` 各做一次**稳定分组**，
**定义（`is_defined && !is_inline_def`）在前、仅声明在后**，组内保持源码顺序。一个名字不可能有两个定义（C 禁止，cxx 也诊断），
所以这个排序只会改变“声明 vs 定义”的胜负，而那正是要修的地方。

#### 验证

| 用例 | gcc | clang | cxx 修前 | cxx 修后 |
|---|---|---|---|---|
| `ord1` 块声明在前、函数定义在后 | ok | ok | **链接失败** | **ok** |
| `blkfn`/《`blkfn2`（函数体内 `extern`） | ok | ok | 链接失败 | ok |
| `obj1`/`obj2`（**对象**同一形状） | ok | ok | 链接失败 | **ok** |
| `ord2`/`ord3`/《fn2`（定义或文件作用域声明在前） | ok | ok | ok | ok |
| `ord4`（块声明后跟 `static` 定义，6.2.2p7） | 报错 | 报错 | 报错 | 报错 |

`ord1` 的 IR 现在是 `define dso_local i32 @f(i32 %tmp0)`。

| | |
|---|---|
| **Fujitsu 样本** | **125 → 128 / 134**，失败 9 → **6**（那 3 个 `undefined reference to ‘func…’` 全消） |
| **验收** | `test/conformance.sh` **236 → 239 passed / 0 gap**（新增：块声明+后置定义运行值 42、定义在前、`static` 后置必须报错）；c2y 101/0；`make test` exit 0；`clang-format-21 --dry-run --Werror src/parser.c` 干净 |
| **全量探针** | 全部基线；**cxx2 = cxx3 = cxx4 逐字节相同**；tests2 106/0 |
| **realworld** | 与基线一致：git 567/567、cpython 381/385（同一批 4 个）、lua/zlib/libpng/sqlite/tinycc 全绿 |
| **仍开着** | 两个 `Sym` 表示一个实体的**深层修法**（让块声明与文件作用域共享同一个 `Sym`，更贴近 6.2.2p2）；另外 `ord4` 目前靠 LLVM 校验报错（`use of undefined value ‘@f’`），而 gcc/clang 给的是具名的 `static declaration of ‘f’ follows non-static declaration`（cxx 已有该诊断，但它检查的是 `sclass`，块声明的 `sclass` 是 `SC_NONE`） |

### R61 块作用域的函数声明：定义被丢掉 —— ⚠️ 已精确复现并追到发射阶段（待修）

Fujitsu 样本里 3 个 `undefined reference to ‘func…’` 的共同形状：**函数在块作用域内声明，定义在文件作用域**。
最小复现（`~/cxxwork/repro/preparse/ord1.c`）：

```c
int main(void) { { int f(int); return f(1) == 2 ? 0 : 1; } }
int f(int x) { return x + 1; }
```

| 顺序 | gcc | clang | cxx |
|---|---|---|---|
| `ord1` 块声明在前、定义在后 | ok | ok | **链接失败** |
| `ord2` 定义在前、块声明在后 | ok | ok | ok |
| `ord3` 文件作用域声明在前 | ok | ok | ok |
| `ord4` 块声明后跟 `static` 定义 | 报错 | 报错 | 报错（三家一致，正确） |

`blkfn2.c`（函数体内显式 `extern int g(int);`）同样失败，所以不是“默认 extern”这一步的问题。

**追踪结果**（临时探针已撤，下面是它打出的）：

```
[def] main  sym=A
[blkdecl] f sym=B ns=(nil)      块声明造了符号 B（new_gvar），只在块作用域里登记
[def] f sym=C ns=…            定义又造了符号 C（另一个 new_gvar）
[reg] f sym=C defined=1 dead=0  C 带着函数体进了 md->fns
[reg] f sym=B defined=0 dead=0  B 也进了
[skip] f defined=0 dead=0       irgen 跳过 B（它确实没有体，正确）
```

也就是：**C 没有被跳过，但 IR 里只有 `call i32 @f(i32 1)` 与一条 `declare i32 @f(i32)`，没有 `define`**——
丢弃发生在 `irgen` 的跳过检查**之后**，即发射/打印阶段（两个同名符号的处理）。

根因已知的一半：块作用域声明把名字登记在**块的** namespace 里（`push_namespace(scope, …)`，`parser.c:5069`），
块一结束就没了；文件作用域那条路径的 `find_ident()` 因此找不到它，又造了符号 C。
按 6.2.2p5，无存储类说明符的函数声明等同 `extern`，**它与文件作用域那个实体是同一个**，所以两个符号应该是一个。

| | |
|---|---|
| **下一步** | 先在发射阶段找到两个同名函数符号如何被归并（打印器的名字分配），再决定修在哪一头：让块声明与文件作用域共享同一个 `Sym`（更接近标准），还是让发射对同名符号取并集 |
| **验收** | 本轮未修代码（临时探针已撤，`grep` 确认为零）；`test/conformance.sh` 236/0、c2y 101/0、`make test` exit 0；复现仍在（作为下一轮的入口） |

### R60 十六进制小数可以没有小数点前的数字 —— ✅ 修好；过滤器再扩一类，Fujitsu 失败 31 → 9

#### （一）真凶不是数字分隔符

上一轮猜的是 C23 数字分隔符（`1'000`）——**猜错了**：cxx 已经支持它（实测三家：
c23/c2y 下都接受；c17 下 cxx 接受而 gcc/clang 拒绝——一处已记录的宽松）。
真正失败的是两类：**虚数字面量后缀**（`i` / `fi` / `iF` / `I`，属 §0 的 `_Complex`）
与 **`0x.8p0f` 这种小数点前无数字的十六进制浮点**。

#### （二）修法：`src/lexer.c` 前缀后的那个检查

词法器在 `0x` 之后**紧接着**要求一个数字（`lexer.c:578`），而 6.4.4.2 的
`hexadecimal-fractional-constant` 允许整数部分为空：`hexadecimal-digit-sequence_opt . hexadecimal-digit-sequence`。
于是 `0x.8p0` 被拆成 `0` + 后缀 `.8p0`，报 `invalid suffix`。修法是在那里把前导 `0` 补回给读数器
（与几行下面 `.5` 被规范化成 `0.5` 同一手法），但要求点后面必须有数字——`0x.p0`
两边都没数字，不是常量。二进制/八进制没有小数形式，依然要求数字。

| 形状 | gcc | clang | cxx（修后） |
|---|---|---|---|
| `0x.8p0`、`0x.8p0f`、`0x.8p0L` | ok | ok | **ok** |
| `0x1.p0`、`0xA.p-2`、`0x1.8p0` | ok | ok | ok |
| `0x.p0`、`0b.1`、`0x.8p` | 拒 | 拒 | **拒** |

运行值与两家**逐位一致**：`0x.8p0`=0.5、`0x.8p1`=1.0、`0x.0000001p0`=4e-9、`0x.8p0L`=0.5。

#### （三）过滤器再扩一类：`_Complex`

按 §0，`_Complex` / `_Imaginary` / `<complex.h>` / `<tgmath.h>` 同样是“明确不做”，所以 `doc/fujitsu.sh` 的
`out_of_scope()` 现在看三件事：① 源文件里的 `_Complex`/`_Imaginary`/两个头文件（关键字无歧义，文本判定安全）；
② clang 的 `-Wdeprecated-non-prototype`（`f()` 与 K&R）；③ **cxx 自己的诊断** `invalid suffix ‘…i…’ on constant`
（虚数面量后缀，两家参考实现都接受，所以只能问 cxx）。

> 一处自己踩的坑：改写时把 `out_of_scope()` 的**调用**换成了关键字检查，函数还在但没人调——
> 那 4 个 `expected 0` 又冒了出来，当场发现并改回。

| | |
|---|---|
| **Fujitsu 样本（192 个测试）** | 失败 **45 → 31 → 9**；现在 **125 / 134 通过**，超出范围 20 OpenMP + 37 无原型/K&R/复数 + 1 参考编译器编不出 |
| **剩下的 9 个全是彼此不同的真缺口** | ① **3 个 `undefined reference to ‘func’/‘func1’/‘func0801’`**（函数没被发须，最可疑）；② `incompatible types when passing argument`；③ `implicit declaration`；④ `#error Need macro __HPC_ACE__ or __SSE2__`（预定义宏）；⑤ `a type specifier is required`；⑥⑦ 退出码/输出各 1 |
| **验收** | `test/conformance.sh` **234 → 236 passed / 0 gap**（新增：五种合法十六进制小数形状加运行值、`0x.p0` 必须被拒）；`test/c2y.sh` 101/0；`make test` exit 0；`clang-format-21 --dry-run --Werror src/lexer.c` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；tests2 仍 **106 通过 / 0 失败**；`doc/probes.sh` 全部基线（含 `d4` 9/0），**cxx2 = cxx3 = cxx4 逐字节相同**。 |

### R59 `(...)` 才是 C23 里“真接受任意实参”的写法 —— ✅ cxx 已经支持（含定义里的一参 `va_start`）

空括号在 C23 变成 `(void)`（§R56）之后，“传什么都行”的正当写法是**裸省略号** `(...)`。
草案的语法直接允许它单独出现（A.3.2 / 6.7.7.1）：

```
parameter-type-list:
        parameter-list
        parameter-list , ...
        ...
```

而 7.16p3 把它的语义说清了：“A function may be called with a variable number of arguments of varying
types if its parameter type list ends with an ellipsis.”

**与旧的无原型形式的关键区别**：`(...)` 是**原型**（prototype），`f()`（C17）不是。
两者都不做形参类型检查（没有命名形参），但原型会做**默认实参提升**并且是可以定义的。

**实测**（gcc / clang / cxx）：

| 用例 | c17 | c23 | c2y |
|---|---|---|---|
| `void f(...);` + `f(1,2,3); f("x",1.5);` | gcc 接受、clang **报错**（“ISO C requires a named parameter before ‘...’”）、cxx 接受 | 三家接受 | 三家接受 |
| `int count(...) { va_start(ap); }`（定义） | gcc/clang **报错**（`va_start` 要求命名参数）、cxx 接受 | 三家接受 | 三家接受 |
| 运行值（`sum(1,2,3,4)` 累加） | — | cxx=gcc=clang=**14** | 同 |

即：**c23/c2y 下三家逐格一致**，cxx 已经支持这个形式——包括定义里的**一参 `va_start(ap)`**（C23 的新形式）。
c17 下 cxx 比 clang 宽松（与 gcc 一致）：那里裸省略号不是 ISO C，clang 报错、gcc 当扩展收下。

| | |
|---|---|
| **结论** | “接受任意实参”在 C2y 里的写法是 `f(...)`；§R56、§R57 过滤掉的只是 `f()` 与 K&R 两种旧形式，不影响这一条 |
| **验收** | 本轮未改代码；conformance 234/0、c2y 101/0、`make test` exit 0 保持不变 |

### R58 把 `f()` 与 K&R 列为“不支持”并从探针过滤 —— ✅ Fujitsu 失败 45 → 31，剩下的都是真缺口

按定调落实两件事：

1. **文档明写**：范围章节 §0 的“明确不做”后面新增一段“**也不支持**”，列出这两种写法与它们在 C23 中的状态
   （弃用到变语义 / 直接删除），并指向 §R56、§R57 的测量。
2. **探针过滤**：`doc/fujitsu.sh` 新增 `out_of_scope()`，在编译之前用
   `clang -std=c17 -Werror=deprecated-non-prototype` 问一次：被这条诊断命中的测试归入 `noproto` 桶，不计入失败。
   选择用编译器而不是正则，是因为这两种写法的边界模糊（函数指针、typedef、多行声明符），
   而 clang 对它们各有一条名字确切的诊断。

**过滤器本身的验证**（三个手写样本）：

| 样本 | 内容 | 结果 |
|---|---|---|
| p1 | `void f();` + `f(1, 2)` | 被过滤（`passing arguments to 'f' without a prototype`） |
| p2 | `int add(a, b) int a; int b; { }` | 被过滤（`a function definition without a prototype`） |
| p3 | `void g(void); g();` | **干净**（不误伤正常原型） |

**过滤后的样本**（192 个测试）：

| | 过滤前 | 过滤后 |
|---|---|---|
| 通过 | 125 / 170 | **125 / 156** |
| 失败 | 45 | **31** |
| 超出范围 | 20 OpenMP + 2 参考编译器编不出 | 20 OpenMP + **14 无原型/K&R** + 2 |

剩下的失败都是真缺口，下一轮的直接工作项：

| 数量 | 诊断 |
|---|---|
| **13** | `invalid suffix ‘X’ on constant`（+另 1 个 integer 版本）—— 疑似 C23 数字分隔符 `1'000` |
| **7** | `expected ‘X’ after top level declarator` |
| **2** | 链接失败：`undefined reference to ‘func’` / ‘func1’ —— 函数没被发尃，值得单独查 |
| 1 + 1 | `#error "not defined macros in float.h"`、`#error Need macro __HPC_ACE__ or __SSE2__` |
| 1 + 1 | `implicit declaration`、`incompatible types when passing argument` |

| | |
|---|---|
| **验收** | 本轮只改文档与探针；conformance 234/0、c2y 101/0、`make test` exit 0 保持不变；`bash -n doc/fujitsu.sh` 通过，`FJ_FILTER_CC=` 可关掉过滤 |

### R57 K&R（旧式）函数定义：C23 是**删除**，不是弃用 —— ✅ 三条证据一致

空括号 `()` 与 K&R 是**两件事**，前者只是语义变了，后者连语法都没了。

**证据一：草案的语法**（草案 38237–38241 行，A.3.4 / 6.9.2）

```
function-definition:
        attribute-specifier-sequence_opt declaration-specifiers declarator function-body
function-body:
        compound-statement
```

C17 的形式是 `… declarator declaration-list_opt compound-statement`，草案里 **`declaration-list` 这个非终结符已经不存在**（全文 7 处都是 `member-declaration-list`，即结构体成员）。

**证据二：草案的 6.11 弃用清单里也没有它们了**。C17 的 6.11.6（空括号函数声明符）与
6.11.7（旧式函数定义）在草案里已经不在——当前清单是 6.11.2 链接、6.11.3 外部名、6.11.4 转义序列、6.11.5 八进制字面量、
6.11.6 后缀运算符、6.11.7 存储类说明符位置、6.11.8 pragma、6.11.9 预定义宏名。它们不在清单里，是因为“弃用”这个状态已经结束：
空括号改了语义（等同 `(void)`，见 §R56），而旧式定义被删除。

**证据三：实测**（`int add(a, b) int a; int b; { … }`）

| 模式 | gcc | clang | cxx |
|---|---|---|---|
| c17 / gnu17 | 接受 | 接受（警告：“a function definition without a prototype is deprecated in all versions of C”） | 报错 |
| c23 / c2y | 仍接受（作为扩展） | **报错**：`unknown type name 'a'`（把标识符列表当原型解析） | 报错 |

顺带一个事实：标识符列表**本来就只能出现在定义里**（C17 6.7.6.3p3：“An identifier list in a function
declarator that is not part of a definition of that function shall be empty”），所以 `int add(a, b);` 三家在**所有模式**下都报错（实测）。

| | |
|---|---|
| **对本项目的影响** | §7（is_fndef 预解析评估）把“`{` 判据与 K&R 天然冲突”列为反对理由之一——**这条理由现在消失了**：K&R 定义在 C2y 里根本不存在，而 cxx 本来就不支持它 |
| **cxx 的现状** | 与 clang `-std=c23/-std=c2y` **一致**（拒绝）；在 c17 下与两家都不一致（那里它们接受）——作为**已记录的差距**保留，优先级低（旧代码才用） |
| **验收** | 本轮未改代码（测量与记录）；conformance 234/0、c2y 101/0、`make test` exit 0 保持不变 |

### R56 空参数列表 `f()`：已弃用，而且 C23 起不再“接受任何参数” —— ⚠️ cxx 在 C17 下判错（待修）

起因是 Fujitsu 样本里 4 个 `too many arguments to function ‘X’; expected 0`。先把三家在三个模式下的行为量清：

| 写法 | 模式 | gcc | clang | cxx |
|---|---|---|---|---|
| `void f();` + `f(1,2,3);`（无定义） | c17 | 接受 | 接受（警告） | **报错** |
| 同上 | c23 / c2y | 报错 | 报错 | 报错 |
| `typedef void g(); g *p; p(1,2);` | c17 | 接受 | 接受 | **报错** |
| `void f() {}` + `f(1);`（定义） | c17 | 接受 | 接受 | **报错** |
| 同上 | c23 / c2y | 报错 | 报错 | 报错 |
| `void f() {}` + `f();` | 全部 | 接受 | 接受 | 接受 |

clang 在 c17 下把结论说得很直白：

```
warning: passing arguments to 'f' without a prototype is deprecated in all
         versions of C and is not supported in C23 [-Wdeprecated-non-prototype]
```

所以：**它从来就是弃用的（deprecated in all versions of C），而 C23 把它彻底取消**——
C23 起 `()` 在**声明与定义两边都**等于 `(void)`，不再是“参数未指定”。

**cxx 的现状**：`func_param()`（`src/parser.c:8087`）只分 `(void)` 与“有参数表”，
`Type` 上**没有“无原型”这一位**，于是 `f()` 与 `f(void)` 在类型上完全相同，在**所有模式**下都按零参数检查。
结果是 C23/C2y 下恰好正确，**C17 下错误**。

| | |
|---|---|
| **修法（下一轮）** | ① `Type` 加一位“无原型”，`func_param()` 在空列表 + 非 C23 模式时置位（C23/C2y 下仍等同 `(void)`）；② 调用检查在无原型时接受任意个数的实参，但要做**默认实参提升**（`float`→`double`、整型提升），并发 clang 同名的 `-Wdeprecated-non-prototype` 警告；③ `is_compatible()` 要让“无原型声明”与“有原型声明”相容（6.7.6.3p15） |
| **影响** | Fujitsu 样本的 4 个失败，以及 `typedef void g(); g *p; p(1,2);` 这类旧代码 |
| **验收** | 本轮未改代码（只做了测量与记录）；`test/conformance.sh` 234/0、c2y 101/0、`make test` exit 0 保持不变 |

### R54/R55 Fujitsu 测试集接入 + `_Atomic` 作为限定符 —— ✅ 探针就位，首轮样本 125/170

#### （一）`_Atomic` 是限定符（6.7.3p1）

`typequal()`（`src/parser.c:990`）只认 `const`/`volatile`/`restrict`，所以 `int * _Atomic p` 报
`expected ‘,’ before ‘_Atomic’`，而两家都接受。作为限定符它指定的是**原子类型**：
`int * _Atomic p` 里原子的是指针本身。新增一行接上 `Q_ATOMIC`——与说明符形式 `_Atomic(T)` 置的是同一位，
所以下游的原子读写路径原样生效。验证：`int * _Atomic`、`int * _Atomic *`、`const _Atomic int *`、
`struct S * _Atomic` 四种写法与 gcc/clang 一致，运行值 `5 7 8`。

#### （二）Fujitsu Compiler Test Suite 接入探针

`~/compiler-test-suite`（fujitsu/compiler-test-suite）：**C/0000 … C/0202 共 204 个目录、37190 个 C 测试**。
它自带的跑法是 LLVM test-suite（CMake + Ninja + lit），所以探针直接做 lit 要做的事：
`lit.local.cfg` 的 `single_source` 意味着每个 `.c` 是独立程序，
而判定是**与参考编译器逐字节对比 stdout 与退出码**。

> 一个值得记下的错误：第一版探针把 `traditional_output` 读成“stdout 必须为空”，
> 结果 569/802 被判为失败。实测 `C/0000/0000_0000.c` 成功时打印 `OK`——参考实现才是唯一诚实的判据。

`doc/fujitsu.sh`：默认每个目录取一个（每个主题都碰到，两分钟），`FJ_LIMIT=0` 跑全量；
`FJ_DIRS` 选目录，`FJ_JOBS` 并行，`FJ_REF` 选参考编译器（空则退化为“无输出 + 零退出”）；
OpenMP 测试跳过（cxx 没有 OpenMP，它自己的 CMake 也会关掉），参考编译器编不出来的计入 `reffail` 而不算 cxx 的账。

**首轮样本结果**（192 个测试，一个目录一个）：

| | |
|---|---|
| 通过 | **125 / 170**（跳过 20 个 OpenMP，参考编译器编不出 2 个） |
| 编译失败 | 43 |
| 退出码不同 / 输出不同 | 1 / 1 |
| **崩溃** | **1** |

失败的诊断分布（前几类）：

| 数量 | 诊断 |
|---|---|
| **13** | `invalid suffix ‘X’ on constant` —— 疑似 C23 数字分隔符（`1'000`） |
| **9** | `a type specifier is required for all declarations` |
| **7** | `expected ‘X’ after top level declarator` |
| **4** | `too many arguments to function ‘X’; expected 0` —— 空括号 `f()` 被当成无参数 |
| 1 | `cxx killed by signal 11` —— **崩溃** |
| 1 | `incompatible types when passing argument` / `when assigning` / `implicit declaration` |
| 1 | `#error "not defined macros in float.h"` |

这些都是下一轮的直接工作项（崩溃优先）。

| | |
|---|---|
| **验收** | `test/conformance.sh` **233 → 234 passed / 0 gap**（`_Atomic` 限定符一条，四种写法加运行值）；`test/c2y.sh` 101/0；`make test` exit 0；`bash -n doc/fujitsu.sh` 通过 |
| **Fujitsu（`doc/fujitsu.sh`）** | 首轮样本：**125 / 170**，失败面见上表 |
| **磁盘** | 发行版已迁到 D: \uff08`wsl --manage Ubuntu --move`），C: 从 5.8 GB 回到 **160.6 GB**；但 `--move` 是块级拷贝，VHDX 仍是 **155 GB 实占**（客户机只用 ~30 GB）——压缩命令见 `compact-wsl-disk.ps1` |

### R52/R53 FFmpeg 剩余失败面的三个真缺口 —— ✅ 24 个原子类型名 + 数组元素限定符（一处修好 ~15 个文件）

（R52 是 A.3.4 预解析方案的评估，见文末 §7；本节是 R53）

R49 后 FFmpeg 剩下的失败面（类型不匹配 26、`a type specifier is required` 13、
`lvalue required in ‘asm’` 1、`expected identifier` 1 等）逐类取代表文件与 gcc/clang 对照，
**七个代表文件全是 cxx 独有的失败**（两家都通过）。本轮修掉两类。

#### （一）`<stdatomic.h>` 缺 24 个 7.17.6 类型名

草案 Table 7.6（类型名等价表）列了 **38** 个名字，cxx 只有前 14 个加 `size_t`/`ptrdiff_t`；
缺的是 `char16_t`/`char32_t`/`wchar_t` 那一行、**八个 `least`**、**八个 `fast`**、`intptr_t`/`uintptr_t`、`intmax_t`/`uintmax_t`，共 **24** 个。
FFmpeg 的 `libavformat/fifo.c`、`allformats.c`、`libavcodec/refstruct.c` 写的就是 `atomic_uintptr_t` 与
`atomic_int_least64_t`，缺了就是 `a type specifier is required for all declarations`。

| | |
|---|---|
| 修法 | `include/stdatomic.h` 按草案顺序补齐整张表（名字与直接类型逐字照抄），并加 `<stdint.h>`（`least`/`fast`/`intptr`/`intmax` 一族从它来） |
| `<uchar.h>` 是 hosted 头 | `char16_t`/`char32_t` 靠它，而**裸机目标没有**：第一版无条件 include 把 `test/atomic.c` 在 rv32 上弄挂了（`uchar.h: cannot open file`）。改用 `__has_include` 守住（实测：它在 cxx 上对主机报 yes、对 rv32 报 no） |
| `char8_t` | C23 才有，按标准用 `__STDC_VERSION__ >= 202311L` 守 |

#### （二）数组里的限定符：一个 bug，三种形状，~15 个文件

26 个“类型不匹配”里有三种形状是**合法的“加限定符”**，而 cxx 报错：

```c
luty = v->curr_luty;                                 /* vc1_mc.c:225  uint8_t (*)[256]      */
bitalloc_tables[i][j] = bitalloc_dst - off[i];       /* dcaenc.c:193  uint16_t (*)[2]       */
const double (*const ctables)[256] = lsbf ? a : b;   /* dsd.c:106     double[CTABLES][256]  */
```

6.7.3p9：写在数组类型上的限定符属于**元素**，所以 `const uint8_t (*)[256]` 就是 `uint8_t (*)[256]` 的限定版本，
赋值就是普通的“加限定符”。cxx 用 `is_compatible()` 判断指向类型是否一致，而它要求限定符**相等**（`type.c:568`），
于是拒绝。gcc/clang 全部接受且不说一句。

| | |
|---|---|
| 修法 | `is_assignable()` 的“指向类型是否一致”改用 `pointee_unqual()`：只剥掉**数组元素那一层**的限定符，数组本身（包括长度）仍然要比；同一层的限定符也用于“丢失限定符”警告的超集判断 |
| **一次踩坑** | 第一版把数组换成了它的元素，于是 `int[4]` 与 `int[3]` 都变成 `int`，**长度检查失效**——`test/c2y.sh` 的“VLA 匹配任意长度”那条定訖（它同时要求错误绑定被诊断）直接抓住，已改正 |
| 保留的水线 | 指针**下面**的限定符属于指向类型，必须一致：`int **` 与 `const int **` 仍然不兼容（C 的著名规则），实测仍被拒绝 |
| 验证 | 三种形状与两个陷阱写成断言；代表文件 `vc1_mc.c` `dcaenc.c` `dsd.c` `motion_est.c` 全部通过 |

**还没动的两类**（已定位）：`libavutil/utils.c:105` 的 `"+m"(state)`（`state` 是**数组**，
cxx 说 `lvalue required in ‘asm’ statement`，两家都接受）与 `libavfilter/vf_curves.c:591` 的声明符列表中间放
`av_unused(version)`（GNU 属性）。

| | |
|---|---|
| **验收** | `test/conformance.sh` **229 → 233 passed / 0 gap**（新增：38 个原子类型名各用一遍加运行值、数组内加限定符、指针下方的陷阱仍被拒、丢失限定符仍告警）；`test/c2y.sh` 101/0；`make test` exit 0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净 |
| **FFmpeg（`doc/ffmpeg.sh`）** | 崩溃 **0**，目标文件 **2251**；剩余 **12** 行 `error:`，类别：类型不匹配 2+1+2、`expected identifier` 2、**非法 IR 2**（`%union.SyncQueueFrame` 与 `%struct.SchedulerNode`）、`redefinition of type` 1、`lvalue required in ‘asm’` 1、`Elementtype` 1 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；tests2 仍 **106 通过 / 0 失败**；`doc/probes.sh` 全部基线（含 `d4` 9/0），**cxx2 = cxx3 = cxx4 逐字节相同**。 |

### R51 内核阻塞点的追查：驱动层的 `-Wp,` / `-Wa,` / `-x` —— ✅ 三类选项现已支持，内核剩下一个**结构性**选择

R48 把内核停点定位到 `scripts/as-version.sh`。本轮把它两个脚本读完并逐条量了一遍：

| 内核的检查 | 脚本要求 | cxx 的实测 |
|---|---|---|
| `cc-version.sh` | `$(CC) -E -P -x c -` 输出 `GCC x.y.z` 或 `Clang x.y.z`，再与 `min-tool-version.sh` 比（gcc 最低 **5.1.0**） | **`GCC 70000`** —— 过门（cxx 的 `__GNUC__` 是 7） |
| `as-version.sh` | `$(CC) -Wa,--version -c -x assembler-with-cpp /dev/null` 的第一行要以 `GNU assembler` 开头；或者命令行里有 `-fintegrated-as` 时直接短路成 `LLVM 0` | **两者都不满足** ⇒ `unknown assembler invoked` |
| `ld-version.sh` | 对 `$(LD)` 做同样的事 | 未到（卡在上一步） |

**顺手挖出的驱动层缺口**：`as-version.sh` 的命令在 cxx 上报的不是“不认识汇编器”，而是
`cxx: fatal error: unknown warning group: -Wa,--version`——`-W` 分支把**所有** `-W…` 当成警告组，
而 `-Wa,`（汇编器选项）、`-Wp,`（预处理器选项）都以 `-W` 开头。内核每一次编译都传 `-Wp,-MMD,$(depfile)`（Makefile 里 **7** 处），
Kbuild 另传 `-Wa,--fatal-warnings`——这两类在 cxx 上全部直接报错。

| | |
|---|---|
| 修法 | `src/main.c`：在 `-W` 分支**之前**接住 `-Wp,` 与 `-Wa,`。`-Wp,-MD,file` / `-Wp,-MMD,file` 就是 `-MD`/`-MMD` + `-MF file` 的另一种写法（cxx 自己写依赖文件），直接映射；`-Wa,` 收集后以一个 `-Wa,a,b` 传给汇编阶段（clang 的集成汇编器本就接受这个形式）；两者之外的内层选项**按名拒绝**，不静默丢弃（静默忽略 `-Wp,-Dfoo` 会改变程序） |
| 另一处 | `parse_opt_x()` 认 `assembler` 但不认 `assembler-with-cpp`，而后者正是内核用的拼法，也就是 cxx 早已有的 `FILE_ASM_PP`（`.S`：先跑预处理器）——接上即可 |
| 细节 | `-Wp,`/`-Wa,` 的内层选项**自带连字符**（`-Wp,-MMD,file`），解析时要先跳一个 `-` |

**修后实测**：`-Wa,--fatal-warnings` 、`-Wp,-MMD,file`、`-Wp,-MD,file` 均通过且依赖文件内容正确（`e.o: \ e.c`）；
`-x assembler-with-cpp` 通过且宏真的先跑；不支持的 `-Wp,-Dfoo=1` 报 `unsupported preprocessor option`。

#### 剩下的阻塞点是一个**身份选择**，不宜硬凑

`as-version.sh` 的两条出路，对应 cxx 的两种自我描述：

1. **自称 GCC**（现状）⇒ 内核不传 `-fintegrated-as`（它在 `scripts/Makefile.clang:28`，只在认定为 clang 时生效）⇒
   必须让 `-Wa,--version` 打出 **GNU assembler** 的版本行——而 cxx 实际用的是 clang 的集成汇编器，打这个行就是说谎。
2. **自称 Clang**（定义 `__clang__`）⇒ 内核走 clang 路径、传 `-fintegrated-as`、脚本直接短路成 `LLVM 0`——
   而这与 cxx 的**实际后端一致**（它就是 clang），但会动到头文件的特性门（`__GNUC_PREREQ` 等），影响面远超内核。

因此本轮**不改**身份，只把事实记录下来：内核要么接受一个“自称 GCC 但用 clang 汇编”的编译器（需要 `-Wa,--version` 回答 GNU 版本），
要么 cxx 改叫 clang。两者都是可以做的工程决定，但都不是一行代码的事，留给下一次讨论。

| | |
|---|---|
| **验收** | `test/conformance.sh` **225 → 229 passed / 0 gap**（四条新增：`-Wp,-MMD,file` 写出并内容正确、`-Wa,` 到达汇编器、不支持的 `-Wp,` 按名拒绝、`-x assembler-with-cpp` 先跑宏）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净 |
| **内核（`doc/kernel.sh`）** | 停点不变（syncconfig 的 `as-version.sh`），但三类选项现已支持，下一步只剩身份决定 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；tests2 仍 **106 通过 / 0 失败**；`doc/probes.sh` 全部基线（含 `d4` 9/0），**cxx2 = cxx3 = cxx4 逐字节相同**。 |

### R50 `-funsigned-char` / `-fsigned-char` 与宏的同步性 —— ✅ 修好三处不一致（3 × 4 单元 + 交叉目标逐格与两家一致）

问题：`-funsigned-char` 到底有没有同步影响 `CHAR_MIN` / `CHAR_MAX` / `__CHAR_UNSIGNED__`。修改前的实测：

| 编译器 | `-funsigned-char` 下 | 语言侧 | `__CHAR_UNSIGNED__` | `CHAR_MIN` | `CHAR_MAX` |
|---|---|---|---|---|---|
| gcc | | unsigned | 1 | 0 | 255 |
| clang | | unsigned | 1 | 0 | 255 |
| **cxx（修前）** | | unsigned | **未定义** | **-128** | **127** |

三处不一致，全在 `src/main.c`：

| # | 现象 | 原因 |
|---|---|---|
| 1 | `__CHAR_UNSIGNED__` 从不定义 | 该宏只在 **arm64/rv32/rv64 的 `target.c` 预定义里**（它们的 ABI 就是无符号），而命令行覆盖只翻转了 `T.ty_char->is_unsigned`，没碰它 |
| 2 | `CHAR_MIN`/`CHAR_MAX` 不跟 | glibc 的 `<limits.h>` 从 `__CHAR_UNSIGNED__` 推出这两个值，所以 #1 一修它就跟了（实测证实：只加宏就变成 0/255） |
| 3 | `-fno-signed-char` 被拒 | 两家都把它当 `-funsigned-char` 的另一种写法，cxx 报 `unknown argument` |

| | |
|---|---|
| 修法 | `src/main.c`：新增 `char_sign_macro()`，在 `cc1()` 里 `machine_flag_macros()` 之后、预处理之前调用：按 `T.ty_char->is_unsigned` 给 `T.predef` **加上或删掉** `#define __CHAR_UNSIGNED__ 1`；`-fno-signed-char` 按无符号处理 |
| 为何放在那里 | 机器字段会重写 `T.predef`，得让它先说完；同时必须在预处理之前，否则头文件看不到 |
| 不受影响 | `signed char` / `unsigned char` 是独立类型，两家都不动，cxx 也不动（断言里同时验了 `sc == -1`、`uc == 255`） |
| 交叉目标 | arm64/riscv64 的 ABI 本来就是无符号：默认 `__CHAR_UNSIGNED__` 为 1、`-fsigned-char` 后为 0——与 clang **逐格相同** |

**验证矩阵**（修后，程序实际看到的值）：

| 编译器 | 模式 | 语言 | `__CHAR_UNSIGNED__` | `CHAR_MIN` | `CHAR_MAX` |
|---|---|---|---|---|---|
| gcc / clang / cxx | 默认 | signed | 0 | -128 | 127 |
| gcc / clang / cxx | `-fsigned-char` | signed | 0 | -128 | 127 |
| gcc / clang / cxx | `-funsigned-char` | unsigned | 1 | 0 | 255 |
| gcc / clang / cxx | `-fno-signed-char` | unsigned | 1 | 0 | 255 |

**已知小差异**：`cxx -dM -E` 的输出不反映这些宏 —— 它走的是另一条宏列表路径（连交叉目标自带的 `__CHAR_UNSIGNED__` 也不显示，而 `#ifdef` 能看到）。
程序侧行为是对的，`-dM` 的展示另计。

| | |
|---|---|
**固化成探针**：`doc/d4.sh` 重写为这一组断言（本机 5 行：默认 / `-fsigned-char` / `-funsigned-char` / `-fno-signed-char` /
两个方向相反的同时给出；交叉 4 行：aarch64 与 riscv64 各自的默认与 `-fsigned-char`），
并加入 `doc/probes.sh` 的每轮名单。它读宏用 `#ifdef` 而不是 `-dM -E`（后者在 cxx 上看不到目标预定义），
并对不认 `-target` 的编译器（gcc）直接跳过交叉行，而不是报成失败。三家实测：cxx 9/0、clang 9/0、gcc 5/0。

**顺带修掉一个三元组缺口**：探针里用 `-target riscv64-linux-gnu` 时 cxx 报 `unknown target`，
而它只认 `riscv64-unknown-linux-gnu`（两家都认前者）。目标别名表里补上 `riscv64-linux-gnu` 与 `riscv32-linux-gnu`。

| **验收** | `test/conformance.sh` **222 → 225 passed / 0 gap**（三条新增：`-fsigned-char` / `-funsigned-char` / `-fno-signed-char` 各自的语言侧、宏侧、`<limits.h>` 与两个固定类型）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；tests2 仍 **106 通过 / 0 失败**；`doc/probes.sh` 全部基线（含新加的 `d4`），**cxx2 = cxx3 = cxx4 逐字节相同**。 |

### R49 FFmpeg 的 `"i"` 约束：常量条件的未选中分支不该发射 —— ✅ 2004 → 2216 个目标文件，该类错误归零

R46/R47 后剩下的最大一类失败是 **245 行 `invalid operand for inline asm constraint 'i'`**（分布在 176 个目标文件上）。
它没有 `file:line` 前缀，而 cxx 源码里也没有这个字符串——是 **LLVM 报的**。

**（1）定位**：从 `make` 的下一行取出真正失败的 176 个目标文件（`libavcodec/4xm.c` 等），
用 `-cc1 -cc1-output` 把 IR 单独存下来再喂给 clang，得到确切的一行：

```llvm
%tmp11 = call i32 asm "shrl $1, $0\0A\09", "=r,i,0,~{dirflag},~{fpsr},~{flags}"(i32 %tmp10, i32 %tmp7)
```

`i` 约束的操作数 `%tmp10` 是 **运行时值**。源头在 `libavcodec/x86/mathops.h`：

```c
static inline uint32_t NEG_USR32(uint32_t a, int8_t s){
    if (__builtin_constant_p(s))
        __asm__ ("shrl %1, %0\n\t" : "+r" (a) : "i" (-s & 0x1F));
    else
        __asm__ ("shrl %1, %0\n\t" : "+r" (a) : "c" ((uint8_t)(-s)));
    return a;
}
```

**（2）根因**：cxx 对 `__builtin_constant_p` 的语义是对的（参数→ 0、字面量→ 1，与 clang 逐条一致），
问题在它把**两个分支都发射出去**：条件已经折叠成常量 0，
`"i"` 那一支不可达，但 LLVM 在优化之前会校验模块里每一个函数，
于是整个翻译单元被拒。gcc 的汇编里只剩 `shrl %cl, %eax`（`"c"` 那一支），
clang 也一样——**两家都丢掉了未选中的分支**。

| | |
|---|---|
| 修法 | `fold_ast` 的 `ND_IF` 分支：条件折叠成整数常量时，只保留取值的那一支；没有 `else` 时换成空语句 |
| 为何是**原地**替换 | 语句链遍历 `next` 时不写回返回值，“返回另一个节点”会被丢掉——调试打印显示折叠确实执行了（`labels=0`、`kind=ND_NUM`、整数），IR 却依然两个块。现在把选中的分支整体拷贝到 `if` 节点上（`next` 保留） |
| 安全边界 | **函数里有标签就不折**（`Sym.labels`）：`goto`、`asm goto` 都可以命名被丢弃分支里的标签。最初的写法是遍历子树找 `ND_LABEL`，而 `Node` 的语句字段与表达式字段**共用 union**，盲目遍历会把一个整数当指针（崩在 `n = 0x4a`），于是改用函数级判据 |
| 验收 | `libavcodec/4xm.c` 等原本失败的单元逐个通过；**FFmpeg 目标文件 2004 → 2216**，崩溃仍为 **0**，该类错误归零；下一个主要类别是类型不匹配 14+8+4 |
| `"i"` 约束本身 | 它没问题：`__asm__("..." : "+r"(a) : "i"(7))` 两家都通过，修后 cxx 也通过（`test/conformance.sh` 新增的断言里有这一条） |

| | |
|---|---|
| **验收（本轮改了代码，全部重跑）** | `test/conformance.sh` **221 → 222 passed / 0 gap**（新增：常量条件只保留取值分支，含 ffmpeg 的 `__builtin_constant_p` 形状、两个运行值与一个含标签的函数；期望值是先用 gcc/clang 实测再写死的）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；tests2 仍 **106 通过 / 0 失败**；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。 |
| **FFmpeg（`doc/ffmpeg.sh`）** | 崩溃 0，目标文件 **2216**；剩余类别：类型不匹配 14+8+4、`a type specifier is required` 13、非法 IR 2（`%union.SyncQueueFrame` 与 `Elementtype`）、`redefinition of type` 1、`lvalue required in ‘asm’ statement` 1、常量类型不匹配 1 |

### R48 工作目录收拾：`~/` 下 2249 个零散文件 + 24 个实验目录 —— ✅ 归档完毕，两个探针搬进仓库

多轮积累下来，`~/` 直接摆着 **2251 个零散文件**（1364 个 `.sh`、851 个 `.py`、其余 `.c/.h/.ll/.log`）
加 **24 个实验目录**。收拾前先核对了依赖：把 `doc/*.sh`、`test/*.sh`、`Makefile` 里的 `$HOME/...` 引用全部列出，
**只有 `$HOME/rw`（探针源码树）与 `$HOME/cxxwork`**，没有任何探针依赖这些零散文件（计划文档里也没有）。

| | |
|---|---|
| 新布局 | `~/cxxwork/active/`（当前轮脚本）、`logs/`（构建与探针日志）、`repro/`（最小复现）、`archive/`（归档），见 **§4.2** |
| 移动 | 2249 个文件 → `archive/loose/`（16M）；24 个目录 → `archive/dirs/`（117M，含 `cxx_head` `qbe` `projects` `tccmix` `tccgcc` `tccboot` `tinycc` `chibicc` 等） |
| 原则 | **只移动、不删除**；`archive/` 确认后整个删掉即可。`download`（Windows 挂载）、`book`、`computer`、`pdf2zh_files` 不属于本项目，未动 |
| 结果 | `~/` 剩 7 个目录（`cxx` 仓库、`cxxwork`、`rw` 源码树，加 4 个不属于本项目的），**零散文件 0** |

**长期重复运行的探针属于仓库**，所以把两天来一直用临时脚本跑的两套搬进了 `doc/`：

| 探针 | 做什么 | 首次运行结果 |
|---|---|---|
| `doc/ffmpeg.sh` | 用 cxx 跑 FFmpeg 自己的 configure（由它决定哪些可选代码路存在）再 `make -k`，报告目标文件数、崩溃数与诊断分布 | 崩溃 0，目标文件 2004 |
| `doc/kernel.sh` | `make defconfig` + `make CC=cxx`，报告停在哪里 | 停在 syncconfig：`unknown assembler invoked`（内核自己的 `scripts/as-version.sh` 认不出 cxx） |

两者都各自带 `timeout`（§4.1 第一条），日志默认落在 `~/cxxwork/logs/{ffmpeg,kernel}/`，可用 `FF_SRC`/`K_SRC`/`FF_JOBS`/`K_JOBS` 等环境变量改路径。

| | |
|---|---|
| **验收** | 本轮**未改编译器代码**（只动了工作目录与探针），所以 `test/conformance.sh` 221/0、`make test`¡、c2y 101/0、四个后端套件与 tests2 106/0 均不变；两个新探针 `bash -n` 通过，`doc/kernel.sh` 实跑复现了内核的停点 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**（块数组归零后仍成立）；tests2 仍 **106 通过 / 0 失败**。 |

### R47 FFmpeg 最后一处崩溃：未归零的块数组 —— ✅ 崩溃 27 → 0（但根因尚未查清）

R46 留下的那 1 处（`libavfilter/bbox.c`）崩在 **IR 打印器**：`dump_blk` 的 phi 循环读到一个垃圾 Phi 节点。
命名的好运在于：**`bbox.c` + `bbox.h` 脱离 FFmpeg 树就能复现**（`bbox.h` 只依赖 `<stdint.h>`），而 gcc 编它正常。

**gdb 给出的位置**：`fn->start == b`——**崩在函数的第一个块**（首块通常没有 phi），`num_blk = 5` 正常。

| | |
|---|---|
| 修法 | `fn->blks` 建好后**逐个块显式归零**（原来只设了 `pred`），再装上 `pred` 数组 |
| 验证 | 加上归零：`bbox.c` **3/3 正常**；撤掉归零重编：**3/3 SIGSEGV**——因果确定 |
| FFmpeg 效果 | 崩溃 **27 → 0**，目标文件 2003 → **2004**；原本崩的 `bbox.o` `flvdec.o` `mpegvideo.o` `h263dec.o` 均可编译（剩下的是别的诊断） |

#### 根因追查：一个被推翻的初判，和一条尚未查清的线索

初判是“`emalloc` 不归零”，写进了代码注释。**这个初判是错的**，已更正：
`src/util.c:191` 与 `:197` 用的都是 **`calloc`**（连池子本身也是 `calloc(1, POOL_SIZE)`），而且池子只单向推进、从不回绕。

那为什么还是脏的？加一行临时打印（已删）看刚分配完的数组：

```
[dbg] num_blk=5  sizeof(Blk)=120 blks=0x7e904a89ed50 phi0=0x7e904a891010 num_pred0=0 head0=0x7e904a890b60
[dbg] num_blk=48 sizeof(Blk)=120 blks=0x7e904a8a1820 phi0=(nil)          num_pred0=0 head0=(nil)
[dbg] num_blk=2  sizeof(Blk)=120 blks=0x721e438514b0 phi0=(nil)          num_pred0=0 head0=(nil)
```

第一个函数的那份**一到手就脏**，而两个指针都落在**同一池的更低地址**上——那是早先分配出去的对象的地址；
后面的函数则是 `nil`。池子只单向推进，所以这块空间应该还没被分配过——
**说明有东西写进了尚未分配的池空间**，即某处**越界写**。

本轮的归零**清掉的是受害区域**，不是那个越界写本身。它已写进代码注释与本节，
列为**独立线索：用 ASan 构一个 cxx（`-fsanitize=address`）跑 `bbox.c` 与自举流程，应能直接定位。

| | |
|---|---|
| **验收** | `test/conformance.sh` 221 passed / 0 gap（本轮未新增：该缺陷依赖堆状态，无法写成确定性断言，取证靠 FFmpeg 与上面的 3/3 对照）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**（块数组归零后仍成立）；tests2 仍 **106 通过 / 0 失败**。 |
| **FFmpeg（尚未纳入 `realworld.sh`）** | 崩溃 **0**，目标文件 **2004**；剩余失败面：`asm` 的 `"i"` 约束 **245**、类型不匹配 14+8+4、`a type specifier is required` 13、非法 IR 1、`lvalue required in ‘asm’ statement` 1 |

### R46 FFmpeg 的 27 处编译器崩溃 —— ✅ 修到剩 1 处（目标文件 1997 → 2003）

R45 记下的 FFmpeg 失败面里，**27 处 `killed by signal 11`** 是编译器自己崩溃，优先级最高。它们分布在 24 个目标文件上，
全是 MPEG 相关的大文件（`mpegvideo*` `flv*` `h26x*` `rv*` 等）。

**（1）定位**：先把日志放到 `~/fflogs/`（R38 已记过 `/tmp` 会被清，这次自己踩了一次），
从 `make` 的下一行（`common.mak:81: <path>.o] Error 1`）取出真正崩溃的目标文件；
崩溃发生在 **`cc1` 子进程**里（gdb 默认跟的是驱动，`set follow-fork-mode child` 也跟错链），
所以直接按驱动的 `-cc1 -cc1-input … -cc1-output …` 形式单进程调用：

```
#0  eval2 (node=..., sym=...) at src/parser.c:4820
        return eval_rval(node->lhs, sym) + node->member->offset;
#2  eval_gvar_data (init=..., ty=...) at src/parser.c:1935
#3  gvar_initializer ... #4 external_declaration ...
```

**（2）根因**：

```c
case ND_SUBACCESS:
case ND_MEMBER: {
    ...
    if (node->ty->kind != TY_ARRAY) error(node->tok, "invalid initializer");
    return eval_rval(node->lhs, sym) + node->member->offset;   // 崩在这里
}
```

两个 case 共用同一段尾巴，而那段尾巴**无条件读 `node->member->offset`**——
`ND_SUBACCESS` 根本没有 member，只要下标的结果类型本身是数组（多维数组的一行）就会空指针解引用：

```c
static int m[2][3];  static int *p = m[1];   /* SIGSEGV */
```

而 `&m[1][0]`、`struct { int a[3]; } s; int *p = s.a;` 都正常——区别就在下标这一支。

| | |
|---|---|
| 修法 | 尾巴先取基址，再按节点种类加偏移：`ND_MEMBER` 加 `member->offset`，`ND_SUBACCESS` 加 `eval(node->rhs) * node->ty->size`（元素自身的步长，多维也对） |
| 验证 | 最小复现编过，且 `p`/`r`/`q`/`t` 的运行值与 gcc **逐字节一致**（`p=4 5 6 r=1 t=7 q=2`，含 `char[2][4]` 与结构成员数组） |
| 效果 | FFmpeg：崩溃 **27 → 1**，目标文件 **1997 → 2003**；失败面里 `invalid operand for inline asm constraint 'i'` 升到 245（因为更多文件走到了那一步） |

**（3）剩下的那 1 处崩溃（已定位，留给下一轮）**：`libavfilter/bbox.c`，崩在 **IR 打印器**：

```
#0  dump_blk (b=...) at src/dumpir.c:436   print_operand(p->arg[i])
p = { result = { type = 4025040960, val = 32767, ty = 0x0 }, arg = 0x6fb,
      blk = 0x5555555ebe28 <ty_int_>, num_arg = 4, next = 0x1100000001 }
```

即某个 **Phi 节点是未初始化/已被复用的**（`arg` 是个小值、`next` 是垃圾）。
把 `bbox.c` 里那个宏展开成等价的独立片段（嵌套循环 + `goto` 出来 + 指针递进）**编得过**，
所以触发点还需 FFmpeg 的具体上下文；下一步用预处理后的 `.i` 逐个函数削减。

| | |
|---|---|
| **验收** | `test/conformance.sh` **220 → 221 passed / 0 gap**（新增：`m[1]`/`m[0]`/`&m[0]`/`c[1]`/`s.a` 五种行地址，含运行值）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**；tests2 仍 **106 通过 / 0 失败**。本轮修的是常量求值器里一个空指针解引用，七棵树里没有 `int m[2][3]; int *p = m[1];` 这种形状；变化在 `test/conformance.sh` 220 → 221 与 FFmpeg 的崩溃 27 → 1。 |
| **FFmpeg（尚未纳入 `realworld.sh`）** | 崩溃 27 → 1，目标文件 1997 → **2003**；剩余失败：`asm` 的 `"i"` 约束 **245**、类型不匹配 14+8+4、`a type specifier is required` 13、非法 IR 1、`lvalue required in ‘asm’ statement` 1、以及上面那 1 处崩溃 |

### R45 新两棵树：Linux 内核与 FFmpeg —— ✅ 修掉 `-Wpointer-sign` 的严格性差异（FFmpeg 22 → 1997 个目标文件）

下载了`linux-6.12`（1.6G 树）与 `ffmpeg-7.1`（101M 树）到 `~/rw/`，各自尝试构建。

#### （一）FFmpeg：被一条严格性差异卡住，修后推进两个数量级

`./configure --cc=$HOME/cxx/cxx` **exit 0**（只有一句“未知编译器，无法选最优 CFLAGS”）；
CFLAGS 落在 `-std=c17 -fPIC -pthread -g -Wall -Wno-unused-const-variable`。构建在 **22 个目标文件**时停下：

```
libavdevice/v4l2.c:717:44:       av_strcasecmp(standard.name, s->standard)
libavfilter/af_adeclick.c:365:27 av_fast_malloc(&c->y, &c->y_size, ...)
   均为 error: incompatible types when passing argument
```

两处都是**指向类型只差符号**：`__u8[32]`（即 `unsigned char *`）传给 `const char *`，
`int *` 传给 `unsigned int *`。gcc/clang 用 `-Wpointer-sign`（在 `-Wall` 里）**警告并照常转换**，
而 `is_assignable()` 只在指向类型 `is_compatible` 时接受（`int` 与 `unsigned int` 不兼容），于是报错。

| | |
|---|---|
| 修法 | `src/type.c` 新增 `sign_only_difference()`（同尺寸、符号不同、排除枚举），`is_assignable()` 在最后一步之前收下它并给出 `WG_POINTER_SIGN` 警告；与已有的 `WG_DISCARDED_QUALIFIERS` 同一形状（警告并接受） |
| 警告组 | 表已用满 24 位（`WG_ALL = (1u<<25)-1`，位 24 是 discarded-qualifiers），所以 `WG_ALL` 扩到 `(1u<<26)-1`，新组 `WG_POINTER_SIGN = 1u << 25`，名字取两家共用的 `-Wpointer-sign` |
| 不变的部分 | 真正的不匹配（`int *` → `double *`）仍然是 **error**，与 gcc 一致 |

**修后重跑** `make -k -j8`：**1997 个目标文件**（从 22）。剩下的失败面（按数量）：

| 数量 | 诊断 |
|---|---|
| **223** | `invalid operand for inline asm constraint 'i'` —— 内联 asm 的 `"i"`（立即数）约束，目前主要阻塞点 |
| **27** | `cxx killed by signal 11` —— **编译器自己崩溃**，无论如何都是缺陷 |
| 13 | `incompatible types when passing argument`（非符号差异的真实不匹配） |
| 13 | `a type specifier is required for all declarations` |
| 5 / 4 | `incompatible types when assigning` / `initializing` |
| 2 | `expected identifier or ‘(’` |
| 1 | `'%tmp7' defined with type 'ptr' but expected '%union.SyncQueueFrame'` —— **非法 IR** |
| 1 | `redefinition of type`；1 × `lvalue required in ‘asm’ statement` |

#### （二）Linux 内核：卡在 kconfig 的汇编器探测上

`make defconfig` 成功（它用宿主 gcc 构建 `scripts/`）；随后 `make -j8 CC=$HOME/cxx/cxx` 在 **syncconfig** 阶段停下：

```
/home/memory/cxx/cxx: unknown assembler invoked
scripts/Kconfig.include:51: Sorry, this assembler is not supported.
```

那句话是**内核自己的** `scripts/as-version.sh` 说的（cxx 二进制里没有这个字符串，已用 `strings` 核实）：
它调 `$(CC)` 当汇编器用、从版本输出里认名字，而 cxx 的 `-v`/`--version` 输出它认不出来。这是**驱动层的兼容性差距**（不是 C 语言层），下一步可以先看它到底在等什么格式。

| | |
|---|---|
| **验收** | `test/conformance.sh` **217 → 220 passed / 0 gap**（三条新增：符号差异两处各告警且编译通过；`-Wno-pointer-sign` 静默；真实不匹配仍拒）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；tests2 仍 **106 通过 / 0 失败**；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**——本轮改的是一条诊断的严重程度，七棵树里没有这种形状；变化在 `test/conformance.sh` 217 → 220 与下面的 FFmpeg 两个数量级。 |
| **新树（本轮引入，尚未纳入 `realworld.sh`）** | FFmpeg：**22 → 1997 个目标文件**，上表为剩余失败面；Linux 6.12：defconfig 通过，目标构建卡在 kconfig 的汇编器探测 |

### R44 探针全量扫描：41 个脚本里的两处异常 —— ✅ 一处修正、一处因探针树被改坏

tests2 全绿后，回过头检查验证网本身：`doc/` 下共 **41 个探针脚本**，而 `doc/probes.sh` 只驱动 12 个，
我每轮又只看它的末八行（`c2ycov*` 那几行一直被截掉）。于是把剩下的全跑一遍（各带 `timeout`）。

| 结果 | |
|---|---|
| 全部通过 | **36 个**（alias asm bootstrap c2ycov2 c2ycov3 c2ycov4 c2ycov5 cleanup d4 driver e12 e3c e3 effects fall inline kw minbug narrow ped selfbuild selfhost shift2 shift3 shift signcmp suite t1 tent uf vlaparam vmgoto wall warn wgroups worder） |
| 异常 1 | `c2ycov: 109 passed, 2 failed` —— 失败的是 `<complex.h>` 与 `<tgmath.h>`，而它们正是§0 里**有意不做**的那两个头（`_Complex`） |
| 异常 2 | `pycxx` —— cpython 源树的 `configure` 报语法错误 |

**异常 1 的修正**：`doc/c2ycov.sh` 没有 `conformance.sh`/探针习惯的“已知缺口”概念，把它们算成失败。
新增 `gap()`（与 `ok()` 并列）并在头文件循环之后单独探测这两个头；汇总行因此变为
`c2ycov: 109 passed, 0 failed, 2 known gap(s)`——与 `c2ycov2` 的“3 library gap(s)”同一口径。
（顺带踩到一个 shell 坑：`printf ... | gap` 会让函数跑在子 shell 里，计数器丢失；改用 here-doc。）

**异常 2 的诊断**：`~/rw/cpython` 是 git 树，`git status` 显示 **`M configure`、`M configure.ac`**，
`bash -n configure` 报 `syntax error near unexpected token \`;;'`。看 diff：有人（很可能是早前为绕开某处而做的手术）
从 `configure.ac` 删掉了 `if test "$Py_LTO" = 'true'; then case $ac_cv_cc_name in clang) ... esac fi` 整块，
但生成物 `configure` 里只删了一半，残留的 `else case e in ... esac fi` 碎片让整个脚本无法解析。
探针 `doc/pycxx.sh` 的目的正是“让 cpython 自己的 feature test 决定哪些可选代码路存在”（现有的 `pyconfig.h` 来自一次 clang 配置，
于是打开了 `_Py_HAVE_EFFICIENT_BUILTIN_SHUFFLEVECTOR` 等 cxx 服务不了的 SIMD 单元，报告却把账算在 cxx 头上）。
处理：把两个被改的文件备份到 `/tmp/cpython-edits/`，再 `git checkout -- configure configure.ac` 恢复跟踪版本（`bash -n` 重新通过），
然后按探针本意重跑 `bash doc/pycxx.sh ./cxx`。

| | |
|---|---|
| **验收** | 36 个探针脚本全部通过；`doc/c2ycov.sh` 改后 **109 passed, 0 failed, 2 known gap(s)**（`c2ycov2` 34/0/3 不变）；cxx 本身未改代码，`test/conformance.sh` 217/0、tests2 106/0 与七棵树记分均不变 |
| **`pycxx`（configure + build cpython）** | 探针现在能跑了：**configure exit 0**（日志 `~/rw/pycxx/configure.log`），`make -k -j8` 建出 **289 个目标文件**后失败（exit 2），三类：① `check-clean-src`（Makefile:950）——源树里有上一次构建留下的产物，与 cxx 无关；② `Modules/_blake2` 与 `Modules/_hmac` 链接失败（`ld returned 1 exit status`）——它们依赖 HACL 的 SIMD 单元，而后者编不过；③ 冻结模块步骤（`Python/frozen_modules/*.h`）报 **`Python init error: interpreter already initialized`** ×5——这是 cxx 编出的 `_freeze_module` 运行时的症状，是一条新线索。**另一个发现**：configure 选了 `_Py_HACL_CAN_COMPILE_VEC128 1` 与 `VEC256 1`——cxx 对 `vector_size`/`ext_vector_type` 是“警告并忽略”（§0 已记的有意差异），于是 cpython 的特性测试**误以为向量可用**，把 SIMD 路径打开了。下一步：先清源树再跑一次，把①排除掉，再看②③ 在干净树上是否依然如此 |

### R43 `#pragma pack` 下的对齐位域 —— ✅ `95_bitfields` 全文匹配，**tests2 全绿（106 / 0）**

R40/R41/R42 后只剩下 `95_bitfields` 的 12 行，全在 **"PACKED - WITH ALIGN"** 两段上。之前我把
`packed`/`aligned`/位域对齐的各种直接组合写成最小用例，三家都一致，所以以为触发点在宏展开细节里——**错在我看了错的块**。

**定位过程**：

1. 让 gcc/clang 各自跑一遍这个用例：**gcc 与 `.expect` 逐字节一致**，而 **clang 与 cxx 差的是同一那 12 行**——
   说明这不是 cxx 自己的怪癖，而是 gcc 的一条我们两家都没实现的规则。
2. 把 gcc 的 `-E` 输出摆出来看四个 TEST 2 块：第 3、4 块的 *struct 文本完全相同*，而 `.expect` 对它们要求不同的结果——
   于是去看源文件本身：第 **105 行 `# pragma pack(push,1)`**、第 **129 行 `# pragma pack(pop)`**——PACKED 那几段在自己的 pragma 里，
   而不是靠 `__attribute__((packed))`。

| | |
|---|---|
| 规则 | `#pragma pack(N)` 把成员的对齐**压到 N**（这里 N=1），但**显式加了 `aligned` 的位域仍然要从该对齐开始**——即 1 字节边界。`long long z:63` 到第 81 位结束，`a` 因此落在第 88 位，记录是 **12** 字节而不是 11 |
| 修法 | R41 写的是 `if (mem->is_align && mem_align > 1) s = ALIGN_UP(s, mem_align * 8);`——**漏了底档**。去掉 `> 1` 即可：对齐是 1 字节时也要对到 8 位 |
| 为何没被最小用例发现 | 我的最小用例用 `__attribute__((packed))`，而**属性与 pragma 在 gcc 里不同**：`packed` 属性会被成员的 `aligned(16)` 盖过（我的 P1 探测：16/32），`#pragma pack` 不会（压到 1） |

| | |
|---|---|
| **验收** | **tests2 全绿：105 → 106 通过 / 0 失败**（参考实现 gcc 能复现的 106 个用例全部通过）；`test/conformance.sh` **216 → 217 passed / 0 gap**（新增：`#pragma pack(push,1)` + `aligned(16)` 位域的 **1/12**、同一声明不加 pragma 的 **16/32**、以及无 `aligned` 的 **8/24**，并验证字段真在第 11 与第 16 字节）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。本轮改的是 `#pragma pack` 下位域的起始位置，七棵树里没有这种组合；变化在 **tests2 全绿（106/0）** 与 `test/conformance.sh` 216 → 217。 |

### R42 VLA 的地址只取决于它的长度 —— ✅ `122_vla_reuse` 转绿（tests2 **105**）

`122_vla_reuse` 要求比 R33 笔记里写的更严：它在前 100 轮把**每个长度对应的地址记下来**，
之后每一轮都拿 `&x[0]` 与 `p[n % 100 + 1]` 比较，所以地址必须是**长度的单值函数**。
gcc/clang 靠把 `n % 100 + 1` 推出上界 100 、把数组提升进帧实现；cxx 没有值域分析，于是换一种等价的、不需要上界的做法。

| | |
|---|---|
| 机制 | 每个 VLA 声明一个属于自己的 `base` 槽：**首次执行记下当时的栈指针**，以后每次到达都先 `stackrestore(base)` 再 `alloca`——地址因此是 `base - round_up(size)`，只与长度有关 |
| `base` 的初值 | 必须是 NULL，而存它的地方只能在**循环外**——就是函数序言。这正是 R31 那个“标志没处放”的症结：现在解析器把这些存储收集起来（`fn_prologue_*`），在函数定义完成时插到体首 |
| 守卫 | `if (base) stackrestore(base);` —— 手搭的 `ND_IF` 必须 `cnt_blk(2)`（gen_if: then / merge），否则 `new_blk()` 断言立到报错 |

**（重要）我自己的第一版是不安全的，被基准测量抓住了**：

回退到“自己的 base”会释放它之后分配的一切，而 **多个 VLA 的对象是同时存活的**。
`vla3size`（`int a[n], b[2n], c[3n];` 在循环里）的对距从与 gcc 逐字节相同的 `32/48` 变成了 `32/32`——
即 **c 的区间压进了 b**。修正：守卫只在**函数内只有一个 VLA 声明**时生效（那时它后面没有别的动态对象）；
数量要到体解析完才知道，所以守卫先发出、在函数定义收尾时把 `>1` 的那些的条件换成常量 0。
于是：`122`（一个）与 `79_vla_continue`（一个）用新机制，多声明的保持原来的纯栈式分配。

| 验证 | 结果 |
|---|---|
| `122_vla_reuse` | **OK** |
| `79_vla_continue` | MATCHES（无回归） |
| `vla3size`（多 VLA） | **32/48**，与 gcc 逐字节相同（不再重叠） |
| `vla3loop` / `vla3goto` | 每轮同址（moved=0/0/0） |

| | |
|---|---|
| **验收** | **tests2 104 → 105 通过 / 1 败**（只剩 `95_bitfields` 的 PACKED-WITH-ALIGN 宏组合）；`test/conformance.sh` **214 → 216 passed / 0 gap**（两条新增：地址只取决于长度（`122` 的形状，20000 轮）；同一作用域多个 VLA 不重叠）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**——本轮动了 VLA 的分配路径，而 cxx 自己的源码里没有会走到那个守卫的形状（一个函数里只有一个 VLA），所以逐字节不变；变化在 **tests2 104 → 105** 与 `test/conformance.sh` 214 → 216。 |

### R41 成员上的 GNU `aligned` 属性 —— ✅ 与两家一致含位域成员（`95_bitfields` 再进一步）

R40 定位到的第二处，这一轮修好了。

| | |
|---|---|
| 病灶 | 成员解析处调 `declspecs(&tok, tok, NULL, &align, NULL, NULL)`（`src/parser.c`）——**对齐度与属性表两个出参都是 NULL**，于是说明符位置的 `__attribute__((aligned(N)))` 被读进来又丢掉；只有声明符之后的写法走 `mem->ty->attrs` 那一支 |
| 修法一 | 接住 attrs 并 `attr_decl_apply(mem_attrs, &mem_funcspec, &align, true)` |
| 修法二 | 显式对齐的**位域**还要从它的对齐开始：gcc 把 `__attribute__((aligned(16))) char a : 4;` 放在记录的第 16 字节，而不是紧跟前一个字段 |
| 修法三 | `align` 现在混了两个来源，而 **`_Alignas` 用在位域上仍须报错**（gcc 也报）：用 `alignas_align` 保留 `declspecs` 给出的那个值，两处 `'_Alignas' cannot be applied to a bit-field` 看它 |

| 最小探测 | gcc / clang | cxx 修前 | cxx 修后 |
|---|---|---|---|
| `struct { char a; __attribute__((aligned(16))) int b; };` | 16 / 32 | 4 / 8 | **16 / 32** |
| `struct { __attribute__((aligned(16))) char a : 4; };` | 16 / 16 | 1 / 1 | **16 / 16** |
| `struct { char a; int b; };`（对照） | 4 / 8 | 4 / 8 | 4 / 8 |
| `_Alignas(16) char a : 4;` | 报错 | 报错 | **报错** |
| 声明符之后的写法 | 16 / 32 | 16 / 32 | 16 / 32 |

#### `95_bitfields` 剩下的 12 行（已定位到宏组合，价值低）

差异全在 **"PACKED - WITH ALIGN"** 的 TEST 2 / TEST 3 上：期望 `align/size : 1 12` 与 `1 7`，
cxx 给 `1 11` 与 `1 6`（少一字节），位图因此在高位字节上不同。
但我把 `packed`、`aligned`、位域上的 `aligned`各种直接组合写成最小用例后，**三家逐行一致**（`P1..P5`、`T1..T3`），
说明触发点在该用例宏组合的细节里（`SELF` 自包含 + `M`/`P`/`A`/`ALIGN`/`PACK` 的展开），不再深追。

| | |
|---|---|
| **验收** | `test/conformance.sh` **212 → 214 passed / 0 gap**（两条新增：成员 `aligned` 的四种形状 + 位域对齐落在第 16 字节 + 声明符之后的写法；以及 `_Alignas` 在位域上被拒）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；tests2 仍 **104 通过 / 2 败**；`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**——这一条对本轮尤其有意义：改的是布局，而 cxx 自己的源码里没有会因此改变布局的成员对齐；变化在 `test/conformance.sh` 212 → 214。 |

### R40 跨过八字节边界的位域（`95_bitfields` 的主体）—— ✅ 打包形形已与两家一致

tinycc 的 `95_bitfields.c` TEST 2 是一个 `packed` 记录：`long long z:63` 前面有 `int x:12` + `char y:6`，
于是 `z` 从第 18 位开始、到第 81 位结束——**起步在一个八字节单元内、结束在下一个**。

| | |
|---|---|
| 原因 | 单元宽度由 `min_bytes_for_bits(bit_offset + width)` 决定，而它到 8 字节就停了；于是单元只有 8 字节而字段需要 9，读取的移位量算成 `64 - 63 - 2 = -1`，LLVM 对移位量取模 64，`z` 读回 `0xc000000000000000`（两家是 `123456789abcdef0`） |
| 修法一 | `min_bytes_for_bits` 超过 64 位后返回 `(bits + 7) / 8`；`get_unit_ty` 超过 8 字节时取 `bitint[bytes * 8][is_unsigned]`——cxx 已经有 `_BitInt` 到 128 位（`bitint[129][2]`），`bit_offset <= 7` 且字段不超 64 位，所以最远 9 字节 |
| 修法二 | 这类单元的掩码不再装得进 64 位，而 `Ref` 的立即数只有 `int32_t`，所以掩码改走 `newcon()` 的 **iN 常量**（`CBits128`） |
| 修法三 | 移位量要用单元的**位宽**而非 `size * 8`：`_BitInt` 的 size 会向目标粒度取整（`_BitInt(72)` 在 x86-64 上 size 是 16），而只有 72 位存在——用 128 算出 `shl i72 ..., 63` 会把字段自己丢掉。现用 `bitint_width()` |

验证：TEST 2 的四行输出（`bits in use` / `bits as set` / `values` / `align/size : 1 11`）**与 gcc、clang 逐字节相同**。

#### `95_bitfields` 为什么还是红的（已定位到另一处）

剩下的差异全在“WITH ALIGN”变体上（`A` = `__attribute__((aligned(16)))`），而它不是位域问题：

| 最小探测 | gcc / clang | cxx |
|---|---|---|
| `struct { char a; A int b; };` | align 16, size 32 | **align 4, size 8** |
| `struct { A char a : 4; };` | align 16, size 16 | **align 1, size 1** |

即 **cxx 完全忽略成员上的 GNU `aligned` 属性**。位置已找到：成员解析处调用
`declspecs(&tok, tok, NULL, &align, NULL, NULL)`（`src/parser.c` 约 6877 行），**`align` 和 `attrs` 两个出参都给了 NULL**，于是说明符位置的
`__attribute__((aligned(N)))` 被读进来又丢掉；而声明符之后的写法（`int b __attribute__((aligned(16)))`）走 `mem->ty->attrs` 那一支，是有效的。
下一步：把 attrs 接住并 `attr_decl_apply(attrs, NULL, &align, true)`，再回到 `95_bitfields` 验证。

| | |
|---|---|
| **验收** | `test/conformance.sh` **211 → 212 passed / 0 gap**（新增：跨八字节边界的位域：`sizeof`、写入后的字节、读回值、指针读写、负值、邻居字段）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；tests2 仍 **104 通过 / 2 败**；`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。本轮改的是位域单元与其掩码/移位量：七棵树里没有跨八字节边界的位域（有的话会读出错值），而**自举逐字节不变**说明对不跨界的位域路径毫无影响；变化在 `test/conformance.sh` 211 → 212。 |

### R39 一个真实的误编译：`指针 - unsigned`（T2 的根因）—— ✅ tcc 字节级自举恢复

**（1）从自举失败到一个表达式**

R38 把 T2 缩到了“目标文件相同、链接产物不同”，这一轮再往下挖：

- 预处理输出**相同**；
- `-c` 目标文件**逐字节相同**（两个 tcc 编 `tcc.c`）；
- 链接产物相差 32 字节，而差异在 **`.eh_frame_hdr`**：gcc 侧 `0x24`（= 12 字节头 + 3 条表项），cxx 侧 `0x0c`（**表项数为 0**）；
- 这个表是 tcc 自己的链接器写的：`tccdbg.c:tcc_eh_frame_hdr()`。

把它的解析循环**原样抠出来**，喂真实的 `.eh_frame` 字节，用 gcc 与 cxx 各编一遍：

```
gcc: count=3
cxx: count=0        (trace：cie version=0 —— 应为 1)
```

**（2）根因：索引在自己的类型里取负，再被零扩展**

最小复现（`~/t2/mini.c`）：

```c
unsigned char *rd = data + 56;
unsigned int cie_offset = 28;
unsigned char *cie = rd - cie_offset + 4;   /* 应为 data+32 */
```

| | `cie - data` |
|---|---|
| gcc | **32** |
| cxx（修前） | **4294967328**（= 2³² + 32），随后解引用直接段错误 |

cxx 为它生成的 IR：

```llvm
%tmp13 = sub i32 0, %tmp12        ; -28，在 32 位里取负
%tmp14 = zext i32 %tmp13 to i64   ; 零扩展 → 4294967268   ❌
%tmp15 = getelementptr i8, ptr %tmp11, i64 %tmp14
```

修法在 `new_sub()` 的 `ptr - num` 分支（`src/parser.c`）：指针减法的整数操作数是 `ptrdiff_t`，
先 `lvalue_convert` + `new_imcast(&rhs, T.ty_long)` 再取负 —— 与同一函数里 `ptr - ptr` 分支已有的写法一致，符号就从源类型来了。
修后：`mini` 与 `mini2`（`int`/`unsigned int`/`unsigned long`/再 `+4`/解引用/循环上界）全部与 gcc 一致，抽出的解析循环 `count=3`。

**（3）端到端验证：tcc 自己的自举测试**

用修好的 cxx 重建 tinycc（`./configure --cc=$HOME/cxx/cxx && make -j8`）后：

| 目标 | 修前 | 修后 |
|---|---|---|
| `test1` / `test2` / `test3` | OK | **OK** |
| `tccb`（`-b` 自编译两个可执行文件必须 `cmp` 相等） | **失败** | **`Exe Bound-Test OK`**，`tccb1 == tccb2` 逐字节相同 |

| | |
|---|---|
| **验收** | `test/conformance.sh` **210 → 211 passed / 0 gap**（新增：指针减 `unsigned int`/`int`/`unsigned long`、再 `+4`、解引用、用作循环上界）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；tests2 仍 **104 通过 / 2 败**；`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。本轮修的是指针减法的索引类型，七棵树里没有 `指针 - unsigned` 这种形状（真有的话会直接错位崩掉），所以记分不动；收获在两处：tinycc 自己的全套测试从“卡在 `tccb`”到 `------- ALL TESTS PASSED --------`，以及 `test/conformance.sh` 210 → 211。 |
| **tinycc 自己的全套测试** | `make -C tests test CC=gcc`（参考用 gcc 编，被测的是 cxx 编出的 tcc）：**exit 0**，末行 `------- ALL TESTS PASSED --------`（含 `test1/2/3`、`test1b` 边界检查、`test4` 对象/链接输出、`tccb` 字节级自我复现） |

### R37 两项验证：多 VLA 的地址偏移；cxx 编出的 tcc 能否通过自己的自举测试

#### （一）多个 VLA 声明时第 2、3 个的地址偏移

三家同一段源码，测量（`~/lifetest/vla*.c`）：

| 形状 | gcc | clang | cxx |
|---|---|---|---|
| 一个作用域三个 **运行时长度**的 VLA（`int a[i],b[j],c[k]`，i=3,j=5,k=7） | b-a=32 c-b=32 | 32 / 32 | **32 / 32**（完全一致） |
| 同一形状但长度是**常量**（`a[3],b[5],c[7]`） | **-12 / -32**（向上） | 20 / 32 | **20 / 28**（紧致，无填充） |
| 循环体内三个 VLA，每轮都离开块 | 稳定 | 稳定 | **稳定**（moved=0/0/0） |
| 长度逐轮变大（`n,2n,3n`）的循环 | 16/16,16/32,32/48,32/48 | 同 | **同（逐字节一致）** |
| `goto` 回块内（三个 VLA，回到块外标签） | 稳定 | 稳定 | **稳定** |

结论：**运行时长度时三家逐字节一致**；每个 VLA 都单独 `stacksave`/分配，第 2、3 个的偏移就是它们的大小（按对齐向上取整），
且在循环里**每轮回到同一位置**（这正是 R33 那个出口边释放的作用，之前每轮漂一个数组大小）。
与两家的差别只在**常量长度时的对齐/填充策略**（cxx 紧致、clang 按 8 位、gcc 往上摆），那是不可观测的布局差异；
而 `122_vla_reuse` 那种“跳回本作用域内部、从不离开”的形状仍然漂移（两家靠优化器提升成固定槽位，见 R33）。

#### （二）cxx 编出的 tcc 跑自己的自举测试

做法：`cp -a ~/rw/tinycc ~/tccboot && cd ~/tccboot && ./configure --cc=$HOME/cxx/cxx && make -j8`
—— **configure 与 make 均成功**，`tcc`（515KB）与 `libtcc1.a` 都是 cxx 编出来的。然后跑 tinycc 自己的测试：

```
make -C tests test1 test2 test3 CC=gcc      # 参考用 $(CC)，自举链用 $(TCC)
```

| 目标 | 内容 | 结果 |
|---|---|---|
| `test1` | cxx 编的 tcc 跑 tcctest | **Auto Test OK** |
| `test2` | 它编译 tcc，再跑 tcctest | **Auto Test2 OK** |
| `test3` | 它编译 tcc 再编译 tcc（三级），再跑 tcctest | **Auto Test3 OK** |
| `test1b` | 边界检查（`-b`）运行结果 | **Auto Bound-Test OK** |
| `tccb` | **字节级自我复现**：`tcc -b tcc.c` 与“那个产物再 `-b tcc.c`”两个可执行文件必须 `cmp` 相等 | **失败** ❌（同一目标在 gcc 宿主树里通过：`Exe Bound-Test OK`） |

**归因（已做到可复现的最小形式）**：同一份 `tcc.c`、**同一个 `libtcc1.a`**（gcc 宿主树的），两个不同的 tcc（一个 gcc 编的、一个 cxx 编的）各自编译，输出差 **369528 字节**；
甚至 `int main(){return 42;}` 这样的输入也差（cxx 侧 3523 字节、gcc 侧 3555 字节，**入口点差 32 字节**）。两边的节表与符号数相同，
差异在链接进去的内容上。而 gcc 宿主树里的自举是**不动点**（`tcc` 编的与它自己编的逐字节相同）——
所以这不是 tcc 自身的不确定性，而是**cxx 编译 `tcc.c` 改变了 tcc 的代码生成行为**（系统性、可复现）。

**结论**：**功能自举通过**（三级链 + 边界检查，输出与 gcc 参考逐行一致），
**字节级自我复现不过**——已开为工作项 T2。

**附：一个预处理器差异（已知，与 clang 同病）**：`tests/tcctest.c:70-77` 故意写了

```c
#define funnyname 42test.h
#define incdir tests/
#define incname < incdir funnyname >
#include incname
```

它要求展开成 `<tests/42test.h>`（8.1 节 `< h-char-sequence >` 在不是单一头文件名记号时先宏展开）。
gcc 把展开后的记号**直接拼接**，cxx 与 clang 一样插了空格，于是报 `tests/ 42test.h: cannot open file`。
因此用 cxx 当宿主跑这套测试时，参考那一步必须用 gcc（`CC=gcc`）；这条差异已记在此处供后续取舍。

| | |
|---|---|
| **记分（`doc/realworld.sh` 全量探针）** | 本轮未动代码（只做验证），七棵树记分与 R35 相同：**git 567/567、cpython 381/385**、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21 |

### R35 区间设计符只求值一次（第二次，这次定位到底）—— ✅ `90_struct-init` 全文匹配（tests2 103）

R31 写过一版，最小复现对了（`1 1 2 2`），但 `90_struct-init` 一跑就崩，当时只做了二分、没做诊断，于是回退。这一次把崩溃定位到底了。

**（1）现象与定位**

崩在 `test_multi_relocs`，`rip = 0x7568`（一个垃圾函数指针）。把那张表单独抠出来编译，IR 里一目了然：

```llvm
%tmp5 = alloca ptr, align 8      ; [0 ... 3] 的临时量
%tmp6 = alloca ptr, align 8      ; [1 ... 2] 的临时量
...
store ptr null, ptr %tmp11       ; [1] = 0 覆盖了该元素，同时也把它身上的赋值带走了
%tmp14 = load ptr, ptr %tmp6     ; [2] 读的是**从未写过**的临时量
store ptr %tmp14, ptr %tmp13
```

原因：临时量的赋值被挂在**区间的第一个元素**上，而后面的 `[1] = 0` 会用新初始化器整个覆盖那个元素——
赋值跟着一起消失，而读它的元素还在。临时量里是栈上的垃圾，于是调到 `0x7568`。

**（2）修法：赋值挂在“元素”而不是“元素的初始化器”上**

`Initializer` 新增 `Node *pre`：在该元素位置上跑的副作用，不受元素自身初始化器被覆盖的影响。
`create_lvar_init` 在数组分支里把它插在该元素之前；区间里**每个**元素（包括第一个）都读那个临时量。于是：

- `[1] = 0` 只能覆盖“读临时量”这件事，写临时量的副作用照旧发生；
- 评估位置不变，所以 `{[0 ... 1] = ++c, [2 ... 3] = ++c}` 的侧效果交错顺序与 gcc 一致（不是“全部先算完再赋值”）；
- 静态初始化器走原路（`static_init_ctx`，在 `gvar_initializer` ——文件作用域、块作用域 static、复合字面量三条静态路径的共同入口）：它没有临时量可写，也不需要——它的表达式必须是常量，每个元素拿到的都是同一个常量。

**（3）连带修掉的一个 IR 缺陷**

新写的赋值直接拿 `first->expr` 去存，而 cxx 的初始化器把 **lvalue 留在类型转换下面**、由 `create_lvar_init` 负责在赋值处插 load，
于是 `{ [6 ... 10] = elt }` 把 `elt` 的**地址**存进了临时量（IR 里是 `trunc ptr %tmp24 to i8`，clang 直接报 “invalid cast opcode”）。
那段惯用法提成 `init_rvalue()`，`create_lvar_init` 与新代码共用。另外，区间元素读临时量时也要先 `lvalue_convert`——
逗号表达式不是 lvalue，`create_lvar_init` 的标量分支就不会再帮忙插 load。

**（4）同一轮的第二件：声明符中间的属性**

`82_attribs_position` 报 “expected ‘)’ before ‘*’”：它要求 `int(ATTR *)(void)`（`ATTR` 在 `(` 与 `*` 之间）能被读成“指向函数的指针”。
GNU 允许属性出现在声明符的任何位置，`abstract_declarator()` 开头少了一步 `skip_leading_attrs()`（R26 给 `is_typename`/`typespec`
加过同样的一步）。加上后 `82_attribs_position` 全文匹配（tests2 103 → 104），并顺手删了一条重复的 `designation` 原型。

| | |
|---|---|
| **验收** | **tests2 102 → 104 通过 / 2 败**（`90_struct-init` 与 `82_attribs_position` 均全文匹配）；`test/conformance.sh` **208 → 210 passed / 0 gap**（两条新断言：区间设计符的断言同时钉住三件事——单次求值、从变量取值（不是地址）、后置设计符覆盖后副作用仍在；以及声明符中间的属性）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。本轮两处改动各自在探针里都没有对应形状：区间设计符出现在初始化器里（七棵树里没有用 GNU 区间设计符的对象），声明符中间的属性也没有；变化在 **tests2 102 → 104** 与 `test/conformance.sh` **208 → 210** |

#### 剩下的 tests2（2 个，差异已量）

- `95_bitfields`：三行差异全在**宽于 64 位的位域单元**上——`0x123456789abcdef0` 被写成 `0xc000000000000000`（高位丢失），
  最后两行的十六进制串少了 16 字节。扩展点在 `src/type.c` 的 `wide_unit`/`width_mask`（R21 只做到 64 位）与 `store_piece()`（R17）。
- `122_vla_reuse`：槽位机制，R33 已写明实现方案。

### R33 统一拆除：VLA 的存储在每一条出口边上释放 —— ✅ `79_vla_continue` 转绿（tests2 102）

R32 把问题和设计定下来后，这一轮把它做了。改动只有一处，因为探查发现 cxx 已经有一个“按作用域深度收拾”的入口：

```
cleanup_leaving(from, to, tok)      —— 所有 goto / break / continue / return 唯一的拆链入口
```

它一直只收拾 cleanup 处理函数，而 VLA 的 SP 回收是 `leave_scope()` 返回的一个节点、由调用方追加在**块末**——所以任何从旁边跳过去的路径都漏掉它。

| | |
|---|---|
| **修法** | 抽出 `scope_sp_release(sc, tok)`（`leave_scope` 也改用它），并在 `cleanup_leaving` 里——**同一个作用域的 cleanup 之后**——追加该作用域的释放。于是同一套拆除同时负责：处理函数按声明逆序跑，然后把栈指针放回去；跨作用域时内层先、外层后。顺序与块末那条路径（先 cleanup 后 `leave_scope`）一致 |
| **实测（修前 → 修后）** | `goto` 跳出 VLA 作用域后再声明一个：**drift(-16) → same**（两家都是 same）；cleanup 的四个边界用例逐行不变；**`79_vla_continue` 转绿**（它的 `continue` 正好跳过了块末的那条释放） |
| **顺序的依据** | 块末那条路径的注释本来就写着“leaving the block destroys its objects, most recently declared first, **and before the stack pointer of a variable length one is put back**”；这一轮只是把同一句话搬到了另外四条路径上 |
| **验收** | **tests2 101 → 102 通过 / 4 败**；`test/conformance.sh` **206 → 208 passed / 0 gap**（两条新断言：goto/break/continue 三条路径上的释放；处理函数能读到同块的数组）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净 |

#### 为什么 `122_vla_reuse` 还是红的（已把机制量出来）

它考的不是“出口边释放”，而是**重新到达同一个声明时同址**——而且那个循环**从不离开函数作用域**，所以出口边的释放对它无效。量出来的事实：

| 形状 | gcc | clang | cxx |
|---|---|---|---|
| `goto` 回到**两个声明之间**（a 仍在生命期内） | a 完好，b 回同址 | 同 | **同**（已一致） |
| 重新到达声明（嵌套块标签 + 流出，`122` 的形状） | **同址** | **同址** | 漂移 ❌ |
| 每轮打印 address（size 逐轮变化 1..4） | **地址与 size 无关**（固定槽） | 同 | 每轮 -16 |
| size 不可推断上界的循环 | **也会漂移** | **也会漂移** | 漂移 |

结论：两家在这里靠的是**优化器把 VLA 的空间提升成固定槽位**（所以地址与 size 无关），这不是语言保证；size 不可推断时它们也漂移。
所以 `122` 考的是优化产物。要跟它一致，可行且安全的做法是：

- 每个 VLA 声明一个“槽位 + 槽位大小”，在**函数序言里**初始化（这正是 R31 那个“标志没处放”的真正解法：序言是循环外的唯一位置）；
- 声明处：`size <= 槽位大小` 就复用槽位，否则分配并记下新槽位（**只长不缩**）。`122` 的 size 前 100 次单调增长、之后循环，而它从第 100 次才开始比较，所以能对上；
- 这与两家仍有一个可观测差异（首轮 size 从小到大时，它们地址恒定、cxx 会随之移动）——属于优化差异，写在这里，不冒充语言合规性。

#### 剩下的 tests2（4 个）

`122_vla_reuse`（上面那个槽位机制）、`90_struct-init`（区间设计符只求值一次，见 R31）、`95_bitfields`（跨 64 位位域 + 位域上的 `aligned(16)`）、`82_attribs_position`（`int (ATTR *)(void)`）。

| | |
|---|---|
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。本轮把 VLA 的存储释放并入了每一条出口边的统一拆除（探针里七棵树都是顺流路径，没有 `goto`/`continue` 跳出 VLA 作用域的形状），变化在 **tests2 101 → 102** 与 `test/conformance.sh` 206 → 208 |

### R32 局部变量生命周期：一次测量、一个缺口、一套统一机制的设计（待实现）

问题：“goto 不能跳进 VLA 所在作用域”这条规则，对 `cleanup` 变量是否也成立？两套生命周期管理是否应该合一？

#### 测量（gcc / clang / cxx 同一段源码）

| 用例 | gcc | clang | cxx |
|---|---|---|---|
| goto 跳入 **VLA** 作用域 | 拒绝 | 拒绝 | **拒绝**（报文与 clang 逐字相同） |
| goto 跳入 **cleanup** 作用域 | **接受**（处理函数运行，看到不定值） | 拒绝 | **接受**（同 gcc） |
| switch case 跳入 **VLA** 作用域 | 拒绝 | 拒绝 | **拒绝** |
| goto 跳过 cleanup 声明（向前跳） | **接受**（处理函数运行，不定值） | 拒绝 | **接受** |
| cleanup 在 break / continue / goto / return 上 | 运行 | 运行 | **运行**（三家逐行一致） |
| 同一作用域两个 cleanup 的顺序 | 逆序 | 逆序 | **逆序** |
| 嵌套作用域：内层先/外层后 | 一致 | 一致 | **一致** |
| **goto 跳出 VLA 作用域**（随后又声明一个 VLA） | 地址**相同** | 地址**相同** | **漂移（-16）** ❌ |
| cleanup 计算机制：使用同作用域早先声明的 VLA | 一致 | 一致 | **一致** |

结论一：**该规则不适用于 cleanup**。C11 6.8.6.1p1 只约束**变长修饰类型**；gcc 对 cleanup 两种跳法都接受（处理函数照跑，
看到的是不确定值），clang 用与 VLA 相同的话术拒绝。cxx 现在是“VM 类型学 clang、cleanup 学 gcc”——这是个**可守但必须写明的选择**，
它不是从标准推出来的。cxx 实现上也确实只有 VLA 这一边：`check_vm_jump()` 遍历的是 `vm_decls`，里面只有变长声明。

结论二：**应该合一，而且缺口不在约束检查、在退出模型**。上表最后两行是决定性的：

- cxx 的 cleanup 是**边上动作**（`cleanup_scope_chain()` 在每条跳转边上拼链）——break/continue/goto/return/嵌套顺序全对；
- cxx 的 VLA 是**尾随语句**（`leave_scope()` 返回一个 restore 节点，由调用方追加在块末）——于是**任何非顺流而出的边都跳过了它**，
  栈指针不回收，下一个 VLA 往上漂（测出 -16）。**这也正是 `122_vla_reuse` 的真正根因**——R29 我攻的是“重新进入声明”，而它缺的是“退出边上的回收”；
  R31 那个“静态/局部标志”之所以难以安放，也是因为它的**重置点本就在退出边上**，而那条边在当前模型里根本不存在。

#### 设计：一个作用域一张“拆除表”，所有出口边统一回收

1. **每个作用域一张有序表**（按声明顺序），项目有两种：`回收 VLA 栈到保存的 SP`、`调用 cleanup 处理函数(&var)`。
   现在这两件事分属 `scope->cleanups` 和 `scope->sp_saved/stack_top`，互不相知；合一后顺序就是声明顺序的逆序，与两家一致（表中 E/G/H/L）。
2. **所有出口边都走同一个回收器**：自然流出块尾、`break`、`continue`、`goto`（部分回收到目标作用域的深度）、`return`、跳出 `switch` 体。
   cleanup 已经是这个模型（所以它全对）；VLA 只要把尾随的那一条换成“块尾这条边”，就能修好上表里唯一的❌。
3. **重新进入同一作用域的声明**（`goto` 向后跳回，122_vla_reuse）：出口边上的回收把 SP 放回作用域保存值，
   再进入声明时分配就落在同一位置；而“跳回本作用域内部、未经过出口边”的情形，声明自己要先 restore 再 alloca——
   它需要的“是否已保存”标志，**重置点就是作用域的出口边**（第 2 步刚好给出了唯一的位置）。
4. **约束检查从同一份数据里读**：`check_vm_jump()` 的“作用域 + 序号”就是回收器算深度用的同一份数据；
   VM 类型必须报错（标准），而 cleanup 要不要报错是一个**写在这里的选择**（目前：学 gcc，不报）。

#### 还没量的一个角落（留给下一步）

`goto` 向后跳回到某个 VLA 声明**之前**，而该作用域里后面还有另一个 VLA（它的声明不会被重新执行）：
重执行前一个声明时把 SP 放回作用域保存值，会把后一个的存储也“释放”。两家分别怎么做、是否可观测，
还没测；实现前先把它量出来，再决定回收器在这种边上停在哪一层。

#### 收益

一次修好：上表那个 ❌（goto 跳出 VLA 作用域后栈指针不回收，真实误编译）、`122_vla_reuse`、`79_vla_continue` 剩下的差异，
并且 R31 那个“标志没处放”的尴尬会自然消失。

### R31 柔性成员对象的 `sizeof`（附一次精确的回退）—— ✅ 与两家一致，并改正了 cxx 自己套件里的两条断言

**（1）落地：柔性成员对象的 `sizeof`**

实测（同一段源码）：

```
struct A { int a; char b[]; };   struct A ga = {1, "abc"};
typedef struct { char a, b[]; } B;   B gb = {'f','o','o',0};
gcc, clang: sizeof(ga) = 4, sizeof(gb) = 1, sizeof(struct A) = 4
cxx 修前:  sizeof(ga) = 8, sizeof(gb) = 4
```

C11 6.7.2.1p18：带柔性数组成员的记录，其大小“如同该成员被省略”。cxx 把成员补全到初始化器产生的记录上（对象存储因此装得下那些元素），
而这个“补全后的大小”就跑到了 `sizeof` 里。修法：`sizeof` 读的是补全后的大小减去那个成员（未补全时它是零长数组，减零）。
修后三家完全一致（`4 1 4`），且小型复现中数据依然在（`strcmp(g65.b, "oo")` 为 0）。

**（2）改正了 cxx 自己套件里的两条断言（请审阅）**

`test/initializer.c` 里有四处 `sizeof(g65)/sizeof(g66)`（两个位置各两条），写的是 **4 和 7**，
也就是 cxx 修前那个与两家不同的值。实测 `T65`（`struct { char a, b[]; }`）在 gcc/clang 下 `sizeof(g65) = 1`、`sizeof(g66) = 1`
（两个对象都是 `T65`），所以改成 **1/1**，并加了一段注释说明依据。数据本身没变（`strcmp` 那几条仍然通过）。
这是本轮唯一一处**改测试断言**，理由是它针的是与两家都不符的行为。

**（3）回退掉的：区间设计符只求值一次**

`int dd[] = {[0 ... 1] = ++c, [2 ... 3] = ++c};` 两家给 `1 1 2 2`，而 cxx 是 `1 2 3 4`。我写了一版（第一个元素存入临时变量、
其余读它），**单独验证通过**：最小复现与 gcc 一致（`1 1 2 2 | c=2`）。但 `90_struct-init` 随即**调到垃圾指针崩溃**：
那个用例里的表是**块作用域 static**，而“这是静态初始化器吗”的标志对它没生效，于是区间走了临时变量路径、静态数据里存进了一个局部地址。
**二分确认**：把临时分支关掉，用例立即恢复（只剩区间那一行差异），于是**当场回退**。
结论：错的不是“只求值一次”，而是**静态/局部的判断**——下一步要在解析块作用域 static 时把标志真正置上。

| | |
|---|---|
| **验收** | `test/conformance.sh` **206 passed / 0 gap**；`make test` exit 0（含改正后的两条断言）；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净；tests2 **101 通过 / 5 败**（`90_struct-init` 剩一行） |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21；`doc/probes.sh` 全部基线，**cxx2 = cxx3 = cxx4 逐字节相同**。本轮修的是柔性成员对象的 `sizeof`（探针里七棵树没有这种形状），变化在 `test/conformance.sh` 206/0 与改正后的 cxx 自己套件；tests2 仍 101/5 |

### R30 静态初始化器里的复合字面量 —— ✅ `90_struct-init` 从“跑崩”到“只差两行”

**（1）落地的修复：静态初始化器里的复合字面量**

上一轮让这个用例编得过了，但它在 `test_compound_with_relocs` **调了一个空函数指针**（gdb：`rip = 0`）。
最小复现：

```c
static struct Wrap global_wrap[] = { ((struct Wrap) {one}), two };
```

cxx 的 `global_wrap[0].func` 是 **NULL**，两家是函数地址。原因：文件作用域的复合字面量被做成一个匿名全局对象，而元素是它的**值拷贝**；
拷贝只在源是 `constexpr` 时才会“挂接源的初始化器树”，于是元素保留了自己的表达式、折出 0。修法：给复合字面量的对象打上标记，它也算合法的源。
修后与两家逐字节一致，用例从“跑崩”变成“跑完、只差两行”。

**（2）试过又收回的：柔性数组对象的 `sizeof`**

第一行差异是 `gw` 的 `sizeof`（cxx 30字节、两家 22）：初始化器把柔性成员补全到记录副本上、同时把它的大小加进了 `ty->size`。
我加了一个“`sizeof` 时把末成员的大小减回去”的助手，但 cxx 自己的套件立刻报 **`sizeof(g65) => 4 expected but got 1`**：
“成员补全”与“大小补全”是两个独立步骤，不总是同时发生，减法假设了它们一起。**当场回退**，
正确做法（留给后续）：在补全记录的地方同时记下**声明时的大小**，`sizeof` 读它。

**（3）`90_struct-init` 剩下的两行（都已定位）**

- `gw` 的 `sizeof`（上面那条）；
- `int dd[] = {[0 ... 1] = ++c, [2 ... 3] = ++c};`：两家输出 `1 1 2 2`（**区间设计符的表达式只求值一次**），cxx 是 `1 2 3 4`（每个元素求一次）。修法：区间设计符先求值到一个临时变量，再分配给各元素。

| | |
|---|---|
| **验收** | `test/conformance.sh` **205 → 206 passed / 0 gap**（新增：静态初始化器里的复合字面量，含数组元素与单独对象两种）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净；tests2 仍 **101 通过 / 5 败** |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21。本轮修的是**静态初始化器里的复合字面量**（它的对象在“值拷贝”时被折成 0，全局表里的函数指针是 NULL），探针里七棵树没有这种形状（真有的话早就崩了），所以记分不动；变化在 `test/conformance.sh` 205 → 206，tests2 仍 101/5（`90_struct-init` 从“跑崩”到“只差两行”）。**中途 `sizeof` 那一版把 cxx 自己的 `sizeof(g65)` 打坏**（4 期望、1 实际），当场回退并重跑全套确认恢复 |

### R29 柔性数组成员的类型身份（附一次收回的 VLA 尝试）—— ✅ `90_struct-init` 从“编不过”到“跑得起来”

**（1）先说收回的那个：VLA 存储复用**

`122_vla_reuse`（`goto` 回到同一块，十万次）与 `79_vla_continue`（循环体内 VLA）都要求地址稳定。
看 IR 很清楚：cxx 把 `stacksave` 和 `alloca` 放在循环头，**每圈都重新保存并往上分配**，地址于是一路漂；
clang 的形状是“一次保存 + 回边上一次 restore”。

我试了一版：重新进入声明时先把栈指针 restore 到**作用域**保存的值。编译通过、两个用例却没变（因为编码只发一次保存），
而且它在语义上是**错的**：同一作用域里先前的 VLA 会被一并释放。**当场回退**（回退时还误删了一个括号，
把 `alloca` 变成了“只有作用域第一个 VLA 才分配”，当场修回）。
**正确做法（留给后续）**：每个 VLA 声明自己一个保存位 + 一个“是否已保存”的标志，
`flag ? (stackrestore(sp), 0) : 0; sp = stacksave(); flag = 1;`；标志必须在**循环外**初始化，而解析器在声明处发不出这样的位置——
这就是下一步的入口。

**（2）本轮落地的：柔性数组成员的类型身份**

`90_struct-init` 第 415 行 `foo(&gw, &phdr)` 报“incompatible types when passing argument”。
原因：`struct W` 末尾是 `struct S s[]`，cxx 把**不完整的柔性成员写成零长数组**，
而带初始化器的对象会得到一份“把成员补全”的**记录副本**（`int[0]` 对 `int[N]`），
于是同一个 `struct W *` 不接受同一个 `struct W` 对象的地址。
修法：柔性记录的**末成员**按元素类型比，不比长度。**实测**：最小复现与两家一致（`sum=33 s1=20` / `empty.n=3`）。
`90_struct-init` 因此**从“第 415 行编不过”变成“编过、跑得起来”**（然后在运行时段错误，那是该用例的下一个问题）。

| | |
|---|---|
| **验收** | tests2 仍 **101 通过 / 5 败**（本轮是把一个用例从编译失败推到运行时）；`test/conformance.sh` **204 → 205 passed / 0 gap**（新增：指向柔性记录的指针，含“初始化过”与“没初始化”两种对象）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21。本轮修的是**柔性数组成员的类型身份**（同一个 `struct W *` 此前不接受同一个 `struct W` 对象的地址），探针里七棵树没有这种形状（真有的话早就编不过了），所以记分不动；变化在 `test/conformance.sh` 204 → 205，而 tests2 仍是 101/5（`90_struct-init` 从“第 415 行编不过”推到“编过、跑得起来”） |

### R28 `case lo ... hi`：一个标签，不是一堆值 —— ✅ 修掉编译器挂死，`118_switch` 转绿

上一轮看剩下失败时发现 `118_switch` **编译不完**（`timeout` 到点都没出来）。原因很直接：
解析器把 `case lo ... hi:` **展开成每个值一个 case 节点**，而那个测试里有 `case -9223372036854775807LL-1LL ... -1LL:`——
9×10^18 次循环，顺带每次还要做一次重复检查。

| | |
|---|---|
| **修法** | `Node` 上加 `ival_end` / `is_range`：一个标签存**两端**。重复检查改成按**区间**比（相交就报，报“区间重叠”）；后端在 switch 前面发一对比较：`lo <= x && x <= hi`——宽度如同类型本身的区间也只花两条比较，而不是一次遍历 |
| **连带修掉的两处** | （a）`new_blk()` 报 `blk_used < curf->num_blk`：块预算是解析期定的，每个区间标签多需**一个**块（比较落空的那个），在解析时一并计上；（b）`narg` 写在了**区间比较之前**的块上，而 switch 落在最后一个新块里，于是打印出 `switch ... [ ]`——**空的**，`case 0` 全部落到 default |
| **为什么（b）能被发现** | 因为这一轮新写的断言里**同时有单值 case 与区间 case**：tinycc 那个用例只有区间，拿它当验证是不够的 |
| **验收** | **tests2 100 → 101 通过 / 5 败**（`118_switch` 转绿）；`test/conformance.sh` **203 → 204 passed / 0 gap**（新增：跨整个 `long long` 的区间 + 单值 case 混用）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净；与 gcc/clang 输出逐字节一致 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21。本轮修的是**区间 case 标签**（此前跨整个 `long long` 的区间会让编译器挂死），探针里七棵树没有这种形状（真有的话早就编不完了），所以记分不动；变化在 `doc/tcctests.sh` **100 → 101 通过 / 5 败**与 `test/conformance.sh` 203 → 204 |

### R27 条件运算符的类型与字符串初始化 —— ✅ `94_generic` 转绿（tests2 100）

上一轮把三个用例推到了新入口，这一轮从入口往下做。主体是**条件运算符的类型**（C11 6.5.15p6），顺序很重要：

| 顺序 | 规则 | 实测依据 |
|---|---|---|
| 1 | **空指针常量先**：一边是空指针常量时，结果取**另一边**的类型 | `0 ? (long *)0 : (void *)0` 是 `long *`——gcc 对 `void *:` 分支直接报“不匹配任何关联”，反证了结果类型 |
| 2 | 空指针常量 = 整数常量 0，或**它转到无限定 `void *`**；`(void const *)0` 不是 | 用例里的注释写的就是“like gcc but not clang, don't treat (void* const as the null-ptr constant”，而 gcc/clang 实测两家都不当它是 |
| 3 | 否则两边都是指针时，结果是**指向复合类型的指针**，且该类型带两边**限定符的并集**；一边是 `void *` 时结果就是限定的 `void *` | `0?(long volatile*)0:(long const*)0` 是 `long const volatile *`；`0?(int volatile*)0:(void const*)1` 是 `void const volatile *` |
| 4 | 不完整数组类型被另一边**补全**（6.2.7p3） | `0?(int (*)[])0:(int (*)[4])0` 是 `int (*)[4]`——不补全就会**同时匹配** `[4]` 与 `[5]` 两个关联 |

另一半是**字符串初始化嵌套字符数组**（C11 6.7.9p14）：cxx 只看最外层数组的元素类型，看到 `char[3]` 而不是 `char` 就报错。
现在它把字符填到**最内层的一个数组**里。

**两处刻意的差异（都是实测后留下的，不是遗漏）**：

- `char x[2][3] = "abc"`（字符串直接作为整个对象的初始化式）：两家**拒绝**，cxx **接受**。
  我试过加上“必须在花括号里”的限制，但 `90_struct-init` 第 273 行那个形状**单独拿出来 gcc 也拒绝**，说明它依赖的不是这条界线，于是收回了限制。多接受一种写法是有意为之。
- `char c[2][2][2] = {"ab","cd"}`：两家会**跨元素延续**（`a b c d 0 0 0 0`，即 "ab" 进 `c[0][0]`、"cd" 进 `c[0][1]`），cxx 是 `a b 0 0 c d 0 0`（"cd" 进了 `c[1][0]`）。
  这是花括号省略（brace elision）在多维字符数组上的一个角落，已记录待后续。

| | |
|---|---|
| **验收** | **tests2 99 → 100 通过 / 6 败**（`94_generic` 转绿；`90_struct-init` 从 273 行推到 **415 行**）；`test/conformance.sh` **201 → 203 passed / 0 gap**（两条新断言：条件运算符的四条类型规则；字符串初始化任意维字符数组）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与基线相同**：git 567/567、cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）、lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21。**中途这一轮一度把 sqlite 打成 0/1、cpython 打成 379/385**：条件运算符的空指针分支只改了类型、没转换值，IR 里出现 `phi ptr [ %x, … ], [ 0, … ]`，clang 报“integer/byte constant must have integer/byte type”。是**全量探针抓到的**（单元测试全绿），修好后重跑回到基线。变化在 `doc/tcctests.sh` **99 → 100 通过 / 6 败**与 `test/conformance.sh` 201 → 203 |

### R26 四个语法缺口（放宽而不放松）—— ✅ 三个用例各自推进一大步，未转绿

这一轮专门挑“报错很早、但实际上合法”的几处补上，四个改动各自很小，但合起来让三个用例从“第一行就报错”变成“跑到很后面”。

| # | 缺口 | 修法与依据 |
|---|---|---|
| 1 | **转换不了任何东西的强制转换被拒** | `(struct S)s`（`s` 已经是 `struct S`）合法——C 要求强转目标是标量型，但“转成自己”不是转换。修法：目标与操作数类型兼容时直接返回操作数（后端无事可做） |
| 2 | **类型名前面的属性看不到** | `((ATTR int (*)(void))p)()`（`ATTR` = `__attribute__((noinline))`）：判断 `(` 是不是强转的探查不认得属性。修法：`is_typename()` 与 `typename()` 都先 `skip_leading_attrs()` |
| 3 | **过时的 GNU 字段设计符 `{a: 1}`** | gcc/clang 仍接受（各自警告），而 cxx 只认 `.a = 1`。修法：`struct_designator()` 同时接受两种写法（冒号留给 `designation()` 判断），四处派发点（两个初始化循环 + 两个结构/联合循环）都认得它 |
| 4 | **强转丢顶层限定符** | C11 6.5.4p5 + 6.3.2.1p2：强转的结果是**非左值**，类型是名字所指类型的**无限定版本**。所以 `(float const)x` 的类型是 `float`，`_Generic` 选择表达式看到的就是它（用例里的注释写的就是这句）。修法：`new_excast()` 的结果类型过 `type_unqual()` |
| **验收** | `test/conformance.sh` **198 → 201 passed / 0 gap**（三条新断言：空转换 + 强转丢限定符；过时设计符（含嵌套）；类型名前的属性）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净；tests2 仍 **99 通过 / 7 败**（三个用例各自推进：`90_struct-init` 从第 168 行到 273 行，`94_generic` 从 88 行到 95 行，`82_attribs_position` 从 46 行到 51 行） |
| **下一步的入口（都已定位）** | `90_struct-init`：`char m1[][2][3] = {..., "abc"}`—字符串初始化嵌套字符数组；`94_generic`：`0?(long volatile*)0:(long const*)0` 的结果类型应是 `long const volatile *`（条件运算符合并两支的限定符，C11 6.5.15p6）；`82_attribs_position`：`int (ATTR *)(void)`—属性在 `*` 前面 |
| **记分（`doc/realworld.sh` 全量探针）** | **与上一轮相同**：git 567/567；cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）；lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21。本轮补的是**四处语法放宽**（空转换、类型名前的属性、过时设计符、强转丢限定符）：七棵树里没有这几种形状（真有的话早就编译失败了），所以记分不动；变化在 `test/conformance.sh` 198 → 201，而 tests2 仍是 99/7（三个用例各推进一大步，见上） |

### R25 两件事：文件作用域 asm 的 `$` 与初始化函数 —— ✅ tests2 97 → 99

**（1）`98_al_ax_extend`：文件作用域 asm 被当成了内联 asm**

`asm("...movl $0x1234ABCD, %eax;...");` 在文件作用域，变成 LLVM 的 `module asm`——那是**直接交给汇编器的文本**。
cxx 用的是内联 asm 的转义（LLVM 在内联模板里用 `$` 做操作数标记，所以一个字面 `$` 写作 `$$`），于是汇编器收到的是
`$$0x1234ABCD`，它把那当成**符号名**，链接报 `R_X86_64_32 ... $0x1234ABCD`。修法：`asm_tmpl_conv()` 加一个“这是 module asm”的参数，
文件作用域不再加倍（内联 asm 照旧，已验证 `__asm__ volatile("movl $42, %0")` 仍然正常）。实测：与 gcc 逐字节一致（`0000ABCE`）

**（2）`108_constructor`：`__attribute__((constructor))` / `((destructor))` 根本没实现**

| | |
|---|---|
| **实现** | 属性表里加两条（GNU 命名空间）；`Sym` 上加 `ctor_prio`/`dtor_prio`（带参数就是优先级，没有就是 65535，两家都是这个默认值）；导出时用 LLVM 的初始化数组——`@llvm.global_ctors` / `@llvm.global_dtors`，`appending global [N x { i32, ptr, ptr }]`，按优先级排序（插入排序，相同优先级保持声明顺序） |
| **一个坑** | 第一版下来 IR 报 `use of undefined value '@testc'`：**静态的构造函数被当成“无人引用的定义”丢掉了**。它确实没人引用——是平台调的。修法：可达性分析把带这两个属性的函数当根 |
| **实测** | `108_constructor` 逐行对上；优先级版本（`constructor(101)` + 默认 + `destructor`）与 gcc/clang 完全一致：`a b main c` |
| **验收** | **tests2 97 → 99 通过 / 7 败**；`test/conformance.sh` **196 → 198 passed / 0 gap**（两条新断言：带 `$` 的文件作用域 asm；构造/析构函数与优先级）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与上一轮相同**：git 567/567；cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）；lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21。本轮是**两处能力补齐**（文件作用域 asm 的 `$`、构造/析构函数）：七棵树里没有用带 `$` 立即数的文件作用域 asm（用了的话链接就失败了，早就会发现），也没有用构造/析构函数（用了的话函数根本不会跑），所以记分不动；变化在 `doc/tcctests.sh` **97 → 99 通过 / 7 败**（`98_al_ax_extend`、`108_constructor` 转绿）与 `test/conformance.sh` 196 → 198 |

### R24 数组类型上的限定符归于元素 —— ✅ 两个用例转绿（C11 6.7.3p9）

`39_typedef` 报“`ca` 重新声明为冲突类型”，`100_c99array-decls` 报“restrict requires a pointer”——两者共用一条规则：
**“如果数组类型的说明包含限定符，那么被限定的是元素类型，而不是数组类型”**。

| | |
|---|---|
| **缺陷（1）限定符落在数组类型上** | `typedef int A[3]; extern A const ca;` 与 `extern const int ca[3];` 本是同一个声明，cxx 把 `const` 记在了数组类型上，两边看成两种类型。修法：声明符结束时，若类型是数组（含 VLA）就用已有的 `array_elem_qual()` 把限定符推到最内层元素 |
| **缺陷（2）`restrict` 直接被拒** | `typedef restrict pointer_array x;`（`pointer_array` = `int *[2]`）合法：被限定的是那两个指针。cxx 在 `declspec` 里遇到 `restrict` 就报错。修法：先收下限定符，在限定符遇到类型时判断——沿数组链找到最内层元素，必须是指针（否则报错，如同 gcc） |
| **缺陷（3）兼容性比较追了 typedef 的 `origin`** | `is_compatible()` 为了看穿 typedef 名会追 `origin`；数组追过去就丢了**元素的限定符**（`A const` 变回 `A`）。修法：数组不追 `origin`（它的身份就在元素里），其余判断照旧 |
| **一次回归（当场捕获）** | 第一版我写成“两边都是数组就直接比元素”，绕过了 switch 底部的**长度**规则，于是 c2y 套件里“VLA 匹配任意长度、定长数组仍然比较”那条失败（`int (*)[4]` 不再拒绝 `int a[3]`）。改成只跳过 origin 追踪、保留后面的长度比较后，c2y 101/0 恢复 |
| **验收** | **tests2 95 → 97 通过 / 9 败**（`39_typedef`、`100_c99array-decls` 转绿）；`test/conformance.sh` **195 → 196 passed / 0 gap**（新增：三种写法同一个对象、`restrict` 到元素、嵌套 restrict 保留）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与上一轮相同**：git 567/567；cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）；lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21。本轮修的是**数组类型上限定符的归属**（C11 6.7.3p9）：探针里七棵树没有“同一对象用 typedef 数组的限定写法声明两次”或“对指针数组用 restrict”的形状，所以记分不动；变化在 `doc/tcctests.sh` **95 → 97 通过 / 9 败**（`39_typedef`、`100_c99array-decls` 转绿）与 `test/conformance.sh` 195 → 196 |

### R23 `#pragma push_macro` / `pop_macro` —— ✅ C23 标准功能，`77_push_pop_macro` 转绿

上一轮看剩下的编译失败时发现，`77_push_pop_macro` 并不报错，只是**输出不对**：它用的
`#pragma push_macro` / `pop_macro` 是 **C23 6.10.11**（N2686）的标准功能，而 cxx 把它当普通 pragma 略过了（三个 `abort` 全是 `333`）。
正好 R20 已经搭好了“预处理器认得 `#pragma` 参数”的路子，这一轮把它实现了。

| | |
|---|---|
| **实现** | `push` 记下名字**当前解析到的定义**（包括“未定义”），`pop` 把它放回去；名字取自引号字符串（按 `len` 读，`tok_text()` 不加 NUL）。栈是一个数组，同名可多次压入，`pop` 弹最近一次 |
| **关键细节（第一版就踩了）** | cxx 的查找 `find_macro` 是“**最新的那条记录定输赢**，它标了 `deleted` 就算未定义”，而 **不会继续往后找**——所以“把旧记录的 `deleted` 清掉”根本恢复不了（它被后来的记录遮住了）。`#undef` 本身就是“压一条删除标记”，所以 `pop` 也得**加一条新记录**：有保存就把它的 `is_objlike`/`body`/`params`/`is_variadic` 拷贝过去，没有就压一条 `deleted` |
| **实测** | `77_push_pop_macro` **逐行对上**（`111 / 222 / 333 / 222 / 111`）；`doc/tcctests.sh` **94 → 95 通过 / 11 败** |
| **验收** | `test/conformance.sh` **194 → 195 passed / 0 gap**（新增：嵌套 push/pop、未定义名字压入再弹出回到未定义、函数式宏完整回来）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **与上一轮相同**：git 567/567；cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）；lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21。本轮是**加 C23 标准能力**（`#pragma push_macro`/`pop_macro`）：探针里七棵树没有用这对 pragma（真用了的话，宏在 pop 之后就是错的，早就编译或运行失败了），所以记分不动；变化在 `doc/tcctests.sh` **94 → 95 通过 / 11 败**（`77_push_pop_macro` 转绿）与 `test/conformance.sh` 194 → 195 |

### R22 位域存储把隔壁清成了零：移位 64 是未定义 —— ✅ `95_bitfields` 的主体修好

R21 修完宽位域掩码后，`95_bitfields` 只剩三处。用测试框架自己的语句序列做最小复现时发现一个**更严重的误编译**：

```c
s.z = 120;      /* unsigned long long z : 38; char a; 紧挨在后面 */
```

三家对比：gcc/clang 的 `a` 保持 `-1`，cxx 变成 **0**；带上 `s.a += 0x44, ++s.a` 后就是 `0x44` 对 `0x45`（恰好差一位，
这就是 R21 里那个“差一位”的真相）。

| | |
|---|---|
| **根因** | 清位掩码用 `(1ULL << total_bits) - 1` 裁到单元宽度。**八字节单元时 `total_bits == 64`**，而移位计数按 mod 64 取——表达式等于 **0**，清位掩码就是 0，于是“清掉字段的位”变成“清掉整个单元”。IIR 里看得很清楚：`and i64 %tmp19, 0` |
| **修法** | 单元宽度就是 64 时不再取位（取反本就是全一）；同时把“字段宽度掩码”也改成 `width >= 64 ? ~0ULL : (1ULL << width) - 1`，因为全宽位域（`unsigned long long x : 64`）会踩到同一个坑 |
| **实测** | 上面那两行现在与 gcc/clang 逐字节一致；`95_bitfields` 的 **TEST 5 与 TEST 5 PACKED 全对**，剩下两类共三处（见下） |
| **剩下的种类一：跨超 64 位的位域** | TEST 2 PACKED 的 `struct { int x:12; char y:6; long long z:63; ... }`：打包后 `z` 从第 18 位开始，**占到第 80 位**，不能装进任何单克隆单元；现在 `min_bytes_for_bits()` 最大只到 8 字节，于是读写都错（实测 `z` 读回 `c000000000000000`，两家是 `123456789abcdef0`）。下一步：让这种字段走**逐字节拼接**（或两次单元访问） |
| **剩下的种类二：位域上的 `aligned(16)`** | TEST 2/3 的 `A char a:4`（`A` = `__attribute__((aligned(16)))`）：两家把记录撑到 `align/size 16 32`，cxx 是 `8 24`——位域上的 aligned 属于 gcc 特例，留待后续 |
| **验收** | `test/conformance.sh` **193 → 194 passed / 0 gap**（新增：写 `z` 不能动 `a`/`b`，包括测试框架那个逐字段置全 1 再赋值的序列）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净；tests2 仍 **94 通过 / 12 败**（`95_bitfields` 从三处到三处，但 TEST 5 两轮全对） |
| **记分（`doc/realworld.sh` 全量探针）** | **与上一轮相同**：git 567/567；cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）；lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21。本轮修的是**八字节单元里位域的清位掩码恒为 0**（移位 64 是未定义）——探针里七棵树没有这种形状的写入序列（位域在八字节单元里、且紧邻着别的成员），所以记分不动；变化在 `test/conformance.sh` 193 → 194 与 `95_bitfields`（TEST 5 与 TEST 5 PACKED 全对，只剩跨 64 位的位域与位域上的 `aligned(16)` 两类） |

### R21 四字节以上的位域：掩码被截断 —— ✅ 一类误编译修正

R20 把 `95_bitfields` 的差异从十四处压到三处后，剩下的全是**宽位域**。最小复现：
`struct { long long x : 45; long long : 2; long long y : 30; unsigned long long z : 38; }`，逐字节对照三家。

| | |
|---|---|
| **缺陷** | 位域存储的两个掩码用 `INT()` 构造，而 `INT()` 是 **`int32_t`**：45 位字段的清位掩码 `~(((1<<45)-1))` 被截成它的低半——**恰好是 0**，于是“清掉周围位”变成了“清掉字段自己”；值掩码同理变成 -1。实测：`s.x = ~0` 在 45 位字段上写了**八个 FF**，两家写的是 `FF FF FF FF FF 1F` |
| **修法** | 按**存储单元自己的宽度**选常量构造器：单元 > 4 字节用 `LONG`，否则 `INT`（两个掩码都改）。**实测**：三个字段各自置全 1 后的整个结构字节与 gcc/clang **逐字节相同**（`FF FF FF FF FF 1F` / `FF FF FF 3F` / `FF FF FF FF 3F`），读回来的值也一致 |
| **验收** | `test/conformance.sh` **192 → 193 passed / 0 gap**（新增：45/30/38 位三个字段的逐字节布局与“写一个不动隔壁”）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净；tests2 仍 **94 通过 / 12 败**（`95_bitfields` 的差异再从三处到三处，但性质变了：布局行全对，剩下是两个值的位差与 TEST 2 PACKED） |
| **记分（`doc/realworld.sh` 全量探针）** | **与上一轮相同**：git 567/567；cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）；lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21。本轮修的是**四字节以上位域的掩码被截断**：探针里七棵树没有这种形状的位域（真有的话读到写到的值本来就是错的），所以记分不动；变化在 `test/conformance.sh` 192 → 193 与 `95_bitfields`（布局行全对，只剩两个值的位差与 TEST 2 PACKED） |

### R20 `#pragma pack` —— ✅ 新能力（`95_bitfields` 的 packed 半边跑通了，差三处宽位域）

`95_bitfields` 失败的主因不是位域布局，而是**这个测试的整个 packed 半边都没生效**：它用 `#pragma pack(push,1)` 而不是
`__attribute__((packed))`，而 cxx 只实现了后者（实测：`struct { char c; int i; }` 在 pragma 下仍然是 8/4，两家是 5/1）。现在四种写法全部
与 gcc/clang 一致：`pack(push,1)` 5/1、`pack(pop)` 8/4、`pack(2)` 6/2、`pack()` 8/4。

| | |
|---|---|
| **实现** | 预处理器认得 `#pragma pack`（`push`/`pop`/`show`/`()`/带参数均可，MSVC 形式的标识符接受但忽略），自己维护 push/pop 栈；**因为预处理器跑完才轮到解析器**，
当前值随一个 `TK_PRAGMA` 标记令牌传给解析器，解析器在文件作用域与块作用域的循环里消费它。布局时：成员对齐被截到 `n`，
且 `n == 1` 时整个记录走**packed 布局**（位域之间也没有存储单元边界）——这正是 gcc 的行为：十二位接七位紧挨着，
`95_bitfields` 期望的就是三字节 |
| **调试中碰到的两个坑** | （a）指令里的数字是 **`TK_PPNUM`**（预处理数字）而不是 `TK_NUM`，而且它的 `ival` **没有被转换**（实测 `kind=155 ival=0`），
必须像 `#line` 那样按 `len` 逐位读字符；（b）`tok_text()` 返回的是**不加 NUL 的源码指针**，用 `%s` 打印会一直跑到文件尾——
我一开始就被这个当成“令牌文本坏了”，多绕了两轮 |
| **现状对比** | `95_bitfields` 的差异从**十四处缩到三处**，且剩下三处全是**宽位域**：`long long z : 63`（TEST 2）与 `long long z : 38`（TEST 5）的布局，
以及 TEST 2 PACKED 的值（`0e` 对 `1e`）——下一轮从这里接 |
| **验收** | `test/conformance.sh` **191 → 192 passed / 0 gap**（新增：四种 pack 写法与两个 packed 位域记录的 `sizeof`/`_Alignof`）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净；tests2 仍 **94 通过 / 12 败**（本轮是能力增加，没有用例因此整体转绿） |
| **记分（`doc/realworld.sh` 全量探针）** | **与上一轮相同**：git 567/567；cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）；lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21。本轮是**加能力**（`#pragma pack`）而不是改语义：七棵树里没有用 `#pragma pack` 的记录（真用了的话布局本来就会被 cxx 算错、早就编不过了），所以记分不动；变化在别处：`test/conformance.sh` 191 → 192、tests2 仍 94/12（`95_bitfields` 的差异从十四处缩到三处，全是宽位域） |

### R19 可变参数聚合参数（放方半边）—— ✅ `73_arm64` 完整转绿

R18 把守方修好后，这一轮把**放方**那半补上：可变参数调用里，聚合参数只有在**它的每一个八字节都装得下**时才走寄存器，
否则整个参数进溢出区（IR 里写作 `byval`）。参照物就是 clang 自己的 IR：
`六个九字节结构体` 在 clang 里是 `i64 %5, i8 %7, i64 %9, i8 %11`（前两个）+ **四个** `ptr byval(%struct.s9)`。

| | |
|---|---|
| **缺陷（1）参数寄存器预算根本没算** | 原来每个可变参数聚合都拆成散装（`agg_is_per_piece`），于是第三个结构体起，clang（后端）只能给每个标量各放一个栈槽，而 ABI 给聚合参数留的是十六字节。修法：目标新增 `vararg_gp_regs`/`vararg_sse_regs`（amd64 = 6/8，其他目标 0 = 无此规则），调用降级时从第一个参数起**记账**（命名参数占掉的也算），只有可变参数尾部才受预算约束；装不下时改发 `byval` 指针（并不再占寄存器） |
| **缺陷（2）一个调用只能标一个 `byval`** | `Ir.byval_at` 是一个 `uint8_t` 下标，而 clang 的参照 IR 里有**四个** `byval` 参数——于是只有最后一个被正确标记，前三个当成普通指针传了过去，被调方 `va_arg` 读到的是指针本身。修法：改成 `uint32_t *byval_at; int nbyval;`（附加时收集下标，打印时查表）。**实测**：修好后 cxx 的调用变成 `i64, i8, i64, i8, ptr byval(...) ×4`，与 clang 逐个对应 |
| **验收** | **`73_arm64` 完整转绿**（两行 stdarg 与全部 HFA 行都对上），**tests2 93 → 94 通过 / 12 败**；`test/conformance.sh` **190 → 191 passed / 0 gap**（新增：五个九字节结构体走可变参数，前两个走寄存器、后三个走溢出区）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净；最小复现（六个九字节结构体）与 gcc/clang **逐字节相同** |
| **记分（`doc/realworld.sh` 全量探针）** | **与上一轮相同**：git 567/567；cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）；lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21。这两轮改的是**变参数聚合参数的 ABI 布局**：探针里七棵树的变参数调用里没有“装不下的聚合参数”，所以记分不动；变化在行为侧：**tests2 93 → 94**（`73_arm64` 完整过）、`test/conformance.sh` 190 → 191、`doc/probes.sh` 仍然全部基线（含 cxx2 = cxx3 = cxx4 逐字节相同） |

### R18 `73_arm64` 的 stdarg 部分：可变参数聚合参数 —— ✅ 诊断到底，守方修好，放方还差一半

R17 修完参数槽越界后，`73_arm64` 只剩两处；这一轮把其中的 stdarg 部分（第 70-71 行）追到了根因：
`myprintf("%9s %9s %9s %9s %9s %9s", s9, s9, s9, s9, s9, s9)`，六个九字节结构体通过**可变参数**传递（十二个八字节）。
用一个直接打印 `va_list` 各字段与溢出区原始字节的探针（clang/gcc/cxx 各编一份）定位到**两个独立缺陷**，一个在守方、一个在放方。

| | |
|---|---|
| **缺陷（1）守方：`va_arg` 的“还有位置吗”只算一个槽** | `struct { char x[9]; }` 是两个八字节，而 ABI 规定：只要有任一个八字节没位置，**整个参数走溢出区**。cxx 的 `va_arg_gp16`（两个八字节的类型用的表，步长 16）却沿用了单槽的界 `offset_bound = 40`，于是偏移 32 的参数（即第 5、第 6 个寄存器）仍被当成在寄存器里读，而那个区域只到 48 字节——**越界读一个字节**。修法：该表的界改成 `48 - 16 = 32`。**证据**：clang 自己的 `va_arg` 就是这么做的——同一个探针里，clang 在 `gp_offset = 40` 时读完第三个结构体后把游标停在 40（即改走溢出区），而 cxx 修前继续从保存区读 |
| **缺陷（2）放方：可变参数里把聚合体拆成散装** | amd64 上 cxx 把聚合参数按“一块一个标量参数”发送（`agg_is_per_piece`），这对**有原型**的调用是自洽的（cxx 自己的被调方也按块接），但可变参数调用**没有原型**：clang（后端）只能把每个标量各放一个栈槽，而 ABI 给聚合参数留的是 `ceil(size/8)*8` 字节——九字节结构体是十六字节。**证据**：探针打印溢出区原始字节，clang 自己的放方是 `ABCDEFGHI.` 以十六字节为步长，cxx 发出的参数却是第一个槽里只有一个 `I`（第九个字节）、各块各占一槽。改法：可变参数调用里把整个聚合体当**一个参数**传（不拆片），让后端按 ABI 布局。已经试过一版（用 `IR_LORD` 取整个记录值后作为一个操作数），**测出来更差**：LLVM 把那个记录值按另一种方式降级，溢出区变成了垃圾（`F...@...G...@...`），所以已回退；下一步得从“传 **结构体本身**（而非 pieces 形状）并确认 LLVM 对可变参数聚合体的降级”入手 |
| **验收** | `make test` exit 0；`test/conformance.sh` **190 passed / 0 gap**（本轮没有新增断言：缺陷（1）是守方的一半，只有当放方也修好后才有一个能稳定通过的用例）；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净；tests2 仍 **93 通过 / 13 败**（`73_arm64` 的 stdarg 行从“第四个起变成 `I`”变成“第三个起”，依然不对，但对坏了的位置与 clang 守方一致） |
| **实测：双槽标量的可变参数（两个界都对）** | `show("five then i128", 1ull, 2ull, 3ull, 4ull, 5ull, (__int128)-1234567890123LL)`：五个八字节后游标到 40，`__int128` 必须走溢出区。三家都输出 `1 2 3 4 5 -> -1234567890123`（而 `four then i128` 四个后游标 32，仍在寄存器，也是三家一致）——这是缺陷（1）在实际代码里已经对上的形状 |
| **记分（`doc/realworld.sh` 全量探针）** | **与上一轮相同**：git 567/567；cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）；lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21。本轮修的是 `va_arg` 的**守方半边**（两槽记录在偏移 32 时必须走溢出区），它只在**混合语言**（clang 编译的放方 + cxx 编译的守方）或放方也按 ABI 布局时才显形；探针里这七棵树都是**整树用 cxx 编译**（放守双方都是 cxx 的拆片约定），所以记分不动；`test/conformance.sh` 也没有为此新增断言——能稳定通过的那个用例要等放方也修好。两槽**标量**（`__int128`）的可变参数形状三家一致，已记在上面的实测行里 |

### R17 参数槽越界（SIGILL）与 `dev`/`main` 分支插曲 —— ✅ 一类栈破坏修正

R16 从 tests2 列出的 13 个失败里，`73_arm64`（一个“在任何架构上都应该跑出同样结果”的 ABI 用例）
直接崩在第四个调用：`struct { char x[3]; }` 按值传递。这一轮把它修成了“能跑完”。

| | |
|---|---|
| **缺陷（1）寄存器块比对象宽时写到参数槽外面** | SysV 下 `struct { char x[3]; }` 走一个 `i32`，而被调方把**四个字节全写进三字节的槽**——那多出来的一字节是它在栈上的邻居。tinycc 的 `73_arm64.c` 因此 **SIGILL**：gdb 里返回地址是被截断的 `0x00000000555551c0`（`main` 实际在 `0x5555555551c0`），那一字节正是保存的返回地址。修法：`store_piece()` ——块先存到一个自己大小的临时槽，再只拷贝对象真正有的字节数；单块与多块两条路都走它 |
| **缺陷（2）同一类问题在“多块”路径上** | 修好三字节后，崩溃往后移到十一字节：`struct { char x[11]; }` 走 `i64`+`i32`（**十二字节装十一个**），`struct { char x[13]; }` 走两个 `i64`（十六装十三）。多块路径逐块存储，最后一块会越界。修法：同一个 `store_piece()`，按 `pt->size - m->offset` 与块大小取小者存储。**实测**：1/2/3/4/5/6/7/8/9/10/11/12/13/15/17 字节全部形状在同一个函数里依次调用，与 gcc/clang 行为一致（修前 SIGILL），15 个参数与实参都完好；`73_arm64` 从“崩”变成“跑完” |
| **验收** | `test/conformance.sh` **189 → 190 passed / 0 gap**（一条新断言：十五种寄存器形状的聚合参数）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`clang-format-21 --dry-run --Werror` 干净；tests2 仍是 **93 通过 / 13 败**（`73_arm64` 还差两处，见下） |
| **记分（`doc/realworld.sh` 全量探针）** | **与上一轮完全相同**：git 567/567；cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）；lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21。这一轮修的是**参数槽越界**：它只在栈布局恰好时才致命，探针里“能编”的单元本来就没被它绊倒，所以记分不动正是预期；变化在行为侧：`73_arm64` 从 SIGILL 变成跑完，`test/conformance.sh` 189 → 190 |
| **`73_arm64` 剩下的两处** | （a）`fa1(s8, s9, s10, s11, s12, s13)`：六个聚合参数 = 十二个块，超出六个寄存器的部分走栈，第 70 行有一个字节被改写（`ABCDEFGH\xa0 I I I`）——同一类问题在**发送方**的栈参区；（b）`fa3`/`fa4` 的 HFA 与 `long double` 用例（第 73-82 行）。下一轮从（a）开始 |
| **`dev` / `main` 分支插曲（记一笔）** | 这一轮开始时工作树在 **`main`**（`2a128b5 tmp`）上，而 R14–R16 的改动与本计划都在 **`dev`**（`fae873c Fix some bug`）上；表现出来就是“补丁打不上、源码比二进制还新”。已经确认并切回 `dev` 继续。另外当日 WSL 实例出现一次文件系统级故障（`df`/`free` 都 SIGSEGV、`/tmp` 只读），`wsl --shutdown` 重启后恢复，未丢数据 |

### R16 行为测试驱动：tinycc tests2 与它找出的六处 —— ✅ 89 → 93 通过（含一类指针误编译）

R15 加的 `doc/tcctests.sh` 把 tinycc 自带的 `tests/tests2` 当成**行为**探针（编译 + 运行 + 对比 `.expect`，
并用 gcc 过滤掉只适合 tcc 的用例），当时给出 17 个失败；本轮修掉其中六处，四个用例因此转绿。
这一类探针的价值在于它看得到**编译通过但跑错**的东西——`doc/realworld.sh` 只能看到“能不能编”。

| | |
|---|---|
| **缺陷（1）位域提升没按宽度算** | 6.3.1.1p2 要求位域“**按宽度限制后**”参与整型提升：gcc/clang 的读法是——取值范围能装进 `int` 的位域就是 `int`，不管它声明成什么（`unsigned u31 : 31` 是 `int`，所以 `u31 - 100` 是**有符号**比较）；装不进的才保留声明类型的等级（`unsigned u32 : 32` 是 `unsigned int`，`unsigned long long u33 : 33` 仍是 `unsigned long long`）。cxx 在 `integer_promotion()` 里直接用声明类型算公共类型，于是每个 unsigned 位域都是 unsigned。修法：新增 `bitfield_type()`（带 `ND_LVTOR` 剥壳——操作数到这里时已经过了左值转换），`integer_promotion()` 与 `usual_arith_conv()` 都用它算类型（后者是 `u31 - 100` 真正走的路）。**实测**：14 种形状（宽度 1/3/31/32/33/64 × unsigned/int/unsigned long long/long long/unsigned char/unsigned short）的类型与符号测试与两家**逐项相同**，`93_integer_promotion` 转绿 |
| **缺陷（2）位域存储写了整个存储单元** | `struct { unsigned x : 12; unsigned char y : 7; } s; s.x = ~0u;` 把 x 所在单元的 16 位全写了，属于 y 的那四位也被置一——**写一个位域会污染隔壁**。读取侧有遮罩，所以读回来的值是对的，但按字节看就错了（tinycc 的 `95_bitfields` 正是把“哪些位被用到”打成位图）。修法：`store()` 里先把新值按**字段宽度**掩码（`width < total_bits` 时）再左移合并；长度不足时只靠类型截断是不够的。**实测**：五个字段各自置全 1 后的整个结构字节，与 gcc/clang **逐字节相同**（修前 `FF FF`/`FF`/`FF` 对上两家的 `FF 0F`/`7F`/`1F`），且 `s.x = ~0u` 后 `y/z/a/b` 均为 0 |
| **缺陷（3）`#pragma once` 与包含守卫按“拼写”认文件** | 头文件的身份取自 `#include` 里写的那串字符，于是 `"x.h"`、`"./x.h"`、`"../dir/x.h"` 是三个不同的文件。tinycc 的 `18_include.c` 正好用这三种拼写包含同一个带 `#pragma once` 的头，期望输出一行，cxx 输出三行。同一缺陷也让**包含守卫捷径**在两种拼写下不生效。修法：新增 `file_identity()`，用 `realpath()` 的规范路径做这两张表的键（诊断仍然打印程序写的那个拼写）；三处都改：`include_file()` 的 `file_id`、`add_pragma()`、守卫登记。**实测**：`18_include` 转绿；另写一份三种拼写包含同一个带守卫的头，三家都编过且运行一致（守卫捷径仍然生效：`Python/ceval.c` 3.1s） |
| **缺陷（4）函数返回类型没去掉限定符** | `const int f(void)` 的返回类型是 `int`（只有外层限定符丢，`const int *f(void)` 保留指向的 const），gcc/clang 只在 `-Wextra` 下提一句（`-Wignored-qualifiers`），但类型确实是去掉的。cxx 保留了 `const`，`150_return_qualifiers` 的静态断言因此失败。修法：`func_type()` 里 `ty->ret = type_unqual(return_ty)`。**实测**：四条静态断言（含“指向的 const 保留”）三家都接受，`150_return_qualifiers` 转绿 |
| **缺陷（5）void 左值也发一条 load（非法 IR）** | `void *pv; i ? *pv : *pv;`（DR 106，tinycc 的 `119_random_stuff.c` 就有）：`*pv` 的类型是 void（6.5.3.2p1）且值被丢弃，cxx 仍然发了 load，后端报 `load void` / `void type only allowed for function results`。修法：`ND_LVTOR` 里对 void 直接返回地址（与记录类型同一条路） |
| **缺陷（6）整数转指针丢了符号（误编译）** | `(void *) -1` 在 cxx 里变成 `0x00000000FFFFFFFF`：发的是 `inttoptr i32 -1 to ptr`，而 LLVM 的 inttoptr 对窄操作数是**零扩展**。gcc/clang 给全 1，而 glibc 的 `MAP_FAILED` 就是 `((void *) -1)`——每个 `mmap` 调用者都要做这个比较，于是判断反了、程序往未映射地址写，直接崩（`119_random_stuff` 的 `tst_const_addr`）。修法：`cast()` 的整数→指针分支先按**源类型的符号性**扩宽到指针宽度（`SEXT`/`ZEXT`）再 inttoptr，所以 `(unsigned) 0xFFFFFFFF` 仍然是 32 位值。**实测**：五种写法（`(void*)-1`、`(void*)-1L`、`(void*)(int)-1`、`(void*)l`、`(void*)u`）与两家逐个相同，`mmap` 那段不再崩，`119_random_stuff` **完整转绿** |
| **验收** | `test/conformance.sh` **183 → 189 passed / 0 gap**（六条新断言：位域按宽度提升、位域存储只写自己的位、一个头三种拼写、返回类型无限定符、整数转指针、void 左值不 load）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)；`doc/probes.sh` 全部基线（含 **cxx2 = cxx3 = cxx4 逐字节相同**）；`clang-format-21 --dry-run --Werror` 干净；**tinycc tests2 89 → 93 通过 / 13 仍败** |
| **记分（`doc/realworld.sh` 全量探针）** | **与上一轮完全相同**：git 567/567；cpython 381/385（余下四个仍是 `Python/pystrhex.c` 的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`）；lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21。本轮六个修复都是语义修正（位域、指针转换、返回类型、文件身份、void 左值），**没有一个把探针里的单元从过变成不过**；而**行为**探针从 89 升到 93——这正是两种探针分工的意义：前者管“能不能编”，后者管“编出来跑得对不对” |
| **tests2 剩下的 13 个** | 7 个是“能编但跑错”（`73_arm64` 可变参数直接崩、`79_vla_continue`、`95_bitfields`（剩下的都是 **packed 位域**：期望 `align/size 1 7` 对 cxx 的 `4 12`）、`108_constructor`、`118_switch`、`122_vla_reuse` 等），"
        "6 个是编译失败：`39_typedef`（`extern const int ca[3];` 与定义冲突）、`82_attribs_position`（表达式位置的属性）、`90_struct-init`（带投影的结构初始化器）、`94_generic`（`_Generic` 的 const 关联）、`100_c99array-decls`（`typedef restrict`）、`98_al_ax_extend`（x86 asm 扩展，链接期报错）。下一轮从 packed 位域与可变参数崩溃开始 |

### R15 机器旗标与特性宏（`-m*`）：第十二轮 —— ✅ 驱动接收 `-m*`，宏从 clang 取；并按 chibicc 的做法用 cxx 自己 configure cpython

起点是一个问题：cpython 剩下的 SIMD 单元能不能靠“改 clang 编译选项”关掉？
答案是不能（clang 在 cxx 里只是后端，看不到 C 源码；选路径的是 cxx 自己的预处理器，依据是这棵树**安装时用宿主 clang 生成的 `pyconfig.h`**），
而正确做法恰好是 chibicc 的：`test/thirdparty/cpython.sh` 里它用自己 **重新 configure**（`CC=$chibicc ./configure; make; make test`），
让树自己的特性测试去决定哪些可选路径存在 —— 新增 `doc/pycxx.sh` 做同一件事（树外构建，不动源目录）。
下面是在这条路上碰到的三个真缺陷。

| | |
|---|---|
| **缺陷（1）驱动拒收所有 `-m*` 机器旗标** | `-msse2`、`-mavx2`、`-march=x86-64-v3`、`-march=native`、`-mtune=generic`、`-m32`、`-mfma`、`-mno-sse`…十二种形状全部报 `fatal error: unknown argument`；而 cpython 构建里的 Hacl 单元就是拿 `-msse -msse2 … -DHACL_CAN_COMPILE_VEC128` 编的。修法：`-m`开头的参数归**后端**（clang 才是把 IR 变成指令的那一步），收集后传给两个 clang 阶段（`-S` 与汇编器）；用户给了 `-march=`/`-mcpu=` 时目标自带的 `-march=` 就不再传（两个会冲突）；`-m32`/`-mx32` 改的是**目标**，而 cxx 的类型模型里没有 32 位 x86，所以明确报错——不能让 64 位 IR 被当成 32 位代码汇编 |
| **缺陷（2）clang 头文件里的 `__has_extension()` / `__building_module()`** | 两个都是 clang 的操作符，而 clang 自己的头文件就写在它们上面：`xmmintrin.h` 有 `!__building_module(_Builtin_intrinsics)`，`hresetintrin.h` 有 `__has_extension(gnu_asm)`。cxx 把它们当成**未知标识符后跟一个括号表**，于是报 `called object ‘0’ is not a function or function pointer`。修法：在 `#if` 求值管线里接上两个操作符，都算 0（cxx 不构建模块、也没有 clang 扩展可报，正是 clang 对一个普通翻译单元的答案），参数括号内整体跳过 |
| **缺陷（3）特性宏：cxx 一个都不定义** | x86_64 上 gcc 和 clang 都定义 `__SSE__ __SSE2__ __SSE_MATH__ __SSE2_MATH__ __MMX__ __FXSR__`，aarch64 上都定义 `__ARM_NEON __ARM_FP __ARM_FEATURE_*` 一整组，cxx 一个都没有（所有此类头文件都走标量路径）。分开处理：**aarch64 补上**（与 clang 逐宏一致，只有 SVE/SME 的三个标记 `__ARM_NEON_SVE_BRIDGE`/`__ARM_STATE_ZA`/`__ARM_STATE_ZT0` 有意不定义，因为 cxx 没有 SVE/SME）；**x86_64 故意不加**（见下）。当用户显式给 `-m*` 时，这些宏**从 clang 自己那里取**：把 clang 在该旗标下的宏表与基线求差，落到目标的预定义宏表上——**不是 `-U`**，因为“先定义再取消”会触发 cxx 自己的 `undefining builtin macro` 警告，而 `-mno-sse` 在 gcc/clang 里就是“从不定义”（实测：两家都不报）。实测结果：`-m64`/`-mtune=generic` 不改宏，`-msse3` 及以上、`-mavx*`、`-mfma`、`-march=x86-64-v2/v3`、`-march=native` 与 clang **逐宏相同** |
| **为什么 x86_64 的基线特性宏故意不定义** | 先按两家的样子加上了，然后实测到它会**把本来走标量路径的代码变成硬错误**：一旦头文件看到 `__SSE2__`，它就会包含编译器自带的 intrinsic 头（`<xmmintrin.h>` 及以上），而那些头是用 cxx 没有的**向量扩展**写的：实测 cpython 的 `Python.h` 会一路走到 `xmmintrin.h`，`Python/pystrhex.c` 就算把 SIMD 开关全关上也编不过。背书一个 cxx 服务不了的特性，代价由使用者承担（而且 gcc/clang 之所以能这么定义，正是因为它们有向量类型），所以基线保持不广告，只有用户点名要的旗标才带上它暗示的那一串。**这是一条有意的差异**，已写在 `src/amd64/target.c` 里 |
| **缺陷（4）`.S` 文件报 `unknown file extension`** | cpython 随树带 `Python/asm_trampoline_x86_64.S`，它用 `#ifdef __x86_64__` 选符号，所以必须先过预处理器。cxx 的 `get_file_type()` 只认 `.s`，`.S` 直接 `fatal error`。修法：新增 `FILE_ASM_PP`，`.S` 交给 clang 一步完成预处理+汇编（clang 本来就是 cxx 第三阶段的汇编器），并把机器旗标与 `-I` 路径一并传过去；`-E`/`-S`/`-c`/链接四种出口都对。实测：一份带 `#ifdef` 的 `.S` 编译、链接、运行均通过 |
| **尝试与回退：把 `vector_size` 改成 error** | 起点是「忽略 `vector_size(16)` 会把 16 字节向量变成标量」，所以先把 `vector_size`/`__vector_size__`/`ext_vector_type` 改成 **error**。**实测证明这是错的**：glibc 自己的 `/usr/include/x86_64-linux-gnu/bits/link.h` 就有 `typedef float La_x86_64_xmm __attribute__ ((__vector_size__ (16)));`，而 `<execinfo.h>` 会把它拉进来——只是 `#include <link.h>` 的程序就编不过了（全量探针实测：cpython **381 → 379**，多出的两个正是 `Python/traceback.c` 与 `Modules/_ctypes/callproc.c`）。而且它**一点好处也没换来**：让 cpython 的 `__builtin_shufflevector` 探针失败的是那个内建函数的**隐式声明**（cxx 对它报 error），与 `vector_size` 无关。所以已回退：不认识的属性在 gcc/clang 里也只是警告，`vector_size` 继续走这条路，代价（向量变标量）记在 §3.5.3 的长期缺口里。两条新断言把这个边界钉住：`vector_size`/`__vector_size__` 与 `__attribute__((frobnicate))` 都只警告，且 `#include <execinfo.h>` + `#include <link.h>` 必须能编 |
| **验收** | `test/conformance.sh` **175 → 183 passed / 0 gap**（八条新断言：`-mavx2` 接受且定义 `__AVX2__`、`-march=x86-64-v3` 带来它的特性集、中性旗标 `-m64` 不改变任何东西、`-m32` 拒收且措辞明确、`__has_extension`/`__building_module` 在 `#if` 里算 0、`.S` 文件编译+链接+运行通过、`vector_size` 与未知属性只警告、`<execinfo.h>`+`<link.h>` 能编）；`make test` exit 0；c2y 101/0；arm64 51、rv64 51、rv32 51(+1 skipped)、`doc/probes.sh` 全部基线（含 **cxx2 = cxx3 = cxx4 逐字节相同**）、`clang-format-21 --dry-run --Werror` 干净。宏表对照：四个目标 × 十四种 x86 旗标组合，只有三种“没有新增特性”的情形与 clang 不同（无旗标、`-msse2`、`-mno-*`）——正是上一行那个故意的选择 |
| **cpython 用 cxx 自己 configure（chibicc 的做法）** | configure **exit 0** — cxx 能把 cpython 的 `configure` 跑完（它的特性测试全是用 `$CC` 编译小程序），而且 `__builtin_shufflevector` 那个探针**正确地失败**（cxx 对未声明的调用报的是 error，不是 warning），所以 `Python/pystrhex.c` 自己就走了标量路径——这正是“用自己 configure”的意义。接着的 `make -k -j8` 建出 **287 个目标文件**（`make -n` 里剩 7 个），只剩**两个失败单元**：`Modules/_hacl/Hacl_Hash_Blake2s_Simd128.o`（`__builtin_ia32_emms`，来自 `<mmintrin.h>`）与 `Hacl_Hash_Blake2b_Simd256.o`（`__builtin_ia32_clui`，来自 `<uintrintrin.h>`）。它们的开关是 configure 里一条**只问旗标能不能被接受**（`AX_CHECK_COMPILE_FLAG([-msse -msse2 …])`）的检查，而 cxx 现在正确地接受了这些旗标，所以它们会被编译——而它们真正需要的是**向量类型**（§3.5.3 的长期缺口），不是驱动、不是 configure、也不是预处理器。换句话说：**cxx 这条路上的真缺陷已经清完，剩下的就是向量类型本身** |
| **记分（`doc/realworld.sh` 全量探针）** | **git 567/567 保持全过**；**cpython 381/385 不变**——R14 修好的四个单元没有退回去（`Python/jit_unwind.c`、`Modules/posixmodule.c`、`Modules/_testsinglephase.c`、`Modules/_testcapimodule.c`），剩下四个仍是 `Python/pystrhex.c`的 `__builtin_shufflevector`、两个 HACL SIMD 单元、`Modules/_ctypes/_ctypes_test.c` 的 `<complex.h>`；lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21 全部不变。本轮新增的驱动能力（`-m*`、特性宏、`.S`）**没有改变任何单元的成败**——能力变多了而记分不动，正是想要的；回退 `vector_size` 那一步也在这里得到了确认（带着 error 的那一版是 379） |

### R14 真实开源项目：第十一轮 —— ✅ R13 列出的真缺陷清零（五处修复，cpython +4）

| | |
|---|---|
| **缺陷（1）宏调用的参数之间不能有预处理指令** | 主循环把输入切成「直到下一个行首 `#`」的片段，然后才对片段做宏展开——一个位于**宏调用参数表内部**的 `#ifdef` 因此把调用从中截断：收集器拿到的参数在指令处就结束了，一路找不到配对的 `)`，直到文件尾报 `premature end of input`（cpython 的 `Python/jit_unwind.c`：`DWRF_SECTION(CIE, … #ifdef __x86_64__ … #endif …)`）。修法：片段收集器记录括号层数，括号**内**的 `#` 不再结束片段；八条条件指令从主循环里提出成 `cond_directive()`，两边共用——该指令就在原地求值，未选中分支的记号不进入参数（括号与逗号也不算），而未选中分支里的非条件指令（常见的是 `#else` 里的 `#error`）随该分支一起逐行跳过。与 gcc/clang 对照：`#if 1 / #if 0 / #else / #elif 0 / #else #error / #endif` 六种形状的预处理输出逐字相同 |
| **缺陷（2）四个内存类内建函数完全缺失** | `__builtin_memset` / `__builtin_memcpy` / `__builtin_memmove` / `__builtin_memcmp` 在 cxx 里一个都没有。glibc 的 `CPU_ZERO_S` 在长度不等于 `sizeof(cpu_set_t)` 时直接写 `__builtin_memset (cpusetp, '\\0', __size)`，于是 `Modules/posixmodule.c` 报 `implicit declaration of function ‘__builtin_memset’`。修法：**它们就是同名库函数**（gcc/clang 也是这么降级的），因此新增一个内建类别的处理：表行的 `intrinsic` 字段改存**库函数名**，`parse_mem_builtin()` 按名字查找（头文件里的声明就是它），没有时按标准原型（`void *f(void *, const void *|int, size_t)`，`memcmp` 返回 `int`）**就地声明一份并登记进模块**，然后走普通的 `fncall()`——于是类型检查、返回值、取地址、`__has_builtin` 全部正常，而且**不必包含 `<string.h>`**（内建函数本来就不该要求头文件）。一个坑：`fncall()` 里的 `lvalue_convert()` 会把函数当左值加载（出 `load (null), ptr @memset`），因为 `add_type()` 对每个 ND_VAR 都置 `is_lvalue`；`postfix()` 靠的是在 `fncall()` **之前**先把函数设计符用 `new_imcast()` 转成指针（6.3.3.1 的转换），这里照做即可 |
| **缺陷（3）括号包着的字符串字面量不能初始化数组** | `static const char helper_mod_name[] = (PyHelpers_MOD_NAME);`（cpython 的 `Modules/_testsinglephase.c`，展开后就是 `= ("…")`）报 `array initializer must be an initializer list`；两家都把括号当作不存在。修法：`initializer2()` 先数一下前导括号，若紧跟着一个字符串字面量且随后恰好闭合相同个数，就把它们与字面量一起消费；其余形状（`(1 + 2)`、`(1, 2)`）照旧落到初始化器列表路径（两家也都拒绝） |
| **缺陷（4）（同一函数里碰到的）`const` 的宽字符串数组被拒** | `static const wchar_t a[] = L"wide";` 报 `array of inappropriate type initialized from string constant`：检查拿字面量的类型与数组的类型直接比，而 `const` 落在**元素**上（`type_unqual()` 只清掉传入类型自己的限定符）；`const char a[] = "x"` 之所以能过，只是因为 `is_char()` 那一半先短路了。修法：按上一行 `braced_str` 检查的写法比**元素类型**（双方 `type_unqual()` 后），`char16_t`/`char32_t` 一同修好；真的不匹配的（`const int x[] = "no"`、`char y[] = L"no"`）仍然报错，与 gcc 同位置同措辞 |
| **缺陷（5）（查剩下失败时碰到的）`__alignof__` 一个对象时给的是类型的对齐** | cpython 的 `Py_ALIGNED(64) char buf[4];`（局部也好、全局也好）实际对齐就是 64（cxx 发出的 alloca/global 也带着它），但 `__alignof__(buf)` 回答类型的 1，于是 `Py_BUILD_ASSERT(__extension__ __alignof__(buf) >= 64)` 失败（`Modules/_testcapimodule.c`）。修法：`__alignof__` 的**表达式形式**在操作数是一个变量时取该变量的对齐（`MAX(var->align, ty->align)`），类型形式仍取类型的对齐；四种形状（局部/全局×数组/标量、`_Alignas`）现在与两家一致，平凡对象仍报类型对齐。**记录一条保留差异**：GNU 拼写的 `aligned` 写在 typedef 后面时应该进入**类型**（`typedef char A[4] __attribute__((aligned(64))); _Alignof(A) == 64`），cxx 仍只把它当声明属性——类型属性与声明属性的分派是 `apply_postdecl_attrs()` 里的另一件事 |
| **验收** | `test/conformance.sh` **171 → 175 passed / 0 gap**，四条新断言：宏参数之间的条件指令（含嵌套 `#if`、未选中分支里的 `#error`，并在运行时校验只有选中分支的语句跑了）、内存类内建（**不包含任何头文件**，运行时校验四个函数与 `__has_builtin`）、括号包着的字符串初始化（含 `const wchar_t` / `char16_t`）、过对齐对象的 `__alignof__`（局部/全局、数组/标量、`_Alignas`，并在运行时检查对象地址真的对齐）。全套复跑：c2y 101/0、arm64 51、rv64 51、rv32 51(+1 skipped)、`doc/probes.sh` 全部基线（c2ycov 109/2、selfhost 21/0/0、asm 68/0 …）、`doc/bootstrap.sh` 仍是 **cxx2 = cxx3 = cxx4 逐字节相同**（预处理器重构后仍然成立，这是本轮最有分量的一条）、`make test` exit 0、`clang-format-21 --dry-run --Werror` 干净 |
| **记分（`doc/realworld.sh` 全量探针）** | **git 567/567 保持全过**；**cpython 377/385 → 381/385**（本轮修好的四个单元：`Python/jit_unwind.c`、`Modules/posixmodule.c`、`Modules/_testsinglephase.c`、`Modules/_testcapimodule.c`）；lua 35/35、zlib 15/15、libpng 18/18、sqlite 1/1、tinycc 21/21 不变 |
| **剩余阻塞项** | **cpython 4，全部是「有意不做」的长期缺口——探针里已经没有剩下的 cxx 缺陷**。三个要 `vector_size` 向量类型：`Python/pystrhex.c`（`__builtin_shufflevector`）、`Modules/Hacl_Hash_Blake2s_Simd128.c`、`Modules/Hacl_Hash_Blake2b_Simd256.c`——它们的开关 `_Py_HAVE_EFFICIENT_BUILTIN_SHUFFLEVECTOR` 来自这棵树**安装时用宿主 clang 生成的 `pyconfig.h`**；第四个是 `Modules/_ctypes/_ctypes_test.c` 在 `__GNUC__` 下包含 `<complex.h>`（`_Complex`，§3.5.3 有意不做；cxx 已按标准定义 `__STDC_NO_COMPLEX__ 1`，是 glibc 不看它）。除这两项外，七个真实项目里没有再能修的失败单元；再往上走只能靠补齐这两项长期缺口（向量类型、复数）或换更大的项目去找新缺陷 |

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

依据见 §1 的 P-工作项与 `doc/c2ycov.sh`（原手工清单 §10 的 6 条覆盖空洞已并入）。

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

### 4.1 工程注意：探路脚本与后台作业（用血换来的两条）

**（一）探路脚本里的任何编译器调用都必须加 `timeout`。**

反面教材：第 R25 / R28 两轮为了“看一眼 `118_switch` 为什么失败”，脚本里写了裸调用

```bash
~/cxx/cxx -w -o /tmp/x_118_switch 118_switch.c     # 没有 timeout
```

而当时 cxx 正好在那个用例的超大 `case ... ` 区间上**死循环展开**。于是 WSL 里留下两个
`cxx ... -cc1` 进程，各占满一个核，分别跑了 **6 小时 10 分**和 **3 小时 11 分**，直到人来问
“后台有一个 6 小时的进程是什么”才被发现。更要命的是：它们加载的是**修复之前**的二进制映像，
所以后来把死循环修好，对这两个进程毫无作用。

规定：

```bash
timeout 60 ~/cxx/cxx ...        # 单文件编译、最小复现
timeout 600 bash doc/tcctests.sh ./cxx
timeout 3000 bash doc/probes.sh ./cxx
timeout 7200 bash doc/realworld.sh ./cxx
```

长跑一律 `timeout` 包住并由后台作业跑；**不要把长命令的输出接给 `tail`**（会没有输出可看），
写进 `/tmp/*.log` 之后再看。

**（二）`job_kill` 之后要复查 WSL 侧的进程：包装进程被杀 ≠ 子进程被杀。**

同一事件里，第 R28 轮那次（作业 `pwsh-272`）我确实执行过取消，日志也回了 “killed”，
但被终止的只是 **PowerShell 侧的包装进程**；`wsl -d Ubuntu -- bash -lc "..."` 那棵进程树
（`bash → cxx → cxx -cc1`）已经和它脱开，继续跑。第 R25 轮那次（`pwsh-174`）甚至没人注意到
它还挂在后台 —— 它在“完成”通知里才现身。

规定：取消作业之后，按下面这套动作确认，再继续下一件事。

```bash
# 1) 看有没有长跑残留（按存活时间倒序）
ps -u memory -o pid,ppid,etimes,pcpu,stat,cmd --sort=-etimes | head -20

# 2) 先 TERM 脚本包装进程，再 KILL 它派生的编译器，最后复查
kill -TERM <bash wrapper pid>
kill -KILL <cxx / cxx -cc1 pid>
ps -u memory -o pid,etimes,pcpu,cmd --sort=-etimes | grep -E 'cxx|probes|realworld|tcctests' | grep -v grep || echo '(clean)'
uptime      # 负载应当回落
```

排查时的判据：`etimes`（存活秒数）远大于该作业的预期耗时、`pcpu` 贴近 100%、命令行里是你
**早先**那轮脚本生成的临时输出路径（`/tmp/x_118_switch`、`/tmp/y118` 这种），三条同时成立
就是僵尸，不必再等。

代价说明：这两个进程与后来的每一轮全量探针**抢核**，是前几轮扫描偏慢的直接原因；负载从
`~3.1` 回落到 `~1` 也印证了这一点。

---


### 4.2 临时文件的落点（本轮起生效）

`~/` 下曾经堆着 2249 个零散脚本（多轮累积的 `rNN_*.sh` / `*.py`），清理时全部移进了归档。
从那以后按用途分开，方便一次性收拾：

| 目录 | 放什么 |
|---|---|
| `~/cxxwork/active/` | 本轮正在用的脚本（`rNN_*.sh`、补丁脚本） |
| `~/cxxwork/logs/` | 构建与探针日志（`logs/ffmpeg/make.log` 之类） |
| `~/cxxwork/repro/` | 要留下的最小复现目录（`bbox`、`vla` 等） |
| `~/cxxwork/archive/` | 归档，确认后整个删掉即可 |

**要长期重复运行**的探针不属于这里，属于仓库：`doc/*.sh`（FFmpeg 与内核的两套也应尽快搬进去）。
本轮清理只做移动、没有删除；`~/download` 等不属于本项目的目录未被触碰。

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

## 7. 可选方案评估：外部定义的一次预解析（is_fndef）

> 起因：A.3.4 的 「f中文文法」写着
> `function-definition: attribute-specifier-sequence_opt declaration-specifiers declarator function-body`。
> 如果先读一遍「属性序列 + 声明说明符 + 声明符」、丢掉中间结果，
> 就能在**读参数列表之前**得到“这是定义还是声明”，从而取消现在那些“事后认领”。
> 本节只做评估，不实施；结论在最后。

### 7.1 现状：四处“事后认领”

现在分发发生在 `external_declaration()`（`src/parser.c:8442`），判据是 `tok->kind == TK_LBRACE`——
而参数列表在**那之前**就已经读完了，所以参数总是按“函数原型作用域”解析，事后再补：

| # | 代码 | 作用 |
|---|---|---|
| 1 | `proto_locals` + `adopt_proto_locals()`（8060–8070，调用在 8543） | 参数类型的 VM 计数器、隐藏栈指针在声明符期间被**摘下**，定义时再**接回**到 `locals` 头部（irgen 把它们当前置条目读） |
| 2 | `proto_scope` + `adopt_proto_vla_exprs()`（8053、8072–8079，调用在 8513） | 把原型作用域里注册的 VLA 边界表达式搬进函数作用域（每次调用在函数体开头求值） |
| 3 | `param_syms` + `note_param_sym()`/`param_sym_of()`（8022–8044，8187 与 8527） | 原型作用域已消失，所以定义时要把“边界表达式里命名了前面参数”的引用重新指向函数体看到的那个符号 |
| 4 | `ty->params` 的不完整类型检查（8450–8459） | “定义的参数必须完整、声明的不必”——注释里写明它**原本在 `func_param()` 里**，因为那时分不清而误伤声明，才搬到这里 |

还有一处**语义错位**作为佐证：`func_param()` 对每个参数都传 `is_param=true`（8129），
于是 `[*]`、数组声明符里的限定符等 6.7.6.2p5 规则在定义里也被当成合法——
实测三家都拒绝 `int defn(int a[*]) { ... }`（cxx 也拒绝，但靠的是另一条路径）。

### 7.2 提案能删掉什么

若在 `external_declaration()` 入口先跑一次 `is_fndef()`，则定义的参数列表**从第一个参数起**就落在函数自己的作用域里：

- 7.1 的 **1、2、3 三套机制直接消失**（约 55 行加四个调用点）：符号不需要“摘下再接回”，
  VLA 边界不需要搬家，参数引用不需要重指；`locals` 头部的顺序也自然成立。
- 7.1 的 **4 可以搬回 `func_param()`**，回到它本来该在的位置（那不是一个“定义才有”的补丁，而是参数自己的规则）。
- `is_param` 的语义也归位：定义的顶层参数不再是“原型作用域”，`[*]` 与限定符规则自然不适用。
- 用户指出的那条事实也因此变成代码里的结构：**除了定义顶层，其余所有参数列表都是函数原型作用域**。

### 7.3 代价与风险

**（一）重复解析的开销（已测）**

预解析只重复「属性序列 + 声明说明符 + 声明符」，不重复函数体。对一个**只有原型**的文件，它就是全部开销，因此可以直接量：

| 文件（4000 条声明） | 仅预处理 | +解析/折叠 | 解析+折叠 |
|---|---|---|---|
| `proto.c`（4000 个原型） | 969 ms | 1024 ms | **55 ms** |
| `defs.c`（4000 个微型定义） | 2629 ms | 2811 ms | 182 ms |
| `sqlite3.c`（26.9 万行） | 5279 ms | 5771 ms | **492 ms** |

即：**4000 个声明符约 55 ms**，换到 sqlite3.c 这种真实翻译单元上，
多一遍预解析约为 **总时间的 1%**（55 ms / 5771 ms），占解析阶段本身约 10%。
（注：预处理占绝大头——sqlite3.c 的 5771 ms 里有 5279 ms 在预处理）。

另一项成本是**内存**：cxx 的分配器从不回收，被丢弃的那一遍的型别与符号会留在 arena 里，量级与声明符本身相当。

**（二）需要新增的机制**

1. **静默模式**：预解析不能重发警告（否则每条声明的警告都会出现两次），
   但语法错误该报的还得报，且不能报两次。
2. **回滚**：符号表、作用域栈、VLA 注册、标签环都要能撤销。
   现有的 `proto_locals` 就是同一个技巧的局部版本，可以推广，但那本身也是一套机制。
3. **判据规则**：“下一个 token 不是 `{` 就是声明”——这在 cxx **当前成立**，
   因为 cxx 不支持 K&R 式定义（已核实：源码里没有任何 K&R 路径）。
   但一旦要加 K&R，判据就要扩成“`{` 或参数声明列表的起始”，而“参数声明列表的起始”正是一个声明说明符的起始——
   与“空声明”不可区分，所以 K&R 与本方案天然冲突。
4. **其他边界**：`asm` 标签、`__attribute__` 序列、`static inline` 等都已在被预读的那段里，不需要额外处理；
   但定义的**重定义/类型冲突**诊断（`check_decl_compatile`、`redefinition of`）8461–8475）仍在第二遍做，不受影响。

### 7.4 结论（本轮不实施）

| 维度 | 判断 |
|---|---|
| 能不能简化 | **能**：7.1 的 1/2/3 三套机制（约 55 行）直接消失，第 4 项回到它该在的位置，`is_param` 语义归位 |
| 性能 | 多一遍声明符解析：**真实文件上约 1% 总时间**（已测，见 7.3），不是瓶颈（瓶颈是预处理） |
| 代价 | 一套“静默 + 回滚”机制，以及一个与 K&R 天然冲突的判据 |
| 建议 | **作为可选方案保留，不立即实施**。两个触发条件：① 原型作用域这块逻辑再次变复杂（比如又多一类 VM 类型规则）；② 要加 K&R 式定义——那时反正要动判据，一并做更划算 |
| 实测口令 | `~/cxxwork/repro/preparse/` 下的 `proto.c`/`defs.c` 生成脚本与 `cxx -dump-tokens` / `-ast-dump` 对比（本节的数字由此得出） |
