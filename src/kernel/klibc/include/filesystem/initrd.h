#ifndef WALLOS_INITRD_WDM_H
#define WALLOS_INITRD_WDM_H

#include "wdm.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Flags for initrd_wdm_init().
 */
	typedef enum {
		INITRD_FLAG_NONE = 0x00, /**< Read-write (writes go to RAM, no persistence). */
		INITRD_FLAG_READ_ONLY = 0x01, /**< Reject all writes at the WDM layer.            */
	} initrd_flags_t;

	/**
	 * @brief Register the initrd blob as a WDM block device.
	 *
	 * Must be called after WDM_Init() and before any attempt to mount the initrd
	 * as a FatFs volume.  Reads the blob address and size from the linker symbols
	 * _initrd_data and _initrd_size.
	 *
	 * The returned handle is suitable for passing directly to mount_drive() or
	 * ff_register_drive().
	 *
	 * Example (in kmain or equivalent):
	 * @code
	 *   WDM_Init();
	 *   WDM_DriveHandle initrd = initrd_wdm_init(INITRD_FLAG_NONE);
	 *   if (!initrd) kpanic("initrd: WDM registration failed");
	 *   mount_drive(0, initrd);   // bind to FatFs pdrv 0
	 * @endcode
	 *
	 * @param flags  INITRD_FLAG_NONE for read-write, INITRD_FLAG_READ_ONLY to
	 *               prevent all writes.
	 *
	 * @return A live WDM_DriveHandle on success, or NULL if the blob is missing,
	 *         misaligned, or WDM_Register() fails.
	 */
	WDM_DriveHandle initrd_wdm_init(initrd_flags_t flags);

#ifdef __cplusplus
}
#endif
#endif /* WALLOS_INITRD_WDM_H */