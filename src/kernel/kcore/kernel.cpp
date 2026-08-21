#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <panic.h>
#include <multiboot.h>

#include <acpi/acpi_init.h>

#include <drivers/keyboard.h>
#include <drivers/serial.h>
#include <drivers/sata/pio.h>
#include <drivers/pci.h>

#include <klibc/kprint.h>
#include <klibc/cpuid_calls.h>
#include <klibc/logger.h>
#include <klibc/features.hpp>
#include <klibc/multiboot.h>
#include <klibc/display.h>

#include <memory/physical_mem.hpp>
#include <memory/virtual_mem.h>
#include <memory/kernel_alloc.h>

#include <system/idt.h>
#include <system/cpuid.h>
#include <system/timer.h>

#include <x86_64/timing.h>

#include <terminal/terminal.h>
#include <terminal/commands/system_commands.h>

#include <ff.h>


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
	void __cxa_pure_virtual() {}; // needed for pure virtual functions
}

#pragma GCC diagnostic ignored "-Wunused-parameter" 
int bootdev_command(int argc, char** argv) {
	const auto a = getBootDev();
	printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "BIOS Drive Number: 0x%x\n", a->biosdev);

	display_set_colors(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG);
	printf("Partition: %d (not relevant for floppy or cd-rom)\n", a->part);
	printf("SubPart: %d (not relevant for floppy or cd-rom)\n", a->slice);
	display_set_colors_default();

	display_set_colors(PRINT_COLOR_PINK, PRINT_DEFAULT_BG);
	switch (a->biosdev) {
		case 0x00: printf("Boot device assumed to be floppy. How did you get here...?\n"); break;
		case 0x80: printf("Boot device assumed to be hard drive.\n"); break;
		case 0xE0: printf("Boot device assumed to be CD-ROM (or similar).\n"); break;
		default: printf("Boot device unknown. How did you get here...? (seriously please let me know)\n");
	}
	display_set_colors_default();
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

/* TODO: Remove this when PMM is fixed. */
#define JANKY_INITRD_LOADER
#ifdef JANKY_INITRD_LOADER
extern "C" {
	extern uint64_t _initrd_start_;
	extern uint64_t _initrd_end_;
	uint64_t _initrd_size;
	uint8_t* _initrd_data;
}

void init_initrd() {
	_initrd_size = (size_t) &_initrd_end_ - (size_t) &_initrd_start_;
	_initrd_data = (uint8_t*) &_initrd_start_;

	printf_serial("[initrd] Initrd start: 0x%llx\r\n", &_initrd_start_);
	printf_serial("[initrd] Initrd end: 0x%llx\r\n", &_initrd_end_);
	printf_serial("[initrd] Initrd size: %llu bytes\r\n", (uint64_t) &_initrd_end_ - (uint64_t) &_initrd_start_);
}
#endif // JANKY_INITRD_LOADER


/**
 * This allows write-combining.
 * This is mainly for the framebuffer (but likely helps us elsewhere).
 */
void init_pat() {
	uint32_t low, high;
	// Read IA32_PAT MSR (0x277)
	asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0x277));

	// We modify Slot 7 (top 8 bits of the high 32-bit register)
	// Clear bits 56-63 and set them to 0x01 (Write-Combining)
	high &= ~(0xFFULL << 24);
	high |= (0x01ULL << 24);

	asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(0x277));
}

#include <scheduler/scheduler.h>
int temp_cmd(int, char**) {
	arch_init_cpus();
	return 0;
}

extern "C" void setup_serial_interrupts();
extern "C" int virt_mem_cli(int argc, char** argv);

/* These are for printing out information about PS/2 controller state.
 * I have had issues with PS/2 on some devices but not others.
 * It mostly has to do with how well some BIOS's emulate the controller.
 * I've honestly kind of resigned to the fact that I *may* not have keyboard support until I get a simple USB driver.
 */
#include <cpu_io.h>
static inline bool wait_output() {
	for (int i = 0; i < 50000; i++)
		if (inb(0x64) & 0x01) return true;
	return false;
}

