#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <acpi/acpi_api.h>
#include <cpu_io.h>

#include <memory/virtual_mem.h>
#include <memory/kernel_alloc.h>
#include <device/device_manager.h>

#include <drivers/serial.h>
#include <drivers/pci.h>
// This is auto-generated using <proj_root>/tools/pci_dev/pci_dev.py
#include <drivers/pci_dev.h>

#include <panic.h>


char* alloc_device_name(const char* short_name) {
	if (!short_name) return NULL;

	size_t base_len = strlen(short_name);

	for (int index = 0; index < 256; index++) {
		char num_buf[12];
		int num_len = strlen(itoa((long long) index, num_buf, 10));
		if (num_len == 0) return NULL;

		size_t total_len = base_len + num_len;
		char* name = (char*) kalloc(total_len + 1);
		if (!name) return NULL;

		memcpy(name, short_name, base_len);
		memcpy(name + base_len, num_buf, num_len + 1); // +1 for \0

		if (!find_device_by_name(name)) {
			return name; // Caller owns this
		}

		kfree(name); // Already taken, try next
	}

	// TODO: THIS MUST BE FREED ON DEVICE DESTRUCTION
	return NULL;
}


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

/**
 * @brief Maps PCI class/subclass to device_interface_flags_t.
 */
device_interface_flags_t pci_class_to_interface(uint8_t base_class, uint8_t sub_class) {
	device_interface_flags_t flags = DEV_INT_PCI;

	switch (base_class) {
		case 0x01: // Mass Storage
			switch (sub_class) {
				case 0x06: flags |= DEV_INT_AHCI; break;
				case 0x08: flags |= DEV_INT_NVME; break;
				default:   break;
			}
			break;

		case 0x03: // Display
			flags |= DEV_INT_VIDEO;
			break;

		case 0x04: // Multimedia
			flags |= DEV_INT_AUDIO; // covers HDA and others
			break;

		case 0x06: // Bridge. There's no protocol flag, we need it for topology only
			break;

		case 0x0C: // Serial Bus
			switch (sub_class) {
				case 0x03: // USB
					// ProgIF tells us which USB controller type we're dealing with
					// caller will need to refine this after reading reg2's ProgIF bits
					flags |= DEV_INT_USB;
					break;
				default:
					break;
			}
			break;

		case 0x02: // Network
			flags |= DEV_INT_NET_MAC;
			break;

		default:
			flags |= DEV_INT_UNKNOWN;
			break;
	}

	return flags;
}

/**
 * @brief Refines USB controller flags using the ProgIF byte from register 0x08.
 *        Call this after pci_class_to_interface() for USB devices.
 */
static device_interface_flags_t pci_usb_progif_to_controller(uint8_t prog_if) {
	switch (prog_if) {
		case 0x00: return DEV_INT_UHCI;
		case 0x10: return DEV_INT_OHCI;
		case 0x20: return DEV_INT_EHCI;
		case 0x30: return DEV_INT_XHCI;
		default:   return DEV_INT_UNKNOWN;
	}
}

// Forward declare because we have some recursion
void scan_bus(MCFGEntry* entry, uint8_t bus, wallos_device_t* parent);

/**
 * @brief Enumerates and reports PCI functions for a given device on a bus.
 *
 * @param entry Pointer to the MCFG entry describing the ECAM region. If NULL, legacy PCI configuration access is used.
 * @param bus PCI bus number.
 * @param device PCI device number (slot) on the bus (0-31).
 */
