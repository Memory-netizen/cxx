# 内建函数模块重构设计（声明式内建 + 统一折叠 + 统一 IR）

状态：**已落地**。声明式内建表（一行一个内建，见 §5.2）、统一折叠（§3.3）、按 id 的 IR 捕获
（§3.4）都在用；**A 类声明化已做完**：表里 **86 行**，其中 **30 行声明式（BCLASS_DECL）**、
**56 行不可声明化（BCLASS_SPECIAL）**，后者的逐族理由见 §2.2，本轮收尾记录见 §5.5。

相关文档：`doc/cxx-c2y-plan.md`（总体计划与工作项）、`doc/c2ycov.sh`（一致性覆盖）。

---

## 0. 目标

把 `__builtin_*` 从「解析期直接降级为专用 AST 节点」改为**三层分离**：

1. **解析期**：能由用户写成普通函数原型的内建，**隐式生成一个普通的函数声明符号**，此后完全走
   既有的 `fncall()` → `ND_FUNCALL` 路径，不再有专用节点。
2. **折叠期**：在 `opt_ast.c` 的 `ND_FUNCALL` 处**统一**做常量折叠——先折叠全部实参，再用
   `is_builtin_fn()` 判断被调者是否为内建，是则按 id 分派到该内建的折叠例程。
3. **IR 期**：`irgen.c` 的 `ND_FUNCALL` 入口按 id 捕获，把函数名映射为 LLVM 内建名。

无法由用户声明的内建（`__builtin_alloca*`、`__builtin_constant_p`、
`__builtin_types_compatible_p`、`__c11_atomic_*`）**保持现状**。

**额外约束（项目方提出）**：后期要脱离 LLVM 自行发射 `IR → 汇编`，因此 IR 必须**保留足够信息**
供自行发射使用；内建处理要有**类型**层面的信息，不能只把一切都压成一次不透明调用。

---

## 1. 现状（已核实）

行号基于实时源码，`sha256` 核验（2025 修订；文档最初记录于 `c1e41977…`/`c0eea630…`，
此后 `cxx.h`/`parser.c` 因 stddef、const 数组成员、dump-ast 等改动而变化，行号已按当前源码重取）：

```
368ef49e83df98a5138c701e25b83d2b367e665062d4086f77d2351a06b2c48e  src/parser.c
f7dd815e99e61583b5e28cc28991f31ccdfc613eea68e7c5abc12d40e23ea84f  src/cxx.h
```

### 1.1 内建表与识别

| 项 | 位置 | 说明 |
|---|---|---|
| `BUILTIN_*` 枚举 | `parser.c:1383-1405` | 文件内匿名枚举，值 1..20 |
| `builtin_fn[]` 表 | `parser.c:1413-1438` | 20 条 `{char *name; uint32_t id; int kind}` |
| `is_builtin_fn(id)` | `parser.c:1442-1450` | 线性扫描，返回 kind（0 = 非内建） |
| 解析期识别 | `parser.c:2069-2075` | `primary()` 的 `TK_IDENT` 分支**最先**判断 |
| `__has_builtin` | `preprocess.c:1913` | 答案就是 `is_builtin_fn(id) != 0` |

`builtin_fn[]` 中间那列 `0` **不是标志位**，是惰性填充的 interned id 缓存槽。

### 1.2 现状的三个结构性限制

1. **内建不产生符号**：`parse_builtin_fn`（`parser.c:1560-1762`）任何分支都不建 `Sym`，所以内建
   **没有类型、没有地址、不能取址、不能存入函数指针、不能间接调用**（`&__builtin_bswap32` 会在
   `skip` 处报错）。
2. **内建不产生 `ND_FUNCALL`**：直接降级为 `ND_BSWAP` / `ND_ALLOCA` / `ND_ATOMICRMW` / `ND_CAS` /
   `ND_FENCE` / `ND_COMMA` / `ND_NUM`。
3. **每个 case 自己解析括号与实参**，没有公用的元数/类型检查机制；识别点**优先于**用户声明，
   所以用户无法覆盖同名内建。

### 1.3 折叠与 IR 的现状

- `fold_node` 的 `ND_FUNCALL`（`opt_ast.c:435-437`）只递归实参、**连 `node->func` 都不访问**，
  完全不折叠。
- **全编译器没有任何「折叠函数调用」的机制**。
- `eval2`（`parser.c:2754-2939`，用于 `#if`/枚举/数组界/case/`_Static_assert`）**没有 `ND_FUNCALL`
  分支**，落到 `default: error(node->tok, "not a compile-time constant")`。
  因此 `static int a[__builtin_bswap32(1)]` 这类**常量表达式**目前用不了内建。
- `is_const_expr`（`parser.c:1443-1451`，即 `__builtin_constant_p` 的实现）调用 `fold_node` 后要求
  `ND_NUM`，所以今天 `__builtin_constant_p(__builtin_bswap32(1))` 也是 0。
- `irgen.c:663-677` 的 `ND_FUNCALL` 已经是一条通用路径：
  `call_ops[0] = gen_expr(node->func)`，其余为实参，`narg = nargs + 1`。
- 普通调用的被调者 `Ref` 是 **`RGlb(id, PTR(TY_FUNC))`**（经 `gen_addr` 的全局分支，
  `irgen.c:222-233`）。`dumpir.c:465-466` 会对 `TY_PTR` **剥一层**再读 `is_variadic`/`params`，
  所以指针与裸 `TY_FUNC` 两种写法都能正确打印。

---

## 2. 分类：哪些内建可以「声明化」

判据是**能否用一条固定的 C 函数原型表达**，且**实参类型不能被隐式转换破坏**。

### 2.1 A 类：声明式（本轮设计对象）

| 内建 | 建议原型 | LLVM 内建 | 备注 |
|---|---|---|---|
| `__builtin_bswap16/32/64` | `extern US __builtin_bswapN(US)` | `llvm.bswap.iN` | 已有，签名固定，**单参数** |
| `__builtin_clz/ctz` | `extern int __builtin_clz(unsigned int)` | `llvm.ctlz.i32` / `llvm.cttz.i32` | **两个参数**，见下 |
| `__builtin_clzl/clzll` | 同上（`unsigned long` / `unsigned long long`） | `llvm.ctlz.iN` → `trunc` 到 `i32` | 返回 `int` |
| `__builtin_ctzl/ctzll` | 同 clz 家族 | `llvm.cttz.iN` → `trunc` | |
| `__builtin_popcount/l/ll` | 同 | `llvm.ctpop.iN` → `trunc` | **单参数** |
| `__builtin_ffs/l/ll` | `extern int __builtin_ffs(int)` | **无**：`cttz` + `add` + `icmp` + `select` 展开 | 返回 `int` |
| `__builtin_parity/l/ll` | 同 | **无**：`ctpop` + `and 1` 展开 | |
| `__builtin_clrsb/l/ll` | 同 | **无**：`icmp slt` + `xor -1` + `select` + `ctlz(i1 false)` + `sub 1` 展开 | 见下 |
| `__builtin_add/sub/mul_overflow` | **每次调用即时合成重载**（见 §4） | `llvm.{s,u}{add,sub,mul}.with.overflow.iN` | 见 §4 |

要点：

1. `clz/ctz/popcount` 家族**返回类型与操作数类型无关**（clang 一律返回 `i32`，宽操作数先算出
   `iN` 再 `trunc iN to i32`）——所以它们可以写成**固定原型**，不需要重载。
