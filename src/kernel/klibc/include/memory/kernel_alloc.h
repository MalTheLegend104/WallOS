#ifndef KERNEL_ALLOC_H
#define KERNEL_ALLOC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif 

	void initKernelAllocator();
	void kfree(void* ptr);
	void* kalloc(size_t bytes);
	void* kcalloc(size_t count, size_t size);

	typedef enum {
		DMA_ZONE_ANY = 0,        // No placement constraint
		DMA_ZONE_32BIT = 1 << 0, // Physical address must be < 4GB
		DMA_NO_ZERO = 1 << 1,    // Skip zeroing the chunk 

		// Alignment field: bits 2-4. Set at most one.
		DMA_ALIGN_NONE = 0 << 2, // No explicit alignment requirement (default)
		DMA_ALIGN_16 = 1 << 2,   // Pointer guaranteed 16-byte aligned
		DMA_ALIGN_32 = 2 << 2,   // Pointer guaranteed 32-byte aligned
		DMA_ALIGN_64 = 3 << 2,   // Pointer guaranteed 64-byte aligned
		DMA_ALIGN_128 = 4 << 2,  // Pointer guaranteed 128-byte aligned
		DMA_ALIGN_256 = 5 << 2,  // Pointer guaranteed 256-byte aligned
		DMA_ALIGN_4096 = 6 << 2, // Pointer guaranteed 4096-byte (page) aligned
	} dma_flags_t;
#define DMA_ALIGN_MASK (7u << 2)

	/**
	 * @brief Allocates DMA-capable memory using the same slab allocator pattern as kalloc().
	 *
	 * @param bytes Number of bytes needed (up to PAGE_2MB_SIZE).
	 * @param flags dma_flags_t bits (DMA_ZONE_* / DMA_NO_ZERO) describing zone + behavior.
	 * @param map_flags Raw page-table flags (BIT_PRESENT, BIT_WRITE, BIT_PCD, ...) to map the
	 *                  backing pages with. Pass 0 to get the allocator's default of a present,
	 *                  writable, cached kernel mapping (BIT_PRESENT | BIT_WRITE).
	 * @param phys_out If non-NULL, receives the physical address backing the returned pointer.
	 */
	void* kalloc_dma(size_t bytes, uint32_t flags, uint64_t map_flags, uintptr_t* phys_out);

	/**
	 * @brief Frees memory previously returned by kalloc_dma(). Same call pattern as kfree() -
	 * no size parameter needed, the allocator already knows the chunk size.
	 */
	void kfree_dma(void* ptr);


#ifdef __cplusplus
}
#endif 
#endif // KERNEL_ALLOC_H