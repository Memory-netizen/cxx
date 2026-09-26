#!/bin/sh
# Run the cxx test suite for one cross target under qemu user-mode:
#   test/run_cross.sh amd64 | arm64 | rv64
# (rv32 needs the bare-metal harness test/run_rv32.sh instead.)
#
# Per test: cxx -target <arch> -S -> cross gcc assembles and links with
# test/common -> qemu-<arch> -L <sysroot>. tls.c is skipped: it needs
# pthread.h from a real libc, which the cross toolchains do not ship.
#
# Missing toolchain/qemu is a SKIP (exit 0), mirroring quadmath's
# cross_verify.sh; the run summary carries the verdict otherwise.

set -u

target="$1"
case "$target" in
  amd64)
    cc=x86_64-linux-gnu-gcc
    qemu=qemu-x86_64
    sysroot=/usr/x86_64-linux-gnu
    ;;
  arm64)
    cc=aarch64-linux-gnu-gcc
    qemu=qemu-aarch64
    sysroot=/usr/aarch64-linux-gnu
    ;;
  rv64)
    cc=riscv64-linux-gnu-gcc
    qemu=qemu-riscv64
    sysroot=/usr/riscv64-linux-gnu
    ;;
  *)
    echo "usage: run_cross.sh amd64|arm64|rv64"
    exit 1
    ;;
esac

# A target matching the host machine is not cross at all: its native
# coverage is `make test`, mirroring quadmath's cross_verify.sh.
hostarch=$(uname -m)
case "$target:$hostarch" in
  amd64:x86_64|amd64:amd64|arm64:aarch64|arm64:arm64|rv64:riscv64)
    echo "== $target SKIPPED (native host, covered by make test) =="
    exit 0
    ;;
esac

command -v "$cc" >/dev/null 2>&1 || { echo "== $target SKIPPED (no $cc) =="; exit 0; }
command -v "$qemu" >/dev/null 2>&1 || { echo "== $target SKIPPED (no $qemu) =="; exit 0; }

cd "$(dirname "$0")/.."

work=$(mktemp -d /tmp/cxx-cross.XXXXXX)
trap 'rm -rf "$work"' EXIT

passed=0
failed=0

for t in test/*.c; do
  b=$(basename "$t" .c)
  case "$b" in
    tls)
      echo "SKIP $b (needs pthread.h)"
      continue
      ;;
    rv32_common|rv32_tf3)
      continue  # rv32 bare-metal harness, not tests
      ;;
  esac
  if ! ./cxx -target "$target" -S -Itest -o "$work/$b.s" "$t" 2>"$work/$b.cerr"; then
    echo "COMPILE-FAIL $b"
    failed=$((failed + 1))
    continue
  fi
  if ! "$cc" -c "$work/$b.s" -o "$work/$b.o" 2>"$work/$b.aerr"; then
    echo "ASSEMBLE-FAIL $b"
    failed=$((failed + 1))
    continue
  fi
  if ! "$cc" -o "$work/$b.elf" "$work/$b.o" -xc test/common 2>"$work/$b.lerr"; then
    echo "LINK-FAIL $b (see $work/$b.lerr)"
    failed=$((failed + 1))
    continue
  fi
  rc=0
  "$qemu" -L "$sysroot" "$work/$b.elf" > "$work/$b.log" 2>&1 || rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "FAIL $b (qemu exit $rc)"
    tail -3 "$work/$b.log"
    failed=$((failed + 1))
  else
    echo "PASS $b"
    passed=$((passed + 1))
  fi
done

echo "== $target: $passed passed, $failed failed =="
[ "$failed" -eq 0 ]
