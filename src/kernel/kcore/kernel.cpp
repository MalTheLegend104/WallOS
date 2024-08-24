#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <system/cpuid.h>
#include <panic.h>
#include <system/timing.h>
#include <multiboot.h>
#include <system/idt.h>

#include <klibc/kprint.h>
#include <klibc/cpuid_calls.h>
#include <klibc/logger.h>
#include <klibc/features.hpp>
#include <klibc/multiboot.h>

#include <drivers/keyboard.h>
#include <drivers/serial.h>

#include <memory/physical_mem.hpp>
#include <memory/virtual_mem.h>
#include <memory/kernel_alloc.h>

#include <terminal/terminal.h>

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

#include <acpi/acpi_init.h>
#pragma GCC diagnostic ignored "-Wunused-parameter" 
int acpi_command(int argc, char** argv) {
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

	acpi_tables();
	return 0;
}

#pragma GCC diagnostic ignored "-Wunused-parameter" 
int bootdev_command(int argc, char** argv) {
	auto a = getBootDev();
	set_colors(VGA_COLOR_LIGHT_CYAN, VGA_DEFAULT_BG);
	printf("BIOS Drive Number: 0x%x\n", a->biosdev);
	set_to_last();

	set_colors(VGA_COLOR_CYAN, VGA_DEFAULT_BG);
	printf("Partition: %d (not relevant for floppy or cd-rom)\n", a->part);
	printf("SubPart: %d (not relevant for floppy or cd-rom)\n", a->slice);
	set_to_last();

	set_colors(VGA_COLOR_PINK, VGA_DEFAULT_BG);
	switch (a->biosdev) {
		case 0x00: printf("Boot device assumed to be floppy. How did you get here...?\n"); break;
		case 0x80: printf("Boot device assumed to be hard drive.\n"); break;
		case 0xE0: printf("Boot device assumed to be CD-ROM (or similar).\n"); break;
		default: printf("Boot device unknown. How did you get here...? (seriously please let me know)\n");
	}
	set_to_last();
	return 0;
}

extern "C" {
	extern uint64_t kernel_end;
}

// Tests for kalloc and physical/virtual mem
int mem_alloc(int argc, char** argv) {
	uintptr_t ptr = Memory::NewKernelPage();
	Logger::infof("Virtual Addr:        0x%llx\n", ptr);
	Logger::infof("KERNEL_VIRTUAL_BASE: 0x%llx\n", KERNEL_VIRTUAL_BASE);
	Logger::infof("Physical:            0x%llx\n", Memory::VirtToPhysBase(ptr));
	Logger::infof("Kernel end:          0x%llx\n", kernel_end);
	Logger::infof("Kernel mapping end:  0x%llx\n", Memory::Info::getPhysKernelEnd());

	return 0;
}

int testKalloc(int argc, char** argv) {
	char* a = (char*) kalloc(12);
	printf("Kalloc 64: 0x%llx\n", a);
	memset(a, 0, 64);
	a[0] = 'K';
	a[1] = 'A';
	a[2] = 'L';
	a[3] = 'L';
	a[4] = 'O';
	a[5] = 'C';
	a[6] = '\0';
	printf("%s\n", a);
	kfree(a);
	return 0;
}

#include <syscall/syscall.h>

#pragma GCC diagnostic ignored "-Wunused-parameter" 
int syscall_command(int argc, char** argv) {
	uint64_t syscall_number = UINT64_MAX;
	// We default to the 64 bit max, anything outside of 0-255 isn't valid.
	uint64_t arg1 = 0;
	if (argc > 1) {
		syscall_number = atoi(argv[1]);
	}

	if (argc > 2) {
		arg1 = atoi(argv[2]);
	}
	uint64_t ret;
	asm volatile(
		"movq %1, %%rax\n\t"
		"movq %2, %%rdi\n\t"
		"int $0x42\n\t"
		"movq %%rax, %0\n\t"
		: "=r" (ret)
		: "r" (syscall_number), "r" (arg1)
		: "rax", "rdi"
		);
	return ret;
}

