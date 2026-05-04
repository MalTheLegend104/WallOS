// This *SIGNIFICANTLY* increases performance whenever there's thousands of allocations.
// I've never had it be a problem until a SuperMicro motherboard wanted me to deal with a 10MB ACPI structure.
#pragma GCC push_options
#pragma GCC optimize ("O3")
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <klibc/kprint.h>
#include <memory/kernel_alloc.h>
#include <memory/virtual_mem.h>
#include <drivers/serial.h>


#define SET_BIT(bitlist_entry, bit)   (bitlist_entry = bitlist_entry | (1 << (8 - bit)))
#define CLEAR_BIT(bitlist_entry, bit) (bitlist_entry = bitlist_entry & ~(1 << (8 - bit)))
#define GET_BIT(bitlist_entry, bit)   (bitlist_entry & (1 << (8 - bit)))
#define BITLIST_BASE(header)          ((uint8_t*) ((uintptr_t) header) + sizeof(slab_header_t))

/* General structure of each slab
 *
 * |<--Header-->|<--BitList-->|<--Padding-->|<--1st Chunk-->|<--...-->|<--Last Chunk-->|
 *
 * Each slab is a 2mb section of memory. Ideally this should be at ~10-12mb of memory usage for the entire kernel.
 * The allocator pre-allocates a 2mb chunk for 2 byte, 4 byte, and 8 byte entries. There's also an entry for page tables (4096 bytes).
 * This covers all basic datatypes, including strings, excluding singular 1 byte entries (like uint8_t).
 * 1 byte entries are *very* inefficient, with each 2mb chunk requiring ~250kb of memory for the bitlist.
 * When allocating things for say, strings, we can afford to waste a little bit of memory by allocating space for it in a 2 or 4 byte chunk.
 * When something request memory, we'll see if it fits nicely into a 8 byte entry, a 4 byte entry, or a 2 byte entry.
 * If it does, it'll allocate it in those respective chunks.
 * If it doesn't, and the requested size is more than 1 byte, it'll add a single byte to the requested amount and redo the search.
 * This reduces the overhead of the header, and results in less wasted memory.
 * There will still be fragmentation, but it will likely outweigh the 250kb overhead of the 1 byte chunk.
 */

typedef enum {
	WORD = 2,
	DWORD = 4,
	QWORD = 8,
	OWORD = 16,
	SIZE_32 = 32,
	SIZE_64 = 64,
	SIZE_128 = 128,
	SIZE_256 = 256,
	PAGE_ENTRY = 4096,
} cache_type_t;

// We're going to have a list of bits that contain information about what is free and taken.

typedef struct slab_header_t {
	size_t object_size;
	slab_header_t* next_slab;

	uintptr_t chunk_base;
	size_t chunk_count; // There will be this / 8 entries in bitlist
} slab_header_t;

slab_header_t* first_slab;
slab_header_t* last_slab;

typedef struct allocated_span_t {
	uintptr_t ptr;
	size_t size;
	slab_header_t* slab_header;
	allocated_span_t* prev;
	allocated_span_t* next;
} allocated_span_t;

slab_header_t* span_slab_start;
slab_header_t* span_slab_end;

allocated_span_t* first_span;
allocated_span_t* last_span;

// We keep a separate list of chunks that are exclusively under the 4GB mark. 
// This list should be very short, it's basically dedicated to very specific drivers
slab_header_t* dma32_slab_head = NULL;


uint64_t calculatePadding(uint64_t bitlist_size, uint64_t chunksize) {
	return PAGE_2MB_SIZE - sizeof(slab_header_t) - bitlist_size - (bitlist_size * chunksize * 8);
}

// This will leave up to 7 * chunksize of bytes left over.
// This would cause more memory wastage than it's worth to keep track of the rest. 
// It would use another byte, plus another few bytes to keep track of how many bits in that int are used.
uint64_t calculateBitlistSize(uint64_t chunksize) {
	// P/8C+1
	// We round down
	// P is the page size - the header
	uint64_t p = PAGE_2MB_SIZE - sizeof(slab_header_t);
	uint64_t divisor = (8 * chunksize) + 1;
	return p / divisor;
}


