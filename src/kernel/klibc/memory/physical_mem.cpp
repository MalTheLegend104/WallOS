#include <memory/physical_mem.hpp>
#include <memory/virtual_mem.hpp>
#include <stdlib.h>
#include <string.h>
#include <panic.h>
#include <stdio.h>
#include <klibc/kprint.h>
#include <klibc/logger.h>
#include <idt.h>

typedef struct Block {
	uintptr_t pointer;
	bool free;
	short size; // 0 if 2mb, 1 if 4kb
	Block* next_block;
} __attribute__((packed)) Block;

Block* block_list = NULL;
Block* last_block = NULL;
Block* last_block_start = NULL;

uintptr_t phys_kernel_end = 0;

extern "C" {
	extern uint64_t kernel_end;
}

uintptr_t Memory::Info::getPhysKernelEnd() {
	return phys_kernel_end;
}

size_t Memory::Info::getFreePageCount() {
	size_t free_phys_pages = 0;
	Block* current = block_list;
	while (current != NULL) {
		if (current->free) free_phys_pages++;
		current = current->next_block;
	}
	return free_phys_pages;
}

/**
 * @brief This is some voodoo magic. It's also poorly commented. GLHF :)
 *
 * @param start_address Start address of the chunk of memory
 * @param length Length of the chunk of memory
 * @param type Type of memory chunk, as defined by the Multiboot2 Memory Map Tag
 */
void map_chunk(uintptr_t start_address, size_t length, uint32_t type) {
	if (type != MULTIBOOT_MEMORY_AVAILABLE) return;
	if (start_address < 0x100000) return;
	if (start_address <= phys_kernel_end) {
		// We have to make sure that we can map the physical memory behind the kernel.
		if (start_address + length < phys_kernel_end) return;
		length = length - (phys_kernel_end - start_address);
		start_address = phys_kernel_end;
	}

	// We want the start address to be on a 2MB boundary.
	uintptr_t old_start_addr = start_address;
	start_address = (start_address + 0x1FFFFF) & ~0x1FFFFF;
	length = length - (start_address - old_start_addr); // Adjust length to start at the new boundary

	printf("\tMemory Chunk: 0x%llx -> 0x%llx bytes\n", start_address, length);
	size_t max_pages = length / PAGE_2MB_SIZE;

	// TODO change start_address to be boundary aligned

	// Calculate the size of the linked list, then see how many pages it takes up
	size_t size = (sizeof(Block) * max_pages);
	size_t pages_taken = (size / PAGE_2MB_SIZE) + 1;

	Block* first_block;
	// Write all the blocks in the chunk
	if (last_block == NULL) {
		last_block = (Block*) ((uint64_t) (&kernel_end));
		first_block = (Block*) (last_block);
	} else {
		first_block = (Block*) (last_block);
		last_block_start->next_block = first_block;
	}
	// We have to round up the start address to the nearest 2mb boundary
	first_block->next_block = NULL;
	first_block->pointer = start_address + (PAGE_2MB_SIZE * pages_taken);
	first_block->free = true;
	first_block->size = 0;
	last_block = first_block + sizeof(Block);
	if (block_list == NULL)
		block_list = first_block;

	// This will map the entire next 2mb block of memory. This avoids a page fault.
	// The page fault handler will handle page faults correctly *after* we initialize the physical allocator.
	// Unfortunately until then we have to be a little bit messy. 
	if ((uintptr_t) last_block + sizeof(Block) >= (Memory::getMappingEnd() + KERNEL_VIRTUAL_BASE)) Memory::MapPreAllocMem((uintptr_t) last_block);

	Block* last = first_block;
	// We've already allocated block 0
	for (size_t i = 1; i <= max_pages - 1; i++) {
		Block* current_block = (Block*) (last_block);
		last->next_block = current_block;
		current_block->next_block = NULL;
		current_block->pointer = start_address + (PAGE_2MB_SIZE * pages_taken);
		current_block->free = true;
		current_block->size = 0;
		last_block_start = current_block;
		last_block = current_block + sizeof(Block);
		last = current_block;
		if ((uintptr_t) last_block + sizeof(Block) >= (Memory::getMappingEnd() + KERNEL_VIRTUAL_BASE)) {
			Memory::MapPreAllocMem((uintptr_t) last_block);
		}
	}

	printf("\t\tTotal Blocks: %llu -> Last Addr: 0x%llx\n", max_pages, start_address + (max_pages * PAGE_2MB_SIZE));
}

