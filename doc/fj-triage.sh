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
    [ -z "$msg" ] && msg="(no output at all -- cxx crashed?)"
fi
printf '%s\t%s\n' "$msg" "$f"
SH
    chmod +x "$WORK/diag.sh"
    CXX="$CXX" xargs -a "$WORK/compile.list" -d '\n' -P "$JOBS" -n 1 "$WORK/diag.sh" > "$WORK/compile.diag" 2>"$WORK/compile.err" || true
fi

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
    files output | xargs -P "$JOBS" -I{} bash -c '
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
        printf "%s\t%s\n" "$class" "$f"
        rm -rf "$d"
    ' _ "$CXX" {} >> "$WORK/output.class"
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
    echo "## 1. 真缺陷：编译被拒（按诊断聚类）"
    echo
    if [ "$n_compile" -eq 0 ]; then
        echo "无。"
    else
        echo "\`compile\` 共 $n_compile 个；逐个重跑 cxx 取诊断后聚类："
        echo
        echo "| 数量 | 诊断 | 代表文件 |"
        echo "|---|---|---|"
        awk -F'\t' '{n[$1]++; if (!($1 in first)) first[$1]=$2} END {for (k in n) printf "%d\t%s\t%s\n", n[k], k, first[k]}' \
            "$WORK/compile.diag" | sort -rn | head -40 |
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
            "$(grep -c '^ref-ng' "$WORK/output.class" 2>/dev/null || echo 0)"
        printf '| `cxx-ng` | %s | cxx 报 NG、参考全 OK —— **真缺陷** |\n' \
            "$(grep -c '^cxx-ng' "$WORK/output.class" 2>/dev/null || echo 0)"
        printf '| `plain` | %s | 两家都不打 OK/NG，打印普通数值，逐个案看 |\n' \
            "$(grep -c '^plain' "$WORK/output.class" 2>/dev/null || echo 0)"
        echo
        for cls in cxx-ng ref-ng; do
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
