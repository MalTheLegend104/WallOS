#include <acpi.h>
#include <stdint.h>
#include <cpu_io.h>
#include <memory/virtual_mem.h>
#include <stdio.h>

ACPI_TABLE_MCFG* mcfg = NULL;

/**
 * Class and Subclass lookup
 */
const char* pci_get_class_info(uint8_t base_class, uint8_t sub_class) {
	switch (base_class) {
		case 0x01: // Mass Storage Controller
			switch (sub_class) {
				case 0x00: return "SCSI Bus Controller";
				case 0x01: return "IDE Controller";
				case 0x02: return "Floppy Disk Controller";
				case 0x05: return "ATA Controller";
				case 0x06: return "SATA Controller (AHCI)";
				case 0x07: return "Serial Attached SCSI";
				case 0x08: return "Non-Volatile Memory (NVMe)";
				default:   return "Mass Storage Controller";
			}
		case 0x02: // Network Controller
			switch (sub_class) {
				case 0x00: return "Ethernet Controller";
				case 0x01: return "Token Ring Controller";
				case 0x02: return "FDDI Controller";
				case 0x03: return "ATM Controller";
				case 0x04: return "ISDN Controller";
				case 0x80: return "Network Controller (Other)";
				default:   return "Network Controller";
			}
		case 0x03: // Display Controller
			switch (sub_class) {
				case 0x00: return "VGA Compatible Controller";
				default:   return "Display Controller";
			}
		case 0x04: // Multimedia Controller
			switch (sub_class) {
				case 0x00: return "Multimedia Video Controller";
				case 0x01: return "Multimedia Audio Controller";
				case 0x03: return "High Definition Audio Controller";
				default:   return "Multimedia Controller";
			}
		case 0x06: // Bridges
			switch (sub_class) {
				case 0x00: return "Host Bridge";
				case 0x01: return "ISA Bridge";
				case 0x02: return "EISA Bridge";
				case 0x03: return "MCA Bridge";
				case 0x04: return "PCI-to-PCI Bridge";
				case 0x05: return "PCMCIA Bridge";
				case 0x06: return "NuBus Bridge";
				case 0x07: return "CardBus Bridge";
				case 0x08: return "RACEway Bridge";
				case 0x09: return "PCI-to-PCI Bridge (Semi-Transparent)";
				case 0x0A: return "InfiniBand-to-PCI Host Bridge";
				default:   return "Bridge Device";
			}
		case 0x07: // Communication Controller
			switch (sub_class) {
				case 0x00: return "Serial Controller (UART)";
				case 0x01: return "Parallel Port";
				default:   return "Communication Controller";
			}
		case 0x0C: // Serial Bus Controller
			switch (sub_class) {
				case 0x00: return "FireWire (IEEE 1394)";
				case 0x03: return "USB Controller";
				case 0x05: return "SMBus";
				default:   return "Serial Bus Controller";
			}
		default: return "Unknown Device Type";
	}
}

/**
 * Read PCI Configuration Space
 * Handles both ECAM (MMIO) and Legacy (Port 0xCF8/0xCFC)
 */
uint32_t pci_config_read32(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset) {
	if (mcfg) {
		// Find the correct MCFG allocation for this segment and bus
		int entry_count = (mcfg->Header.Length - sizeof(ACPI_TABLE_MCFG)) / sizeof(ACPI_MCFG_ALLOCATION);
		ACPI_MCFG_ALLOCATION* allocs = (ACPI_MCFG_ALLOCATION*) (mcfg + 1);

		for (int i = 0; i < entry_count; i++) {
			if (allocs[i].PciSegment == seg &&
				bus >= allocs[i].StartBusNumber &&
				bus <= allocs[i].EndBusNumber) {

				// Calculate ECAM address
				// Address = Base + ((Bus - Start) << 20 | Slot << 15 | Func << 12 | Offset)
				uintptr_t phys_addr = (uintptr_t) allocs[i].Address +
					(((uintptr_t) bus - allocs[i].StartBusNumber) << 20) |
					((uintptr_t) slot << 15) |
					((uintptr_t) func << 12) |
					(offset & 0xFFF);

				// Map and read
				volatile uint32_t* virt_addr = (volatile uint32_t*) mapKernelLocation(phys_addr, 4);
				return *virt_addr;
			}
		}
	}

	// Legacy PCI Configuration Mechanism #1
	uint32_t address = (uint32_t) ((1U << 31) | ((uint32_t) bus << 16) |
		((uint32_t) slot << 11) | ((uint32_t) func << 8) | (offset & 0xFC));
	outl(0xCF8, address);
	return inl(0xCFC);
}

/**
 * Recursive Bus Scan
 */
void pci_scan_bus(uint16_t seg, uint8_t bus) {
	for (uint8_t slot = 0; slot < 32; slot++) {
		uint32_t id_reg = pci_config_read32(seg, bus, slot, 0, 0x00);
		if ((id_reg & 0xFFFF) == 0xFFFF) continue;

		for (uint8_t func = 0; func < 8; func++) {
			uint32_t dev_id_reg = pci_config_read32(seg, bus, slot, func, 0x00);
			if ((dev_id_reg & 0xFFFF) == 0xFFFF) continue;

			uint32_t class_reg = pci_config_read32(seg, bus, slot, func, 0x08);
			uint32_t header_reg = pci_config_read32(seg, bus, slot, func, 0x0C);

			uint8_t base_class = (class_reg >> 24) & 0xFF;
			uint8_t sub_class = (class_reg >> 16) & 0xFF;
			uint8_t header_type = (header_reg >> 16) & 0xFF;

			// Updated Log with the new class info function
			printf("[%04x:%02x:%02x.%d] ID %04x:%04x | %s\n",
				seg, bus, slot, func,
				(uint16_t) (dev_id_reg & 0xFFFF), (uint16_t) (dev_id_reg >> 16),
				pci_get_class_info(base_class, sub_class));

			// Is this a PCI-to-PCI Bridge?
			if (base_class == 0x06 && sub_class == 0x04) {
				uint32_t bus_reg = pci_config_read32(seg, bus, slot, func, 0x18);
				uint8_t secondary_bus = (bus_reg >> 8) & 0xFF;
				pci_scan_bus(seg, secondary_bus);
			}

			if (func == 0 && !(header_type & 0x80)) break;
		}
	}
}

/**
 * 3. Entry Point
 */
void pci_init_discovery() {
	ACPI_STATUS status = AcpiGetTable((char*) "MCFG", 1, (ACPI_TABLE_HEADER**) &mcfg);

	if (ACPI_SUCCESS(status)) {
		int entry_count = (mcfg->Header.Length - sizeof(ACPI_TABLE_MCFG)) / sizeof(ACPI_MCFG_ALLOCATION);
		ACPI_MCFG_ALLOCATION* allocs = (ACPI_MCFG_ALLOCATION*) (mcfg + 1);

		for (int i = 0; i < entry_count; i++) {
			pci_scan_bus(allocs[i].PciSegment, allocs[i].StartBusNumber);
		}
	} else {
		pci_scan_bus(0, 0);
	}
}