#include "ff.h"
#include "diskio.h"

#include <string.h>
#include <system/timer.h>
#include <filesystem/wdm.h>

#define FF_MAX_DRIVES FF_VOLUMES

static WDM_DriveHandle fatfs_drives[FF_MAX_DRIVES];
static bool            fatfs_registered[FF_MAX_DRIVES];

#include <system/timer.h>
#include <stdio.h>

// MS-DOS date/time packing, as used by FAT directory entries.
// fdate: bits 9-15 = Year-1980, bits 5-8 = Month, bits 0-4 = Day
// ftime: bits 11-15 = Hour, bits 5-10 = Minute, bits 0-4 = Second/2
// I kinda reused logic from elsewhere, this shoudl really use endian_bits
uint32_t fat_pack_time(const wall_time_t* t) {
	uint16_t fdate = (uint16_t) ((((t->year - 1980) & 0x7F) << 9) | ((t->month & 0x0F) << 5) | (t->day & 0x1F));
	uint16_t ftime = (uint16_t) (((t->hour & 0x1F) << 11) | ((t->minute & 0x3F) << 5) | ((t->second / 2) & 0x1F));
	return ((uint32_t) fdate << 16) | ftime;
}

// This really isn't used
void fat_print_time(uint32_t fdate, uint32_t ftime) {
	uint16_t year = (uint16_t) ((fdate >> 9) + 1980);
	uint8_t  month = (uint8_t) ((fdate >> 5) & 0x0F);
	uint8_t  day = (uint8_t) (fdate & 0x1F);
	uint8_t  hour = (uint8_t) ((ftime >> 11) & 0x1F);
	uint8_t  minute = (uint8_t) ((ftime >> 5) & 0x3F);
	uint8_t  second = (uint8_t) ((ftime & 0x1F) * 2);

	printf("%04u-%02u-%02u %02u:%02u:%02u", year, month, day, hour, minute, second);
}

bool ff_register_drive(BYTE pdrv, WDM_DriveHandle handle) {
	if (pdrv >= FF_MAX_DRIVES || !handle) return false;
	fatfs_drives[pdrv] = handle;
	fatfs_registered[pdrv] = true;
	return true;
}

void ff_unregister_drive(BYTE pdrv) {
	if (pdrv >= FF_MAX_DRIVES) return;
	fatfs_drives[pdrv] = NULL;
	fatfs_registered[pdrv] = false;
}

static inline WDM_DriveHandle get_handle(BYTE pdrv) {
	if (pdrv >= FF_MAX_DRIVES || !fatfs_registered[pdrv]) return NULL;
	return fatfs_drives[pdrv];
}

/* Map a WDM_Status to the closest DRESULT equivalent. */
DRESULT wdm_to_dresult(WDM_Status st) {
	switch (st) {
		case WDM_OK:             return RES_OK;
		case WDM_ERR_INVALID:    return RES_PARERR;
		case WDM_ERR_NO_MEDIA:   return RES_NOTRDY;
		case WDM_ERR_WRITE_PROT: return RES_WRPRT;
		default:                 return RES_ERROR;
	}
}

DSTATUS disk_status(BYTE pdrv) {
	WDM_DriveHandle h = get_handle(pdrv);
	if (!h) return STA_NOINIT;

	WDM_DriveInfo info;
	if (WDM_GetInfo(h, &info) != WDM_OK) return STA_NOINIT;
	if (info.read_only)                  return STA_PROTECT;

	return 0;
}

DSTATUS disk_initialize(BYTE pdrv) {
	/* WDM drivers are fully initialised during WDM_Register() / on_attach().
	 * Nothing extra to do here, just confirm the handle is valid.
	 */
	WDM_DriveHandle h = get_handle(pdrv);
	if (!h) return STA_NOINIT;

	WDM_DriveInfo info;
	if (WDM_GetInfo(h, &info) != WDM_OK) return STA_NOINIT;

	return info.read_only ? STA_PROTECT : 0;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
	WDM_DriveHandle h = get_handle(pdrv);
	if (!h) return RES_NOTRDY;

	WDM_Status st = WDM_Read(h, (WDM_LBA) sector, (uint32_t) count,
		buff, WDM_FLAG_NONE);
	return wdm_to_dresult(st);
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
	WDM_DriveHandle h = get_handle(pdrv);
	if (!h) return RES_NOTRDY;

	WDM_Status st = WDM_Write(h, (WDM_LBA) sector, (uint32_t) count, buff, WDM_FLAG_NONE);
	return wdm_to_dresult(st);
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
	WDM_DriveHandle h = get_handle(pdrv);
	if (!h) return RES_NOTRDY;

	WDM_DriveInfo info;

	switch (cmd) {
		case CTRL_SYNC: {
				WDM_Status st = WDM_Flush(h);
				return wdm_to_dresult(st);
			}

		case GET_SECTOR_COUNT:
			if (WDM_GetInfo(h, &info) != WDM_OK) return RES_ERROR;
			*(LBA_t*) buff = (LBA_t) info.sector_count;
			return RES_OK;

		case GET_SECTOR_SIZE:
			if (WDM_GetInfo(h, &info) != WDM_OK) return RES_ERROR;
			*(WORD*) buff = (WORD) info.sector_size;
			return RES_OK;

		case GET_BLOCK_SIZE:
			// Report optimal_xfer if the driver exposes it, otherwise fall back to 1 
			if (WDM_GetInfo(h, &info) != WDM_OK) return RES_ERROR;
			*(DWORD*) buff = info.optimal_xfer ? info.optimal_xfer : 1;
			return RES_OK;

		default: return RES_PARERR;
	}
}

DWORD get_fattime(void) {
	wall_time_t now;
	uint32_t packed = wallclock_read(&now) ? fat_pack_time(&now) : 0; // yes this is a cursed way to read the time and pack it if it exists
	return packed;
}

#include <memory/kernel_alloc.h>
void* ff_memalloc(UINT msize) { return kalloc((size_t) msize); }
void  ff_memfree(void* mblock) { kfree(mblock); }