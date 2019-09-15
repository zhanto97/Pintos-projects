

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH("i386")
ENTRY(start)
SECTIONS
{

  . = 0xc0000000 + 0x100000;

  _start = .;


  .text : { *(.start) *(.text) } = 0x90
  .rodata : { *(.rodata) *(.rodata.*)
       . = ALIGN(0x1000);
       _end_kernel_text = .; }
  .data : { *(.data) }


  _start_bss = .;
  .bss : { *(.bss) }
  _end_bss = .;

  _end = .;
}