2. **`ctlz`/`cttz` 有两个参数**：`llvm.ctlz.iN(iN, i1 immarg)`，第二个是 `is_zero_undef`。
   **取值随内建而变**（实测 clang 23.1.2）：

   | 内建 | 第二参数 | 含义 |
   |---|---|---|
   | `__builtin_clz` / `__builtin_ctz`（及 `l`/`ll`） | `i1 true` | 参数为 0 是 UB |
   | `__builtin_clrsb` | `i1 false` | 参数为 0 **有定义**（`clrsb(0)` = 位宽-1） |

   实现时必须按内建选值，不能一律 `true`。
3. **`ffs`/`parity`/`clrsb` 没有对应的 LLVM 内建**，clang 是**多指令展开**的：

   ```llvm
   ; __builtin_ffs(int x)
   %4 = call i32 @llvm.cttz.i32(i32 %3, i1 true)
   %5 = add i32 %4, 1
   %6 = icmp eq i32 %3, 0
   %7 = select i1 %6, i32 0, i32 %5      ; x==0 时结果 0

   ; __builtin_parity(int x)
   %4 = call i32 @llvm.ctpop.i32(i32 %3)
   %5 = and i32 %4, 1

   ; __builtin_clrsb(int x)
   %4 = icmp slt i32 %3, 0
   %5 = xor i32 %3, -1
   %6 = select i1 %4, i32 %5, i32 %3    ; x<0 ? ~x : x
   %7 = call i32 @llvm.ctlz.i32(i32 %6, i1 false)
   %8 = sub nuw i32 %7, 1
   ```

   因此这一类**不能只做「名字映射」**，需要在 IR 层构造一小段指令序列——对「自行发射汇编」
   而言这反而是好事（展开后的指令语义自足，不依赖 LLVM 内建的知识）。
   **建议把 `ffs`/`parity`/`clrsb` 放在第 3 步之后**，第 2 步先做纯内建映射的
   `clz/ctz/popcount` 家族。
4. **窄类型的计数内建会先提升**：clang 对 `__builtin_popcount(signed char x)` 生成
   `sext i8 %3 to i32` 后调用 `llvm.ctpop.i32`——即实参按 C 的默认提升到 `int` 再算。
   所以这家族的固定原型**用 `int`/`unsigned int` 形参是正确的**，不需要即时重载。

### 2.2 B 类：保持现状（只能由编译器实现）

判据是「不能用一条固定的 C 函数原型表达」——**或者**结果不必在运行期产生、实参在求值前不能
被折叠。逐族如下（行数即 `builtin_defs[]` 里的行数，共 56 行）：

| 族 | 行数 | 为什么不能声明化 |
|---|---|---|
| `__c11_atomic_*`（13）＋ `__atomic_*` 泛型拼法（4） | 17 | 携带内存序、需要左值对象、CAS 需要两个内存序；实参类型随对象而变 |
| `__builtin_{add,sub,mul}_overflow` | 3 | 操作数必须按**自身宽度**到达 IR（内建名由宽度决定）；固定原型会把 `short` 提升成 `int`，算出的宽度就错了。已按 §4 每次调用**即时合成重载** |
| `__builtin_alloca` / `_with_align` | 2 | 不是普通调用：栈分配语义、结果生存期到函数返回、对齐参数是**位数** |
| `__builtin_constant_p` | 1 | 语义要求**看到未折叠的实参**；一旦走 `ND_FUNCALL`，实参会先被折叠，判定恒为真 —— 静默错误 |
| `__builtin_types_compatible_p` / `__builtin_offsetof` | 2 | 实参是**类型名**／成员指示符，不是表达式 |
| `__builtin_assume_aligned` | 1 | 原型是可变参数（`(ptr, align, ...)`）；返回值还随实参类型（gcc 返回同一类型，`void *` 会把后续表达式的指针运算变成逐字节） |
| `__builtin_va_arg` / `_va_start` / `_va_end` / `_va_copy` | 4 | `va_arg` 的第二个实参是**类型名**；`va_start` 需要「最后一个具名参数」与可变参数函数检查；四者的操作数都是 va_list **对象的地址**（数组 va_list 要退化、结构体要取址），声明式调用会把结构体按值传 |
| `__sync_lock_release` | 1 | 实参类型随对象（要按对象宽度存零） |
| 常量产出族：`huge_val{,f,l}`、`inf{,f,l}`、`nan{f,,l}`、`nans{f,,l}` | 12 | 值必须在**编译期**得到（`INFINITY`/`NAN`/`HUGE_VAL` 都是常量表达式），`nan`/`nans` 的实参还必须是**字符串字面量** —— 固定原型既表达不了「必须折成常量」，也表达不了「必须是字面量」 |
| 比较族：`isgreater`、`isgreaterequal`、`isless`、`islessequal`、`islessgreater`、`isunordered` | 6 | 操作数保持**调用点写出的类型**：固定原型会把 `float` 提升为 `double`（多一条 `fpext`），把 `long double` 降为 `double`（**丢精度，比较结果可能变**） |
| 分类族：`isnan`、`isinf`、`isinf_sign`、`isfinite`、`isnormal`、`signbit`、`fpclassify` | 7 | 同上（类型泛化）；`fpclassify` 另有六个实参 |

因此判据补一条：**除「能用固定原型表达」之外，还要求结果可以是运行期的值、实参在求值前不被
折叠**。B 类保留 `parse_builtin_fn`，`primary()` 里按「先查 A 类表 → 否则查 B 类表」的顺序分派。

本轮把**能声明化却还留在 B 类的 6 行**搬走了：`__builtin_unreachable`、`__sync_synchronize`、
`__builtin_memcpy`、`__builtin_memmove`、`__builtin_memset`、`__builtin_memcmp`（见 §5.5）。

---

## 3. 设计

### 3.1 新增元数据：`builtin_fn[]` 加一列「类别」

现表 `{name, id, kind}` 扩为 `{name, id, kind, cls}`，`cls ∈ {BCLASS_DECL, BCLASS_SPECIAL}`。
好处：`__has_builtin` 继续与表**同源**，无需第二张名单（这是现状最有价值的性质，必须保持）。

`BUILTIN_*` 枚举需要**从 `parser.c` 移到 `cxx.h`**，因为 `opt_ast.c` 与 `irgen.c` 都要按 id 分派。

### 3.2 解析期：隐式声明

**接入点**：`parser.c:2064` 的 `is_builtin_fn` 判断处。改造后：

```
if (tok->kind == TK_IDENT) {
    int kind = is_builtin_fn(tok->id);
    if (kind && builtin_class(kind) == BCLASS_SPECIAL) {
        Node *node = parse_special_builtin(rest, tok, kind);   // 现状逻辑
        if (node) return node;
    }
    if (kind) ensure_builtin_decl(tok->id, kind);   // A 类：隐式声明
    ... 现有 find_ident 查找路径不变（会找到刚注入的符号）...
}
```

**`ensure_builtin_decl(id, kind)`** 的语义等价于在文件作用域插入：

```c
extern <ret> __builtin_xxx(<params>);
```

实现要点（API 已核实）：

- 用 `push_namespace(id, SYM_FUNC, fty, tok)` 把符号登记进**当前作用域**（`parser.c:301`）。
  这是现成的登记 API；`primary()` 在表达式位置，作用域可能是块作用域，因此同一 id 会在多个
  作用域各注入一份——**无害**（同名同型），但更干净的做法是在 `parse()` 开头对**所有 A 类内建**
  一次性注入文件作用域，避免顺序依赖。
