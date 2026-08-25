
#include <filesystem/fat/fat32_vfs.h>
#include <filesystem/fat/fat_internal.h>
#include <stdlib.h>
#include <string.h>

#include <memory/kernel_alloc.h>

#include <filesystem/vfs.h>

static const char* strip_slash(const char* p) {
	return (p && p[0] == '/') ? p + 1 : p;
}

static VFS_Status map_lookup(fat_lookup_status_t s) {
	switch (s) {
		case FAT_LOOKUP_OK:         return VFS_OK;
		case FAT_LOOKUP_NOT_FOUND:  return VFS_ERR_NOENT;
		case FAT_LOOKUP_WRONG_TYPE: return VFS_ERR_ISDIR;
		case FAT_LOOKUP_IO_ERROR:   return VFS_ERR_IO;
		case FAT_LOOKUP_BAD_PATH:   return VFS_ERR_INVALID;
		default:                    return VFS_ERR_IO;
	}
}

static int alloc_file_slot(vfs_fat32_ctx_t* ctx) {
	for (int i = 0; i < VFS_FAT32_OPEN_MAX; i++)
		if (!ctx->files[i].used) return i;
	return -1;
}

static int alloc_dir_slot(vfs_fat32_ctx_t* ctx) {
	for (int i = 0; i < VFS_FAT32_OPEN_MAX; i++)
		if (!ctx->dirs[i].used) return i;
	return -1;
}

/**
 * Derive the parent directory cluster for a relative path.
 * "FOO/BAR/baz.txt" is cluster of FOO/BAR.
 * "baz.txt" is root cluster.
 */
static uint32_t parent_cluster_of(vfs_fat32_ctx_t* ctx, const char* rel_path) {
	/* Find last '/'. */
	const char* last_sep = strrchr(rel_path, '/');
	if (!last_sep) {
		// File is directly under the mount root.
		return ctx->ebr.root_cluster_number;
	}

	/* Copy the parent portion and resolve it. */
	size_t parent_len = (size_t) (last_sep - rel_path);
	char parent_path[VFS_PATH_MAX];
	if (parent_len >= sizeof(parent_path)) return 0;
	memcpy(parent_path, rel_path, parent_len);
	parent_path[parent_len] = '\0';

	fat_resolved_dirent_t dir_entry;
	fat_lookup_status_t ls = fat32_find_directory(ctx->drive, &ctx->ebr, parent_path, &dir_entry);
	if (ls != FAT_LOOKUP_OK) return 0;
	return dir_entry.first_cluster;
}

/**
 * Build a packed 32-byte dirent from a resolved entry with an updated size.
 * Preserves the original timestamps and attributes.
 */
static void pack_updated_dirent(const fat_resolved_dirent_t* entry, uint32_t new_size, uint8_t  out_raw32[32]) {
	// Start from the raw on-disk copy stored in the resolved entry.
	const fat_dirent_t* d = &entry->raw;

	// Repack manually into little-endian layout (per spec).
	memset(out_raw32, 0, 32);
	memcpy(out_raw32, d->name, 11); /* name[0..10] */
	out_raw32[11] = d->attributes; /* attributes */
	out_raw32[12] = d->nt_reserved;
	out_raw32[13] = d->creation_time_tenths;
	fat_internal_write16(out_raw32, 14, d->creation_time);
	fat_internal_write16(out_raw32, 16, d->creation_date);
	fat_internal_write16(out_raw32, 18, d->last_access_date);
	fat_internal_write16(out_raw32, 20, (uint16_t) (entry->first_cluster >> 16));
	fat_internal_write16(out_raw32, 22, d->write_time);
	fat_internal_write16(out_raw32, 24, d->write_date);
	fat_internal_write16(out_raw32, 26, (uint16_t) (entry->first_cluster & 0xFFFF));
	fat_internal_write32(out_raw32, 28, new_size);
}