// Prints debug information about the slab.
void printSlabInfo(slab_header_t* info, uintptr_t base, size_t bls, size_t padding) {
	printf("\tADDR: 0x%llx\n", base);
	printf("\tOBJ_SIZE: %u\n", info->object_size);
	printf("\tCHUNK_BASE: 0x%llx\n", info->chunk_base);
	printf("\tCHUNK_COUNT: %llu\n", info->chunk_count);
	printf("\tBLS: %llu\n", bls);
	printf("\tPADDING: %llu\n", padding);
}

void createSpanList() {
	uintptr_t base = Memory::NewKernelPage();
	slab_header_t* header = (slab_header_t*) base;
	header->object_size = sizeof(allocated_span_t);
	header->next_slab = NULL;

	uint64_t bls = calculateBitlistSize(sizeof(allocated_span_t));
	header->chunk_count = bls * 8;
	// Set all entries in the bitlist to zero.
	// memset((header + sizeof(header)), 0, bls);
	memset((uint8_t*) header + sizeof(slab_header_t), 0, bls);

	// Calculate the base
	uint64_t padding = calculatePadding(bls, sizeof(allocated_span_t));

	// This should be border aligned.
	// The calculation grows the chunklist "backwards" ensuring no overlap and a perfect alignment.
	header->chunk_base = base + sizeof(slab_header_t) + bls + padding;

	if (span_slab_start == NULL) {
		span_slab_start = header;
	} else {
		span_slab_end->next_slab = header;
	}
	span_slab_end = header;
}

/**
 * @brief Creates a slab of object_size byte chunks.
 *
 * @param object_size Amount of bytes per chunk.
 */
void initSlab(uint64_t object_size) {
	// printf_serial("[KALLOC] initSlab called with obj_size: 0x%llx\r\n", object_size);
	uintptr_t base = Memory::NewKernelPage();
	slab_header_t* header = (slab_header_t*) base;
	header->object_size = object_size;
	header->next_slab = NULL;

	uint64_t bls = calculateBitlistSize(object_size);
	header->chunk_count = bls * 8;
	// Set all entries in the bitlist to zero.
	// memset((header + sizeof(header)), 0, bls);
	memset((uint8_t*) header + sizeof(slab_header_t), 0, bls);

	// Calculate the base
	uint64_t padding = calculatePadding(bls, object_size);

	// This should be border aligned.
	// The calculation grows the chunklist "backwards" ensuring no overlap and a perfect alignment.
	header->chunk_base = base + sizeof(slab_header_t) + bls + padding;

	if (first_slab == NULL) {
		first_slab = header;
	} else {
		last_slab->next_slab = header;
	}
	last_slab = header;

	//printSlabInfo(header, base, bls, padding);
}

#include <memory/physical_mem.hpp>

/**
 * @brief Initializes a new 2MB slab using 32-bit physical memory
 */
slab_header_t* initSlab32(uint64_t object_size) {
	// Uses your new 32-bit physical allocator
	uintptr_t phys = Memory::PhysicalAlloc2MB_32bit();
	if (!phys) return NULL;

	// Map it into the kernel virtual address space
	// TODO: This needs a new 32 bit friendly version
	uintptr_t base = Memory::MapSequentialKernelPages(1, phys);

	slab_header_t* header = (slab_header_t*) base;
	header->object_size = object_size;
	header->next_slab = NULL;

	uint64_t bls = calculateBitlistSize(object_size);
	header->chunk_count = bls * 8;
	memset((uint8_t*) header + sizeof(slab_header_t), 0, bls);

	uint64_t padding = calculatePadding(bls, object_size);
	header->chunk_base = base + sizeof(slab_header_t) + bls + padding;

	return header;
}


void* kalloc_dma_64(size_t size, uintptr_t* phys_out) {
	if (size > PAGE_2MB_SIZE) {
		printf_serial("[KALLOC] Requested DMA size of %d bytes (0x%x). Too large for current allocator...\r\n", size, size);
		return NULL;
	}
	// I'm too lazy to allocate smaller physical pages at the moment...
	uintptr_t phys = Memory::PhysicalAlloc2MBSequential(1);
	if (!phys) return NULL;

	uintptr_t virt = Memory::MapSequentialKernelPages(1, phys);
	if (!virt) return NULL;

	// DMA memory MUST be zeroed to prevent hardware from reading garbage/old descriptor data.
	memset((void*) virt, 0, PAGE_2MB_SIZE);

	if (phys_out) *phys_out = phys;
	return (void*) virt;
}

