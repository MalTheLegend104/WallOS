#ifndef WALLOS_FILESYSTEMS_H
#define WALLOS_FILESYSTEMS_H

#include <filesystem/vfs.h>

#ifdef __cplusplus
extern "C" {
#endif

	/**
	 * @brief Status codes returned by the filesystem management layer.
	 */
	typedef enum {
		FILESYSTEM_SUCCESS = 0,

		/* Discovery */
		FILESYSTEM_NO_FILESYSTEM,          ///< Nothing on this drive/partition looks like a known filesystem
		FILESYSTEM_NO_DRIVE,               ///< Could not find the specified drive
		FILESYSTEM_DRIVE_IN_USE,           ///< Drive is already in use by something else. This does not mean it's already mounted (though it could, this is a very generic error)

		/* Invalid/unusable objects */
		FILESYSTEM_INVALID_DRIVE,          ///< Invalid drive number/handle/path/name
		FILESYSTEM_INVALID_FILESYSTEM,     ///< Filesystem information or state is invalid
		FILESYSTEM_UNSUPPORTED_FILESYSTEM, ///< Filesystem was identified but is not supported
		FILESYSTEM_CORRUPT_FILESYSTEM,     ///< Filesystem was identified but its on-disk structure is corrupt

		/* Mount state */
		FILESYSTEM_ALREADY_MOUNTED,        ///< The specified drive/filesystem is already mounted
		FILESYSTEM_NOT_MOUNTED,            ///< The specified drive/filesystem is not mounted
		FILESYSTEM_MOUNT_FAILED,           ///< The filesystem driver failed to mount the filesystem

		/* Resources */
		FILESYSTEM_OUT_OF_MEMORY,          ///< Could not allocate required resources
		FILESYSTEM_NO_MOUNTPOINT,          ///< No valid mountpoint was specified
		FILESYSTEM_MOUNTPOINT_IN_USE,      ///< The specified mountpoint is already in use

		/* I/O / device failures */
		FILESYSTEM_DEVICE_ERROR,           ///< The underlying device reported an error
		FILESYSTEM_IO_ERROR,               ///< An I/O operation failed
		FILESYSTEM_DEVICE_NOT_READY,       ///< The underlying device is not ready

		/* Permissions / policy */
		FILESYSTEM_READ_ONLY,              ///< The filesystem or underlying device is read-only
		FILESYSTEM_ACCESS_DENIED,          ///< The requested operation is not permitted

		FILESYSTEM_UNKNOWN_ERROR,          ///< An unspecified error occurred
	} filesystem_status_t;

	typedef enum {
		FILESYSTEM_FAT12_16 = 0,
		FILESYSTEM_FAT32,
		FILESYSTEM_ISO9660,

		FS_TYPE_COUNT // keep this at the end
	} known_fs_types_t;

	/**
	 * @brief Probe a drive for a known filesystem.
	 *
	 * @param drive Drive to probe.
	 * @param type Receives the detected filesystem type.
	 *
	 * @return FILESYSTEM_SUCCESS if a filesystem was detected, or a FILESYSTEM_* status code on failure.
	 */
	filesystem_status_t filesystem_probe(WDM_DriveHandle drive, known_fs_types_t* type);

	/**
	 * @brief Mount a filesystem on the specified drive.
	 *
	 * @param drive Drive containing the filesystem.
	 * @param mountpoint VFS path where the filesystem should be mounted.
	 *
	 * @return FILESYSTEM_SUCCESS if mounted successfully, or a FILESYSTEM_* status code on failure.
	 */
	filesystem_status_t filesystem_mount(WDM_DriveHandle drive, const char* mountpoint);

	/**
	 * @brief Mount a drive as an explicit, caller-chosen filesystem type.
	 * If that driver has a probe() op, it's used to sanity-check the drive first.
	 *
	 * @param drive Drive containing the filesystem.
	 * @param mountpoint VFS path where the filesystem should be mounted.
	 * @param type Filesystem type to mount as.
	 *
	 * @return FILESYSTEM_SUCCESS if mounted successfully, or a FILESYSTEM_* status code on failure.
	 * @retval FILESYSTEM_NO_FILESYSTEM if @p type has a probe() op and it reports the drive does not contain that filesystem.
	 * @retval FILESYSTEM_UNSUPPORTED_FILESYSTEM if no driver is registered for @p type.
	 */
	filesystem_status_t filesystem_mount_explicit(WDM_DriveHandle drive, const char* mountpoint, known_fs_types_t type);

	/**
	 * @brief Unmount a filesystem from the specified mountpoint.
	 *
	 * @param mountpoint VFS path of the filesystem to unmount.
	 *
	 * @return FILESYSTEM_SUCCESS if unmounted successfully, or a FILESYSTEM_* status code on failure.
	 */
	filesystem_status_t filesystem_unmount(const char* mountpoint);

	/**
	 * @brief Snapshot of one active mount, as returned by filesystem_enumerate_mounts().
	 */
	typedef struct {
		char mountpoint[VFS_PATH_MAX];
		known_fs_types_t type;
		WDM_DriveHandle drive; /**< The drive/partition handle this mount was made on - whatever filesystem_mount()/filesystem_mount_explicit() was originally given. */
	} filesystem_mount_info_t;

	/**
	 * @brief List every filesystem currently mounted through this layer.
	 *
	 * Fills @p out with up to @p max entries and stores the total mounted count in @p *out_count (even if it exceeds @p max).
	 * Pass @p out = NULL and @p max = 0 to query the count only.
	 * Each entry's 'drive' field can be compared against a WDM_DriveHandle to find what, if anything, is mounted on it.
	 *
	 * @param out Output array of mount info (or NULL).
	 * @param max Maximum number of entries to fill.
	 * @param out_count Receives the total number of active mounts. This means that if there are more mounts than `max`, out will only be filled to `max`, and this can be used to determine if `out` was truncated.
	 *
	 * @return FILESYSTEM_SUCCESS always.
	 */
	filesystem_status_t filesystem_enumerate_mounts(filesystem_mount_info_t* out, size_t max, size_t* out_count);

	/**
	 * @brief Register a filesystem driver with the filesystem layer.
	 *
	 * @param type Filesystem type provided by the driver.
	 * @param ops Filesystem driver operations.
	 *
	 * @return FILESYSTEM_SUCCESS if registered successfully, or a FILESYSTEM_* status code on failure.
	 */
	filesystem_status_t filesystem_register(known_fs_types_t type, const VFS_FSOps* ops);

	bool find_fs_type_by_name(const char* name, known_fs_types_t* out_type);
	const char* fs_type_name(known_fs_types_t type);
	void print_fs_name_list(void);

#ifdef __cplusplus
}
#endif

#endif //  WALLOS_FILESYSTEMS_H