#include <stdio.h>
static VFS_Status fat32_vfs_on_mount(WDM_DriveHandle drive, void* fs_ctx) {
	if (!drive || !fs_ctx) return VFS_ERR_INVALID;
	vfs_fat32_ctx_t* ctx = (vfs_fat32_ctx_t*) fs_ctx;
	ctx->drive = drive;

	// TODO: This should NOT be hard coded at 512
	uint8_t sector[512];
	WDM_Status status = WDM_Read(drive, 0, 1, sector, WDM_FLAG_NONE);
	if (status != WDM_OK) {
		// printf("STATUS: %d\n", status);
		return VFS_ERR_IO;
	}
	fat_type_t fat_type = get_fat_type(sector);
	if (fat_type != FAT_TYPE_FAT32) {
		// printf("FAT TYPE: %d\n", fat_type);
		return VFS_ERR_IO;
	}

	fill_fat32(sector, &ctx->ebr);

	ctx->fsinfo_valid = false;
	uint16_t fsi = ctx->ebr.fsinfo_sector;
	if (fsi != 0 && fsi != 0xFFFF) {
		uint8_t fsi_buf[512];
		if (WDM_Read(drive, fsi, 1, fsi_buf, WDM_FLAG_NONE) == WDM_OK) {
			fill_fat32_fsinfo(fsi_buf, &ctx->fsinfo);
			if (ctx->fsinfo.signature == 0x41615252 && ctx->fsinfo.signature2 == 0x61417272 && ctx->fsinfo.trail_signature == 0xAA550000) {
				ctx->fsinfo_valid = true;
			}
		}
	}

	return VFS_OK;
}

static void fat32_vfs_on_unmount(void* fs_ctx) {
	if (!fs_ctx) return;
	vfs_fat32_ctx_t* ctx = (vfs_fat32_ctx_t*) fs_ctx;

	for (int i = 0; i < VFS_FAT32_OPEN_MAX; i++) {
		if (ctx->files[i].used) {
			kfree(ctx->files[i].buf);
			ctx->files[i].buf = NULL;
			ctx->files[i].used = false;
		}
		if (ctx->dirs[i].used) {
			fat_dirent_list_free(&ctx->dirs[i].listing);
			ctx->dirs[i].used = false;
		}
	}

	vfs_fat32_free(ctx);
}

static VFS_Status fat32_vfs_open_file(void* fs_ctx, const char* path, VFS_OpenFlags flags, VFS_FD* out_fd) {
	if (!fs_ctx || !path || !out_fd) return VFS_ERR_INVALID;
	vfs_fat32_ctx_t* ctx = (vfs_fat32_ctx_t*) fs_ctx;
	const char* rel = strip_slash(path);

	// Mount root is a directory, never a file. 
	if (rel[0] == '\0') return VFS_ERR_ISDIR;

	bool writable = (flags & (VFS_O_WRONLY | VFS_O_RDWR)) != 0;
	bool create = (flags & VFS_O_CREAT) != 0;
	bool excl = (flags & VFS_O_EXCL) != 0;
	bool truncate = (flags & VFS_O_TRUNC) != 0;

	int slot = alloc_file_slot(ctx);
	if (slot < 0) return VFS_ERR_FDFULL;

	fat_resolved_dirent_t entry;
	fat_lookup_status_t   ls = fat32_find_file(ctx->drive, &ctx->ebr, rel, &entry);

	if (ls == FAT_LOOKUP_WRONG_TYPE) return VFS_ERR_ISDIR;

	if (ls == FAT_LOOKUP_NOT_FOUND) {
		if (!create) return VFS_ERR_NOENT;
		fat_resolved_dirent_t new_entry;
		fat_lookup_status_t ws = fat32_write_file(ctx->drive, &ctx->ebr, ctx->fsinfo_valid ? &ctx->fsinfo : NULL, rel, NULL, 0, &new_entry);
		if (ws != FAT_LOOKUP_OK) return map_lookup(ws);
		entry = new_entry;
	} else if (ls != FAT_LOOKUP_OK) {
		return map_lookup(ls);
	} else if (create && excl) {
		return VFS_ERR_EXIST;
	}

	/* Determine in-memory content. */
	uint8_t* buf = NULL;
	uint32_t size = 0;

	if (truncate && writable) {
		// Zero the file on disk
		fat_resolved_dirent_t updated;
		fat_lookup_status_t ws = fat32_write_file(ctx->drive, &ctx->ebr, ctx->fsinfo_valid ? &ctx->fsinfo : NULL, rel, NULL, 0, &updated);
		if (ws != FAT_LOOKUP_OK) return map_lookup(ws);
		entry = updated;
		buf = (uint8_t*) kalloc(1); /* 1-byte sentinel. Size stays 0 */
		if (!buf) return VFS_ERR_IO;
	} else {
		// Load existing contents.
		buf = fat32_read_file(ctx->drive, &ctx->ebr, &entry, &size);
		if (!buf && size != 0) return VFS_ERR_IO;
		if (!buf) {
			buf = (uint8_t*) kalloc(1);
			if (!buf) return VFS_ERR_IO;
		}
	}

	vfs_fat32_file_t* f = &ctx->files[slot];
	f->entry = entry;
	f->parent_cluster = parent_cluster_of(ctx, rel);
	f->flags = flags;
	f->buf = buf;
	f->size = size;
	f->pos = (flags & VFS_O_APPEND) ? size : 0;
	f->used = true;

	*out_fd = (VFS_FD) slot;
	return VFS_OK;
}

