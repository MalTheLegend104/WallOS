#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <klibc/kprint.h>
#include <memory/kernel_alloc.h>
#include <memory/virtual_mem.hpp>


#define SET_BIT(bitlist_entry, bit)   (bitlist_entry = bitlist_entry | (1 << (8 - bit)))
#define CLEAR_BIT(bitlist_entry, bit) (bitlist_entry = bitlist_entry & ~(1 << (8 - bit)))
#define GET_BIT(bitlist_entry, bit)   (bitlist_entry & (1 << (8 - bit)))

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

	PAGE_ENTRY = 4096,
} cache_type_t;

// We're going to have a list of bits that contain information about what is free and taken.

typedef struct slab_header_t {
	size_t object_size;
	slab_header_t* next_slab;

	uintptr_t chunk_base;
	size_t chunk_count; // There will be this / 8 entries in bitlist
	uint8_t* bitlist;
} __attribute__((packed)) slab_header_t;

slab_header_t* first_slab;
slab_header_t* last_slab;

uint64_t calculatePadding(uint64_t bitlist_size, uint64_t chunksize) {
	return PAGE_2MB_SIZE - 64 - bitlist_size - (bitlist_size * chunksize * 8);
}

// This will leave up to 7 * chunksize of bytes left over.
// This would cause more memory wastage than it's worth to keep track of the rest. 
// It would use another byte, plus another few bytes to keep track of how many bits in that int are used.
uint64_t calculateBitlistSize(uint64_t chunksize) {
	// P/8C+1
	// We round down
	// P is the page size - the header
	uint64_t p = PAGE_2MB_SIZE - 64;
	uint64_t divisor = (8 * chunksize) + 1;
	return p / divisor;
}

typedef struct allocated_span_t {
	uintptr_t ptr;
	size_t size;
	allocated_span_t* next_span;
	allocated_span_t* prev_span;
} allocated_span_t;

allocated_span_t* first_span;
allocated_span_t* last_span;

slab_header_t* span_list_start;
slab_header_t* span_list_end;

void allocateSpanList() {
	uintptr_t base = Memory::NewKernelPage();
	slab_header_t* header = (slab_header_t*) base;
	header->object_size = sizeof(allocated_span_t);
	header->next_slab = NULL;

	uint64_t bls = calculateBitlistSize(sizeof(allocated_span_t));
	header->chunk_count = bls * 8;
	// Set all entries in the bitlist to zero.
	memset(header->bitlist, 0, bls);

	// Calculate the base
	uint64_t padding = calculatePadding(bls, sizeof(allocated_span_t));

	// This should be border aligned.
	// The calculation grows the chunklist "backwards" ensuring no overlap and a perfect alignment.
	header->chunk_base = base + 64 + bls + padding;

	span_list_start = header;
	span_list_end = header;

	printf("\t%u Byte Header Initialized.\n", sizeof(allocated_span_t));
}

allocated_span_t* allocateSpan() {
	slab_header_t* header = span_list_start;
	size_t chunk_number = 0;
	while (header != NULL) {
		if (header->object_size != sizeof(allocated_span_t)) {
			header = header->next_slab;
			continue;
		}
		chunk_number = 1;
		for (size_t i = 0; i < header->chunk_count / 8; i++) {
			for (int j = 1; j <= 8; j++) {
				if (!GET_BIT(header->bitlist[i], j)) {
					setChunkUsed(header, chunk_number);
					return (allocated_span_t*) (header->chunk_base + ((chunk_number - 1) * sizeof(allocated_span_t)));
				}
				chunk_number++;
			}
		}
		header = header->next_slab;
	}
	return NULL;
}

void freeSpan();

// Prints debug information about the slab.
void printSlabInfo(slab_header_t* info, uintptr_t base, size_t bls, size_t padding) {
	printf("\tADDR: 0x%llx\n", base);
	printf("\tOBJ_SIZE: %u\n", info->object_size);
	printf("\tCHUNK_BASE: 0x%llx\n", info->chunk_base);
	printf("\tCHUNK_COUNT: %llu\n", info->chunk_count);
	printf("\tBLS: %llu\n", bls);
	printf("\tPADDING: %llu\n", padding);
}

