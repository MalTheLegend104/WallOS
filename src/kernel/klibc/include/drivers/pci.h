#ifndef WALLOS_PCI_H
#define WALLOS_PCI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	void pci_init_discovery();
	void pci_scan_bus(uint16_t seg, uint8_t bus);
	uint32_t pci_config_read32(uint16_t seg, uint8_t bus, uint8_t slot, uint8_t func, uint16_t offset);

#ifdef __cplusplus
}
#endif

#endif // WALLOS_PCI_H