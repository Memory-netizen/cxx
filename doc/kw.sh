#!/bin/bash
# Compare the draft's keyword list (6.4.2) with cxx's keyword table.
cd /home/memory/cxx || exit 1
python3 - <<'PY'
import re, subprocess

# --- draft 6.4.2 keyword list -----------------------------------------
txt = open("/home/memory/cxx/doc/n3685.txt", encoding="utf-8").read().splitlines()
block = "\n".join(txt[5003:5015])          # the `keyword: one of` grid
draft = set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", block))
draft -= {"keyword", "one", "of"}
# Table 6.1 alternative spellings
alt = set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*",
                     "\n".join(txt[5024:5032])))
alt -= {"Keyword", "Alternative", "Spelling", "alignas", "alignof", "bool",
        "static_assert", "thread_local"}

# --- cxx keyword table ------------------------------------------------
src = open("src/lexer.c", encoding="utf-8").read()
body = src[src.index("void convert_keywords"):src.index("void convert_keywords") + 4000]
cxx = set(re.findall(r'\{"([A-Za-z_][A-Za-z0-9_]*)",', body))
# the table is written as {"alignas", TK_ALIGNAS}, ... -- some entries may
# use a different spacing; fall back to a looser scan of the same region
cxx |= set(re.findall(r'"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*TK_', body))

print("draft keywords      :", len(draft))
print("draft alt spellings :", len(alt))
print("cxx keywords        :", len(cxx))
print()
print("in draft, missing from cxx :", sorted(draft - cxx))
print("in cxx, not in draft (incl. GNU/ext):", sorted(cxx - draft - alt))
print("alt spellings present in cxx:", sorted(alt & cxx), "missing:", sorted(alt - cxx))
PY
