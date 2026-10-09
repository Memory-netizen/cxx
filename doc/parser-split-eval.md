# parser.c 拆分评估（存档备用）

状态：**仅评估，未实施**。日后再议拆分时可直接照此执行；本文同时收录 `builtin_fn.c`
那一份专项评估（§5）。

相关文档：`doc/builtin-redesign.md`（内建重构设计）、`doc/cxx-c2y-plan.md`（总体计划）。

行号与数字取自 2025 年当前源码（`src/parser.c` 9,482 行）。

---

## 0. 结论速览

1. **提案可行，但不建议一次性搬迁**：`parser.h + decl.c + stmt.c + expr.c + builtin_fn.c`
   是**一次互递归文法的文本位移**，不是分层；边界由纪律维持，编译器不提供任何约束。
2. **提案有缺口**：量出来的 `core` 组 **1,704 行（18.0%）无家可归**（节点构造器、作用域/名字
   空间机制、常量求值、`parse()` 本身）；`asm_*` 约 342 行是独立领域，塞进 `stmt.c` 不如 `asm.c`。
3. **收益主要在可读性/定位**；增量编译几乎为零（`parser.o` 仅 0.34 s），且本项目没有单元测试
   框架，「更容易单测」不成立。
4. **风险集中在头文件**：`parser.h` 要暴露 **134 个符号 + 54 个文件级变量**，而 `scope`、`cur_fn`
   这类状态被每个组读写 —— 拆分后是「共享同一份可变状态的四个文件」。
5. **验证手段很强**，风险因此可控：纯位移必须保住 **bootstrap 逐字节相同**
   （cxx2 = cxx3 = cxx4、21/21 目标文件一致），再加 `make test`、`tcctests`、全量 Fujitsu、崩溃全扫。
6. 建议路径：**阶段 0 只建 `parser.h`（不搬代码）→ `decl.c`（变更份额最大）→ `expr.c`/`stmt.c`
   → `builtin_fn.c`（等 A 类声明化收尾后再搬）**；`eval.c`（常量求值 ≈500 行）内聚最好，值得优先于
   expr/stmt。低成本替代：先在 `parser.c` 顶部加一份按领域的**分节目录注释**。

---

## 1. 量出来的结构

`src/parser.c`：9,482 行、241 个顶层定义、222 个 `static` 函数、54 个文件级 `static` 变量。

| 目标文件 | 定义数 | 行数 | 占比 | 最大的几项 |
|---|---|---|---|---|
| `decl.c` | 73 | **2,946** | 31.1% | `declspecs` 397、`external_declaration` 396、`init_decl_list` 341、`enum_decl` 185、`initializer2` 138 |
| *（提案未安排）* `core` | 81 | **1,704** | 18.0% | `eval2` 266、`layout_struct` 130、`eval_int128_wide` 93、`parse` 89、`array_dimensions` 82、`check_unused_statics` 82 |
| `builtin_fn.c` | 24 | 1,262 | 13.3% | `parse_builtin_fn` 532、`builtin_defs` 165、`atomic_compound_assign` 123 |
| `stmt.c` | 34 | 1,164 | 12.3% | `asm_stmt` 139、`asm_tmpl_conv` 128、`compound_stmt2` 127、`label` 120 |
| `expr.c` | 29 | 1,027 | 10.8% | `unary` 153、`generic_selection` 149、`postfix` 135、`primary` 133 |
| 静态表/前向声明/注释 | — | ~1,379 | 14.5% | `sc_table`、`as_binop`、222 条前向声明 |

常量求值家族（`eval2` 266 + `eval_int128_wide` 93 + `eval_fp128` 46 + `const_array_elem` 65 +
`is_const_expr` 28 ≈ **500 行**）只吃 `Node` 吐常量，与文法耦合最低 —— 单看内聚性，
`eval.c` 比 `expr.c`/`stmt.c` 更值得先拆。

内建领域另有 `asm_*` 一组（`asm_stmt` 139 + `asm_tmpl_conv` 128 + `asm_operand` 42 + `asm_decl` 33
≈ **342 行**），同时碰声明、语句、类型与约束机制，宜独立成 `asm.c`。

---

## 2. 耦合：三向环 + 一个大内部 API + 共享可变状态

### 2.1 三个文法文件互为递归（跨组调用边，调用次数/涉及符号数）

