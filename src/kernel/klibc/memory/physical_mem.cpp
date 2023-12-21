#include <memory/physical_mem.h>
#include <stdlib.h>
#include <string.h>
#include <panic.h>
#include <stdio.h>
#include <klibc/kprint.h>
#include <klibc/logger.h>
#include <idt.h>

extern uint64_t endkernel;

typedef struct Block {
	size_t pointer;
	bool free;
	Block* next_block;
} Block;
typedef struct Chunk {
	size_t size;
	Chunk* next_Chunk;
	Block* block_list;
	size_t list_length;
} Chunk;

Chunk* chunk_list;

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

/**
 * @brief Maps a chunk of memory. This is cursed.
 *
 * @param start_address Start address of the chunk of memory
 * @param length Length of the chunk of memory
 * @param type Type of memory chunk, as defined by the Multiboot2 Memory Map Tag
 */
void map_chunk(uintptr_t start_address, size_t length, uint32_t type) {
	if (type != MULTIBOOT_MEMORY_AVAILABLE) return;
	//if (start_address >= 0xffffffff) return;
	if (start_address < 0x100000) return;
	if (start_address == 0x100000) {
		printf("Length of Kernel: %llu -> start addr %llu\n", length, start_address);
		return;
	}
	size_t max_pages = length / 0x1000;
	// At the very beginning of each chunk of memory, we're going to put the chunk header, followed by the list of blocks
	Chunk* chunk = (Chunk*) start_address;
	chunk->size = length;
	chunk->block_list = (Block*) (start_address += sizeof(Chunk));
	chunk->list_length = max_pages;
	if (chunk_list != NULL) {
		chunk_list->next_Chunk = chunk;
	}
	chunk_list = chunk;

	// Calculate the size of the linked list, then see how many pages it takes up
	size_t size = sizeof(Chunk) + (sizeof(Block) * max_pages);
	size_t pages_taken = (size / 0x1000) + 1;

	// Write all the blocks in the chunk
	Block* first_block = (Block*) (start_address + sizeof(Chunk));
	first_block->next_block = (first_block + sizeof(Block));
	first_block->pointer = start_address + (0x1000 * pages_taken);
	first_block->free = false; // The first block is always going to be taken by the linked list

	// We've already allocated block 0
	for (size_t i = 1; i < max_pages - 1; i++) {
		chunk->block_list[i].free = true;
		chunk->block_list[i].pointer = chunk->block_list[i - 1].pointer + 0x1000;
		chunk->block_list[i].next_block = &(chunk->block_list[i + 1]);
	}

	chunk->block_list[max_pages - 1].free = true;
	chunk->block_list[max_pages - 1].pointer = chunk->block_list[max_pages - 2].pointer + 0x1000;
	chunk->block_list[max_pages - 1].next_block = NULL;

	// Mark the pages occupied the chunk and blocklist as taken
	for (size_t i = 0; i < pages_taken; i++) {
		chunk->block_list[i].free = false;
	}
}

void physical_mem_init() {
	struct multiboot_tag_mmap* mmap_tag = MultibootManager::getMMap();
	struct multiboot_mmap_entry* mmap;
	set_colors(VGA_COLOR_LIGHT_CYAN, VGA_DEFAULT_BG);
	printf("Initalizing Physical Memory Allocator\n");
	set_to_last();
	set_colors(VGA_COLOR_LIGHT_CYAN, VGA_DEFAULT_BG);
	for (mmap = mmap_tag->entries; (size_t) mmap < (size_t) mmap_tag + mmap_tag->size; mmap = (struct multiboot_mmap_entry*) ((size_t) mmap + (size_t) mmap_tag->entry_size)) {
		printf("\tMemory Chunk: 0x%llx -> %llu bytes -> Type %d\n", mmap->addr, mmap->len, mmap->type);
		map_chunk(mmap->addr, mmap->len, mmap->type);
	}
	set_to_last();
}

void physical_alloc() {

}

void physical_dealloc() {

}