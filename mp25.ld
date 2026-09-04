INCLUDE LinkerScript_EL3_EL1_common.ld

ENTRY(_ResetA35)

/* Select Memory in which code has to be placed */
/* and offset of code from start of this memory */
REGION_ALIAS("RAM", SYSRAM_EL3_LAUNCHER);

/* Code Memory */
__MEM_START__ = ORIGIN(RAM);
__MEM_SIZE__  = LENGTH(RAM);

/* put the EL3 stack at the end of the memory */
EL3_STACK_BASE = __MEM_START__ + __MEM_SIZE__;

SECTIONS
{
  /* Code starts with the vector table, the bootcode, then the
   * remainder of the code */
  . = __MEM_START__;
  .text.el3 . : {
        KEEP (* (RESET))
        _sboot2.o(.text.early)
        _sboot2.o(.text)
        _sboot2.o(.text*)
        _sboot2.o(.rodata*)
   } >RAM
  .data.el3 ALIGN(16) : {
        _sboot2.o(.data*)
   } >RAM

  . = ALIGN(16);
  __sbss_start__ = .;
  .bss.el3 . : { _sboot2.o(.bss) } >RAM
  . = ALIGN(16);
  __sbss_end__ = .;

  . = MAX(., 0x0e005000);
  .text.el1 ALIGN(4096) : {
        el1.o(.text)
        _sboot2.o(.mmu.el1)
	KEEP(*(.text.startup))
        *(.text) /* remainder of code */
	*(.text*)
	*(.rodata*)
        . = ALIGN(8);
	_MOD_TABLE_ = .;
	KEEP(*(.modtab))
	LONG(0)
	LONG(0)
   }>RAM

  /* Data */
        __data_start = .;
        .data  : { *(.data)  }>RAM
        .data1 : { *(.data1) }>RAM

  /* Uninitialized data */
  /* 16-byte alignment constraints for fitting needs of toolset based on GCC 5.3.1 */
        . = ALIGN(16);
        __bss_start__ = .;
        .bss   : { *(.bss)   }>RAM
        . = ALIGN(16);
        __bss_end__ = .;
        . = ALIGN(4);
        __end__ = .;
        PROVIDE (end = .);

  /* Notes section
   * This is not used so we discard it. Although not used it needs to be
   * explicitly mentioned in the linker script as some toolchains will place
   * the notes section at address 0 if it is not explicitly mentioned. */
  /DISCARD/ : { *(.note*) }
  .eh_frame 0 (INFO) : { KEEP(*(.eh_frame)) }

}
