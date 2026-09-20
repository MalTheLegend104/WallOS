#ifndef WALLOS_IOAPIC_H
#define WALLOS_IOAPIC_H

#include <stdint.h>
#include <stdbool.h>
#include <acpi/acpi_generic_tables.h>

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// IOAPIC Defines
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// I/O APIC Register Offsets (Direct MMIO Access)
#define IOAPIC_REGSEL       0x00    // Index Register (Read/Write)
#define IOAPIC_IOWIN        0x10    // Data Register (Read/Write)

// I/O APIC Internal Registers (Accessed via IOAPIC_REGSEL)
#define IOAPIC_ID           0x00    // Bits 24-27: I/O APIC ID
#define IOAPIC_VER          0x01    // Bits 16-23: Max Redirection Entry; Bits 0-7: Version
#define IOAPIC_ARB          0x02    // Arbitration Priority
#define IOAPIC_REDTBL(n)    (0x10 + 2 * (n)) // Redirection Table Entry n (Low 32 bits)
											 // High 32 bits are at IOAPIC_REDTBL(n) + 1

// Redirection Table Entry Bits (Lower 32-bits)
#define IOAPIC_RTE_VECTOR       0x000000FF // Bits 0-7: IDT Vector
#define IOAPIC_RTE_DELMOD       0x00000700 // Bits 8-10: Delivery Mode
#define IOAPIC_RTE_DESTMOD      0x00000800 // Bit 11: Destination Mode (0=Physical, 1=Logical)
#define IOAPIC_RTE_DELIVS       0x00001000 // Bit 12: Delivery Status (Read-only)
#define IOAPIC_RTE_INTPOL       0x00002000 // Bit 13: Interrupt Polarity (0=High, 1=Low)
#define IOAPIC_RTE_REMIRR       0x00004000 // Bit 14: Remote IRR (for Level Triggered)
#define IOAPIC_RTE_TRIGGER      0x00008000 // Bit 15: Trigger Mode (0=Edge, 1=Level)
#define IOAPIC_RTE_MASK         0x00010000 // Bit 16: Mask (0=Enabled, 1=Disabled)

// Delivery Mode Values
#define IOAPIC_DELMOD_FIXED     (0x0 << 8)
#define IOAPIC_DELMOD_LOWEST    (0x1 << 8)
#define IOAPIC_DELMOD_SMI       (0x2 << 8)
#define IOAPIC_DELMOD_NMI       (0x4 << 8)
#define IOAPIC_DELMOD_INIT      (0x5 << 8)
#define IOAPIC_DELMOD_EXTINT    (0x7 << 8)

typedef struct {
	uint8_t  id;
	uint32_t gsi_base;
	uint32_t max_redirection_entries;
	uintptr_t mmio_base;
} IOAPIC_Device;

/**
 * Initializes all I/O APICs found in the MADT.
 */
void ioapic_init(MADTTable* madt);

uint32_t ioapic_read_reg(uintptr_t base, uint32_t reg_index);
void ioapic_write_reg(uintptr_t base, uint32_t reg_index, uint32_t value);

void ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t cpu_apic_id, bool masked);

void ioapic_set_mask(uint32_t gsi, bool masked);

IOAPIC_Device* ioapic_find_for_gsi(uint32_t gsi);

#endif // WALLOS_IOAPIC_H