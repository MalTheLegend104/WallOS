#ifndef PHYSICAL_MEM_H
#define PHYSICAL_MEM_H

#include <multiboot.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_ORDER 11 // This is 8MiB
#define PAGE_SIZE 4096

#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))
#define ALIGN_UP(x, align)   (((x) + (align) - 1) & ~((align) - 1))

/* We keep certain flags relating to memory regions. We have 16 positions for flags (can be expanded to more, just causes more overhead).
 * These are meant to be used for debugging (by throwing panics when the PMM is misused), as well as enforcing that we never accidentally touch regions we shouldn't.
 */
#define PMM_PAGE_USABLE   (1 << 0)   // Currently in buddy free list
#define PMM_PAGE_RESERVED (1 << 1)   // Must never be allocated
#define PMM_PAGE_UNUSABLE (1 << 2)   // Bad RAM / holes
#define PMM_PAGE_ACPI     (1 << 3)   // ACPI reclaimable or NVS. These can only be touched by the ACPI subsystem.
#define PMM_PAGE_KERNEL   (1 << 4)   // Kernel image / mem_map / stacks
#define PMM_PAGE_DMA      (1 << 5)   // Belongs to DMA zones
#define PMM_PAGE_MMIO     (1 << 6)   // Belongs to a MMIO device
#define PMM_PAGE_BIT_7    (1 << 7)   // Reserved for future use
#define PMM_PAGE_BIT_8    (1 << 8)   // Reserved for future use
#define PMM_PAGE_BIT_9    (1 << 9)   // Reserved for future use
#define PMM_PAGE_BIT_10   (1 << 10)  // Reserved for future use
#define PMM_PAGE_BIT_11   (1 << 11)  // Reserved for future use
#define PMM_PAGE_BIT_12   (1 << 12)  // Reserved for future use
#define PMM_PAGE_BIT_13   (1 << 13)  // Reserved for future use
#define PMM_PAGE_BIT_14   (1 << 14)  // Reserved for future use
#define PMM_PAGE_BIT_15   (1 << 15)  // Reserved for future use

typedef struct {
	size_t total;
	size_t usable;
	size_t reserved;
	size_t free_pages;
} mmap_info;

#ifdef __cplusplus
extern "C" {
#endif

	/**
	 * @brief Builds the buddy allocator from the multiboot memory map.
	 */
	void pmm_init(struct multiboot_tag_mmap* mmap_tag);

	/* Information about the PMM used by commands */

	size_t pmm_total_free_bytes(void);
	size_t pmm_total_used_bytes(void);
	size_t pmm_free_page_count(void);
	size_t pmm_used_page_count(void);
	const mmap_info* pmm_get_mmap_info(void);
	uintptr_t pmm_phys_kernel_end(void);

	/**
	 * @brief Reserves an area of memory for specific things (framebuffer, known mmio regions for example).
	 * This is to prevent the mmap from overwriting it, and from the mmap from pointing to it.
	 * Only regions reserved before pmm_init() runs are taken out of the buddy lists, pmm_init() walks the list once.
	 */
	void pmm_reserve_memory(uintptr_t base_addr, size_t size);

	/**
	 * @brief Get a 2MB chunk of memory from below the 4GB mark.
	 * A lot of legacy hardware (OHCI, UHCI, etc) can only access 32 bit physical addresses.
	 * This should only be used sparingly, there's only so much 32 bit accessible memory that's free.
	 * @return Physical address of the chunk, or 0 if none is available.
	 */
	uintptr_t pmm_alloc_2mb_32bit(void);

	/**
	 * @brief Get a single 2MB chunk of physical memory (2MB aligned).
	 * @return Physical address of the chunk, or 0 on failure. Free with pmm_dealloc_2mb().
	 */
	uintptr_t pmm_alloc_2mb(void);

	/**
	 * @brief Get `amount` physically contiguous 2MB chunks.
	 * The underlying block is a power of two, so the amount is rounded up (1 -> 2MB, 2 -> 4MB, 3 or 4 -> 8MB).
	 * The maximum is 2^(MAX_ORDER - 9) chunks.
	 * @return Physical address, or 0 on failure.
	 */
	uintptr_t pmm_alloc_2mb_sequential(size_t amount);

	/**
	 * @brief Takes [addr, addr + len) out of the buddy lists and flags it reserved.
	 * @return The page-aligned start address of the range.
	 */
	uintptr_t pmm_mark_allocated(uintptr_t addr, size_t len);

	/**
	 * @brief Free a single 2MB chunk from pmm_alloc_2mb() or pmm_alloc_2mb_32bit().
	 */
	void pmm_dealloc_2mb(uintptr_t phys_addr);

	/* Everything below here is the interface used by the VMM. All of these take the PMM lock, so they are safe from any CPU. */

	/* Every allocated block has a reference count that starts at 1.
	 * The VMM bumps it when a frame is shared between address spaces (copy-on-write) and drops it when a mapping goes away.
	 * Code that never shares frames can ignore it and just call pmm_free_pages().
	 */

	/** @brief Allocates 2^order naturally aligned 4KB pages (order 0 = 4KB, 9 = 2MB). Returns the physical address, or 0 on failure. */
	uintptr_t pmm_alloc_pages(uint8_t order);

	/** @brief Same as pmm_alloc_pages(), but the whole block is below the 4GB mark. */
	uintptr_t pmm_alloc_pages_32bit(uint8_t order);

	/**
	 * @brief Allocate one 4KB page
	 * @return the physical address, or 0 on failure
	 */
	uintptr_t pmm_alloc_4kb(void);

	/**
	 * @brief Allocates exactly `pages` physically contiguous 4KB pages.
	 * Release with pmm_free_contiguous() and the same page count.
	 */
	uintptr_t pmm_alloc_contiguous(size_t pages);

	/**
	 * @brief Frees a block from pmm_alloc_contiguous().
	 */
	void pmm_free_contiguous(uintptr_t phys, size_t pages);

	/**
	 * @brief Frees a block from pmm_alloc_pages().
	 * The order is read back from the page metadata.
	 */
	void pmm_free_pages(uintptr_t phys);

	/* These last 4 are really only meant to be used by the VMM */

	uint32_t pmm_ref_get(uintptr_t phys);
	uint32_t pmm_ref_inc(uintptr_t phys);
	uint32_t pmm_ref_dec(uintptr_t phys);

	/**
	 * @brief Drops one reference, frees the block if it was the last one.
	 * @return true if the block was freed.
	 */
	bool pmm_put(uintptr_t phys);

#ifdef __cplusplus
}
#endif

#endif // PHYSICAL_MEM_H
