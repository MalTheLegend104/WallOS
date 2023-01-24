#ifndef INTERNAL_CALLS_H
#define INTERNAL_CALLS_H
#include <stdint.h>
#include <cpuid.h>
#ifdef __cplusplus
extern "C" {
#endif
	static inline uint64_t rdtsc(void) {
		uint32_t low, high;
		asm volatile("rdtsc":"=a"(low), "=d"(high));
		return ((uint64_t) high << 32) | low;
	}

	static inline unsigned long read_cr0(void) {
		unsigned long val;
		asm volatile ("mov %%cr0, %0" : "=r"(val));
		return val;
	}
#ifdef __cplusplus
}
#endif

#endif