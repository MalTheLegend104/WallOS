#include <memory/physical_mem.h>
#include <stdlib.h>
#include <string.h>
#include <panic.h>
#include <stdio.h>
#include <klibc/kprint.h>
#include <klibc/logger.h>
#include <idt.h>

#pragma GCC diagnostic ignored "-Wunused-parameter" 
int memtest(int argc, char** argv) {
	struct multiboot_tag_mmap* mmap_tag = MultibootManager::getMMap();
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

#define MAX_BLOCK_SIZE 4096 // 4KiB pages
#define MAX_CHUNKS 256
__attribute__((__packed__))
typedef struct Block {
	size_t pointer;
	bool free;
	Block* next_block;
} Block;
__attribute__((__packed__))
typedef struct Chunk {
	size_t size;
	bool usable;
	Chunk* next_Chunk;
	Block* block_list;
	size_t list_length;
} Chunk;

Chunk* chunk_list;

void map_chunk(uint64_t* start_address, size_t length, bool useable) {
	if (!useable) return;
	size_t max_pages = length / 4096;
	// At the very beginning of each chunk of memory, we're going to put the chunk header, followed by the list of blocks
	Chunk* chunk = (Chunk*) start_address;
	chunk->size = length;
	chunk->usable = useable;
	chunk->block_list = (Block*) (start_address += sizeof(Chunk));
	chunk->list_length = max_pages;
	if (chunk_list != NULL) {
		chunk_list->next_Chunk = chunk;
	}
	chunk_list = chunk;

	// Write all the blocks in the chunk
	Block* first_block = (Block*) (start_address += sizeof(Chunk));
	first_block->next_block = (first_block += sizeof(Block));
	first_block->pointer;
	first_block->free = true;
	// We've already allocated block 0
	Block* currentChunk = chunk->block_list;
	for (size_t i = 1; i < max_pages - 1; i++) {

	}

	// Mark the pages occupied the chunk and blocklist as taken
	// Calculate the size of the chunk and all blocks, then see how many pages it takes up
	size_t size = sizeof(Chunk) + (sizeof(Block) * max_pages);
	size_t pages_taken = (size / 4096) + 1;
	for (size_t i = 0; i < pages_taken; i++) {
		chunk->block_list[i].free = false;
	}
}

void physical_mem_init() {
	struct multiboot_tag_mmap* mmap_tag = MultibootManager::getMMap();
	struct multiboot_mmap_entry* mmap;
	size_t total = 0, usable = 0, reserved = 0;
	for (mmap = mmap_tag->entries; (size_t) mmap < (size_t) mmap_tag + mmap_tag->size; mmap = (struct multiboot_mmap_entry*) ((size_t) mmap + (size_t) mmap_tag->entry_size)) {
		total += mmap->len;
		if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
			usable += mmap->len;
		} else {
			logger(WARN, "Unusable memory: Addr: %X Reason: %d Bytes: %llu\n", mmap->addr, mmap->type, mmap->len);
			reserved += mmap->len;
		}
	}
}

void physical_alloc() {

}