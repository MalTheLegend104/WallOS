; Scheduler x86_64 entry stubs
;
; Every trap that can switch tasks is here.
; They all build the same two-part saved context on the interrupted task's *kernel* stack:
;
;     ctx -> +0  r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rdx rcx rbx rax | reason | rip cs rflags rsp ss
;            +64 FPU/SSE/AVX save area (64-byte aligned, x86_fpu_area_size bytes)
;
; THESE MUST MATCH:
;     - struct x86_regs and CTX_HDR in sched_arch.c
;     - arch_task_context_init()
;
; GS convention:
;     in kernel mode: GS_BASE = this CPU's cpu_t*, KERNEL_GS_BASE = the current task's user GS
;     in user mode:    GS_BASE = the current task's user GS, KERNEL_GS_BASE = this CPU's cpu_t*
; The exit path tests the CS of the frame being RESUMED, which is not necessarily the one we entered with.

bits 64
default rel

extern x86_trap_dispatch
extern x86_fpu_use_xsave ; byte:  1 = xsave/xrstor, 0 = fxsave/fxrstor
extern x86_fpu_area_size ; qword: bytes in the save area, multiple of 64
extern x86_user_cs       ; qword: user CS selector (RPL 3 included)
extern x86_user_ss       ; qword: user SS selector (RPL 3 included)

%define CTX_HDR      64
%define REASON_OFF   (15 * 8) ; reason slot, relative to the GPR frame
%define CPU_KSTACK   16       ; gs:16 = cpu->arch.syscall_kstack
%define CPU_USERRSP  24       ; gs:24 = cpu->arch.syscall_scratch
%define REASON_SYSCALL 4

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

; Stack on entry: reason, rip, cs, rflags, rsp, ss (cs at [rsp+16])
x86_sched_common:
	test byte [rsp + 16], 3 ; Check to see if we came from ring 3
	jz   x86_trap_body ; if we didn't, don't swapgs
	swapgs
	jmp  x86_trap_body

x86_trap_body:
	; Save general-purpose registers (GPRs) onto the kernel stack in the order defined by struct x86_regs
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

	; Nothing below can touch xmm/ymm until they are saved
	mov  rbx, rsp                     ; rbx = GPR frame (callee-saved, survives the C call)

	; Calculate space needed for the full context structure (Header + FPU area)
	mov  rax, [rel x86_fpu_area_size] ; Load dynamic FPU save area size (fxsave or xsave)
	add  rax, CTX_HDR                 ; Add header size (contains pointer to GPR frame, reason, etc)
	sub  rsp, rax                     ; Allocate space for the entire context structure on the stack
	and  rsp, -64                     ; rsp = ctx, align to 64-byte boundary (required for AVX/XSAVE)

	mov  [rsp], rbx                   ; Store pointer to the GPR frame at offset 0 (ctx->regs)
	lea  rdi, [rsp + CTX_HDR]         ; rdi = pointer to the FPU/SSE/AVX save area (arg for xsave/fxsave)

	; Save floating point / SIMD register state (XSAVE vs FXSAVE)
	cmp  byte [rel x86_fpu_use_xsave], 0
	je   .fx_save

	; Clear the WHOLE 64-byte XSAVE header, including XSTATE_BV (+512) and reserved bytes (+520..+575).
    ; Reason: xsave only updates components it touches and preserves the rest from memory
    ; Stack garbage in upper bits would cause a subsequent xrstor to trigger a #GP(0) exception on invalid bits
	xor  eax, eax
	mov  [rdi + 512], rax
	mov  [rdi + 520], rax
	mov  [rdi + 528], rax
	mov  [rdi + 536], rax
	mov  [rdi + 544], rax
	mov  [rdi + 552], rax
	mov  [rdi + 560], rax
	mov  [rdi + 568], rax

	mov  eax, -1           ; EDX:EAX acts as the instruction mask (bitmask = XCR0 AND EDX:EAX)
	mov  edx, -1           ; Setting both to -1 requests saving ALL feature components enabled in XCR0
	xsave64 [rdi]          ; Save extended processor state
	jmp  .saved
