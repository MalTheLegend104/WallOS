#ifndef PHYSICAL_MEM_H
#define PHYSICAL_MEM_H


#include <multiboot.h>
#include <stdint.h>
#include <stddef.h>

#include <klibc/multiboot.h>

typedef struct {
	size_t total;
	size_t usable;
	size_t reserved;
} mmap_info;

namespace Memory {
	void PhysicalMemInit();

	namespace Info {
		size_t getFreePageCount();
		size_t getUsedPageCount();
		uintptr_t getPhysKernelEnd();
		const mmap_info* getMMapInfo();
	}

	uintptr_t PhysicalAlloc2MB();
	uintptr_t PhysicalAlloc2MBSequential(size_t amount);

	uintptr_t PhysicalMarkAllocated(uintptr_t base_addr, uintptr_t final_addr);

	void PhysicalDeAlloc2MB(uintptr_t phys_addr);
}

#endif // PHYSICAL_MEM_H