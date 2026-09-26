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
  _stack_top = 0x80020000; /* 128 KB into the virt machine RAM */
}
