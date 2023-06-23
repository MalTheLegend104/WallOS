global long_mode_start
extern kernel_main

extern multiboot_data_magic
extern multiboot_data_address

section .text
bits 64
long_mode_start:
    ; load null into all data segment registers
    mov ax, 0
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
	mov  edi, DWORD[multiboot_data_magic]
    mov  esi, DWORD[multiboot_data_address]
	call kernel_main
    hlt