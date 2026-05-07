#include <filesystem/fatfs_vfs.h>

#include "ff.h"
#include "diskio.h"
#include <filesystem/vfs.h>
#include <filesystem/wdm.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <memory/kernel_alloc.h>

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Internal types
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/** One slot in the open-file table. */
typedef struct {
	FIL  fil;       /**< FatFs file object (valid when @p in_use is true). */
	bool in_use;    /**< Slot is occupied.                                 */
} fatfs_file_slot_t;

/** One slot in the open-directory table. */
typedef struct {
	DIR   dir;      /**< FatFs directory object. */
	bool in_use;
} fatfs_dir_slot_t;

/** Per-mount driver context. */
struct fatfs_vfs_ctx {
	FATFS              fs;                                      /**< FatFs work area for this volume.        */
	WDM_DriveHandle    wdm_handle;                              /**< Underlying block device.                */
	BYTE               pdrv;                                    /**< FatFs physical drive number.            */
	fatfs_file_slot_t  files[FATFS_VFS_MAX_OPEN_FILES];         /**< Open-file table.                        */
	fatfs_dir_slot_t   dirs[FATFS_VFS_MAX_OPEN_FILES];
	char               drive_prefix[4];                         /**< "N:" string, cached on alloc.           */
};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Translations
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

VFS_Status fr_to_vfs(FRESULT fr) {
	switch (fr) {
		case FR_OK:                  return VFS_OK;
		case FR_NO_FILE:             /* fall-through */
		case FR_NO_PATH:             return VFS_ERR_NOENT;
		case FR_EXIST:               return VFS_ERR_EXIST;
		case FR_WRITE_PROTECTED:     return VFS_ERR_IO;          /* no write-prot code in VFS */
		case FR_DENIED:              return VFS_ERR_IO;
		// case FR_IS_DIRECTORY:        return VFS_ERR_ISDIR;
		// case FR_NOT_DIRECTORY:       return VFS_ERR_NOTDIR;
		// case FR_DIR_NOT_EMPTY:       return VFS_ERR_NOTEMPTY;
		case FR_NOT_ENOUGH_CORE:     return VFS_ERR_IO;
		case FR_INVALID_NAME:        /* fall-through */
		case FR_INVALID_PARAMETER:   return VFS_ERR_INVALID;
		case FR_DISK_ERR:            /* fall-through */
		case FR_INT_ERR:             /* fall-through */
		case FR_NOT_READY:           /* fall-through */
		case FR_NO_FILESYSTEM:       /* fall-through */
		default:                     return VFS_ERR_IO;
	}
}

