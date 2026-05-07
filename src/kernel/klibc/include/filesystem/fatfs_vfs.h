#ifndef WALLOS_FATFS_VFS_H
#define WALLOS_FATFS_VFS_H

#include <stdint.h>
#include <stdbool.h>

#include "ff.h"
#include "diskio.h"
#include <filesystem/vfs.h>
#include <filesystem/wdm.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FATFS_VFS_MAX_OPEN_FILES
#define FATFS_VFS_MAX_OPEN_FILES VFS_FD_MAX
#endif

	/**
	 * Per-mount context.  Treat as opaque outside this unit.
	 * Allocation and deallocation go through fatfs_vfs_alloc_ctx / fatfs_vfs_free_ctx so that
	 * the implementation can change its layout without breaking callers.
	 */
	typedef struct fatfs_vfs_ctx fatfs_vfs_ctx_t;

	/**
	 * @brief Allocate and initialise a per-mount context.
	 *
	 * Associates @p pdrv with @p wdm_handle via ff_register_drive(), allocates
	 * the FATFS work area, and prepares the open-file table.
	 *
	 * @param wdm_handle  WDM handle for the underlying block device.
	 * @param pdrv        FatFs physical drive number (0 ... FF_VOLUMES-1).
	 *
	 * @return Pointer to the newly allocated context, or NULL on failure.
	 */
	fatfs_vfs_ctx_t* fatfs_vfs_alloc_ctx(WDM_DriveHandle wdm_handle, BYTE pdrv);

	/**
	 * @brief Release a per-mount context previously obtained from fatfs_vfs_alloc_ctx.
	 *
	 * Called automatically by on_unmount().
	 * Only call this directly if fatfs_vfs_alloc_ctx() succeeded but VFS_Mount() was never reached.
	 *
	 * @param ctx  Context to free.
	 */
	void fatfs_vfs_free_ctx(fatfs_vfs_ctx_t* ctx);

	/**
	 * @brief VFS_FSOps vtable for FatFs-backed mounts.
	 *
	 * Pass a pointer to this symbol as the @p ops argument of VFS_Mount().
	 * Its address is stable for the lifetime of the kernel.
	 */
	extern const VFS_FSOps fatfs_vfs_ops;

#ifdef __cplusplus
}
#endif
#endif /* WALLOS_FATFS_VFS_H */