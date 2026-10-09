#!/bin/bash
# Turn a full Fujitsu run into a work list.
#
#   bash doc/fj-triage.sh [results] [out]
#
# The probe's per-test verdicts live in the file FJ_RESULTS names (its stdout
# carries only the summary, and its temp directory is removed on exit). One
# line per test that did not pass -- \`compile\`, \`exit\`, \`output\`, \`timeout\` --
# and one per test set aside -- \`skip\` (OpenMP), \`gap <feature>\`, \`noproto\`,
# \`reffail\`, \`refcrash\`, \`reftimeout\`. A \`compile\` line says only that cxx
# refused the file, so this script compiles those files again (in parallel) to
# collect the diagnostic, groups them into classes, and writes the classes out
# as a work list.
#
# Feature gaps are listed apart from defects on purpose: section 0 says a
# feature cxx does not have is recorded and filtered, not implemented on the
# spot, and only the failures that remain are real defects.
set -u
CXX=${CXX:-$HOME/cxx/cxx}
LOG=${1:-$HOME/cxxwork/logs/fj-full.results}
SUMMARY=${TRIAGE_SUMMARY:-$HOME/cxxwork/logs/fj-full.log}
OUT=${2:-doc/fj-worklist.md}
JOBS=${TRIAGE_JOBS:-8}
WORK=${TRIAGE_WORK:-$HOME/cxxwork/logs/triage}
mkdir -p "$WORK"

[ -s "$LOG" ] || { echo "no results at $LOG (set FJ_RESULTS when running the probe)" >&2; exit 2; }
[ -x "$CXX" ] || { echo "no compiler at $CXX" >&2; exit 2; }

count() { grep -c "^$1 " "$LOG" 2>/dev/null || true; }
files() { grep "^$1 " "$LOG" | sed "s/^$1 //"; }

echo "== triaging $LOG"

# --- 1. compile failures: ask cxx what it says ---
files compile > "$WORK/compile.list"
n_compile=$(wc -l < "$WORK/compile.list")
if [ "$n_compile" -gt 0 ]; then
    cat > "$WORK/diag.sh" <<'SH'
#!/bin/bash
f=$1
out=$(timeout 60 "$CXX" -w -o /dev/null "$f" -lm 2>&1)
# A link failure is the caller's build step failing, not a rejected unit, and
# the linker's message carries no "error:" of ours.
if echo "$out" | grep -q 'undefined reference'; then
    sym=$(echo "$out" | grep -m1 -o "undefined reference to [^ ]*" | cut -c24-50)
    printf '%s\t%s\n' "link: undefined reference to $sym" "$f"
    exit 0
fi
msg=$(echo "$out" | grep -m1 'error:' | sed 's/.*error: //' | sed "s/‘[^’]*’/‘X’/g" | cut -c1-72)
if [ -z "$msg" ]; then
    # cxx's own diagnostics do not always carry the word "error".
    msg=$(echo "$out" | grep -m1 -v '^ *[0-9]* |' | head -1 | cut -c1-72)
    # No message at all means cxx compiled the file this time: the verdict in
    # the results file predates a fix, and the entry is stale rather than a
    # crash. (A crash prints "internal compiler error" through the driver.)
    if [ -z "$msg" ]; then
        if "$CXX" -w -c -o /dev/null "$f" > /dev/null 2>&1; then
            msg="（已修复：本次编译通过，结果文件是旧的）"
        else
            msg="(no output at all -- cxx crashed?)"
        fi
    fi
fi
printf '%s\t%s\n' "$msg" "$f"
SH
    chmod +x "$WORK/diag.sh"
    CXX="$CXX" xargs -a "$WORK/compile.list" -d '\n' -P "$JOBS" -n 1 "$WORK/diag.sh" > "$WORK/compile.diag" 2>"$WORK/compile.err" || true
fi

# --- 1a2. tests recorded as divergences: the standard is on cxx's side ---
#
# doc/fj-divergences.md lists the tests where cxx refuses or differs and the
# standard says cxx is right (the references are the lenient ones). They are
# not defects and are filed apart.
DIVERGENCES=${DIVERGENCES:-doc/fj-divergences.md}
: > "$WORK/compile.divergence"
if [ -f "$DIVERGENCES" ]; then
    grep -oE 'C/[0-9]+/[0-9_/]+\.c' "$DIVERGENCES" | sort -u > "$WORK/divergent.list"
fi

