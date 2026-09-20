#ifndef WALLOS_ATAPI_H
#define WALLOS_ATAPI_H

#include <stdint.h>
#include <filesystem/wdm.h>
#include <drivers/sata/ahci.h>

#ifdef __cplusplus
extern "C" {
#endif

	/**
	 * @brief Identify an ATAPI device and probe basic capacity.
	 *
	 * Runs IDENTIFY PACKET DEVICE, then uses SCSI TEST UNIT READY and READ CAPACITY(10) to populate model, serial, and sector info.
	 * No media is treated as empty (sector_count = 0, sector_size = 2048).
	 *
	 * @param port AHCI port containing the ATAPI device
	 * @param slot_count Available command slots
	 * @return 0 on success, -1 if the device does not respond
	 */
	int atapi_identify(ahci_port_t* port, uint32_t slot_count);

	/**
	 * @brief Register an ATAPI port with WDM as a read-only, removable block device.
	 *
	 * @param port AHCI port to register (must have already been through atapi_identify())
	 * @param slot_count Command slots available on the parent controller
	 * @return WDM handle on success, NULL on failure
	 */
	WDM_DriveHandle atapi_wdm_register_port(ahci_port_t* port, uint32_t slot_count);

#ifdef __cplusplus
}
#endif
#endif // WALLOS_ATAPI_H