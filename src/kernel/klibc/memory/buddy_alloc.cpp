#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <panic.h>
#include <drivers/serial.h>
#include <klibc/multiboot.h>
#include <memory/virtual_mem.h>

#define MAX_ORDER 11 // This is 8MiB
#define PAGE_SIZE 4096

#define ALIGN_DOWN(x, align)  ((x) & ~((align) - 1))
#define ALIGN_UP(x, align)    (((x) + (align) - 1) & ~((align) - 1))

/* We keep certain flags relating to memory regions.
 * These are meant to be used for debugging (by throwing panics when the PMM is misused),
 * as well as enforcing we never accidentally touch regions we shouldn't.
 */
#define PAGE_USABLE		(1 << 0)  // Currently in buddy free list
#define PAGE_RESERVED	(1 << 1)  // Must never be allocated
#define PAGE_UNUSABLE	(1 << 2)  // Bad RAM / holes
#define PAGE_ACPI		(1 << 3)  // ACPI reclaimable or NVS. These can only be touched by the ACPI subsystem.
#define PAGE_KERNEL		(1 << 4)  // Kernel image / mem_map / stacks
#define PAGE_DMA		(1 << 5)  // Belongs to DMA zone (future use)
#define PAGE_MMIO		(1 << 6)  // Belongs to a MMIO device

typedef struct {
	// These are commented so I keep track of array size without doing math every time I look at it.
	// They're self explanitory but still explained bc why not.
	uint32_t next;    // 4: Index of next free block in this order list
	uint32_t prev;    // 4: Index of previous free block
	uint8_t  order;   // 1: Order of the block
	bool     is_free; // 1: Is block free
	uint16_t flags;   // 2: Flags to keep track of information about the memory block
} Page;

// We determine the location of this in the init function
Page* mem_map;
uintptr_t mem_map_phys;

// The amount of 4kb pages the system has, from 0x0 to physical memory max.
size_t total_system_pages;

// The array of list heads (indices)
// We use 0xFFFFFFFF as a "null" index
uint32_t free_lists[MAX_ORDER + 1];

// Convert a physical address to its index in the mem_map
uint32_t addr_to_idx(uintptr_t phys_addr) { return phys_addr / PAGE_SIZE; }

// Convert an index in the mem_map to a physical address
uintptr_t idx_to_addr(uint32_t index) { return (uintptr_t) index * PAGE_SIZE; }

// Find the index of the "buddy" block
uint32_t get_buddy_idx(uint32_t index, uint8_t order) { return index ^ (1 << order); }

void push_to_list(uint8_t order, uint32_t index) {
	Page* page = &mem_map[index];

	// The new page points to the old head
	page->next = free_lists[order];
	page->prev = 0xFFFFFFFF; // NULL index

	// If there was an old head, it now points back to the new page
	if (free_lists[order] != 0xFFFFFFFF) {
		mem_map[free_lists[order]].prev = index;
	}

	// Move the head of the list to our new page
	free_lists[order] = index;
}

void remove_from_list(uint8_t order, uint32_t index) {
	Page* page = &mem_map[index];

	// If there is a next element, update its 'prev' to our 'prev'
	if (page->next != 0xFFFFFFFF) {
		mem_map[page->next].prev = page->prev;
	}

	// If there is a prev element, update its 'next' to our 'next'
	if (page->prev != 0xFFFFFFFF) {
		mem_map[page->prev].next = page->next;
	} else {
		// We were the head, update the global list head
		free_lists[order] = page->next;
	}

	// Always clear metadata on the node being removed
	page->next = 0xFFFFFFFF;
	page->prev = 0xFFFFFFFF;
}

uint32_t buddy_alloc(uint8_t order) {
	int found_order = order;
	while (found_order <= MAX_ORDER && free_lists[found_order] == 0xFFFFFFFF) {
		found_order++;
	}

	if (found_order > MAX_ORDER) return 0xFFFFFFFF;

	// REMOVE the large block from its list FIRST
	uint32_t block_idx = free_lists[found_order];
	remove_from_list(found_order, block_idx);

	// SPLIT logic
	while (found_order > order) {
		found_order--;

		// The right half becomes the new buddy
		uint32_t buddy_idx = block_idx + (1 << found_order);

		// SET METADATA for the buddy
		mem_map[buddy_idx].order = found_order;
		mem_map[buddy_idx].is_free = true;

		// The LEFT block (block_idx) is effectively shortened.
		// We don't add it to a list yet because we are still splitting it.
		mem_map[block_idx].order = found_order;

		// Push only the RIGHT half (the buddy) to the free list
		push_to_list(found_order, buddy_idx);
	}

	// Clean up before returning
	mem_map[block_idx].order = order;  // ensure the order is set regardless of the loop above
	mem_map[block_idx].is_free = false;
	return block_idx;
}

