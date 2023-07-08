global header_start
section .multiboot_header
; This is basically the same as defining a struct in C
; It's kinda ugly, ignore that.
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


; Framebuffer stuff
; Remember we're at the mercy of grub and the bios
; We can request (and requesting does result in VBE mode) things but the request isn't guaranteed.
%if 0 ;we dont need this yet and im abusing nasm
align 8
mb2_tag_fb_start:
	dw 5
	; this is the type flag
	; https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html#Framebuffer-info to see the types
	; type 1 results in  1024x768x32bit color framebuffer type 1 in qemu and bochs (totally didn't manually comb through hex to figure this out)
	dw 1	
	dd mb2_tag_fb_end - mb2_tag_fb_start
	dd 0
	dd 0
    dd 0
mb2_tag_fb_end:
%endif

align 8
mb2_tag_end_start:
	dw 0                                    ; last tag
    dw 0
    dd mb2_tag_end_end - mb2_tag_end_start
mb2_tag_end_end:
header_end: