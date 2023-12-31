ENTRY(start)


SECTIONS
{
  KERNAL_VIRTUAL_BASE = 0xFFFFFFFF80000000;
	. = 1M;

  .boot.text : {
    KEEP(*(.multiboot_header))
    *(.boot.text)
  }

  .boot.data : {
    *(.boot.data)
    . = ALIGN(4096);
  }

  . += KERNAL_VIRTUAL_BASE;

	.text ALIGN(4096) : AT(ADDR(.text) - KERNAL_VIRTUAL_BASE) {
    _text_start_ = .;
    *(.text)
  }

  .rodata ALIGN(4096) : AT(ADDR(.rodata) - KERNAL_VIRTUAL_BASE) {
    _rodata_start_ = .;
    *(.rodata)
  }

  .data ALIGN(4096) : AT(ADDR(.data) - KERNAL_VIRTUAL_BASE) {
    _data_start_ = .;
    *(.data)
  }

  .bss ALIGN(4096) : AT(ADDR(.bss) - KERNAL_VIRTUAL_BASE) {
    _bss_start_ = .;
    *(.bss)
    _bss_end_ = .;
  }
  
  kernel_end = .;

  /DISCARD/ : {
    *(.comment)
  }
}

