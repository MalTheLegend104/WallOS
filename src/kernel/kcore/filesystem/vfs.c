/**
 * Wallos VFS (Virtual File System).
 * TODO: Spinlocks should be on table accesses
 */
#include <filesystem/vfs.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Internal structures
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/** One entry in the global mount table. */
typedef struct {
	bool             active;
	char             path[VFS_PATH_MAX]; /**< Absolute mount point, e.g. "/mnt/usb" */
	size_t           path_len;           /**< strlen(path), cached                  */
	WDM_DriveHandle  drive;
	const VFS_FSOps* ops;
	void* fs_ctx;
	uint32_t         mount_seq;          /**< Insertion order, for reverse unmount  */
	uint32_t         open_count;         /**< Number of live FDs on this mount      */
} VFS_MountEntry;

/** One entry in the global FD table. */
typedef struct {
	bool           active;
	VFS_MountEntry* mount;   /**< Back-pointer to the owning mount      */
	VFS_FD         drv_fd;   /**< The fd value returned by the driver   */
	VFS_OpenFlags  flags;    /**< Flags this descriptor was opened with */
} VFS_FDEntry;

static VFS_MountEntry vfs_mounts[VFS_MOUNT_MAX];
static VFS_FDEntry    vfs_fds[VFS_FD_MAX];
static bool           wdm_initialized = false;
static uint32_t       vfs_mount_seq = 0;

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Internal helpers
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/**
 * Lightweight sanity check on a caller-supplied path.
 * Returns VFS_OK, VFS_ERR_INVALID, or VFS_ERR_TOOLONG.
 */
static VFS_Status validate_path(const char* path) {
	if (!path) {
		return VFS_ERR_INVALID;
	}

	/* Must be absolute. */
	if (path[0] != '/') {
		return VFS_ERR_INVALID;
	}

	size_t len = strnlen(path, VFS_PATH_MAX);

	if (len >= VFS_PATH_MAX) {
		return VFS_ERR_TOOLONG;
	}

	return VFS_OK;
}

/**
 * Find the mount entry whose path is the longest prefix of `path`.
 * Returns a pointer into vfs_mounts, or NULL if no mount matches.
 */
static VFS_MountEntry* resolve_mount(const char* path) {
	VFS_MountEntry* best = NULL;
	size_t          best_len = 0;

	for (int i = 0; i < VFS_MOUNT_MAX; i++) {
		VFS_MountEntry* m = &vfs_mounts[i];

		if (!m->active) {
			continue;
		}

		size_t mlen = m->path_len;

		if (mlen > best_len && strncmp(path, m->path, mlen) == 0) {
			/*
			 * The mount point must match a full path component boundary.
			 * "/" matches everything.
			 * Any other mount point must be followed by '/' or '\0' in the input path.
			 */
			if (mlen == 1 || path[mlen] == '/' || path[mlen] == '\0') {
				best = m;
				best_len = mlen;
			}
		}
	}

	return best;
}

/**
 * Return the portion of `path` that should be passed to the filesystem driver.
 * The input path with the mount point prefix stripped. Always begins with '/'.
 *
 * Examples:
 *   mount="/mnt/usb", path="/mnt/usb/foo.txt" → "/foo.txt"
 *   mount="/mnt/usb", path="/mnt/usb"          → "/"
 *   mount="/",        path="/etc/cfg"          → "/etc/cfg"
 */
static const char* driver_path(const VFS_MountEntry* m, const char* path) {
	/* Root, we pass the whole path unchanged. */
	if (m->path_len == 1) {
		return path;
	}

	const char* rel = path + m->path_len;

	/* Mount point was the entire path. */
	if (*rel == '\0') {
		return "/";
	}

	/* rel now points to the '/' separator that follows the mount prefix. */
	return rel;
}

/**
 * Find and mark a free slot in the FD table.
 * Returns the FD index, or VFS_FD_INVALID if the table is full.
 */
static VFS_FD alloc_fd(void) {
	for (int i = 0; i < VFS_FD_MAX; i++) {
		if (!vfs_fds[i].active) {
			vfs_fds[i].active = true;
			return (VFS_FD) i;
		}
	}

	return VFS_FD_INVALID;
}

/**
 * Check that `fd` is in range and active.
 */
static bool validate_fd(VFS_FD fd) {
	if (fd < 0 || fd >= VFS_FD_MAX) {
		return false;
	}

	return vfs_fds[fd].active;
}

/**
 * Close one FD entry, delegating to the driver.
 * The caller is responsible for removing it from the open_count.
 * Does not zero the slot on driver error, but always decrements count.
 */
static VFS_Status close_fd_entry(VFS_FDEntry* entry) {
	VFS_MountEntry* m = entry->mount;
	VFS_Status      st = m->ops->close_file(m->fs_ctx, entry->drv_fd);

	m->open_count--;
	memset(entry, 0, sizeof(*entry));
	return st;
}