wallos_device_t* check_device(MCFGEntry* entry, uint8_t bus, uint8_t device, wallos_device_t* parent) {
	wallos_device_t* first_dev = NULL;

	for (uint8_t func = 0; func < 8; func++) {
		// I hate this ternary, but it's the nicest way to do this.
		uint32_t reg0 = (entry) ? pci_read_mcfg(entry, bus, device, func, 0) : pci_read_legacy(bus, device, func, 0);

		uint16_t vendor_id = reg0 & 0xFFFF;
		if (vendor_id == 0xFFFF) {
			// This is kinda the "universal" sign that it isn't there.
			// At the very least, the device is broken and we don't wanna deal with it.
			if (func == 0) return NULL;
			continue;
		}

		uint16_t device_id = (reg0 >> 16) & 0xFFFF;
		// Read Register 0x08: Class (bits 31-24), Subclass (bits 23-16), ProgIF, RevID
		uint32_t reg2 = (entry) ? pci_read_mcfg(entry, bus, device, func, 0x08) : pci_read_legacy(bus, device, func, 0x08);

		uint8_t base_class = (reg2 >> 24) & 0xFF;
		uint8_t sub_class = (reg2 >> 16) & 0xFF;
		uint8_t prog_if = (reg2 >> 8) & 0xFF;

		// Build interface flags
		device_interface_flags_t flags = pci_class_to_interface(base_class, sub_class);

		// Refine USB controller type from ProgIF
		if (base_class == 0x0C && sub_class == 0x03) {
			flags |= pci_usb_progif_to_controller(prog_if);
		}

		// HDA is PCI class 0x04, subclass 0x03
		if (base_class == 0x04 && sub_class == 0x03) {
			flags |= DEV_INT_HDA;
		}

		// const char* class_name = get_pci_class_name(base_class, sub_class);
		const char* class_name = get_pci_class_name_short(base_class, sub_class);
		char* name = alloc_device_name(class_name);

		wallos_device_t* dev = create_device(flags, name);
		if (!dev) {
			printf_serial("[PCI] Failed to allocate device %02x:%02x.%u\r\n", bus, device, func);
			kfree(name);
			continue;
		}

		if (func == 0) first_dev = dev;

		dev->vendor_id = vendor_id;
		dev->device_id = device_id;
		dev->subsystem_id = 0;
		dev->parent = parent;            // was bus_device
		dev->location.pci.bus = bus;
		dev->location.pci.slot = device;
		dev->location.pci.function = func;

		if (parent) {
			dev->next_sibling = parent->first_child;  // was bus_device
			parent->first_child = dev;                // was bus_device
		}

		register_device(dev);

		const char* vendor_name = get_pci_vendor_name(vendor_id);
		const char* device_name = get_pci_device_name(vendor_id, device_id);
		if (!vendor_name) vendor_name = "Unknown Vendor";
		if (!device_name) device_name = "Unknown Device";

		if (!name) name = (char*) class_name;

		printf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, "[%02x:%02x.%u](%04x:%04x) %s, %s\n", bus, device, func, vendor_id, device_id, vendor_name, name);
		// printf_serial("[PCI] Created device: 0x%llx\r\n", dev);
		printf_serial(
			"[PCI] [%02x:%02x.%u](%04x:%04x) %-22s %-30s [%s][%02x:%02x]\r\n",
			bus, device, func,
			vendor_id, device_id,
			vendor_name, device_name,
			class_name,
			base_class, sub_class
		);

		// Check Header Type to see if it's a multi-function device
		uint32_t reg3 = (entry) ? pci_read_mcfg(entry, bus, device, func, 0x0C) : pci_read_legacy(bus, device, func, 0x0C);
		uint8_t header_type = (reg3 >> 16) & 0xFF;

		// Check if this specific device function is a PCI-to-PCI bridge
		if ((header_type & 0x7F) == 0x01) {
			dev->interfaces |= DEV_INT_ALREADY_BOUND;
			uint32_t reg6 = (entry) ? pci_read_mcfg(entry, bus, device, func, 0x18) : pci_read_legacy(bus, device, func, 0x18);
			uint8_t secondary_bus = (reg6 >> 8) & 0xFF;

			// Avoid infinite loops and don't scan back to parent
			if (secondary_bus > bus) {
				// This exact bridge function is now the correct parent for the subordinate bus
				scan_bus(entry, secondary_bus, dev);
			}
		}

		// If not multi-function (bit 7 clear) and we are on func 0, don't check func 1-7
		if (func == 0 && !(header_type & 0x80)) break;
	}

	return first_dev;
}
/**
 * @brief Scans a PCI bus and enumerates all devices and subordinate buses.
 *
 * @param entry Pointer to the MCFG entry describing the ECAM region. If NULL, legacy PCI configuration access is used.
 * @param bus PCI bus number to scan.
 * @param parent Parent device pointer. The root pci bus (0) is simply `pci`.
 */