void buddy_free(uint32_t index, uint8_t order) {
	// Bounds check
	if (index >= total_system_pages) {
		panic_s("buddy_free: index out of bounds");
	}

	Page* page = &mem_map[index];

	// DOUBLE-FREE / CORRUPTION CHECKS
	if (page->is_free) {
		panic_s("buddy_free: double free detected");
	}

	if (page->order != order) {
		panic_s("buddy_free: order mismatch (corrupt free)");
	}

	uint32_t curr_idx = index;
	uint8_t curr_order = order;

	// We attempt to merge until we hit the maximum order or find an unmergeable buddy
	while (curr_order < MAX_ORDER) {
		// Calculate buddy index using XOR: addr ^ (1 << order)
		uint32_t buddy_idx = curr_idx ^ (1 << curr_order);

		// Bounds check. Does the buddy exist in physical RAM?
		// This really only applies to the very last 8MB of memory, we still need to check it.
		if (buddy_idx >= total_system_pages) {
			break;
		}

		Page* buddy_page = &mem_map[buddy_idx];

		// Is the buddy mergeable?
		// It must be FREE and it must be the EXACT SAME order.
		// If it's a different order, it's either part of a larger block 
		// or split into smaller blocks.
		if (!buddy_page->is_free || buddy_page->order != curr_order) {
			break;
		}

		// --- AT THIS POINT, WE MERGE ---

		// Remove the buddy from its current free list
		remove_from_list(curr_order, buddy_idx);

		// Mark the buddy as no longer being a "head" of a free block
		// This prevents other logic from thinking this index is a valid start.
		buddy_page->is_free = false;

		// Update curr_idx to the start of the new combined block.
		// Even if we are the "right" buddy, this mask forces us to the "left" address.
		curr_idx &= ~(1 << curr_order);

		// Move up to the next order and try to merge again
		curr_order++;
	}

	// Finalize the largest merged block
	Page* final_page = &mem_map[curr_idx];
	final_page->is_free = true;
	final_page->order = curr_order;

	// Put the merged block back
	push_to_list(curr_order, curr_idx);
}

uint8_t calculate_max_fit_order(uint32_t base_idx, uint32_t available_pages) {
	// Find the alignment of the base index. 
	// If base_idx is 0x4, it's aligned to Order 2 (2^2 = 4).
	// We use __builtin_ctz (Count Trailing Zeros) to find the first '1' bit.
	uint8_t alignment_order = (base_idx == 0) ? MAX_ORDER : (uint8_t) __builtin_ctz(base_idx);

	// Find the largest power of two that fits in the available space.
	// If we have 7 pages left, the largest power of two is 4 (Order 2).
	uint8_t space_order = 31 - __builtin_clz(available_pages);

	// The order we use is the minimum of these three:
	uint8_t order = alignment_order;
	if (space_order < order) order = space_order;
	if (order > MAX_ORDER)   order = MAX_ORDER;

	return order;
}

void init_region(uintptr_t start, uintptr_t end) {
	uint32_t start_idx = addr_to_idx(ALIGN_UP(start, PAGE_SIZE));
	uint32_t end_idx = addr_to_idx(ALIGN_DOWN(end, PAGE_SIZE));

	for (uint32_t i = start_idx; i < end_idx; ) {
		// Find the largest power-of-two block we can fit here
		// that is also aligned to its own size.
		uint8_t order = calculate_max_fit_order(i, end_idx - i);

		mem_map[i].is_free = true;
		mem_map[i].order = order;
		push_to_list(order, i);

		i += (1 << order); // Move to the next block
	}
}

/**
 * mark_region_flags - Set flags for a range of physical memory
 * @start: Starting physical address
 * @end: Ending physical address (exclusive)
 * @flags: Flags to set (PAGE_USABLE, PAGE_RESERVED, etc.)
 *
 * This function marks all pages in the given range with the specified flags.
 * It properly handles alignment and ensures we don't go out of bounds.
 */
void mark_region_flags(uintptr_t start, uintptr_t end, uint16_t flags) {
	// Align start up to page boundary, end down to page boundary
	uint32_t start_idx = addr_to_idx(ALIGN_UP(start, PAGE_SIZE));
	uint32_t end_idx = addr_to_idx(ALIGN_DOWN(end, PAGE_SIZE));

	// Bounds check
	if (start_idx >= total_system_pages) {
		return;
	}

	if (end_idx > total_system_pages) {
		end_idx = total_system_pages;
	}

	// Mark all pages in range
	for (uint32_t i = start_idx; i < end_idx; i++) {
		mem_map[i].flags = flags;
	}
}