# --- 1b. name the feature a refused file needs ---
#
# A file cxx refuses because it uses something cxx does not have is a gap, not
# a defect (section 0 of the plan: recorded and filtered, not implemented on
# the spot). The feature is read off the source, which is what says it.
feature_of() {
    f=$1
    if grep -q 'redefine_extname' "$f" 2>/dev/null; then echo '#pragma redefine_extname'; return; fi
    if grep -qE '^[[:space:]]*#ident' "$f" 2>/dev/null; then echo '#ident'; return; fi
    if grep -qE 'atomic_(thread|signal)_fence' "$f" 2>/dev/null; then echo 'atomic_*_fence'; return; fi
    if grep -q 'FLT_HAS_SUBNORM\|DBL_HAS_SUBNORM\|LDBL_HAS_SUBNORM' "$f" 2>/dev/null; then echo '*_HAS_SUBNORM（标准已标为过时）'; return; fi
    if grep -q 'FLT_DECIMAL_DIG\|DBL_DECIMAL_DIG' "$f" 2>/dev/null; then echo 'float.h 的 *_DECIMAL_DIG'; return; fi
    if grep -q '_Complex\|__STDC_IEC_559_COMPLEX__\|__STDC_NO_COMPLEX__' "$f" 2>/dev/null; then echo '_Complex'; return; fi
    # Function-level attributes and anything that only matters to an
    # optimiser: the user's ruling is that these are recorded as gaps and done
    # in an optimisation phase, not now.
    if grep -q 'always_inline\|__builtin_return_address\|__builtin_frame_address' "$f" 2>/dev/null; then
        echo '函数级属性 / 内联（优化阶段）'; return
    fi
    if grep -q 'weakref\|noinline' "$f" 2>/dev/null; then
        echo '函数级/对象属性 noinline、weakref（优化阶段）'; return
    fi
    # __builtin_constant_p is implemented -- the user's reading -- so a file
    # that uses it is a defect's file, not a gap's.
    if grep -qE '__SSE[0-9_]*__|__AVX[0-9_]*__|__MMX__|__AVX2__' "$f" 2>/dev/null; then
        echo '目标特性宏（优化/代码生成阶段）'; return
    fi
    if grep -q '__attribute__[[:space:]]*((weak))\|__attribute__((weak))' "$f" 2>/dev/null; then echo '__attribute__((weak))'; return; fi
    if grep -qE '\\[[:space:]]+$' "$f" 2>/dev/null; then echo '字符串里反斜杠接空白再接换行（GNU 扩展）'; return; fi
    # Not a defect either: the reference refuses the file as well. Two of
    # the files left (C/0048/0049, C/0186/0131) are like that -- gcc,
    # clang and cxx all reject them, so there is nothing to fix.
    if ! clang -w -std=c23 -c -o /dev/null "$f" > /dev/null 2>&1; then
        echo '参考实现（clang -std=c23）同样拒绝'
        return
    fi
    echo ''
    return
}
: > "$WORK/compile.gap"
: > "$WORK/compile.defect"
while IFS=$'\t' read -r msg file; do
    [ -z "${file:-}" ] && continue
    if [ -s "$WORK/divergent.list" ] && grep -qF "${file#$HOME/compiler-test-suite/}" "$WORK/divergent.list"; then
        printf '%s\t%s\n' "$msg" "$file" >> "$WORK/compile.divergence"
        continue
    fi
    feat=$(feature_of "$file" "$msg")
    if [ -n "$feat" ]; then
        printf '%s\t%s\t%s\n' "$feat" "$msg" "$file" >> "$WORK/compile.gap"
    else
        printf '%s\t%s\n' "$msg" "$file" >> "$WORK/compile.defect"
    fi
done < "$WORK/compile.diag"

# --- 1c. the same question for the runtime failures ---
#
# A test that compiles and then differs may still be a feature cxx does not
# have -- an attribute it does not forward, say -- and the user's ruling puts
# those with the gaps. The source decides, exactly as it does for a refusal.
: > "$WORK/runtime.gap"
: > "$WORK/runtime.defect"
for kind in exit output timeout; do
    files "$kind" | while read -r f; do
        feat=$(feature_of "$f" "")
        if [ -n "$feat" ]; then
            printf '%s\t%s\t%s\n' "$feat" "$kind" "$f" >> "$WORK/runtime.gap"
        else
            printf '%s\t%s\n' "$kind" "$f" >> "$WORK/runtime.defect"
        fi
    done
done

# --- 2. runtime failures, by directory ---
for kind in exit output timeout; do
    files "$kind" | sed 's|/[^/]*$||' | sed 's|.*/C/||' | sort | uniq -c | sort -rn > "$WORK/$kind.dirs"
done

