# Kernel Allocator

> This is to document how the allocator works in general.
> If you are merely implementing something else in the kernel and need heap allocation, include `<memory/kernel_alloc.h>`.
> See [Core Allocation Functions](#core-allocation-functions) and [DMA Allocation](#dma-allocation)

The kernel allocator (`kalloc`/`kfree` and friends) is a slab allocator.
Rather than managing memory as one big heap, it preallocates 2MB chunks of memory from the virtual memory manager and carves each one into fixed-size objects.
This trades some memory for speed, allocation and deallocation reduce to a bitlist scan instead of walking a general-purpose free list.

Every 2MB chunk the allocator owns is called a **slab**.
A slab is dedicated to exactly one **object size**. A 2-byte slab only ever hands out 2-byte chunks, a 4096-byte slab only ever hands out page-table-sized chunks, etc.
Slabs of the same object size are chained together in a linked list, and if none of them have room, a fresh 2MB slab is allocated and appended.

## Slab Layout

```
|<--Header-->|<--BitList-->|<--Padding-->|<--1st Chunk-->|<--...-->|<--Last Chunk-->|
```

Every slab starts with a `slab_header_t`, immediately followed by a bitlist (one bit per chunk, tracking free/used), a small amount of padding to keep the first chunk aligned, and then the chunks themselves.

## Header

```C
typedef struct slab_header_t {
	size_t object_size;
	slab_header_t* next_slab;

	uintptr_t chunk_base;
	size_t chunk_count;     // there are chunk_count / 8 bytes in the bitlist

	// Only populated/used for DMA slabs. Regular kalloc() slabs leave these at 0.
	uintptr_t phys_base;    // physical address backing this slab's virtual chunk_base
	uint32_t  dma_zone;     // DMA_ZONE_* bits this slab was created with (DMA_ZONE_MASK applied)
	uint64_t  map_flags;    // page-table flags this slab's pages were mapped with
} slab_header_t;
```

A few things worth calling out that aren't obvious from the field names:

- There is **no** `bitlist` pointer in the header. The bitlist lives immediately after the header in memory and is located with the `BITLIST_BASE(header)` macro (`header + sizeof(slab_header_t)`), not through a stored pointer.
- `phys_base`, `dma_zone`, and `map_flags` only mean something for slabs created through `initSlabDMA()`.
  Regular `kalloc()`/`kfree()` slabs (from `initSlab()`) leave all three zeroed.
- `chunk_count` is always `bitlist_size_in_bytes * 8`. Every bit in the bitlist maps to exactly one chunk.

## Bit-list and Padding

### Formulae

Two formulas size out each slab, given page size _P_, header size _H_, and object size _C_ (all in
bytes):

- Bit-list size (in bytes): $B = \left\lfloor\dfrac{P-H}{8C+1}\right\rfloor$
- Padding between the bit-list and the first chunk: $P - H - B - (B \times C \times 8)$
  - The max possible padding is $7C$. The formula for _B_ assumes every bit of every byte in the bit-list maps to a real chunk, so there can be up to 7 leftover bits' worth of chunk space per slab that just isn't tracked.

### Pre-defined Slabs

`initKernelAllocator()` eagerly creates one slab per general-purpose object size, plus a dedicated slab for allocation-span metadata (used for multi-chunk allocations. See [Spans](#spans) below).
That's 10 slabs at 2MB apiece, so the allocator starts out at a flat **20MB** of kernel memory before a single `kalloc()` call is made:

| Object Size (bytes) | Bit-list size (bytes) | Padding (bytes) |
| :-----------------: | :-------------------: | :-------------: |
|          2          |        123358         |       10        |
|          4          |         63548         |       12        |
|          8          |         32263         |        1        |
|         16          |         16256         |       72        |
|         32          |         8159          |       233       |
|         64          |         4087          |       465       |
|         128         |         2045          |       971       |
|         256         |         1023          |       969       |
|        4096         |          63           |      32649      |

As object size grows, more of the slab is lost to padding relative to the bit-list.
This is expected and not currently worth fixing (see below).

Notably absent from this table: 1-byte chunks.
A 1-byte object size would need roughly 250KB of bitlist per 2MB slab just to track single-byte chunks, which is a terrible trade.
Instead, anything smaller than 2 bytes just gets rounded up into the 2-byte slab.
A little wasted space per allocation beats a permanently oversized bitlist.

This padding waste **could** be recovered by having the header track exactly how many trailing bits of the bitlist are unused (rather than assuming all of them are real chunks), but that changes the header size, which changes the formula, which isn't worth the problem right now.
There's no allocation pattern in the kernel that's sensitive to it.
The one object size under real pressure is the 4096-byte page-table entry slab, which is why it exists as its own dedicated size rather than being rounded up into some generic "big object" bucket.

### Reference calculator

```C
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 0x200000ULL
#define HEADER_SIZE sizeof(slab_header_t) // 56 bytes with the current header layout

uint64_t calculateBitlistSize(uint64_t chunksize) {
	uint64_t p = PAGE_SIZE - HEADER_SIZE;
	uint64_t divisor = (8 * chunksize) + 1;
	return p / divisor;
}

int main() {
	for (uint64_t sizes[] = {2, 4, 8, 16, 32, 64, 128, 256, 4096}, i = 0; i < 9; i++) {
		printf("%llu\n", calculateBitlistSize(sizes[i]));
	}
	return 0;
}
```

## Allocating and Freeing a Chunk

`kalloc(bytes)` picks the smallest object size that fits the request (2, 4, 8, 16, 32, 64, 128, 256, or 4096 bytes), then walks that size's slab chain looking for enough **consecutive** free chunks to cover the request.
A request bigger than one chunk (e.g. a 20-byte request against the 8-byte slab) needs 3 consecutive chunks, not just 3 free chunks anywhere in the bitlist. If no slab in the chain has room, a new 2MB slab of that object size is created and the search is retried via a single recursive call.

A few edge cases worth knowing:

- `kalloc(0)` is normalized to a 1-byte (→ 2-byte chunk) request, matching typical `malloc(0)` behavior.
- Requests larger than 2MB skip the slab system entirely and go straight to `Memory::MapSequentialKernelPages()` for a multi-page mapping.
  These allocations are **not** tracked by any slab and must eventually be freed accordingly. See the `krealloc` caveat below.
- `kfree(ptr)` figures out which slab owns `ptr` by checking whether it falls inside that slab's 2MB address range, then computes the chunk index from `(ptr - chunk_base) / object_size`.
  It also checks the DMA slab list first and refuses to free DMA memory through the regular path.
  That has to go through `kfree_dma()` instead, since dma slabs act differently than regular slabs.

## Spans

A single allocation can span more than one chunk (e.g. requesting 20 bytes from the 8-byte slab needs 3 chunks).
The bitlist alone can't tell `kfree()` how many chunks to release, a `chunk_count` isn't stored anywhere per-allocation, so multi-chunk allocations are additionally tracked in a separate structure:

```C
typedef struct allocated_span_t {
	uintptr_t ptr;
	size_t size;              // number of chunks, not bytes
	slab_header_t* slab_header;
	allocated_span_t* prev;
	allocated_span_t* next;
} allocated_span_t;
```

Spans live in their own doubly-linked list (`first_span`/`last_span`) and are themselves allocated out of a dedicated slab chain (`span_slab_start`/`span_slab_end`, created by `createSpanList()`), using the same bitlist mechanism as everything else, just for `allocated_span_t`-sized chunks instead of kernel data.
Single-chunk allocations never get a span. `addSpan()` is only called when `amount_of_objects > 1`.

`findSpan(ptr)` does a linear scan to check whether a pointer being freed has an associated span, and `removeSpan()` unlinks it, frees its chunk in the span slab, and clears its bitlist entry(ies).
Since `allocateSpan()` itself can run out of room, it calls `createSpanList()` and retries if every existing span slab is full, mirroring how `kalloc()` grows its own slab chains.

## Core Allocation Functions

- **`initKernelAllocator()`**: sets up all 9 general-purpose object-size slabs, the span slab, and the allocator's spinlock. Must run before any `kalloc`/`kfree` call.
- **`kalloc(bytes)`** / **`kfree(ptr)`**: the primary allocation pair, described above.
- **`kcalloc(count, size)`**: `kalloc(count * size)` with a zeroed result, plus an overflow check on `count * size` and a `NULL` return for a zero count or size (matching `calloc()` semantics).
- **`krealloc(ptr, new_size)`**: resizes a previous `kalloc`/`kcalloc`/`krealloc` allocation.
  - `ptr == NULL` behaves like `kalloc(new_size)`.
  - `new_size == 0` behaves like `kfree(ptr)` and returns `NULL`.
  - If the allocation (accounting for its span, if it has one) already has enough capacity, `krealloc` returns the same pointer unchanged.
  - Otherwise it allocates fresh memory, copies the old contents over, and frees the original.
  - `krealloc` explicitly refuses to operate on DMA memory (use `kalloc_dma`/`kfree_dma` for that) and on pointers it can't find in any tracked slab (which currently means any allocation over 2MB, since those bypass slab tracking and their true size is unknown).
    Growing or shrinking a >2MB allocation via `krealloc` is not supported and will fail rather than risk an out-of-bounds copy.

All of the above (aside from the >2MB path in `kalloc`, which still needs to be reasoned about separately) share the same `memlock` spinlock, so allocator state is safe to touch from multiple contexts.

## DMA Allocation

Some hardware needs memory with specific physical-address or alignment guarantees.
A device descriptor ring that must live under 4GB, for instance.

```C
void* kalloc_dma(size_t bytes, uint32_t flags, uint64_t map_flags, uintptr_t* phys_out);
void  kfree_dma(void* ptr);
```

- **`flags`** is a `dma_flags_t` bitmask:
  - `DMA_ZONE_ANY` / `DMA_ZONE_32BIT`: physical placement constraint.
    - `DMA_ZONE_32BIT` guarantees the backing physical address is below 4GB (`Memory::PhysicalAlloc2MB_32bit()`)
      - `DMA_ZONE_ANY` just grabs the next available 2MB physical range.
    - `DMA_ZONE_MASK` is the subset of `dma_flags_t` that affects which slab a chunk is drawn from.
      - Non-zone bits are deliberately excluded from that mask so they can't accidentally cause an `ANY`-zone and a `32BIT`-zone request to be treated as interchangeable.
  - `DMA_NO_ZERO`: skip zeroing the returned memory. DMA memory is zeroed by default (hardware reading stale descriptor data from a previous allocation can be a big problem), so only pass this when the caller is about to fully overwrite the chunk anyway.
  - `DMA_ALIGN_16` through `DMA_ALIGN_4096`: guarantees the returned pointer is aligned to at least that many bytes, by bumping the request up to whichever object-size class provides that alignment for free (e.g. `DMA_ALIGN_64` forces at least the 64-byte size class, even for a 10-byte request).
  - `DMA_ALIGN_NONE` (the default, value 0) adds no extra requirement beyond the object size's own alignment. At most one alignment flag should be set, they occupy a dedicated 3-bit field (`DMA_ALIGN_MASK`) in the flags word.
- **`map_flags`** are raw page-table flags (`BIT_PRESENT`, `BIT_WRITE`, `BIT_PCD`, ...) for how the backing pages get mapped. Passing `0` resolves to `DMA_MAP_FLAGS_DEFAULT`: (`BIT_SIZE | BIT_WRITE | BIT_PRESENT`) a normal present, writable, cached kernel mapping.
- **`phys_out`**: if non-`NULL`, receives the physical address backing the returned pointer. Most DMA devices require the physical address.

A DMA slab is matched for reuse only if its object size, zone, _and_ map flags all match the request.
Mixing, say, a cached and an uncached mapping in the same slab would mean two allocations from the same slab could need different page-table entries, which the current one-mapping-per-slab design can't express, and isn't desirable.
If no existing DMA slab matches, `initSlabDMA()` allocates a fresh physical 2MB region, maps it with the requested `map_flags`, and the search is retried recursively.

`kfree_dma()` mirrors `kfree()` (span-aware, chunk-zeroing) but only ever searches the DMA slab chain.
Requests over 2MB are rejected outright by `kalloc_dma()` (logged via `printf_serial`).
It is expected that a driver will allocate and track it's own memory if the request is this large.

## Adding a New Slab Size

If a new fixed size class is genuinely needed (rather than letting a request round up into an existing class):

1. Add it to `cache_type_t`.
2. Call `initSlab()` for it in `initKernelAllocator()` if it should be eagerly available at boot, and add its size/object-size to the `kalloc()`/`kalloc_dma()` size-selection chain (the `if (bytes <= N)`) so requests actually route to it.
3. Update the bit-list/padding table above. Use the reference calculator, updated with the real `sizeof(slab_header_t)` if the header has changed since.
