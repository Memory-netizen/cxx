# 拉取式前端：主张评估与重构计划

设计阶段文档，不含实现。评估对象：把"**整文件词法化 → 整条流物化 → parser 遍历**"改成
"**parser 驱动、token 按需产出**"（parser 调 `next_tok()`/`lookahead(N)`，预处理器调
`next_rawtok()`，词法器按需产出，预处理层负责前瞻/回退）。

实测依据在 `doc/perf.md` §8cl（四个编译器的分相/指令数/缓存）与 §8cm（死区普查与收益推算）。

---

## 0. 摘要

1. **方向成立**，而且与现有代码的落差比看起来小：`#include` 今天**已经是输入栈** ——
   `include_file()`（preprocess.c:1882）用 `*rest = tok2` 把被包含文件的链接进流，
   `push_file()`/`pop_file()`（176/199）记回程；`preprocess2` 的段循环本身就是段级流。
   真正批量的只有两处：**单文件全量词法**、**"预处理器把整条流物化后才交给 parser"**。
2. **"按需读入文件"要放弃**：`Token` 不存拼写，只存 `(file_uid, loc)`，文字由
   `tok_text()` 现算（cxx.h:600 `file_of(uid)->contents + loc`）。字节层必须**常驻**，
   惰性属于**记号层**。滑窗字节缓冲会同时打断宏展开的 stringize/paste 与 parse 期的诊断。
3. 收益（§8cm 推算）：前端约 **−4% ~ −10%（最可能 ~6%）**，`cxx -c` 端到端
   **−1.3% ~ −3%**；**内存是确定收益**（死区记号占预处理输入 39.4%，arena 117 MB 中
   31.6 MB 是死区）。真正的大头不在前端：`cxx -c sqlite3` 里 cc1 只占 32%，cc1 里
   IR 打印占 36%。
4. 你的七条主张里，**五条成立、一条要限范围、一条要换形式**（详见 §3）。

---

## 1. 目标与非目标

**目标**

* 内存：token 工作集从"整条流 + 整份宏展开中间物"降到"前瞻窗口 + 当前展开"。
* 让 `-E`/`-M` 可以边产边打（今天要物化 125 MB 才打印）。
* 为将来的增量/IDE 用法留出接口（只解析到某个声明）。

**非目标**

* 不为速度做这件事（§8cl：10× 的差距在后端，不在前端）。
* 不重写宏展开引擎的语义；不改 parser 的函数形态（95 个 `Token **rest` 出参保持）。

---

## 2. 三层与不变量

| 层 | 组件 | 不变量 |
|---|---|---|
| **L1 字节层** | `read_file()`（lexer.c:1181）、`SrcFile`（cxx.h:339） | **整份内容常驻且地址稳定**（`mmap` 或一次性读入后永不释放）；`line_offsets` 只增不减 |
| **L2 记号层** | `next_rawtok` / 展开栈 / 输出层 | 记号按需产出；**输入栈四类源**；已产出尾巴可**撤回**；宏展开仍以"每次展开物化小表"为单位 |
| **L3 parser** | `parser.c`（9,906 行，95 个 `rest` 出参） | 游标是**数据**（`tok`/`rest`），不是全局状态；缓冲**可回退**，但**不做 pp 状态快照** |

---

## 3. 逐条评估你的主张

### 3.1 `tok_text()` 的字节常驻

**事实**：36 行 / 37 次调用，分布与冷热：

| 位置 | 次数 | 冷热 | 备注 |
|---|---|---|---|
| lexer.c:58 `skip()` 报错 | 1 | 冷（词法期） | |
| lexer.c:462 `convert_universal_chars` | 1 | 热（含 `\u` 时） | 就地 |
| lexer.c:511/513 `convert_char_literal` | 2 | **热**（每个字符常量） | 就地 |
| lexer.c:601/602 `convert_pp_num` | 2 | **热**（每个 pp-number） | 就地 |
| preprocess.c:923/945/955 `join_tokens`/`join_spelling` | 3 | **热**（stringize `#`） | 读 `read_macro_args` 收集的实参记号 |
| preprocess.c:994 `paste` | 2 | **热**（`##`） | 同上 |
| main.c:872/873/888/922 `needs_space`/`print_tokens` | 4 | 热（仅 `-E`） | |
| parser.c 7 处 + type.c 4 处 | 11 | 冷（全是 error/warning） | 读 `Node.tok`/`Type.name`/`ty->name` 里的记号 |
| preprocess.c:605/1431/1715/1766/1843/1921/1928 | 7 | 冷（`#if` 报错、`#include` 名、`#pragma pack`、`#line`） | |
| dumpast.c:209/210、dumptok.c:78/100 | 4 | 冷（`-ast-dump`/`-dump-tokens`） | |

