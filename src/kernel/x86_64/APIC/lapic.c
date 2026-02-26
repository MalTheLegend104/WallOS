#include <stdint.h>
#include <stdlib.h>

#include <x86_64/lapic.h>

static volatile uint64_t* lapic_base = NULL;


static inline uint32_t lapic_read(uint32_t offset) {
	return *(volatile uint32_t*) ((uintptr_t) lapic_base + offset);
}

static inline void lapic_write(uint32_t offset, uint32_t value) {
	*(volatile uint32_t*) ((uintptr_t) lapic_base + offset) = value;

	// Ensure the write completes before returning
	// Reads from LAPIC registers are serializing, so do a dummy read
	(void) *(volatile uint32_t*) ((uintptr_t) lapic_base + offset);
}

void set_lapic_base(uint64_t* base) { lapic_base = base; }