#include <stdlib.h>
#include <string.h>
#include <cpuid.h>
#include <panic.h>
#include <klibc/kprint.h>
#include <klibc/cpuid_calls.h>
#include <klibc/logger.h>
#include <klibc/features.hpp>

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
	void kernel_early(void);
	void kernel_main(void);
	void __cxa_pure_virtual() { }; // needed for pure virtual functions
}

void kernel_early(void) {
	// Do stuff that MUST be done before the kernel is loaded here.
	// This includes stuff like global constructors.
	//Features();
}

void kernel_main(void) {
	// Features();
	kernel_early();
	clearVGABuf();
	set_colors(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
	print_logo();
	// Do stuff that needs to be enabled before interrupts here.

	// Enable interrupts

	// Everything else

	// INITIALIZE STUFF
	puts_vga("\n\nIntializing OS.\n");
	// Logger::logf("This is a log message. %d\n", 10);
	// Logger::infof("This is a info message. %d\n", 10);
	// Logger::warnf("This is a warn message. %d\n", 10);
	// Logger::errorf("This is a error message. %d\n", 10);
	// Logger::fatalf("This is a fatal message. %d\n", 10);

   // Logger::Checklist::blankEntry("This is a test.");
   // Logger::Checklist::checkEntry("This is a test.");
   // Logger::Checklist::noCheckEntry("This is a test.");

	puts_vga("Checking CPU Features:\n");
	/* Okay imma keep it real C++ hates structs and idk why
	 * It will NOT let me call cpuFeatures() from the class itself. At all.
	 * It's marked as extern C. It know's that it's C code.
	 * If I had to guess it has something to do with how C++ treats structs.
	 * Regardless, this is how this code has to be, and it is how it will stay.
	 */
	cpu_features f = cpuFeatures();
	Features::checkFeatures(&f);// this will halt the OS if needed features aren't present

	// Test the panic system
	// panic_s("This is a really long message to test the way the buffer works. qwertyuiop[]\\asdaghjkl;'zxcvbnm,./1234567890-=!@#$%^&*()_+\"?<>{}|");
}