- 建 `Sym`：`new_var(id, fty)` → `is_function = true`，**不要**放进 `globals`（原因见 §3.4）。
- 类型用 `func_type(ret)` 再挂 `params`；固定原型的内建直接由一张签名表生成。
- 注入后**不再需要任何特殊处理**：`find_ident` 找到它 → `new_var_node` → `postfix()` 的
  `TY_FUNC → pointer_to` 衰减 → `fncall()` → 实参经 `check_asop` + `new_imcast` 正常转换
  → 产出普通 `ND_FUNCALL`。

**用户可覆盖**：因为只是「若未声明则注入」，用户自己写 `extern ... __builtin_bswap32(...)`
（或任何同名声明）就用自己的。这比现状（内建名字永远优先）更符合 C 的直觉。

**注意**：A 类内建虽然变成了普通符号，但**不能取址**：`&__builtin_bswap32`、把内建名存进
函数指针都会被拒绝（`builtin functions must be directly called`，与 clang 同句）。最初的实现
允许它，代价是模块里出现了一个没有任何声明引入的符号，LLVM 直接拒绝整个模块
（`use of undefined value '@__builtin_bswap32'`）—— 见 §5.5。

### 3.3 折叠期：`opt_ast.c` 的 `ND_FUNCALL` 统一折叠

#### 先修一个既有缺陷：实参折叠被丢弃

`opt_ast.c:435-437` 现状是：

```c
case ND_FUNCALL:
    for (Node *a = node->args; a; a = a->next) fold_node(a);   // 返回值未写回
    return node;
```

`fold_node` 的约定是**返回替换节点**，调用方必须写回；对照 `ND_COMMA`（`opt_ast.c:369-370`）
就正确地做了 `node->lhs = fold_node(node->lhs);`。所以这一处的实参折叠**从未生效**：
`f(2+3)` 里的 `2+3` 不会被折叠，一直原样留给 IR 层去算。

后果与影响面：

- 功能上无害（IR 层仍会算出正确结果），所以长期未被发现；
- 但它让 `fold_ast` 对**所有函数调用实参**失效，是接内建折叠前必须修的前置缺陷；
- 修法（注意 `next` 由 `fold_node` 内部驱动，不能像二元节点那样直接赋值）：

```c
case ND_FUNCALL: {
    Node **p = &node->args;
    while (*p) {
        Node *folded = fold_node(*p);
        if (folded && folded != *p) {
            folded->next = (*p)->next;   // 保持链表
            *p = folded;
        }
        p = &(*p)->next;
    }
    ... 内建折叠（下）...
}
```

**验证方式**：`int f(int); int g(void){ return f(2+3); }` 的 IR 里实参应为 `i32 5` 而不是一条 `add`。
这条应加进 `test/conformance.sh`（或 `test/ir.sh`）。

#### 内建统一折叠

修好写回之后，同处接内建分派，完整形态：

```c
case ND_FUNCALL: {
    // 1) 自底向上折叠全部实参，并把替换节点写回链表
    Node **p = &node->args;
    while (*p) {
        Node *folded = fold_node(*p);
        if (folded && folded != *p) {
            folded->next = (*p)->next;
            *p = folded;
        }
        p = &(*p)->next;
    }
    // 2) 被调者是内建吗？（复用 is_builtin_fn，不新增 Sym 字段）
    int kind = builtin_kind_of(node->func);
    if (!kind) return node;
    // 3) 按 id 分派到该内建的折叠例程；不可折叠则保留原调用
    return fold_builtin_call(kind, node) ?: node;
}
```

**如何从 `node->func` 取 id**（设计要求：直接复用 `is_builtin_fn`）：

`node->func` 的实际形状有两种可能，都需要处理：
- 普通调用：`ND_IMCAST(ND_VAR sym, PTR(TY_FUNC))`（`postfix()` 的衰减产生）
- 若将来直接构造：`ND_VAR sym`

所以先剥掉 `ND_IMCAST`/`ND_LVTOR`，拿到 `ND_VAR` 的 `sym`，再：
`builtin_kind_of()` = 若 `sym->is_function` 则 `is_builtin_fn(sym->id)`，否则 0。

**复用 `is_builtin_fn` 的价值**：不新增任何 per-`Sym` 的 id 字段，也不需要维护第二张表；
`__has_builtin`、解析期分派、折叠期分派、IR 期分派**共用同一个真值来源**。

**每个内建一个折叠例程**，签名统一为：

```c
// 返回折叠后的常量节点；无法折叠（实参非常量）返回 NULL
static Node *fold_builtin_<name>(Node *call, Node *args[]);
```

`fold_bswap`（`opt_ast.c:45-59`）就是现成模板：守卫常量 → 计算 → `folded_int(..., call->ty, call)`。
`clz/ctz/popcount` 用 `int128_bit_width`/位运算在 `Int128` 域算即可。

**分派方式**：一个 `static Node *(*const fold_builtin[])(...)` 表按 `BUILTIN_*` id 索引，
或一个 `switch`。建议 `switch`——20 个分支规模不需要间接跳转，且 `-Werror` 下的
`-Wswitch` 能保证新增内建必须补折叠分支。

### 3.4 IR 期：`irgen.c` 的 `ND_FUNCALL` 按 id 捕获

在 `irgen.c:663` 的 `ND_FUNCALL` 里，**发射前**判断被调者是否内建：

```c
case ND_FUNCALL: {
    int kind = builtin_kind_of(node->func);
    if (kind) return gen_builtin_call(node, kind);   // 映射到 LLVM 内建
    ... 现有通用路径不变 ...
}
```

`gen_builtin_call` 负责：
1. 由 `kind` + 实参类型决定 LLVM 内建名（如 `llvm.bswap.i32`、`llvm.ctlz.i32`）。
2. 用 `intern(name, strlen(name))` + `register_asm_name(id, name)` 登记名字
   （`dumpir.c:111-123`）。**必须用 `intern` 取真实 id**：`print_ident` 里有
   `assert(id >> IBits < itbl[id & IMask].nstr)`，伪造 id 会断言失败。
3. 构造 `Ref` 与 `new_ins(IR_CALL, ...)`。被调者 `Ref` 的 `ty` 必须是 **`TY_FUNC` 或
   `PTR(TY_FUNC)`**（`dumpir.c:465-466` 只剥一层）。

**关键：内建符号不能进 `md->fns`。** 否则 `dump_module` 会为它发 `declare`，而那条声明由 cxx 的
`Type` 生成，**与 LLVM 内建的真实签名未必一致**（溢出内建尤其严重：LLVM 是
`declare { i32, i1 } @llvm.sadd.with.overflow.i32(i32, i32)`）。让 LLVM 走「首次使用自动声明」，
按**调用点**推导重载，天然一致。

实现方式：A 类隐式符号**不加入 `globals`**（于是不进 `md->fns`），只在作用域里可见；
`irgen` 用 `sym->id` 生成 `RGlb`，`print_sym_name` 查已登记的 `asm_names` 表得到内建名。
若要更显式，可给 `Sym` 加一个 `int builtin;` 字段并让 `dump_fn` 跳过，但**不必要**。

**`print_sym_name` 的引号规则**（`dumpir.c:134-144`）：已登记且**含 `.`** 者**裸打**
（`llvm.bswap.i32` —— 加引号会让引号成为标识符的一部分，非法）；已登记但不含 `.` 者加引号
（`__asm__` 名字）；未登记者落到 `print_ident`。内建名含 `.`，所以自然裸打。

