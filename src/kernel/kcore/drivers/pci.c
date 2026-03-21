#include <stdint.h>
#include <stdio.h>

#include <acpi/acpi_api.h>
#include <cpu_io.h>
#include <memory/virtual_mem.h>
#include <drivers/serial.h>

#include <panic.h>

/* Due to the way I've structured the VMM, a local cache of virtual pages and corresponding addresses is the best way to deal with the needed MMIO regions.
 * This prevents a ton of unnecessary mapping if the MMIO region is overlapped with another 2MB page.
 */
// I arbitrarily chose 64, I don't think we'll need anywhere near 128MB of virtual address space for this. 
#define PCI_MMIO_CACHE_SIZE 64

typedef struct {
	uintptr_t phys_base; // 2MB-aligned physical base
	uintptr_t virt_base;
} PCIMMIOMapping;

PCIMMIOMapping pci_mmio_cache[PCI_MMIO_CACHE_SIZE];
size_t pci_mmio_cache_count = 0;

/**
 * @brief Looks up or maps the 2MB page containing the given physical address.
 *
 * @param phys_addr Physical address to be mapped
 * @return uintptr_t the virtual address corresponding to the physical address.
 */
uintptr_t pci_get_or_map_page(uintptr_t phys_addr) {
	uintptr_t phys_base = phys_addr & ~0x1FFFFF; // 2MB-align

	// Check the cache first
	for (size_t i = 0; i < pci_mmio_cache_count; i++) {
		if (pci_mmio_cache[i].phys_base == phys_base) {
			return pci_mmio_cache[i].virt_base + (phys_addr - phys_base);
		}
	}

	// If not cached, map it
	if (pci_mmio_cache_count >= PCI_MMIO_CACHE_SIZE) {
		panic_s("PCI MMIO page cache exhausted.");
	}

	uintptr_t virt_base = mapSequentialKernelPagesWithFlags(1, phys_base, PDE_FLAGS_UC_2MB);
	if (!virt_base) panic_s("Failed to map PCI MMIO page.");

	pci_mmio_cache[pci_mmio_cache_count++] = (PCIMMIOMapping){ phys_base, virt_base };

	return virt_base + (phys_addr - phys_base);
}


/**
 * @brief Reads a 32-bit value from PCI configuration space using CPU ports.
 *
 * Address calculation follows the standard ECAM layout:
 * - Bus:      bits [20-27]
 * - Device:   bits [15-19]
 * - Function: bits [12-14]
 * - Offset:   bits [00-11]
 *
 * @param entry Pointer to the MCFG entry describing the ECAM region.
 * @param bus PCI bus number (must be within entry->start_bus to entry->end_bus).
 * @param slot PCI device number (0-31).
 * @param func PCI function number (0-7).
 * @param offset Register offset within the PCI configuration space (must be 4-byte aligned).
 * @return The 32-bit value read from the specified PCI configuration register.
 */
uint32_t pci_read_legacy(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
	uint32_t address = (uint32_t) ((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t) 0x80000000));
	outl(0xCF8, address);
	return inl(0xCFC);
}

/**
 * @brief Reads a 32-bit value from PCI configuration space using ECAM (MCFG).
 *
 * Address calculation follows the standard ECAM layout:
 * - Bus:      bits [20-27]
 * - Device:   bits [15-19]
 * - Function: bits [12-14]
 * - Offset:   bits [00-11]
 *
 * @param entry Pointer to the MCFG entry describing the ECAM region.
 * @param bus PCI bus number (must be within entry->start_bus to entry->end_bus).
 * @param slot PCI device number (0-31).
 * @param func PCI function number (0-7).
 * @param offset Register offset within the PCI configuration space (must be 4-byte aligned).
 * @return The 32-bit value read from the specified PCI configuration register.
 */
uint32_t pci_read_mcfg(MCFGEntry* entry, uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
	uintptr_t phys_addr = (uintptr_t) entry->base_addr + (((bus - entry->start_bus) << 20) | (slot << 15) | (func << 12) | offset);

	uintptr_t virt_addr = pci_get_or_map_page(phys_addr);
	return *(volatile uint32_t*) virt_addr;
}

#include <drivers/pci_dev.h>

/**
 * @brief Enumerates and reports PCI functions for a given device on a bus.
 *
 * @param entry Pointer to the MCFG entry describing the ECAM region. If NULL, legacy PCI configuration access is used.
 * @param bus PCI bus number.
 * @param device PCI device number (slot) on the bus (0-31).
 */