uintptr_t scan_memory_map(struct multiboot_tag_mmap* mmap_tag) {
	// This should return the maximum address 	
	uintptr_t max_addr;

	struct multiboot_mmap_entry* mmap;
	for (mmap = mmap_tag->entries; (size_t) mmap < (size_t) mmap_tag + mmap_tag->size; mmap = (struct multiboot_mmap_entry*) ((size_t) mmap + (size_t) mmap_tag->entry_size)) {
		uintptr_t current_max = mmap->addr + mmap->len;
		if (current_max > max_addr) max_addr = current_max;
	}

	total_system_pages = addr_to_idx(ALIGN_DOWN(max_addr, PAGE_SIZE));
	return max_addr;
}

Page* find_free_region_internal(size_t size) {
	// Find (and map with the VMM) a region of memory for the PMM map.


	return nullptr;
}

extern "C" {
	extern uint64_t kernel_start;
	extern uint64_t kernel_end;
}

// uint64_t phys_kernel_end = (uint64_t) (&kernel_end) - KERNEL_VIRTUAL_BASE;

void pmm_init() {
	struct multiboot_tag_mmap* mmap_tag = MultibootManager::getMMap();

	// Calculate system size
	uintptr_t max_addr = scan_memory_map(mmap_tag);

	printf_serial("Max address for buddy alloc: 0x%llx\r\n", max_addr);

	if (max_addr == NULL) panic_s("Failed to parse multiboot memory map.");

	// Calculate mem_map size
	size_t mem_map_size = total_system_pages * sizeof(Page);

	return;

	// Find suitable location for mem_map
	mem_map = find_free_region_internal(mem_map_size);

	if (!mem_map) panic_s("Couldn't allocate enough contiguous memory for the memory map.");

	// Initialize all free lists to empty
	for (int i = 0; i <= MAX_ORDER; i++) {
		free_lists[i] = 0xFFFFFFFF;
	}

	// Mark ALL pages as unusable initially
	for (uint32_t i = 0; i < total_system_pages; i++) {
		mem_map[i].is_free = false;
		mem_map[i].order = 0;
		mem_map[i].next = 0xFFFFFFFF;
		mem_map[i].prev = 0xFFFFFFFF;
		mem_map[i].flags = PAGE_UNUSABLE;
	}

	// Process each usable region from bootloader memory map
	// I pseudocoded this because I don't want to deal with multiboot rn
	// for (each usable region in bootloader_memmap) {
	// 	// Mark pages with appropriate flags
	// 	mark_region_flags(region.start, region.end, PAGE_USABLE);

	// 	// Add to buddy system
	// 	init_region(region.start, region.end);
	// }

	struct multiboot_mmap_entry* mmap;
	for (mmap = mmap_tag->entries; (size_t) mmap < (size_t) mmap_tag + mmap_tag->size; mmap = (struct multiboot_mmap_entry*) ((size_t) mmap + (size_t) mmap_tag->entry_size)) {
		// we have mmap len, addr, type, and zero.

		uint16_t region_flags = 0;

		switch (mmap->type) {
			case MULTIBOOT_MEMORY_AVAILABLE:
				region_flags |= PAGE_USABLE;
				break;

			case MULTIBOOT_MEMORY_RESERVED:
				region_flags |= PAGE_RESERVED;
				break;

			case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE: __attribute__((fallthrough));
			case MULTIBOOT_MEMORY_NVS:
				region_flags |= PAGE_ACPI;
				break;

			case MULTIBOOT_MEMORY_BADRAM:
				region_flags |= PAGE_UNUSABLE;
				break;

			default:
				// This shouldn't be possible
				// We'll just mark it as unusable
				region_flags |= PAGE_UNUSABLE;
				break;
		}



	}

	// Mark special regions (kernel, mem_map itself, etc.)
	mark_region_flags(kernel_start, kernel_end, PAGE_KERNEL);
	mark_region_flags(mem_map_phys, mem_map_phys + mem_map_size, PAGE_KERNEL);
}

/* Principles of init.
 * Instead of mapping while we go, we're going to determine how much we actually have at first.
 * We calculate how much memory we have, determine how long the linked list will need to be,
 * find a good chunk of memory that will fit our list, request a CONTINUOUS mapping from the VMM.
 * Once we have the location, we can go back through and actually fill in the list.
 * The function to actually init the list will be a copy of buddy_free(), where everything is input at order 0 and the merging is dealt with automatically.
 * The init list function will set relevant flags for memory regions.
 *
 * We also calculate the total_system_pages, which covers everything from address 0 to the end of physical memory.
 * When initializing memory, we'll map all possible pages, and just set the reserved or unusable chunks as not free (and they'll never get added to the freelist)
 */