static VFS_Status fat32_vfs_close_file(void* fs_ctx, VFS_FD fd) {
	if (!fs_ctx) return VFS_ERR_INVALID;
	vfs_fat32_ctx_t* ctx = (vfs_fat32_ctx_t*) fs_ctx;

	/* Directory fd? */
	if (fd >= VFS_FAT32_OPEN_MAX && fd < 2 * VFS_FAT32_OPEN_MAX) {
		int di = fd - VFS_FAT32_OPEN_MAX;
		if (!ctx->dirs[di].used) return VFS_ERR_BADF;
		fat_dirent_list_free(&ctx->dirs[di].listing);
		ctx->dirs[di].used = false;
		return VFS_OK;
	}

	if (fd < 0 || fd >= VFS_FAT32_OPEN_MAX) return VFS_ERR_BADF;
	vfs_fat32_file_t* f = &ctx->files[fd];
	if (!f->used) return VFS_ERR_BADF;

	// There is no need to flush, we update on calls to write.
	kfree(f->buf);
	f->buf = NULL;
	f->used = false;
	return VFS_OK;
}

static VFS_Status fat32_vfs_read_file(void* fs_ctx, VFS_FD fd, void* buf, size_t size, size_t* out_read) {
	// Yes this function looks awful. I wrote it at 3am over a month ago...
	if (!fs_ctx || !buf || !out_read) return VFS_ERR_INVALID;
	if (fd < 0 || fd >= VFS_FAT32_OPEN_MAX) return VFS_ERR_BADF;
	vfs_fat32_ctx_t* ctx = (vfs_fat32_ctx_t*) fs_ctx;
	vfs_fat32_file_t* f = &ctx->files[fd];
	if (!f->used) return VFS_ERR_BADF;
	if (f->flags == VFS_O_WRONLY) return VFS_ERR_BADF;

	if (f->pos >= f->size) { *out_read = 0; return VFS_OK; }

	size_t avail = f->size - f->pos;
	size_t to_copy = size < avail ? size : avail;
	memcpy(buf, f->buf + f->pos, to_copy);
	f->pos += (uint32_t) to_copy;
	*out_read = to_copy;
	return VFS_OK;
}

static VFS_Status fat32_vfs_write_file(void* fs_ctx, VFS_FD fd, const void* buf, size_t size, size_t* out_written) {
	if (!fs_ctx || !buf || !out_written) return VFS_ERR_INVALID;
	if (fd < 0 || fd >= VFS_FAT32_OPEN_MAX) return VFS_ERR_BADF;
	vfs_fat32_ctx_t* ctx = (vfs_fat32_ctx_t*) fs_ctx;
	vfs_fat32_file_t* f = &ctx->files[fd];
	if (!f->used) return VFS_ERR_BADF;
	if (f->flags == VFS_O_RDONLY) return VFS_ERR_BADF;

	if (f->flags & VFS_O_APPEND) f->pos = f->size;

	uint32_t new_end = f->pos + (uint32_t) size;

	// Grow in-memory buffer if the write extends past the current size.
	// We don't have realloc (and really realloc would just be this in wrapper)
	if (new_end > f->size) {
		uint8_t* nb = (uint8_t*) kalloc(new_end);
		if (!nb) return VFS_ERR_NOSPACE;

		// If there's an existing buffer, copy its contents and free it
		if (f->buf) {
			memcpy(nb, f->buf, f->size);
			kfree(f->buf);
		}

		f->buf = nb;
		f->size = new_end;
	}

	memcpy(f->buf + f->pos, buf, size);
	f->pos += (uint32_t) size;

	// Write the cluster chain with the full updated buffer.
	uint32_t new_first_cluster = f->entry.first_cluster;
	bool ok = fat32_write_file_data(ctx->drive, &ctx->ebr, ctx->fsinfo_valid ? &ctx->fsinfo : NULL, f->entry.first_cluster, f->buf, f->size, &new_first_cluster);
	if (!ok) return VFS_ERR_IO;

	// Update cached first_cluster in case the chain was reallocated.
	f->entry.first_cluster = new_first_cluster;

	// Patch the directory entry's size (and first_cluster) in place.
	uint8_t raw32[32];
	pack_updated_dirent(&f->entry, f->size, raw32);

	if (!fat32_patch_dirent(ctx->drive, &ctx->ebr, f->parent_cluster, f->entry.raw.name, raw32)) {
		return VFS_ERR_IO;
	}

	*out_written = size;
	return VFS_OK;
}