| 从 \ 到 | expr | stmt | decl | core |
|---|---|---|---|---|
| **expr** | — | 2/2 | 18/9 | 41/22 |
| **stmt** | 21/7 | — | 28/19 | 61/24 |
| **decl** | 23/10 | 3/3 | — | 84/40 |
| **builtin** | 21/9 | 0/0 | 6/5 | 23/8 |

三向环是真实存在的：声明里有语句（函数体、`asm_decl`）与表达式（数组界、枚举值、位域）；
语句里有声明（块）与表达式；表达式里有声明（复合字面量、`typeof`）与语句（语句表达式 `({…})`）。

### 2.2 内部 API 的宽度

**134 个 parser.c 符号被别的组调用**，其中 **38 个被 3 个以上组调用**。热点：
`new_node`(32)、`new_binary`(17)、`new_var_node`(16)、`new_unary`(12)、`const_expr`(12)、
`cnt_blk`(12)、`assign`(11)、`get_ident`(9)、`is_typename`(8)、`enter_scope`/`leave_scope`(各 8)、
`new_var`(7)、`find_ident`(7)、`new_num`(7)、`push_namespace`(7)。

另有约 15 个文件内结构体/枚举需要进 `parser.h`：`Scope`、`NameSpace`、`TagNameSpace`、`Cleanup`、
`VmDecl`、`ParkedRef`、`InitDesg`，以及三个匿名 enum/struct。

### 2.3 状态才是真正的耦合

**54 个文件级 `static` 变量**：`scope`/`file_scope`、`locals`/`globals`、`cur_fn`/`cur_init`/`cur_sw`、
`gotos`/`labels`/`named_loop`、`vm_decls`/`vm_seq`、`fn_vla_guards`/`fn_vla_decls`、`strlit_ht`、
`parked`、`live_init`、`static_init_ctx`、`uneval_operand`、`local_label_num`…

热点被每个组读写（按出现次数：`scope` decl 75、stmt 38、core 25、expr 17、builtin 4；`cur_fn` 18/13/6/2/4）。
`parser.h` 必须把它们全部 `extern` 化 → 四个文件共享同一份可变状态，**「expr.c 不得读声明状态」
这类约束无法由编译器强制**。

---

## 3. 收益（量化）

| 收益 | 实测/结论 |
|---|---|
| **可读性与定位** | 真实且是主要动机：没有超过 3k 行的文件，产生式在文件名上 |
| 补丁局部性 | 最近 40 个改过 parser.c 的提交共 **358 行**变更：decl 39.4%、core 34.9%、expr 11.5%、builtin 9.5%、eval 4.7%、stmt 0% → 一次改动通常落在一个组 ✓ 但单次提交本就 ~9 行，评审负担改善有限 |
| 增量编译 | **`parser.o` 编译 0.34 s**（irgen.o 0.21 s、type.o 0.15 s）→ 拆分每次编辑省约 0.2 s ✗ 可忽略 |
| 可测试性 | 本项目**没有单元测试框架**（测试全过 driver 二进制）✗ |
| 并行开发 | 两个方向的工作不挤在同一个 9.5k 行文件上 ✓（无法量化） |
| 可检查的边界 | 类似「irgen 无用户可见诊断」（`grep -c` 为 0）的结构性检查 ✓ 弱但真实 |

---

## 4. 风险（量化）

| 风险 | 说明 |
|---|---|
| **头文件成为新的耦合中心** | 134 条声明 + 54 个 `extern`；纪律不可强制。先例：本项目倾向于「一份共享实现」而非每文件一份拷贝（2025 年刚清掉 `abi_lowering` 的第 3 份副本；当前跨文件同名 `static` 仅 4 对：`cast` parser/irgen、`filter_tokens` preprocess/main、`from_hex` int128/lexer、`print_type` dumpir/dumpast）——拆分不能变成复制小工具的许可证 |
| **三向环不可消除** | 跟一个构造仍要在 3 个文件 + 头文件间跳；搬的是文本不是概念单元 |
| **一次性大搬迁不可评审** | 必须拆成多提交（先头文件，再逐组） |
| 符号外链后的重名 | 与其余单元的 206 个外部符号 + `cxx.h` 比对：**无冲突**；真冲突也会是立即链接错误 |
| 与在飞工作冲突 | parser.c 是修复最密集处（churn 39% 在 decl、35% 在 core）；`builtin-redesign.md` 的 A 类声明化仍在进行，会**删除**一部分 `parse_builtin_fn` |
| **验证手段强** | 纯位移必须保住 bootstrap 逐字节相同；再加 `make test`（driver/error/ir/conformance/c2y）、`tcctests` 106、全量 Fujitsu 17,638/189、崩溃全扫 37,190→0、`-E` 往返。未知量不是「会不会改坏」，而是「边界值不值」 |
| 构建侧 | `SRCS := $(shell find src -name '*.c')` 自动纳入新文件；`selfhost`/`bootstrap` 会编译每个文件 ✓ 无需改 Makefile |

