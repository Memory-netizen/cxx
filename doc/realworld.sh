#!/bin/bash
# Compile real C projects with cxx and report, per project, how many
# translation units it accepts.
#
#   bash doc/realworld.sh [compiler]
#
# Sources live under $RW (default ~/rw); a project that is not there is
# reported as absent, so the probe runs anywhere. Each project is built the way
# its own build system would: the flags come from `make -n`, and a configure
# step is run first where the project needs one (with the host compiler, so
# that feature detection is honest). The point of the probe is the number, and
# the diagnostic classes behind the ones that fail.
set -u
C=${1:-./cxx}
RW=${RW:-$HOME/rw}
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1

tmp=$(mktemp -d /tmp/cxx-realworld-XXXXXX)
trap 'rm -rf $tmp' INT TERM HUP EXIT

# --- one driver ------------------------------------------------------
# compile_set <name> <dir> <flags...>: every *.c in <dir>
# compile_cmds <name> <dir>: the compile commands <dir>'s makefile would run
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

compile_one() { # compile_one <src> <flags...>
    local src=$1
    shift
    timeout 300 "$C" -w "$@" -c -o /dev/null "$src" 2>> "$log"
}

report() { # report <name>
    local name=$1
    printf '  %-9s %2d ok, %2d failed' "$name" "$ok" "$bad"
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
        if compile_one "$f" -I"$dir" "$@"; then verdict ok; else verdict no "$f"; fi
    done
    report "$name"
}

# The flags a project's own build uses, per translation unit. Taking them from
# `make -n` is what keeps the probe honest: it is the project's build, with
# only the compiler swapped.
# <prepare> is "prepare" when the project needs a host build first: its
# makefile generates headers (git's command-list.h, version-def.h) that the
# sources include, and `make -n` does not create them.
project_makefile() { # project_makefile <name> <dir> <cap> <prepare?>
    local name=$1 dir=$2 cap=${3:-0} prepare=${4:-}
    if [ ! -d "$dir" ]; then
        printf '  %-9s not present\n' "$name"
        return
    fi
    make_n() { (cd "$dir" && make -n 2>/dev/null) | grep -E ' -c ' > "$tmp/$name.cmds" || true; }
    if [ ! -f "$dir/Makefile" ] && [ ! -f "$dir/makefile" ] && [ -x "$dir/configure" ]; then
        (cd "$dir" && ./configure > "$tmp/$name.configure" 2>&1) ||
            { printf '  %-9s configure failed\n' "$name"; return; }
    fi
    make_n
    if [ ! -s "$tmp/$name.cmds" ] && [ -x "$dir/configure" ]; then
        # A makefile that only works once it is configured (tinycc's needs
        # config.mak) reports nothing until configure has run.
        (cd "$dir" && ./configure > "$tmp/$name.configure" 2>&1) ||
            { printf '  %-9s configure failed\n' "$name"; return; }
        make_n
    fi
    if [ "$prepare" = prepare ]; then
        # The host compiler builds it once, which is what creates the
        # generated headers, and only then can make -n name every flag.
        (cd "$dir" && make -j8 > "$tmp/$name.hostbuild" 2>&1)
        make_n
    fi
    if [ ! -s "$tmp/$name.cmds" ]; then
        printf '  %-9s no compile commands from make -n\n' "$name"
        return
    fi
    start "$name"
    local line src flags n=0
    while read -r line; do
        src=$(echo "$line" | grep -oE '[A-Za-z0-9_./-]+\.c\b' | tail -1)
        [ -n "$src" ] || continue
        [ -f "$dir/$src" ] || continue
        n=$((n + 1))
        [ "$cap" -gt 0 ] && [ "$n" -gt "$cap" ] && break
        flags=$(echo "$line" | grep -oE '(-D[A-Za-z_0-9]+|-[DI][^ ]+|--std=[^ ]+|-std=[^ ]+)' | tr '\n' ' ')
        if compile_one "$dir/$src" -I"$dir" $flags; then verdict ok; else verdict no "$src"; fi
    done < "$tmp/$name.cmds"
    report "$name"
}

# --- the projects ----------------------------------------------------

echo "== real projects under $RW =="
project_flags lua            "$RW/lua" -std=c99 -DLUA_USE_LINUX
project_flags zlib           "$RW/zlib" -DZ_HAVE_UNISTD_H

if [ -d "$RW/libpng" ]; then
    # The tree ships the configuration header the build would generate.
    [ -f "$RW/libpng/pnglibconf.h" ] ||
        cp "$RW/libpng/pnglibconf.h.prebuilt" "$RW/libpng/pnglibconf.h" 2>/dev/null
fi
project_flags libpng         "$RW/libpng"

if [ -f "$RW/sqlite/sqlite3.c" ]; then
    start sqlite
    if compile_one "$RW/sqlite/sqlite3.c" -I"$RW/sqlite"; then verdict ok; else verdict no sqlite3.c; fi
    report sqlite
elif [ -f "$RW/sqlite3.c" ]; then
    start sqlite
    if compile_one "$RW/sqlite3.c" -I"$RW"; then verdict ok; else verdict no sqlite3.c; fi
    report sqlite
else
    printf '  %-9s not present\n' sqlite
fi

# tinycc and git take their flags from their own configure + make.
project_makefile tinycc  "$RW/tinycc" 40
project_makefile git     "$RW/git" 60 prepare
project_makefile cpython "$RW/cpython" 60 prepare

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
exit 0