void run_tests_builtin() {
	int ret;
	// Signed decimal integer
	ret = printf("Signed Decimal: %d\n", -123);

	// Unsigned decimal integer
	ret = printf("Unsigned Decimal: %u\n", 123U);

	// Unsigned octal
	ret = printf("Unsigned Octal: %o\n", 123U);

	// Unsigned hexadecimal (lowercase)
	ret = printf("Unsigned Hex (lowercase): %x\n", 123U);

	// Unsigned hexadecimal (uppercase)
	ret = printf("Unsigned Hex (uppercase): %X\n", 123U);

	// Decimal floating point (lowercase)
	ret = printf("Float (lowercase): %f\n", 123.456);

	// Decimal floating point (uppercase)
	ret = printf("Float (uppercase): %F\n", 123.456);

	// Scientific notation (lowercase)
	ret = printf("Scientific (lowercase): %e\n", 123.456);

	// Scientific notation (uppercase)
	ret = printf("Scientific (uppercase): %E\n", 123.456);

	// Shortest representation of floating point (lowercase)
	ret = printf("Shortest Float (lowercase): %g\n", 123.456);

	// Shortest representation of floating point (uppercase)
	ret = printf("Shortest Float (uppercase): %G\n", 123.456);

	// Hexadecimal floating point (lowercase)
	ret = printf("Hexadecimal Float (lowercase): %a\n", 123.456);

	// Hexadecimal floating point (uppercase)
	ret = printf("Hexadecimal Float (uppercase): %A\n", 123.456);

	// Character
	ret = printf("Character: %c\n", 'A');

	// String
	ret = printf("String: %s\n", "Hello, World!");

	// Pointer
	int x = 42;
	ret = printf("Pointer: %p\n", (void*) &x);

//	// Number of characters written so far
//	int chars_written;
//	ret = printf("Number of Chars: %n", &chars_written);
//	ret = printf("%d\n", chars_written);

	ret = printf("Precision: \"%.5d\"\n", 123);

	ret = printf("Width:     \"%5d\"\n", 123);

	// Width and precision
	ret = printf("Width and Precision: \"%10.2f\"\n", 123.456);

	// Left-justified with width
	ret = printf("Left-justified: \"%-10d\"\n", 123);

	// Zero-padded with width
	ret = printf("Zero-padded: \"%010d\"\n", 123);

	// Plus sign for positive numbers
	ret = printf("Plus sign: %+d\n", 123);

	// Space for positive numbers
	ret = printf("Space sign: % d\n", 123);

	// Alternate form for octal
	ret = printf("Alternate Octal: %#o\n", 123U);

	// Alternate form for hexadecimal
	ret = printf("Alternate Hex: %#X\n", 123U);

	// Alternate form for floating point
	ret = printf("Alternate Float: %#g\n", 123.0);

	// Length modifiers
	ret = printf("Short: %hd\n", (short) 123);

	ret = printf("Long: %ld\n", (long) 123);

	ret = printf("Long Long: %lld\n", (long long) 123);

	ret = printf("Char: %hhd\n", (char) 123);

	ret = printf("Intmax: %jd\n", (intmax_t) 123);

	ret = printf("Size_t: %zd\n", (size_t) 123);

	ret = printf("Ptrdiff_t: %td\n", (ptrdiff_t) 123);

	ret = printf("Long Float: %Lf\n", (long double) 123.456);
}

void kernel_main(unsigned int magic, multiboot_info* mbt_info) {
	initScreen();
	init_serial();
	printf_serial("Welcome to WallOS!\r\n");
	Memory::initVirtualMemory();

	MultibootManager::initialize(magic, mbt_info);
	cpu_features f = cpuFeatures();
	Features::checkFeatures(&f);
	Features::enableFeatures();
	initIDT();

	printf_serial("Kernel Mapping End: 0x%llx\r\nRSDP ADDR: 0x%llx\r\n", Memory::GetMappingEnd(), MultibootManager::getACPI()->rsdp);


	Memory::PhysicalMemInit();


	acpi_tables();
	__asm volatile("cli\n\thlt");
	run_tests_builtin();


	printf_serial("Physical kernel end: 0x%llx\r\n", Memory::Info::getPhysKernelEnd());


	/* This is all framebuffer stuff.
	 * I'm not in too much of a rush about it, it was just a fun experiment
	 * multiboot_tag_framebuffer* e = MultibootManager::getFramebufferTag();
	 * pixelwidth = e->common.framebuffer_bpp;
	 * pitch = e->common.framebuffer_pitch;
	 * uintptr_t fb_addr = e->common.framebuffer_addr;
	 * uint8_t* fb = (uint8_t*) fb_addr;
	 * Memory::mapFramebuffer(fb_addr, e->common.framebuffer_height * e->common.framebuffer_pitch);
	 * framebuf(0, 0);

	 * int bpp = e->common.framebuffer_bpp;
	 * for (int i = 0; i < 200; i++) {
	 * 	for (int j = 0; j < 200; j++) {
	 * 		//putpixel(fb, i, j, 0xffffff, bpp / 8, e->common.framebuffer_pitch);
	 * 	}
	 * }
	 * for (int i = 0; i < 26; i++) {
	 * 	//putchar(fb, e->common.framebuffer_pitch, 'a', i, 0, 0xFF0000, 0x000000);
	 * }
	 * init_ssfn();
	 * print_logo_ssfn();
	 */

	// Things that need interrupts here (like keyboard, mouse, etc.)
	// Everything that needs an IRQ should be done after the PIT as it messes with the mask
	pit_init(1000);
	keyboard_init();

	initKernelAllocator();

	Syscall::initialize();

	// After we're done checking features, we need to set up our terminal.
	// Eventually this will be a userspace program. 
	registerCommand((Command) { testKalloc, 0, "kalloc", 0, 0 });
	registerCommand((Command) { mem_alloc, 0, "mem_alloc", 0, 0 });
	registerCommand((Command) { acpi_command, 0, "acpi", 0, 0 });
	registerCommand((Command) { syscall_command, 0, "syscall", 0, 0 });
	registerCommand((Command) { bootdev_command, 0, "bootdev", 0, 0 });
	terminalMain();
}