static VFS_Status fat32_vfs_make_dir(void* fs_ctx, const char* path) {
	if (!fs_ctx || !path) return VFS_ERR_INVALID;
	vfs_fat32_ctx_t* ctx = (vfs_fat32_ctx_t*) fs_ctx;
	const char* rel = strip_slash(path);
	if (!rel || rel[0] == '\0') return VFS_ERR_INVALID;

	fat32_fsinfo_t* fsinfo = ctx->fsinfo_valid ? &ctx->fsinfo : NULL;

	// Verify the target does not already exist.
	fat_resolved_dirent_t existing;
	fat_lookup_status_t ls = fat32_resolve_path(ctx->drive, &ctx->ebr, rel, false, &existing);
	if (ls == FAT_LOOKUP_OK || ls == FAT_LOOKUP_WRONG_TYPE) return VFS_ERR_EXIST;
	if (ls != FAT_LOOKUP_NOT_FOUND) return map_lookup(ls);

	uint32_t par_cluster = parent_cluster_of(ctx, rel);
	if (par_cluster < 2) return VFS_ERR_NOENT;

	// Validate the final component is a legal 8.3 name.
	const char* name_start = strrchr(rel, '/');
	name_start = name_start ? name_start + 1 : rel;
	if (!name_start || name_start[0] == '\0') return VFS_ERR_INVALID;

	uint32_t dir_cluster = 0;
	if (!fat32_write_directory(ctx->drive, &ctx->ebr, fsinfo, par_cluster, &dir_cluster)) return VFS_ERR_NOSPACE;

	if (!fat32_add_dirent_chain(ctx->drive, &ctx->ebr, fsinfo, par_cluster, name_start, FAT_ATTR_DIRECTORY, dir_cluster, 0, NULL)) {
		fat32_free_cluster_chain(ctx->drive, &ctx->ebr, fsinfo, dir_cluster);
		return VFS_ERR_IO;
	}

	return VFS_OK;
}

static VFS_Status fat32_vfs_remove_dir(void* fs_ctx, const char* path) {
	if (!fs_ctx || !path) return VFS_ERR_INVALID;
	vfs_fat32_ctx_t* ctx = (vfs_fat32_ctx_t*) fs_ctx;
	const char* rel = strip_slash(path);
	if (!rel || rel[0] == '\0') return VFS_ERR_INVALID;

	// Resolve the target
	// must be a directory.
	fat_resolved_dirent_t entry;
	fat_lookup_status_t ls = fat32_find_directory(ctx->drive, &ctx->ebr, rel, &entry);
	if (ls == FAT_LOOKUP_WRONG_TYPE) return VFS_ERR_NOTDIR;
	if (ls != FAT_LOOKUP_OK) return map_lookup(ls);

	// List the directory and check it is empty (ignoring '.' and '..').
	fat_dirent_list_t listing;
	fat_dirent_list_init(&listing);
	if (!fat32_list_directory(ctx->drive, &ctx->ebr, entry.first_cluster, &listing)) return VFS_ERR_IO;

	bool non_empty = false;
	for (size_t i = 0; i < listing.count; i++) {
		if (!fat_is_dot_entry(&listing.entries[i])) { non_empty = true; break; }
	}
	fat_dirent_list_free(&listing);

	if (non_empty) return VFS_ERR_NOTEMPTY;

	fat32_fsinfo_t* fsinfo = ctx->fsinfo_valid ? &ctx->fsinfo : NULL;
	if (!fat32_free_cluster_chain(ctx->drive, &ctx->ebr, fsinfo, entry.first_cluster)) return VFS_ERR_IO;

	// Unlink the directory and its LFN run from the parent.
	uint32_t par_cluster = parent_cluster_of(ctx, rel);
	if (!fat32_unlink_entry(ctx->drive, &ctx->ebr, par_cluster, entry.raw.name)) {
		return VFS_ERR_IO;
	}

	return VFS_OK;
}

