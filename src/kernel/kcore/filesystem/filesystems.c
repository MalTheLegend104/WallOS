#include <filesystem/filesystems.h>

#include <stdio.h>
#include <string.h>

/* We never need more concurrent mounts than the VFS itself can hold. */
#define FS_MAX_MOUNTS VFS_MOUNT_MAX

/* Used to keep track of mounts/unmounts */
typedef struct {
	bool in_use;
	char mountpoint[VFS_PATH_MAX];
	known_fs_types_t type;
	WDM_DriveHandle drive;
	const VFS_FSOps* ops; //< Driver that owns fs_ctx. destroy_context() is called on it at unmount.
	void* fs_ctx; //< Opaque context returned by ops->create_context().
} fs_mount_record_t;

/* Registered driver vtables, indexed by known_fs_types_t. NULL == unregistered. */
static const VFS_FSOps* fs_drivers[FS_TYPE_COUNT];

/* Bookkeeping for mounts made through this layer, so filesystem_unmount() knows what to tear down for a given mountpoint. */
static fs_mount_record_t fs_mounts[FS_MAX_MOUNTS];

// ------------------------------------------------------------------------------------------------
// Small helpers
// ------------------------------------------------------------------------------------------------

static filesystem_status_t validate_mountpoint(const char* mountpoint) {
	if (mountpoint == NULL || mountpoint[0] == '\0' || strnlen(mountpoint, VFS_PATH_MAX) >= VFS_PATH_MAX) {
		return FILESYSTEM_NO_MOUNTPOINT;
	}

	return FILESYSTEM_SUCCESS;
}

static bool drive_is_valid(WDM_DriveHandle drive) {
	WDM_DriveInfo info;

	if (drive == NULL) {
		return false;
	}

	return WDM_GetInfo(drive, &info) == WDM_OK;
}

/**
 * @brief Same check that happens in VFS_Mount().
 * This is used on register to avoid anything invalid well before we get to mounting.
 */
static bool fsops_is_complete(const VFS_FSOps* ops) {
	return ops->create_context != NULL
		&& ops->destroy_context != NULL
		&& ops->on_mount != NULL
		&& ops->on_unmount != NULL
		&& ops->open_file != NULL
		&& ops->close_file != NULL
		&& ops->read_file != NULL
		&& ops->write_file != NULL
		&& ops->make_dir != NULL
		&& ops->remove_dir != NULL
		&& ops->open_dir != NULL
		&& ops->read_dir != NULL;
}

static filesystem_status_t vfs_status_to_filesystem_status(VFS_Status status) {
	switch (status) {
		case VFS_OK: return FILESYSTEM_SUCCESS;
		case VFS_ERR_INVALID: return FILESYSTEM_INVALID_FILESYSTEM;
		case VFS_ERR_IO: return FILESYSTEM_DEVICE_ERROR;
		case VFS_ERR_NOMNT: return FILESYSTEM_NOT_MOUNTED;
		case VFS_ERR_BUSY: return FILESYSTEM_MOUNTPOINT_IN_USE;
		case VFS_ERR_MNTFULL: return FILESYSTEM_OUT_OF_MEMORY;
		case VFS_ERR_TOOLONG: return FILESYSTEM_NO_MOUNTPOINT;
		default: return FILESYSTEM_UNKNOWN_ERROR;
	}
}

static fs_mount_record_t* find_mount_record(const char* mountpoint) {
	int i;

	for (i = 0; i < FS_MAX_MOUNTS; i++) {
		if (fs_mounts[i].in_use && strncmp(fs_mounts[i].mountpoint, mountpoint, VFS_PATH_MAX) == 0) {
			return &fs_mounts[i];
		}
	}

	return NULL;
}

static fs_mount_record_t* find_free_mount_record(void) {
	int i;

	for (i = 0; i < FS_MAX_MOUNTS; i++) {
		if (!fs_mounts[i].in_use) {
			return &fs_mounts[i];
		}
	}

	return NULL;
}

