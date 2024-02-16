#ifndef KERNEL_ALLOC_H
#define KERNEL_ALLOC_H

#ifdef __cplusplus
extern "C" {
#endif 

	void initKernelAllocator();
	void kfree(void* ptr);
	void* kalloc(size_t bytes);

#ifdef __cplusplus
}
#endif 
#endif // KERNEL_ALLOC_H