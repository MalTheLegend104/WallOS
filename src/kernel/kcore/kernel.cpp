#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <panic.h>
#include <multiboot.h>

#include <acpi/acpi_init.h>

#include <drivers/keyboard.h>
#include <drivers/serial.h>

#include <klibc/kprint.h>
#include <klibc/cpuid_calls.h>
#include <klibc/logger.h>
#include <klibc/features.hpp>
#include <klibc/multiboot.h>

#include <memory/physical_mem.hpp>
#include <memory/virtual_mem.h>
#include <memory/kernel_alloc.h>

#include <system/idt.h>
#include <system/cpuid.h>
#include <system/timing.h>

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
int bootdev_command(int argc, char** argv) {
	const auto a = getBootDev();
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
	const uintptr_t ptr = Memory::NewKernelPage();
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

// Apollo is the name of a framebuffer library im working on.
// It's private right now and this was a test to make sure that it worked in a freestanding environment
// This code will be useful, so it's getting pushed to main. 
// The actual framebuffer implementation will later be on a different branch.
#ifdef APOLLO_TEST
#include <apollo.h>
#include <fonts/apollo_12x18.h>
#include <drivers/framebuffer.h>

coordinate_pair current = { 0, 0 };

// void putc_apollo(const unsigned char c) {
// 	if (c == '\0') return;
// 	apollo_print_char()
// }

void pixel_serial(apollo_pixel_type type) {
	switch (type) {
		case APOLLO_PIXEL_TYPE_UNKNOWN:
			printf_serial("Unknown Pixel Type");
			break;
		case APOLLO_PIXEL_TYPE_RGB32:
			printf_serial("RGB 32-bit");
			break;
		case APOLLO_PIXEL_TYPE_RGBA32:
			printf_serial("RGBA 32-bit");
			break;
		case APOLLO_PIXEL_TYPE_BGR32:
			printf_serial("BGR 32-bit");
			break;
		case APOLLO_PIXEL_TYPE_BGRA32:
			printf_serial("BGRA 32-bit");
			break;
		case APOLLO_PIXEL_TYPE_RGB16_565:
			printf_serial("RGB 16-bit (5-6-5)");
			break;
		case APOLLO_PIXEL_TYPE_RGB16_555:
			printf_serial("RGB 16-bit (5-5-5)");
			break;
		case APOLLO_PIXEL_TYPE_BGR16_565:
			printf_serial("BGR 16-bit (5-6-5)");
			break;
		case APOLLO_PIXEL_TYPE_BGR16_555:
			printf_serial("BGR 16-bit (5-5-5)");
			break;
		case APOLLO_PIXEL_TYPE_RGB24:
			printf_serial("RGB 24-bit");
			break;
		case APOLLO_PIXEL_TYPE_BGR24:
			printf_serial("BGR 24-bit");
			break;
		default:
			printf_serial("Invalid Pixel Type");
			break;
	}
}

void framebuffer() {
	multiboot_tag_framebuffer* e = MultibootManager::getFramebufferTag();

	// Memory::mapFramebuffer((uintptr_t) fb_base, e->common.framebuffer_height * e->common.framebuffer_pitch);

	if (e->common.framebuffer_type == 1) {
		// TODO: actually get the pixel type.
		framebuffer_info_t fb_info;
		framebuffer_t fb;
		print_fb_info();

		apollo_get_info(&fb_info);

		printf_serial("here\r\n");
		printf_serial("Framebuffer Info:\r\n");
		printf_serial("\tWidth: %i\r\n", fb_info.width);
		printf_serial("\tHeight: %i\r\n", fb_info.height);
		printf_serial("\tPitch: %i\r\n", fb_info.pitch);
		printf_serial("\tPixel Width: %i\r\n\t", fb_info.pixel_width);
		pixel_serial(fb_info.type);
		printf_serial("\r\n");

		printf_serial("Framebuffer total length: %llu bytes.\r\n", fb_info.height * fb_info.width * fb_info.pixel_width);

		//fb.buffer = (uint8_t*) kalloc(fb_info.height * fb_info.width * fb_info.pixel_width);
		fb.buffer = (uint8_t*) e->common.framebuffer_addr;

		printf_serial("Double buffer location: %p\r\n", fb.buffer);

		fb.info = &fb_info;

		//apollo_color_t fg = { 0, 0xff, 0xff, 0xff, APOLLO_PIXEL_TYPE_ARGB8888 };
		apollo_color_t fg = { 0, 0x1f, 0, 0x1f, APOLLO_PIXEL_TYPE_RGB16_565 };
		apollo_color_t bg = { 0, 0, 0, 0, APOLLO_PIXEL_TYPE_RGBA32 };
		apollo_font_color_t c = { fg, bg };

		//apollo_print_char(&fb, &apollo_8x8, 'a', (coordinate_pair) { 0, 0 }, c);

		uint8_t a[] = {
			0x0A, 0xdb, 0xdb, 0xbb, 0x20, 0x20, 0x20, 0x20, 0xdb, 0xdb,
			0xbb, 0x20, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xbb, 0x20, 0xdb,
			0xdb, 0xbb, 0x20, 0x20, 0x20, 0x20, 0x20, 0xdb, 0xdb, 0xbb,
			0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0xdb, 0xdb, 0xdb, 0xdb,
			0xdb, 0xdb, 0xbb, 0x20, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb,
			0xdb, 0xbb, 0x0a, 0xdb, 0xdb, 0xba, 0x20, 0x20, 0x20, 0x20,
			0xdb, 0xdb, 0xba, 0xdb, 0xdb, 0xc9, 0xcd, 0xcd, 0xdb, 0xdb,
			0xbb, 0xdb, 0xdb, 0xba, 0x20, 0x20, 0x20, 0x20, 0x20, 0xdb,
			0xdb, 0xba, 0x20, 0x20, 0x20, 0x20, 0x20, 0xdb, 0xdb, 0xc9,
			0xcd, 0xcd, 0xcd, 0xdb, 0xdb, 0xbb, 0xdb, 0xdb, 0xc9, 0xcd,
			0xcd, 0xcd, 0xcd, 0xbc, 0x0a, 0xdb, 0xdb, 0xba, 0x20, 0xdb,
			0xbb, 0x20, 0xdb, 0xdb, 0xba, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb,
			0xdb, 0xdb, 0xba, 0xdb, 0xdb, 0xba, 0x20, 0x20, 0x20, 0x20,
			0x20, 0xdb, 0xdb, 0xba, 0x20, 0x20, 0x20, 0x20, 0x20, 0xdb,
			0xdb, 0xba, 0x20, 0x20, 0x20, 0xdb, 0xdb, 0xba, 0xdb, 0xdb,
			0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xbb, 0x0a, 0xdb, 0xdb, 0xba,
			0xdb, 0xdb, 0xdb, 0xbb, 0xdb, 0xdb, 0xba, 0xdb, 0xdb, 0xc9,
			0xcd, 0xcd, 0xdb, 0xdb, 0xba, 0xdb, 0xdb, 0xba, 0x20, 0x20,
			0x20, 0x20, 0x20, 0xdb, 0xdb, 0xba, 0x20, 0x20, 0x20, 0x20,
			0x20, 0xdb, 0xdb, 0xba, 0x20, 0x20, 0x20, 0xdb, 0xdb, 0xba,
			0xc8, 0xcd, 0xcd, 0xcd, 0xcd, 0xdb, 0xdb, 0xba, 0x0a, 0xc8,
			0xdb, 0xdb, 0xdb, 0xc9, 0xdb, 0xdb, 0xdb, 0xc9, 0xbc, 0xdb,
			0xdb, 0xba, 0x20, 0x20, 0xdb, 0xdb, 0xba, 0xdb, 0xdb, 0xdb,
			0xdb, 0xdb, 0xdb, 0xdb, 0xbb, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb,
			0xdb, 0xdb, 0xbb, 0xc8, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb,
			0xc9, 0xbc, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xba,
			0x0a, 0x20, 0xc8, 0xcd, 0xcd, 0xbc, 0xc8, 0xcd, 0xcd, 0xbc,
			0x20, 0xc8, 0xcd, 0xbc, 0x20, 0x20, 0xc8, 0xcd, 0xbc, 0xc8,
			0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xbc, 0xc8, 0xcd, 0xcd,
			0xcd, 0xcd, 0xcd, 0xcd, 0xbc, 0x20, 0xc8, 0xcd, 0xcd, 0xcd,
			0xcd, 0xcd, 0xbc, 0x20, 0xc8, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
			0xcd, 0xbc, 0x0a, '\0'
		};

		apollo_print_string(&fb, &apollo_12x18, (const char*) a, (coordinate_pair) { 0, 0 }, c, true, true);
		coordinate_pair pair[] = {
			{0, 0},
			{100, 100},
			{100, 150}
		};

		//apollo_draw_triangle(&fb, pair, fg);
		//apollo_fill_buffer(&fb, fg);
		apollo_draw_buffer(&fb);

		//__asm __volatile("cli\n\thlt");

	} else {
		printf_serial("FRAMEBUFFER TYPE: %d -> UNKNOWN, LEADS TO PANIC.\r\n", e->common.framebuffer_type);
		panic_s("Framebuffer of wrong type.");
	}

}
#endif // APOLLO_TEST

#include <drivers/sata/pio.h>

/* TODO: Remove this when PMM is fixed. */
#define JANKY_INITRD_LOADER
#ifdef JANKY_INITRD_LOADER
extern "C" {
	// extern int drive_mount_cmd(int argc, char** argv);

	extern uint64_t _initrd_start_;
	extern uint64_t _initrd_end_;
	uint64_t _initrd_size;
	uint8_t* _initrd_data;
}

void init_initrd() {
	// size_t size = (size_t) _binary_initrd_img_size;
	// const uint8_t* data = _binary_initrd_img_start;

	_initrd_size = (size_t) &_initrd_end_ - (size_t) &_initrd_start_;
	_initrd_data = (uint8_t*) &_initrd_start_;

	printf_serial("Initrd start: 0x%llx\r\n", &_initrd_start_);
	printf_serial("Initrd end: 0x%llx\r\n", &_initrd_end_);
	printf_serial("Initrd size: %llu bytes\r\n", (uint64_t) &_initrd_end_ - (uint64_t) &_initrd_start_);

	// now you can feed `data` and `size` into your FS code
}
#endif // JANKY_INITRD_LOADER

extern void pmm_init();

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

	multiboot_tag_framebuffer* e = MultibootManager::getFramebufferTag();
	Memory::mapFramebuffer((uintptr_t) e->common.framebuffer_addr, e->common.framebuffer_height * e->common.framebuffer_pitch);
	//	framebuffer_init();

	// We get initrd from grub via a multiboot module tag.
	// We *should* be reserving this memory so it doesn't get allocated to something else.
	// the physical allocator is a horrible mess from past me
	// I'm taking my win with the regular filesystem and leaving this for now.
	// multiboot_tag_module* module_tag = MultibootManager::getModuleTag();
	// printf_serial("    MODULE tag exists. Module command line: %s\r\n", module_tag->cmdline);
	// printf_serial("    Module start: 0x%X, end: 0x%X\r\n", module_tag->mod_start, module_tag->mod_end);
	// printf_serial("    Module Type: %d\r\n", module_tag->type);
	// printf_serial("    Module Size: %d bytes\r\n", module_tag->size);
	// printf_serial("    Module Physical Size: %d bytes\r\n", module_tag->mod_end - module_tag->mod_start);
	//Memory::reserveMemory(module_tag->mod_start, module_tag->mod_end - module_tag->mod_start);

	init_initrd();

	// pmm_init();
	// Things that need interrupts here (like keyboard, mouse, etc.)
	// Everything that needs an IRQ should be done after the PIT as it messes with the mask
	// If it requires allocations, add it after `initKernelAllocator()`
	pit_init(1000);
	keyboard_init();

	// wait_for_esc();

	Memory::PhysicalMemInit();

	acpi_tables();

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

	// panic_s("GOT BEFORE IDE DRIVES?????");
	detect_ide_drives();

	initKernelAllocator();

	//framebuffer();

	Syscall::initialize();

	initialize_acpi();


	// char* array[] = { (char*) "drive", (char*) "mount", (char*) "0" };
	// drive_mount_cmd(3, array);

	printf_serial("Ended kernel init... handing control to WallShell.\r\n");
	set_colors(VGA_COLOR_PINK, VGA_DEFAULT_BG);
	printf("Ended kernel init... Press `ESC` to hand control to WallShell.\r\n");
	set_to_last();
	// WALLOS_CLI_HLT();

	wait_for_esc();

	// char* args[] = { "acpi", "list" };
	// acpi_command(2, args);

	// After we're done checking features, we need to set up our terminal.
	// Eventually this will be a userspace program. 
	registerCommand((Command) { testKalloc, 0, "kalloc", 0, 0 });
	registerCommand((Command) { mem_alloc, 0, "mem_alloc", 0, 0 });
	registerCommand((Command) { acpi_command, 0, "acpi", 0, 0 });
	registerCommand((Command) { syscall_command, 0, "syscall", 0, 0 });
	registerCommand((Command) { bootdev_command, 0, "bootdev", 0, 0 });
	terminalMain();
}