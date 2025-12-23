#include <memory/physical_mem.hpp>
#include <memory/virtual_mem.h>
#include <stdlib.h>
#include <string.h>
#include <panic.h>
#include <stdio.h>
#include <klibc/kprint.h>
#include <klibc/logger.h>
#include <system/idt.h>
#include <assert.h>

#include <drivers/serial.h>

mmap_info mem_info;

typedef struct Block {
	uintptr_t pointer;
	bool free;
	Block* next_block;
} __attribute__((packed)) Block;

Block* block_list = NULL;
Block* last_block = NULL;
Block* last_block_start = NULL;

typedef struct {
	uintptr_t virt_start_addr;
	uintptr_t real_start_addr;
	size_t length;
} __attribute__((packed)) acpi_block;

acpi_block acpi_blocks[128];
size_t acpi_block_index = 0;

#define MAX_RESERVED 50
typedef struct {
	uintptr_t addr;
	size_t size;
} region;
region reservedMemory[MAX_RESERVED];
size_t reservedChunks = 0;

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

size_t Memory::Info::getUsedPageCount() {
	size_t used_phys_pages = 0;
	Block* current = block_list;
	while (current != NULL) {
		if (!(current->free)) used_phys_pages++;
		current = current->next_block;
	}
	return used_phys_pages;
}

void map_acpi_reclaimable(uintptr_t start_addr, size_t length) {
	if (acpi_block_index >= 128) return;
	acpi_blocks[acpi_block_index].real_start_addr = start_addr;
	acpi_blocks[acpi_block_index].length = length;

	uintptr_t new_start_address = start_addr & ~0x1FFFFF; // Round down to the nearest 2MB boundary

	acpi_blocks[acpi_block_index].virt_start_addr = new_start_address;

	acpi_block_index++;

	printf_serial("\tMapped ACPI block.\r\n");
	printf_serial("\t\tStart Addr: 0x%llx\r\n", start_addr);
	printf_serial("\t\tVirt Addr:  0x%llx\r\n", new_start_address);
	printf_serial("\t\tLength:     0x%llx\r\n", length);
}

/**
 * @brief This is some voodoo magic. It's also poorly commented. GLHF :)
 *
 * @param start_address Start address of the chunk of memory
 * @param length Length of the chunk of memory
 * @param type Type of memory chunk, as defined by the MultiBoot2 Memory Map Tag
 */