# --- 2b. classify the output differences: who fails its own check? ---
#
# The tests that print \`OK\`/\`NG\` about their own checks split into two kinds,
# and they need opposite treatment: where the reference reports NG and cxx
# does not, cxx is right and the reference diverges (struct layout is the usual
# reason) -- recorded, not chased. Where cxx reports NG and the reference does
# not, that is a defect. Tests that print plain values are their own class.
if [ "$(count output)" -gt 0 ]; then
    : > "$WORK/output.class"
    # The tests section 1c already filed as gaps are not classified here: an
    # attribute cxx does not forward is a gap, not a runtime defect.
    files output | grep -vxFf <(cut -f3 "$WORK/runtime.gap" 2>/dev/null | sort -u) 2>/dev/null |
    xargs -P "$JOBS" -I{} bash -c '
        cxx="$1"
        f="$2"
        d=$(mktemp -d)
        ref=$d/ref; cxxb=$d/cxx
        clang -w -o "$ref" "$f" -lm > /dev/null 2>&1 && timeout 10 "$ref" > "$d/ref.out" 2>&1
        "$cxx" -w -o "$cxxb" "$f" -lm > /dev/null 2>&1 && timeout 10 "$cxxb" > "$d/cxx.out" 2>&1
        rok=$(grep -c OK "$d/ref.out" 2>/dev/null || true)
        rng=$(grep -c NG "$d/ref.out" 2>/dev/null || true)
        cok=$(grep -c OK "$d/cxx.out" 2>/dev/null || true)
        cng=$(grep -c NG "$d/cxx.out" 2>/dev/null || true)
        class=plain
        if [ "${rng:-0}" -gt 0 ] && [ "${cng:-0}" -eq 0 ]; then class=ref-ng
        elif [ "${cng:-0}" -gt 0 ] && [ "${rng:-0}" -eq 0 ]; then class=cxx-ng
        fi
        # A test doc/fj-divergences.md already records stays recorded here as
        # well: whether it is "cxx prints NG" or "nobody prints OK/NG" is
        # beside the point once the difference has been judged and written
        # down with its citation.
        if [ -s "$3" ] && grep -qF "${f#$4/}" "$3"; then class=divergence; fi
        printf "%s\t%s\n" "$class" "$f"
        rm -rf "$d"
    ' _ "$CXX" {} "$WORK/divergent.list" "$HOME/compiler-test-suite" >> "$WORK/output.class"
fi