**IR 要为将来自行发射汇编保留的信息**：`IR_CALL` 已携带
(a) 目标类型 `dst.ty`、(b) 被调者符号名、(c) **每个实参自己的类型** `args[i].ty`。
对 `bswap`/`clz`/`popcount` 这类纯函数，这三项足以自行发射（宽度由 `args[1].ty` 得到）。
**不足以**自行发射的是「返回值是多值」的溢出内建——见 §4。

---

## 4. 溢出内建：IR 契约与额外要求

### 4.1 clang 的权威契约（已实测取得）

`int f(int a, int b, int *r){ return __builtin_add_overflow(a,b,r); }`：

```llvm
%10 = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %7, i32 %8)
%11 = extractvalue { i32, i1 } %10, 1
%12 = extractvalue { i32, i1 } %10, 0
store i32 %12, ptr %9, align 4
%13 = zext i1 %11 to i32
ret i32 %13
declare { i32, i1 } @llvm.sadd.with.overflow.i32(i32, i32)
```

实测确认的四点：

1. **返回类型是结构 `{ iN, i1 }`**，不是标量——这是与 bswap/clz 家族的**本质差别**。
2. 有符号用 `llvm.sadd`，无符号用 **`llvm.uadd`**（`__builtin_add_overflow` 的语义随操作数
   符号性变化，由实参类型决定）。
3. **宽度取实参宽度**：`signed char` → `@llvm.sadd.with.overflow.i8`；
   `__int128` → `i128`。**不做整型提升**，所以实参类型绝不能被隐式转换改变。
4. 结果写入 `*r` 用 `store`，函数返回 `zext i1 → i32`。

### 4.2 为什么必须「即时重载」

因为 (3)：若把原型写成 `extern int __builtin_add_overflow(int, int, int*)`，`fncall` 会给
`short` 实参插入 `new_imcast` 到 `int`，宽度信息就丢了，选不出 `i8`/`i16` 内建。

同时也不能用「无原型变参」规避：变参分支会做**默认实参提升**（`parser.c:2126-2142`），
`short` 同样会被提升成 `int`。所以**两条捷径都不行，必须即时重载**。

### 4.3 即时重载的做法

按 clang 模式，**每次调用按实参类型合成一个专属声明**：

1. 从实参取得 `T1`（左）、`T2`（右）、`T3`（结果指针，应为 `PTR(T1)`）。
2. 合成**唯一** id：`mangle("__builtin_add_overflow", T1, T2, T3)`——用类型对做键，
   相同类型对复用同一个符号（`find_ident` 命中即不再注入）。
3. `fty = func_type(T.ty_int)`，`params = [T1, T2, T3]`（**非变参**，于是 `fncall` 不会提升，
   且按 `T1` 精确装换）。
4. `push_namespace(mangled_id, SYM_FUNC, fty, tok)` 注入；也不需要进 `globals`。
5. 折叠/IR 期从**实参类型**（而非声明）取宽度与符号性——两者此时一致。

**借用点**：`irgen` 里已有 `GEN` 按实参类型分派的成熟做法（原子内建的 `armw_op_of`、
浮点/指针特判，`parser.c:1642-1691`），可参考其结构。

### 4.4 实施记录（已完成）

`{ iN, i1 }` 是多值返回，而 `IR_CALL` 的 `dst` 是单个 `Ref`。设计时列了两个方案，实际采用的是
**A 的简化形式**——与 A 的关键差别是**不合成具名结构体**：

1. **聚合类型是字面量，不进类型表。** `extractvalue` 只接受字面聚合，LLVM 对具名结构体直接报
   `invalid indices for extractvalue`。所以合成的 `{ iN, i1 }` **不注册 uid**（`insert_ty` 会
   生成 `%struct.…` 名字，反而不可用），而 `print_type` 对未命名的聚合按 `{ i32, i1 }` 内联输出。
   这比原方案 A 更省：不需要给 `Type` 加结构合成机制，也不必让 `dump_module` 跳过内部类型。
2. **`print_type` 的新分支是必要的，不是便利。** 它原本对结构体无条件打印 `%uid`，未命名时得到
   非法的 `%0`；现在未命名即内联输出，这条同时把「内部聚合」表达清楚了。
3. **`IR_EXTRACTVAL` 原本把聚合类型硬编码成 `{ T, i1 }`**（cmpxchg 的 `dst.ty` 携带的是**比较
   类型**，是标量，聚合形状由打印器推出）。溢出内建的 `dst.ty` **本身就是聚合**，所以打印器
   改为两种情况都支持：操作数是聚合就照它打印，否则按 `{ T, i1 }` 补出来。
4. **宽度与符号性只能从实参取，且不能让前端做整型提升。** 内建的 LLVM 名是
   `llvm.sadd.with.overflow.i16` 这类**宽度 + 符号性**的组合，而符号性又随实参类型变化
   （`int` → `sadd`，`unsigned` → `uadd`）。因此这类内建**没有原型**：写成原型会让
   `short` 实参被 `new_imcast` 提升成 `int`，宽度信息就丢了。它们归入 `BCLASS_SPECIAL`，
   由 `parse_builtin_fn()` 逐实参读取、保持各自类型，内建名在 `irgen` 按实参现场拼装。
5. **内建识别不能依赖作用域查找。** 上述「没有原型」意味着不存在可查的内建符号。改为在
   内建类型上打标（`Type.is_builtin`，`Type.id` 存 kind），`builtin_kind_of()` 见到该标记
   即认定为该内建。用户若自行声明同名函数，走的是普通查找，不受影响。
6. **`Type` 加字段会波及其位置初始化处。** `type.c` 的 `TYPE` 宏原本是位置初始化，新增
   `is_builtin` 后静默错位（把 `size` 当成了 `kind`）。已改为**指定初始化**，与各
   `target.c` 的写法一致；`insert_ty()` 由 `static` 改为导出，因为 irgen 侧也要注册类型。

结果：`<stdckdint.h>` 端到端可用（`ckd_add`/`ckd_mul` 等），运行结果与 gcc/clang 逐字一致。

### 4.5 溢出内建不做常量折叠（决定）

这三个内建**刻意不进 `fold_builtin_call`**，与 `bswap`/`clz`/`ffs` 家族不同：

它们的语义不只是产生一个值，还**写穿第三个实参**（`*r = 结果`）。所以折叠不能像纯值内建那样
「算出一个常量替换整个调用」——那会丢掉写入；要正确折叠就得改写表达式（拆成「写 `*r`」+
「常量标志」两条），把一个求值问题变成改写问题。

代价仅是常量场合走运行期：`int r; int f = __builtin_add_overflow(1, 2, &r);` 会真的调用内建。
后端（LLVM）在优化时对常量实参会自行折叠掉整个 `llvm.*.with.overflow` 调用，所以实际代码质量
不受影响；受影响的只有 `const_expr` 这类**必须在前端求值**的场合，例如
`_Static_assert(__builtin_add_overflow(1, 2, &r) == 0)` 或全局初始化器里的同类写法。

判定：这类用法罕见，而改写式折叠引入的复杂度与出错面（写入顺序、`*r` 的副作用、
`const_expr` 上下文里的左值限制）不成比例。**保持不折叠。**

---

## 5. 实施步骤（建议顺序）