void map_chunk(uintptr_t start_address, size_t length, uint32_t type) {
	if (type != MULTIBOOT_MEMORY_AVAILABLE && type != MULTIBOOT_MEMORY_ACPI_RECLAIMABLE) {
		printf_serial("\tType (%d) isn't MULTIBOOT_MEMORY_AVAILABLE\r\n", type);
		return;
	}
	if (start_address < 0x100000) {
		printf_serial("\tStart address is below 1MB. This is identity mapped so we ignore it.\r\n");
		return;
	}
	if (start_address <= phys_kernel_end) {
		// We have to make sure that we can map the physical memory behind the kernel.
		if (start_address + length < phys_kernel_end) {
			printf_serial("\tAddr + length is below the kernel.\r\n");
			return;
		}
		length = length - (phys_kernel_end - start_address);
		start_address = phys_kernel_end;
	}

	if (type == MULTIBOOT_MEMORY_ACPI_RECLAIMABLE) {
		map_acpi_reclaimable(start_address, length);
		return;
	}

	// check if the memory region just so happens to contain reserved memory
	uintptr_t end_addr = start_address + length;
	for (size_t i = 0; i < reservedChunks; i++) {
		uintptr_t start_reserved = reservedMemory[i].addr;
		uintptr_t end_reserved = reservedMemory[i].addr + reservedMemory[i].size;

		if (start_reserved > start_address && start_reserved < end_addr) {
			// split the chunk into start_address -> start_reserved then end_reserved -> end_addr
			// First chunk
			size_t len = start_reserved - start_address;
			map_chunk(start_address, len, MULTIBOOT_MEMORY_AVAILABLE);
			if (end_reserved > end_addr) {
				Memory::reserveMemory(end_addr, end_reserved - end_addr);
				break;
			}
			// second chunk
			len = end_addr - end_reserved;
			map_chunk(end_reserved, len, MULTIBOOT_MEMORY_AVAILABLE);
		}
	}

	// We want the start address to be on a 2MB boundary.
	uintptr_t old_start_addr = start_address;
	uintptr_t new_start_address = (start_address + 0x1FFFFF) & ~0x1FFFFF; // Round up & clear the lower 21 bits 

	if ((new_start_address - old_start_addr) > length) {
		printf("\tMemory chunk too small to map... (%u bytes)\n", new_start_address - old_start_addr);
		printf_serial("\tMemory chunk too small to map... (%u bytes, need at least %u)\r\n", new_start_address - old_start_addr, PAGE_2MB_SIZE);
		return;
	}

	length = length - (new_start_address - old_start_addr); // Adjust length to start at the new boundary

	printf("\tMemory Chunk: 0x%llx -> 0x%llx bytes\n", new_start_address, length);
	printf_serial("\tMemory Chunk: 0x%llx -> 0x%llx bytes\r\n", new_start_address, length);
	size_t max_pages = length / PAGE_2MB_SIZE;

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
	first_block->pointer = new_start_address + (PAGE_2MB_SIZE * pages_taken);
	first_block->free = true;
	last_block = first_block + sizeof(Block);
	if (block_list == NULL)
		block_list = first_block;

	// This will map the entire next 2mb block of memory. This avoids a page fault.
	// The page fault handler will handle page faults correctly *after* we initialize the physical allocator.
	// Unfortunately until then we have to be a little bit messy. 
	if (((uintptr_t) last_block) + sizeof(Block) >= (Memory::GetMappingEnd() + KERNEL_VIRTUAL_BASE)) Memory::MapPreAllocMem(((uintptr_t) last_block) + sizeof(Block));

	Block* last = first_block;
	// We've already allocated block 0
	for (size_t i = 1; i <= max_pages - 1; i++) {
		Block* current_block = (Block*) (last_block);
		last->next_block = current_block;
		current_block->next_block = NULL;
		current_block->pointer = last->pointer + PAGE_2MB_SIZE;
		current_block->free = true;
		last_block_start = current_block;
		last_block = current_block + sizeof(Block);
		last = current_block;
		if (((uintptr_t) last_block) + sizeof(Block) >= (Memory::GetMappingEnd() + KERNEL_VIRTUAL_BASE)) { Memory::MapPreAllocMem(((uintptr_t) last_block) + sizeof(Block)); }
	}

	printf("\t\tTotal Blocks: %llu -> Last Addr: 0x%llx\n", max_pages, new_start_address + (max_pages * PAGE_2MB_SIZE));
	printf_serial("\t\tTotal Blocks: %llu -> Last Addr: 0x%llx\r\n", max_pages, new_start_address + (max_pages * PAGE_2MB_SIZE));
}

void fillMMapInfo(struct multiboot_tag_mmap* mmap_tag) {
	struct multiboot_mmap_entry* mmap;
	mem_info.total = 0;
	mem_info.usable = 0;
	mem_info.reserved = 0;
	for (mmap = mmap_tag->entries; (size_t) mmap < (size_t) mmap_tag + mmap_tag->size; mmap = (struct multiboot_mmap_entry*) ((size_t) mmap + (size_t) mmap_tag->entry_size)) {
		// For some reason, on large memory systems, if you add to the total before the if statement it breaks things.
		// My best guess is that GCC is doing some magic that makes no sense, especially since this gets compiled with -O0.
		if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
			mem_info.usable += mmap->len;
		} else {
			mem_info.reserved += mmap->len;
		}
		mem_info.total += mmap->len;
	}
}

const mmap_info* Memory::Info::getMMapInfo() {
	return &mem_info;
}

/**
 * @brief Reserves an area of memory for system processes. This is to prevent the mmap from (1) overwriting it, and (2) the mmap from pointing to it.
 *
 * @param base_addr Base address of the section to mark as reserved.
 * @param size Length of the region in bytes.
 */
void Memory::reserveMemory(uintptr_t base_addr, size_t size) {
	assert(reservedChunks <= MAX_RESERVED);
	reservedMemory[reservedChunks].addr = base_addr;
	reservedMemory[reservedChunks].size = size;
	reservedChunks++;
}

