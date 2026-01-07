#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <klibc/kprint.h>
#include <klibc/logger.h>
#include <memory/physical_mem.hpp>
#include <memory/virtual_mem.h>

#include <terminal/terminal.h>
#include <terminal/commands/system_commands.h>

extern "C" {
	extern uint64_t kernel_end;
	int meminfo(int argc, char** argv);
	int meminfo_help(int argc, char** argv);
}

extern size_t mem_map_size;

const mmap_info* memory_info;

void printTotal() {
	printf_color(PRINT_COLOR_PINK, PRINT_DEFAULT_BG, "Total Memory:\n");

	printf_color(PRINT_COLOR_PURPLE, PRINT_DEFAULT_BG, "\t%llu bytes\n\t%llu KiB\n\t%llu MiB\n",
		memory_info->total,
		memory_info->total / 1024,
		memory_info->total / 1024 / 1024);

}

void printUsable() {
	printf_color(PRINT_COLOR_LIGHT_GREEN, PRINT_DEFAULT_BG, "Usable Memory:\n");

	printf_color(PRINT_COLOR_GREEN, PRINT_DEFAULT_BG, "\t%llu bytes\n\t%llu KiB\n\t%llu MiB\n",
		memory_info->usable,
		memory_info->usable / 1024,
		memory_info->usable / 1024 / 1024);

}

void printReserved() {
	printf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, "Reserved Memory:\n");

	printf_color(PRINT_COLOR_RED, PRINT_DEFAULT_BG, "\t%llu bytes\n\t%llu KiB\n\t%llu MiB\n",
		memory_info->reserved,
		memory_info->reserved / 1024,
		memory_info->reserved / 1024 / 1024);

}

void printMapSize() {
	printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "System Memory Map Size:\n");

	printf_color(PRINT_COLOR_DARK_GREY, PRINT_DEFAULT_BG, "\t%llu bytes\n\t%llu KiB\n\t%llu MiB\n",
		mem_map_size,
		mem_map_size / 1024,
		mem_map_size / 1024 / 1024);

}

void printKernelSize() {
	printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "Raw Kernel Size:\n");

	display_set_colors(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG);
	uint64_t k_end = (uint64_t) &kernel_end - KERNEL_VIRTUAL_BASE;
	printf("\t%llu bytes\n\t%llu KiB\n\t%llu MiB\n",
		k_end,
		k_end / 1024,
		k_end / 1024 / 1024);
	display_set_colors_default();
}

void printFreePhysical() {
	size_t free_bytes = Memory::Info::getTotalFreeBytes();
	size_t free_pages = Memory::Info::getFreePageCount();

	printf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, "Free Physical Memory:\n");

	display_set_colors(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG);
	printf("\t%llu pages (4KB)\n", free_pages);
	printf("\t%llu bytes\n\t%llu KiB\n\t%llu MiB\n",
		free_bytes,
		free_bytes / 1024,
		free_bytes / 1024 / 1024);
	display_set_colors_default();
}

void printUsedPhysical() {
	size_t used_bytes = Memory::Info::getTotalUsedBytes();
	size_t used_pages = Memory::Info::getUsedPageCount();

	printf_color(PRINT_COLOR_LIGHT_BLUE, PRINT_DEFAULT_BG, "Used Physical Memory:\n");

	display_set_colors(PRINT_COLOR_BLUE, PRINT_DEFAULT_BG);
	printf("\t%llu pages (4KB)\n", used_pages);
	printf("\t%llu bytes\n\t%llu KiB\n\t%llu MiB\n",
		used_bytes,
		used_bytes / 1024,
		used_bytes / 1024 / 1024);
	display_set_colors_default();
}

bool printIndividual(int argc, char** argv) {
	bool printedSomething = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--total") == 0) {
			printTotal();
			printedSomething = true;
		} else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--usable") == 0) {
			printUsable();
			printedSomething = true;
		} else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--reserved") == 0) {
			printReserved();
			printedSomething = true;
		} else if (strcmp(argv[i], "-ms") == 0 || strcmp(argv[i], "--mmap-size") == 0) {
			printMapSize();
			printedSomething = true;
		} else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--kernel") == 0) {
			printKernelSize();
			printedSomething = true;
		} else if (strcmp(argv[i], "-fp") == 0 || strcmp(argv[i], "--free-physical") == 0) {
			printFreePhysical();
			printedSomething = true;
		} else if (strcmp(argv[i], "-up") == 0 || strcmp(argv[i], "--used-physical") == 0) {
			printUsedPhysical();
			printedSomething = true;
		}
	}
	return printedSomething;
}