BYTE vfs_flags_to_fatfs(VFS_OpenFlags flags) {
	BYTE mode = 0;

	bool rd = !(flags & VFS_O_WRONLY);
	bool wr = (flags & VFS_O_WRONLY) || (flags & VFS_O_RDWR);

	if (rd && !wr) mode |= FA_READ;
	if (wr && !rd) mode |= FA_WRITE;
	if (rd && wr)  mode |= FA_READ | FA_WRITE;

	if (flags & VFS_O_CREAT) {
		if (flags & VFS_O_EXCL)
			mode |= FA_CREATE_NEW;            /* fail if exists   */
		else if (flags & VFS_O_TRUNC)
			mode |= FA_CREATE_ALWAYS;         /* create/truncate  */
		else
			mode |= FA_OPEN_ALWAYS;           /* create if absent */
	} else {
		mode |= FA_OPEN_EXISTING;             /* must already exist */
	}

	return mode;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Path Helpers
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/* Build the FatFs path "N:relative/path" into @p out (which must be at least VFS_PATH_MAX bytes).
 * @p vfs_path begins with '/', which we skip.
 *
 * @return false if the resulting string would exceed VFS_PATH_MAX-1 chars.
 */
bool fatfs_make_path(const fatfs_vfs_ctx_t* ctx, const char* vfs_path, char* out) {
	// drive_prefix is "N:" (2 chars + null).
	size_t prefix_len = strlen(ctx->drive_prefix);
	// Skip the leading '/' from the VFS-supplied path.
	const char* rel = (*vfs_path == '/') ? vfs_path + 1 : vfs_path;
	size_t rel_len = strlen(rel);

	if (prefix_len + rel_len >= VFS_PATH_MAX) return false;

	memcpy(out, ctx->drive_prefix, prefix_len);
	memcpy(out + prefix_len, rel, rel_len + 1); /* include null */
	return true;
}

/** Allocate a free slot. Returns index or -1. */
int alloc_slot(fatfs_vfs_ctx_t* ctx) {
	for (int i = 0; i < FATFS_VFS_MAX_OPEN_FILES; i++) {
		if (!ctx->files[i].in_use) {
			ctx->files[i].in_use = true;
			return i;
		}
	}
	return -1;
}

/** Validate a slot index and return its FIL pointer, or NULL on bad fd. */
FIL* get_fil(fatfs_vfs_ctx_t* ctx, VFS_FD fd) {
	if (fd < 0 || fd >= FATFS_VFS_MAX_OPEN_FILES) return NULL;
	if (!ctx->files[fd].in_use)                    return NULL;
	return &ctx->files[fd].fil;
}

/** Release a slot. */
void free_slot(fatfs_vfs_ctx_t* ctx, VFS_FD fd) {
	if (fd >= 0 && fd < FATFS_VFS_MAX_OPEN_FILES) ctx->files[fd].in_use = false;
}

int alloc_dir_slot(fatfs_vfs_ctx_t* ctx) {
	for (int i = 0; i < FATFS_VFS_MAX_OPEN_FILES; i++) {
		if (!ctx->dirs[i].in_use) {
			ctx->dirs[i].in_use = true;
			return i;
		}
	}
	return -1;
}

DIR* get_dir(fatfs_vfs_ctx_t* ctx, VFS_FD fd) {
	if (fd < 0 || fd >= FATFS_VFS_MAX_OPEN_FILES) return NULL;
	if (!ctx->dirs[fd].in_use) return NULL;
	return &ctx->dirs[fd].dir;
}

void free_dir_slot(fatfs_vfs_ctx_t* ctx, VFS_FD fd) {
	if (fd >= 0 && fd < FATFS_VFS_MAX_OPEN_FILES) ctx->dirs[fd].in_use = false;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// VFS Ops
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

VFS_Status fatfs_on_mount(WDM_DriveHandle drive, void* fs_ctx) {
	fatfs_vfs_ctx_t* ctx = (fatfs_vfs_ctx_t*) fs_ctx;

	/* Sanity check: the context must already have been wired up by fatfs_vfs_alloc_ctx()
	 * The drive passed by the VFS must match what was recorded there.
	 */
	if (!ctx || ctx->wdm_handle != drive) return VFS_ERR_INVALID;

	/* Register the WDM handle with the diskio layer so FatFs can reach it.
	 * ff_register_drive() is idempotent on re-registration of the same slot,
	 * so calling it here (in addition to fatfs_vfs_alloc_ctx) is safe.
	 */
	if (!ff_register_drive(ctx->pdrv, drive)) return VFS_ERR_IO;

	// Mount the FatFs volume. 1 = mount immediately, not deferred.
	FRESULT fr = f_mount(&ctx->fs, ctx->drive_prefix, 1);
	return fr_to_vfs(fr);
}

void fatfs_on_unmount(void* fs_ctx) {
	fatfs_vfs_ctx_t* ctx = (fatfs_vfs_ctx_t*) fs_ctx;
	if (!ctx) return;

	//Passing NULL as the FATFS pointer unmounts the volume from the FatFs internal table and flushes cached metadata.

	f_unmount(ctx->drive_prefix);

	ff_unregister_drive(ctx->pdrv);

	// The VFS guarantees all fds are closed before calling on_unmount, but zero the table defensively anyway.
	memset(ctx->files, 0, sizeof(ctx->files));
}

VFS_Status fatfs_open_file(void* fs_ctx, const char* path, VFS_OpenFlags flags, VFS_FD* out_fd) {
	fatfs_vfs_ctx_t* ctx = (fatfs_vfs_ctx_t*) fs_ctx;
	if (!ctx || !path || !out_fd) return VFS_ERR_INVALID;

	char fatfs_path[VFS_PATH_MAX];
	if (!fatfs_make_path(ctx, path, fatfs_path)) return VFS_ERR_TOOLONG;

	int slot = alloc_slot(ctx);
	if (slot < 0) return VFS_ERR_FDFULL;

	BYTE mode = vfs_flags_to_fatfs(flags);
	FRESULT fr = f_open(&ctx->files[slot].fil, fatfs_path, mode);
	if (fr != FR_OK) {
		free_slot(ctx, slot);
		return fr_to_vfs(fr);
	}

	/* To handle VFS_O_APPEND, we seek to end so the first write lands there.
	 * Subsequent writes will also go to EOF because we re-seek in fatfs_write_file when the flag is active.
	 * Since we don't store the original open flags here we only do the initial seek.
	 * FatFs does not have a native append mode so the caller is responsible for not mixing seek + write when O_APPEND is set.
	 */
	if (flags & VFS_O_APPEND) {
		fr = f_lseek(&ctx->files[slot].fil, f_size(&ctx->files[slot].fil));
		if (fr != FR_OK) {
			f_close(&ctx->files[slot].fil);
			free_slot(ctx, slot);
			return fr_to_vfs(fr);
		}
	}

	*out_fd = (VFS_FD) slot;
	return VFS_OK;
}

VFS_Status fatfs_close_file(void* fs_ctx, VFS_FD fd) {
	fatfs_vfs_ctx_t* ctx = (fatfs_vfs_ctx_t*) fs_ctx;
	if (!ctx) return VFS_ERR_INVALID;

	// Try to find it in the file table 
	FIL* fil = get_fil(ctx, fd);
	if (fil) {
		FRESULT fr = f_close(fil);
		free_slot(ctx, fd);
		return fr_to_vfs(fr);
	}

	// If not a file, try to find it in the directory table 
	DIR* dir = get_dir(ctx, fd);
	if (dir) {
		FRESULT fr = f_closedir(dir);
		free_dir_slot(ctx, fd);
		return fr_to_vfs(fr);
	}

	return VFS_ERR_BADF;
}

VFS_Status fatfs_read_file(void* fs_ctx, VFS_FD  fd, void* buf, size_t size, size_t* out_read) {
	fatfs_vfs_ctx_t* ctx = (fatfs_vfs_ctx_t*) fs_ctx;
	if (!buf || !out_read) return VFS_ERR_INVALID;

	FIL* fil = get_fil(ctx, fd);
	if (!fil) return VFS_ERR_BADF;

	UINT bytes_read = 0;
	FRESULT fr = f_read(fil, buf, (UINT) size, &bytes_read);
	if (fr != FR_OK) return fr_to_vfs(fr);

	*out_read = (size_t) bytes_read;
	return VFS_OK;
}

VFS_Status fatfs_write_file(void* fs_ctx, VFS_FD fd, const void* buf, size_t size, size_t* out_written) {
	fatfs_vfs_ctx_t* ctx = (fatfs_vfs_ctx_t*) fs_ctx;
	if (!buf || !out_written) return VFS_ERR_INVALID;

	FIL* fil = get_fil(ctx, fd);
	if (!fil) return VFS_ERR_BADF;

	UINT bytes_written = 0;
	FRESULT fr = f_write(fil, buf, (UINT) size, &bytes_written);
	if (fr != FR_OK) return fr_to_vfs(fr);

	// FR_OK with bytes_written < size is how FatFs signals a full disk.
	if (bytes_written < (UINT) size) return VFS_ERR_NOSPACE;

	*out_written = (size_t) bytes_written;
	return VFS_OK;
}

VFS_Status fatfs_make_dir(void* fs_ctx, const char* path) {
	fatfs_vfs_ctx_t* ctx = (fatfs_vfs_ctx_t*) fs_ctx;
	if (!ctx || !path) return VFS_ERR_INVALID;

	char fatfs_path[VFS_PATH_MAX];
	if (!fatfs_make_path(ctx, path, fatfs_path)) return VFS_ERR_TOOLONG;

	FRESULT fr = f_mkdir(fatfs_path);
	return fr_to_vfs(fr);
}

VFS_Status fatfs_remove_dir(void* fs_ctx, const char* path) {
	fatfs_vfs_ctx_t* ctx = (fatfs_vfs_ctx_t*) fs_ctx;
	if (!ctx || !path) return VFS_ERR_INVALID;

	char fatfs_path[VFS_PATH_MAX];
	if (!fatfs_make_path(ctx, path, fatfs_path)) return VFS_ERR_TOOLONG;

	/*
	 * FatFs f_unlink() can remove both files and empty directories.
	 * The VFS guarantees the caller only passes directories here,
	 * and FatFs returns FR_DIR_NOT_EMPTY when appropriate,
	 * which maps to VFS_ERR_NOTEMPTY as required.
	 */
	FRESULT fr = f_unlink(fatfs_path);
	return fr_to_vfs(fr);
}

VFS_Status fatfs_open_dir(void* fs_ctx, const char* path, VFS_FD* out_fd) {
	fatfs_vfs_ctx_t* ctx = (fatfs_vfs_ctx_t*) fs_ctx;
	if (!ctx || !path || !out_fd) return VFS_ERR_INVALID;

	char fatfs_path[VFS_PATH_MAX];
	if (!fatfs_make_path(ctx, path, fatfs_path)) return VFS_ERR_TOOLONG;

	int slot = alloc_dir_slot(ctx);
	if (slot < 0) return VFS_ERR_FDFULL;

	FRESULT fr = f_opendir(&ctx->dirs[slot].dir, fatfs_path);
	if (fr != FR_OK) {
		free_dir_slot(ctx, slot);
		return fr_to_vfs(fr);
	}

	*out_fd = (VFS_FD) slot;
	return VFS_OK;
}

VFS_Status fatfs_read_dir(void* fs_ctx, VFS_FD fd, VFS_DirEnt* out_ent) {
	fatfs_vfs_ctx_t* ctx = (fatfs_vfs_ctx_t*) fs_ctx;
	if (!out_ent) return VFS_ERR_INVALID;

	DIR* dir = get_dir(ctx, fd);
	if (!dir) return VFS_ERR_BADF;

	FILINFO fno;
	FRESULT fr = f_readdir(dir, &fno);
	if (fr != FR_OK) return fr_to_vfs(fr);

	// FatFs sets fno.fname[0] to 0 when the end of the directory is reached.
	if (fno.fname[0] == 0) {
		out_ent->name[0] = '\0';
		return VFS_OK;
	}

	// Map FatFs metadata to VFS entry
	strncpy(out_ent->name, fno.fname, VFS_PATH_MAX);
	out_ent->is_directory = (fno.fattrib & AM_DIR) ? true : false;
	out_ent->size = (size_t) fno.fsize;

	return VFS_OK;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Operations Table
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

const VFS_FSOps fatfs_vfs_ops = {
	.on_mount = fatfs_on_mount,
	.on_unmount = fatfs_on_unmount,
	.open_file = fatfs_open_file,
	.close_file = fatfs_close_file, // If you merged close logic
	.read_file = fatfs_read_file,
	.write_file = fatfs_write_file,
	.make_dir = fatfs_make_dir,
	.remove_dir = fatfs_remove_dir,
	.open_dir = fatfs_open_dir,
	.read_dir = fatfs_read_dir,
};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Context
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

fatfs_vfs_ctx_t* fatfs_vfs_alloc_ctx(WDM_DriveHandle wdm_handle, BYTE pdrv) {
	if (!wdm_handle || pdrv >= FF_VOLUMES) return NULL;

	fatfs_vfs_ctx_t* ctx = (fatfs_vfs_ctx_t*) kalloc(sizeof(*ctx));
	if (!ctx) return NULL;

	memset(ctx, 0, sizeof(*ctx));
	ctx->wdm_handle = wdm_handle;
	ctx->pdrv = pdrv;

	// Build "N:" prefix string once.
	ctx->drive_prefix[0] = (char) ('0' + pdrv);
	ctx->drive_prefix[1] = ':';
	ctx->drive_prefix[2] = '\0';

	memset(ctx->files, 0, sizeof(ctx->files));
	memset(ctx->dirs, 0, sizeof(ctx->dirs));

	// Wire the diskio layer so FatFs can see this drive.
	if (!ff_register_drive(pdrv, wdm_handle)) {
		kfree(ctx);
		return NULL;
	}

	return ctx;
}

void fatfs_vfs_free_ctx(fatfs_vfs_ctx_t* ctx) {
	if (!ctx) return;
	kfree(ctx);
}