.fx_save:
	fxsave64 [rdi]         ; Fallback, save legacy FPU/SSE state
.saved:
	mov  rsi, rsp          ; arg1 (rsi) = pointer to the full context (ctx)
	mov  rdi, rbx          ; arg0 (rdi) = pointer to the GPR frame
	cld                    ; SysV needs DF=0. iretq restores the interrupted task's RFLAGS
	call x86_trap_dispatch ; rax = frame to resume (same one means no switch, not that we really care here)

	; Switch to the target context stack
	mov  rsp, rax                         ; the actual context switch
	lea  rdi, [rsp + CTX_HDR]             ; rdi = pointer to the target task's FPU save area

	; Restore floating point / SIMD register state
	cmp  byte [rel x86_fpu_use_xsave], 0
	je   .fx_rest
	mov  eax, -1
	mov  edx, -1
	xrstor64 [rdi]                        ; Restore extended processor state via XSAVE block
	jmp  .restored
.fx_rest:
	fxrstor64 [rdi]                       ; Fallback, restore legacy FPU/SSE state
.restored:
	mov  rsp, [rsp]                       ; ctx->regs

	; Unwind context header and restore GPRs

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

	test byte [rsp + 8], 3 ; CS of the frame we are RESUMING
	jz   .to_kernel        ; Returning to kernel
	swapgs                 ; Returning to ring 3, need to swap the GS back
.to_kernel:
	iretq                  ; Pops rip, cs, rflags, rsp, ss for us

; void x86_iret_user(uint64_t rip, uint64_t user_rsp) (noreturn)
; First drop of a task into ring 3
; Caller has already set up CR3, KERNEL_GS_BASE (= user GS) and RSP0
global x86_iret_user
x86_iret_user:
	cli

	; Build teh frame required by iretq required for returning to ring 3
	push qword [rel x86_user_ss] ; Push user Stack Segment (SS) selector
    push rsi                     ; Push user Stack Pointer (RSP) passed in rsi
    push qword 0x202             ; Push RFLAGS: 0x202 sets the Interrupt Flag (IF=1) and bit 1 (always 1)
    push qword [rel x86_user_cs] ; Push user Code Segment (CS) selector
    push rdi                     ; Push Instruction Pointer (RIP) passed in rdi

	; clear everything to avoid leaking kernel information
	xor  eax, eax
	xor  ebx, ebx
	xor  ecx, ecx
	xor  edx, edx
	xor  esi, esi
	xor  edi, edi
	xor  ebp, ebp
	xor  r8d, r8d
	xor  r9d, r9d
	xor  r10d, r10d
	xor  r11d, r11d
	xor  r12d, r12d
	xor  r13d, r13d
	xor  r14d, r14d
	xor  r15d, r15d

	swapgs
	iretq


; Syscall entry. 
; IF/DF/TF/AC are cleared by SFMASK. We are on the USER stack with user GS.
; We build the same frame an interrupt would, so a syscall can be preempted, blocked and resumed like any other context
; rcx = user rip, r11 = user rflags (hardware put them there)
global x86_syscall_entry
x86_syscall_entry:
	swapgs ; swap to kernel GS

	; Save the user stack pointer into the CPU's scratch slot
	mov  [gs:CPU_USERRSP], rsp
	mov  rsp, [gs:CPU_KSTACK] ; swap to the task's kernel stack

	; Manually push the interrupt frame and reason code
	push qword [rel x86_user_ss]
	push qword [gs:CPU_USERRSP]
	push r11
	push qword [rel x86_user_cs]
	push rcx
	push qword REASON_SYSCALL

	; Jump to the trap body to get to the dispatch
	; We handle interrupts this way so that we can deal with syscalls for yielding and exit
	; It will relegate the other syscalls to the proper handlers
	jmp x86_trap_body

section .note.GNU-stack noalloc noexec nowrite progbits
