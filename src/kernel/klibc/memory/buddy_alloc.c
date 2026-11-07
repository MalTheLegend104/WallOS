#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/serial.h>
#include <memory/physical_mem.h>
#include <memory/virtual_mem.h>
#include <panic.h>
#include <stdio.h>

extern uint64_t kernel_start;
extern uint64_t kernel_end;

uint64_t buddy_phys_kernel_end;

// TODO: I really need a better locking system
// This is only like this because we need to wait with CLI enabled and spinlock_t depends on the allocator in the first place
static volatile uint32_t pmm_lock_word = 0;

static inline uint64_t pmm_lock(void) {
	uint64_t flags;
	__asm__ volatile("pushfq; cli; popq %0" : "=r"(flags) : : "memory");
	while (__atomic_exchange_n(&pmm_lock_word, 1, __ATOMIC_ACQUIRE)) {
		__asm__ volatile("pause");
	}
	return flags;
}

static inline void pmm_unlock(uint64_t flags) {
	__atomic_store_n(&pmm_lock_word, 0, __ATOMIC_RELEASE);
	if (flags & (1 << 9)) {
		__asm__ volatile("sti" : : : "memory");
	}
}

typedef struct {
	// These are commented so I keep track of array size without doing mental math every time I look at it.
	// They're self explanitory but still explained bc why not.
	uint32_t next;     // 4: Index of next free block in this order list
	uint32_t prev;     // 4: Index of previous free block
	uint8_t order;     // 1: Order of the block
	bool is_free;      // 1: Is block free
	uint16_t flags;    // 2: Flags to keep track of information about the memory block
	uint32_t refcount; // 4: Users of this block (VMM sharing / COW). Only meaningful on the head page of an allocated block.
} Page; // 16 bytes total
_Static_assert(sizeof(Page) == 16, "Page must stay 16 bytes, mem_map size is computed from it");

// We determine the location of this in the init function
Page* mem_map;
uintptr_t mem_map_phys;
size_t mem_map_size;

// The amount of 4kb pages the system has, from 0x0 to physical memory max.
size_t total_system_pages;

mmap_info mem_info;

// The array of list heads (indices)
// We use 0xFFFFFFFF as a "null" index
uint32_t free_lists[MAX_ORDER + 1];

// Convert a physical address to its index in the mem_map
static uint32_t addr_to_idx(uintptr_t phys_addr) { return phys_addr / PAGE_SIZE; }

// Convert an index in the mem_map to a physical address
static uintptr_t idx_to_addr(uint32_t index) { return (uintptr_t) index * PAGE_SIZE; }

// Find the index of the "buddy" block
static inline uint32_t get_buddy_idx(uint32_t index, uint8_t order) { return index ^ (1 << order); }

static void push_to_list(uint8_t order, uint32_t index) {
	Page* page = &mem_map[index];
	page->next = free_lists[order];
	page->prev = 0xFFFFFFFF;

	if (free_lists[order] != 0xFFFFFFFF) {
		mem_map[free_lists[order]].prev = index;
	}
	free_lists[order] = index;

	// TELEMETRY: Add pages (2^order) to the free count
	mem_info.free_pages += (1ULL << order);
}

static void remove_from_list(uint8_t order, uint32_t index) {
	Page* page = &mem_map[index];

	if (page->next != 0xFFFFFFFF) {
		mem_map[page->next].prev = page->prev;
	}
	if (page->prev != 0xFFFFFFFF) {
		mem_map[page->prev].next = page->next;
	} else {
		free_lists[order] = page->next;
	}

	page->next = 0xFFFFFFFF;
	page->prev = 0xFFFFFFFF;

	// TELEMETRY: Subtract pages (2^order) from the free count
	// Only subtract if we aren't underflowing (safety check)
	size_t pages = (1ULL << order);
	if (mem_info.free_pages >= pages) {
		mem_info.free_pages -= pages;
	}
}