void scan_bus(MCFGEntry* entry, uint8_t bus, wallos_device_t* parent) {
	for (uint8_t dev = 0; dev < 32; dev++) {
		// Check if device exists at Func 0
		uint32_t reg0 = (entry) ? pci_read_mcfg(entry, bus, dev, 0, 0) : pci_read_legacy(bus, dev, 0, 0);
		if ((reg0 & 0xFFFF) == 0xFFFF) continue;

		// Parse all functions of this device. 
		// check_device will handle recursion if it finds a bridge
		check_device(entry, bus, dev, parent);
	}
}

/**
 * @brief Enumerate the PCI bus to discover connected devices.
 *
 * If the MCFG (PCIe) isn't present, we use legacy I/O ports.
 */
void pci_discover(void) {
	WALLOS_RUN_ONCE();

	MCFGTable* mcfg = get_mcfg();

	// Create a root node so the device tree has something to hang PCI devices from
	wallos_device_t* pci_root = create_device(DEV_INT_PCI | DEV_INT_INTERFACE_ONLY, "pci");
	if (!pci_root) {
		printf_serial("[PCI] Failed to allocate PCI root device\r\n");
		return;
	}
	register_device(pci_root);

	if (mcfg) {
		printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "Enumerating via MCFG (ECAM)...\n");
		for (uint32_t i = 0; i < mcfg->entry_count; i++) {
			scan_bus(&mcfg->entries[i], mcfg->entries[i].start_bus, pci_root);
		}
	} else {  // If NULL, we fallback to "Configuration Method #1" (great naming scheme PCI...)
		printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "MCFG not found. Falling back to Legacy IO Ports...\n");
		scan_bus(NULL, 0, pci_root);
	}
}

/**
 * @brief Finds the MCFG entry covering a given bus, or NULL for legacy fallback.
 */
static MCFGEntry* pci_find_mcfg_entry(uint8_t bus) {
	MCFGTable* mcfg = get_mcfg();
	if (!mcfg) return NULL;

	for (uint32_t i = 0; i < mcfg->entry_count; i++) {
		MCFGEntry* e = &mcfg->entries[i];
		if (bus >= e->start_bus && bus <= e->end_bus)
			return e;
	}
	return NULL;
}

/**
 * @brief Writes a 32-bit value to PCI configuration space using legacy I/O ports.
 */
static void pci_write_legacy(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
	uint32_t address = (uint32_t) ((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
	outl(0xCF8, address);
	outl(0xCFC, value);
}

/**
 * @brief Writes a 32-bit value to PCI configuration space using ECAM (MCFG).
 */
static void pci_write_mcfg(MCFGEntry* entry, uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
	uintptr_t phys_addr = (uintptr_t) entry->base_addr + (((bus - entry->start_bus) << 20) | (slot << 15) | (func << 12) | offset);

	uintptr_t virt_addr = pci_get_or_map_page(phys_addr);
	*(volatile uint32_t*) virt_addr = value;
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
	MCFGEntry* entry = pci_find_mcfg_entry(bus);
	return entry ? pci_read_mcfg(entry, bus, slot, func, offset) : pci_read_legacy(bus, slot, func, offset);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
	uint32_t val = pci_config_read32(bus, slot, func, offset & ~0x3);
	return (val >> ((offset & 2) * 8)) & 0xFFFF;
}

uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
	uint32_t val = pci_config_read32(bus, slot, func, offset & ~0x3);
	return (val >> ((offset & 3) * 8)) & 0xFF;
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
	MCFGEntry* entry = pci_find_mcfg_entry(bus);
	if (entry) pci_write_mcfg(entry, bus, slot, func, offset, value);
	else pci_write_legacy(bus, slot, func, offset, value);
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
	uint32_t shift = (offset & 2) * 8;
	uint32_t mask = ~(0xFFFFu << shift);
	uint32_t old = pci_config_read32(bus, slot, func, offset & ~0x3);
	pci_config_write32(bus, slot, func, offset & ~0x3, (old & mask) | ((uint32_t) value << shift));
}

void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t func,
	uint8_t offset, uint8_t value) {
	uint32_t shift = (offset & 3) * 8;
	uint32_t mask = ~(0xFFu << shift);
	uint32_t old = pci_config_read32(bus, slot, func, offset & ~0x3);
	pci_config_write32(bus, slot, func, offset & ~0x3, (old & mask) | ((uint32_t) value << shift));
}

