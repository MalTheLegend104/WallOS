bits 64


section .text
global arch_switch_context

; void arch_switch_context(struct arch_context* from, struct arch_context* to)
; Arguments:
;   rdi = from
;   rsi = to
; Clobbers: rax
; Does not return to caller.
arch_switch_context:
    ; ----------------------------
    ; Save callee-saved registers into [from]
    ; ----------------------------
    mov [rdi + 0], rbx
    mov [rdi + 8], rbp
    mov [rdi +16], r12
    mov [rdi +24], r13
    mov [rdi +32], r14
    mov [rdi +40], r15

    ; Save current stack pointer
    mov [rdi +48], rsp

    ; Save return address (RIP) into from->rip
    lea rax, [rel 1f]      ; label after jump
    mov [rdi +56], rax

    ; ----------------------------
    ; Load callee-saved registers from [to]
    ; ----------------------------
    mov rbx, [rsi + 0]
    mov rbp, [rsi + 8]
    mov r12, [rsi +16]
    mov r13, [rsi +24]
    mov r14, [rsi +32]
    mov r15, [rsi +40]

    ; Load stack pointer from to->rsp
    mov rsp, [rsi +48]

    ; Jump to saved instruction pointer
    jmp [rsi +56]

1:  ; label for storing RIP in 'from'
    ret  ; we never actually execute this
