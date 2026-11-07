global enable_sse

section .text
bits 32
; This is used early to let us use SSE registers during setup
; We enable more floating point stuff later on, once we get through setup
enable_sse:
	mov eax, cr0
	and ax, 0xFFFB		;clear coprocessor emulation CR0.EM
	or ax, 0x2			;set coprocessor monitoring  CR0.MP
	mov cr0, eax
	mov eax, cr4
	or ax, 3 << 9		;set CR4.OSFXSR and CR4.OSXMMEXCPT at the same time
	mov cr4, eax
	ret


bits 64
section .text
extern x86_fpu_have_avx ; 1 = XCR0 has AVX enabled

; void x86_fpu_reset(void)
; Put this CPU's live x87/SSE/AVX registers in their reset state.
; Used when a CPU is brought up and before a task first enters ring 3, so kernel leftovers in xmm/ymm are never visible to user code.
global x86_fpu_reset
x86_fpu_reset:
	fninit      ; x87 init, resets it to the default state
	sub  rsp, 8 ; temp stack space
	; Bits 0-5:   exception status flags cleared
	; Bits 7-12:  all six SIMD floating-point exceptions masked
	; Bits 13-14: round to nearest
	; Bit 15:     flush-to-zero disabled
	mov  dword [rsp], 0x1F80
	; loads the value above into MXCSR
	ldmxcsr [rsp]
	add  rsp, 8 ; remove our temp stack space
	cmp  byte [rel x86_fpu_have_avx], 0 ; checks if we have AVX
	je   .sse ; if we don't have AVX, go to the SSE reset
	vzeroall ; clears the AVX registers YMM0-15, including XMM portions
	ret
.sse: ; We have to manually clear all the SSE registers, we xor each one with itself
%assign i 0 ; for (int i = 0; i < 16; i++) xmm<i> XOR xmm<i>
%rep 16
	xorps xmm %+ i, xmm %+ i
%assign i i+1
%endrep
	ret