**判定：一半成立。**

* **成立**："热路径上文字就在当前缓冲区里" —— lexer 内部那 6 处确实就地（转换与报错都发生在
  扫描位置），stringize/paste 也发生在同一次展开内。
* **已修正**（见 `doc/perf.md` §8cn）：`-E` 与 `#line` **不是**"很久以后"的读者 ——
  `-E` 完全可以边产边打（实测 gcc/clang/tcc 在诊断之前已交出 106~109 KB，cxx 是 **0 B**），
  `#line` 读的也是刚 `read_line()` 读进来的那一行。这两条从"字节必须常驻"的理由里去掉。
* **仍然不成立**的部分，真正的理由是**宏体借用了别的文件的文字**：
  1. `#define` 把宏体存成记号链，记号的 `(file_uid, loc)` 指向**定义它那个文件**。宏可以在
     几千行之后、甚至从另一个文件里展开，此时 `#`/`##`（`join_tokens` 923/945/955、
     `paste` 994）与 `-ast-dump`/诊断仍要读那份文字 —— 而那个文件在逻辑上**早就 include
     结束了**。滑窗一旦重置指针，这里必崩，且崩的距离没有常数上界（实参可以任意长）；
  2. parser/type 那 11 处虽然是冷路径，读的却是**长期存活结构里的记号**
     （`Node.tok`、`Type.name`、`Sym.tok`、`Member.tok`），问题不是"文字还在不在缓冲区"，
     而是**记号对象本身还在不在**（这是 `Loc` 存在的原因，cxx.h:551-562）。
* **结论**：`next_c` 的"缓冲区空则 `read` 进来、重置指针"应改为
  **在 mmap 视图上推进指针**（不重置）；惰性只放在记号层。
* **另一条可选路线（记录在案，不推荐）**：让宏体**自己拥有拼写**（tcc 的 `TokenString`
  做法：`#define` 时把体复制一份文字），这样文件字节原则上可以释放。代价是每个体记号一次
  定义期复制，且 `Loc` 的语义要跟着改（`Loc` 只有 `(file_uid, loc)`，文字没了它就报不出
  源码行）—— 为了省几个 MB 去动 20 字节的 `Loc` 与全部诊断，不划算。
* 顺带一句：`Token` 带 `loc`/`len` 并不是"因为非流式"，而是"诊断要能定位到一个比记号池
  活得久的位置" —— `Loc` 的注释（cxx.h:551-562）就是这么写的。这条与流式化无关。

### 3.2 用 id 比较替掉 `cond_incl->if_tok == guard_ifndef`

**事实**：`guard_ifndef`（静态，按文件保存在 `FileStack` 176/199）在 `#endif` 处与
`cond_incl->if_tok` 比较（2045），再加 `tok->kind == TK_EOF`（2048）判定"守护宏覆盖到文件尾"。
`if_tok` 另有两处用途：`#endif without #if`（2038）与 `unterminated conditional directive`（2073）
的报错位置。

**判定：成立，但形式要换。** "检查当前栈深度为 1"不是精确条件（同一文件可以有多个
`#ifndef`，也可以嵌套）—— 精确形式是**条件帧序号**：

* `push_cond_incl()` 给每个帧发一个单调 `seq`；
* `detect_include_guard1()` 记下该文件的 `guard_seq`（而不是 `Token *guard_ifndef`）；
* `#endif` 时比 `cond_incl->seq == guard_seq` 即可，与栈深度无关；
* 报错位置改成 `Loc cond_if_loc`（`Loc` 本就设计成能比池子活得久）。

**同一类问题里更硬的一处**：`guard[].macro`（1666）**跨整个编译持有 `Token *`**，并用它做
`find_macro(guard[i].macro)`（1885）判断"这个被守护的头文件已经读过了"。流式化＋回收之后
这个指针必失效 —— 改成存**宏名 `id`**（`intern` 后的 `uint32_t`）+ 按 id 查宏表即可。