void Memory::PhysicalMemInit() {
	struct multiboot_tag_mmap* mmap_tag = MultibootManager::getMMap();
	struct multiboot_mmap_entry* mmap;
	phys_kernel_end = (uint64_t) (&kernel_end) - KERNEL_VIRTUAL_BASE;
	set_colors(VGA_COLOR_YELLOW, VGA_DEFAULT_BG);
	printf("Initalizing Physical Memory Allocator:\n");
	set_to_last();
	set_colors(VGA_COLOR_BROWN, VGA_DEFAULT_BG);
	for (mmap = mmap_tag->entries; (size_t) mmap < (size_t) mmap_tag + mmap_tag->size; mmap = (struct multiboot_mmap_entry*) ((size_t) mmap + (size_t) mmap_tag->entry_size)) {
		map_chunk(mmap->addr, mmap->len, mmap->type);
	}
	set_to_last();

	// We need to get the offset that the memory map has taken up, then mark it as not free.
	// The first "n" number of blocks represent the memory directly behind the kernel
	uintptr_t end_of_map = (uintptr_t) last_block - (uintptr_t) (&kernel_end);
	size_t amount_of_blocks = (end_of_map / PAGE_2MB_SIZE) + 1; // We need to round up a page.


	// Finally, we need to set phys_kernel_end to the new address including the memory map
	// Setting kernel_end becomes a mess, so I wont even bother. 
	// Everything after both memory init functions will use this value and add the virtual base as needed.
	phys_kernel_end = (uintptr_t) last_block - KERNEL_VIRTUAL_BASE;

	size_t index = 0;
	Block* current = block_list;
	while (index < amount_of_blocks) {
		if (current->pointer > phys_kernel_end) break;
		current->free = false;
		current = current->next_block;
		index++;
	}

}

// ------------------------------------------------------------------------------------------------
// We're going to force the kernel allocator and user allocator to get 2mb pages. 
// The allocator will deal with these 2mb by further dividing it up into 4kb pages if needed,
// along with dealing with actually mapping it to the virtual address space. 
// ------------------------------------------------------------------------------------------------
// We're going to keep a pointer to the last allocated block, which makes allocation O(1) normal case
// In the case that the user uses all memory, this will likely end up being O(n) normal
Block* last_allocated_block = NULL;

/**
 * @brief Get a 2MB page in physical memory.
 *
 * @return uintptr_t Pointer to the base of the chunk of memory.
 * Check for a 0 return value, this means it couldn't find a chunk of memory.
 */
uintptr_t Memory::PhysicalAlloc2MB() {
	// First attempt, we check if last_allocated_block.next_block is free
	if (last_allocated_block != NULL && last_allocated_block->next_block != NULL) {
		if (last_allocated_block->next_block->free) {
			last_allocated_block = last_allocated_block->next_block;
			last_allocated_block->free = false;
			return last_allocated_block->pointer;
		}
	}

	// We have to go through the entire map otherwise
	Block* current = block_list;
	while (current != NULL) {
		if (current->free) {
			current->free = false;
			last_allocated_block = current;
			return (current->pointer);
		}
		current = current->next_block;
	}
	return 0; // GCC complains about returning null, bc we're technically returning an int, not a pointer
}

/**
 * @brief Mark the page starting at phys_addr as free.
 * Call memset and clear the memory before passing to this function.
 * Please ensure that phys_addr is the base address of the page.
 *
 * @param phys_addr Base address of the page to be freed.
 */
void Memory::PhysicalDeAlloc2MB(uintptr_t phys_addr) {
	Block* current = block_list;
	while (current != NULL) {
		if (current->pointer == phys_addr) {
			current->free = true;
			return;
		}
		current = current->next_block;
	}
}