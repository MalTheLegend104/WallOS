#ifndef PHYSICAL_MEM_H
#define PHYSICAL_MEM_H


#include <multiboot.h>
#ifdef __cplusplus
extern "C" {
#endif

	int memtest(int argc, char** argv);

#ifdef __cplusplus
}

#include <klibc/multiboot.hpp>

namespace Memory {
	void physical_mem_init();
}

#endif
#endif // PHYSICAL_MEM_H