### 3.3 `join_adjacent_string_literals`

**事实**：两个 pass（2627-2661 先统一编码前缀并逐记号 `convert_str_literal` 解码，
2664+ 再拼接），同时承担 `convert_str_literal`。`-E` 的打印发生在它**之前**
（main.c:1032 打印，1039 才 join）。

**判定：成立，三点补充。**

1. **必须在输出层做**（宏展开之后）：`"a" EMPTY "b"` 只有展开后才相邻 —— phase 6 在
   phase 4 之后；lexer 看不到这种相邻性。
2. **顺序是语义的一部分**：`-E` 打印的是**未合并**的记号（gcc/clang 同样如此），所以
   "合并"只能是 parser 侧消费者看到的行为，`-E` 消费者不合并。流式实现里这等于两个钩子。
3. **指令里的 `#include "a" "b"` 不能因为"直接跳到行尾"就丢掉诊断**：今天由
   `skip_line()` 发 extra-token 警告（preprocess.c:215 `WG_EXTRA_TOKENS`）。流式跳过时
   要保留"本行还有非空白记号"这个检查，否则少一条诊断（tcctests 里有依赖它的用例）。

### 3.4 `filter_tokens` 不再需要

**判定：成立，而且比你说的更强。** 实测：正常运行里它**一个记号都不丢**
（`TK_WS`/`TK_NL`/`TK_COMMENT` 只在 `-dump-raw-tokens`（`opt_keep_ws_comments`）下生成，
lexer.c:1004/1017/1043；计数得 `ws = 0`）—— 它今天就是**一条纯遍历**，只为那个调试模式留着。
main.c:928 那份 `filter_tokens` 丢的是 `TK_LINE`（行标记），只为 `-E` 打印服务。

流式化后：两份 `filter_tokens` 都消失；`-dump-raw-tokens`/`-dump-tokens` 保留一条
"物化"路径（调试模式，不在热路径上）。

### 3.5 宏展开引擎维护一个 token 流

**判定：成立，且现状已经有一半。** 需要明确的是**四类源**与**一条回退能力**：

| 源 | 现状位置 | 流式后 |
|---|---|---|
| 文件 | `tokenize_file()` + `*rest = tok2`（1882-1898） | 文件源按需产出 |
| 宏体/实参展开 | `copy_body()`(887)、`subst()`(1014)、`expand_macro()`(1218) | **保持物化小表**，产物压进输出栈 |
| `#embed` 生成 | `emit_embed()`(1589) 往流里塞逗号分隔的字节值 | 第四类源（生成器） |
| 命令行 `-D/-U` | `prep_cmdline()` | 文件源的特例 |

**不能简化掉的一条**：`rescan_call()`（1163-1201）会**撤回已经产出的记号**
（`before->next = NULL`）并**窥视输入流后面的记号**（1172 `in->kind != TK_LPAREN`）。
所以流式化只做在**外层**（文件/段），宏展开引擎继续按"每次展开物化成小表"工作 ——
把 `expand_macro`/`subst` 逐记号流式化会直接丢掉 C/0048/0048_0087 与 C/0054/0054_0102
那两类语义（hideset 的 `noexpand` 戳、`origin` 链）。

### 3.6 让 lexer 在字符串扫描里跨空白继续

**判定：不安全，建议不做。** 三条理由：

1. 指令行里**不能**合并：`#include "a" "b"` 必须保持两个记号才有 extra-token 诊断；
2. 宏分隔的相邻串 lexer 看不到（见 3.3-1）；
3. 合并还要做**前缀统一**与 `unsupported non-standard concatenation` 诊断
   （preprocess.c:2652），这属于 phase 6 —— 放输出层只有一处实现，放 lexer 就变成两处。

唯一收益是"少产生几个记号"，与 3.4 同源，不值得为它复制一份语义。

### 3.7 实时行号与增量行偏移表

**判定：成立。** 今天 `get_location()`（lexer.c:1420+）在 `SrcFile.line_offsets`
（cxx.h:346）上二分，表由 `tokenize()` 一次性建好（lexer.c:1412）。

**补充**：行首表必须**整份保留**（与字节是否常驻无关）：`Loc` 只有 `(file_uid, loc)`，
`get_location()`（lexer.c:1420+）靠这张表把它换成行列，而诊断可以在 parse 期任意时刻对一个
很旧的 `Loc` 发问。成本可以忽略：每行 4 B，sqlite3.c 31 万行 = 1.2 MB。增量 append 即可，
`get_location()` 的二分不用改。