void kfree_dma(void* ptr, size_t size) {
	uintptr_t virt = (uintptr_t) ptr;
	uintptr_t phys = Memory::VirtToPhysBase(virt);

	// I really need a way to do this....
	// Memory::unmap(virt, size);

	Memory::PhysicalDeAlloc2MB(virt);
}

#include <memory/spinlock.h>

spinlock_t* memlock;

/**
 * @brief Initializes the kernel allocator. Creates a 2, 4, 8, and 4096 cache.
 */
void initKernelAllocator() {
	printf_color(PRINT_COLOR_LIGHT_GREEN, PRINT_DEFAULT_BG, "Initializing Kernel Slab Allocator.\n");

	display_set_colors(PRINT_COLOR_GREEN, PRINT_DEFAULT_BG);

	initSlab(WORD);
	printf("\t%u Byte Header Initialized.\n", WORD);

	initSlab(DWORD);
	printf("\t%u Byte Header Initialized.\n", DWORD);

	initSlab(QWORD);
	printf("\t%u Byte Header Initialized.\n", QWORD);

	initSlab(OWORD);
	printf("\t%u Byte Header Initialized.\n", OWORD);

	initSlab(SIZE_32);
	printf("\t%u Byte Header Initialized.\n", SIZE_32);

	initSlab(SIZE_64);
	printf("\t%u Byte Header Initialized.\n", SIZE_64);

	initSlab(SIZE_128);
	printf("\t%u Byte Header Initialized.\n", SIZE_128);

	initSlab(SIZE_256);
	printf("\t%u Byte Header Initialized.\n", SIZE_256);

	initSlab(PAGE_ENTRY);
	printf("\t%u Byte Header Initialized.\n", PAGE_ENTRY);

	// This is broken, causes an infinite loop
	// I didn't even try to debug it.
	// initSlab32(PAGE_ENTRY);
	// printf("\t32-Bit DMA (%u Bytes) Header Initialized.\n", PAGE_ENTRY);

	createSpanList();
	printf("\t%u Byte Header Initialized.\n", sizeof(allocated_span_t));

	memlock = spinlock_create();
	printf("\tCreated Spinlock.\n");

	display_set_colors_default();
}

/**
 * @brief Set if the chunk is used in the bitlist.
 *
 * @param header Header containing the chunk
 * @param chunk chunk number in the slab
 */
void setChunkUsed(slab_header_t* header, size_t chunk) {
	// We can divide the chunk by 8 to get which spot in the bitlist it is
	size_t bitlist_spot = chunk / 8;
	// Anything that's 8 % 8 = 0 will produce a number 1 too high
	if (chunk % 8 == 0) bitlist_spot--;

	// The bitlist is in squential order, so it starts filling at the highest bit of the uint8_t
	// This means bit "8" is the final bit in 0b00000001
	// The macro takes this into account.
	uint8_t index = chunk % 8;
	if (chunk % 8 == 0) index = 8;

	SET_BIT(BITLIST_BASE(header)[bitlist_spot], index);
}

// void setChunkUsed(slab_header_t* header, size_t chunk) {
// 	size_t zero_indexed_chunk = chunk - 1;
// 	size_t byte_idx = zero_indexed_chunk / 8;
// 	size_t bit_idx = zero_indexed_chunk % 8;
// 	SET_BIT(BITLIST_BASE(header)[byte_idx], (bit_idx + 1));  // +1 for macro's 1-8 indexing
// }

/**
 * @brief Set if the chunk is free in the bitlist.
 *
 * @param header Header containing the chunk
 * @param chunk chunk number in the slab
 */
void setChunkFree(slab_header_t* header, size_t chunk) {
	size_t bitlist_spot = chunk / 8;
	if (chunk % 8 == 0) bitlist_spot--;

	uint8_t index = chunk % 8;
	if (chunk % 8 == 0) index = 8;

	CLEAR_BIT(BITLIST_BASE(header)[bitlist_spot], index);
}

