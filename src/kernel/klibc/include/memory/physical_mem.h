#ifndef PHYSICAL_MEM_H
#define PHYSICAL_MEM_H


#include <multiboot.h>
#ifdef __cplusplus
extern "C" {
#endif
	int memtest(int argc, char** argv);
#ifdef __cplusplus
}

#include <stdint.h>
#include <klibc/multiboot.hpp>

namespace Memory {
	void PhysicalMemInit();

	uintptr_t getPhysKernelEnd();

	uintptr_t PhysicalAlloc2MB();
	void PhysicalDeAlloc2MB(uintptr_t phys_addr);
}

#endif
#endif // PHYSICAL_MEM_H