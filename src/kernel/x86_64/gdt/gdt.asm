global gdt_load

section .text
bits 64
gdt_load:
    mov rdi, qword [rsp]   ; Load the GDT descriptor address from the stack
    lgdt [rdi]
    mov ax, 0x10           ; Code segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret
