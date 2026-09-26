#!/bin/sh
# Run the cxx test suite on bare-metal rv32 (qemu-system-riscv32).
#
# Per test: cxx -target rv32bare -S -> assemble -> link against the
# freestanding mini-libc (rv32_common.c), the fp128 soft-float shims
# (rv32_tf3.c) and the vendored fp128/int128 library -> run under qemu.
# The guest writes the SiFive test device (0x100000): 0x5555 = pass,
# 0x3333 | (code << 16) = fail, which qemu turns into its exit code.
#
# tls.c is skipped: it needs pthread.h from a real (g)libc, which the
# cross toolchain does not ship for rv32.

set -u
cd "$(dirname "$0")/.."

if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
  TOOL=riscv64-linux-gnu-gcc
elif command -v gcc >/dev/null 2>&1 && [ "$(uname -m)" = "riscv64" ]; then
  TOOL=gcc
else
  echo "no riscv64 cross gcc found; cannot run rv32 tests"
  exit 1
fi
if ! command -v qemu-system-riscv32 >/dev/null 2>&1; then
  echo "qemu-system-riscv32 not found"
  exit 1
fi

march=rv32imafdc
mabi=ilp32d

work=$(mktemp -d /tmp/cxx-rv32.XXXXXX)
trap 'rm -rf "$work"' EXIT

# -nostdinc keeps glibc headers out; re-add the compiler's own
# freestanding headers (stdint.h, stdbool.h, stddef.h, stdarg.h).
gccinc="$("$TOOL" -print-file-name=include)"
common_flags="-std=c11 -O2 -ffreestanding -fno-stack-protector -fno-pie -fno-pic -mcmodel=medany -march=$march -mabi=$mabi -nostdlib -nostdinc -I. -Itest -I$gccinc"

echo "== building freestanding support =="
for src in test/rv32_common.c test/rv32_tf3.c src/support/fp128.c src/support/int128.c; do
  obj="$work/$(basename "$src" .c).o"
  if ! $TOOL $common_flags -c "$src" -o "$obj" 2>"$work/support.err"; then
    echo "BUILD-FAIL $src (see $work/support.err)"
    exit 1
  fi
done

passed=0
failed=0
skipped=0

echo "== running tests =="
for t in test/*.c; do
  b=$(basename "$t" .c)
  if [ "$b" = "tls" ]; then
    echo "SKIP $b (needs pthread.h)"
    skipped=$((skipped + 1))
    continue
  fi
  case "$b" in
    rv32_common|rv32_tf3) continue;;  # harness, not tests
  esac
  if ! ./cxx -target rv32bare -S -Itest -o "$work/$b.s" "$t" 2>"$work/$b.cerr"; then
    echo "COMPILE-FAIL $b"
    failed=$((failed + 1))
    continue
  fi
  if ! $TOOL -march=$march -mabi=$mabi -c "$work/$b.s" -o "$work/$b.o" 2>"$work/$b.aerr"; then
    echo "ASSEMBLE-FAIL $b"
    failed=$((failed + 1))
    continue
  fi
  if ! $TOOL -march=$march -mabi=$mabi -nostdlib -no-pie -Wl,--build-id=none \
       -T test/rv32_link.ld "$work/rv32_common.o" "$work/rv32_tf3.o" \
       "$work/fp128.o" "$work/int128.o" "$work/$b.o" -o "$work/$b.elf" \
       2>"$work/$b.lerr"; then
    echo "LINK-FAIL $b (see $work/$b.lerr)"
    failed=$((failed + 1))
    continue
  fi
  rc=0
  # stdin must NOT be a terminal: `timeout` runs qemu in a new process
  # group and qemu's stdio chardev would raise SIGTTOU (symptom: empty
  # log, qemu exit 124). </dev/null avoids all tty handling.
  timeout 60 qemu-system-riscv32 -M virt -bios none -kernel "$work/$b.elf" \
      -display none -monitor none -serial stdio \
      < /dev/null > "$work/$b.log" 2>&1 || rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "FAIL $b (qemu exit $rc)"
    tail -5 "$work/$b.log"
    failed=$((failed + 1))
  else
    echo "PASS $b"
    passed=$((passed + 1))
  fi
done

echo "== rv32 bare-metal: $passed passed, $failed failed, $skipped skipped =="
[ "$failed" -eq 0 ]
