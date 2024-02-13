#include <memory/kernel_alloc.h>
#include <memory/virtual_mem.hpp>
#include <stdint.h>
#include <stddef.h>

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
} slab_header_t;

slab_header_t* first_slab;
slab_header_t* last_slab;

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

void initKernelAllocator() {
	// Init 2 byte slab
	uintptr_t two_byte_base = Memory::NewKernelPage();
	slab_header_t* two_byte_header = (slab_header_t*) two_byte_base;
	two_byte_header->object_size = WORD;
	two_byte_header->next_slab = NULL;

	// After removing the size of the header, there's 131068 chunks. This is 16383 uint8_t's. 
	// There's a balance between size of the bitlist and amount of chunks but I cant figure out the math for it right now.



	// Init 4 byte slab

	// Init 8 byte slab

	// Init page entry slab

}