> **进度**：第 1 步（`bswap` 样板）**已完成**，见 §5.1 的实施记录。
> 第 4 步（`eval2` 支持内建调用）经实测是第 1 步的**必要前置**，已一并完成。
> 第 2 步（`clz/ctz/popcount`）**已完成**；`ffs`/`parity`/`clrsb` 家族**已完成**（它们没有
> LLVM 内建，按指令序列展开，见 §5.4）；第 3 步（溢出内建）**已完成**，见 §4.4 的实施记录。
>
> 内建表已改为**指定初始化** `[BUILTIN_*] = {…}`：每行自带 kind，表的书写顺序不再需要与
> 枚举同步，`kind - 1` 一类的换算全部消失。原先的断言只校验**总数**，抓不到「顺序错位但
> 长度不变」这类错误——这正是实施 `ffs` 家族时踩到的坑（见 §5.4 第 1 点）。

### 5.4 `ffs` / `parity` / `clrsb` 家族实施记录（已完成）

这 9 个内建没有 LLVM 内建可用，按指令序列展开；表里仍然描述原型，因为原型决定了实参到达
`irgen` 时的宽度与符号性，而这正是展开所需的：

| 内建 | 展开 |
| --- | --- |
| `ffs{,l,ll}` | `cttz(x, true) + 1`，`x == 0` 时取 0（`select`） |
| `parity{,l,ll}` | `ctpop(x) & 1` |
| `clrsb{,l,ll}` | 按符号性选 `~x`/`x`（`select`），再 `ctlz(·, false) - 1` |

`clz/ctz` 的 `is_zero_undef` 是 `true`，`clrsb` 的 `ctlz` 是 `false`——因为
`clrsb(0)` 与 `clrsb(-1)` 都有定义，`ctlz` 必须在 0 处有定义。

实施中抓到两个**真实的**缺陷，都不是靠"能编译"能发现的：

1. **表与枚举的错位只校验总数是抓不到的。** 新行插在了 `va_*` 之前而枚举在之后，长度不变、
   断言通过，但 `kind` 与行不再对应——`is_builtin_fn()` 对 `__builtin_ffs` 返回 0（未找到），
   报出的是"隐式声明"。已改为指定初始化，从结构上杜绝。
2. **`clrsb` 的折叠与展开一开始都错，且错法不同。**
   - 折叠：`clrsb(v) = width - bit_width(v, SIGNED)` 依赖 `int128_bit_width` 的有符号语义，
     而它对负数返回 `bit_width(|v|) + 1`（-1 给 2），于是 `clrsb(-1)` 得 30 而非 31。
     最终按定义直接算：符号位广播后与 `v` 相异，取最高相异位。
   - 展开：`gen_scan_call` 的主分支条件写成了 `is_ffs || !is_parity`，对 `clrsb` 也成立，
     于是对着**未经 select 的原值**多发了一次 `ctlz`。两种错法互相掩盖，逐点对照
     gcc/clang 的全定义域取值才定位出来。

`i1` 立即数打印：`INT(x)` 编码的是 i32 类型，用于 `select` 的两个分支时须与另一分支同宽，
否则 LLVM 报 `both values to select must have same type`。已加 `imm_of(v, ty)` 统一处理。

---

### 5.1 第 1 步实施记录（已完成）

改动落在 `cxx.h`、`parser.c`、`opt_ast.c`、`irgen.c`、`type.c`、`dumpast.c`，
`ND_BSWAP` 节点及其全部分支已删除；`new_bswap` 由 `builtin_proto` + `declare_builtins` 取代。
conformance 由 47 增至 **51 passed**（新增 4 项内建回归）。

实施中发现并解决的四点，**设计时未预见，后续步骤会同样遇到**：

1. **`Type.name` 必须设置**。`fncall()` 的元数检查与 `check_decl_compatile()` 都通过
   `ty->name` 报错（`tok_text(ty->name)`），而 `func_type()` 不设该字段。用一个
   **拼写可达**的真 token 即可——见下面第 5 点。
2. **`params` 与 `nparam` 必须手动设**。`func_type()` 只填返回类型；`fncall()` 靠
   `params` 走形参链、靠 `nparam` 报元数错误，漏设会误报 "too many arguments"。
3. **内建 id 的缓存哨兵不能用 id 本身**。原实现以 `if (!builtin_fn[0].id)` 判断
   "是否已 intern"；id 是 `hash | (index << IBits)`，**0 是合法 id**，于是
   `__builtin_alloca` 一旦落到 id 0，填充永不执行、`declare_builtins` 全程跳过。
   已改为独立的 `builtin_ids_ready` 标志，并让 `declare_builtins` 主动填充。
4. **第 4 步是前置而非可选**。全局初始化器经 `const_expr` → `eval2` 求值，**不经过
   `fold_ast`**，所以 `int a = __builtin_bswap32(0x11223344);` 曾报
   "not a compile-time constant"。另外 `eval2` 调 `fold_builtin_call` 前必须先
   **折叠实参并把返回值写回链表**——实参到达时通常包着 `ND_IMCAST`
   （形参类型触发的隐式转换），折叠器只认裸 `ND_NUM`；而 `fold_node()` 是
   "返回替换节点"的约定，与 §3.3 记录的实参折叠缺陷是同一个坑。

5. **注入应当在「使用点」而不是 `parse()` 开头**。`primary()` 里那个标识符 token
   本身就是完整可用的信息：它拼写正确、`file`/`loc` 指向源码中该次出现。直接
   `ty->name = tok` 即可，既不必合成 token，诊断也能落在调用处：

   ```
   a.c:1:40: error: too few arguments to function ‘__builtin_bswap32’; expected 1
   b.c:1:44: error: too many arguments to function ‘__builtin_bswap32’; expected 1
   ```

   （最初在 `parse()` 开头批量注入、`loc = 0` 合成 token，诊断会指到文件开头。）

6. **查找要走父作用域，注入要落在文件作用域**。`declare_builtin()` 里两处作用域细节：

   - `find_ident(tok, **true**, false)`：`search_par` 必须为 true。用 false 时从函数内部
     找不到文件作用域那份声明，于是**每遇到一次就在当前块再插一份**。
   - `push_namespace(**file_scope**, ...)`：内建是文件作用域的实体，不是块内对象。写进
     当前块会让每个块各有一份同名声明。

   为此 `push_namespace()` 增加了目标作用域参数（原先隐式写 `scope`，共 10 个调用点已显式
   传入），而不是临时切换全局 `scope` 指针。

   这两点合起来的效果：嵌套块里用 4 次只产生 4 条调用、**0 条 `declare`**
   （LLVM 按调用点自动声明），跨函数使用也共享同一份声明。

7. **「是不是内建」必须看 Sym，不能看名字**。`builtin_kind_of()` 起初写成
   `is_builtin_fn(sym->id)`，于是**用户声明同名函数时被误判为内建**：
   用户声明后 `declare_builtin()` 不会注入（名字已被占用），那个 `Sym` 是用户的，
   但名字仍在表里——调用被降到 `llvm.bswap`，用户函数白定义。

   ```c
   unsigned __builtin_bswap32(unsigned);              /* 用户声明 */
   int f(unsigned x) { return __builtin_bswap32(x); } /* 曾被误编译为 llvm.bswap */
   ```

   修法是给 `Sym` 加 `bool is_builtin`，**只在注入时置位**，判据改为
   `sym->is_function && sym->is_builtin`。这与 §3.3「复用 `is_builtin_fn`，不新增
   per-Sym 字段」的初始设想相反——那里的结论是错的：名字不足以区分来源。

