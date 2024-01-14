# Memory

WallOS has a three layer approach to memory.

```
╔═════════════════╗    ╔════════════════╗     ╔═════════════════════╗
║ Physical Memory ╠════╣ Virtual Memory ╠══╦══╣   Kernel Allocator  ║
╚═════════════════╝    ╚════════════════╝  ║  ╚═════════════════════╝
                                           ║  ╔═════════════════════╗
                                           ╚══╣ Userspace Allocator ║
					      ╚═════════════════════╝
```

### Physical Memory

- `physical_mem.c/physical_mem.h`
- Buddy Allocation
- Deals with the physical allocation and dealloction of physical memory.

### Virtual Memory

- `virtual_mem.c/virtual_mem.hpp`
- Maps virtual address space to physical address space.
- Deals with page faults
- Interacts with the buddy allocator to get/free pages when needed

### Kernel Allocator

- `kernel_alloc.c/kernel_alloc.h`
- Slab Allocation
  - Deals with fragmentation and fixed size objects much better than other allocators
  - Useful since 90% of things in the kernel are fixed size objects
- Talks to the virtual memory layer to request more pages or free pages.

### Userspace Allocator

- `user_alloc.c/user_alloc.h`
- Heap Allocation
  - Much better for flexable (variable-sized) sized objects, meaning things like creating classes, loading files, dyanmic arrays, etc. are better managed.
  - Useful since most userspace programs don't allocate tons of fix-sized memory. Most user programs have differently sized classes, variables, strings, etc, being created and freed.
- Talks to the virtual memory layer to request more pages or free pages.

## Inter-Allocator communication

- Ideally, userspace programs should never be accessing memory created by the kernel allocator.
- On the same note, kernel allocator never needs to use the userspace allocator.

  - The virtual memory addresses potentially passed through syscalls can still be read by the kernel.
- The vitrual memory layer controls the mappings from physical to virtual and vise versa, meaning that the allocators never interact.

  - The virtual memory layer can ensure isolation of userspace and kernel, making memory more secure.

## Key Considerations

- Modularity:
  - Each layer is meant to be modular and independant, so changes on one layer shouldn't affect the functionality of the others.
- Isolation:
  - The virtual memory manager creates a strict enforcement of the separation between kernel and user space.
- Efficiency:
  - Slab allocation works better in a OS-level environement where most memory structures are a fixed size
    - Slab allocation works with fixed-sized things.
    - Imagine only being able to allocate things 1024 bytes (1KiB) at a time.
  - Heap allocation works well where there is a wide mix of sizes
    - Slower and more prone to fragmentation than slab allocation
    - Accomodates things like dynamic arrays and linked lists much more effectively.
    - Can allocate any size object needed.