---

## 4. 输出层管线（顺序就是语义）

今天的顺序（preprocess.c:2706-2719 + main.c:1032-1039）：

```
filter_tokens → preprocess2（展开 + 段内 _Pragma） → scan_pragma_op
  → convert_keywords → convert_ppnumber（含 convert_char_literal）
  → [-E 在这里打印]
  → join_adjacent_string_literals（含 convert_str_literal）
  → parse
```

流式化后，`next_tok()` 内部固定为：

```
从展开栈取一个记号
  → _Pragma 处理（scan_pragma_op 的语义）
  → keyword 化（convert_keywords）
  → pp-number / char 转换（convert_ppnumber / convert_char_literal）
  → 字符串合并 + 解码（join_adjacent_string_literals + convert_str_literal）
  → 返回给 parser
```

两个必须保留的钩子：`-E`/`-M` 消费者要在**合并之前**取记号；`-dump-raw-tokens`
要在**全部转换之前**取记号（保留物化路径即可）。

---

## 5. parser 侧接口（细化）

**事实**（实测/统计）：parser **没有全局游标**（`^Token *tok;` 0 个），游标是函数参数
（95 个 `Token **rest`、117 处 `skip(tok,…)`、357 处 `Token *`）；最深前瞻只有 **2 个记号**
（`tok->next->next` 全文件 14 处）；`find_typedef()`（1015-1021）**只看 1 个记号**；
`is_typename()` 是**不消费**的谓词（14 处调用）；`skip_leading_attrs()`（8 处）要不消费地
走过属性；只有 2 处 `tok = start;` 式回退。

**设计**：

* **保留 `tok`/`rest` 形态**，不改 95 个签名（改成 tcc 的全局游标要动 456 处 `->next`，
  收益为零）；
* 在 pp 与 parser 之间放一个**可增长、可回退的记号缓冲**：
  `peek(n)`（不足则拉）、`save()/restore()`（缓冲内下标，**不是** pp 状态快照）；
* 因此 **不需要** `TentativeParsingAction` 式的全状态保存/恢复 —— 只要缓冲不丢，
  试解析（丢弃 `rest`）与 `is_typename`/`skip_leading_attrs` 的前瞻都自然成立；
* **回收**：游标之前的记号可回收，但前提是 §3.1 里那 10 处长期 `Token *` 先变成 `Loc`
  （cxx.h:685/771/1117/1179/1401/1523/1580/1596 + `guard[].macro` + `cond_incl->if_tok`）。

---

## 6. 分期计划

| 阶段 | 内容 | 触及 | 验收 | 风险 | 量级 |
|---|---|---|---|---|---|
| **0** | `PpCtx` + 门面 `pp_next()`/`pp_peek()`，底层**仍是**现有批量流水线 | preprocess.c 头部 53 个可变 static | 全闸门（应全绿） | 低 | 1-2 天 |
| **1** | **惰性词法 + 死区跳过（不动 parser）**：文件按块产出记号；`#if` 未取分支用字符级跳过（仍识别注释/字符串/字符常量与行首指令）；删掉 `filter_tokens` | lexer.c、preprocess.c（`include_file`/`preprocess2`） | 全闸门 + 死区计数对比 + 峰值 RSS | 中（跳过逻辑） | ~1 周 |
| **2** | **输出层管线 + parser 拉取**：可回退缓冲 + `peek`/`save`/`restore`；去掉 `scan_pragma_op`/`convert_keywords`/`convert_ppnumber`/`join_adjacent_string_literals` 的整流遍历 | main.c、preprocess.c、parser.c（仅入口与前瞻点） | 全闸门 + `-E` 逐字节 + IR 逐字节 | **高** | 2-4 周 |
| **3** | **回收 + `Token*`→`Loc`**（10 处） | cxx.h、parser.c、type.c、preprocess.c | 全闸门 + 峰值 RSS | 中 | ~1 周 |
| **4** | **`-E`/`-M` 流式打印**（不物化）。**这是行为变更**：今天 cxx 在诊断前一个字节都不输出，gcc/clang/tcc 会输出（§8cn）；流式化后 cxx 落到 tcc 那一侧（出错前半段已出去、随后停），而 gcc/clang 是"报完继续打全文" | main.c、preprocess.c | `-E` 逐字节 + `strace` 次序（诊断前 stdout 非空） | 低-中 | 3-5 天 |