void keyboard_debug() {
	// Status register (port 0x64)
	uint8_t status = inb(0x64);
	printf_serial("=== PS/2 8042 Debug ===\r\n");
	printf_serial("Status Register (0x%02x):\r\n", status);
	printf_serial("  Output buffer full:  %d\r\n", (status >> 0) & 1);
	printf_serial("  Input buffer full:   %d\r\n", (status >> 1) & 1);
	printf_serial("  System flag:         %d\r\n", (status >> 2) & 1);
	printf_serial("  Command/data:        %d\r\n", (status >> 3) & 1);
	printf_serial("  Keyboard locked:     %d\r\n", (status >> 4) & 1);
	printf_serial("  Aux buffer full:     %d\r\n", (status >> 5) & 1);
	printf_serial("  Timeout error:       %d\r\n", (status >> 6) & 1);
	printf_serial("  Parity error:        %d\r\n", (status >> 7) & 1);

	// Read config byte (command 0x20)
	uint8_t config = 0xFF;
	outb(0x64, 0x20);
	if (wait_output()) config = inb(0x60);
	printf_serial("Config Byte (0x%02x):\r\n", config);
	printf_serial("  Port 1 interrupt:    %d\r\n", (config >> 0) & 1);
	printf_serial("  Port 2 interrupt:    %d\r\n", (config >> 1) & 1);
	printf_serial("  System flag:         %d\r\n", (config >> 2) & 1);
	printf_serial("  Port 1 clock:        %d (0=enabled)\r\n", (config >> 4) & 1);
	printf_serial("  Port 2 clock:        %d (0=enabled)\r\n", (config >> 5) & 1);
	printf_serial("  Port 1 translation:  %d\r\n", (config >> 6) & 1);
	printf_serial("======================\r\n");
}

#include <device/device_manager.h>
#include <drivers/driver_manager.h>
#include <drivers/sata/ahci.h>
#include <filesystem/wdm.h>
#include <filesystem/initrd.h>
#include <filesystem/vfs.h>
#include <filesystem/fatfs_vfs.h>
#include <klibc/internal_calls.h>
#include <klibc/kernel_rng.h>
#include <drivers/usb/hosts/xhci.h>

extern "C" int tst_command(int argc, char** argv) {
	// this is just going to be a "permanent" command to let me quickly test things.
	// I routinely end up adding and removing this stupid command signature from this file when needing to do targeted testing.

	(void) argc;
	(void) argv;

	return 0;
}

