#!/bin/bash
# How much real code would cxx's no-prototype policy reject?
#
#   bash doc/noproto.sh [name:dir ...]
#
# cxx reads `f()` as if it were `f(void)` in every C version (section 0,
# R56/R57): a call through a declaration without a prototype is an error, and a
# K&R definition is not a definition at all. Both references accept the first
# in C17 (clang warns, gcc is silent) and neither has the second since C23.
#
# The census is taken by the reference compiler rather than by a regex: `f()`
# with no arguments is perfectly good code, and only a declaration *without a
# prototype* that is then *called with arguments* is the unsupported form.
# clang has one switch for both. Each translation unit is compiled the way the
# project's own build would compile it -- the commands come from `make -n`, so
# the flags are the project's own -- with clang as the compiler and
# -Werror=deprecated-non-prototype added. A unit that fails carries that
# diagnostic; anything else is counted apart.
#
#   CENSUS_CC=clang        the reference compiler
#   CENSUS_JOBS=8          units compiled at once
#   CENSUS_WORK=...        where the raw make output and per-unit results go
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
CC=${CENSUS_CC:-clang}
JOBS=${CENSUS_JOBS:-8}
WORK=${CENSUS_WORK:-$HOME/cxxwork/logs/noproto}
mkdir -p "$WORK"

# The compile commands a project's own build would run: <dir> \t <src> \t <flags>.
collect() { # collect <raw> <base> <out>
    python3 - "$1" "$2" "$3" <<'PY'
import os, shlex, sys

raw, base, out = sys.argv[1], sys.argv[2], sys.argv[3]
dirs, seen, rows = [], set(), []
cur = base
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
        flags, i = [], 1
        while i < len(argv):
            a = argv[i]
            if a in ("-I", "-D", "-U", "-include", "-isystem", "-iquote", "-idirafter") and i + 1 < len(argv):
                flags.append(a + argv[i + 1])
                i += 2
                continue
            if a.startswith(("-I", "-D", "-U", "-std=", "--std=")):
                flags.append(a)
            i += 1
        rows.append("%s\t%s\t%s" % (cur, src, "\x1f".join(flags)))
with open(out, "w") as f:
    for r in rows:
        f.write(r + "\n")
PY
}

# The per-unit worker, so the loop can run under xargs.
cat > "$WORK/one.sh" <<'SH'
#!/bin/bash
# <dir> \t <src> \t <flags joined by 0x1f>
IFS=$'\t' read -r dir src flags <<< "$1"
args=()
if [ -n "$flags" ]; then
    IFS=$'\x1f' read -r -a args <<< "$flags"
fi
log=$(mktemp)
# No -Wno-everything here: it would disable the very diagnostic being counted
# (-Werror= sets a severity, it does not re-enable what is off), and the
# positive control caught that.
( cd "$dir" && timeout 300 "$CENSUS_CC" -Werror=deprecated-non-prototype \
    ${args+"${args[@]}"} -c -o /dev/null "$src" ) > "$log" 2>&1
rc=$?
if [ $rc -eq 0 ]; then
    echo "clean"
elif grep -q 'without a prototype' "$log"; then
    if grep -q 'definition without a prototype\|function definition without' "$log"; then
        echo "kr"
    else
        echo "noproto"
    fi
else
    echo "other"
fi
rm -f "$log"
SH
chmod +x "$WORK/one.sh"

run() { # run <name> <dir> <prepare>
    local name=$1 dir=$2 prep=$3
    if [ ! -d "$dir" ]; then
        printf '  %-9s not present\n' "$name"
        return
    fi
    local raw=$WORK/$name.raw cmds=$WORK/$name.cmds
    if [ "$prep" != "-" ]; then
        (cd "$dir" && eval "$prep") > "$WORK/$name.prepare" 2>&1 ||
            { printf '  %-9s prepare failed (%s)\n' "$name" "$WORK/$name.prepare"; return; }
    fi
    (cd "$dir" && make -n 2>/dev/null) > "$raw"
    collect "$raw" "$dir" "$cmds"
    if [ ! -s "$cmds" ]; then
        (cd "$dir" && make -Bn 2>/dev/null) > "$raw"
        collect "$raw" "$dir" "$cmds"
    fi
    # zlib and libpng ship a configure script and no usable makefile until it
    # has run (the realworld probe drives those two with a wildcard instead,
    # but a census wants the project's own flags).
    if [ ! -s "$cmds" ] && [ -x "$dir/configure" ]; then
        (cd "$dir" && ./configure) > "$WORK/$name.configure" 2>&1 ||
            { printf '  %-9s configure failed (%s)\n' "$name" "$WORK/$name.configure"; return; }
        (cd "$dir" && make -n 2>/dev/null) > "$raw"
        collect "$raw" "$dir" "$cmds"
    fi
    # A tree with no makefile at all -- the compiler test suite is one -- is
    # censused file by file, with only its own directory on the include path.
    # This is also the positive control: the suite is full of the two forms.
    if [ ! -s "$cmds" ]; then
        (cd "$dir" && find . -name '*.c' | sed 's|^\./||') | while read -r src; do
            printf '%s\t%s\t%s\n' "$dir" "$src" "-I$dir"
        done > "$cmds"
    fi
    local total
    total=$(wc -l < "$cmds")
    if [ "$total" -eq 0 ]; then
        printf '  %-9s no compile commands from make -n\n' "$name"
        return
    fi
    export CENSUS_CC=$CC
    xargs -a "$cmds" -d '\n' -P "$JOBS" -n 1 "$WORK/one.sh" > "$WORK/$name.results" 2>"$WORK/$name.xargs" || true
    local clean np kr other
    clean=$(grep -c '^clean$' "$WORK/$name.results" || true)
    np=$(grep -c '^noproto$' "$WORK/$name.results" || true)
    kr=$(grep -c '^kr$' "$WORK/$name.results" || true)
    other=$(grep -c '^other$' "$WORK/$name.results" || true)
    printf '  %-9s units=%-6s clean=%-6s rejected by the policy=%-5s (K&R %s + call form %s)  other=%-5s\n' \
        "$name" "$total" "$clean" "$((np + kr))" "$kr" "$np" "$other"
}

# The project list. A leading "name:dir" argument replaces it entirely.
if [ $# -gt 0 ]; then
    for spec in "$@"; do
        name=${spec%%:*}
        rest=${spec#*:}
        case $rest in
            *:*) run "$name" "${rest%%:*}" "${rest#*:}" ;;
            *) run "$name" "$rest" "-" ;;
        esac
    done
    exit 0
fi

echo "== no-prototype census with $CC, $JOBS at a time (raw output in $WORK)"
while IFS=$'\t' read -r name dir prep; do
    run "$name" "$dir" "$prep"
done <<EOF
busybox	$HOME/rw/census/busybox	make defconfig
curl	$HOME/rw/census/curl	./configure --without-ssl --without-zlib --without-libpsl --disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-mqtt --without-brotli --without-zstd
redis	$HOME/rw/census/redis	-
nginx	$HOME/rw/census/nginx	./configure --without-http_rewrite_module --without-http_gzip_module
lua	$HOME/rw/lua	-
zlib	$HOME/rw/zlib	-
libpng	$HOME/rw/libpng	-
tinycc	$HOME/rw/tinycc	-
git	$HOME/rw/git	-
ffmpeg	$HOME/rw/ffmpeg	-
cpython	$HOME/rw/cpython	-
EOF