8. **同名定义必须报错，不能静默丢弃**。注入的符号代表编译器提供的定义，因此
   `is_defined = true`；文件作用域的函数定义路径据此报
   `definition of builtin function ‘__builtin_bswap32’`，**与 clang 的诊断逐字一致**。
   否则用户定义的函数体被静默忽略（符号不进 `md->fns`，不发射），而调用仍走内建——
   既不报错也不用它，是最糟的一种结果。

   | 场景 | cxx | clang | gcc |
   |---|---|---|---|
   | 同名变量（文件作用域） | 接受（读它可用；调用它报 not a function） | 拒绝 | 接受 |
   | 同名变量（块作用域） | 接受 | 接受 | 接受 |
   | 同名函数声明（不定义） | 接受，按普通函数调用 | 接受 | 接受 |
   | 同名函数定义 | 报 `definition of builtin function` | 同 | 接受 |
   | 同名函数指针变量 | 接受，调用走函数指针 | 拒绝 | 接受 |
   | 同名 typedef / 枚举常量 / 结构体标签 | 接受 | 接受 | 接受 |

   cxx 在**每一行都与 gcc 一致**；clang 因 `__` 前缀标识符为实现保留而一律拒绝
   （6.4.2.1），属未定义行为，不作为对齐目标。

### 5.2 元数据集中：一行表项 = 一个内建

第 1 步之后，一个内建的信息原本散在四处（parser 的本地表、`builtin_proto` 的 switch、
irgen 的 switch、以及各处按 kind 的硬编码判断）。现已收敛为**一张表**
`builtin_defs[]`（定义在 `parser.c`，声明在 `cxx.h`）：

```c
typedef struct BuiltinDef {
    char *name;       // C 拼写
    int kind;         // BUILTIN_* id
    BuiltinClass cls;
    char *intrinsic;  // "llvm.bswap.i%d"，或 NULL（非纯内建）
    int ret;          // BuiltinTargetType 选择子
    int args;
    bool uniform;     // 各形参同型
    uint32_t nargs;
    Token *tok;
    uint32_t id;      // interned 名，首次使用时填
} BuiltinDef;
```

**加一个「纯内建映射」型内建 = 加一行**：parser 由 `ret`/`args` 合成声明，折叠器按
`kind` 分派，irgen 用 `intrinsic` 格式化名字。`__builtin_bswap16` 那一行即：

```c
{"__builtin_bswap16", BUILTIN_BSWAP16, BCLASS_DECL, "llvm.bswap.i%d", BT_USHORT, BT_USHORT, true, 1, NULL, 0},
```

配套的三处结构调整：

1. **查找只剩一个循环**。原先 `builtin_def`（按 kind 扫）与 `is_builtin_fn`（按 id 扫）
   是两段同构代码；现在按 id 的扫描只有 `builtin_find()` 一处，`is_builtin_fn` 与
   `builtin_class` 都是它的投影。
2. **分发与发射分离**。`gen_builtin_call()` 只负责「识别 + 分发」；实际发射在
   `gen_intrinsic_call()`，它服务所有表里带 `intrinsic` 的内建；表里没有 `intrinsic`
   的落到 `fatal("no IR lowering for builtin ...")`。分发函数因此不随内建数量增长。
3. **表定义在 `.c` 而非头文件**。写成头文件里的定义会让每个 TU 各得一份，链接期
   多重定义。

**踩到的坑**：搬表时把 `__builtin_bswap16` 的 `ret`/`args` 误写成 `BT_UINT`，宽度按
结果类型算成了 `i32`，`bswap16(0x1234)` 返回 **0**。conformance 的
`__builtin_bswap16/32/64` 用例当场抓住。教训：**宽度取自操作数类型，`ret` 与 `args`
必须一致**；且 `BuiltinTargetType` 需要 `BT_USHORT` 这类选择子——最初只列了
`BT_INT/BT_UINT/BT_LONG/BT_ULONG/BT_LLONG/BT_ULLONG`，漏了短整型。

### 5.5 A 类声明化收尾实施记录（已完成）

表里最后 6 行能声明化却还在 B 类的内建搬完了：`__builtin_unreachable`、`__sync_synchronize`、
`__builtin_memcpy`、`__builtin_memmove`、`__builtin_memset`、`__builtin_memcmp`。census：
**86 行 = 30 DECL + 56 SPECIAL**。

改动：

1. **表新增「具名形参表」选择子**（`BT_MEMCPY_ARGS`/`BT_MEMSET_ARGS`/`BT_MEMCMP_ARGS`）：memcpy
   一族的形参类型不同（`void *`、`const void *`、`size_t`），而原来一行只有 `args`＋`uniform`
   两列。选择子而不是给 `BuiltinDef` 加三个字段，是为了保住「一行表项 = 一个内建」，也让其余
   83 行的位置初始化原样不动。
2. **`__builtin_unreachable`、`__sync_synchronize` 是 `void (void)`**，irgen 按 id 降级
   （前者仍是 void 表达式的空操作；后者发 `fence seq_cst`）。
3. **memcpy 一族由 irgen 发库调用**（`gen_libcall()`）：`intrinsic` 字段里不是 `llvm.*` 的名字
   就是它代表的库函数；`declare_builtin()` 顺带把该库函数声明进模块 —— 没有声明，LLVM 会拒绝
   那个引用。`parse_mem_builtin()`（44 行）连同它的 4 个 case 一起删除。

实施中抓到两个**既有缺陷**（都不是这次改动引入的，但都被它暴露）：

- **取内建地址会产出 LLVM 拒绝的模块**。A 类内建变成符号后 `&__builtin_bswap32` 一直"能编译"，
  但模块里引用了一个没有任何声明引入的符号，LLVM 报 `use of undefined value`。clang 的答复是
  `builtin functions must be directly called`，cxx 现在同句拒绝（§3.2 的"获得取址能力"一句
  据此更正）。
- **任何 `void *` 转换都误报 `discards qualifiers`**（`type.c` 的 `is_assignable()`：`void` 对
  与对象指针"按构造不兼容"，`!agree` 一项因而恒真）。`char *` → `void *` 也报，`memcpy(dst,
  src, n)` 全都报 —— 与两家都不符。现在只有**目标真的丢掉限定符**时才报，与 clang 逐点一致：

  | 转换 | 修前 cxx | 修后 cxx | clang |
  |---|---|---|---|
  | `char *` → `void *` | 报 | 不报 | 不报 |
  | `const char *` → `const void *` | 报 | 不报 | 不报 |
  | `const char *` → `void *` | 报 | 报 | 报 |
  | `volatile char *` → `void *` | 报 | 报 | 报 |

验证：`make test` exit 0（conformance **315 / 0 gap**、c2y 101 / 0）、**bootstrap 逐字节相同**
（cxx2 = cxx3 = cxx4，21/21 目标文件）、`tcctests` 106 ok / 0 failed、跨目标 arm64 / rv64 / rv32
各 51 / 0。新增断言：`test/ir.sh` 4 条（库调用与声明、memcmp 的 int、fence）、`conformance.sh`
7 条（arity、const 源可编译可运行、四个限定符用例、取址被拒）。

### 5.3 表结构对后续步骤的适配度（待第 2 步验证）

`__builtin_bswap` 家族已是完整表驱动；`clz/ctz/popcount`（第 2 步）尚未加入。加入时
除一行表项外还需要：

- **`ctlz`/`cttz` 有两个参数**，第二个是 `i1 immarg`（`is_zero_undef`）：`clz`/`ctz`
  取 `true`，`clrsb` 取 `false`。`intrinsic` 里的 `%d` 只够表达宽度，第二参数的取值
  需要新的表列，或让 `gen_intrinsic_call` 支持额外的立即数实参。
