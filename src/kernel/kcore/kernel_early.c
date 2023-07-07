// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// This file has no access to anything. Kernel_early gets called before literally anything is set 
// up. We *do* have access to multiboot info, if it is necessary, although we don't have access
// to the multiboot parser (it's built in 64 bit). This file should be almost fully self contained.
// We do have the freestanding headers but that's it. Honestly there shouldn't be anything in here.
// 99% of things that come before long mode should be done in asm. If you're about to write 
// something here, strongly consider if it's worth fighting the compiler instead of doing it in 
// asm. 
// 
// There is a STRONG chance GCC will compile this wrong, since we can't mix 32 bit objective files
// with 64 bit ones. We have to compile this in 64 bit mode. We *can* use objcpy to mess with 
// the compiler(), but ideally we shouldn't. Ideally this should remain empty for the rest of time.
// If you REALLY 100% ABSOLUTELY MUST do something here, and you need to use objcpy, please don't 
// try to deal with the makefile yourself, just ask me to do it.
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
#include <stdint.h>

void kernel_early(void){

}