OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)
/* SEARCH_DIR("/usr/arm-linux-gnueabi/lib"); */

MEMORY {
	ram (!rx) : o = 0x2ffc2500, l = 100k 
}

SECTIONS {
  .text : { 
	*( .text) *( .text.*) 
	_MOD_TABLE_ = .;
	KEEP(*(.modtab))
	LONG(0)
  } >ram 
  .rodata ALIGN(4) : { *(.rodata) *(.rodata.*) } >ram 
  . = ALIGN(8); 
  _rodata_end = .;
  .data ALIGN(4) : { _data = . ; *(.data); _edata = . ;  } >ram

  .bss : ALIGN(16)
  {
   _bss_start = .;
   *(.dynbss)
   *(.bss .bss.* .gnu.linkonce.b.*)
   *(COMMON)
   _bss_end = . ;
  } >ram
  . = ALIGN(4);
  _end = .;
  end = .;
  /DISCARD/ : { *(.note.GNU-stack) *(.gnu_debuglink) }
  .dbgid 0 : { *(.dbgid); } 
}