/**
 * @brief Shared by filesystem_mount() and mount_drive()
 *
 * Asks the driver for a context, mounts it, and records the mount so filesystem_unmount() can find it again.
 * On any failure, whatever was created here is destroyed before returning.
 */
static filesystem_status_t mount_core(WDM_DriveHandle drive, const char* mountpoint, known_fs_types_t type, const VFS_FSOps* ops) {
	fs_mount_record_t* record;
	void* fs_ctx;
	VFS_Status vfs_status;

	record = find_free_mount_record();
	if (record == NULL) return FILESYSTEM_OUT_OF_MEMORY;

	fs_ctx = ops->create_context();
	if (fs_ctx == NULL) return FILESYSTEM_OUT_OF_MEMORY;

	vfs_status = VFS_Mount(mountpoint, drive, ops, fs_ctx);
	if (vfs_status != VFS_OK) {
		ops->destroy_context(fs_ctx);
		return vfs_status_to_filesystem_status(vfs_status);
	}

	record->in_use = true;
	strncpy(record->mountpoint, mountpoint, VFS_PATH_MAX - 1);
	record->mountpoint[VFS_PATH_MAX - 1] = '\0';
	record->type = type;
	record->drive = drive;
	record->ops = ops;
	record->fs_ctx = fs_ctx;

	return FILESYSTEM_SUCCESS;
}

// ------------------------------------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------------------------------------

filesystem_status_t filesystem_probe(WDM_DriveHandle drive, known_fs_types_t* type) {
	int i;

	if (type == NULL) return FILESYSTEM_UNKNOWN_ERROR;

	if (!drive_is_valid(drive)) return FILESYSTEM_INVALID_DRIVE;

	for (i = 0; i < FS_TYPE_COUNT; i++) {
		const VFS_FSOps* ops = fs_drivers[i];

		if (ops == NULL || ops->probe == NULL) {
			continue;
		}

		if (ops->probe(drive)) {
			*type = (known_fs_types_t) i;
			return FILESYSTEM_SUCCESS;
		}
	}

	return FILESYSTEM_NO_FILESYSTEM;
}

filesystem_status_t filesystem_mount(WDM_DriveHandle drive, const char* mountpoint) {
	known_fs_types_t type;
	filesystem_status_t status;
	const VFS_FSOps* ops;

	status = validate_mountpoint(mountpoint);
	if (status != FILESYSTEM_SUCCESS) return status;


	/* Auto-detect. Try every registered driver via probe() and take whichever one claims the drive. */
	status = filesystem_probe(drive, &type);
	if (status != FILESYSTEM_SUCCESS) return status;


	ops = fs_drivers[type];
	/* Detected by probe(), but nobody registered a full driver for it. */
	if (ops == NULL) return FILESYSTEM_UNSUPPORTED_FILESYSTEM;


	return mount_core(drive, mountpoint, type, ops);
}

filesystem_status_t filesystem_mount_explicit(WDM_DriveHandle drive, const char* mountpoint, known_fs_types_t type) {
	filesystem_status_t status;
	const VFS_FSOps* ops;

	status = validate_mountpoint(mountpoint);
	if (status != FILESYSTEM_SUCCESS) return status;


	if (!drive_is_valid(drive)) return FILESYSTEM_INVALID_DRIVE;


	if ((int) type < 0 || type >= FS_TYPE_COUNT) return FILESYSTEM_INVALID_FILESYSTEM;


	ops = fs_drivers[type];
	if (ops == NULL) return FILESYSTEM_UNSUPPORTED_FILESYSTEM;

	/* Since the caller explicitly requested the filesystem, we only check against that type.
	 * If there is no probe, we blindly trust the caller.
	 */
	if (ops->probe != NULL && !ops->probe(drive)) return FILESYSTEM_NO_FILESYSTEM;

	return mount_core(drive, mountpoint, type, ops);
}

