#include <klibc/kprint.h>
#include <panic.h>
#include <stdlib.h>
#include <cpuid.h>
#include <string.h>
#include <klibc/cpuid_calls.h>
extern "C" {
	void kernel_main(void);
	void __cxa_pure_virtual() { }; // needed for pure virtual functions
}

void kernel_main(void) {
	clearVGABuf();
	// Do stuff that needs to be enabled before interrupts here.

	// Enable interrupts

	// Everything else
	set_color_vga(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);

	// Print the CPU vendor ID
	// puts_vga("Vendor ID: ");
	// puts_vga(vendorID());
	// putc_vga('\n');

	puts_vga("CPU Features:\n");
	cpu_features features = cpuFeatures();

	// Test the panic system
	// panic_s("This is a really long message to test the way the buffer works. qwertyuiop[]\\asdaghjkl;'zxcvbnm,./1234567890-=!@#$%^&*()_+\"?<>{}|");
}