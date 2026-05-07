/*-----------------------------------------------------------------------/
/  Low level disk interface modlue include file   (C)ChaN, 2019          /
/-----------------------------------------------------------------------*/

#ifndef _DISKIO_DEFINED
#define _DISKIO_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

/* Status of Disk Functions */
	typedef unsigned char   DSTATUS;

	/* Results of Disk Functions */
	typedef enum {
		RES_OK = 0,     /* 0: Successful */
		RES_ERROR,      /* 1: R/W Error */
		RES_WRPRT,      /* 2: Write Protected */
		RES_NOTRDY,     /* 3: Not Ready */
		RES_PARERR      /* 4: Invalid Parameter */
	} DRESULT;


	/*---------------------------------------*/
	/* Prototypes for disk control functions */

	DSTATUS disk_initialize(BYTE pdrv);
	DSTATUS disk_status(BYTE pdrv);
	DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count);
	DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count);
	DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff);

#include <stdbool.h>

	typedef struct WDM_Drive* WDM_DriveHandle;

	/**
	 * @brief Associate a FatFs physical drive number with a WDM handle.
	 *
	 * Must be called before f_mount() for the corresponding volume.
	 * Calling again on the same pdrv replaces the previous handle.
	 *
	 * @param pdrv   FatFs physical drive number (0 ... FF_VOLUMES-1).
	 * @param handle An initialised, registered WDM drive handle.
	 *
	 * @return true on success, false if pdrv is out of range or handle is NULL.
	 */
	bool ff_register_drive(BYTE pdrv, WDM_DriveHandle handle);

	/**
	 * @brief Deregister a FatFs physical drive number.
	 *
	 * Call after f_unmount() to release the association.
	 *
	 * @param pdrv FatFs physical drive number.
	 */
	void ff_unregister_drive(BYTE pdrv);

/* Disk Status Bits (DSTATUS) */

#define STA_NOINIT      0x01    /* Drive not initialized */
#define STA_NODISK      0x02    /* No medium in the drive */
#define STA_PROTECT     0x04    /* Write protected */


/* Command code for disk_ioctrl functions */

/* Generic command (Used by FatFs) */
#define CTRL_SYNC           0   /* Complete pending write process (needed at _FS_READONLY == 0) */
#define GET_SECTOR_COUNT    1   /* Get media size (needed at _USE_MKFS == 1) */
#define GET_SECTOR_SIZE     2   /* Get sector size (needed at _MAX_SS != _MIN_SS) */
#define GET_BLOCK_SIZE      3   /* Get erase block size (needed at _USE_MKFS == 1) */
#define CTRL_TRIM           4   /* Inform device that the data on the block of sectors is no longer used (needed at _USE_TRIM == 1) */

#ifdef __cplusplus
}
#endif

#endif