static VFS_Status fat32_vfs_open_dir(void* fs_ctx, const char* path, VFS_FD* out_fd) {
	if (!fs_ctx || !path || !out_fd) return VFS_ERR_INVALID;
	vfs_fat32_ctx_t* ctx = (vfs_fat32_ctx_t*) fs_ctx;
	const char* rel = strip_slash(path);

	uint32_t dir_cluster = ctx->ebr.root_cluster_number;

	if (rel[0] != '\0') {
		fat_resolved_dirent_t entry;
		fat_lookup_status_t ls = fat32_find_directory(ctx->drive, &ctx->ebr, rel, &entry);
		if (ls == FAT_LOOKUP_WRONG_TYPE) return VFS_ERR_NOTDIR;
		if (ls != FAT_LOOKUP_OK) return map_lookup(ls);
		dir_cluster = entry.first_cluster;
	}

	int di = alloc_dir_slot(ctx);
	if (di < 0) return VFS_ERR_FDFULL;

	vfs_fat32_dir_t* d = &ctx->dirs[di];
	fat_dirent_list_init(&d->listing);

	if (!fat32_list_directory(ctx->drive, &ctx->ebr, dir_cluster, &d->listing)) {
		fat_dirent_list_free(&d->listing);
		return VFS_ERR_IO;
	}

	d->index = 0;
	d->used = true;
	*out_fd = (VFS_FD) (VFS_FAT32_OPEN_MAX + di);
	return VFS_OK;
}

static VFS_Status fat32_vfs_read_dir(void* fs_ctx, VFS_FD fd, VFS_DirEnt* out_ent) {
	if (!fs_ctx || !out_ent) return VFS_ERR_INVALID;
	if (fd < VFS_FAT32_OPEN_MAX || fd >= 2 * VFS_FAT32_OPEN_MAX) return VFS_ERR_BADF;
	vfs_fat32_ctx_t* ctx = (vfs_fat32_ctx_t*) fs_ctx;
	vfs_fat32_dir_t* d = &ctx->dirs[fd - VFS_FAT32_OPEN_MAX];
	if (!d->used) return VFS_ERR_BADF;

	while (d->index < d->listing.count) {
		const fat_resolved_dirent_t* e = &d->listing.entries[d->index++];
		if (fat_is_dot_entry(e)) continue;

		const char* name = (e->long_name[0] != '\0') ? e->long_name : e->short_name;
		strncpy(out_ent->name, name, VFS_PATH_MAX - 1);
		out_ent->name[VFS_PATH_MAX - 1] = '\0';
		out_ent->is_directory = (e->raw.attributes & FAT_ATTR_DIRECTORY) != 0;
		out_ent->size = out_ent->is_directory ? 0 : e->raw.file_size;
		return VFS_OK;
	}

	out_ent->name[0] = '\0';
	out_ent->is_directory = false;
	out_ent->size = 0;
	return VFS_OK;
}

const VFS_FSOps vfs_fat32_ops = {
	.on_mount = fat32_vfs_on_mount,
	.on_unmount = fat32_vfs_on_unmount,
	.open_file = fat32_vfs_open_file,
	.close_file = fat32_vfs_close_file,
	.read_file = fat32_vfs_read_file,
	.write_file = fat32_vfs_write_file,
	.make_dir = fat32_vfs_make_dir,
	.remove_dir = fat32_vfs_remove_dir,
	.open_dir = fat32_vfs_open_dir,
	.read_dir = fat32_vfs_read_dir,
};


vfs_fat32_ctx_t* vfs_fat32_alloc(void) {
	return (vfs_fat32_ctx_t*) kcalloc(1, sizeof(vfs_fat32_ctx_t));
}

void vfs_fat32_free(vfs_fat32_ctx_t* ctx) {
	kfree(ctx);
}