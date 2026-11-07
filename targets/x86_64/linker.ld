ENTRY(start)

SECTIONS
{
	KERNAL_VIRTUAL_BASE = 0xFFFFFFFF80000000;
	
	/* Processor bring-up requires we have this below 1MB. */
	.trampoline 0x8000 : ALIGN(4096) {
		trampoline_start = .;
		*(.bootstrap.text)
		*(.bootstrap.data)
		. = ALIGN(4096);
		trampoline_end = .;
	}

	. = 1M;
	kernel_start = .;
	
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
        __kernel_text_start = .;
        *(.text .text.*)
        . = ALIGN(4096);
        __kernel_text_end = .;
    }

    .rodata ALIGN(4096) : AT(ADDR(.rodata) - KERNAL_VIRTUAL_BASE) {
        __kernel_rodata_start = .;
        *(.rodata .rodata.*)
        . = ALIGN(4096);
        __kernel_rodata_end = .;
    }

    .data ALIGN(4096) : AT(ADDR(.data) - KERNAL_VIRTUAL_BASE) {
        _data_start_ = .;
        *(.data .data.*)
    }

    .bss ALIGN(4096) : AT(ADDR(.bss) - KERNAL_VIRTUAL_BASE) {
        _bss_start_ = .;
        *(.bss .bss.*)
        _bss_end_ = .;
    }
	
	/* TODO: THIS NEEDS TO BE REMOVED. */
	.initrd ALIGN(4096) : {
		_initrd_start_ = .;
        KEEP(*(.initrd*))
		_initrd_end_ = .;
    }

	kernel_end = .;

	/DISCARD/ : {
		*(.comment)
	}
}

