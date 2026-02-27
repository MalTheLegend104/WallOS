;------------------------------------------------------------------------------
; Enables the local APIC globally by writing IA32_APIC_BASE MSR
; Input: rdi = LAPIC physical base address
; This MAY cause problems if *somehow* the LAPIC address is >4GB mark
;------------------------------------------------------------------------------
global enable_lapic_msr
enable_lapic_msr:
    mov     ecx, 0x1B          ; IA32_APIC_BASE MSR

    ; Read current MSR
    rdmsr                       ; eax = low, edx = high

    ; Enable LAPIC bit (bit 11)
    bts     eax, 11

    ; Clear old base address (bits 12..35)
    mov     ecx, eax
    and     ecx, 0xFFF			; keep low bits (status + flags)
    mov     eax, ecx			; clear old base

    ; Set new LAPIC physical base (input in RDI)
    mov     ecx, edi             ; lapic_phys
    shr     ecx, 12              ; shift to bits 12..35
    shl     ecx, 12
    or      eax, ecx

    ; Write back MSR
    wrmsr

    ret