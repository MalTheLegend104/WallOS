#include <filesystem/iso9660/iso9660.h>
#include <filesystem/vfs.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <memory/kernel_alloc.h>

// ------------------------------------------------------------------------------------------------
// Open file/directory descriptor table
//
// ISO 9660 is read-only, so an "open file" just means we have remembered the file's extent LBA, its byte size, and the caller's current read offset.
// Directories work the same way but we track how far through the directory data the readdir cursor has advanced.
// ------------------------------------------------------------------------------------------------

#define ISO_VFS_MAX_FD ISO9660_MAX_OPEN

typedef enum {
	ISO_FD_FREE = 0,
	ISO_FD_FILE,
	ISO_FD_DIR,
} iso_fd_kind_t;

typedef struct {
	iso_fd_kind_t kind;
	uint32_t extent_lba; /**< LBA where the file/dir data begins. */
	uint32_t extent_size; /**< Byte size of the extent. */
	uint32_t offset; /**< Current byte offset within the extent. */
} iso_fd_entry_t;

/* Per-mount state.
 * Stored in a heap-allocated iso_mount_t that we keep in fs_ctx alongside the iso9660_ctx_t.
 */
typedef struct {
	iso9660_ctx_t iso; /**< Low-level ISO 9660 context. */
	iso_fd_entry_t fds[ISO_VFS_MAX_FD]; /**< Per-mount open descriptor table. */
} iso_mount_t;

static iso_fd_entry_t* _alloc_fd(iso_mount_t* m, VFS_FD* out_fd) {
	for (int i = 0; i < ISO_VFS_MAX_FD; i++) {
		if (m->fds[i].kind == ISO_FD_FREE) {
			*out_fd = (VFS_FD) i;
			return &m->fds[i];
		}
	}
	return NULL;
}

static iso_fd_entry_t* _get_fd(iso_mount_t* m, VFS_FD fd) {
	if (fd < 0 || fd >= ISO_VFS_MAX_FD) return NULL;
	if (m->fds[fd].kind == ISO_FD_FREE) return NULL;
	return &m->fds[fd];
}

static VFS_Status iso_on_mount(WDM_DriveHandle drive, void* fs_ctx) {
	iso_mount_t* m = (iso_mount_t*) fs_ctx;
	memset(m->fds, 0, sizeof(m->fds));

	if (!iso9660_init(&m->iso, drive)) {
		printf("[ISO9660_VFS] on_mount: failed to read PVD\n");
		return VFS_ERR_IO;
	}
	return VFS_OK;
}

static void iso_on_unmount(void* fs_ctx) {
	iso_mount_t* m = (iso_mount_t*) fs_ctx;
	iso9660_destroy(&m->iso);
}

static VFS_Status iso_open_file(void* fs_ctx, const char* path, VFS_OpenFlags flags, VFS_FD* out_fd) {
	iso_mount_t* m = (iso_mount_t*) fs_ctx;

	/* ISO 9660 is read-only */
	if (flags & (VFS_O_WRONLY | VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC))
		return VFS_ERR_UNSUPPORTED;

	iso9660_directory_record_t rec;
	if (!iso9660_get_file(&m->iso, path, &rec))
		return VFS_ERR_NOENT;

	if (rec.file_flags.directory) {
		kfree(rec.file_id);
		return VFS_ERR_ISDIR;
	}

	VFS_FD fd;
	iso_fd_entry_t* entry = _alloc_fd(m, &fd);
	if (!entry) {
		kfree(rec.file_id);
		return VFS_ERR_FDFULL;
	}

	entry->kind = ISO_FD_FILE;
	entry->extent_lba = rec.location_of_extent;
	entry->extent_size = rec.data_length;
	entry->offset = 0;

	kfree(rec.file_id);
	*out_fd = fd;
	return VFS_OK;
}

static VFS_Status iso_close_file(void* fs_ctx, VFS_FD fd) {
	iso_mount_t* m = (iso_mount_t*) fs_ctx;
	iso_fd_entry_t* entry = _get_fd(m, fd);
	if (!entry) return VFS_ERR_BADF;
	entry->kind = ISO_FD_FREE;
	return VFS_OK;
}

/*
 * ISO 9660 sectors are 2048 bytes.
 * We read one sector at a time so we never need a large stack buffer, at the cost of extra WDM calls for reads that straddle sector boundaries.
 */

static VFS_Status iso_read_file(void* fs_ctx, VFS_FD fd, void* buf, size_t size, size_t* out_read) {
	iso_mount_t* m = (iso_mount_t*) fs_ctx;
	iso_fd_entry_t* entry = _get_fd(m, fd);

	if (!entry || entry->kind != ISO_FD_FILE) return VFS_ERR_BADF;
	if (!buf || !out_read)                    return VFS_ERR_INVALID;

	*out_read = 0;

	/* Clamp to remaining file data */
	if (entry->offset >= entry->extent_size) return VFS_OK; /* EOF */
	uint32_t remaining = entry->extent_size - entry->offset;
	if (size > remaining) size = remaining;
	if (size == 0) return VFS_OK;

	uint8_t sector_buf[ISO_SECTOR_SIZE];
	uint8_t* dst = (uint8_t*) buf;
	size_t   to_go = size;

	while (to_go > 0) {
		uint32_t sector_index = entry->offset / ISO_SECTOR_SIZE;
		uint32_t sector_off = entry->offset % ISO_SECTOR_SIZE;
		uint32_t can_read = ISO_SECTOR_SIZE - sector_off;
		if (can_read > to_go) can_read = (uint32_t) to_go;

		WDM_Status s = WDM_Read(m->iso.drive, (WDM_LBA) (entry->extent_lba + sector_index), 1, sector_buf, WDM_FLAG_NONE);
		if (s != WDM_OK) return VFS_ERR_IO;

		memcpy(dst, sector_buf + sector_off, can_read);
		dst += can_read;
		entry->offset += can_read;
		to_go -= can_read;
	}

	*out_read = size;
	return VFS_OK;
}

