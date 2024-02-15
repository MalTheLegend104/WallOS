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

#pragma GCC diagnostic ignored "-Wunused-parameter" 
int framebuf(int argc, char** argv) {
	multiboot_tag_framebuffer* fb = MultibootManager::getFramebufferTag();
	printf("Framebuffer Type:   %d\n", fb->common.framebuffer_type);
	printf_serial("Framebuffer Type:   %d\r\n", fb->common.framebuffer_type);

	printf("Framebuffer Addr:   0x%llx\n", fb->common.framebuffer_addr);
	printf_serial("Framebuffer Addr:   0x%llx\r\n", fb->common.framebuffer_addr);

	printf("Framebuffer pitch:  %lld\n", fb->common.framebuffer_pitch);
	printf_serial("Framebuffer pitch:  %lld\r\n", fb->common.framebuffer_pitch);

	printf("Framebuffer width:  %lld\n", fb->common.framebuffer_width);
	printf_serial("Framebuffer width:  %lld\r\n", fb->common.framebuffer_width);

	printf("Framebuffer height: %lld\n", fb->common.framebuffer_height);
	printf_serial("Framebuffer height: %lld\r\n", fb->common.framebuffer_height);

	printf("Framebuffer bpp:    %lld\n", fb->common.framebuffer_bpp);
	printf_serial("Framebuffer bpp:    %lld\r\n", fb->common.framebuffer_bpp);

	return 0;
}

extern "C" {
	extern uint64_t kernel_end;
}

int mem_alloc(int argc, char** argv) {
	uintptr_t ptr = Memory::NewKernelPage();
	Logger::infof("Virtual Addr:        0x%llx\n", ptr);
	Logger::infof("KERNEL_VIRTUAL_BASE: 0x%llx\n", KERNEL_VIRTUAL_BASE);
	Logger::infof("Physical:            0x%llx\n", Memory::VirtToPhysBase(ptr));
	Logger::infof("Kernel end:          0x%llx\n", kernel_end);
	Logger::infof("Kernel mapping end:  0x%llx\n", Memory::Info::getPhysKernelEnd());

	return 0;
}

static void putpixel(uint8_t* screen, int x, int y, int color, int pixelWidth, int pitch) {
	uint32_t where = (x * pixelWidth) + (y * pitch);
	screen[where] = color;              	  // BLUE
	screen[where + 1] = (color >> 8) & 255;   // GREEN
	screen[where + 2] = (color >> 16) & 255;  // RED
}

uint8_t pixelwidth = 0;
uint32_t pitch = 0;
static void fillrect(unsigned char* vram, unsigned char r, unsigned char g, unsigned char b, unsigned char w, unsigned char h) {
	unsigned char* where = vram;
	int i, j;

	for (i = 0; i < w; i++) {
		for (j = 0; j < h; j++) {
			//putpixel(vram, 64 + j, 64 + i, (r << 16) + (g << 8) + b);
			where[j * pixelwidth] = r;
			where[j * pixelwidth + 1] = g;
			where[j * pixelwidth + 2] = b;
		}
		where += pitch;
	}
}

#include <memory/kernel_alloc.h>

void kernel_main(unsigned int magic, multiboot_info* mbt_info) {
	initScreen();
	init_serial();
	Memory::initVirtualMemory();

	MultibootManager::initialize(magic, mbt_info);

	cpu_features f = cpuFeatures();
	Features::checkFeatures(&f);
	Features::enableFeatures();
	setup_idt();
	Memory::PhysicalMemInit();

	// This is all framebuffer stuff. 
	// I'm not in too much of a rush about it, it was just a fun experiement
	// multiboot_tag_framebuffer* e = MultibootManager::getFramebufferTag();
	// pixelwidth = e->common.framebuffer_bpp;
	// pitch = e->common.framebuffer_pitch;
	// uintptr_t fb_addr = e->common.framebuffer_addr;
	// uint8_t* fb = (uint8_t*) fb_addr;
	// Memory::mapFramebuffer(fb_addr, e->common.framebuffer_height * e->common.framebuffer_pitch);
	// framebuf(0, 0);

	// int bpp = e->common.framebuffer_bpp;
	// for (int i = 0; i < 200; i++) {
	// 	for (int j = 0; j < 200; j++) {
	// 		//putpixel(fb, i, j, 0xffffff, bpp / 8, e->common.framebuffer_pitch);
	// 	}
	// }
	// for (int i = 0; i < 26; i++) {
	// 	//putchar(fb, e->common.framebuffer_pitch, 'a', i, 0, 0xFF0000, 0x000000);
	// }
	// init_ssfn();
	// print_logo_ssfn();

	// Things that need interrupts here (like keyboard, mouse, etc.)
	// Everything that needs an IRQ should be done after the PIT as it messes with the mask
	pit_init(1000);
	keyboard_init();

	// Framebuffer ignore this
	//multiboot_tag_framebuffer* e = MultibootManager::getFramebufferTag();
	//putpixel((uintptr_t*) e->common.framebuffer_addr, 50, 50, 255, e->common.framebuffer_bpp, e->common.framebuffer_pitch);

	initKernelAllocator();

	// After we're done checking features, we need to set up our terminal.
	// Eventually this will be a userspace program. 
	registerCommand((Command) { acpi_command, 0, "acpi", 0, 0 });
	registerCommand((Command) { framebuf, 0, "framebuf", 0, 0 });
	registerCommand((Command) { mem_alloc, 0, "mem_alloc", 0, 0 });
	terminalMain();
}