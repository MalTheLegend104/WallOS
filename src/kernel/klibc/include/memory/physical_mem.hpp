#ifndef PHYSICAL_MEM_H
#define PHYSICAL_MEM_H


#include <multiboot.h>
#include <stdint.h>
#include <stddef.h>

#include <klibc/multiboot.hpp>

namespace Memory {
	void PhysicalMemInit();

	namespace Info {
		size_t getFreePageCount();
		uintptr_t getPhysKernelEnd();
	}

	uintptr_t PhysicalAlloc2MB();
	void PhysicalDeAlloc2MB(uintptr_t phys_addr);
}

#endif // PHYSICAL_MEM_H