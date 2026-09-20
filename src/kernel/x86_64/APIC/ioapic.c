#include <x86_64/ioapic.h>
#include <memory/virtual_mem.h>

#include <drivers/serial.h>
#include <string.h>

static IOAPIC_Device ioapics[WALLOS_IOAPIC_MAX];
static uint32_t ioapic_count = 0;
static MADTTable* madt_ref = NULL;

// Direct Register Access
uint32_t ioapic_read_reg(uintptr_t base, uint32_t reg_index) {
	*(volatile uint32_t*) (base + IOAPIC_REGSEL) = reg_index;
	uint32_t val = *(volatile uint32_t*) (base + IOAPIC_IOWIN);

	// printf_serial("[IOAPIC] READ  base=0x%lx reg=0x%x -> 0x%x\r\n", base, reg_index, val);

	return val;
}

void ioapic_write_reg(uintptr_t base, uint32_t reg_index, uint32_t value) {
	// printf_serial("[IOAPIC] WRITE base=0x%lx reg=0x%x val=0x%x\r\n", base, reg_index, value);

	*(volatile uint32_t*) (base + IOAPIC_REGSEL) = reg_index;
	*(volatile uint32_t*) (base + IOAPIC_IOWIN) = value;
}

void ioapic_init(MADTTable* madt) {
	printf_serial("[IOAPIC] Initializing IOAPIC subsystem\r\n");

	madt_ref = madt;
	ioapic_count = 0;

	for (uint32_t i = 0; i < madt->entry_count; i++) {
		MADTEntry* e = &madt->entries[i];
		if (e->type != 1) continue;

		if (ioapic_count >= WALLOS_IOAPIC_MAX) break;

		IOAPIC_Device* dev = &ioapics[ioapic_count++];
		dev->id = e->ioapic.io_apic_id;
		dev->gsi_base = e->ioapic.gsi_base;

		uint32_t phys_addr = e->ioapic.io_apic_addr;

		// Calculate the 2MB alignment bc the VMM annoying
		uint32_t phys_base = phys_addr & ~(PAGE_2MB_SIZE - 1);
		uint32_t offset = phys_addr % PAGE_2MB_SIZE;

		printf_serial("[IOAPIC] Mapping IOAPIC: phys=0x%x (aligned to 0x%x, offset 0x%x)\r\n", phys_addr, phys_base, offset);

		// Map the aligned base address
		uintptr_t virt_base = mapSequentialKernelPagesWithFlags(
			1,
			phys_base,
			BIT_SIZE | BIT_WRITE | BIT_PRESENT | BIT_PCD
		);

		dev->mmio_base = virt_base + offset;

		printf_serial("[IOAPIC] MMIO mapped phys=0x%x -> virt=0x%lx\r\n", phys_addr, dev->mmio_base);

		uint32_t ver = ioapic_read_reg(dev->mmio_base, IOAPIC_VER);
		dev->max_redirection_entries = ((ver >> 16) & 0xFF) + 1;

		for (uint32_t pin = 0; pin < dev->max_redirection_entries; pin++) {
			ioapic_write_reg(dev->mmio_base, IOAPIC_REDTBL(pin), IOAPIC_RTE_MASK);
			ioapic_write_reg(dev->mmio_base, IOAPIC_REDTBL(pin) + 1, 0);
		}
	}
}

IOAPIC_Device* ioapic_find_for_gsi(uint32_t gsi) {
	for (uint32_t i = 0; i < ioapic_count; i++) {
		uint32_t start = ioapics[i].gsi_base;
		uint32_t end = start + ioapics[i].max_redirection_entries;

		if (gsi >= start && gsi < end) {
			printf_serial(
				"[IOAPIC] GSI %u handled by IOAPIC id=%u (range %u-%u)\r\n",
				gsi, ioapics[i].id, start, end - 1
			);

			return &ioapics[i];
		}
	}

	printf_serial("[IOAPIC] No IOAPIC found for GSI %u\r\n", gsi);

	return NULL;
}

void ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t cpu_apic_id, bool masked) {
	printf_serial(
		"[IOAPIC] Routing IRQ %u -> vector %u cpu_apic=%u masked=%u\r\n",
		irq, vector, cpu_apic_id, masked
	);

	uint32_t gsi = irq;
	uint16_t acpi_flags = 0;

	for (uint32_t i = 0; i < madt_ref->entry_count; i++) {
		if (madt_ref->entries[i].type == 2 &&
			madt_ref->entries[i].override.irq_source == irq) {

			gsi = madt_ref->entries[i].override.gsi;
			acpi_flags = madt_ref->entries[i].override.flags;

			printf_serial("[IOAPIC] Override: IRQ %u -> GSI %u flags=0x%x\r\n", irq, gsi, acpi_flags);

			break;
		}
	}

	IOAPIC_Device* dev = ioapic_find_for_gsi(gsi);
	if (!dev) {
		printf_serial("[IOAPIC] ERROR: Cannot route IRQ %u (no IOAPIC)\r\n", irq);
		return;
	}

	uint32_t pin = gsi - dev->gsi_base;

	printf_serial("[IOAPIC] Routing GSI %u -> IOAPIC id=%u pin=%u\r\n", gsi, dev->id, pin);

	uint32_t low = (vector & IOAPIC_RTE_VECTOR) | IOAPIC_DELMOD_FIXED;

	if ((acpi_flags & 0x3) == 0x03) {
		printf_serial("[IOAPIC] Polarity: Active LOW\r\n");
		low |= IOAPIC_RTE_INTPOL;
	}

	if (((acpi_flags >> 2) & 0x3) == 0x03) {
		printf_serial("[IOAPIC] Trigger: LEVEL\r\n");
		low |= IOAPIC_RTE_TRIGGER;
	}

	if (masked) {
		printf_serial("[IOAPIC] Entry initially masked\r\n");
		low |= IOAPIC_RTE_MASK;
	}

	uint32_t high = (uint32_t) cpu_apic_id << 24;

	// printf_serial("[IOAPIC] RTE low=0x%x high=0x%x\r\n", low, high);

	uint32_t temp_low = low | IOAPIC_RTE_MASK;

	ioapic_write_reg(dev->mmio_base, IOAPIC_REDTBL(pin), temp_low);
	ioapic_write_reg(dev->mmio_base, IOAPIC_REDTBL(pin) + 1, high);
	ioapic_write_reg(dev->mmio_base, IOAPIC_REDTBL(pin), low);

	printf_serial("[IOAPIC] Route complete IRQ=%u GSI=%u pin=%u vector=%u\r\n", irq, gsi, pin, vector);
}

void ioapic_set_mask(uint32_t gsi, bool masked) {
	printf_serial("[IOAPIC] Set mask GSI=%u masked=%u\r\n", gsi, masked);

	IOAPIC_Device* dev = ioapic_find_for_gsi(gsi);
	if (!dev) return;

	uint32_t pin = gsi - dev->gsi_base;

	uint32_t low = ioapic_read_reg(dev->mmio_base, IOAPIC_REDTBL(pin));

	printf_serial("[IOAPIC] Current RTE low=0x%x\r\n", low);

	if (masked) low |= IOAPIC_RTE_MASK;
	else        low &= ~IOAPIC_RTE_MASK;

	printf_serial("[IOAPIC] Updated RTE low=0x%x\r\n", low);

	ioapic_write_reg(dev->mmio_base, IOAPIC_REDTBL(pin), low);
}