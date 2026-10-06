; Scheduler x86_64 entry stub
;
; Every scheduling trap lands here (timer, yield, resched IPI, kernel-preempt IPI).
; The stub saves all GPRs on the interrupted task's stack, calls x86_sched_dispatch(regs*),
; and then switches stacks to whatever context pointer it returns, pops and iretq's. 
; A task's "saved context" is therefore simply the stack pointer of that pushed frame.
;
; Frame layout (low -> high):
;     r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rdx rcx rbx rax | reason | rip cs rflags rsp ss
; This has to match the struct `x86_regs` in sched_arch.c
; If that (or this) are updated, make sure to change the other. 

bits 64
default rel

extern x86_sched_dispatch

section .text

%macro SCHED_ENTRY 2 ; name, reason code
global %1
%1:
	push qword %2
	jmp  x86_sched_common
%endmacro

SCHED_ENTRY x86_sched_timer_entry,    0
SCHED_ENTRY x86_sched_yield_entry,    1
SCHED_ENTRY x86_sched_resched_entry,  2
SCHED_ENTRY x86_sched_kpreempt_entry, 3

x86_sched_common:
	push rax
	push rbx
	push rcx
	push rdx
	push rsi
	push rdi
	push rbp
	push r8
	push r9
	push r10
	push r11
	push r12
	push r13
	push r14
	push r15

	mov  rdi, rsp           ; arg0 = pointer to saved frame
	and  rsp, -16           ; SysV alignment (frame size leaves us at 8 mod 16)
	cld                     ; SysV ABI requires DF=0 when entering C code. iretq restores the interrupted task's original RFLAGS/DF
	call x86_sched_dispatch ; rax = frame to resume (same one means no switch, not that we really care here)

	mov  rsp, rax           ; the actual context switch

	pop  r15
	pop  r14
	pop  r13
	pop  r12
	pop  r11
	pop  r10
	pop  r9
	pop  r8
	pop  rbp
	pop  rdi
	pop  rsi
	pop  rdx
	pop  rcx
	pop  rbx
	pop  rax
	add  rsp, 8 ; drop `reason`
	iretq

section .note.GNU-stack noalloc noexec nowrite progbits
