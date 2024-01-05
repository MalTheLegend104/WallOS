#include <stdio.h>
#include <panic.h>
#include <klibc/kprint.h>
#include <klibc/logger.h>
#include <klibc/features.hpp>

bool Features::AVX;
bool Features::FXSR;
bool Features::APIC;
const char* Features::highest_supported_float;
struct cpu_features* Features::features;

bool Features::listFeature(const char* name, bool a) {
	puts_vga("        ");
	if (a) {
		set_colors(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
		Logger::Checklist::checkEntry("%s is supported.", name);
		set_colors_default();
	} else {
		set_colors(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
		Logger::Checklist::blankEntry("%s is not supported.", name);
		set_colors_default();
	}
	return a;
}

bool Features::listFeatureCheck(const char* name, bool a) {
	puts_vga("    ");
	if (a) {
		set_colors(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
		Logger::Checklist::checkEntry("%s is supported.", name);
		set_colors_default();
	} else {
		set_colors(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
		Logger::Checklist::noCheckEntry("%s is not supported.", name);
		set_colors_default();
	}
	return a;
}

void Features::checkFloatingPointSupport() {
	/*
	 * Testing for cpu features is simple:
	 * if (features->feature == FEATURE_SUPPORTED) {
	 *     // feature is supported
	 * } else {
	 *     // feature isnt supported
	 * }
	 */
	if (listFeature("AVX", features->AVX == FEATURE_SUPPORTED)) {
		AVX = true;
		putc_vga('\t');
		FXSR = features->FXSR == FEATURE_SUPPORTED;
		listFeature("FXSR", FXSR);
		if (FXSR) {
			highest_supported_float = "FXSR";
		} else {
			highest_supported_float = "AVX";
		}
		return;
	}
	if (listFeature("SSE4.2", features->SSE4_2 == FEATURE_SUPPORTED)) {
		highest_supported_float = "SSE4.2";
		return;
	} else if (listFeature("SSE4.1", features->SSE4_1 == FEATURE_SUPPORTED)) {
		highest_supported_float = "SSE4.2";
		return;
	} else if (listFeature("SSSE3", features->SSSE3 == FEATURE_SUPPORTED)) {
		highest_supported_float = "SSSE3";
		return;
	} else if (listFeature("SSE3", features->SSE3 == FEATURE_SUPPORTED)) {
		highest_supported_float = "SSE3";
		return;
	} else if (listFeature("SSE2", features->SSE2 == FEATURE_SUPPORTED)) {
		highest_supported_float = "SSE2";
		return;
	} else if (listFeature("SSE", features->SSE == FEATURE_SUPPORTED)) {
		highest_supported_float = "SSE";
		return;
	} else if (listFeatureCheck("FPU", features->FPU == FEATURE_SUPPORTED)) {
		highest_supported_float = "FPU";
		return;
	}
	highest_supported_float = nullptr;
}

/**
 * @brief Check all important features.
 * It will call abort() if needed features do not exist.
 * @param f struct of features that will be checked.
 */
void Features::checkFeatures(struct cpu_features* f) {
	puts_vga_color("Checking CPU Features:\n", VGA_COLOR_PURPLE, VGA_COLOR_BLACK);
	/* Okay imma keep it real C++ hates structs and idk why
	 * It will NOT let me call cpuFeatures() from the class itself. At all.
	 * It's marked as extern C. It know's that it's C code.
	 * If I had to guess it has something to do with how C++ treats structs.
	 * Regardless, this is how this code has to be, and it is how it will stay.
	 */
	features = f;
	// Ideally we are going to be avoiding floats as much as possible
	// Halts the cpu if not present.
	puts_vga_color("    Checking Floating Point Support:\n", VGA_COLOR_PURPLE, VGA_COLOR_BLACK);
	checkFloatingPointSupport();
	if (highest_supported_float == nullptr) {
		Logger::Checklist::noCheckEntry("No Floating Point support.");
		Logger::fatalf("This OS requires floating point operations.\
Any x86_64 CPU is required to support a minimum of SSE2.\
If this system has a x86_64 CPU, then this is an issue on our side, \
report it to our GitHub Repo.\n");
		__asm volatile ("hlt");
	}

	// Check APIC
	if (features->APIC == FEATURE_SUPPORTED) {
		APIC = true;
		puts_vga("    ");
		Logger::Checklist::checkEntry("APIC exists.");
	} else {
		APIC = false;
		puts_vga("    ");
		Logger::Checklist::noCheckEntry("APIC does not exists.");
	}

	if (features->FXSR == FEATURE_SUPPORTED) {
		// Just set this up so we can properly use floating point stuff later.
		char fxsave_region[512] __attribute__((aligned(16)));
		asm volatile(" fxsave %0 "::"m"(fxsave_region));
	}
}

/**
 * @brief Returns the string equalivent of
 * the highest supported floating point instruction set
 *
 * @return const char* String of the highest supported floating point instruction set
 */
const char* Features::highestFloat() {
	return highest_supported_float;
}

/**
 * @brief Returns whether or not the APIC exists.
 *
 * @return true Does exist.
 * @return false Does not exist.
 */
bool Features::getAPIC() {
	return APIC;
}




// ASM code to enable sse
extern "C" void enable_sse();

/* This enables floating point operations.
 * Currently we only really care about sse and sse2
 * In theory this should enable all forms of sse, not just those two
 * It works. We dont need those fancy new features from SSE3+.
 */
void Features::enableSSE() {
	/* SSE2 is requried support on x86_64 systems.
	 * FPU SHOULD be automatically enabled on x86 systems.
	 * IDK about ARM
	 */
	if ((Features::highestFloat()[0] == 'S') || (Features::highestFloat()[0] == 'F') || (Features::highestFloat()[0] == 'A')) {
		set_colors(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
		printf("Enabling floating point operations: %s\n", Features::highestFloat());
		enable_sse();
		set_to_last();
	} else {
		panic_s("SSE Instructions do not exist.");
	}
}

/**
 * @brief Sets up the APIC how we need it. This assumes that interrupts have been enabled.
 *
 * @return true If the APIC exists and is set up.
 * @return false If the APIC does not exist or cannot be set up.
 * In the case this returns false, one should set up the 8529 PIC or stop execution.
 */
bool Features::setupAPIC() {
	if (Features::APIC != true) {
		return false;
	}
	return true;
}


void Features::enableFeatures() {
	Features::enableSSE();
	// We'll hopefully get to the APIC eventually.
	// puts_vga_color("Enabling APIC.\n", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
	// if (!Features::setupAPIC()) {

	// }
}