#include <stdlib.h>
#include <string.h>
#include <cpuid.h>
#include <panic.h>
#include <paging.h>
#include <klibc/kprint.h>
#include <klibc/cpuid_calls.h>
#include <klibc/logger.h>
#include <klibc/features.hpp>
#include <stdio.h>
#include <multiboot.h>
#include <klibc/multiboot.hpp>
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
	void kernel_early(void);
	void kernel_main(unsigned int magic, multiboot_info* mbt_info);
	void __cxa_pure_virtual() { }; // needed for pure virtual functions
}

/* This enables floating point operations.
 * Currently we only really care about sse and sse2
 * In theory this should enable all forms of sse, not just those two
 * We still only compile the OS with sse and sse2, and gcc really doesn't care.
 * It works. We dont need those fancy new features from SSE3+.
 */
extern "C" void enable_sse();

void kernel_early(void) {

}

void kernel_main(unsigned int magic, multiboot_info* mbt_info) {    
	kernel_early();
	clearVGABuf();
	set_colors(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
	print_logo();

	// INITIALIZE STUFF
	puts_vga("\n\nIntializing OS.\n");

	puts_vga("Checking Multiboot Configuration:\n");
	MultibootManager::initialize(magic, mbt_info);
	if (!MultibootManager::validateAll()){
		panic_s("Multiboot is invalid.");
	}

	puts_vga("Checking CPU Features:\n");
	/* Okay imma keep it real C++ hates structs and idk why
	 * It will NOT let me call cpuFeatures() from the class itself. At all.
	 * It's marked as extern C. It know's that it's C code.
	 * If I had to guess it has something to do with how C++ treats structs.
	 * Regardless, this is how this code has to be, and it is how it will stay.
	 */
	cpu_features f = cpuFeatures();
	Features::checkFeatures(&f);

	// Enable CPU features
	/* SSE2 is requried support on x86_64 systems.
	 * FPU SHOULD be automatically enabled on x86 systems.
	 * IDK about ARM
	 * */
	if (Features::highestFloat()[0] == 'S') {
		Logger::Checklist::checkEntry("Enabling floating point operations: %s", Features::highestFloat());
		enable_sse();
	}

	// We should enable stuff before we enable interrupts(unless they require interrupts ofc)
	// Do stuff that needs to be enabled before interrupts here.

	// Enable interrupts

	// Everything else
	// After we're done checking features, we need to set up our terminal.

}