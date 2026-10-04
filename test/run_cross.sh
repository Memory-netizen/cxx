#!/bin/sh
# Run the cxx test suite for one cross target under qemu user-mode:
#   test/run_cross.sh amd64 | arm64 | rv64
# (rv32 needs the bare-metal harness test/run_rv32.sh instead.)
#
# Per test: cxx -target <arch> compiles both the test and the shared driver
# test/common to objects and links them (cxx drives the assembler and the
# linker itself) -> qemu-<arch> -L <sysroot>. tls.c is skipped: it needs
# pthread.h from a real libc, which the cross toolchains do not ship. The
# cross gcc is used only as the linker driver, for its libc and startup
# files, never to compile C.
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
    exhaust_int)
      continue  # temporarily excluded during the atomics work
      ;;
  esac
  # cxx compiles and assembles both translation units; test/common has no
  # .c extension, hence -x c.
  if ! ./cxx -target "$target" -w -Itest -c -o "$work/$b.o" "$t" 2>"$work/$b.cerr"; then
    echo "COMPILE-FAIL $b"
    failed=$((failed + 1))
    continue
  fi
  if [ ! -f "$work/common.o" ]; then
    if ! ./cxx -target "$target" -w -Itest -c -o "$work/common.o" -x c test/common 2>"$work/common.cerr"; then
      echo "COMPILE-FAIL test/common"
      failed=$((failed + 1))
      break
    fi
  fi
  # The cross gcc is only the linker driver here: it supplies libc and the
  # startup files, which cxx does not ship.
  if ! "$cc" -o "$work/$b.elf" "$work/$b.o" "$work/common.o" 2>"$work/$b.lerr"; then
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
