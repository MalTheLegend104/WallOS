#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <klibc/kprint.h>
#include <klibc/logger.h>
#include <memory/physical_mem.hpp>
#include <memory/virtual_mem.hpp>

#include <terminal/terminal.h>
#include <terminal/commands/systemCommands.h>

extern "C" {
	extern uint64_t kernel_end;
	int meminfo(int argc, char** argv);
	int meminfo_help(int argc, char** argv);
}

const mmap_info* memory_info;

void printTotal() {
	set_colors(VGA_COLOR_PINK, VGA_DEFAULT_BG);
	printf("Total Memory:\n");
	set_to_last();
	set_colors(VGA_COLOR_PURPLE, VGA_DEFAULT_BG);
	printf("\t%llu bytes\n\t%llu KiB\n\t%llu MiB\n", memory_info->total, memory_info->total / 1024, (memory_info->total / 1024) / 1024);
	set_to_last();
}

void printUsable() {
	set_colors(VGA_COLOR_LIGHT_GREEN, VGA_DEFAULT_BG);
	printf("Usable Memory:\n");
	set_to_last();
	set_colors(VGA_COLOR_GREEN, VGA_DEFAULT_BG);
	printf("\t%llu bytes\n\t%llu KiB\n\t%llu MiB\n", memory_info->usable, memory_info->usable / 1024, (memory_info->usable / 1024) / 1024);
	set_to_last();
}

void printReserved() {
	set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
	printf("Reserved Memory:\n");
	set_to_last();
	set_colors(VGA_COLOR_RED, VGA_DEFAULT_BG);
	printf("\t%llu bytes\n\t%llu KiB\n\t%llu MiB\n", memory_info->reserved, memory_info->reserved / 1024, (memory_info->reserved / 1024) / 1024);
	set_to_last();
}

void printMapSize() {
	uint64_t memory_map_size = Memory::Info::getPhysKernelEnd() - ((uintptr_t) (&kernel_end) - KERNEL_VIRTUAL_BASE);
	set_colors(VGA_COLOR_LIGHT_GREY, VGA_DEFAULT_BG);
	printf("System Memory Map Size:\n");
	set_to_last();
	set_colors(VGA_COLOR_DARK_GREY, VGA_DEFAULT_BG);
	printf("\t%llu bytes\n\t%llu KiB\n\t%llu MiB\n", memory_map_size, memory_map_size / 1024, (memory_map_size / 1024) / 1024);
	set_to_last();
}

void printKernelSize() {
	set_colors(VGA_COLOR_LIGHT_CYAN, VGA_DEFAULT_BG);
	printf("Raw Kernel Size:\n");
	set_to_last();
	set_colors(VGA_COLOR_CYAN, VGA_DEFAULT_BG);
	uint64_t k_end = (uint64_t) &kernel_end - KERNEL_VIRTUAL_BASE;
	printf("\t%llu bytes\n\t%llu KiB\n\t%llu MiB\n", k_end, k_end / 1024, (k_end / 1024) / 1024);
	set_to_last();
}

void printFreePhysical() {
	size_t free_phys_pages = Memory::Info::getFreePageCount();
	set_colors(VGA_COLOR_YELLOW, VGA_DEFAULT_BG);
	printf("Free Physical Pages: %llu\n", free_phys_pages);
	set_to_last();
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
		} else if (strcmp(argv[i], "--mmap-size") == 0) {
			printMapSize();
			printedSomething = true;
		} else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--kernel") == 0) {
			printKernelSize();
			printedSomething = true;
		} else if (strcmp(argv[i], "-fp") == 0 || strcmp(argv[i], "--free-physical") == 0) {
			printFreePhysical();
			printedSomething = true;
		}
	}
	return printedSomething;
}

int meminfo(int argc, char** argv) {
	memory_info = Memory::Info::getMMapInfo();

	if (argc > 1) {
		if (printIndividual(argc, argv)) return 0;
	}

	/* Total Memory */
	printTotal();

	/* Usable Memory */
	printUsable();

	/* Reserved Memory */
	printReserved();

	/* System Memory Map Size */
	printMapSize();

	/* Raw Kernel Size */
	printKernelSize();

	/* Free Physical Pages */
	printFreePhysical();
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
				"Prints the amount of free physical pages in memory.\n\nThis is mostly for debugging purposes, but may be interesting to users.\nThe system uses 2MB physical pages, multiplying this number by 2MB (0x200000), should give you roughly the usable memory.",
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
		"--total,",
		"-t          -> Prints the amount of total system memory.\n",
		"--usable",
		"-u          -> Prints the amount of usable system memory.\n",
		"--reserved,",
		"-r          -> Prints the amount of reserved system memory.\n",
		"--mmap-size -> Prints the size of the system memory map.\n",
		"--kernel,",
		"-k          -> Prints the size of the raw kernel.\n",
		"--free-physical,",
		"-fp         -> Prints the amount of free physical pages in memory.\n",

		"If no flags are provided it will print all of the above.",
	};
	HelpEntry entry = {
		"MemInfo",
		"Command to display useful memory information.",
		NULL,
		0,
		optional,
		11
	};
	printSpecificHelp(&entry);

	return 0;
}