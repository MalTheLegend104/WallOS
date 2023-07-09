#include <stdint.h>
#include <gdt.h>
// Define the GDT
gdt_entry_t gdt[3];
gdt_descriptor_t gdtr;

// Function to set up a GDT entry
void gdt_entry_init(uint32_t index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity) {
    gdt[index].base_low = (base & 0xFFFF);
    gdt[index].base_middle = (base >> 16) & 0xFF;
    gdt[index].base_high = (base >> 24) & 0xFF;

    gdt[index].limit_low = (limit & 0xFFFF);
    gdt[index].granularity = ((limit >> 16) & 0x0F);
    gdt[index].granularity |= (granularity & 0xF0);

    gdt[index].access = access;
}

/**
 * @brief Loads the GDT.
 *
 * @param gdtr_ptr Pointer to the gdtr
 */
extern void gdt_load(uint64_t gdtr_ptr);

// Function to initialize the GDT
void gdt_init(void) {
    // Set up null descriptor
    gdt_entry_init(0, 0, 0, 0, 0);

    // Set up code segment descriptor
    gdt_entry_init(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    // Set up data segment descriptor
    gdt_entry_init(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    // Load the GDT
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint64_t) &gdt;
    gdt_load((uint64_t) &gdtr);
}