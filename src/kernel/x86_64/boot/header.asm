global header_start
section .multiboot_header
; This is basically the same as defining a struct in C
; It's kinda ugly, ignore that.

; If we don't have one set, we need to use defaults
; I *hate* 1024x768 with a passion.
%ifndef FB_WIDTH
    %define FB_WIDTH 1920
    %define FB_HEIGHT 1080
    %define FB_BPP 32
%elifndef FB_HEIGHT
    %define FB_WIDTH 1920
    %define FB_HEIGHT 1080
    %define FB_BPP 32
%elifndef FB_BPP
    %define FB_WIDTH 1920
    %define FB_HEIGHT 1080
    %define FB_BPP 32
%endif

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
%if 1 ;we dont need this yet and im abusing nasm
align 8
mb2_tag_fb_start:
	dw 5
	; this is the type flag
	; https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html#Framebuffer-info to see the types
	; type 1 results in  1024x768x32bit color framebuffer type 1 in qemu and bochs (totally didn't manually comb through hex to figure this out)
	dw 0	
	dd mb2_tag_fb_end - mb2_tag_fb_start
	; dd 640 ; width in pixels
	; dd 480 ; height in pixels
    ; dd 16 ; bpp
	dd FB_WIDTH
	dd FB_HEIGHT
	dd FB_BPP
	; It's not guaranteed to give us the framebuffer we want.
mb2_tag_fb_end:
%endif

align 8
mb2_tag_end_start:
	dw 0                                    ; last tag
    dw 0
    dd mb2_tag_end_end - mb2_tag_end_start
mb2_tag_end_end:
header_end: