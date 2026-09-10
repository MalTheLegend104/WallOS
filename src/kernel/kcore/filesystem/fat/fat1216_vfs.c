#include <filesystem/fat/fat1216_vfs.h>
#include <filesystem/fat/fat.h>
#include <filesystem/fat/fat_internal.h>
#include <stdlib.h>
#include <string.h>

#include <memory/kernel_alloc.h>

#include <filesystem/vfs.h>

static const char* strip_slash(const char* p) {
	return (p && p[0] == '/') ? p + 1 : p;
}

/* WRONG_TYPE here means "found it, but it's a directory, not a file"
 * open_dir() handles its own WRONG_TYPE case separately ("it's a file, not a directory").
 */
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

static int alloc_file_slot(vfs_fat1216_ctx_t* ctx) {
	for (int i = 0; i < VFS_FAT1216_OPEN_MAX; i++)
		if (!ctx->files[i].used) return i;
	return -1;
}

static int alloc_dir_slot(vfs_fat1216_ctx_t* ctx) {
	for (int i = 0; i < VFS_FAT1216_OPEN_MAX; i++)
		if (!ctx->dirs[i].used) return i;
	return -1;
}

static VFS_Status fat1216_vfs_on_mount(WDM_DriveHandle drive, void* fs_ctx) {
	if (!drive || !fs_ctx) return VFS_ERR_INVALID;
	vfs_fat1216_ctx_t* ctx = (vfs_fat1216_ctx_t*) fs_ctx;
	ctx->drive = drive;

	// TODO: This should NOT be hard coded at 512
	uint8_t sector[512];
	WDM_Status status = WDM_Read(drive, 0, 1, sector, WDM_FLAG_NONE);
	if (status != WDM_OK) {
		return VFS_ERR_IO;
	}

	fat_type_t fat_type = get_fat_type(sector);
	if (fat_type != FAT_TYPE_FAT12 && fat_type != FAT_TYPE_FAT16) {
		return VFS_ERR_IO;
	}
	ctx->type = fat_type;

	fill_fat1216(sector, &ctx->ebr);

	return VFS_OK;
}