static uint32_t buddy_alloc(uint8_t order) {
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

static void buddy_free(uint32_t index, uint8_t order) {
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

static uint8_t calculate_max_fit_order(uint32_t base_idx, uint32_t available_pages) {
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
	if (order > MAX_ORDER) order = MAX_ORDER;

	return order;
}

static void init_region(uintptr_t start, uintptr_t end) {
	uint32_t start_idx = addr_to_idx(ALIGN_UP(start, PAGE_SIZE));
	uint32_t end_idx = addr_to_idx(ALIGN_DOWN(end, PAGE_SIZE));

	for (uint32_t i = start_idx; i < end_idx;) {
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
 * @flags: Flags to set (PMM_PAGE_USABLE, PMM_PAGE_RESERVED, etc.)
 *
 * This function marks all pages in the given range with the specified flags.
 * It properly handles alignment and ensures we don't go out of bounds.
 */
static void mark_region_flags(uintptr_t start, uintptr_t end, uint16_t flags) {
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

	// printf_serial("Marking region: addr 0x%llx, idx: 0x%llx, flags: %d", idx_to_addr(i), i, flags);
	// Mark all pages in range
	for (uint32_t i = start_idx; i < end_idx; i++) {
		mem_map[i].flags = flags;
	}
}

/**
 * Find the free buddy block (if any) that contains page idx.
 *
 * Only the head page of a free block has is_free set, and a block of order N is always aligned to 2^N pages, so the candidate heads are idx rounded down at each order.
 */
static uint32_t find_free_block(uint32_t idx, uint8_t* order_out) {
	for (uint8_t order = 0; order <= MAX_ORDER; order++) {
		uint32_t head = idx & ~((1u << order) - 1);
		Page* p = &mem_map[head];

		if (p->is_free && p->order == order) {
			*order_out = order;
			return head;
		}
	}

	return 0xFFFFFFFF;
}

/**
 * Return [start, end) to the free lists as maximal aligned blocks.
 */
static void free_fragment(uint32_t start, uint32_t end) {
	for (uint32_t i = start; i < end;) {
		uint8_t order = calculate_max_fit_order(i, end - i);

		mem_map[i].is_free = true;
		mem_map[i].order = order;
		push_to_list(order, i);
		i += 1u << order;
	}
}

/**
 * Take [start_idx, end_idx) out of the free lists.
 *
 * Free blocks that only partially overlap the range are split.
 * The parts outside the range go back on the free lists.
 * Pages in the range that are already allocated are skipped.
 * The caller must hold the PMM lock
 */
static void reserve_pages_locked(uint32_t start_idx, uint32_t end_idx) {
	if (end_idx > total_system_pages) end_idx = total_system_pages;

	uint32_t i = start_idx;

	while (i < end_idx) {
		uint8_t order;
		uint32_t head = find_free_block(i, &order);

		if (head == 0xFFFFFFFF) {
			i++;
			continue;
		}

		uint32_t block_end = head + (1u << order);

		remove_from_list(order, head);
		mem_map[head].is_free = false;

		if (head < start_idx) free_fragment(head, start_idx);
		if (block_end > end_idx) free_fragment(end_idx, block_end);

		i = block_end;
	}
}

static void mark_and_allocate_region(uintptr_t start, uintptr_t end, uint16_t flags) {
	uint32_t start_idx = addr_to_idx(ALIGN_UP(start, PAGE_SIZE));
	uint32_t end_idx = addr_to_idx(ALIGN_DOWN(end, PAGE_SIZE));

	// Bounds check
	if (start_idx >= total_system_pages) {
		return;
	}

	if (end_idx > total_system_pages) {
		end_idx = total_system_pages;
	}

	printf_serial(
		"[PMM] Reserving region: 0x%llx - 0x%llx (flags: 0x%x)\r\n",
		idx_to_addr(start_idx),
		idx_to_addr(end_idx),
		flags
	);

	reserve_pages_locked(start_idx, end_idx);

	for (uint32_t i = start_idx; i < end_idx; i++) {
		mem_map[i].flags = flags;
	}
}

static uintptr_t scan_memory_map(struct multiboot_tag_mmap* mmap_tag) {
	// This should return the maximum address
	uintptr_t max_addr = 0;
	uintptr_t max_usable_addr = 0;

	struct multiboot_mmap_entry* mmap;
	for (mmap = mmap_tag->entries; (size_t) mmap < (size_t) mmap_tag + mmap_tag->size; mmap = (struct multiboot_mmap_entry*) ((size_t) mmap + (size_t) mmap_tag->entry_size)) {

		uintptr_t current_max = mmap->addr + mmap->len;
		if (current_max > max_addr) {
			max_addr = current_max;
			if (mmap->type != MULTIBOOT_MEMORY_RESERVED && mmap->type != MULTIBOOT_MEMORY_BADRAM) {
				max_usable_addr = current_max;
			}
		}
		printf("[PMM] Base 0x%llx, Type %d, Length: 0x%llx\n", mmap->addr, mmap->type, mmap->len);
	}


	// SANITY CHECK (thanks QEMU for saying I have 1 TiB of memory):
	// Only trigger if we are significantly high (64 GiB) AND the gap is suspiciously large (3x higher than the last usable address).
	const uintptr_t upper_limit = 0x1000000000ULL;
	if (max_addr > max_usable_addr * 3) {
		printf("[PMM][WARNING] Max address is more than 3 times higher than max usable address.\n");
		printf_serial("[PMM][WARNING] Max address is more than 3 times higher than max usable address.\r\n");

		if (max_addr > upper_limit) {
			printf("[PMM] Max address is above 64GiB.\n\t- Assuming max usable is highest RAM location...\n\t- This is unusual on anything other than QEMU...\n");
			printf_serial("[PMM] Max address is above 64GiB.\r\n\t- Assuming max usable is highest RAM location...\r\n");
			max_addr = max_usable_addr;
		}
	}

	total_system_pages = addr_to_idx(ALIGN_DOWN(max_addr, PAGE_SIZE));
	return max_addr;
}

// Need a way to map our own things as reserved so we don't accidentally overwrite them.
#define MAX_RESERVED 50
typedef struct {
	uintptr_t addr;
	size_t size;
} reserved_region;
reserved_region reservedMemory[MAX_RESERVED];
size_t reservedChunks = 0;

void pmm_reserve_memory(uintptr_t base_addr, size_t size) {
	if (reservedChunks >= MAX_RESERVED) {
		panic_s("PMM: too many reserved regions, raise MAX_RESERVED.");
	}

	// Find insertion index
	size_t insert = 0;
	while (insert < reservedChunks && reservedMemory[insert].addr < base_addr) {
		insert++;
	}

	// Shift entries to the right
	for (size_t i = reservedChunks; i > insert; i--) {
		reservedMemory[i] = reservedMemory[i - 1];
	}

	// Insert new region
	reservedMemory[insert].addr = base_addr;
	reservedMemory[insert].size = size;

	reservedChunks++;
}

static Page* find_free_region_internal(struct multiboot_tag_mmap* mmap_tag, size_t size) {
	uintptr_t region = 0;

	struct multiboot_mmap_entry* mmap;
	for (mmap = mmap_tag->entries;
		 (size_t) mmap < (size_t) mmap_tag + mmap_tag->size;
		 mmap = (struct multiboot_mmap_entry*) ((size_t) mmap + mmap_tag->entry_size)) {

		if (mmap->type != MULTIBOOT_MEMORY_AVAILABLE)
			continue;

		uintptr_t region_start = mmap->addr;
		uintptr_t region_end = mmap->addr + mmap->len;

		// Enforce kernel boundary
		if (region_end <= buddy_phys_kernel_end)
			continue;

		if (region_start < buddy_phys_kernel_end)
			region_start = buddy_phys_kernel_end;

		if (region_start >= region_end)
			continue;

		// Walk reserved regions and test gaps
		uintptr_t cursor = region_start;

		for (size_t i = 0; i < reservedChunks; i++) {
			uintptr_t rs = reservedMemory[i].addr;
			uintptr_t re = rs + reservedMemory[i].size;

			// Reserved region completely before cursor
			if (re <= cursor)
				continue;

			// Reserved region starts after usable area
			if (rs >= region_end)
				break;

			// Gap before this reserved region
			if (rs > cursor) {
				uintptr_t gap_start = cursor;
				uintptr_t gap_end = rs;

				// Align gap start to 2MB
				uintptr_t aligned = (gap_start + PAGE_2MB_SIZE - 1) & ~(PAGE_2MB_SIZE - 1);

				if (aligned + size <= gap_end) {
					region = aligned;
					goto found;
				}
			}

			// Advance cursor past reserved region
			if (re > cursor)
				cursor = re;
		}

		// Tail gap after last reserved region
		if (cursor < region_end) {
			uintptr_t aligned = (cursor + PAGE_2MB_SIZE - 1) & ~(PAGE_2MB_SIZE - 1);

			if (aligned + size <= region_end) {
				region = aligned;
				goto found;
			}
		}
	}

found:
	if (!region) {
		printf("[PMM][FATAL] couldn't find a region to map to...\n");
		return NULL;
	}

	mem_map_phys = region;

	// The VMM direct-maps all usable RAM before we get here, so mem_map is reachable without mapping anything extra.
	size_t pages = (size + PAGE_2MB_SIZE - 1) / PAGE_2MB_SIZE;
	uintptr_t virt = vmm_phys_to_virt_addr(region);

	printf("[PMM] Found mem_map region:\n");
	printf("  Phys: 0x%llx\n", region);
	printf("  Virt: 0x%llx\n", virt);
	printf("  Size: 0x%llx\n", size);
	printf("  Pages: %zu\n", pages);

	return (Page*) virt;
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
void pmm_init(struct multiboot_tag_mmap* mmap_tag) {
	pmm_reserve_memory((uintptr_t) mmap_tag, mmap_tag->size);

	buddy_phys_kernel_end = (uint64_t) (&kernel_end) - KERNEL_VIRTUAL_BASE;

	// Calculate system size
	uintptr_t max_addr = scan_memory_map(mmap_tag);

	printf_serial("[PMM] Max address for buddy alloc: 0x%llx\r\n", max_addr);
	printf("[PMM] Max address for buddy alloc: 0x%llx\n", max_addr);

	printf_serial("[PMM] Page count for buddy alloc: 0x%llx\r\n", max_addr);
	printf("[PMM] Page count for buddy alloc: 0x%llx\n", max_addr);

	if (max_addr == 0) panic_s("Failed to parse multiboot memory map.");

	// Tell the VMM which physical ranges it may direct-map (real RAM and ACPI tables only).
	// We keep the MMIO and holes unmapped because we REALLY don't want to touch them on accident
	for (struct multiboot_mmap_entry* dm = mmap_tag->entries; (size_t) dm < (size_t) mmap_tag + mmap_tag->size; dm = (struct multiboot_mmap_entry*) ((size_t) dm + (size_t) mmap_tag->entry_size)) {
		if (dm->len == 0 || dm->addr >= max_addr) {
			continue;
		}
		if (dm->type != MULTIBOOT_MEMORY_AVAILABLE && dm->type != MULTIBOOT_MEMORY_ACPI_RECLAIMABLE && dm->type != MULTIBOOT_MEMORY_NVS) {
			continue;
		}
		uintptr_t dm_end = dm->addr + dm->len;
		if (dm_end > max_addr) {
			dm_end = max_addr;
		}
		vmm_map_direct_region(dm->addr, dm_end);
	}

	// Calculate mem_map size
	mem_map_size = total_system_pages * sizeof(Page);

	printf_serial("[PMM] Size needed for buddy alloc mem_map: 0x%llx\r\n", mem_map_size);

	// Find suitable location for mem_map
	mem_map = find_free_region_internal(mmap_tag, mem_map_size);

	printf("[PMM] MEM_MAP ADDR: 0x%llx\n", mem_map);

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
		mem_map[i].flags = PMM_PAGE_UNUSABLE;
		mem_map[i].refcount = 0;
	}

	mem_info.total = 0;
	mem_info.usable = 0;
	mem_info.reserved = 0;

	int entry_count = 0;
	struct multiboot_mmap_entry* mmap;
	for (mmap = mmap_tag->entries; (size_t) mmap < (size_t) mmap_tag + mmap_tag->size; mmap = (struct multiboot_mmap_entry*) ((size_t) mmap + (size_t) mmap_tag->entry_size)) {

		uint16_t region_flags = 0;
		const char* type_str = "UNKNOWN";

		uintptr_t start = mmap->addr;
		uintptr_t end = mmap->addr + mmap->len;

		if (start > max_addr || mmap->len == 0) {
			printf_serial("[PMM][WARN] Corrupt MMap entry detected (hopefully because of QEMU)! Base: 0x%llx Entry Count: %d\r\n", start, entry_count);
			continue;
		}

		mem_info.total += (end - start);

		switch (mmap->type) {
			case MULTIBOOT_MEMORY_AVAILABLE:
				region_flags |= PMM_PAGE_USABLE;
				type_str = "AVAILABLE";
				break;
			case MULTIBOOT_MEMORY_RESERVED:
				region_flags |= PMM_PAGE_RESERVED;
				type_str = "RESERVED";
				break;
			case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE:
				region_flags |= PMM_PAGE_ACPI;
				type_str = "ACPI_RECLAIMABLE";
				break;
			case MULTIBOOT_MEMORY_NVS:
				region_flags |= PMM_PAGE_ACPI;
				type_str = "ACPI_NVS";
				break;
			case MULTIBOOT_MEMORY_BADRAM:
				region_flags |= PMM_PAGE_UNUSABLE;
				type_str = "BAD_RAM";
				break;
			default:
				region_flags |= PMM_PAGE_UNUSABLE;
				type_str = "UNDEFINED";
				break;
		}

		if (region_flags & PMM_PAGE_USABLE)
			mem_info.usable += (end - start);
		else
			mem_info.reserved += (end - start);

		// Verbose logging for each map entry
		printf_serial(
			"[PMM] MMap Entry #%d: Base: 0x%llx, End: 0x%llx, Type: %s (%d)\r\n",
			entry_count++,
			start,
			end,
			type_str,
			mmap->type
		);

		// CRITICAL CHECK: Ensure we don't try to initialize memory outside our mem_map range
		if (addr_to_idx(start) >= total_system_pages) {
			printf_serial("  [!] Skipping: Region starts beyond calculated total_system_pages\r\n");
			continue;
		}

		// Only add to the free list if it's actually usable RAM
		if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
			printf_serial("  [+] Initializing usable region into buddy lists...\r\n");
			init_region(start, end);
		} else {
			printf_serial("  [-] Skipping buddy init for non-available region.\r\n");
		}

		// Always mark flags so the PMM knows what's at these addresses
		mark_region_flags(start, end, region_flags);
	}
	// __asm__ volatile("cli");
	// __asm__ volatile("hlt");

	// Mark special regions (kernel, mem_map itself, etc.)
	mark_and_allocate_region((uintptr_t) (&kernel_start), buddy_phys_kernel_end, PMM_PAGE_KERNEL);
	mark_and_allocate_region(mem_map_phys, mem_map_phys + mem_map_size, PMM_PAGE_KERNEL);

	// Physical page 0 is never handed out. The VMM uses 0 as "allocation failed".
	mark_and_allocate_region(0, PAGE_SIZE, PMM_PAGE_RESERVED);

	for (size_t i = 0; i < reservedChunks; i++) {
		mark_and_allocate_region(reservedMemory[i].addr, reservedMemory[i].addr + reservedMemory[i].size, PMM_PAGE_KERNEL);
		printf_serial("[PMM] Marking reserved region as kernel memory.\r\n\tADDR: 0x%llx\r\n\tSIZE: 0x%llx\r\n", reservedMemory[i].addr, reservedMemory[i].addr + reservedMemory[i].size);
	}

	// Everything the VMM needs from us exists now
	vmm_set_pmm_ready();

	return;
}

const mmap_info* pmm_get_mmap_info() { return &mem_info; }

uintptr_t pmm_phys_kernel_end(void) {
	return buddy_phys_kernel_end;
}

size_t pmm_total_free_bytes(void) {
	// mem_info.free_pages tracks 4KB pages in the buddy allocator
	return mem_info.free_pages * PAGE_SIZE;
}

size_t pmm_total_used_bytes(void) {
	// Total usable RAM minus what is currently free
	size_t free_bytes = pmm_total_free_bytes();
	if (free_bytes > mem_info.usable) {
		return 0;
	}
	return mem_info.usable - free_bytes;
}

size_t pmm_free_page_count(void) {
	return mem_info.free_pages;
}

size_t pmm_used_page_count(void) {
	// Total usable RAM pages minus what is currently in the free lists
	size_t usable_pages = mem_info.usable / PAGE_SIZE;
	if (mem_info.free_pages > usable_pages) return 0;
	return usable_pages - mem_info.free_pages;
}

uintptr_t pmm_alloc_2mb_sequential(size_t page_count) {
	// printf_serial("\r\n[PMM] PhysicalAlloc2MBSequential ENTER\r\n");
	// printf_serial("[PMM] requested page_count=%llu\r\n", page_count);

	if (page_count == 0) {
		return 0;
	}

	// Calculate the order needed for page_count * 2MB
	// Each order-9 block is 512 pages (2MB)
	// We need an order that fits page_count * 512 pages

	size_t total_pages = page_count << 9; // page_count * 512

	// Find the smallest order that fits
	uint8_t needed_order = 0;
	while ((1U << needed_order) < total_pages) {
		needed_order++;
	}

	if (needed_order > MAX_ORDER) {
		printf_serial(
			"[PMM] ERROR: requested size too large (order %u > MAX %u)\r\n",
			needed_order,
			MAX_ORDER
		);
		return 0;
	}

	// printf_serial("[PMM] Allocating order-%u block (contains %llu x 2MB)\r\n", needed_order, page_count);

	uintptr_t result = pmm_alloc_contiguous(total_pages);

	if (!result) {
		printf_serial("[PMM] ERROR: contiguous alloc failed for %llu pages\r\n", (unsigned long long) total_pages);
		return 0;
	}

	// printf_serial("[PMM] SUCCESS addr=0x%llx\r\n", result);

	return result;
}

static uint32_t phys_alloc_32bit(uint8_t order) {
	for (uint8_t found_order = order; found_order <= MAX_ORDER; found_order++) {
		uint32_t current_idx = free_lists[found_order];

		while (current_idx != 0xFFFFFFFF) {
			uintptr_t phys_addr = idx_to_addr(current_idx);
			uintptr_t block_size = (1ULL << found_order) * PAGE_SIZE;

			// Check if the block is entirely below 4GB (0xFFFFFFFF)
			if (phys_addr + block_size <= 0x100000000ULL) {
				// Found one
				// Remove it from the middle of the list
				remove_from_list(found_order, current_idx);

				// Reuse existing split logic
				uint32_t block_idx = current_idx;
				uint8_t temp_order = found_order;
				while (temp_order > order) {
					temp_order--;
					uint32_t buddy_idx = block_idx + (1 << temp_order);
					mem_map[buddy_idx].order = temp_order;
					mem_map[buddy_idx].is_free = true;
					mem_map[block_idx].order = temp_order;
					push_to_list(temp_order, buddy_idx);
				}

				mem_map[block_idx].order = order;
				mem_map[block_idx].is_free = false;
				return block_idx;
			}
			// Move to the next block in the same order list
			current_idx = mem_map[current_idx].next;
		}
	}

	return 0xFFFFFFFF; // No 32-bit blocks available
}

uintptr_t pmm_alloc_2mb(void) {
	return pmm_alloc_2mb_sequential(1);
}

uintptr_t pmm_alloc_2mb_32bit(void) {
	// This used to return the mem_map index instead of the address.
	return pmm_alloc_pages_32bit(9);
}

uintptr_t pmm_mark_allocated(uintptr_t addr, size_t len) {
	uint64_t lock_flags = pmm_lock();
	uint32_t start_idx = addr_to_idx(ALIGN_DOWN(addr, PAGE_SIZE));
	uint32_t end_idx = addr_to_idx(ALIGN_UP(addr + len, PAGE_SIZE));

	if (end_idx > total_system_pages) end_idx = total_system_pages;

	reserve_pages_locked(start_idx, end_idx);

	for (uint32_t i = start_idx; i < end_idx; i++) {
		mem_map[i].flags |= PMM_PAGE_RESERVED;
	}

	pmm_unlock(lock_flags);
	return idx_to_addr(start_idx);
}

void pmm_dealloc_2mb(uintptr_t phys_addr) {
	uint32_t idx = addr_to_idx(phys_addr);

	if (idx >= total_system_pages)
		panic_s("PhysicalDeAlloc2MB: invalid address");

	uint64_t lock_flags = pmm_lock();
	buddy_free(idx, 9);
	pmm_unlock(lock_flags);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// VMM Stuff
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Gives a block its starting reference. Caller holds the lock.
static uintptr_t finish_alloc(uint32_t idx) {
	if (idx == 0xFFFFFFFF) {
		return 0;
	}
	mem_map[idx].refcount = 1;
	return idx_to_addr(idx);
}

uintptr_t pmm_alloc_pages(uint8_t order) {
	if (order > MAX_ORDER) {
		return 0;
	}
	uint64_t lock_flags = pmm_lock();
	uintptr_t result = finish_alloc(buddy_alloc(order));
	pmm_unlock(lock_flags);
	return result;
}

uintptr_t pmm_alloc_pages_32bit(uint8_t order) {
	if (order > MAX_ORDER) {
		return 0;
	}
	uint64_t lock_flags = pmm_lock();
	uintptr_t result = finish_alloc(phys_alloc_32bit(order));
	pmm_unlock(lock_flags);
	return result;
}

uintptr_t pmm_alloc_4kb(void) {
	return pmm_alloc_pages(0);
}

uintptr_t pmm_alloc_contiguous(size_t pages) {
	if (pages == 0) {
		return 0;
	}

	uint8_t order = 0;
	while (order <= MAX_ORDER && ((size_t) 1 << order) < pages) {
		order++;
	}
	if (order > MAX_ORDER) {
		return 0;
	}

	uint64_t lock_flags = pmm_lock();
	uint32_t idx = buddy_alloc(order);
	if (idx == 0xFFFFFFFF) {
		pmm_unlock(lock_flags);
		return 0;
	}

	uint32_t block_pages = 1u << order;
	if (pages != block_pages) {
		/* The part we keep, [0, pages), becomes one block per set bit in `pages`, biggest first.
		 * Every chunk ends up naturally aligned, and its head carries the metadata buddy_free() checks.
		 */
		uint32_t off = 0;
		for (int bit = MAX_ORDER; bit >= 0; bit--) {
			if (pages & ((size_t) 1 << bit)) {
				mem_map[idx + off].order = bit;
				mem_map[idx + off].is_free = false;
				mem_map[idx + off].refcount = 1;
				off += 1u << bit;
			}
		}

		/* The slack at the tail goes back to the free lists.
		 * Take the biggest naturally aligned chunk that still fits, starting where the kept part ended.
		 * It can't merge into the kept part because those heads aren't free.
		 */
		while (off < block_pages) {
			uint8_t k = 0;
			while (k < MAX_ORDER && (off & ((1u << (k + 1)) - 1)) == 0 && off + (1u << (k + 1)) <= block_pages) {
				k++;
			}
			mem_map[idx + off].order = k;
			mem_map[idx + off].is_free = false;
			buddy_free(idx + off, k);
			off += 1u << k;
		}
	} else {
		mem_map[idx].refcount = 1;
	}

	pmm_unlock(lock_flags);
	return idx_to_addr(idx);
}

void pmm_free_pages(uintptr_t phys) {
	uint32_t idx = addr_to_idx(phys);
	if (idx >= total_system_pages) {
		panic_s("PhysicalFreePages: invalid address");
	}

	uint64_t lock_flags = pmm_lock();
	buddy_free(idx, mem_map[idx].order);
	pmm_unlock(lock_flags);
}

void pmm_free_contiguous(uintptr_t phys, size_t pages) {
	uint32_t idx = addr_to_idx(phys);
	if (idx >= total_system_pages) {
		panic_s("PhysicalFreeContiguous: invalid address");
	}

	// Same decomposition PhysicalAllocContiguous() used when it set up the chunk heads.
	uint64_t lock_flags = pmm_lock();
	uint32_t off = 0;
	for (int bit = MAX_ORDER; bit >= 0; bit--) {
		if (pages & ((size_t) 1 << bit)) {
			buddy_free(idx + off, bit);
			off += 1u << bit;
		}
	}
	pmm_unlock(lock_flags);
}

uint32_t pmm_ref_get(uintptr_t phys) {
	uint32_t idx = addr_to_idx(phys);
	if (idx >= total_system_pages) {
		return 0;
	}
	return __atomic_load_n(&mem_map[idx].refcount, __ATOMIC_ACQUIRE);
}

uint32_t pmm_ref_inc(uintptr_t phys) {
	uint32_t idx = addr_to_idx(phys);
	if (idx >= total_system_pages) {
		panic_s("PhysicalRefInc: invalid address");
	}
	return __atomic_add_fetch(&mem_map[idx].refcount, 1, __ATOMIC_ACQ_REL);
}

uint32_t pmm_ref_dec(uintptr_t phys) {
	uint32_t idx = addr_to_idx(phys);
	if (idx >= total_system_pages) {
		panic_s("PhysicalRefDec: invalid address");
	}

	uint32_t* rc = &mem_map[idx].refcount;
	uint32_t old = __atomic_load_n(rc, __ATOMIC_RELAXED);
	do {
		if (old == 0) {
			panic_s("PhysicalRefDec: refcount underflow");
		}
	} while (!__atomic_compare_exchange_n(rc, &old, old - 1, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
	return old - 1;
}

bool pmm_put(uintptr_t phys) {
	if (pmm_ref_dec(phys) == 0) {
		pmm_free_pages(phys);
		return true;
	}
	return false;
}
