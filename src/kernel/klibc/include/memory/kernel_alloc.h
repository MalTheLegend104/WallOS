#ifndef KERNEL_ALLOC_H
#define KERNEL_ALLOC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif 

	void initKernelAllocator();
	void kfree(void* ptr);
	void* kalloc(size_t bytes);
	void* kcalloc(size_t count, size_t size);

	void* kalloc_dma_64(size_t size, uintptr_t* phys_out);
	void kfree_dma(void* ptr, size_t size);

#ifdef __cplusplus
}
#endif 
#endif // KERNEL_ALLOC_H