static VFS_Status iso_write_file(void* fs_ctx, VFS_FD fd, const void* buf, size_t size, size_t* out_written) {
	(void) fs_ctx; (void) fd; (void) buf; (void) size; (void) out_written;
	return VFS_ERR_UNSUPPORTED; /* ISO 9660 is read-only */
}

static VFS_Status iso_make_dir(void* fs_ctx, const char* path) {
	(void) fs_ctx; (void) path;
	return VFS_ERR_UNSUPPORTED;
}

static VFS_Status iso_remove_dir(void* fs_ctx, const char* path) {
	(void) fs_ctx; (void) path;
	return VFS_ERR_UNSUPPORTED;
}

static VFS_Status iso_open_dir(void* fs_ctx, const char* path, VFS_FD* out_fd) {
	iso_mount_t* m = (iso_mount_t*) fs_ctx;

	iso9660_directory_record_t rec;
	if (!iso9660_get_file(&m->iso, path, &rec))
		return VFS_ERR_NOENT;

	if (!rec.file_flags.directory) {
		kfree(rec.file_id);
		return VFS_ERR_NOTDIR;
	}

	VFS_FD fd;
	iso_fd_entry_t* entry = _alloc_fd(m, &fd);
	if (!entry) {
		kfree(rec.file_id);
		return VFS_ERR_FDFULL;
	}

	entry->kind = ISO_FD_DIR;
	entry->extent_lba = rec.location_of_extent;
	entry->extent_size = rec.data_length;
	entry->offset = 0;

	kfree(rec.file_id);
	*out_fd = fd;
	return VFS_OK;
}

static VFS_Status iso_read_dir(void* fs_ctx, VFS_FD fd, VFS_DirEnt* out_ent) {
	iso_mount_t* m = (iso_mount_t*) fs_ctx;
	iso_fd_entry_t* entry = _get_fd(m, fd);

	if (!entry || entry->kind != ISO_FD_DIR) return VFS_ERR_BADF;
	if (!out_ent)                            return VFS_ERR_INVALID;

	/* End-of-directory sentinel */
	if (entry->offset >= entry->extent_size) {
		out_ent->name[0] = '\0';
		return VFS_OK;
	}

	/* Read sector(s) on demand and walk to the next non-zero record.
	 * To keep stack usage bounded we read one sector at a time.
	 */

	uint8_t sector_buf[ISO_SECTOR_SIZE];
	while (true) {
		if (entry->offset >= entry->extent_size) {
			out_ent->name[0] = '\0';
			return VFS_OK;
		}

		uint32_t sector_index = entry->offset / ISO_SECTOR_SIZE;
		uint32_t sector_off = entry->offset % ISO_SECTOR_SIZE;

		WDM_Status s = WDM_Read(m->iso.drive, (WDM_LBA) (entry->extent_lba + sector_index), 1, sector_buf, WDM_FLAG_NONE);
		if (s != WDM_OK) return VFS_ERR_IO;

		uint8_t rec_len = sector_buf[sector_off];
		if (rec_len == 0) {
			// Padding
			// advance to next sector boundary
			entry->offset = (sector_index + 1) * ISO_SECTOR_SIZE;
			continue;
		}

		iso9660_directory_record_t rec;
		iso9660_read_directory_record(&rec, &sector_buf[sector_off]);
		entry->offset += rec_len;

		// Skip '.' (0x00) and '..' (0x01) entries
		bool is_special = (rec.file_id_len == 1 && ((uint8_t) rec.file_id[0] == 0x00 || (uint8_t) rec.file_id[0] == 0x01));
		if (is_special) {
			kfree(rec.file_id);
			continue;
		}

		// Strip ";N" version suffix for the name
		char* name = rec.file_id;
		size_t name_len = strlen(name);
		if (name_len >= 2 && name[name_len - 2] == ';') name[name_len - 2] = '\0';

		strncpy(out_ent->name, name, VFS_PATH_MAX - 1);
		out_ent->name[VFS_PATH_MAX - 1] = '\0';
		out_ent->is_directory = rec.file_flags.directory;
		out_ent->size = rec.data_length;

		kfree(rec.file_id);
		return VFS_OK;
	}
}

static bool iso_probe(WDM_DriveHandle drive) {
	return is_iso9660(drive);
}

static void* iso_create_context() {
	iso_mount_t* ctx = (iso_mount_t*) kalloc(sizeof(iso_mount_t));
	if (!ctx) return NULL;
	memset(ctx, 0, sizeof(*ctx));
	return ctx;
}

static void iso_destroy_context(void* ctx) {
	if (ctx) kfree(ctx);
}

/* Public Vtable */
const VFS_FSOps iso9660_vfs_ops = {
	.create_context = iso_create_context,
	.destroy_context = iso_destroy_context,
	.probe = iso_probe,
	.on_mount = iso_on_mount,
	.on_unmount = iso_on_unmount,
	.open_file = iso_open_file,
	.close_file = iso_close_file,
	.read_file = iso_read_file,
	.write_file = iso_write_file,
	.make_dir = iso_make_dir,
	.remove_dir = iso_remove_dir,
	.open_dir = iso_open_dir,
	.read_dir = iso_read_dir,
};