// This has to be seperate because we need a "first" entry in the linked list.
void initTwoByte() {
	/* Init 2 byte slab */
	uintptr_t two_byte_base = Memory::NewKernelPage();
	slab_header_t* two_byte_header = (slab_header_t*) two_byte_base;
	two_byte_header->object_size = WORD;
	two_byte_header->next_slab = NULL;

	uint64_t two_byte_bls = calculateBitlistSize(2);
	two_byte_header->chunk_count = two_byte_bls * 8;
	// Set all entries in the bitlist to zero.
	memset(two_byte_header->bitlist, 0, two_byte_bls);

	// Calculate the base
	uint64_t two_byte_padding = calculatePadding(two_byte_bls, 2);

	// This should be border aligned.
	// The calculation grows the chunklist "backwards" ensuring no overlap and a perfect alignment.
	two_byte_header->chunk_base = two_byte_base + 64 + two_byte_bls + two_byte_padding;

	first_slab = two_byte_header;
	last_slab = two_byte_header;

	printf("\t2 Byte Header Initialized.\n");
	//printSlabInfo(two_byte_header, two_byte_base, two_byte_bls, two_byte_padding);
}

/**
 * @brief Creates a slab of object_size byte chunks.
 *
 * @param object_size Amount of bytes per chunk.
 */
void initSlab(uint64_t object_size) {
	uintptr_t base = Memory::NewKernelPage();
	slab_header_t* header = (slab_header_t*) base;
	header->object_size = object_size;
	header->next_slab = NULL;

	uint64_t bls = calculateBitlistSize(object_size);
	header->chunk_count = bls * 8;
	// Set all entries in the bitlist to zero.
	memset(header->bitlist, 0, bls);

	// Calculate the base
	uint64_t padding = calculatePadding(bls, object_size);

	// This should be border aligned.
	// The calculation grows the chunklist "backwards" ensuring no overlap and a perfect alignment.
	header->chunk_base = base + 64 + bls + padding;

	last_slab->next_slab = header;
	last_slab = header;

	printf("\t%u Byte Header Initialized.\n", object_size);
	//printSlabInfo(header, base, bls, padding);
}

/**
 * @brief Initializes the kernel allocator. Creates a 2, 4, 8, and 4096 cache.
 */
void initKernelAllocator() {
	set_colors(VGA_COLOR_LIGHT_GREEN, VGA_DEFAULT_BG);
	printf("Initializing Kernel Slab Allocator.\n");
	set_to_last();
	set_colors(VGA_COLOR_GREEN, VGA_DEFAULT_BG);
	initTwoByte();
	initSlab(DWORD);
	initSlab(QWORD);
	initSlab(PAGE_ENTRY);
	set_to_last();
	// Init any more structures here (like FILE* or other common structs)
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

	SET_BIT(header->bitlist[bitlist_spot], index);
}

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

	CLEAR_BIT(header->bitlist[bitlist_spot], index);
}

void kfree(void* ptr) {
	slab_header_t* header = first_slab;
	while (header != NULL) {
		// If the addr is after the starting addr of the header and before the end address it's in that slab
		if (ptr > header && ptr < header + PAGE_2MB_SIZE) {
			size_t chunk = (size_t) ((uintptr_t) ptr - header->chunk_base) / header->object_size;
			memset(ptr, 0, header->object_size);
			setChunkFree(header, chunk);
			return;
		}
		header = header->next_slab;
	}
}

void* kalloc(size_t bytes) {
	size_t object_size = 2;
	if (bytes % 8 == 0) object_size = 8;
	else if (bytes % 4 == 0) object_size = 4;
	else if (bytes % 2 != 0) bytes++;
	size_t amount_of_objects = bytes / object_size;

	slab_header_t* header = first_slab;
	size_t chunk_number = 0;
	while (header != NULL) {
		if (header->object_size != object_size) {
			header = header->next_slab;
			continue;
		}

		size_t consective_chunks = 0;
		chunk_number = 0;
		for (size_t i = 0; i < header->chunk_count / 8; i++) {
			for (int j = 1; j <= 8; j++) {
				if (!GET_BIT(header->bitlist[i], j)) {
					if (consective_chunks == 0) chunk_number = (i * 8) + j;

					consective_chunks++;

					if (consective_chunks == amount_of_objects) {
						for (size_t k = 0; k < amount_of_objects; k++) {
							setChunkUsed(header, chunk_number + k);
						}
						goto finish;
					}
				} else {
					consective_chunks = 0;
				}
			}
			if (consective_chunks == 0) chunk_number = i * 8;
		}
		header = header->next_slab;
	}

finish:
	if (header == NULL) {
		// didnt find memory
		// for now we're just going to return a nullptr, but we're going to eventually allocate a new slab and then allocate it.
		return NULL;
	} else {
		//printf("chunk #: %llu\n", (chunk_number - 1));
		return (void*) (header->chunk_base + ((chunk_number - 1) * object_size));
	}
}