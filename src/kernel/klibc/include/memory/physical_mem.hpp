#ifndef PHYSICAL_MEM_H
#define PHYSICAL_MEM_H


#include <multiboot.h>
#include <stdint.h>
#include <stddef.h>

#include <klibc/multiboot.h>

#define MAX_ORDER 11 // This is 8MiB
#define PAGE_SIZE 4096

#define ALIGN_DOWN(x, align)  ((x) & ~((align) - 1))
#define ALIGN_UP(x, align)    (((x) + (align) - 1) & ~((align) - 1))

/* We keep certain flags relating to memory regions. We have 16 positions for flags (can be expanded to more, just causes more overhead).
 * These are meant to be used for debugging (by throwing panics when the PMM is misused),
 * as well as enforcing that we never accidentally touch regions we shouldn't.
 */
#define PMM_PAGE_USABLE		(1 << 0)   // Currently in buddy free list
#define PMM_PAGE_RESERVED	(1 << 1)   // Must never be allocated
#define PMM_PAGE_UNUSABLE	(1 << 2)   // Bad RAM / holes
#define PMM_PAGE_ACPI		(1 << 3)   // ACPI reclaimable or NVS. These can only be touched by the ACPI subsystem.
#define PMM_PAGE_KERNEL		(1 << 4)   // Kernel image / mem_map / stacks
#define PMM_PAGE_DMA		(1 << 5)   // Belongs to DMA zone (future use)
#define PMM_PAGE_MMIO		(1 << 6)   // Belongs to a MMIO device
#define PMM_PAGE_BIT_7		(1 << 7)   // Reserved for future use
#define PMM_PAGE_BIT_8		(1 << 8)   // Reserved for future use
#define PMM_PAGE_BIT_9		(1 << 9)   // Reserved for future use
#define PMM_PAGE_BIT_10		(1 << 10)  // Reserved for future use
#define PMM_PAGE_BIT_11		(1 << 11)  // Reserved for future use
#define PMM_PAGE_BIT_12		(1 << 12)  // Reserved for future use
#define PMM_PAGE_BIT_13		(1 << 13)  // Reserved for future use
#define PMM_PAGE_BIT_14		(1 << 14)  // Reserved for future use
#define PMM_PAGE_BIT_15		(1 << 15)  // Reserved for future use

typedef struct {
	size_t total;
	size_t usable;
	size_t reserved;
	size_t free_pages;
} mmap_info;

namespace Memory {
	void PhysicalMemInit();

	namespace Info {
		size_t getTotalFreeBytes();
		size_t getTotalUsedBytes();
		size_t getFreePageCount();
		size_t getUsedPageCount();
		const mmap_info* getMMapInfo();
		uintptr_t getPhysKernelEnd();
	}

	/**
	 * @brief Get a 2MB chunk of memory from below the 4GB mark.
	 * A lot of legacy hardware (OHCI, UHCI, etc) can only access 32 bit physical addresses.
	 * This should only be used sparingly, there's only so much 32 bit accessible memory that's free.
	 */
	uintptr_t PhysicalAlloc2MB_32bit();

	uintptr_t PhysicalAlloc2MB();
	uintptr_t PhysicalAlloc2MBSequential(size_t amount);

	uintptr_t PhysicalMarkAllocated(uintptr_t base_addr, uintptr_t final_addr);

	void PhysicalDeAlloc2MB(uintptr_t phys_addr);
}

#endif // PHYSICAL_MEM_H