/* Linker script for running the cxx test suite on bare-metal rv32
 * (qemu-system-riscv32 -M virt). Mirrors quadmath's baremetal/link.ld. */
ENTRY(_start)

SECTIONS {
  . = 0x80000000;
  /* _start must be the very first bytes: qemu's boot ROM jumps to the
   * load base address */
  .text : { *(.text.start) *(.text) *(.text.*) }
  .rodata : { *(.rodata) *(.rodata.*) }
  .data : { *(.data) *(.data.*) }
  .bss : {
    __bss_start = .;
    *(.bss) *(.bss.*)
    __bss_end = .;
  }
  . = ALIGN(16);
  /* Top of the 128 MB virt RAM: the stack grows down, away from the
   * image. A fixed offset into RAM (the old 0x80020000) makes the stack
   * overwrite the code once the image exceeds it. */
  _stack_top = 0x88000000;
}
