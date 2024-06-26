#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cpuid.h>
#include <panic.h>
#include <timing.h>
#include <multiboot.h>
#include <idt.h>

#include <klibc/kprint.h>
#include <klibc/cpuid_calls.h>
#include <klibc/logger.h>
#include <klibc/features.hpp>
#include <klibc/multiboot.hpp>

#include <drivers/keyboard.h>
#include <drivers/serial.h>

#include <memory/physical_mem.hpp>
#include <memory/virtual_mem.hpp>
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
	return 0;
}

extern "C" {
	extern uint64_t kernel_end;
}

/* Tests for kalloc and physical/virtual mem
 *  int mem_alloc(int argc, char** argv) {
 * 	uintptr_t ptr = Memory::NewKernelPage();
 * 	Logger::infof("Virtual Addr:        0x%llx\n", ptr);
 * 	Logger::infof("KERNEL_VIRTUAL_BASE: 0x%llx\n", KERNEL_VIRTUAL_BASE);
 * 	Logger::infof("Physical:            0x%llx\n", Memory::VirtToPhysBase(ptr));
 * 	Logger::infof("Kernel end:          0x%llx\n", kernel_end);
 * 	Logger::infof("Kernel mapping end:  0x%llx\n", Memory::Info::getPhysKernelEnd());
 *
 * 	return 0;
 * }

 * int testKalloc(int argc, char** argv) {
 * 	char* a = (char*) kalloc(12);
 * 	printf("Kalloc 64: 0x%llx\n", a);
 * 	memset(a, 0, 64);
 * 	a[0] = 'K';
 * 	a[1] = 'A';
 * 	a[2] = 'L';
 * 	a[3] = 'L';
 * 	a[4] = 'O';
 * 	a[5] = 'C';
 * 	a[6] = '\0';
 * 	printf("%s\n", a);
 * 	//kfree(a);
 * 	return 0;
 * }
 */

void kernel_main(unsigned int magic, multiboot_info* mbt_info) {
	initScreen();
	init_serial();
	Memory::initVirtualMemory();

	MultibootManager::initialize(magic, mbt_info);

	puts_vga_color("before sse", VGA_COLOR_RED, VGA_COLOR_YELLOW);
	cpu_features f = cpuFeatures();
	Features::checkFeatures(&f);
	Features::enableFeatures();
	initIDT();
	Memory::PhysicalMemInit();

	/* This is all framebuffer stuff.
	 * I'm not in too much of a rush about it, it was just a fun experiement
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

	// After we're done checking features, we need to set up our terminal.
	// Eventually this will be a userspace program. 
	//registerCommand((Command) { testKalloc, 0, "kalloc", 0, 0 });
	//registerCommand((Command) { mem_alloc, 0, "mem_alloc", 0, 0 });
	registerCommand((Command) { acpi_command, 0, "acpi", 0, 0 });
	terminalMain();
}