#!/bin/bash
# Compile real translation units with cxx, gcc and clang and report the wall
# time and the peak resident set size of each.
#
#   bash doc/speed.sh [compiler]        # default ./cxx
#
# The sources live under $RW (default ~/rw). A translation unit is compiled
# with the flags its own build would use -- taken from `make -n`, with the
# fallbacks doc/realworld.sh needs for a tree that is already built -- and with
# the parts that measure the program rather than the compiler removed
# (optimization level, debug info, dependency generation, language mode,
# warnings), so that all three see the same job.
#
#   RW=/path           where the source trees are
#   SPEED_TU="a b"     only these workloads (by name)
#   SPEED_RUNS=3       runs per measurement; the fastest is reported
#   SPEED_MODE="fso full"  which modes to measure
#   SPEED_VERBOSE=1    print each command as it runs
#
# A measurement is /usr/bin/time around one compiler run: wall clock seconds
# and the peak RSS in kilobytes of the largest process in the tree the
# compiler forks -- cc1 and the assembler for every one of them, and for cxx
# also the clang its driver runs to turn the IR into assembly.
set -u
C=${1:-./cxx}
RW=${RW:-$HOME/rw}
RUNS=${SPEED_RUNS:-3}
MODES=${SPEED_MODE:-"fso full"}
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1
case $C in /*) ;; *) [ -x "$C" ] && C=$root/${C#./} ;; esac
C=$(cd "$(dirname "$C")" && pwd)/$(basename "$C")

tmp=$(mktemp -d /tmp/cxx-speed-XXXXXX)
trap 'rm -rf $tmp' INT TERM HUP EXIT

GCC_FLAGS="-std=c23"
CLANG_FLAGS="-std=c2y"
CXX_FLAGS=""
mode_flags() { if [ "$1" = fso ]; then printf '%s' "-fsyntax-only"; else printf '%s' "-c -o /dev/null"; fi; }

# What a project's command line carries that is set here instead, or that
# measures something else.
SPEED_EXCLUDE='^-O|^-g|^-W|^-std=|^-M|^-fstack-protector|^-fno-stack-protector|^-fdebug|^-gsplit'

# --- the flags a project's own build would use ------------------------
# project_flags <dir> <source-basename> <out>
project_flags() {
    local dir=$1 src=$2 out=$3
    : > "$out"
    [ -d "$dir" ] || return 1
    # A tree that is already built prints no recipe. The fallbacks are the
    # ones doc/realworld.sh works out -- and each is tried on its own, because
    # `make -B` on a tree with autoconf state re-runs its configure, which is
    # both slow and beside the point:
    #   * -B asks for every recipe, safe only with no config.status to remake;
    #   * -W <source> declares each source modified, which asks for the same
    #     recipes without touching anything else.
    local W="" variant
    W=$(cd "$dir" && find . -name '*.c' -printf '-W %p -W %P -W %f ' 2>/dev/null)
    for variant in plain B W; do
        case $variant in
            plain) ( cd "$dir" && make -n 2>/dev/null ) > "$tmp/make.log" 2>&1 ;;
            B)
                [ -e "$dir/config.status" ] && continue
                ( cd "$dir" && make -Bn 2>/dev/null ) > "$tmp/make.log" 2>&1
                ;;
            W) ( cd "$dir" && make -n $W 2>/dev/null ) > "$tmp/make.log" 2>&1 ;;
        esac
        extract_flags "$tmp/make.log" "$dir" "$src" "$out" && return 0
    done
    return 1
}

# extract_flags <make-log> <dir> <source-basename> <out>
extract_flags() {
    python3 - "$1" "$2" "$3" "$4" <<'PY'
import os, shlex, sys

log, base, want, out = sys.argv[1:5]
dirs = []
cur = base
found = False
for line in open(log, errors="replace"):
    line = line.rstrip("\n")
    if "Entering directory " in line:
        d = line.split("Entering directory ", 1)[1].strip()
        if len(d) > 1 and d[0] in "'\"" and d[-1] == d[0]:
            d = d[1:-1]
        dirs.append(d)
        cur = d
        continue
    if "Leaving directory " in line:
        if dirs:
            dirs.pop()
        cur = dirs[-1] if dirs else base
        continue
    if " -c " not in line:
        continue
    try:
        argv = shlex.split(line)
    except ValueError:
        continue
    src = ""
    for a in argv[1:]:
        if a.endswith(".c") and os.path.isfile(os.path.join(cur, a)):
            src = a
    if not src or os.path.basename(src) != want:
        continue
    flags, skip = [], False
    for a in argv[1:]:
        if skip:
            skip = False
            continue
        if a == "-c" or a == src or a in ("-MD", "-MMD", "-MP"):
            continue
        if a == "-o":
            skip = True
            continue
        if a.startswith("-MF"):
            continue
        flags.append(a)
    with open(out, "w") as f:
        f.write(" ".join(shlex.quote(x) for x in flags))
    found = True
    break
sys.exit(0 if found else 1)
PY
}

# --- flags cxx does not know ------------------------------------------
# A project's command line carries switches that steer code generation and the
# front end ignores. cxx refuses one it does not implement, so the ones it
# names are dropped from the workload -- and reported, because "the project's
# own flags" is part of what is being measured. They cost the front end
# nothing, so dropping them does not flatter cxx.
prune_flags() { # prune_flags <dir> <flags> <src> <dropped-out>
    local dir=$1 flags=$2 src=$3 out=$4
    : > "$out"
    local i=0
    while [ "$i" -lt 8 ]; do
        i=$((i + 1))
        local err
        err=$(cd "$dir" && "$C" $flags -w -O0 -fsyntax-only "$src" 2>&1 >/dev/null | head -1)
        case $err in
            *"unknown argument: "*)
                local bad=${err##*unknown argument: }
                bad=${bad%% *}
                printf '%s
' "$bad" >> "$out"
                # -e: the pattern starts with a dash, which grep would
                # otherwise read as an option.
                flags=$(printf '%s\n' "$flags" | tr ' ' '\n' | grep -vxF -e "$bad" | tr '\n' ' ')
                ;;
            *) break ;;
        esac
    done
    printf '%s' "$flags"
}

# --- one measurement --------------------------------------------------
# time_one <cc> <compiler-flags> <dir> <flags> <src> <mode> -> "seconds KB"
# A run that fails is not a measurement: the compiler's own status says so.
time_one() {
    local cc=$1 ccflags=$2 dir=$3 flags=$4 src=$5 mode=$6
    local mflags best_t=999999 best_m=0 out t m st
    mflags=$(mode_flags "$mode")
    [ -n "${SPEED_VERBOSE:-}" ] && printf '    %s %s %s -w -O0 %s %s\n' "$(basename "$cc")" "$ccflags" "$flags" "$mflags" "$src" >&2
    for _ in $(seq "$RUNS"); do
        # The status has to be time's, which is the compiler's: a pipeline
        # would report the status of its last command instead.
        ( cd "$dir" && /usr/bin/time -f "%e %M" "$cc" $ccflags $flags -w -O0 $mflags "$src" ) >/dev/null 2>"$tmp/run.err"
        st=$?
        [ "$st" -eq 0 ] || return 1
        out=$(grep -E '^[0-9.]+ [0-9]+$' "$tmp/run.err" | tail -1)
        t=${out%% *}
        m=${out##* }
        case $t in '' | *[!0-9.]*) return 1 ;; esac
        case $m in '' | *[!0-9]*) return 1 ;; esac
        if [ "$(echo "$t < $best_t" | bc)" = 1 ]; then
            best_t=$t
            best_m=$m
        fi
    done
    printf '%s %s' "$best_t" "$best_m"
}

# --- the workloads ----------------------------------------------------
# name <TAB> directory <TAB> source <TAB> flags (empty = from make -n)
workloads() {
    printf 'sqlite3\t%s/sqlite\tsqlite3.c\t-I.\n' "$RW"
    printf 'cpython-parser\t%s/cpython\tParser/parser.c\t\n' "$RW"
    printf 'cpython-ceval\t%s/cpython\tPython/ceval.c\t\n' "$RW"
    printf 'cpython-compile\t%s/cpython\tPython/compile.c\t\n' "$RW"
    printf 'cpython-pickle\t%s/cpython\tModules/_pickle.c\t\n' "$RW"
    printf 'ffmpeg-h264slice\t%s/ffmpeg\tlibavcodec/h264_slice.c\t\n' "$RW"
    printf 'ffmpeg-h264dec\t%s/ffmpeg\tlibavcodec/h264dec.c\t\n' "$RW"
    printf 'tinycc-tccgen\t%s/tinycc\ttccgen.c\t\n' "$RW"
    printf 'zlib-deflate\t%s/zlib\tdeflate.c\t-I. -DZ_HAVE_UNISTD_H\n' "$RW"
    printf 'lua-lparser\t%s/lua\tlparser.c\t-DLUA_USE_LINUX\n' "$RW"
}

printf 'cxx: %s\n' "$C"
printf '%s\n' "$(gcc --version | head -1)"
printf '%s\n' "$(clang --version | head -1)"
printf 'runs per measurement: %s, modes: %s, load: %s\n\n' "$RUNS" "$MODES" "$(cut -d' ' -f1-3 /proc/loadavg)"

printf '%-18s %-6s %8s %8s %8s %7s | %7s %7s %7s\n' workload mode gcc clang cxx cxx/gcc gcc clang cxx
printf '%-18s %-6s %8s %8s %8s %7s | %7s %7s %7s\n' "" "" "secs" "secs" "secs" "" "MB" "MB" "MB"

while IFS=$'\t' read -r name dir src flags; do
    if [ -n "${SPEED_TU:-}" ]; then
        case " $SPEED_TU " in *" $name "*) ;; *) continue ;; esac
    fi
    if [ ! -f "$dir/$src" ]; then
        printf '%-18s %s\n' "$name" "missing"
        continue
    fi
    if [ -z "$flags" ]; then
        if ! project_flags "$dir" "$(basename "$src")" "$tmp/flags"; then
            printf '%-18s %s\n' "$name" "no flags from make -n"
            continue
        fi
        flags=$(cat "$tmp/flags")
    fi
    flags=$(printf '%s\n' "$flags" | tr ' ' '\n' | grep -vE "$SPEED_EXCLUDE" | tr '\n' ' ')
    flags=$(prune_flags "$dir" "$flags" "$src" "$tmp/dropped")
    if [ -s "$tmp/dropped" ]; then
        printf '%-18s %s\n' "$name" "cxx does not know: $(tr '\n' ' ' < "$tmp/dropped")"
    fi
    for mode in $MODES; do
        gt=- ct=- xt=- ratio=- gm=0 cm=0 xm=0 gst=ok cst=ok xst=ok
        for spec in gcc clang cxx; do
            case $spec in
                gcc) cc=gcc; cf=$GCC_FLAGS ;;
                clang) cc=clang; cf=$CLANG_FLAGS ;;
                cxx) cc=$C; cf=$CXX_FLAGS ;;
            esac
            if r=$(time_one "$cc" "$cf" "$dir" "$flags" "$src" "$mode"); then
                case $spec in
                    gcc) gt=${r%% *}; gm=${r##* } ;;
                    clang) ct=${r%% *}; cm=${r##* } ;;
                    cxx) xt=${r%% *}; xm=${r##* } ;;
                esac
            else
                case $spec in gcc) gst=fail ;; clang) cst=fail ;; cxx) xst=fail ;; esac
            fi
        done
        case $gt in '' | *[!0-9.]*) ratio=- ;; *) ratio=$(echo "scale=2; $xt / $gt" | bc) ;; esac
        [ "$gst$cst$xst" = okokok ] || ratio="*"
        printf '%-18s %-6s %8s %8s %8s %7s | %7s %7s %7s\n' "$name" "$mode" \
            "$gt$([ "$gst" = fail ] && echo '!' || true)" \
            "$ct$([ "$cst" = fail ] && echo '!' || true)" \
            "$xt$([ "$xst" = fail ] && echo '!' || true)" \
            "$ratio" "$((gm / 1024))" "$((cm / 1024))" "$((xm / 1024))"
    done
done < <(workloads)
