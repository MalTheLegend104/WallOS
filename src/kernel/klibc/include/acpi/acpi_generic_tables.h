#ifndef WALLOS_ACPI_GENERIC_TABLES_H
#define WALLOS_ACPI_GENERIC_TABLES_H

/* This header is meant to serve as a "bridge" between ACPI subsystems,
 * replicating the header format between them into generalized ones for things that may need them.
 *
 * This prevents the general system code from needing to deal with either uACPI or ACPICA directly.
 *
 * Current headers:
 * - MADT (APIC)
 * - HPET
 */

#include <stdint.h>

typedef struct {
	uint8_t type;      // 0 = Processor Local APIC, 1 = IO APIC, etc.
	uint8_t length;    // length of this entry
	union {
		struct {
			uint8_t acpi_processor_id;
			uint8_t apic_id;
			uint32_t flags; // bit 0 = enabled
		} lapic;

		struct {
			uint8_t io_apic_id;
			uint8_t reserved;
			uint32_t io_apic_addr;
			uint32_t gsi_base;
		} ioapic;

		struct {
			uint8_t  bus_source;	// Constant 0 (ISA)
			uint8_t  irq_source;	// The ISA IRQ (e.g., 0 for Timer)
			uint32_t gsi;			// What it's actually wired to (e.g., 2)
			uint16_t flags;			// Polarity and Trigger Mode
		} override;
	};
} MADTEntry __attribute__((packed));

// Generalized MADT table representation
typedef struct {
	uint32_t lapic_base;   // physical base address of LAPIC
	uint32_t flags;        // PC-AT flags from header

	MADTEntry* entries;    // pointer to an array of entries
	uint32_t entry_count;  // number of entries
} MADTTable __attribute__((packed));

typedef struct {
	uint64_t base_addr;        // physical MMIO address
	uint8_t  hpet_number;      // ACPI HPET ID
	uint16_t vendor_id;        // optional
	uint8_t  min_tick;         // smallest tick in fs
	uint8_t  flags;            // 0 = not periodic, 1 = periodic

	uint64_t main_counter;
} HPETTable __attribute__((packed));

#endif // WALLOS_ACPI_GENERIC_TABLES_H