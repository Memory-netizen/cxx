#!/bin/bash
# Build FFmpeg with cxx and report how far it gets.
#
#   bash doc/ffmpeg.sh [compiler] [srcdir]
#
#   FF_SRC=~/rw/ffmpeg   the source tree (configured in place, like a user's build)
#   FF_JOBS=8            make -j
#   FF_STEP=5400         timeout for each of configure and make
#   FF_CONFIGURE=1       force a fresh configure even if one has been done
#   FF_MAKE_ARGS="V=1"   extra make variables (V=1 shows the commands)
#
# FFmpeg's configure probes the compiler hard -- it compiles and runs a test
# program per optional feature -- so running it with cxx as CC is what decides
# which code paths the build will then ask cxx to compile. That is the honest
# measurement; a tree configured by some other compiler measures that
# compiler's feature set instead.
#
# The output is a count of translation units plus the classes of diagnostic
# behind the failures, the same shape doc/realworld.sh reports. Two rounds
# went into this probe: R45 (a pointer-sign difference turned into a refusal,
# 22 -> 1997 objects) and R46/R47 (27 compiler crashes, then 0).
set -u
C=${1:-./cxx}
SRC=${2:-${FF_SRC:-$HOME/rw/ffmpeg}}
JOBS=${FF_JOBS:-8}
MAKE_ARGS=${FF_MAKE_ARGS:-}
STEP=${FF_STEP:-5400}
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1

case $C in
    /*) ;;
    *) [ -x "$C" ] && C=$root/${C#./} ;;
esac
[ -x "$C" ] || { echo "no compiler at $C" >&2; exit 2; }
[ -d "$SRC" ] || { echo "no FFmpeg source at $SRC (set FF_SRC)" >&2; exit 2; }
[ -x "$SRC/configure" ] || { echo "$SRC has no configure" >&2; exit 2; }

logs=${FF_LOGS:-$HOME/cxxwork/logs/ffmpeg}
mkdir -p "$logs" || exit 1

printf '== FFmpeg in %s with %s ==\n' "$SRC" "$C"
cd "$SRC" || exit 1

if [ ! -f ffbuild/config.mak ] || [ -n "${FF_CONFIGURE:-}" ]; then
    printf '   configure: '
    # CC= must be absolute: FFmpeg's build runs from its own directories.
    if timeout "$STEP" ./configure --cc="$C" > "$logs/configure.log" 2>&1; then
        printf 'exit 0 (log: %s)\n' "$logs/configure.log"
    else
        printf 'exit %s -- stopping here\n' "$?"
        tail -12 "$logs/configure.log"
        exit 1
    fi
fi

printf '   make -k -j%s%s: ' "$JOBS" "${MAKE_ARGS:+ $MAKE_ARGS}"
timeout "$STEP" make -k "-j$JOBS" $MAKE_ARGS > "$logs/make.log" 2>&1
printf 'exit %s (log: %s)\n' "$?" "$logs/make.log"

objects=$(find . -name '*.o' | wc -l)
crashes=$(grep -c 'killed by signal' "$logs/make.log")
printf '   objects: %s\n' "$objects"
printf '   crashes: %s\n' "$crashes"
for b in ffmpeg ffprobe ffplay; do
    [ -f "$b" ] && printf '   built: %s\n' "$b"
done

printf '   diagnostic classes:\n'
grep -E 'error:' "$logs/make.log" | sed 's/.*error: //' | sort | uniq -c | sort -rn |
    awk '{printf "      %5s  %s\n", $1, substr($0, index($0, $2))}' | head -"${FF_CLUSTER:-10}"