- **宽操作数先算 `iN` 再 `trunc i32`**（返回 `int`），窄实参按 C 默认提升到 `int`。
- `ffs`/`parity`/`clrsb` **无对应 LLVM 内建**，必须多指令展开，属独立发射函数
  （表里 `intrinsic = NULL` 的那些）。

即：当前表结构对「一个内建 = 一条内建调用」够用，对多指令展开与额外立即数实参还需
再加一列——**不宜现在抽象，等第 2 步落地后按实际需要定形**。

### 第 1 步：只做 `bswap` 样板，打通全链路
1. `cxx.h`：`BUILTIN_*` 枚举从 `parser.c` 移入；`builtin_fn[]` 加 `cls` 列。
2. `parser.c`：`ensure_builtin_decl()` + `parse()` 开头对 A 类批量注入；`primary():2064` 改为
   「A 类注入 / B 类走 `parse_special_builtin`」。
3. **删掉** `new_bswap`（`parser.c:1530-1548`）与 `ND_BSWAP` 节点及其在
   `cxx.h:514`、`type.c:866`、`opt_ast.c:468`、`irgen.c:404`、`dumpast.c:81/506` 的全部分支。
   —— 注意 `-Werror`：这几个 switch 没有 `default:`，删枚举成员必须同步删分支。
4. `opt_ast.c`：`ND_FUNCALL` 统一折叠 + 按 id 的 `switch`，`fold_bswap` 改为以调用节点为入参。
5. `irgen.c`：`ND_FUNCALL` 按 id 捕获 + `gen_builtin_call()`（把现有 `gen_bswap` 的逻辑收进去）。

### 第 2 步：`clz/ctz/popcount` 家族（纯内建映射）
固定原型，无需重载。要点：
- `ctlz`/`cttz` 的第二参数按内建取 `true`/`false`（见 §2.1 表 2）。
- 宽操作数先算 `iN` 再 `trunc iN to i32`（返回 `int`）。
- 窄实参按 C 默认提升到 `int` 后调用（clang 行为，见 §2.1 第 4 点）。

### 第 2b 步：`ffs`/`parity`/`clrsb` 家族（多指令展开）
没有对应 LLVM 内建，需在 IR 层构造指令序列（见 §2.1 第 3 点的展开形式）。
放在第 2 步之后，因为它需要 `IR_CALL` 之外还会用 `IR_CMP_*`/`IR_SELECT`/`IR_ADD` 等已有指令，
但**多了一个「一个内建调用产出多条 IR 且需要中间临时值」**的结构，与纯映射不同。

### 第 3 步：溢出内建
前置：结构值支持（§4.4）。之后即时重载 + `llvm.{s,u}{add,sub,mul}.with.overflow.iN`。

### 第 4 步（可选，与重构解耦）：`eval2` 支持内建调用
`eval2`（`parser.c:2754-2939`）加 `ND_FUNCALL` 分支：复用折叠期的分派表求值。
这样 `static int a[__builtin_bswap32(1)]`、`case __builtin_clz(1):` 才能用。
**注意**：`is_const_expr` 依赖 `fold_node`，第 1 步做完后
`__builtin_constant_p(__builtin_bswap32(1))` 会自动从 0 变 1 —— **这是行为变化**，
需要在 `conformance.sh` 里加一条正向用例固化。

---

## 6. 风险与注意事项

| 风险 | 说明与对策 |
|---|---|
| 内建名遮蔽 | 现状内建**永远优先**于用户声明；改后变成「用户声明优先」。属行为变化，但更符合 C 直觉，需记录 |
| 取址能力 | A 类内建变成普通符号后可取址、可存函数指针。行为扩展，可接受 |
| 内建符号进 `md->fns` | **必须避免**：否则发出的 `declare` 与 LLVM 内建真实签名可能不符（溢出内建必错）。做法：不加入 `globals` |
| `Ref` 只有 16 字节 | 放不下内建名，所以必须 `intern` + `register_asm_name` 走旁表（`gen_bswap` 已有先例） |
| 伪造 id | `print_ident` 内有 `assert(id >> IBits < itbl[id & IMask].nstr)`，必须用 `intern()` 取真实 id |
| `eval2` 与折叠期不一致 | 两处都要能算同一个内建，否则 `case`/数组界能用而 `static` 初始化不能用（或反之）。共用同一张分派表 |
| 头文件依赖不可靠 | 改 `cxx.h`（枚举/结构）后 `make` 可能误判为最新，产生 `unknown ir op kind N` / `0 passed, 48 failed`。**必须 `make clean && make`** |
| `constant_p` 语义 | 它必须留在 B 类；若误入 A 类，其实参会被先折叠，判定恒为真——静默错误 |

---

## 7. 验证清单

每步完成后按 `doc/cxx-c2y-plan.md` 的约定跑：`make test` + `test-arm64` / `test-rv64` / `test-rv32`，
并在 `test/conformance.sh` 增补回归项。

**与 clang 的 IR 对照**（本设计的关键验证手段）：

- 参考 IR 生成脚本：`tools/builtin-ref-ir.sh`（见 §8），产出 clang 对各内建的权威 IR。
- 差分脚本（待写）：把同一份源码分别交给 clang 与 cxx，抽取两者 IR 中**内建调用那一行**做比较
  （归一化寄存器名与临时编号后逐字对比），例如期望双方都是
  `call i32 @llvm.bswap.i32(i32 ...)` 与 `call i16 @llvm.bswap.i16(i16 ...)`。

**要固定的行为**（建议加入 `test/conformance.sh`）：

1. `unsigned short`/`unsigned int`/`unsigned long` 的 `__builtin_bswapN` 运行结果。
2. `ctlz`/`cttz` 的 **`is_zero_undef` 取值**：反汇编或对 IR 做文本断言，确认
   `__builtin_clz` 生成 `i1 true`、`__builtin_clrsb` 生成 `i1 false`（用错会改变 `clz(0)` 的语义）。
3. `ffs`/`parity`/`clrsb` 的**运行结果**，重点覆盖边界：`ffs(0)==0`、`parity(0)==0`、
   `clrsb(0)==位宽-1`、`clrsb(-1)==位宽-1`。
4. 宽操作数的计数内建返回 `int` 且值正确（如 `__builtin_clzll(1ull)==63`）。
5. 窄实参经默认提升后再算（如 `__builtin_popcount((signed char)-1)`：提升到 `int` 得 32，
   而**不是** `i8` 的 8）——这一条最容易被"直接按实参宽度算"实现错，务必固化。
6. 常量实参在 IR 中**不出现内建调用**（前端已折叠，如 `__builtin_bswap32(0x11223344u)`
   应为 `ret i32 1144201745`）。
7. `__builtin_constant_p(__builtin_bswap32(1))` 的期望值（第 4 步后为 1；此前为 0，属行为变化）。
8. 用户自行声明同名函数时，用户声明生效。
9. 内建结果进入变参位置时仍走默认实参提升
   （clang 参考：`call i16 @llvm.bswap.i16` 后 `zext i16 %4 to i32`）。

---

## 8. 参考 IR 生成脚本

位置：`tools/builtin-ref-ir.sh`（内容见下）。它把每类内建交给 clang 编译成 LLVM IR 并抽取
函数体与 `declare`，用于：
- 固化「内建 → LLVM 内建名/签名」的映射（尤其溢出内建的 `{iN,i1}`）；
- 后期自行发射汇编时，作为**语义与类型信息的参考基准**。

