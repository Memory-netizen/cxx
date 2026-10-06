#!/bin/bash
# Run every archived C2y probe against a freshly built cxx and print the
# one-line summary of each. Usage: bash doc/probes.sh ./cxx
cd "$(dirname "$0")/.." || exit 1
C=${1:-./cxx}
for p in c2ycov c2ycov2 c2ycov3 c2ycov5 cleanup vlaparam vmgoto asm selfhost selfbuild bootstrap; do
    printf '%-10s ' "$p"
    bash "doc/$p.sh" "$C" 2>/dev/null | tail -1
done
printf '%-10s ' kw
bash doc/kw.sh 2>/dev/null | grep 'missing from cxx'