# --- 3. the report ---
{
    echo "# compiler-test-suite 全量跑出来的工作清单"
    echo
    echo "由 \`bash doc/fj-triage.sh\` 从 \`$LOG\` 自动生成。"
    echo "口径见 \`doc/cxx-c2y-plan.md\` §0：**缺口只记录并屏蔽**，下面“真缺陷”一节才是要修的。"
    echo
    echo "## 0. 总数"
    echo
    [ -s "$SUMMARY" ] && grep '^fujitsu:' "$SUMMARY" | tail -1
    echo
    echo "| 类别 | 数量 |"
    echo "|---|---|"
    for k in compile exit output timeout skip noproto reffail refcrash reftimeout; do
        printf '| `%s` | %s |\n' "$k" "$(count $k)"
    done
    printf '| `gap` | %s |\n' "$(count gap)"
    echo
    echo "## 1a. 缺口：用到了 cxx 还没有的特性（只记录）"
    echo
    if [ -s "$WORK/compile.gap" ]; then
        echo "按“文件需要的特性”归类，逐个读文件得出："
        echo
        echo "| 数量 | 特性 | 代表文件 |"
        echo "|---|---|---|"
        awk -F'\t' '{n[$1]++; if (!($1 in first)) first[$1]=$3} END {for (k in n) printf "%d\t%s\t%s\n", n[k], k, first[k]}' \
            "$WORK/compile.gap" | sort -rn |
            while IFS=$'\t' read -r c feat f; do
                printf '| %s | `%s` | `%s` |\n' "$c" "$feat" "${f#$HOME/compiler-test-suite/}"
            done
    else
        echo "无。"
    fi
    echo
    if [ -s "$WORK/compile.divergence" ]; then
        echo "## 1a2. 记录的分歧：按标准判定 cxx 正确（\`doc/fj-divergences.md\`）"
        echo
        echo "| 诊断 | 测试 |"
        echo "|---|---|"
        awk -F'\t' '{printf "| `%s` | `%s` |\n", $1, $2}' "$WORK/compile.divergence" |
            sed "s|$HOME/compiler-test-suite/||"
        echo
    fi
    echo "## 1b. 真缺陷：编译被拒（按诊断聚类）"
    echo
    if [ "$n_compile" -eq 0 ]; then
        echo "无。"
    else
        echo "\`compile\` 共 $n_compile 个；逐个重跑 cxx 取诊断后聚类："
        echo
        echo "| 数量 | 诊断 | 代表文件 |"
        echo "|---|---|---|"
        awk -F'\t' '{n[$1]++; if (!($1 in first)) first[$1]=$2} END {for (k in n) printf "%d\t%s\t%s\n", n[k], k, first[k]}' \
            "$WORK/compile.defect" | sort -rn | head -40 |
            while IFS=$'\t' read -r c msg f; do
                case $msg in
                    *"implicit declaration"*)
                        printf '| %s | `%s`（**政策类**：C23 已删除的旧形式，见 §0 与 §R81） | `%s` |\n' \
                            "$c" "$msg" "${f#$HOME/compiler-test-suite/}" ;;
                    *)
                        printf '| %s | `%s` | `%s` |\n' "$c" "$msg" "${f#$HOME/compiler-test-suite/}" ;;
                esac
            done
    fi
    echo
    if [ -s "$WORK/runtime.gap" ]; then
        echo "## 1c. 缺口：运行期失败里同样是用到了没有的特性"
        echo
        echo "| 数量 | 特性 | 代表文件 |"
        echo "|---|---|---|"
        awk -F'\t' '{n[$1]++; if (!($1 in first)) first[$1]=$3} END {for (k in n) printf "%d\t%s\t%s\n", n[k], k, first[k]}' \
            "$WORK/runtime.gap" | sort -rn |
            while IFS=$'\t' read -r c feat f; do
                printf '| %s | `%s` | `%s` |\n' "$c" "$feat" "${f#$HOME/compiler-test-suite/}"
            done
        echo
    fi
    echo "## 2. 真缺陷：运行期不一致"
    echo
    if [ -s "$WORK/output.class" ]; then
        echo "### \`output\` 的分类：谁没通过它自己的检查"
        echo
        echo "逐个用两家各编译运行一遍，数它们自己打印的 \`OK\`/\`NG\`："
        echo
        echo "| 类别 | 数量 | 读法 |"
        echo "|---|---|---|"
        printf '| `ref-ng` | %s | 参考实现自己报 NG、cxx 全 OK —— **cxx 正确，属参考分歧，只记录** |\n' \
            "$(awk '/^ref-ng/{n++} END{print n+0}' "$WORK/output.class" 2>/dev/null)"
        printf '| `cxx-ng` | %s | cxx 报 NG、参考全 OK —— **真缺陷** |\n' \
            "$(awk '/^cxx-ng/{n++} END{print n+0}' "$WORK/output.class" 2>/dev/null)"
        printf '| `plain` | %s | 两家都不打 OK/NG，打印普通数值，逐个案看 |\n' \
            "$(awk '/^plain/{n++} END{print n+0}' "$WORK/output.class" 2>/dev/null)"
        printf '| `divergence` | %s | 已在 `doc/fj-divergences.md` 记录，判定为 cxx 正确 |\n' \
            "$(awk '/^divergence/{n++} END{print n+0}' "$WORK/output.class" 2>/dev/null)"
        echo
        for cls in cxx-ng divergence ref-ng; do
            grep "^$cls" "$WORK/output.class" | cut -f2 | sed "s|$HOME/compiler-test-suite/||" | sed "s|^|  - \`|; s|\$|\`|" |
                head -12
        done
        echo
    fi
    for kind in exit output timeout; do
        n=$(count $kind)
        [ "$n" -eq 0 ] && continue
        echo "### \`$kind\`（$n）—— 按目录"
        echo
        echo '```'
        head -12 "$WORK/$kind.dirs"
        echo '```'
        echo
        echo "代表文件："
        echo
        files "$kind" | head -5 | sed "s|^|  - \`|; s|$|\`|" | sed "s|$HOME/compiler-test-suite/||"
        echo
    done
    echo "## 3. 缺口（已屏蔽，不在本阶段实现）"
    echo
    if [ "$(count gap)" -gt 0 ]; then
        grep '^gap ' "$LOG" | sed 's/^gap //; s/ | .*//' | sort | uniq -c | sort -rn |
            awk '{printf "| %s | %s |\n", $1, substr($0, index($0,$2))}' |
            { echo "| 数量 | 缺口 |"; echo "|---|---|"; cat; }
    else
        echo "无。"
    fi
    echo
    echo "## 4. 超出范围（两家参考实现也如此判定）"
    echo
    for k in skip noproto reffail refcrash reftimeout; do
        printf '| `%s` | %s |\n' "$k" "$(count $k)"
    done
    echo
    echo "## 5. 下一阶段建议的次序"
    echo
    echo "1. §1 里数量最大的那一类（真缺陷，编译被拒）—— 每类先做最小复现，再定根因。"
    echo "2. §2 的 \`exit\`/\`output\`：先按目录归并，同一目录的多个失败通常同源。"
    echo "3. §3 的缺口按 §0 政策**只记录**；要扩功能时另开阶段统一规划。"
} > "$OUT"

echo "   compile failures: $n_compile"
echo "   wrote $OUT"