int meminfo(int argc, char** argv) {
	memory_info = Memory::Info::getMMapInfo();

	if (argc > 1) {
		if (printIndividual(argc, argv)) {
			return 0;
		}
	}

	printTotal();
	printUsable();
	printReserved();
	printMapSize();
	printKernelSize();
	printFreePhysical();
	printUsedPhysical();

	return 0;
}

int meminfo_help(int argc, char** argv) {
	if (argc > 1) {
		if (strcmp(argv[1], "-t") == 0 || strcmp(argv[1], "--total") == 0) {
			HelpEntry entry = {
				"MemInfo (Total)",
				"Prints the amount of total system memory.\n\nThis memory is NOT guaranteed to all be usable. The system may have reserved memory for peripherals, bios structures, etc.",
				NULL,
				0,
				NULL,
				0
			};
			printSpecificHelp(&entry);
			return 0;
		} else if (strcmp(argv[1], "-u") == 0 || strcmp(argv[1], "--usable") == 0) {
			HelpEntry entry = {
				"MemInfo (Usable)",
				"Prints the amount of usable system memory.\n\nThis memory is all the memory that can be used for general purposes by the OS.",
				NULL,
				0,
				NULL,
				0
			};
			printSpecificHelp(&entry);
			return 0;
		} else if (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "--reserved") == 0) {
			HelpEntry entry = {
				"MemInfo (Reserved)",
				"Prints the amount of reserved system memory.\n\nThis memory is NOT usable. The system may have reserved memory for peripherals, bios structures, etc. The OS has no control over this.",
				NULL,
				0,
				NULL,
				0
			};
			printSpecificHelp(&entry);
			return 0;
		} else if (strcmp(argv[1], "--mmap-size") == 0) {
			HelpEntry entry = {
				"MemInfo (System Memory Map Size)",
				"Prints the size of the system memory map.\n\nThis is mostly for debugging purposes, but may be interesting to users.",
				NULL,
				0,
				NULL,
				0
			};
			printSpecificHelp(&entry);
			return 0;
		} else if (strcmp(argv[1], "-k") == 0 || strcmp(argv[1], "--kernel") == 0) {
			HelpEntry entry = {
				"MemInfo (Kernel)",
				"Prints the size of the raw kernel.\n\nThis does NOT include memory reserved by the kernel during operation, it is the size of the kernel that was loaded into memory on boot.",
				NULL,
				0,
				NULL,
				0
			};
			printSpecificHelp(&entry);
			return 0;
		} else if (strcmp(argv[1], "-fp") == 0 || strcmp(argv[1], "--free-physical") == 0) {
			HelpEntry entry = {
				"MemInfo (Free Physical Pages)",
				"Prints the amount of free physical memory.\n\nShows both the number of 4KB pages tracked by the buddy allocator and the total free bytes in various units (bytes, KiB, MiB).",
				NULL,
				0,
				NULL,
				0
			};
			printSpecificHelp(&entry);
			return 0;
		} else if (strcmp(argv[1], "-up") == 0 || strcmp(argv[1], "--used-physical") == 0) {
			HelpEntry entry = {
				"MemInfo (Used Physical Memory)",
				"Prints the amount of used physical memory.\n\nShows both the number of 4KB pages in use and the total used bytes in various units (bytes, KiB, MiB). Calculated as usable memory minus free memory.",
				NULL,
				0,
				NULL,
				0
			};
			printSpecificHelp(&entry);
			return 0;
		}
	}

	const char* optional[] = {
		"--total, -t      -> Prints the amount of total system memory.\n",
		"--usable, -u     -> Prints the amount of usable system memory.\n",
		"--reserved,-r    -> Prints the amount of reserved system memory.\n",
		"--mmap-size, -ms -> Prints the size of the system memory map.\n",
		"--kernel, -k     -> Prints the size of the raw kernel.\n",
		"--free-physical,",
		"-fp              -> Prints the amount of free physical memory.\n",
		"--used-physical,",
		"-up              -> Prints the amount of used physical memory.\n",
		"If no flags are provided it will print all of the above.",
	};
	HelpEntry entry = {
		"MemInfo",
		"Command to display useful memory information.",
		NULL,
		0,
		optional,
		10
	};
	printSpecificHelp(&entry);

	return 0;
}