覆盖：`bswap16/32/64`、`clz/ctz/popcount/ffs/parity/clrsb`（含 `l`/`ll` 变体）、
`add/sub/mul_overflow`（含 `signed char` / `unsigned` / `__int128`）、常量折叠程度、
B 类对照（`constant_p` / `types_compatible_p` / `alloca`）、以及内建结果进变参位置的提升行为。

（脚本正文已写入 `tools/builtin-ref-ir.sh`；运行方式 `bash tools/builtin-ref-ir.sh`，
产物在 `/tmp/builtin-ref/*.ll`。）

---

## 9. 与现有文档的关系

- 本文替换 `doc/cxx-c2y-plan.md` §2.6–2.9 里 `ND_BSWAP` 专用的做法：
  `ND_BSWAP` 节点在本文方案下**整个删除**，其折叠能力改由 `ND_FUNCALL` 统一折叠承担，
  其 IR lowering 改由 `gen_builtin_call()` 承担。
- `doc/cxx-c2y-plan.md` 的 B4（`__builtin_*_overflow`）在本文里升级为「A 类 + 即时重载 +
  结构值前置」，是本文第 3 步。

---

## 10. 已决定的取舍（记录以免重提）

### 10.0 调试基础设施：`-dump-tokens` / `-ast-dump`

这两个选项是后续 debug 的基础，已完善（`src/dumptok.c`、`src/dumpast.c`、`src/main.c`）：

| 修复 | 说明 |
|---|---|
| `-raw-dump-tokens` 从未生效 | 帮助文本写 `-raw-dump-tokens`，参数解析却只认 `-dump-raw-tokens`；现已两者都接受 |
| AST 无源位置 | 每个节点现在打印 `Loc=<file:line:col>`，取自其代表 token（合成节点借用了来源 token，因此总能解析出位置） |
| 7 个节点无名字 | `ND_SUBACCESS` / `ND_ALLOCA` / `ND_CAS` / `ND_ATOMICRMW` / `ND_FENCE` / `ND_SP_SAVE` / `ND_SP_RESTORE` 此前落到 `unknown`；现 79 个 `NodeKind` 全部有名字（脚本核对：缺 0、陈旧 0） |
| 原子节点静默 | `ND_CAS` / `ND_ATOMICRMW` 此前只 `break` 不输出；现打印 `op=`/`order=`/`fail_order=`/`weak=` 并递归子节点 |
| 限定符全部丢失 | `print_type` 不打印 `const`/`volatile`/`restrict`/`_Atomic`；现由各类型自身打印（限定符就在该类型对象上） |
| **VLA 长度打印垃圾值** | 见下方「顺带修掉的编译器 bug」 |

### 10.0.1 顺带修掉的编译器 bug：VLA 类型被 union 重叠写入破坏

`parser.c` 的数组后缀处理对**所有**类型无条件写：

```c
ty->qual = qual;
ty->is_static = is_static;   // ← 与 vla_len 重叠
ty->is_star = is_star;       // ← 与 vla_cnt 重叠
```

而 `Type` 的 union 里，`{int len; bool is_static; bool is_star;}` 与
`{Node *vla_len; Sym *vla_cnt;}` **共用内存**。对 VLA（`vla_len` 是 64 位指针），
`is_static`/`is_star` 落在其**高 32 位**，写入 false（0）就把指针**截断成低 32 位**。
实测证据（`-ast-dump` 探针）：

```
a  kind=17(TY_VLA)  size=-1  vla_len=0x78850890      ← 被截断
                    vla_cnt=0x763e78850a60           ← 完整 64 位
```

影响：VLA 的长度/计数节点指针损坏；`int (*p)[n]` 这类**指向 VLA 的指针**在 dump 时
直接段错误（解引用坏指针）。修复：只对非 `TY_VLA` 写 `is_static`/`is_star`。

**这是一处真实的数据损坏**，不只是 dump 问题——`ast-dump` 只是让它显形。
VLA 运行期语义验证：`sum(5)==15`、`sum2(3,4)==138`、`sizeof(int[5])==20`，
与 clang 输出逐字一致。

### 10.0.2 已知限制

- VLA 长度若是**复合表达式**（`n + 1`、`m * 2`），只显示其代表 token 的文本，
  即 `int[+]`、`int[*]`。原因是 parser 把二元表达式的 `tok` 设为**运算符**，
  而表达式树只保留一个代表 token，dump 侧无法还原完整源码。
  要修需在 parser 层让 VLA 长度保留整段 token 范围，属解析器表示问题，未纳入本次范围。
- 单字符如 `int[n]` 与 `int[3]` 显示正确。

### 10.1 常量表达式里读取 const 对象

C23 6.6 允许把「对象的值」用作常量表达式，clang 因此接受对一个**已初始化数组**的读取，
gcc 则只接受 `constexpr`。cxx 采取与 clang 一致的**中间立场**（已实现，`eval2` 的
`ND_SUBACCESS`/`ND_MEMBER` 分支 + `const_array_elem`）：

| 情形 | cxx | gcc | clang |
|---|---|---|---|
| `const int a[2]={7,8}; int t = a[1];` | 接受 | 接受 | 接受 |
| `int a[2]={7,8}; int t = a[1];`（非 const） | **拒绝** | 拒绝 | 拒绝 |
| `const struct S s={7}; int t = s.a;` | 接受 | 接受 | 接受 |
| `const int a[4]={1,2}; int t = a[3];`（隐式零） | 接受（0） | 接受 | 接受 |
| `const int m[2][2]={{1}}; int t = m[1][1];` | 接受（0） | 接受 | 接受 |
| `struct S s={7}; int t = s.a;`（非 const） | 拒绝 | 拒绝 | 拒绝 |

判定要点：const 限定符在数组上位于**最内层元素类型**（`ty->base->qual`），
在结构体上位于类型本身；非 const 对象一律拒绝，因此不涉及 `Q_MEMCONST` 的传播路径。

实现契约（`const_array_elem`）：返回元素常量初始化器；哨兵 `IMPLICIT_ZERO` 表示
「隐式初始化，值为 0」，它同样**是常量**；`NULL` 表示根不是 const 对象——留地址形式
（`int *p = g.a.a;`）给调用方。一旦确认根是 const，任何无法解析的访问**立即报错**，
不再回退。

### 10.2 `const` 对象的 const 成员用作数组界 —— 保持现状（不向 clang 靠拢）

```c
struct S { const int n; };
const struct S s = {4};
int arr[s.n];        /* clang 接受；gcc 拒绝；cxx 拒绝 */
```

**决定：保持拒绝，与 gcc 一致。** 理由是这条落在 `eval` 对成员链的常量求值上，
与 §10.1 的初始化放宽是两条不同的语义分支；而 cxx 与 gcc 在此一致，
不构成缺陷。若将来要支持，应作为独立项排期。

（`case` 标签用的是另一条路径，cxx 与 gcc、clang 三者一致：接受。）

### 10.3 `max_align_t` 的定义方式
采用 `union { long long; long double; }` 而非照抄 glibc 的
`struct { long long; long double; }`。两者**对齐都是 16**（这是 `max_align_t` 唯一有
语义作用的属性，`src/util.c` 的 `ALIGNMENT` 正是取它），但在 amd64 上
`sizeof` 分别是 16 / 32。clang 与 gcc 的 `sizeof` 是 32；若不希望这一差异存在，
把 union 换成 struct 即可，仅影响 `sizeof(max_align_t)` 这一极少使用的值。
