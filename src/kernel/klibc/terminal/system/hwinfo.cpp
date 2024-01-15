#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <timing.h>

#include <klibc/kprint.h>
#include <klibc/features.hpp>
#include <memory/physical_mem.hpp>
#include <memory/virtual_mem.hpp>

#include <terminal/terminal.h>
#include <terminal/commands/systemCommands.h>

extern "C" {
	extern uint64_t kernel_end;
	int sysinfo(int argc, char** argv);
	void sysinfo_boot();
}

void printValue(const char* title, const char* format, ...) {
	set_colors(VGA_COLOR_PINK, VGA_DEFAULT_BG);
	printf(title);
	set_to_last();
	set_colors(VGA_COLOR_LIGHT_GREY, VGA_DEFAULT_BG);
	va_list arg;
	va_start(arg, format);
	vprintf(format, arg);
	va_end(arg);
	set_to_last();
}

void printUptime() {
	size_t time = get_system_up_time();
	// Calculate years, months, days, hours, minutes, and seconds
	// Calculate years, months, days, hours, minutes, and seconds
	size_t years = time / (0x16BEE00); // 0x16BEE00 = 1000 * 60 * 60 * 24 * 365
	time %= (0x16BEE00);

	size_t months = time / (0x1C9C380); // 0x1C9C380 = 1000 * 60 * 60 * 24 * 30
	time %= (0x1C9C380);

	size_t days = time / (0x5265C00); // 0x5265C00 = 1000 * 60 * 60 * 24
	time %= (0x5265C00);

	size_t hours = time / (0x36EE80); // 0x36EE80 = 1000 * 60 * 60
	time %= (0x36EE80);

	size_t minutes = time / (0xEA60); // 0xEA60 = 1000 * 60
	time %= (0xEA60);

	size_t seconds = time / 0x3E8; // 0x3E8 = 1000
	time %= 0x3E8;
	set_colors(VGA_COLOR_PINK, VGA_DEFAULT_BG);
	printf("Uptime: ");
	set_to_last();

	set_colors(VGA_COLOR_LIGHT_GREY, VGA_DEFAULT_BG);
	if (years > 0) {
		printf("%lldy ", years);
	}
	if (months > 0) {
		printf("%lldm ", months);
	}
	if (days > 0) {
		printf("%lldd ", days);
	}
	if (hours > 0) {
		printf("%lldh ", hours);
	}
	if (minutes > 0) {
		printf("%lldm ", minutes);
	}
	if (seconds > 0) {
		printf("%llds ", seconds);
	}
	printf("%lldms \n", time);
	set_to_last();
}

// The first physical page begins after the kernel, rounded up.
// If the kernel ends at 1.5MB, the physical page starts at 2MB.
// We have to add the kernel length + used pages
// When mapping physical memory, it automatically marks pages as taken if the memory map runs into them.
// We only want to check the raw kernel end then add the pages after
void printMemInfo() {
	const mmap_info* info = Memory::Info::getMMapInfo();
	uint64_t k_end = (uint64_t) &kernel_end - KERNEL_VIRTUAL_BASE;
	size_t used = (Memory::Info::getUsedPageCount() * PAGE_2MB_SIZE) + k_end;
	if (used < 10000000) { // 10 MiB
		used = (used / 1024); // Make it in KiB
		printValue("Memory: ", "%lluKiB / %lluMiB\n", used, (info->usable / 1024) / 1024);
	} else {
		used = (used / 1024) / 1024; // Make it in MiB
		printValue("Memory: ", "%lluMiB / %lluMiB\n", used, (info->usable / 1024) / 1024);
	}
}

#pragma GCC diagnostic ignored "-Wunused-parameter" 
/**
 * @brief Prints general system information.
 *
 * @param argc Ignored
 * @param argv Ignored
 * @return int Always 0
 */
int sysinfo(int argc, char** argv) {
	/* This is supposed to be similar to neofetch on linux:
	 * OS: WallOS v0.1
	 * Uptime: <time>
	 * Packages: 847 (dpkg), 6 (snap)
	 * Shell: WallOS Shell v1.0
	 * Theme: Adwaita [GTK3]
	 * Icons: Adwaita [GTK3] <------------- both this and theme are replaced with gui info
	 * CPU: AMD Ryzen 7 5825U with Radeon Graphics (16) @ 1.996GHz
	 * Memory: 550MiB / 7594MiB
	 */
	// clearVGABuf();
	// print_logo();
	printf("\n");

	printValue("OS: ", "%s\n", WALLOS_VERSION);
	printUptime();
	printValue("Packages: ", "No package manager yet.\n");
	printValue("Shell: ", "%s\n", WALLOS_SHELL_VERSION);
	printValue("GUI: ", "Default (VGA Text Mode)\n");
	printValue("CPU: ", "%s\n", Features::getCPUName());
	printMemInfo();

	printf("\n");
	return 0;
}

/**
 * @brief Information to display on terminal boot.
 * Should avoid things like uptime, since it's kinda irrelevent at boot time.
 */
void sysinfo_boot() {
	printValue("General System Info:\n", "");
	printValue("OS:     ", "%s\n", WALLOS_VERSION);
	printValue("Shell:  ", "%s\n", WALLOS_SHELL_VERSION);
	printValue("GUI:    ", "Default (VGA Text Mode)\n");
	printValue("CPU:    ", "%s\n", Features::getCPUName());
	printMemInfo();
}