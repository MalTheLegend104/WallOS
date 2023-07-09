#ifndef GDT_H
#define GDT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Define the GDT entry structure
	typedef struct {
		uint16_t limit_low;
		uint16_t base_low;
		uint8_t base_middle;
		uint8_t access;
		uint8_t granularity;
		uint8_t base_high;
	} __attribute__((packed)) gdt_entry_t;

	// Define the GDT descriptor structure
	typedef struct {
		uint16_t limit;
		uint64_t base;
	} __attribute__((packed)) gdt_descriptor_t;

	// Function to set up a GDT entry
	void gdt_entry_init(uint32_t index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity);

	/**
	 * @brief Loads the GDT.
	 *
	 * @param gdtr_ptr Pointer to the gdtr
	 */
	extern void gdt_load(uint64_t gdtr_ptr);

	// Function to initialize the GDT
	void gdt_init();
#ifdef __cplusplus
}
#endif
#endif