/**
 * Call on_unmount and clear a mount slot.
 * Caller must ensure open_count == 0 beforehand.
 */
static void unmount_entry(VFS_MountEntry* m) {
	m->ops->on_unmount(m->fs_ctx);
	memset(m, 0, sizeof(*m));
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

VFS_Status VFS_Init(void) {
	if (wdm_initialized) return VFS_OK;

	memset(vfs_mounts, 0, sizeof(vfs_mounts));
	memset(vfs_fds, 0, sizeof(vfs_fds));
	vfs_mount_seq = 0;
	wdm_initialized = true;
	return VFS_OK;
}

void VFS_Shutdown(void) {
	if (!wdm_initialized) return;

	/* Force-close all open file descriptors. */
	for (int i = 0; i < VFS_FD_MAX; i++) {
		if (vfs_fds[i].active) {
			close_fd_entry(&vfs_fds[i]);
		}
	}

	/* Unmount in reverse insertion order (highest seq first),
	 * so inner mounts are always torn down before the outer mounts they shadow.
	 */
	uint32_t seq = vfs_mount_seq;

	while (seq > 0) {
		seq--;

		for (int i = 0; i < VFS_MOUNT_MAX; i++) {
			if (vfs_mounts[i].active && vfs_mounts[i].mount_seq == seq) {
				unmount_entry(&vfs_mounts[i]);
				break;
			}
		}
	}

	vfs_mount_seq = 0;
	wdm_initialized = false;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Mount table
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

VFS_Status VFS_Mount(const char* path, WDM_DriveHandle drive, const VFS_FSOps* ops, void* fs_ctx) {
	VFS_Status st = validate_path(path);

	if (st != VFS_OK) return st;
	if (!ops) return VFS_ERR_INVALID;

	// All operations are required.
	// Yes this is ugly. Yes this is the easiest way to do this.
	if (!ops->on_mount || !ops->on_unmount ||
		!ops->open_file || !ops->close_file ||
		!ops->read_file || !ops->write_file ||
		!ops->make_dir || !ops->remove_dir ||
		!ops->open_dir || !ops->read_dir) {
		return VFS_ERR_INVALID;
	}

	// Find a free slot
	VFS_MountEntry* slot = NULL;

	for (int i = 0; i < VFS_MOUNT_MAX; i++) {
		if (!vfs_mounts[i].active) {
			slot = &vfs_mounts[i];
			break;
		}
	}

	if (!slot) return VFS_ERR_MNTFULL;

	size_t plen = strlen(path);

	slot->path_len = plen;
	slot->drive = drive;
	slot->ops = ops;
	slot->fs_ctx = fs_ctx;
	slot->open_count = 0;
	slot->mount_seq = vfs_mount_seq;
	memcpy(slot->path, path, plen + 1);

	VFS_Status mst = ops->on_mount(drive, fs_ctx);

	if (mst != VFS_OK) {
		memset(slot, 0, sizeof(*slot));
		return VFS_ERR_IO;
	}

	slot->active = true;
	vfs_mount_seq++;
	return VFS_OK;
}

VFS_Status VFS_Unmount(const char* path) {
	VFS_Status st = validate_path(path);

	if (st != VFS_OK) return st;

	/* Find an exact match on the mount point path. */
	VFS_MountEntry* target = NULL;

	for (int i = 0; i < VFS_MOUNT_MAX; i++) {
		VFS_MountEntry* m = &vfs_mounts[i];

		if (m->active && strcmp(m->path, path) == 0) {
			target = m;
			break;
		}
	}

	if (!target) {
		return VFS_ERR_NOMNT;
	}

	if (target->open_count > 0) {
		return VFS_ERR_BUSY;
	}

	unmount_entry(target);
	return VFS_OK;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// File operations
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

VFS_Status VFS_Open(const char* path, VFS_OpenFlags flags, VFS_FD* out_fd) {
	if (!out_fd) return VFS_ERR_INVALID;

	VFS_Status st = validate_path(path);

	if (st != VFS_OK) return st;

	VFS_MountEntry* m = resolve_mount(path);

	if (!m) return VFS_ERR_NOMNT;

	VFS_FD vfd = alloc_fd();

	if (vfd == VFS_FD_INVALID) return VFS_ERR_FDFULL;

	VFS_FD     drv_fd = VFS_FD_INVALID;
	VFS_Status dst = m->ops->open_file(m->fs_ctx, driver_path(m, path), flags, &drv_fd);

	if (dst != VFS_OK) {
		/* Release the pre-allocated slot. */
		memset(&vfs_fds[vfd], 0, sizeof(vfs_fds[vfd]));
		return dst;
	}

	vfs_fds[vfd].mount = m;
	vfs_fds[vfd].drv_fd = drv_fd;
	vfs_fds[vfd].flags = flags;
	m->open_count++;

	*out_fd = vfd;
	return VFS_OK;
}

VFS_Status VFS_Close(VFS_FD fd) {
	if (!validate_fd(fd)) {
		return VFS_ERR_BADF;
	}

	return close_fd_entry(&vfs_fds[fd]);
}

VFS_Status VFS_Read(VFS_FD fd, void* buf, size_t size, size_t* out_read) {
	if (!buf || !out_read) return VFS_ERR_INVALID;

	if (!validate_fd(fd)) {
		return VFS_ERR_BADF;
	}

	VFS_FDEntry* entry = &vfs_fds[fd];
	VFS_MountEntry* m = entry->mount;

	/*
	 * Access mode check. VFS_O_RDONLY == 0x00 so we cannot test it with a plain bitwise AND
	 * We have to mask the low two bits and compare explicitly.
	 */
	uint8_t mode = (uint8_t) (entry->flags & 0x03);

	if (mode == VFS_O_WRONLY) return VFS_ERR_BADF;

	if (size == 0) {
		*out_read = 0;
		return VFS_OK;
	}

	return m->ops->read_file(m->fs_ctx, entry->drv_fd, buf, size, out_read);
}

VFS_Status VFS_Write(VFS_FD fd, const void* buf, size_t size, size_t* out_written) {
	if (!buf || !out_written) return VFS_ERR_INVALID;

	if (!validate_fd(fd)) {
		return VFS_ERR_BADF;
	}

	VFS_FDEntry* entry = &vfs_fds[fd];
	VFS_MountEntry* m = entry->mount;

	/* Must be open for writing. */
	uint8_t mode = (uint8_t) (entry->flags & 0x03);

	if (mode == VFS_O_RDONLY) {
		return VFS_ERR_BADF;
	}

	if (size == 0) {
		*out_written = 0;
		return VFS_OK;
	}

	return m->ops->write_file(m->fs_ctx, entry->drv_fd, buf, size, out_written);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Directory operations
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------


VFS_Status VFS_Mkdir(const char* path) {
	VFS_Status st = validate_path(path);

	if (st != VFS_OK) return st;

	VFS_MountEntry* m = resolve_mount(path);

	if (!m) return VFS_ERR_NOMNT;

	return m->ops->make_dir(m->fs_ctx, driver_path(m, path));
}

VFS_Status VFS_Rmdir(const char* path) {
	VFS_Status st = validate_path(path);

	if (st != VFS_OK) return st;


	/* Refuse to remove an active mount point before the resolve step,
	 * so we catch the case where `path` is itself a mount point even if a parent mount would otherwise absorb it.
	 */
	for (int i = 0; i < VFS_MOUNT_MAX; i++) {
		if (vfs_mounts[i].active && strcmp(vfs_mounts[i].path, path) == 0) {
			return VFS_ERR_BUSY;
		}
	}

	VFS_MountEntry* m = resolve_mount(path);

	if (!m)	return VFS_ERR_NOMNT;

	return m->ops->remove_dir(m->fs_ctx, driver_path(m, path));
}

VFS_Status VFS_Opendir(const char* path, VFS_FD* out_fd) {
	if (!out_fd) return VFS_ERR_INVALID;

	VFS_Status st = validate_path(path);
	if (st != VFS_OK) return st;

	VFS_MountEntry* m = resolve_mount(path);
	if (!m) return VFS_ERR_NOMNT;

	VFS_FD vfd = alloc_fd();
	if (vfd == VFS_FD_INVALID) return VFS_ERR_FDFULL;

	VFS_FD drv_fd = VFS_FD_INVALID;
	VFS_Status dst = m->ops->open_dir(m->fs_ctx, driver_path(m, path), &drv_fd);

	if (dst != VFS_OK) {
		/* Clean up the allocated slot on failure */
		vfs_fds[vfd].active = false;
		return dst;
	}

	vfs_fds[vfd].mount = m;
	vfs_fds[vfd].drv_fd = drv_fd;
	vfs_fds[vfd].flags = VFS_O_RDONLY; // Directories are opened read-only
	m->open_count++;

	*out_fd = vfd;
	return VFS_OK;
}

VFS_Status VFS_Readdir(VFS_FD fd, VFS_DirEnt* out_ent) {
	if (!out_ent) {
		return VFS_ERR_INVALID;
	}

	if (!validate_fd(fd)) {
		return VFS_ERR_BADF;
	}

	VFS_FDEntry* entry = &vfs_fds[fd];
	VFS_MountEntry* m = entry->mount;

	return m->ops->read_dir(m->fs_ctx, entry->drv_fd, out_ent);
}