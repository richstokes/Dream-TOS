OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_dc_app_main)
SECTIONS {
  . = 0;
  .text : { *(.text .text.*) }
  . = ALIGN(4);
  .rodata : { *(.rodata .rodata.*) }
  . = ALIGN(4);
  .data : { *(.data .data.*) }
  . = ALIGN(4);
  .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
