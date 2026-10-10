#!/bin/bash
# Turn the raw run into doc/perf-results.txt plus the summary numbers the
# report quotes, so every figure in doc/perf.md comes from one place.
set -u
cd "$(dirname "$0")/.." || exit 1
: # results file is now tracked in doc/

python3 - <<'PY'
import math, re

rows = []
for line in open("doc/perf-results.txt"):
    f = line.split()
    if len(f) != 10 or f[0] in ("workload",) or not f[1] in ("fso", "full"):
        continue
    name, mode = f[0], f[1]
    try:
        g, c, x = float(f[2]), float(f[3]), float(f[4])
        gm, cm, xm = int(f[7]), int(f[8]), int(f[9])
    except ValueError:
        continue
    if f[2].endswith("!") or f[3].endswith("!") or f[4].endswith("!"):
        continue
    rows.append((name, mode, g, c, x, gm, cm, xm))


def geo(vals):
    return math.exp(sum(math.log(v) for v in vals) / len(vals))


for mode in ("fso", "full"):
    sel = [r for r in rows if r[1] == mode]
    if not sel:
        continue
    print(f"== {mode}: {len(sel)} workloads")
    print(f"   time   cxx/gcc  geomean {geo([r[4]/r[2] for r in sel]):5.2f}   "
          f"worst {max(r[4]/r[2] for r in sel):5.2f} ({max(sel, key=lambda r: r[4]/r[2])[0]})")
    print(f"   time   cxx/clang geomean {geo([r[4]/r[3] for r in sel]):5.2f}   "
          f"worst {max(r[4]/r[3] for r in sel):5.2f} ({max(sel, key=lambda r: r[4]/r[3])[0]})")
    print(f"   memory cxx/gcc  geomean {geo([r[7]/r[5] for r in sel]):5.2f}   "
          f"worst {max(r[7]/r[5] for r in sel):5.2f} ({max(sel, key=lambda r: r[7]/r[5])[0]})")
    print(f"   memory cxx/clang geomean {geo([r[7]/r[6] for r in sel]):5.2f}")
    print(f"   largest cxx RSS {max(r[7] for r in sel)} MB on {max(sel, key=lambda r: r[7])[0]}")

sql = [r for r in rows if r[0] == "sqlite3"]
if sql:
    for name, mode, g, c, x, gm, cm, xm in sql:
        print(f"== sqlite3 {mode}: 250k lines -> cxx {250/x:.0f}k lines/s, gcc {250/g:.0f}k, clang {250/c:.0f}k")

# The startup cost, from the smallest file in the set.
small = min(rows, key=lambda r: r[2] if r[1] == "fso" else 9e9)
print(f"== smallest fso workload {small[0]}: gcc {small[2]}s clang {small[3]}s cxx {small[4]}s")
PY
