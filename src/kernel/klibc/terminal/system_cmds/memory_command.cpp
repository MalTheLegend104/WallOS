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
#include <terminal/wall_shell.h>

extern "C" {
	extern uint64_t kernel_end;
	int meminfo(int argc, char** argv);
}

extern size_t mem_map_size;

const mmap_info* memory_info;

// Defined with extern "C" linkage so it can be referenced from system_commands.c
// (a plain C translation unit) in registerSystemCommands().
extern "C" const ws_command_argument_t meminfo_args[] = {
	{ WS_ARG_TYPE_FLAG, false, "--total",         "-t",  "Prints the amount of total system memory." },
	{ WS_ARG_TYPE_FLAG, false, "--usable",        "-u",  "Prints the amount of usable system memory." },
	{ WS_ARG_TYPE_FLAG, false, "--reserved",      "-r",  "Prints the amount of reserved system memory." },
	{ WS_ARG_TYPE_FLAG, false, "--mmap-size",     "-ms", "Prints the size of the system memory map." },
	{ WS_ARG_TYPE_FLAG, false, "--kernel",        "-k",  "Prints the size of the raw kernel." },
	{ WS_ARG_TYPE_FLAG, false, "--free-physical", "-fp", "Prints the amount of free physical memory." },
	{ WS_ARG_TYPE_FLAG, false, "--used-physical", "-up", "Prints the amount of used physical memory." },
};
extern "C" const size_t meminfo_args_count = sizeof(meminfo_args) / sizeof(meminfo_args[0]);

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

bool printIndividual(const ws_context_t* ctx) {
	bool printedSomething = false;

	if (ws_get_flag(ctx, "--total")) {
		printTotal();
		printedSomething = true;
	}
	if (ws_get_flag(ctx, "--usable")) {
		printUsable();
		printedSomething = true;
	}
	if (ws_get_flag(ctx, "--reserved")) {
		printReserved();
		printedSomething = true;
	}
	if (ws_get_flag(ctx, "--mmap-size")) {
		printMapSize();
		printedSomething = true;
	}
	if (ws_get_flag(ctx, "--kernel")) {
		printKernelSize();
		printedSomething = true;
	}
	if (ws_get_flag(ctx, "--free-physical")) {
		printFreePhysical();
		printedSomething = true;
	}
	if (ws_get_flag(ctx, "--used-physical")) {
		printUsedPhysical();
		printedSomething = true;
	}

	return printedSomething;
}

int meminfo(int argc, char** argv) {
	memory_info = Memory::Info::getMMapInfo();

	if (argc > 1) {
		ws_context_t* ctx = ws_getCurrentContext();
		if (ws_parse_args(ctx, argc, argv) && printIndividual(ctx)) {
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