---

## 5. `builtin_fn.c` 专项

| 项 | 实测 |
|---|---|
| 规模 | 25 个定义、**1,264 行 = parser.c 的 13.3%**（`parse_mem_builtin` 44 行已随 §R119 删除）；`parse_builtin_fn` 532 行（62 个 case）、`builtin_defs` 表 165 行（**86 行表项**：评估时为 24 DECL + 62 SPECIAL，A 类收尾（§R119）后为 30 + 56）、`atomic_compound_assign` 123 行 |
| 出向依赖 | 需要 parser.c 的 **22 个符号**，其中 **17 个今天是 `static`**（要新增声明）；还要共享 4 个变量（`scope` 14 处、`cur_fn` 20 处、`live_init` 5 处、`cur_init` 3 处） |
| 入向缝隙 | 只有 **6 处**：`primary` →（`is_builtin_fn`/`builtin_class`/`declare_builtin`/`parse_builtin_fn`）、`postfix`/`unary`/`assign` → `atomic_compound_assign`、`eval2` → `builtin_kind_of`、`external_declaration` → `is_builtin_fn` |
| 已经是四文件分布 | parser（表+解析）、`opt_ast.c`（折叠，`fold_builtin_call`）、`irgen.c`（降级，用 `builtin_kind_of`/`builtin_type`/`builtin_def`）、`preprocess.c`（`__has_builtin` 用 `is_builtin_fn`） |
| 关键判断 | A 类声明化会把 A 类内建从 `parse_builtin_fn` 里**删掉**（走普通 `fncall`）。所以**先做完声明化再搬**，否则要搬两次：做完后 `parse_builtin_fn` 只剩 B 类（`alloca*`、`constant_p`、`types_compatible_p`、`offsetof`、`__c11_atomic_*` 等） |
| 只搬元数据的折中 | 表 + 识别 + 元数据（`builtin_defs` 165、`builtin_find`、`is_builtin_fn`、`builtin_class`、`builtin_kind_of`、`builtin_def`、`intern_builtin_ids`、`check_builtin_rows`、`builtin_target_type`、`builtin_type` ≈ **270 行**）**不依赖 parser 任何内部**（只用 cxx.h 类型、`T` 钩子、`intern`/`error`）→ 零新增暴露，且 `builtin_kind_of` 已被 irgen/opt_ast 使用、`builtin_type`/`builtin_def` 被 irgen 使用。`declare_builtin` 要用 `scope`，留在 parser.c |

---

## 6. 若实施：分阶段清单

| 阶段 | 内容 | 说明 |
|---|---|---|
| 0 | 建 `src/parser.h`（~15 个类型 + 134 条原型 + 54 个 `extern`），**代码一行不搬** | 把最易错的部分单独隔离并可评审 |
| 1 | `decl.c` | churn 份额最大（39.4%），收益最快 |
| 2 | `expr.c`、`stmt.c`（各一提交） | 互递归的一对，头文件已就位后是机械位移 |
| 3 | `builtin_fn.c` | 等 A 类声明化收尾 |
| 4 | `eval.c`（≈500 行，可选但内聚最好）；`asm.c`（≈342 行） | |
| 替代 | 只在 `parser.c` 顶部加分节目录注释（~20 行，零风险） | 先做这个再判断是否需要拆文件 |

每阶段验证：`make -j8` → `make test` → **bootstrap 逐字节相同** → 阶段结束后跑一次全量 Fujitsu。

三条约束：`parser.h` 只给 parser 家族用（**不要**被 `irgen.c`/`opt_ast.c` 包含，否则会成为第二个
`cxx.h`）；不要用拆分当复制小工具的借口；每个阶段单独可回退。
