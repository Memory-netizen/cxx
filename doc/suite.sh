#!/bin/bash
source ~/.profile >/dev/null 2>&1
cd /home/memory/cxx || exit 1
echo "=== make test ==="
make test 2>&1 | grep -E '^(conformance|c2y):'
echo "=== cross targets ==="
for tgt in arm64 rv64 rv32; do make test-$tgt 2>&1 | tail -1; done