allocated_span_t* allocateSpan() {
	slab_header_t* header = span_slab_start;
	size_t chunk_number = 0;
	while (header != NULL) {
		chunk_number = 0;
		for (size_t i = 0; i < header->chunk_count / 8; i++) {
			chunk_number = i * 8;
			for (int j = 1; j <= 8; j++) {
				if (!GET_BIT(BITLIST_BASE(header)[i], j)) {
					chunk_number = (i * 8) + j;

					setChunkUsed(header, chunk_number);

					// printf_serial("i: %llu -> j: %llu\nchunk#: %llu\n", i, j, chunk_number);
					// printf_serial("[KALLOC] Header: 0x%llx -> bitlist_base: 0x%llx\r\n", header, BITLIST_BASE(header));
					// printf_serial("\tchunk count: %llu -> object_size: %llu\r\n", header->chunk_count, header->object_size);
					// printf_serial("\tchunk base: 0x%llx\r\n", header->chunk_base);
					goto span_finish;
				}
			}
		}
		header = header->next_slab;
	}

span_finish:
	if (header == NULL) {
		createSpanList();
		return allocateSpan();
	} else {
		allocated_span_t* ptr = (allocated_span_t*) (header->chunk_base + ((chunk_number - 1) * sizeof(allocated_span_t)));

		// Safety: Zero out the metadata before returning it
		memset(ptr, 0, sizeof(allocated_span_t));

		ptr->slab_header = header;
		return ptr;
	}
}

void addSpan(uintptr_t ptr, size_t count) {
	allocated_span_t* span = allocateSpan();
	if (span == NULL) return;

	span->ptr = ptr;
	span->size = count;
	span->next = NULL;
	span->prev = last_span;

	if (first_span == NULL) {
		first_span = span;
	} else {
		last_span->next = span;
	}

	last_span = span;

	//printf("added span: addr: 0x%llx -> ptr: 0x%llx -> size: %llu\n", (uintptr_t) span, span->ptr, span->size);
}

allocated_span_t* findSpan(uintptr_t ptr) {
	allocated_span_t* span = first_span;
	while (span != NULL) {
		if (span->ptr == ptr) return span;
		span = span->next;
	}
	return span;
}

void removeSpan(allocated_span_t* span) {
	// printf_serial("removeSpan ptr:0x%llx size:0x%llx next:0x%llx hdr:0x%llx hdr_obj:0x%llx\r\n", span, span->size, span->next, span->slab_header, span->slab_header->object_size);

	if (span->prev != NULL)
		span->prev->next = span->next;
	if (span->next != NULL)
		span->next->prev = span->prev;

	if (first_span == span) first_span = span->next;
	if (last_span == span) last_span = span->prev;

	slab_header_t* header = span->slab_header;
	size_t chunk = (size_t) ((uintptr_t) span - header->chunk_base) / header->object_size;
	chunk++;

	// memset(span, 0, header->object_size);
	memset(span, 0, sizeof(allocated_span_t));
	setChunkFree(header, chunk);
}

void kfree(void* ptr) {
	spinlock_lock(memlock);

	if ((uintptr_t) ptr <= 0x100000000) {
		slab_header_t* h = dma32_slab_head;
		while (h != NULL) {
			if ((uintptr_t) ptr > (uintptr_t) h && (uintptr_t) ptr < (uintptr_t) h + PAGE_2MB_SIZE) {
				spinlock_unlock(memlock);
				return;
			}
			h = h->next_slab;
		}
	}

	slab_header_t* header = first_slab;
	while (header != NULL) {
		// If the addr is after the starting addr of the header and before the end address it's in that slab
		if ((uintptr_t) ptr > (uintptr_t) header && (uintptr_t) ptr < (uintptr_t) header + PAGE_2MB_SIZE) {
			size_t chunk = (size_t) ((uintptr_t) ptr - header->chunk_base) / header->object_size;
			chunk++; // The caluclation gives it in terms of index, we need index + 1
			allocated_span_t* span = findSpan((uintptr_t) ptr);
			if (span != NULL) {
				//printf("Found span: 0x%llx -> PTR: 0x%llx -> SIZE: 0x%llx\n", span, span->ptr, span->size);

				for (size_t i = 0; i < span->size; i++) {
					setChunkFree(header, chunk + i);
				}

				memset(ptr, 0, span->size * header->object_size);
				removeSpan(span);
			} else {
				memset(ptr, 0, header->object_size);
				setChunkFree(header, chunk);
			}
			spinlock_unlock(memlock);
			return;
		}
		header = header->next_slab;
	}
	spinlock_unlock(memlock);
}