void check_device(MCFGEntry* entry, uint8_t bus, uint8_t device) {
	for (uint8_t func = 0; func < 8; func++) {
		// I hate this ternary, but it's the nicest way to do this.
		uint32_t reg0 = (entry) ? pci_read_mcfg(entry, bus, device, func, 0) : pci_read_legacy(bus, device, func, 0);

		uint16_t vendor_id = reg0 & 0xFFFF;
		if (vendor_id == 0xFFFF) {
			// This is kinda the "universal" sign that it isn't there.
			// At the very least, the device is broken and we don't wanna deal with it.
			if (func == 0) return;
			// If we're not func 0, another func may exist.
			continue;
		}

		uint16_t device_id = (reg0 >> 16) & 0xFFFF;

		// Read Register 0x08: Class (bits 31-24), Subclass (bits 23-16), ProgIF, RevID
		uint32_t reg2 = (entry) ? pci_read_mcfg(entry, bus, device, func, 0x08) : pci_read_legacy(bus, device, func, 0x08);

		uint8_t base_class = (reg2 >> 24) & 0xFF;
		uint8_t sub_class = (reg2 >> 16) & 0xFF;

		const char* class_name = get_pci_class_name(base_class, sub_class);

		// printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "[%02x:%02x.%d] %04x:%04x - %s\n", bus, device, func, vendor_id, device_id, class_name);
		// printf_serial("[PCI][%02x:%02x.%d] %04x:%04x - %s\r\n", bus, device, func, vendor_id, device_id, class_name);

		const char* vendor_name = get_pci_vendor_name(vendor_id);
		const char* device_name = get_pci_device_name(vendor_id, device_id);

		// Fallbacks if your lookup returns NULL
		if (!vendor_name) vendor_name = "Unknown Vendor";
		if (!device_name) device_name = "Unknown Device";

		printf_color(
			PRINT_COLOR_CYAN, PRINT_DEFAULT_BG,
			"[%02x:%02x.%u](%04x:%04x) %s, %s\n",
			bus, device, func,
			vendor_id, device_id,
			vendor_name,
			class_name
		);

		printf_serial(
			"[PCI][%02x:%02x.%u](%04x:%04x) %-22s %-30s [%s][%02x:%02x]\r\n",
			bus, device, func,
			vendor_id, device_id,
			vendor_name,
			device_name,
			class_name,
			base_class, sub_class
		);

		// Check Header Type to see if it's a multi-function device
		uint32_t reg3 = (entry) ? pci_read_mcfg(entry, bus, device, func, 0x0C) : pci_read_legacy(bus, device, func, 0x0C);
		uint8_t header_type = (reg3 >> 16) & 0xFF;

		// If not multi-function (bit 7 clear) and we are on func 0, don't check func 1-7
		if (func == 0 && !(header_type & 0x80)) break;
	}
}

/**
 * @brief Scans a PCI bus and enumerates all devices and subordinate buses.
 *
 * @param entry Pointer to the MCFG entry describing the ECAM region. If NULL, legacy PCI configuration access is used.
 * @param bus PCI bus number to scan.
 */
void scan_bus(MCFGEntry* entry, uint8_t bus) {
	for (uint8_t dev = 0; dev < 32; dev++) {
		// Check if device exists at Func 0
		uint32_t reg0 = (entry) ? pci_read_mcfg(entry, bus, dev, 0, 0) : pci_read_legacy(bus, dev, 0, 0);
		if ((reg0 & 0xFFFF) == 0xFFFF) continue;

		// Parse all functions of this device (including bridges)
		check_device(entry, bus, dev);

		// Check if any function of this device is a bridge that needs stepping into
		for (uint8_t func = 0; func < 8; func++) {
			uint32_t reg0 = (entry) ? pci_read_mcfg(entry, bus, dev, func, 0) : pci_read_legacy(bus, dev, func, 0);
			if ((reg0 & 0xFFFF) == 0xFFFF) continue;

			uint32_t reg3 = (entry) ? pci_read_mcfg(entry, bus, dev, func, 0x0C) : pci_read_legacy(bus, dev, func, 0x0C);
			uint8_t header_type = (reg3 >> 16) & 0x7F;

			if (header_type == 0x01) { // It's a bridge!
				uint32_t reg6 = (entry) ? pci_read_mcfg(entry, bus, dev, func, 0x18) : pci_read_legacy(bus, dev, func, 0x18);
				uint8_t secondary_bus = (reg6 >> 8) & 0xFF;

				// Avoid infinite loops and don't scan back to parent
				if (secondary_bus > bus) {
					scan_bus(entry, secondary_bus);
				}
			}

			// If func 0 is not multi-function, don't check other functions for bridges
			if (func == 0 && !((reg3 >> 16) & 0x80)) break;
		}
	}
}

/**
 * @brief Enumerate the PCI bus to discover connected devices.
 *
 * If the MCFG (PCIe) isn't present, we use legacy I/O ports.
 */
void pci_discover(void) {
	MCFGTable* mcfg = get_mcfg();

	if (mcfg) {
		printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "Enumerating via MCFG (ECAM)...\n");
		for (int i = 0; i < mcfg->entry_count; i++) {
			scan_bus(&mcfg->entries[i], mcfg->entries[i].start_bus);
		}

	} else { // If NULL, we fallback to "Configuration Method #1" (great naming scheme PCI...)
		printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "MCFG not found. Falling back to Legacy IO Ports...\n");
		scan_bus(NULL, 0);
	}
}