void Memory::PhysicalMemInit() {
	struct multiboot_tag_mmap* mmap_tag = MultibootManager::getMMap();
	struct multiboot_mmap_entry* mmap;
	fillMMapInfo(mmap_tag);
	phys_kernel_end = (uint64_t) (&kernel_end) - KERNEL_VIRTUAL_BASE;
	set_colors(VGA_COLOR_YELLOW, VGA_DEFAULT_BG);
	printf("Initalizing Physical Memory Allocator:\n");
	printf_serial("Initalizing Physical Memory Allocator:\r\n");
	set_to_last();
	set_colors(VGA_COLOR_BROWN, VGA_DEFAULT_BG);
	for (mmap = mmap_tag->entries; (size_t) mmap < (size_t) mmap_tag + mmap_tag->size; mmap = (struct multiboot_mmap_entry*) ((size_t) mmap + (size_t) mmap_tag->entry_size)) {
		printf("Found chunk: addr->0x%x len->0x%x\n", mmap->addr, mmap->len);
		printf_serial("Found chunk: addr->0x%x len->0x%x\r\n", mmap->addr, mmap->len);
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
	return PhysicalAlloc2MBSequential(1);
}

uintptr_t Memory::PhysicalAlloc2MBSequential(size_t page_count) {
	Block* base_block = NULL;
	Block* last_block = NULL;
	size_t current_streak = 0;

	// First attempt, we check if last_allocated_block.next_block is free
	if (last_allocated_block != NULL && last_allocated_block->next_block != NULL) {
		base_block = last_block = last_allocated_block->next_block;
		current_streak++;
		if (page_count == 1) {
			last_allocated_block = base_block;
			last_allocated_block->free = false;
			return (last_allocated_block->pointer);
		} else {
			for (size_t i = 0; i < page_count; i++) {
				if (last_block->next_block->free) {
					last_block = last_block->next_block;
					current_streak++;
					if (current_streak == page_count) {
						uintptr_t return_addr = base_block->pointer;
						last_allocated_block = last_block;
						for (size_t i = 0; i > page_count; i++) {
							base_block->free = false;
							base_block = base_block->next_block;
						}
						return (return_addr);
					}
				} else {
					break;
				}
			}
		}
	}

	// We have to go through the entire map otherwise
	Block* current = block_list;
	while (current != NULL) {
		if (current->free && current_streak != page_count) {
			base_block = last_block = current;
			current_streak++;
			if (page_count == 1) {
				last_allocated_block = base_block;
				last_allocated_block->free = false;
				return (last_allocated_block->pointer);
			} else {
				for (size_t i = 0; i < page_count; i++) {
					if (last_block->next_block->free) {
						last_block = last_block->next_block;
						current_streak++;
						if (current_streak == page_count) {
							uintptr_t return_addr = base_block->pointer;
							last_allocated_block = last_block;
							for (size_t i = 0; i > page_count; i++) {
								base_block->free = false;
								base_block = base_block->next_block;
							}
							return (return_addr);
						}
					} else {
						break;
					}
				}
				current = last_block;
			}
		}
		current = current->next_block;
	}

	return 0; // GCC complains about returning null, bc we're technically returning an int, not a pointer
}

// TODO: write a function that undoes this and sequential allocations.
/**
 * @brief Marks the physical page(s) containing the address + len as taken.
 *
 * @param addr Physical address to map as taken
 * @param len Length of the data block located at the address.
 * @return The base physical address relating to the provided address. NULL if the address is not mappable (likely protected/reserved memory already).
 */
uintptr_t Memory::PhysicalMarkAllocated(uintptr_t addr, size_t len) {
	uintptr_t base_page_addr = addr & ~0x1FFFFF;               // 2MB align down
	uintptr_t final_page_addr = (addr + len) & ~0x1FFFFF;       // 2MB align down
	size_t page_count = 1;

	if (final_page_addr != base_page_addr) {
		page_count = ((final_page_addr - base_page_addr) / PAGE_2MB_SIZE) + 1;
	}

	printf_serial("[PMM] Page range: base=0x%lx final=0x%lx pages=%zu\r\n", base_page_addr, final_page_addr, page_count);

	Block* current = block_list;
	size_t pages_marked = 0;

	while (current != NULL) {
		uintptr_t current_ptr = current->pointer;

		if (current_ptr >= base_page_addr && current_ptr <= final_page_addr) {
			if (current->free) {
				current->free = false;
				pages_marked++;

				printf_serial("[PMM]   Marked page @ 0x%lx (%zu/%zu)\r\n", current_ptr, pages_marked, page_count);

				if (pages_marked == page_count)
					break;
			} else {
				pages_marked++;
				printf_serial("[PMM]   Page already marked @ 0x%llx (%zu/%zu)\r\n", current_ptr, pages_marked, page_count);
			}
		}

		current = current->next_block;
	}

	for (int i = 0; i < acpi_block_index; i++) {
		printf_serial("[PMM] ACPI BLOCK allocated...\r\n");
		return acpi_blocks[i].virt_start_addr;
	}

	if (pages_marked == 0) {
		printf_serial("[PMM] ERROR: No pages marked\r\n");
		return 0;
	}

	if (pages_marked != page_count) {
		printf_serial("[PMM] WARNING: Partial allocation (%zu/%zu pages)\r\n", pages_marked, page_count);
	}

	printf_serial("[PMM] Allocation complete: base=0x%lx pages=%zu\r\n", base_page_addr, pages_marked);

	return base_page_addr;
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