void kernel_main(unsigned int magic, multiboot_info* mbt_info) {
	// ------------------------------------------------------------------------------------------------
	// Very early init
	// We *are not* guaranteed to have any graphics until after the framebuffer is set up.
	// ------------------------------------------------------------------------------------------------
	// If we have VGA Text Mode, we set it up before everything else.
	// If we don't we have to wait for framebuffer init (which relies on a lot of this early init).
	init_pat();

	initScreen();

	// Tries to initialize all COM port 1-4, if present.
	// Information about serial can be accessed using the `serial` command, to see what got loaded.
	init_all_serial();

	printf_serial("Welcome to WallOS!\r\n");
	Memory::initVirtualMemory();

	MultibootManager::initialize(magic, mbt_info);
	cpu_features f = cpuFeatures();
	Features::checkFeatures(&f);
	// Features::enableFeatures();

	rng_seed(rdtsc());

	// This inits the first 22 interrupts + the PIT interrupt (PIT is disabled at this point).
	initIDT();

	// printf_serial("Kernel Mapping End: 0x%llx\r\nRSDP ADDR: 0x%llx\r\n\r\n", Memory::GetMappingEnd(), MultibootManager::getACPI()->rsdp);

	// ------------------------------------------------------------------------------------------------
	// Framebuffer
	// ------------------------------------------------------------------------------------------------
	multiboot_tag_framebuffer* e = MultibootManager::getFramebufferTag();

	display_mode_t display_mode = DISPLAY_MODE_VGA_TEXT;
	if (e->common.framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) display_mode = DISPLAY_MODE_FRAMEBUFFER;
	if (e->common.framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT) display_mode = DISPLAY_MODE_VGA_TEXT;

	Memory::mapFramebuffer(
		(uintptr_t) e->common.framebuffer_addr,
		e->common.framebuffer_height * e->common.framebuffer_pitch,
		display_mode == DISPLAY_MODE_VGA_TEXT
	);
	display_init(display_mode);

	// ------------------------------------------------------------------------------------------------
	// Initrd
	// ------------------------------------------------------------------------------------------------
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

	// I need to move this to be a multiboot module...
	// I mean this works fine and initrd is limited to 2MB so...
	init_initrd();
	/* initrd is always drive 0:
	 * I don't entirely know if I want to keep that as an explicit path or
	 * hide it in /initrd in the virtual FS whenever we get one set up.
	 */
	// mount_drive(0);

	// ------------------------------------------------------------------------------------------------
	// Early Interrupt Handlers
	// ------------------------------------------------------------------------------------------------
	// Things that use interrupts but other init steps depend on should be set here.
	// Everything else should come in the regular interrupt handler section.
	// Everything that needs an IRQ should be done after the PIT as it messes with the mask
	// If it requires allocations, add it after `initKernelAllocator()`
	io_delay_init(); // uses outb(0x80, 0) to try to add a small delay. it's best effort if we don't have a good resolution timer
	pit_init(1000);
	rtc_cmos_init();
	timer_no_interrupts_init(); // sets up TSC timing for ACPI on x86_64

	i8042_flush();
	keyboard_init();
	keyboard_debug();

	// ------------------------------------------------------------------------------------------------
	// Physical Memory & Allocators
	// ------------------------------------------------------------------------------------------------
	Memory::PhysicalMemInit();
	initKernelAllocator();

	// The last thing this requires is the allocator.
	display_init_late();
	// ------------------------------------------------------------------------------------------------
	// Regular Interrupt Handlers
	// ------------------------------------------------------------------------------------------------
	Syscall::initialize();
	setup_serial_interrupts();


	// ------------------------------------------------------------------------------------------------
	// ACPI
	// ------------------------------------------------------------------------------------------------
	uint64_t acpi_runtime = timer_uptime_ms();
	initialize_acpi();
	acpi_runtime = timer_uptime_ms() - acpi_runtime;
	printf_color(PRINT_COLOR_PINK, PRINT_DEFAULT_BG, "ACPI Init took a total of %llu ms\n", acpi_runtime);

	// ------------------------------------------------------------------------------------------------
	// Device Discovery
	// ------------------------------------------------------------------------------------------------
	serial_register_devices();
	pci_discover();
	// uhci_register();

	// Not really device discovery, but it needs to exist after acpi init so...
	hpet_init();
	pit_init_dev();

	// ------------------------------------------------------------------------------------------------
	// Drive detection (and eventual virtual FS setup)
	// ------------------------------------------------------------------------------------------------
	// I have no better spot to put this so it goes here.
	detect_ide_drives();
	// TODO: I commented this out because initializing a specific drive on one of my test PCs takes FOREVER
	// Need to re-enable this when done with AHCI
	// ahci_register_driver();

	WDM_Init();
	WDM_DriveHandle initrd = initrd_wdm_init(INITRD_FLAG_NONE);
	if (!initrd) panic_s("initrd: WDM registration failed");
	// mount_drive(0, initrd); // pdrv 0, same as before
	// fatfs_vfs_ctx_t* ctx = fatfs_vfs_alloc_ctx(initrd, 9);
	// VFS_Mount("/initrd", initrd, &fatfs_vfs_ops, ctx);

	mount_drive("/initrd", initrd, 0);

	// register_usb_controller_drivers();
	xhci_init();

	dm_bind_all_registered();

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Everything after this point should be handoff code.
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	printf_serial("Ended kernel init... handing control to WallShell.\r\n");
	printf_color(PRINT_COLOR_PINK, PRINT_DEFAULT_BG, "Ended kernel init... Press `ESC` to hand control to WallShell.\r\n");

	// After we're done checking features, we need to set up our terminal.
	// Eventually this will be a userspace program. 
	registerCommand((Command) { testKalloc, 0, "kalloc", 0, 0 });
	registerCommand((Command) { mem_alloc, 0, "mem_alloc", 0, 0 });
	registerCommand((Command) { acpi_command, 0, "acpi", 0, 0 });
	registerCommand((Command) { syscall_command, 0, "syscall", 0, 0 });
	registerCommand((Command) { bootdev_command, 0, "bootdev", 0, 0 });
	registerCommand((Command) { serial_cli_cmd, 0, "serial", 0, 0 });
	registerCommand((Command) { virt_mem_cli, 0, "vmm", 0, 0 });
	registerCommand((Command) { device_cmd, 0, "device", dev_aliases, 1 }); // "dev" is the only alias.
	registerCommand((Command) { cpu_info, 0, "cpu", 0, 0 });
	registerCommand((Command) { driver_cli, 0, "driver", 0, 0 });
	// registerCommand((Command) { tst_command, 0, "tst", 0, 0 });
	terminalMain();

	// char* cmd[] = { "dev", "list", "-u" };
	// device_cmd(3, cmd);

	// char* cmd2[] = { "dev", "list", "-b" };
	// device_cmd(3, cmd2);

	// // char* cmd2[] = { "time", "-i" };
	// // time_command(2, cmd2);

	// while (true) WALLOS_HLT();
}