static void fat1216_vfs_on_unmount(void* fs_ctx) {
	if (!fs_ctx) return;
	vfs_fat1216_ctx_t* ctx = (vfs_fat1216_ctx_t*) fs_ctx;

	for (int i = 0; i < VFS_FAT1216_OPEN_MAX; i++) {
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

	vfs_fat1216_free(ctx);
}

static VFS_Status fat1216_vfs_open_file(void* fs_ctx, const char* path, VFS_OpenFlags flags, VFS_FD* out_fd) {
	if (!fs_ctx || !path || !out_fd) return VFS_ERR_INVALID;
	vfs_fat1216_ctx_t* ctx = (vfs_fat1216_ctx_t*) fs_ctx;
	const char* rel = strip_slash(path);

	// Mount root is a directory, never a file.
	if (rel[0] == '\0') return VFS_ERR_ISDIR;

	// Read-only because I *really* don't want to deal with writing to FAT12/16
	// Anything that has substantial enough storage for use on modern computers can use FAT32
	uint8_t mode = (uint8_t) (flags & 0x03);
	if (mode != VFS_O_RDONLY) return VFS_ERR_UNSUPPORTED;
	if (flags & (VFS_O_CREAT | VFS_O_TRUNC)) return VFS_ERR_UNSUPPORTED;

	int slot = alloc_file_slot(ctx);
	if (slot < 0) return VFS_ERR_FDFULL;

	fat_resolved_dirent_t entry;
	fat_lookup_status_t ls = fat1216_find_file(ctx->drive, &ctx->ebr, ctx->type, rel, &entry);
	if (ls != FAT_LOOKUP_OK) return map_lookup(ls);

	uint32_t size = 0;
	uint8_t* buf = fat1216_read_file(ctx->drive, &ctx->ebr, ctx->type, &entry, &size);
	if (!buf) return VFS_ERR_IO;

	vfs_fat1216_file_t* f = &ctx->files[slot];
	f->entry = entry;
	f->flags = flags;
	f->buf = buf;
	f->size = size;
	f->pos = 0;
	f->used = true;

	*out_fd = (VFS_FD) slot;
	return VFS_OK;
}

static VFS_Status fat1216_vfs_close_file(void* fs_ctx, VFS_FD fd) {
	if (!fs_ctx) return VFS_ERR_INVALID;
	vfs_fat1216_ctx_t* ctx = (vfs_fat1216_ctx_t*) fs_ctx;

	/* Directory fd? */
	if (fd >= VFS_FAT1216_OPEN_MAX && fd < 2 * VFS_FAT1216_OPEN_MAX) {
		int di = fd - VFS_FAT1216_OPEN_MAX;
		if (!ctx->dirs[di].used) return VFS_ERR_BADF;
		fat_dirent_list_free(&ctx->dirs[di].listing);
		ctx->dirs[di].used = false;
		return VFS_OK;
	}

	if (fd < 0 || fd >= VFS_FAT1216_OPEN_MAX) return VFS_ERR_BADF;
	vfs_fat1216_file_t* f = &ctx->files[fd];
	if (!f->used) return VFS_ERR_BADF;

	kfree(f->buf);
	f->buf = NULL;
	f->used = false;
	return VFS_OK;
}

static VFS_Status fat1216_vfs_read_file(void* fs_ctx, VFS_FD fd, void* buf, size_t size, size_t* out_read) {
	if (!fs_ctx || !buf || !out_read) return VFS_ERR_INVALID;
	if (fd < 0 || fd >= VFS_FAT1216_OPEN_MAX) return VFS_ERR_BADF;
	vfs_fat1216_ctx_t* ctx = (vfs_fat1216_ctx_t*) fs_ctx;
	vfs_fat1216_file_t* f = &ctx->files[fd];
	if (!f->used) return VFS_ERR_BADF;

	if (f->pos >= f->size) { *out_read = 0; return VFS_OK; }

	size_t avail = f->size - f->pos;
	size_t to_copy = size < avail ? size : avail;
	memcpy(buf, f->buf + f->pos, to_copy);
	f->pos += (uint32_t) to_copy;
	*out_read = to_copy;
	return VFS_OK;
}

static VFS_Status fat1216_vfs_write_file(void* fs_ctx, VFS_FD fd, const void* buf, size_t size, size_t* out_written) {
	(void) buf; (void) size; (void) out_written;
	if (!fs_ctx) return VFS_ERR_INVALID;
	vfs_fat1216_ctx_t* ctx = (vfs_fat1216_ctx_t*) fs_ctx;
	if (fd < 0 || fd >= VFS_FAT1216_OPEN_MAX || !ctx->files[fd].used) return VFS_ERR_BADF;

	// Read only
	return VFS_ERR_UNSUPPORTED;
}

static VFS_Status fat1216_vfs_make_dir(void* fs_ctx, const char* path) {
	(void) path;
	if (!fs_ctx) return VFS_ERR_INVALID;

	// Read only
	return VFS_ERR_UNSUPPORTED;
}

static VFS_Status fat1216_vfs_remove_dir(void* fs_ctx, const char* path) {
	(void) path;
	if (!fs_ctx) return VFS_ERR_INVALID;

	// Read only
	return VFS_ERR_UNSUPPORTED;
}

static VFS_Status fat1216_vfs_open_dir(void* fs_ctx, const char* path, VFS_FD* out_fd) {
	if (!fs_ctx || !path || !out_fd) return VFS_ERR_INVALID;
	vfs_fat1216_ctx_t* ctx = (vfs_fat1216_ctx_t*) fs_ctx;
	const char* rel = strip_slash(path);

	uint32_t dir_cluster = 0; // 0 == root

	if (rel[0] != '\0') {
		fat_resolved_dirent_t entry;
		fat_lookup_status_t ls = fat1216_find_directory(ctx->drive, &ctx->ebr, ctx->type, rel, &entry);
		if (ls == FAT_LOOKUP_WRONG_TYPE) return VFS_ERR_NOTDIR;
		if (ls != FAT_LOOKUP_OK) return map_lookup(ls);
		dir_cluster = entry.first_cluster;
	}

	int di = alloc_dir_slot(ctx);
	if (di < 0) return VFS_ERR_FDFULL;

	vfs_fat1216_dir_t* d = &ctx->dirs[di];
	fat_dirent_list_init(&d->listing);

	if (!fat1216_list_directory(ctx->drive, &ctx->ebr, ctx->type, dir_cluster, &d->listing)) {
		fat_dirent_list_free(&d->listing);
		return VFS_ERR_IO;
	}

	d->index = 0;
	d->used = true;
	*out_fd = (VFS_FD) (VFS_FAT1216_OPEN_MAX + di);
	return VFS_OK;
}

static VFS_Status fat1216_vfs_read_dir(void* fs_ctx, VFS_FD fd, VFS_DirEnt* out_ent) {
	if (!fs_ctx || !out_ent) return VFS_ERR_INVALID;
	if (fd < VFS_FAT1216_OPEN_MAX || fd >= 2 * VFS_FAT1216_OPEN_MAX) return VFS_ERR_BADF;
	vfs_fat1216_ctx_t* ctx = (vfs_fat1216_ctx_t*) fs_ctx;
	vfs_fat1216_dir_t* d = &ctx->dirs[fd - VFS_FAT1216_OPEN_MAX];
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

void* vfs_fat1216_alloc(void) {
	return kcalloc(1, sizeof(vfs_fat1216_ctx_t));
}

void vfs_fat1216_free(void* ctx) {
	kfree(ctx);
}

const VFS_FSOps vfs_fat1216_ops = {
	.create_context = vfs_fat1216_alloc,
	.destroy_context = vfs_fat1216_free,
	.on_mount = fat1216_vfs_on_mount,
	.on_unmount = fat1216_vfs_on_unmount,
	.open_file = fat1216_vfs_open_file,
	.close_file = fat1216_vfs_close_file,
	.read_file = fat1216_vfs_read_file,
	.write_file = fat1216_vfs_write_file,
	.make_dir = fat1216_vfs_make_dir,
	.remove_dir = fat1216_vfs_remove_dir,
	.open_dir = fat1216_vfs_open_dir,
	.read_dir = fat1216_vfs_read_dir,
};