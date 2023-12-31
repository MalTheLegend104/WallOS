ENTRY(start)

KERNAL_VIRTUAL_BASE = 0xFFFFFFFF80000000;

SECTIONS
{
	. = 1M;

	.boot : {
		KEEP(*(.multiboot_header))
	}

  . += KERNAL_VIRTUAL_BASE;

	.text ALIGN(4096) : AT(ADDR(.text) - KERNEL_VMA) {
    _text_start_ = .;
    *(.text)
  }

  .data ALIGN(4096) : AT(ADDR(.data) - KERNEL_VMA) {
    _data_start_ = .;
    *(.data)
  }

  .rodata ALIGN(4096) : AT(ADDR(.data) - KERNEL_VMA) {
    _rodata_start_ = .;
    *(.rodata)
  }

  .bss ALIGN(4096) : AT(ADDR(.bss) - KERNEL_VMA) {
    _bss_start_ = .;
    *(.bss)
  }
  
  kernel_end = .;

  /DISCARD/ : {
    *(.comment)
  }
}

