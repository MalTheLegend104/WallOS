#include <memory/physical_mem.h>
#include <stdlib.h>
#include <string.h>
#include <panic.h>
#include <stdio.h>
#include <klibc/kprint.h>
#include <klibc/logger.h>
#include <multiboot.h>
#include <klibc/multiboot.hpp>
#include <idt.h>

#pragma GCC diagnostic ignored "-Wunused-parameter" 
int memtest(int argc, char** argv) {
	multiboot_tag_mmap* mmap_tag = MultibootManager::getMMap();
	logger(INFO, "Entry size: %d\n", mmap_tag->entry_size);
	logger(INFO, "Entries: %d\n", mmap_tag->size / mmap_tag->entry_size);
	struct multiboot_mmap_entry* mmap;
	size_t total = 0, usable = 0, reserved = 0;
	for (mmap = mmap_tag->entries; (size_t) mmap < (size_t) mmap_tag + mmap_tag->size; mmap = (struct multiboot_mmap_entry*) ((size_t) mmap + (size_t) mmap_tag->entry_size)) {

		// printf("New Entry:\tBase: %X", mmap->addr);
		// printf("\tLength: %llu", mmap->len);
		// printf("\tType: %llu\n", mmap->type);
		total += mmap->len;
		if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
			usable += mmap->len;
		} else {
			logger(WARN, "Unusable memory: Addr: %X Reason: %d Bytes: %llu\n", mmap->addr, mmap->type, mmap->len);
			reserved += mmap->len;
		}
	}
	set_colors(VGA_COLOR_PINK, VGA_DEFAULT_BG);
	printf("Total Memory:\n");
	set_to_last();
	set_colors(VGA_COLOR_PURPLE, VGA_DEFAULT_BG);
	printf("\t%llu bytes\n\t%llu KiB\n\t%llu MiB\n", total, total / 1024, (total / 1024) / 1024);

	set_to_last();
	set_colors(VGA_COLOR_LIGHT_GREEN, VGA_DEFAULT_BG);
	printf("Usable Memory:\n");
	set_to_last();
	set_colors(VGA_COLOR_GREEN, VGA_DEFAULT_BG);
	printf("\t%llu bytes\n\t%llu KiB\n\t%llu MiB\n", usable, usable / 1024, (usable / 1024) / 1024);

	set_to_last();
	set_colors(VGA_COLOR_LIGHT_RED, VGA_DEFAULT_BG);
	printf("Reserved Memory:\n");
	set_to_last();
	set_colors(VGA_COLOR_RED, VGA_DEFAULT_BG);
	printf("\t%llu bytes\n\t%llu KiB\n\t%llu MiB\n", reserved, reserved / 1000, (reserved / 1000) / 1000);
	set_to_last();
	return 0;
}
