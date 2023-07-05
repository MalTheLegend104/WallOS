global header_start
section .multiboot_header
; This is basically the same as defining a struct in C
align 8
header_start:
	; magic number
	dd 0xe85250d6 ; multiboot2
	; architecture
	dd 0 ; protected mode i386
	; header length
	dd header_end - header_start
	; checksum
	dd -(0xe85250d6 + 0 + (header_end - header_start))

	; end tag
	dw 0
	dw 0
	dd 8
header_end: