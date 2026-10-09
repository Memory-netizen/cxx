# compiler-test-suite 全量跑出来的工作清单

由 `bash doc/fj-triage.sh` 从 `/home/memory/cxxwork/logs/fj-full5.results` 自动生成。
口径见 `doc/cxx-c2y-plan.md` §0：**缺口只记录并屏蔽**，下面“真缺陷”一节才是要修的。

## 0. 总数

fujitsu: 17638 of 17833 passed, 189 failed (out of scope: 2717 OpenMP, 7459 no-prototype/K&R; gaps: 398; unbuildable by the reference: 8783)

| 类别 | 数量 |
|---|---|
| `compile` | 171 |
| `exit` | 0 |
| `output` | 18 |
| `timeout` | 0 |
| `skip` | 2717 |
| `noproto` | 7459 |
| `reffail` | 8783 |
| `refcrash` | 5 |
| `reftimeout` | 1 |
| `gap` | 398 |

## 1a. 缺口：用到了 cxx 还没有的特性（只记录）

按“文件需要的特性”归类，逐个读文件得出：

| 数量 | 特性 | 代表文件 |
|---|---|---|
| 5 | `#pragma redefine_extname` | `C/0059/0059_0054.c` |
| 4 | `参考实现（clang -std=c23）同样拒绝` | `C/0048/0048_0049.c` |
| 3 | `#ident` | `C/0125/0125_0014.c` |
| 2 | `字符串里反斜杠接空白再接换行（GNU 扩展）` | `C/0059/0059_0081.c` |
| 2 | `atomic_*_fence` | `C/0057/0057_0025.c` |
| 1 | `函数级属性 / 内联（优化阶段）` | `C/0178/0178_0039.c` |
| 1 | `*_HAS_SUBNORM（标准已标为过时）` | `C/0057/0057_0075.c` |
| 1 | `float.h 的 *_DECIMAL_DIG` | `C/0057/0057_0076.c` |
| 1 | `_Complex` | `C/0057/0057_0079.c` |
| 1 | `__attribute__((weak))` | `C/0181/0181_0235/0181_0235_0000.c` |

## 1a2. 记录的分歧：按标准判定 cxx 正确（`doc/fj-divergences.md`）

| 诊断 | 测试 |
|---|---|
| `lvalue required as ‘X’ operand` | `C/0044/0044_0007.c` |
| `incompatible types when assigning` | `C/0202/0202_0097.c` |
| `incompatible types when assigning` | `C/0202/0202_0100.c` |
| `incompatible types when assigning` | `C/0202/0202_0103.c` |
| `incompatible types when assigning` | `C/0202/0202_0106.c` |

## 1b. 真缺陷：编译被拒（按诊断聚类）

`compile` 共 171 个；逐个重跑 cxx 取诊断后聚类：

| 数量 | 诊断 | 代表文件 |
|---|---|---|
| 145 | `implicit declaration of function ‘X’`（**政策类**：C23 已删除的旧形式，见 §0 与 §R81） | `C/0055/0055_0154.c` |

## 1c. 缺口：运行期失败里同样是用到了没有的特性

| 数量 | 特性 | 代表文件 |
|---|---|---|
| 2 | `函数级属性 / 内联（优化阶段）` | `C/0077/0077_0031.c` |
| 2 | `*_HAS_SUBNORM（标准已标为过时）` | `C/0005/0005_0051.c` |
| 1 | `目标特性宏（优化/代码生成阶段）` | `C/0044/0044_0003.c` |
| 1 | `字符串里反斜杠接空白再接换行（GNU 扩展）` | `C/0059/0059_0050.c` |
| 1 | `函数级/对象属性 noinline、weakref（优化阶段）` | `C/0059/0059_0087.c` |
| 1 | `#pragma redefine_extname` | `C/0059/0059_0051.c` |
| 1 | `float.h 的 *_DECIMAL_DIG` | `C/0005/0005_0053.c` |

## 2. 真缺陷：运行期不一致

### `output` 的分类：谁没通过它自己的检查

逐个用两家各编译运行一遍，数它们自己打印的 `OK`/`NG`：

| 类别 | 数量 | 读法 |
|---|---|---|
| `ref-ng` | 6 | 参考实现自己报 NG、cxx 全 OK —— **cxx 正确，属参考分歧，只记录** |
| `cxx-ng` | 0 | cxx 报 NG、参考全 OK —— **真缺陷** |
| `plain` | 0 | 两家都不打 OK/NG，打印普通数值，逐个案看 |
| `divergence` | 3 | 已在 `doc/fj-divergences.md` 记录，判定为 cxx 正确 |

  - `C/0055/0055_0670.c`
  - `C/0055/0055_0676.c`
  - `C/0178/0178_0035.c`
  - `C/0054/0054_0019.c`
  - `C/0054/0054_0027.c`
  - `C/0054/0054_0018.c`
  - `C/0054/0054_0021.c`
  - `C/0054/0054_0023.c`
  - `C/0054/0054_0020.c`

### `output`（18）—— 按目录

```
      6 0054
      3 0059
      3 0005
      2 0077
      2 0055
      1 0178
      1 0044
```

代表文件：

  - `C/0005/0005_0051.c`
  - `C/0005/0005_0052.c`
  - `C/0005/0005_0053.c`
  - `C/0044/0044_0003.c`
  - `C/0054/0054_0018.c`

## 3. 缺口（已屏蔽，不在本阶段实现）

| 数量 | 缺口 |
|---|---|
| 274 | () with parameters unspecified (C17) |
| 73 | __sync_* (unimplemented forms) |
| 51 | SSE/MMX intrinsics |

## 4. 超出范围（两家参考实现也如此判定）

| `skip` | 2717 |
| `noproto` | 7459 |
| `reffail` | 8783 |
| `refcrash` | 5 |
| `reftimeout` | 1 |

## 5. 下一阶段建议的次序

1. §1 里数量最大的那一类（真缺陷，编译被拒）—— 每类先做最小复现，再定根因。
2. §2 的 `exit`/`output`：先按目录归并，同一目录的多个失败通常同源。
3. §3 的缺口按 §0 政策**只记录**；要扩功能时另开阶段统一规划。