#include <drivers/serial.h>
void* kalloc(size_t bytes) {
	spinlock_lock(memlock);

	if (bytes > PAGE_2MB_SIZE) {
		printf_serial("[KALLOC] Requested memory allocation.\r\n\tBytes: 0x%llx ", bytes);
		void* ret = (void*) Memory::MapSequentialKernelPages(((int) (bytes / PAGE_2MB_SIZE)) + 1);
		printf_serial("virt: 0x%llx", (uint64_t) ret);
		spinlock_unlock(memlock);
		return ret;
	}

	// Determine best object size - pick the smallest size that fits
	size_t object_size;
	if (bytes <= 2)   object_size = WORD;
	else if (bytes <= 4)   object_size = DWORD;
	else if (bytes <= 8)   object_size = QWORD;
	else if (bytes <= 16)  object_size = OWORD;
	else if (bytes <= 32)  object_size = SIZE_32;
	else if (bytes <= 64)  object_size = SIZE_64;
	else if (bytes <= 128) object_size = SIZE_128;
	else if (bytes <= 256) object_size = SIZE_256;
	else                   object_size = PAGE_ENTRY;

	size_t amount_of_objects = (bytes + object_size - 1) / object_size;

	slab_header_t* header = first_slab;

	while (header != NULL) {
		if (header->object_size != object_size) {
			header = header->next_slab;
			continue;
		}

		size_t consecutive_chunks = 0;
		size_t start_chunk = 0;

		// Iterate through all chunks linearly
		for (size_t chunk = 1; chunk <= header->chunk_count; chunk++) {
			size_t byte_idx = (chunk - 1) / 8;
			size_t bit_idx = ((chunk - 1) % 8) + 1;

			if (!GET_BIT(BITLIST_BASE(header)[byte_idx], bit_idx)) {
				if (consecutive_chunks == 0) {
					start_chunk = chunk;
				}
				consecutive_chunks++;

				if (consecutive_chunks == amount_of_objects) {
					// Found enough consecutive chunks
					for (size_t k = 0; k < amount_of_objects; k++) {
						setChunkUsed(header, start_chunk + k);
					}

					uintptr_t ptr = header->chunk_base + ((start_chunk - 1) * object_size);
					if (amount_of_objects > 1) {
						addSpan(ptr, amount_of_objects);
					}
					spinlock_unlock(memlock);
					return (void*) ptr;
				}
			} else {
				consecutive_chunks = 0;
			}
		}

		header = header->next_slab;
	}

	// No space found, create new slab
	printf_serial("[KALLOC] Requested memory allocation. Bytes: 0x%llx\r\n", bytes);
	initSlab(object_size);

	spinlock_unlock(memlock);
	return kalloc(bytes);
}

/**
 * @brief Allocates a 4096-byte chunk guaranteed to be below 4GB physical.
 */
void* kalloc_32bit_4k() {
	spinlock_lock(memlock);

	slab_header_t* header = dma32_slab_head;
	const size_t object_size = 4096;

	while (header != NULL) {
		// We only store 4096-byte chunks in this list, but safety first
		if (header->object_size == object_size) {
			for (size_t chunk = 1; chunk <= header->chunk_count; chunk++) {
				size_t byte_idx = (chunk - 1) / 8;
				size_t bit_idx = ((chunk - 1) % 8) + 1;

				if (!GET_BIT(BITLIST_BASE(header)[byte_idx], bit_idx)) {
					setChunkUsed(header, chunk);
					uintptr_t ptr = header->chunk_base + ((chunk - 1) * object_size);

					spinlock_unlock(memlock);
					return (void*) ptr;
				}
			}
		}
		header = header->next_slab;
	}

	// If we reach here, we need a new 32-bit slab
	printf_serial("[KALLOC32] Allocating new 32-bit DMA slab (4KB objects)\r\n");
	slab_header_t* new_slab = initSlab32(object_size);

	if (!new_slab) {
		printf_serial("[KALLOC32] FATAL: Out of 32-bit physical memory!\r\n");
		spinlock_unlock(memlock);
		return NULL;
	}

	// Add to the 32-bit specific list
	new_slab->next_slab = dma32_slab_head;
	dma32_slab_head = new_slab;

	spinlock_unlock(memlock);

	// Recurse once to grab the first chunk from the new slab
	return kalloc_32bit_4k();
}

#pragma GCC pop_options