filesystem_status_t filesystem_unmount(const char* mountpoint) {
	fs_mount_record_t* record;
	VFS_Status vfs_status;

	if (mountpoint == NULL || mountpoint[0] == '\0') return FILESYSTEM_NO_MOUNTPOINT;


	record = find_mount_record(mountpoint);
	if (record == NULL) return FILESYSTEM_NOT_MOUNTED;


	vfs_status = VFS_Unmount(mountpoint);
	if (vfs_status != VFS_OK) {
		/* Mount stays registered (VFS_ERR_BUSY means fds still open) */
		return vfs_status_to_filesystem_status(vfs_status);
	}

	record->ops->destroy_context(record->fs_ctx);
	record->in_use = false;
	record->ops = NULL;
	record->fs_ctx = NULL;
	record->drive = NULL;
	record->mountpoint[0] = '\0';

	return FILESYSTEM_SUCCESS;
}

filesystem_status_t filesystem_enumerate_mounts(filesystem_mount_info_t* out, size_t max, size_t* out_count) {
	size_t count = 0;
	int i;

	for (i = 0; i < FS_MAX_MOUNTS; i++) {
		if (!fs_mounts[i].in_use) {
			continue;
		}

		/* Still tally the total even past 'max', so the caller can tell it was truncated. */
		if (out != NULL && count < max) {
			strncpy(out[count].mountpoint, fs_mounts[i].mountpoint, VFS_PATH_MAX - 1);
			out[count].mountpoint[VFS_PATH_MAX - 1] = '\0';
			out[count].type = fs_mounts[i].type;
			out[count].drive = fs_mounts[i].drive;
		}

		count++;
	}

	if (out_count != NULL) {
		*out_count = count;
	}

	return FILESYSTEM_SUCCESS;
}

filesystem_status_t filesystem_register(known_fs_types_t type, const VFS_FSOps* ops) {
	if ((int) type < 0 || type >= FS_TYPE_COUNT || ops == NULL) return FILESYSTEM_INVALID_FILESYSTEM;


	if (!fsops_is_complete(ops))  return FILESYSTEM_INVALID_FILESYSTEM;


	/* Registering over an existing entry replaces it.
	 * There is no reason to NOT allow for re-registration, might eventually support driver updates and such
	 */
	fs_drivers[type] = ops;

	return FILESYSTEM_SUCCESS;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// FS Names
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

typedef struct {
	const char* name; //< Matched against the 'fs' argument of 'mount'.
	known_fs_types_t type;
} fs_name_entry_t;

static const fs_name_entry_t fs_names[] = {
	{ "fat12",   FILESYSTEM_FAT12_16 },
	{ "fat16",   FILESYSTEM_FAT12_16 },
	{ "fat32",   FILESYSTEM_FAT32    },
	{ "iso9660", FILESYSTEM_ISO9660  },
};
#define FS_NAME_COUNT (sizeof(fs_names) / sizeof(fs_names[0]))

bool find_fs_type_by_name(const char* name, known_fs_types_t* out_type) {
	for (size_t i = 0; i < FS_NAME_COUNT; i++) {
		if (strcmp(name, fs_names[i].name) == 0) {
			*out_type = fs_names[i].type;
			return true;
		}
	}
	return false;
}
const char* fs_type_name(known_fs_types_t type) {
	switch (type) {
		case FILESYSTEM_FAT12_16: return "fat12/16";
		case FILESYSTEM_FAT32:    return "fat32";
		case FILESYSTEM_ISO9660:  return "iso9660";
		default:                  return "unknown";
	}
}

void print_fs_name_list(void) {
	printf("Available filesystems: ");
	for (size_t i = 0; i < FS_NAME_COUNT; i++) {
		printf("%s%s", fs_names[i].name, (i + 1 < FS_NAME_COUNT) ? ", " : "\n");
	}
}