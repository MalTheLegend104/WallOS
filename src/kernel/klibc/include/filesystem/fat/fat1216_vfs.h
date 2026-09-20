#ifndef WALLOS_VFS_FAT1216_H
#define WALLOS_VFS_FAT1216_H

#include <filesystem/vfs.h>
#include <filesystem/fat/fat1216.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef VFS_FAT1216_OPEN_MAX
/** Maximum simultaneously open files *and* directories (each). */
#  define VFS_FAT1216_OPEN_MAX 32
#endif

	/* Per file state. Read-only, so no write cursor bookkeeping beyond pos. */
	typedef struct {
		bool used;
		VFS_OpenFlags flags;
		fat_resolved_dirent_t entry; //< Resolved dirent at open time.
		uint8_t* buf; //< In-memory file contents (malloc'd).
		uint32_t size; //< File size in bytes.
		uint32_t pos; //< Read cursor.
	} vfs_fat1216_file_t;

	/* Per-open-directory state */
	typedef struct {
		bool used;
		fat_dirent_list_t listing; //< Snapshot of the directory taken at opendir.
		size_t index; //< Next entry to return from read_dir.
	} vfs_fat1216_dir_t;

	/* Driver context */
	typedef struct {
		WDM_DriveHandle drive;
		fat1216_ebr_t ebr; //< Parsed boot sector / EBR.
		fat_type_t type; //< FAT_TYPE_FAT12 or FAT_TYPE_FAT16.

		vfs_fat1216_file_t files[VFS_FAT1216_OPEN_MAX];
		vfs_fat1216_dir_t dirs[VFS_FAT1216_OPEN_MAX];
	} vfs_fat1216_ctx_t;

	/**
	 * @brief Heap-allocate and zero-init a fresh driver context.
	 * @return Pointer on success, NULL on OOM.
	 */
	void* vfs_fat1216_alloc(void);

	/**
	 * @brief Free a driver context that is no longer mounted.
	 */
	void vfs_fat1216_free(void* ctx);

	extern const VFS_FSOps vfs_fat1216_ops;

#ifdef __cplusplus
}
#endif
#endif /* WALLOS_VFS_FAT1216_H */