**阶段 1 已完成并合入工作树**（`doc/perf.md` §8co），三版：

* **v1**（跳过模式复用词法器）：闸门全绿、六个负载 IR 逐字节相同、sqlite3 的 arena −28.7%、
  峰值 RSS −14.0%，但**指令数 +0.7% ~ +4.9%** —— 跳过仍然逐记号扫描，省的不够付多出来的分支。
* **v2**（**裸字符级 skipper**；字面量仍调词法器自己的 reader，保证"到哪里结束"同源）：
  sqlite3 **指令数 −3.63%、墙钟 −5.3%、峰值 RSS −14.0%**，而闸门、IR、`-E` 全部保持逐字节
  一致。**验收项达成**：死区跳过的每字节成本已降到正常词法的 1/3 以下。

* **v2 + 1b**（惰性 include：lexer 的 chunk 随 `FileStack` 入栈/出栈）：
  **五个负载指令数全部不再上升**（−0.70% ~ −3.64%），头文件密集的 git-commit 从 +2.15%
  翻成 −0.38% 并拿到 −2.5% 的 RSS，sqlite3 的 −14.2% RSS 保持。闸门全绿。

落地后仍在的已知小账：chunk 机制本身约 +1% 的分支开销（`LINK` 里的 `is_sol` 判断、
`stop_pending` 维护），可以按"本文件有没有指令"选一套无 chunk 的循环摊掉；这一条留着，
不影响已合入的结论。

**阶段 1 是性价比最高的一步**：它单独就能吃掉 §8cm 里"死区不再词法化 + 不再分配/清零"
（约 −4% ~ −5% 的 cc1、arena −27%），而且**完全不动 parser**，风险面小得多。
建议先做 1，用实测决定 2/3 是否值得。

**回归网（现成，每阶段都跑）**：conformance 326/0、c2y 101/0、`doc/tcctests.sh` 106/0、
`doc/bootstrap.sh`（cxx2=cxx3=cxx4 逐字节 + 21/21）、arm64/rv64/rv32 各 51/0、
四个负载 IR 逐字节、六个负载 `-E` 逐字节、memcheck 零报错。

**阶段 4 另加一条行为闸门**（`doc/perf.md` §8cn 的方法）：`strace -f -e trace=write`，
只看产出 `-E` 文本的那个 pid，要求"诊断之前 stdout 已有输出"。今天是
`106,496 B (gcc) / 108,950 B (tcc) / 0 B (cxx)`。

---

## 7. 与重构解耦的"先手"

按 §8cl/§8cm 的实测，下面几件事比整个重构更划算，且互不冲突：

1. **`dumpir.c:ofmt` 静态化格式串** —— 单函数占 cc1 的 19.05%（§8cj）。
2. **死区不词法化** —— 就是阶段 1，单独可交付。
3. 把 `filter_tokens`（什么都不丢）、`convert_keywords`、`convert_ppnumber` 的遍历
   就地合并，减少 pass 数（不改架构）。

---

## 8. 不做什么

* **不做滑窗字节缓冲**（§3.1）；
* **不把宏展开引擎 token 级流式化**（§3.5）；
* **不改 95 个 `rest` 出参的函数形态**（§5）；
* **不做全 pp 状态快照式试解析**（§5）；
* **不为速度做这件事**（§8cl：差距在后端）。

---

## 9. 未决问题

1. **合成记号的文本**：`paste()`（980-993）与 `<scratch space>`（2552）今天靠"合成的
   SrcFile"承载文字，流式化后保持这条规则即可 —— 但要确认没有别处再依赖"文字一定在
   原始文件里"。
2. **回收粒度**：按 arena chunk 回收，还是"游标之前的整份文件"整块回收？取决于阶段 3 的实测。
3. **`-dump-raw-tokens`/`-dump-tokens`**：保留物化路径（调试模式，非热路径），还是改成
   边产边打（会改变输出顺序）？
4. **`#embed` 的源**：`emit_embed()` 生成的是"逗号分隔的字面量序列"，它的 token 属于
   合成 SrcFile；流式化后它是第四类输入源，需要和宏展开栈共用"回退"能力。
