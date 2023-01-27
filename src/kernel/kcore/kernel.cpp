#include <stdlib.h>
#include <string.h>
#include <cpuid.h>
#include <panic.h>
#include <klibc/kprint.h>
#include <klibc/cpuid_calls.h>
#include <klibc/logger.h>

cpu_features features;

/*
 * Okay, this is where the fun begins. Literally and figuratively.
 * We mark these extern c because we need to call it from asm,
 * because asm can't see c++ functions. Cool, no big deal.
 * But you may be saying, "if we mark it extern c we can't use c++ features."
 * You sweet sweet child. Welcome to OSDEV. We torture compilers and break languages.
 * Everything here is C++.
 * You can use templates. You can use classes. You can use namespaces.
 * We are just TRICKING the linker into thinking this is C.
 * The linker trust us. It shouldn't. This isn't the only time we abuse it.
 */
extern "C" {
	void kernel_main(void);
	void __cxa_pure_virtual() { }; // needed for pure virtual functions
}

void kernel_main(void) {
	clearVGABuf();
	set_colors(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
	// Do stuff that needs to be enabled before interrupts here.

	// Enable interrupts

	// Everything else

	// Print the CPU vendor ID
	// puts_vga("Vendor ID: ");
	// puts_vga(vendorID());
	// putc_vga('\n');

	/*
	 * Testing for cpu features is simple:
	 * if (features.feature == FEATURE_SUPPORTED) {
	 *     // feature is supported
	 * } else {
	 *     // feature isnt supported
	 * }
	 */
	puts_vga("CPU Features:\n");

	// We really only need to do this once. 
	// Really this should be moved elsewhere though.
	features = cpuFeatures();


	// Test the panic system
	// panic_s("This is a really long message to test the way the buffer works. qwertyuiop[]\\asdaghjkl;'zxcvbnm,./1234567890-=!@#$%^&*()_+\"?<>{}|");
}