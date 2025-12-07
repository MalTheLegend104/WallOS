/*******************************************************************************************
 * This is a VERY poor implementation of this for PIO mode SATA drives.
 * It is only intended as a temporary solution.
 * It will be rewritten when there is a proper SATA driver is implemented in the kernel.
 *******************************************************************************************/

#include "ff.h"
#include "diskio.h"
#include <drivers/sata/pio.h>
#include <system/ktime.h>

// Access to our drive info from pio.cpp
extern drive_info_t drive_zero;
extern drive_info_t drive_one;
extern drive_info_t drive_two;
extern drive_info_t drive_three;

// Helper to get drive info pointer based on pdrv
static drive_info_t* get_drive_info_ptr(BYTE pdrv) {
	switch (pdrv) {
		case 0: return &drive_zero;
		case 1: return &drive_one;
		case 2: return &drive_two;
		case 3: return &drive_three;
		default: return NULL;
	}
}

DSTATUS disk_status(BYTE pdrv) {
	drive_info_t* drive = get_drive_info_ptr(pdrv);

	if (!drive) return STA_NOINIT;       // Invalid drive number
	if (!drive->exists) return STA_NODISK; // Drive not connected

	return 0;
}

DSTATUS disk_initialize(BYTE pdrv) {
	drive_info_t* drive = get_drive_info_ptr(pdrv);

	if (!drive) return STA_NOINIT;

	// If not detected yet, try one last identification
	if (!drive->exists) {
		drive->exists = identify(pdrv);
	}

	return drive->exists ? 0 : STA_NODISK;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
	drive_info_t* drive = get_drive_info_ptr(pdrv);
	if (!drive || !drive->exists) return RES_NOTRDY;

	// FatFs might request more sectors than PIO can handle in one go (255 limit on uint8_t).
	// The driver takes uint8_t for sector_count.
	// We must loop if count > 255.
	// This would be easy to fix, but I don't really feel like it and this will all have to be rewritten for an actual SATA driver later on anyway.

	UINT remaining = count;
	LBA_t current_lba = sector;
	BYTE* current_buff = buff;

	while (remaining > 0) {
		uint8_t chunk = (remaining > 255) ? 255 : (uint8_t) remaining;

		// Call your driver
		if (!sata_pio_read28(pdrv, (uint32_t) current_lba, chunk, current_buff)) {
			return RES_ERROR;
		}

		remaining -= chunk;
		current_lba += chunk;
		current_buff += (chunk * 512); // Advance buffer by 512 bytes per sector
	}

	return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
	drive_info_t* drive = get_drive_info_ptr(pdrv);
	if (!drive || !drive->exists) return RES_NOTRDY;

	UINT remaining = count;
	LBA_t current_lba = sector;
	const BYTE* current_buff = buff;

	while (remaining > 0) {
		uint8_t chunk = (remaining > 255) ? 255 : (uint8_t) remaining;

		if (!sata_pio_write28(pdrv, (uint32_t) current_lba, chunk, current_buff)) {
			return RES_ERROR;
		}

		remaining -= chunk;
		current_lba += chunk;
		current_buff += (chunk * 512);
	}

	return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
	drive_info_t* drive = get_drive_info_ptr(pdrv);
	if (!drive || !drive->exists) return RES_NOTRDY;

	switch (cmd) {
		case CTRL_SYNC:
			// The write function already calls COMMAND_FLUSH_CACHE,
			// This should probably be separate in the future.
			// For now, return OK.
			return RES_OK;

		case GET_SECTOR_COUNT:
			// Note: FatFs expects LBA_t (DWORD/QWORD) pointer in buff
			*(LBA_t*) buff = get_capacity_bytes(&(drive->identify)) / 512;
			return RES_OK;

		case GET_SECTOR_SIZE:
			// Standard ATA sector size
			*(WORD*) buff = 512;
			return RES_OK;

		case GET_BLOCK_SIZE:
			// Erase block size in units of sectors (mostly for flash memory)
			// For Hard Drives, typically 1 is fine or optimized for cluster size.
			*(DWORD*) buff = 1;
			return RES_OK;
	}

	return RES_PARERR;
}

DWORD get_fattime(void) {
	return get_system_msdos_time();
}