#!/bin/bash
# Try to build the Linux kernel with cxx and report where it stops.
#
#   bash doc/kernel.sh [compiler] [srcdir]
#
#   K_SRC=~/rw/linux     the source tree (configured in place)
#   K_JOBS=8             make -j
#   K_STEP=3600          timeout for the build
#   K_DEFCONFIG=1        run `make defconfig` first (host gcc builds scripts/)
#
# The kernel builds its own tooling with HOSTCC (the host compiler) and only
# the target objects with CC, so this measures cxx against the kernel's target
# build. `make defconfig` runs with the host compiler for the same reason: the
# configuration step is not what is under test -- except that it is, in one
# respect, since the kernel's scripts ask $(CC) for its assembler version
# (scripts/Kconfig.include and scripts/as-version.sh), and a compiler whose
# version output they cannot read stops the build before a single .c file is
# compiled. That is where cxx stands today.
set -u
C=${1:-./cxx}
SRC=${2:-${K_SRC:-$HOME/rw/linux}}
JOBS=${K_JOBS:-8}
STEP=${K_STEP:-3600}
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1

case $C in
    /*) ;;
    *) [ -x "$C" ] && C=$root/${C#./} ;;
esac
[ -x "$C" ] || { echo "no compiler at $C" >&2; exit 2; }
[ -d "$SRC" ] || { echo "no kernel source at $SRC (set K_SRC)" >&2; exit 2; }

logs=${K_LOGS:-$HOME/cxxwork/logs/kernel}
mkdir -p "$logs" || exit 1

printf '== Linux kernel in %s with %s ==\n' "$SRC" "$C"
cd "$SRC" || exit 1

if [ ! -f .config ] || [ -n "${K_DEFCONFIG:-}" ]; then
    printf '   defconfig: '
    if timeout 900 make defconfig > "$logs/defconfig.log" 2>&1; then
        printf 'exit 0\n'
    else
        printf 'exit %s -- stopping here (log: %s)\n' "$?" "$logs/defconfig.log"
        tail -10 "$logs/defconfig.log"
        exit 1
    fi
fi

printf '   make -j%s CC=%s: ' "$JOBS" "$C"
timeout "$STEP" make "-j$JOBS" CC="$C" > "$logs/make.log" 2>&1
printf 'exit %s (log: %s)\n' "$?" "$logs/make.log"

printf '   objects: %s\n' "$(find . -name '*.o' 2>/dev/null | wc -l)"
printf '   panic messages: %s\n' "$(grep -c 'killed by signal' "$logs/make.log")"
printf '   diagnostic classes:\n'
grep -E 'error:' "$logs/make.log" | sed 's/.*error: //' | sort | uniq -c | sort -rn |
    awk '{printf "      %5s  %s\n", $1, substr($0, index($0, $2))}' | head -"${K_CLUSTER:-10}"
if ! grep -q 'error:' "$logs/make.log"; then
    printf '      (no error: lines -- the build stopped earlier; last lines)\n'
    grep -v '^  \|^$' "$logs/make.log" | tail -6 | sed 's/^/      /'
fi
