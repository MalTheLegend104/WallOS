// We really need to clean up these includes.
#include <stdlib.h>
#include <string.h>
#include <cpuid.h>
#include <panic.h>
#include <stdio.h>
#include <klibc/kprint.h>
#include <klibc/cpuid_calls.h>
#include <klibc/logger.h>
#include <klibc/features.hpp>
#include <multiboot.h>
#include <klibc/multiboot.hpp>
#include <idt.h>
#include <gdt.h>
#include <testing.h>
#include <drivers/keyboard.h>
#include <terminal/terminal.h>
#include <timing.h>
/* Okay, this is where the fun begins. Literally and figuratively.
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
	void kernel_main(unsigned int magic, multiboot_info* mbt_info);
	void __cxa_pure_virtual() { }; // needed for pure virtual functions
}

#pragma GCC diagnostic ignored "-Wunused-parameter" 
int acpi(int argc, char** argv) {
	acpi_tag* acpi = MultibootManager::getACPI();
	RSDP_t* r = acpi->rsdp;
	puts_vga_color("ACPI INFO:\n", VGA_COLOR_PINK, VGA_DEFAULT_BG);
	set_colors(VGA_COLOR_PURPLE, VGA_DEFAULT_BG);
	printf("\tSignature: ");
	// The signature is not null terminated, but is guaranteed to be 8 characters long
	for (int i = 0; i < 8; i++) {
		putc_vga(r->signature[i]);
	}
	printf("\n\tOEM: %s\n", r->OEMID);
	printf("\tAddress: 0x%x\n", r->rsdtAddress);
	set_to_last();
	return 0;
}


// static void putpixel(uintptr_t* screen, int x, int y, int color, int pixelwidth, int pitch) {
// 	unsigned where = x * pixelwidth + y * pitch;
// 	screen[where] = color & 255;              // BLUE
// 	screen[where + 1] = (color >> 8) & 255;   // GREEN
// 	screen[where + 2] = (color >> 16) & 255;  // RED
// }

void kernel_main(unsigned int magic, multiboot_info* mbt_info) {
	clearVGABuf();
	disable_cursor();
	set_colors(VGA_DEFAULT_FG, VGA_DEFAULT_BG);
	print_logo();

	// Do stuff that needs to be enabled before interrupts here.
	puts_vga_color("\n\nIntializing OS.\n", VGA_COLOR_PINK, VGA_COLOR_BLACK);

	puts_vga_color("\nChecking Multiboot Configuration:\n", VGA_COLOR_PURPLE, VGA_COLOR_BLACK);
	MultibootManager::initialize(magic, mbt_info);
	if (!MultibootManager::validateAll()) {
		panic_s("Multiboot is invalid.");
	}

	puts_vga_color("Checking CPU Features:\n", VGA_COLOR_PURPLE, VGA_COLOR_BLACK);
	/* Okay imma keep it real C++ hates structs and idk why
	 * It will NOT let me call cpuFeatures() from the class itself. At all.
	 * It's marked as extern C. It know's that it's C code.
	 * If I had to guess it has something to do with how C++ treats structs.
	 * Regardless, this is how this code has to be, and it is how it will stay.
	 */
	cpu_features f = cpuFeatures();
	Features::checkFeatures(&f);

	// Enable CPU features
	Features::enableSSE();
	// We'll hopefully get to the APIC eventually.
	// puts_vga_color("Enabling APIC.\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
	// if (!Features::setupAPIC()) {

	// }


	// Enable interrupts
	puts_vga_color("Enabling Interrupts.\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
	setup_idt();

	//puts_vga_color("Setting up Paging.\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
	//paging_init();
	//puts_vga_color("Testing Paging.\n", VGA_COLOR_PURPLE, VGA_COLOR_BLACK);

	// Things that need interrupts here (like keyboard, mouse, etc.)
	// Everything that needs an IRQ should be done after the PIT as it messes with the mask
	pit_init(1000);
	keyboard_init();

	// Framebuffer ignore this
	// multiboot_tag_framebuffer* e = MultibootManager::getFramebufferTag();
	// putpixel((uintptr_t*) e->common.framebuffer_addr, 50, 50, 255, e->common.framebuffer_bpp, e->common.framebuffer_pitch);

	// After we're done checking features, we need to set up our terminal.
	// Eventually we will clear the screen before handing control over, the user doesnt need the debug stuff.
	registerCommand((Command) { memtest, 0, "memtest", 0, 0 });
	registerCommand((Command) { acpi, 0, "acpi", 0, 0 });
	terminalMain();
}