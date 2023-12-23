#ifndef PHYSICAL_MEM_H
#define PHYSICAL_MEM_H

#include <multiboot.h>

#ifdef __cplusplus
extern "C" {
#include <klibc/multiboot.hpp>
#endif

	int memtest(int argc, char** argv);
	void physical_mem_init();
#ifdef __cplusplus
}
#endif // __cplusplus
#endif // PHYSICAL_MEM_H