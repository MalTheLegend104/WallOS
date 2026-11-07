; I tried to make the VMM code kinda/sorta platform independent (at least the interface).
; TODO: A lot of the VMM should probably be abstracted away like the scheduler
; Problem for another day though
; 
; This is all the ASM code needed the by VMM. 
; Mostly fault-safe user memory copy/clear routines.
; Return the number of bytes not processed (0 = success).
; Resolved page faults retry normally, unrecoverable faults jump to a fixup that returns the remaining count.
; SMAP access is opened with STAC and closed with CLAC on both paths. 
; Pointer validation is handled by the C wrappers.
; Requires DF clear (which should always be true, just wanted to note it).

bits 64
default rel

extern vmm_smap_on            ; volatile uint8_t, 1 once CR4.SMAP is enabled

global vmm_uaccess_copy_raw:function
global vmm_uaccess_copy_insn
global vmm_uaccess_copy_fixup
global vmm_uaccess_clear_raw:function
global vmm_uaccess_clear_insn
global vmm_uaccess_clear_fixup

section .text
align 16
vmm_uaccess_copy_raw:
	mov rcx, rdx              ; REP (rcx) is the number of bytes to copy (passed in rdx)
	cmp byte [vmm_smap_on], 0 ; check if SMAP is enabled
	je .copy
	stac                      ; Allow supervisor access to user pages (SMAP)
.copy:
vmm_uaccess_copy_insn:
	rep movsb                 ; Copy RCX bytes from RSI to RDI. May fault.
vmm_uaccess_copy_fixup:       ; PF handler redirects here if the fault is unrecoverable
	cmp byte [vmm_smap_on], 0
	je .done
	clac                      ; Close the SMAP access window
.done:
	mov rax, rcx              ; Return bytes remaining (0 means success)
	ret

align 16
vmm_uaccess_clear_raw:
	mov rcx, rsi              ; REP: number of bytes to clear
	xor eax, eax              ; STOSB writes AL (zero) to each destination byte
	cmp byte [vmm_smap_on], 0
	je .clear
	stac                      ; Allow supervisor access to user pages (SMAP)
.clear:
vmm_uaccess_clear_insn:
	rep stosb                 ; Clear RCX bytes at RDI. May fault.
vmm_uaccess_clear_fixup:      ; PF handler redirects here if the fault is unrecoverable
	cmp byte [vmm_smap_on], 0
	je .done
	clac                      ; Close the SMAP access window
.done:
	mov rax, rcx              ; Return bytes remaining (0 means success)
	ret

section .note.GNU-stack noalloc noexec nowrite progbits