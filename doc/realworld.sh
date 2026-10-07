#!/bin/bash
# Compile real C projects with cxx and report, per project, how many
# translation units it accepts.
#
#   bash doc/realworld.sh [compiler]
#
# Sources live under $RW (default ~/rw); a project that is not there is
# reported as absent, so the probe runs anywhere. Each project is built the way
# its own build system would: the commands come from `make -n`, and a configure
# step is run first where the project needs one (with the host compiler, so
# that feature detection is honest). The point of the probe is the number, and
# the diagnostic classes behind the ones that fail.
#
#   RW=/path        where the source trees are
#   RW_ONLY="git"   only these projects
#   RW_CAP=8        at most this many translation units per project
#   RW_CLUSTER=10   how many diagnostic classes to show per project
set -u
C=${1:-./cxx}
RW=${RW:-$HOME/rw}
CLUSTER=${RW_CLUSTER:-2}
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1

# Every compile runs with the project's directory as the working directory,
# which is what its own build does, so the compiler has to be reachable from
# there.
case $C in
    /*) ;;
    *) [ -x "$C" ] && C=$root/${C#./} ;;
esac

tmp=$(mktemp -d /tmp/cxx-realworld-XXXXXX)
trap 'rm -rf $tmp' INT TERM HUP EXIT

want() { # want <name>
    [ -z "${RW_ONLY:-}" ] && return 0
    case " $RW_ONLY " in *" $1 "*) return 0 ;; esac
    return 1
}

# --- one driver ------------------------------------------------------
ok=0
bad=0
failed_list=""

verdict() { # verdict <name> <file> <log>
    if [ "$1" = ok ]; then
        ok=$((ok + 1))
    else
        bad=$((bad + 1))
        failed_list="$failed_list $2"
    fi
}

compile_one() { # compile_one <dir> <src> <flags...>
    local dir=$1 src=$2
    shift 2
    (cd "$dir" && timeout 300 "$C" -w "$@" -c -o /dev/null "$src") 2>> "$log"
}

report() { # report <name>
    local name=$1
    printf '  %-9s %3d ok, %3d failed' "$name" "$ok" "$bad"
    if [ "$bad" -gt 0 ] && [ -s "$log" ]; then
        printf '  | '
        grep -o 'error: .*' "$log" | sed 's/error: //' | sed "s/‘[^’]*’/‘X’/g" |
            cut -c1-42 | sort | uniq -c | sort -rn | head -2 |
            awk '{printf "%s x%s  ", substr($0, index($0,$2)), $1}'
    fi
    echo
}

start() { # start <name>
    ok=0
    bad=0
    log=$tmp/$1.log
    : > "$log"
}

# --- per project -----------------------------------------------------

project_flags() { # project_flags <name> <dir> <flags...>
    local name=$1 dir=$2
    shift 2
    if [ ! -d "$dir" ]; then
        printf '  %-9s not present\n' "$name"
        return
    fi
    start "$name"
    local f
    for f in "$dir"/*.c; do
        if compile_one "$dir" "$f" -I"$dir" "$@"; then verdict ok; else verdict no "$f"; fi
    done
    report "$name"
}

# The commands a project's own build would run, one translation unit each, as
# `<directory><TAB><source><TAB><flags>`. Each is compiled in the directory
# make would have run it in: the recursive makes appear in the output as
# `Entering directory`, and tinycc's lib/ commands only make sense there. The
# recipe text is shell-quoted -- `-DTCC_GITHASH="\"2026-10-03 mob@43c7708\""`
# is one word -- so the words are split with shlex, which is the only splitter
# here that agrees with the shell make runs the recipe with.
collect_cmds() { # collect_cmds <raw> <dir> <cap> <out>
    python3 - "$1" "$2" "$3" "$4" <<'PY'
import os, shlex, sys

raw, base, cap, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
dirs, seen, rows = [], set(), []
cur = base
n = 0

with open(raw, errors="replace") as f:
    for line in f:
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
        if not src:
            continue
        key = os.path.join(cur, src)
        if key in seen:
            continue
        seen.add(key)
        n += 1
        if cap and n > cap:
            break
        flags, i = [], 1
        while i < len(argv):
            a = argv[i]
            if a in ("-I", "-D") and i + 1 < len(argv):
                flags.append(a + argv[i + 1])
                i += 2
                continue
            if a.startswith(("-I", "-D", "-std=", "--std=")):
                flags.append(a)
            i += 1
        rows.append("%s\t%s\t%s" % (cur, src, "\x1f".join(flags)))

with open(out, "w") as f:
    for r in rows:
        f.write(r + "\n")
PY
}

# The flags a project's own build uses, per translation unit. Taking them from
# `make -n` is what keeps the probe honest: it is the project's build, with
# only the compiler swapped.
#
# Three adjustments are needed to get commands out of a tree in any state:
#
#   - Generated makefiles are declared up to date (-o). cpython's Makefile
#     remakes config.status from `configure`, and a snapshot whose configure
#     was regenerated fails when re-run; the tree is configured already.
#   - A tree with nothing left to do prints no recipe at all, so every source
#     is declared modified (-W), which is what asks for one.
#   - A tree whose makefile cannot be read yet -- tinycc's needs the config.mak
#     its configure writes -- has to be configured first.
#
# <prepare> is "prepare" when the project needs a host build first: its
# makefile generates headers (git's command-list.h, cpython's
# Python/frozen_modules) that the sources include and `make -n` does not
# create.
project_makefile() { # project_makefile <name> <dir> <cap> <prepare?>
    local name=$1 dir=$2 cap=${RW_CAP:-${3:-0}} prepare=${4:-}
    if [ ! -d "$dir" ]; then
        printf '  %-9s not present\n' "$name"
        return
    fi
    local old="" f
    for f in configure config.status Makefile makefile; do
        [ -e "$dir/$f" ] && old="$old -o $f"
    done
    local raw=$tmp/$name.raw cmds=$tmp/$name.cmds

    configure_it() {
        (cd "$dir" && ./configure > "$tmp/$name.configure" 2>&1) ||
            { printf '  %-9s configure failed\n' "$name"; return 1; }
    }
    collect() { collect_cmds "$raw" "$dir" "$cap" "$cmds"; }
    run_make() {
        (cd "$dir" && make -n $old 2>/dev/null) > "$raw"
        collect
        [ -s "$cmds" ] && return 0
        # Nothing left to do, so make prints no recipe at all. -B asks for
        # every one of them, and it is also the only way to reach a sub-make's
        # commands: a tree with no autoconf state to protect is asked that way
        # (tinycc's lib/ recipes appear only under it).
        if [ ! -e "$dir/config.status" ]; then
            (cd "$dir" && make -Bn $old 2>/dev/null) > "$raw"
            collect
            [ -s "$cmds" ] && return 0
        fi
        # cpython's Makefile remakes config.status from `configure`, so -B
        # would re-run it -- and a snapshot whose configure was regenerated
        # exits 2. Declaring each source modified asks for the same recipes
        # without touching anything else.
        # `-W` takes the name as the makefile spells it, and a recursive make
        # spells it relative to its own directory, so each source is named
        # three ways; only the one that matches a prerequisite counts.
        local W
        W=$(cd "$dir" && find . -name '*.c' -printf '-W %p -W %P -W %f ' 2>/dev/null)
        (cd "$dir" && make -n $old $W 2>/dev/null) > "$raw"
        collect
    }

    if [ ! -e "$dir/Makefile" ] && [ ! -e "$dir/makefile" ] && [ -x "$dir/configure" ]; then
        configure_it || return
    fi
    run_make
    if [ ! -s "$cmds" ] && [ -x "$dir/configure" ] && [ ! -e "$dir/config.status" ]; then
        configure_it || return
        run_make
    fi
    if [ "$prepare" = prepare ]; then
        # The host compiler builds it once, which is what creates the
        # generated headers, and only then can make -n name every flag.
        if ! (cd "$dir" && make -j"$(nproc)" $old > "$tmp/$name.hostbuild" 2>&1); then
            printf '  %-9s note: host build incomplete, generated headers may be missing\n' "$name" >&2
        fi
        run_make
    fi
    if [ ! -s "$cmds" ]; then
        printf '  %-9s no compile commands from make -n\n' "$name"
        return
    fi

    start "$name"
    local cdir src flagstr n=0
    while IFS=$'\t' read -r cdir src flagstr; do
        [ -n "$cdir" ] && [ -n "$src" ] || continue
        n=$((n + 1))
        local fargs=()
        [ -n "$flagstr" ] && IFS=$'\x1f' read -r -a fargs <<< "$flagstr"
        if compile_one "$cdir" "$src" ${fargs[@]+"${fargs[@]}"}; then verdict ok; else verdict no "$src"; fi
    done < "$cmds"
    if [ "$n" -eq 0 ]; then
        printf '  %-9s no translation units found\n' "$name"
        return
    fi
    report "$name"
}

# --- the projects ----------------------------------------------------

echo "== real projects under $RW =="
want lua && project_flags lua       "$RW/lua" -std=c99 -DLUA_USE_LINUX
want zlib && project_flags zlib     "$RW/zlib" -DZ_HAVE_UNISTD_H

if want libpng && [ -d "$RW/libpng" ]; then
    # The tree ships the configuration header the build would generate.
    [ -f "$RW/libpng/pnglibconf.h" ] ||
        cp "$RW/libpng/pnglibconf.h.prebuilt" "$RW/libpng/pnglibconf.h" 2>/dev/null
fi
want libpng && project_flags libpng "$RW/libpng"

if want sqlite; then
    if [ -f "$RW/sqlite/sqlite3.c" ]; then
        start sqlite
        if compile_one "$RW/sqlite" "$RW/sqlite/sqlite3.c" -I"$RW/sqlite"; then verdict ok; else verdict no sqlite3.c; fi
        report sqlite
    elif [ -f "$RW/sqlite3.c" ]; then
        start sqlite
        if compile_one "$RW" "$RW/sqlite3.c" -I"$RW"; then verdict ok; else verdict no sqlite3.c; fi
        report sqlite
    else
        printf '  %-9s not present\n' sqlite
    fi
fi

# The projects whose flags come from their own configure + make.
want tinycc  && project_makefile tinycc  "$RW/tinycc"  0 prepare
want git     && project_makefile git     "$RW/git"     0 prepare
want cpython && project_makefile cpython "$RW/cpython" 0 prepare

if [ "$bad" -gt 0 ]; then
    echo
    echo "== first failures (up to 8) =="
    n=0
    for f in $failed_list; do
        [ "$n" -ge 8 ] && break
        n=$((n + 1))
        first=$(grep -m1 -F "$(basename "$f"):" "$tmp"/*.log 2>/dev/null | head -1 | sed 's/.*error: //' | cut -c1-56)
        printf '  %-18s %s\n' "$(basename "$f")" "$first"
    done
fi

# The classes behind every failure, not just the two the table has room for:
# what a round works on next is chosen from here.
if [ "$CLUSTER" -gt 2 ]; then
    echo
    echo "== diagnostic classes (top $CLUSTER) =="
    for name in lua zlib libpng sqlite tinycc git cpython; do
        [ -s "$tmp/$name.log" ] || continue
        echo "  --- $name"
        grep -o 'error: .*' "$tmp/$name.log" | sed 's/error: //' | sed "s/‘[^’]*’/‘X’/g" |
            cut -c1-64 | sort | uniq -c | sort -rn | head -"$CLUSTER" |
            awk '{printf "      %4s  %s\n", $1, substr($0, index($0,$2))}'
    done
fi
exit 0