/**
 * @brief Reads a BAR and returns its base address with the type/IO bits masked off.
 *        Returns 0 if the BAR is unimplemented.
 *
 * @param bar_index  0-5
 * @param is_io_out  Optional; set to 1 if the BAR is I/O space, 0 if memory.
 * @param is_64_out  Optional; set to 1 if this is a 64-bit memory BAR.
 */
uintptr_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func,
	uint8_t bar_index, int* is_io_out, int* is_64_out) {
	if (bar_index > 5) return 0;

	uint8_t  offset = 0x10 + bar_index * 4;
	uint32_t bar = pci_config_read32(bus, slot, func, offset);

	if (bar == 0 || bar == 0xFFFFFFFF) return 0;

	int is_io = bar & 0x1;
	if (is_io_out) *is_io_out = is_io;
	if (is_64_out) *is_64_out = 0;

	if (is_io) {
		return (uintptr_t) (bar & ~0x3u);
	}

	// Memory BAR: bits [2:1] encode the type
	int type = (bar >> 1) & 0x3;
	if (type == 0x2) { // 64-bit BAR
		if (bar_index > 4) return 0; // no room for the upper half

		uint32_t bar_hi = pci_config_read32(bus, slot, func, offset + 4);
		if (is_64_out) *is_64_out = 1;
		return (uintptr_t) ((uint64_t) (bar & ~0xFu) | ((uint64_t) bar_hi << 32));
	}

	// 32-bit memory BAR
	return (uintptr_t) (bar & ~0xFu);
}

/**
 * @brief Reads the size of a BAR by writing all-ones and reading back.
 *        Restores the original value afterward.
 *
 * @param bar_index  0-5
 * @return Size in bytes, or 0 if unimplemented.
 */
size_t pci_bar_size(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index) {
	if (bar_index > 5) return 0;

	uint8_t  offset = 0x10 + bar_index * 4;
	uint32_t original = pci_config_read32(bus, slot, func, offset);

	if (original == 0 || original == 0xFFFFFFFF) return 0;

	pci_config_write32(bus, slot, func, offset, 0xFFFFFFFF);
	uint32_t readback = pci_config_read32(bus, slot, func, offset);
	pci_config_write32(bus, slot, func, offset, original); // restore

	// Mask off the type bits and invert
	// Yes this is ugly, just ignore it
	uint32_t mask = (original & 0x1) ? ~0x3u : ~0xFu;
	readback &= mask;
	if (!readback) return 0;

	return (size_t) (~readback + 1);
}

/**
 * @brief Enables or disables Bus Mastering for a device (needed for DMA).
 */
void pci_set_bus_master(uint8_t bus, uint8_t slot, uint8_t func, int enable) {
	uint16_t cmd = pci_config_read16(bus, slot, func, 0x04);
	if (enable) cmd |= (1 << 2);
	else cmd &= ~(1 << 2);
	pci_config_write16(bus, slot, func, 0x04, cmd);
}

/**
 * @brief Enables or disables Memory Space access for a device.
 */
void pci_set_mem_space(uint8_t bus, uint8_t slot, uint8_t func, int enable) {
	uint16_t cmd = pci_config_read16(bus, slot, func, 0x04);
	if (enable) cmd |= (1 << 1);
	else cmd &= ~(1 << 1);
	pci_config_write16(bus, slot, func, 0x04, cmd);
}

/**
 * @brief Enables or disables I/O Space access for a device.
 */
void pci_set_io_space(uint8_t bus, uint8_t slot, uint8_t func, int enable) {
	uint16_t cmd = pci_config_read16(bus, slot, func, 0x04);
	if (enable) cmd |= (1 << 0);
	else cmd &= ~(1 << 0);
	pci_config_write16(bus, slot, func, 0x04, cmd);
}