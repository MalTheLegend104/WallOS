# Kernel Allocator

The kernel allocator is a slab allocator that preallocates structures for the kernel for quick allocation and deallocation.

The allocator gets 2MB chunks of memory from the virtual memory manager (which gives it physical memory and maps it to the kernel address space).

## Slab

The very basis of a slab allocator is the `slab` and the `cache`.
The `slab` is the physical chunk of memory, which contains a `cache` that the allocator can allocate fixed sized objects from.
This is the general structure of each slab:
```
|<--Header-->|<--BitList-->|<--Padding-->|<--1st Chunk-->|<--...-->|<--Last Chunk-->|
```

## Header

The structure of a slab contains a header:
  ```C
  typedef struct slab_header_t {
      size_t object_size;
      slab_header_t* next_slab;
      uintptr_t chunk_base;
      size_t chunk_count;
      uint8_t* bitlist;
  } slab_header_t;
  ```
- The slabs themselves act as a linked list, each pointing to the next slab.
- Each slab has a different `object_size`, which is the amount of bytes of an object.

## Bit-list and Padding

### Formulae

There are two important formulas for determining information about the slab:
- Bit-list size:
    $\frac{P-H}{8C+1}$
    Where *P* is the page size in bytes, *H* is the header the size in bytes, and *C* is the object size in bytes. 
- Padding between bit-list and first chunk:
    $P-H-B-(B*C*8)$ 
    Where *P*, *H*, and *C*, are the same as above, and *B* is the bit-list size in bytes (calculated using the formula above).
  - The maximum padding is $7*C$, since the system expects every bit in every uint8_t in the bit-list to be a usable chunk of memory.

### Pre-defined Slabs

There are 4 pre-defined slabs, resulting in a starting usage of 8MB of memory for the kernel.

| Object Size | Bit-list size | padding |
|:-----------:|:-------------:|:-------:|
|      2      |    123358     |    2    |
|      4      |     63548     |    4    |
|      8      |     32262     |   58    |
|    4096     |      63       |  32641  |
- As you can see, the higher the object size the more memory is wasted by padding.
  - This ***could*** be remedied by keeping track of how many bits are unused in the bit-list. 
  - This would change the size of the header and the formula would change.
- This isn't a concern as of right now, since there shouldn't be any super large sized objects. 
- The only large object that is in high demand is page table entries, hence the 4096 entry.

### Program to calculate bit-list size

```C
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 0x200000ULL

uint64_t calculateBitlistSize(uint64_t chunksize) {
	uint64_t p = PAGE_SIZE - 64;
	uint64_t divisor = (8 * chunksize) + 1;
	return p / divisor;
}

int main() {
    // Write C code here
    printf("%u\n", calculateBitlistSize(2));
    printf("%u\n", calculateBitlistSize(4));
    printf("%u\n", calculateBitlistSize(8));
    printf("%u\n", calculateBitlistSize(4096));
    return 0;
}
```