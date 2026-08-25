#ifndef WALLOS_VFS_H
#define WALLOS_VFS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "wdm.h"

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Limits
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

#ifndef VFS_PATH_MAX
/** Maximum length of an absolute path, including the null terminator. */
#define VFS_PATH_MAX   256
#endif

#ifndef VFS_FD_MAX
/** Maximum number of simultaneously open file descriptors. */
#define VFS_FD_MAX      64
#endif

#ifndef VFS_MOUNT_MAX
/** Maximum number of simultaneously mounted filesystems. */
#define VFS_MOUNT_MAX   16
#endif

#ifndef VFS_FD_INVALID
/** Sentinel value for an invalid or un-opened file descriptor. */
#define VFS_FD_INVALID  (-1)
#endif

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Basic types
	//
	// NOTE: Some of this may seem weird (like VFS_FD being an int). Almost everything "weird" was
	//       done in an attempt to make POSIX compliance easier.
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/** File descriptor. Negative values are invalid (see VFS_FD_INVALID). */
	typedef int VFS_FD;

	/** Error codes returned by all VFS functions. */
	typedef enum {
		VFS_OK = 0,                /**< Success                                      */
		VFS_ERR_INVALID = -1,      /**< Invalid argument (NULL pointer, bad flags)   */
		VFS_ERR_IO = -2,           /**< I/O error propagated from the WDM layer      */
		VFS_ERR_NOENT = -3,        /**< No such file or directory                    */
		VFS_ERR_EXIST = -4,        /**< File or directory already exists             */
		VFS_ERR_NOTDIR = -5,       /**< A path component is not a directory          */
		VFS_ERR_ISDIR = -6,        /**< Expected a file, got a directory             */
		VFS_ERR_NOTEMPTY = -7,     /**< Directory is not empty                       */
		VFS_ERR_NOMNT = -8,        /**< Path does not map to any mounted filesystem  */
		VFS_ERR_BADF = -9,         /**< Bad file descriptor                          */
		VFS_ERR_BUSY = -10,        /**< Resource busy (open fds, active mount point) */
		VFS_ERR_NOSPACE = -11,     /**< No space left on device                      */
		VFS_ERR_TOOLONG = -12,     /**< Path exceeds VFS_PATH_MAX                    */
		VFS_ERR_OVERFLOW = -13,    /**< Read/write offset exceeds file bounds        */
		VFS_ERR_MNTFULL = -14,     /**< Mount table is full (VFS_MOUNT_MAX reached)  */
		VFS_ERR_FDFULL = -15,      /**< FD table is full (VFS_FD_MAX reached)        */
		VFS_ERR_UNSUPPORTED = -16, /**< Operation not supported by this filesystem   */
	} VFS_Status;

	/** Flags passed to VFS_Open. May be OR'd together. */
	typedef enum {
		VFS_O_RDONLY = 0x00, /**< Open for reading only                            */
		VFS_O_WRONLY = 0x01, /**< Open for writing only                            */
		VFS_O_RDWR = 0x02,   /**< Open for reading and writing                     */
		VFS_O_CREAT = 0x04,  /**< Create file if it does not exist                 */
		VFS_O_TRUNC = 0x08,  /**< Truncate file to zero length on open             */
		VFS_O_EXCL = 0x10,   /**< With VFS_O_CREAT: fail if file already exists    */
		VFS_O_APPEND = 0x20, /**< Writes always go to end of file                  */
	} VFS_OpenFlags;

	typedef struct {
		char name[VFS_PATH_MAX]; /**< Null-terminated filename. */
		bool is_directory;       /**< True if entry is a directory, false if file. */
		size_t size;             /**< File size in bytes (if applicable). */
	} VFS_DirEnt;

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Filesystem driver vtable
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/**
	 * @brief Filesystem driver operations vtable.
	 *
	 * A filesystem driver fills out this struct and passes it to VFS_Mount().
	 * The VFS holds a pointer to the vtable for the lifetime of the mount,
	 * so the struct must remain valid (static or otherwise long-lived storage) until VFS_Unmount() returns.
	 *
	 * All function pointers are **required** unless explicitly marked optional.
	 * Paths delivered to driver ops are always absolute, begin with @c '/',
	 * and are relative to the mount root (i.e. the mount point prefix has been stripped by the VFS before the call).
	 */
	typedef struct {
		/**
		 * @brief Called by VFS_Mount() after the mount table entry is reserved.
		 *        **Required.**
		 *
		 * The driver should validate the on-disk structure (e.g. read and verify a superblock) and initialise any state stored in @p fs_ctx.
		 * If this returns a non-OK status, the mount is aborted and the table entry is freed.
		 *
		 * @param drive  WDM handle for the underlying block device.
		 * @param fs_ctx Opaque driver context supplied to VFS_Mount().
		 *
		 * @return VFS_OK to allow the mount to proceed, or any VFS_ERR_* code to abort.
		 */
		VFS_Status(*on_mount)(WDM_DriveHandle drive, void* fs_ctx);

		/**
		 * @brief Called by VFS_Unmount() after all open fds on this mount have been closed and the entry is removed from the table.
		 *        **Required.**
		 *
		 * The driver should flush any cached metadata and release resources held in @p fs_ctx.
		 * No further ops will be called on this context after this function returns.
		 *
		 * @param fs_ctx Opaque driver context supplied to VFS_Mount().
		 */
		void (*on_unmount)(void* fs_ctx);

		/**
		 * @brief Open or create a file. **Required.**
		 *
		 * The driver allocates whatever internal file state is needed and writes a descriptor value into @p *out_fd.
		 * The value must be non-negative and unique within this driver instance.
		 *
		 * @param fs_ctx  Opaque driver context.
		 * @param path    Path relative to the mount root (begins with '/').
		 * @param flags   VFS_OpenFlags controlling access mode and creation.
		 * @param out_fd  Receives the driver-level file descriptor on success.
		 *
		 * @return VFS_OK on success.
		 * @retval VFS_ERR_NOENT   if the file does not exist and VFS_O_CREAT was not specified.
		 * @retval VFS_ERR_EXIST   if VFS_O_CREAT | VFS_O_EXCL and file exists.
		 * @retval VFS_ERR_ISDIR   if the path names a directory.
		 * @retval VFS_ERR_NOSPACE if there is no space to create the file.
		 * @retval VFS_ERR_IO      on hardware failure.
		 */
		VFS_Status(*open_file)(void* fs_ctx, const char* path, VFS_OpenFlags flags, VFS_FD* out_fd);

		/**
		 * @brief Close a file and release driver resources. **Required.**
		 *
		 * After this call the driver may reclaim any state associated with @p fd.
		 * The VFS guarantees this is only called for fds that were previously returned by @p open_file on the same context.
		 *
		 * @param fs_ctx Opaque driver context.
		 * @param fd     Driver-level file descriptor to close.
		 *
		 * @return VFS_OK on success.
		 * @retval VFS_ERR_BADF if @p fd is not a valid open descriptor.
		 * @retval VFS_ERR_IO   on hardware failure during implicit flush.
		 */
		VFS_Status(*close_file)(void* fs_ctx, VFS_FD fd);

		/**
		 * @brief Read bytes from an open file. **Required.**
		 *
		 * Reads up to @p size bytes from the file's current position into @p buf and advances the position by the number of bytes read.
		 * Sets @p *out_read to the number of bytes actually transferred. A value of 0 indicates end-of-file.
		 *
		 * @param fs_ctx   Opaque driver context.
		 * @param fd       Driver-level file descriptor.
		 * @param buf      Destination buffer; at least @p size bytes.
		 * @param size     Maximum number of bytes to read.
		 * @param out_read Receives the number of bytes actually read.
		 *
		 * @return VFS_OK on success (including EOF).
		 * @retval VFS_ERR_BADF if @p fd is not open for reading.
		 * @retval VFS_ERR_IO   on hardware failure.
		 */
		VFS_Status(*read_file)(void* fs_ctx, VFS_FD fd, void* buf, size_t size, size_t* out_read);

		/**
		 * @brief Write bytes to an open file. **Required.**
		 *
		 * Writes @p size bytes from @p buf to the file at its current position,
		 * or at end-of-file if the file was opened with VFS_O_APPEND.
		 * Sets @p *out_written to the number of bytes actually written.
		 *
		 * @param fs_ctx      Opaque driver context.
		 * @param fd          Driver-level file descriptor.
		 * @param buf         Source buffer; at least @p size bytes.
		 * @param size        Number of bytes to write.
		 * @param out_written Receives the number of bytes actually written.
		 *
		 * @return VFS_OK on success.
		 * @retval VFS_ERR_BADF   if @p fd is not open for writing.
		 * @retval VFS_ERR_NOSPACE if the device has no space remaining.
		 * @retval VFS_ERR_IO     on hardware failure.
		 */
		VFS_Status(*write_file)(void* fs_ctx, VFS_FD fd, const void* buf, size_t size, size_t* out_written);

		/**
		 * @brief Create a directory. **Required.**
		 *
		 * Creates the final component of @p path as a new directory.
		 * Parent directories must already exist.
		 * This is not recursive.
		 *
		 * @param fs_ctx Opaque driver context.
		 * @param path   Path relative to the mount root (begins with '/').
		 *
		 * @return VFS_OK on success.
		 * @retval VFS_ERR_NOENT   if a parent component does not exist.
		 * @retval VFS_ERR_EXIST   if the path already exists.
		 * @retval VFS_ERR_NOSPACE if there is no space to create the entry.
		 * @retval VFS_ERR_IO      on hardware failure.
		 */
		VFS_Status(*make_dir)(void* fs_ctx, const char* path);

		/**
		 * @brief Remove an empty directory. **Required.**
		 *
		 * @param fs_ctx Opaque driver context.
		 * @param path   Path relative to the mount root (begins with '/').
		 *
		 * @return VFS_OK on success.
		 * @retval VFS_ERR_NOENT    if the path does not exist.
		 * @retval VFS_ERR_NOTDIR   if the path is not a directory.
		 * @retval VFS_ERR_NOTEMPTY if the directory contains entries.
		 * @retval VFS_ERR_IO       on hardware failure.
		 */
		VFS_Status(*remove_dir)(void* fs_ctx, const char* path);

		/**
		 * @brief Open a directory for reading. **Required.**
		 *
		 * Similar to open_file, but for directories.
		 * @param out_fd Receives the driver-level directory descriptor.
		 */
		VFS_Status(*open_dir)(void* fs_ctx, const char* path, VFS_FD* out_fd);

		/**
		 * @brief Read the next entry from an open directory. **Required.**
		 *
		 * @param fd Driver-level directory descriptor.
		 * @param out_ent Pointer to entry structure to fill.
		 *
		 * @return VFS_OK on success.
		 * @retval VFS_OK with out_ent->name[0] == '\0' if end of directory reached.
		 */
		VFS_Status(*read_dir)(void* fs_ctx, VFS_FD fd, VFS_DirEnt* out_ent);
	} VFS_FSOps;

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Lifecycle
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/**
	 * @brief Initialise the VFS subsystem.
	 *
	 * Clears the mount table and the file descriptor table.
	 * Must be called once before any other VFS function, from a single-threaded context.
	 * WDM_Init() must have been called successfully before the first VFS_Mount() call.
	 *
	 * Calling VFS_Init() a second time without an intervening VFS_Shutdown() is a no-op.
	 *
	 * @return VFS_OK always.
	 */
	VFS_Status VFS_Init(void);

	/**
	 * @brief Tear down the VFS subsystem.
	 *
	 * Force-closes all open file descriptors, then unmounts all registered filesystems in reverse mount order.
	 * Must be called from a single-threaded context.
	 *
	 * After this call all VFS_FD values and mount state are invalid.
	 */
	void VFS_Shutdown(void);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Mount table
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/**
	 * @brief Mount a filesystem at an absolute path.
	 *
	 * Registers a new entry in the mount table and calls @p ops->on_mount().
	 * If @p on_mount returns non-OK, the mount is aborted.
	 * Mounting at a path that is a subdirectory of an existing mount is valid;
	 * the inner mount shadows that subtree for all subsequent path resolution.
	 *
	 * The @p ops pointer must remain valid until VFS_Unmount() is called for this mount point.
	 *
	 * @param path    Absolute mount point path (e.g. @c "/" or @c "/mnt/usb").
	 * @param drive   WDM handle for the underlying block device.
	 * @param ops     Filesystem driver vtable.
	 * @param fs_ctx  Opaque context passed to every @p ops call.
	 *
	 * @return VFS_OK on success.
	 * @retval VFS_ERR_INVALID  if @p path, @p ops, or any required op is NULL.
	 * @retval VFS_ERR_TOOLONG  if @p path exceeds VFS_PATH_MAX.
	 * @retval VFS_ERR_MNTFULL  if VFS_MOUNT_MAX entries are already registered.
	 * @retval VFS_ERR_IO       if @p ops->on_mount() returns non-OK.
	 * @retval VFS_ERR_BUSY     if the mount point @p path is already used.
	 */
	VFS_Status VFS_Mount(
		const char* path,
		WDM_DriveHandle  drive,
		const VFS_FSOps* ops,
		void* fs_ctx
	);

	/**
	 * @brief Unmount a filesystem by its mount point path.
	 *
	 * Fails if any file descriptors are currently open on the target mount.
	 * Calls @p ops->on_unmount() and removes the entry from the mount table.
	 * Unmount inner (more specific) mounts before outer ones.
	 *
	 * @param path Absolute mount point path to unmount.
	 *
	 * @return VFS_OK on success.
	 * @retval VFS_ERR_NOMNT   if @p path is not a registered mount point.
	 * @retval VFS_ERR_BUSY    if one or more file descriptors are open on
	 *                         this mount.
	 * @retval VFS_ERR_TOOLONG if @p path exceeds VFS_PATH_MAX.
	 */
	VFS_Status VFS_Unmount(const char* path);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// File operations
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/**
	 * @brief Open or create a file.
	 *
	 * Resolves @p path to the most specific mounted filesystem (longest prefix match),
	 * strips the mount point prefix, and delegates to the filesystem driver's @p open_file op.
	 * On success a non-negative file descriptor is written to @p *out_fd.
	 * This descriptor is valid until VFS_Close() is called on it.
	 *
	 * @p path must be absolute. Relative paths are not supported.
	 * It is expected that the environment resolve all paths before calling this.
	 *
	 * @param path    Absolute path of the file to open.
	 * @param flags   VFS_OpenFlags controlling access mode and creation.
	 * @param out_fd  Receives the file descriptor on success.
	 *
	 * @return VFS_OK on success.
	 * @retval VFS_ERR_INVALID  if @p path or @p out_fd is NULL.
	 * @retval VFS_ERR_TOOLONG  if @p path exceeds VFS_PATH_MAX.
	 * @retval VFS_ERR_NOMNT    if the path does not map to any mount.
	 * @retval VFS_ERR_FDFULL   if VFS_FD_MAX descriptors are already open.
	 * @retval VFS_ERR_NOENT    if the file does not exist and VFS_O_CREAT
	 *                          was not set.
	 * @retval VFS_ERR_EXIST    if VFS_O_CREAT | VFS_O_EXCL and file exists.
	 * @retval VFS_ERR_ISDIR    if the path names a directory.
	 * @retval VFS_ERR_IO       on hardware failure.
	 */
	VFS_Status VFS_Open(const char* path, VFS_OpenFlags flags, VFS_FD* out_fd);

	/**
	 * @brief Close an open file descriptor.
	 *
	 * Releases the descriptor and any VFS-level state associated with it.
	 * After a successful return, @p fd is invalid and must not be reused.
	 * Descriptors are not recycled until they have been explicitly closed.
	 *
	 * @param fd File descriptor to close.
	 *
	 * @return VFS_OK on success.
	 * @retval VFS_ERR_BADF if @p fd is not a valid open descriptor.
	 * @retval VFS_ERR_IO   on hardware failure during implicit flush.
	 */
	VFS_Status VFS_Close(VFS_FD fd);

	/**
	 * @brief Read bytes from an open file.
	 *
	 * Reads up to @p size bytes from @p fd's current position into @p buf.
	 * @p *out_read is set to the number of bytes actually read.
	 * A value of 0 indicates end-of-file.
	 * The file position is advanced by the number of bytes read.
	 *
	 * @param fd       Open file descriptor.
	 * @param buf      Destination buffer; at least @p size bytes.
	 * @param size     Maximum number of bytes to read.
	 * @param out_read Receives the number of bytes actually read.
	 *
	 * @return VFS_OK on success (including EOF).
	 * @retval VFS_ERR_BADF    if @p fd is not open for reading.
	 * @retval VFS_ERR_INVALID if @p buf or @p out_read is NULL.
	 * @retval VFS_ERR_IO      on hardware failure.
	 */
	VFS_Status VFS_Read(VFS_FD fd, void* buf, size_t size, size_t* out_read);

	/**
	 * @brief Write bytes to an open file.
	 *
	 * Writes @p size bytes from @p buf to @p fd.
	 * If the file was opened with VFS_O_APPEND, the write position is always end-of-file regardless of the current offset.
	 * @p *out_written is set to the number of bytes actually written.
	 *
	 * @param fd          Open file descriptor.
	 * @param buf         Source buffer; at least @p size bytes.
	 * @param size        Number of bytes to write.
	 * @param out_written Receives the number of bytes actually written.
	 *
	 * @return VFS_OK on success.
	 * @retval VFS_ERR_BADF    if @p fd is not open for writing.
	 * @retval VFS_ERR_INVALID if @p buf or @p out_written is NULL.
	 * @retval VFS_ERR_NOSPACE if the device has no space remaining.
	 * @retval VFS_ERR_IO      on hardware failure.
	 */
	VFS_Status VFS_Write(VFS_FD fd, const void* buf, size_t size, size_t* out_written);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Directory operations
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/**
	 * @brief Create a directory.
	 *
	 * Creates the final component of @p path as a new directory.
	 * Parent directories must already exist; this is not a recursive mkdir.
	 * Returns VFS_ERR_EXIST if the path already exists, whether as a file or a directory.
	 *
	 * @param path Absolute path of the directory to create.
	 *
	 * @return VFS_OK on success.
	 * @retval VFS_ERR_INVALID  if @p path is NULL.
	 * @retval VFS_ERR_TOOLONG  if @p path exceeds VFS_PATH_MAX.
	 * @retval VFS_ERR_NOMNT    if the path does not map to any mount.
	 * @retval VFS_ERR_NOENT    if a parent component does not exist.
	 * @retval VFS_ERR_EXIST    if the path already exists.
	 * @retval VFS_ERR_NOSPACE  if there is no space to create the entry.
	 * @retval VFS_ERR_IO       on hardware failure.
	 */
	VFS_Status VFS_Mkdir(const char* path);

	/**
	 * @brief Remove an empty directory.
	 *
	 * The directory must be empty.
	 * Returns VFS_ERR_BUSY if @p path is an active mount point; unmount it first.
	 *
	 * @param path Absolute path of the directory to remove.
	 *
	 * @return VFS_OK on success.
	 * @retval VFS_ERR_INVALID   if @p path is NULL.
	 * @retval VFS_ERR_TOOLONG   if @p path exceeds VFS_PATH_MAX.
	 * @retval VFS_ERR_NOMNT     if the path does not map to any mount.
	 * @retval VFS_ERR_NOENT     if the path does not exist.
	 * @retval VFS_ERR_NOTDIR    if the path is not a directory.
	 * @retval VFS_ERR_NOTEMPTY  if the directory contains entries.
	 * @retval VFS_ERR_BUSY      if the path is an active mount point.
	 * @retval VFS_ERR_IO        on hardware failure.
	 */
	VFS_Status VFS_Rmdir(const char* path);

	/**
	 * @brief Open a directory for enumeration.
	 *
	 * Resolves @p path and delegates to the driver's @p open_dir.
	 * The resulting @p VFS_FD must be closed with VFS_Close().
	 *
	 * @param path   Absolute path of the directory.
	 * @param out_fd Receives the file descriptor on success.
	 *
	 * @return VFS_OK on success.
	 * @retval VFS_ERR_NOTDIR if the path is not a directory.
	 * @retval VFS_ERR_NOMNT  if the path does not map to any mount.
	 */
	VFS_Status VFS_Opendir(const char* path, VFS_FD* out_fd);

	/**
	 * @brief Read the next entry from an open directory descriptor.
	 *
	 * Each call retrieves the next file or subdirectory within the directory.
	 * To signal the end of the directory, the @p name field in @p out_ent
	 * will be an empty string (first byte is '\0').
	 *
	 * @param fd      Descriptor returned by VFS_Opendir.
	 * @param out_ent Pointer to structure to be populated with entry data.
	 *
	 * @return VFS_OK on success (including end-of-directory).
	 * @retval VFS_ERR_BADF if @p fd is not a valid directory descriptor.
	 */
	VFS_Status VFS_Readdir(VFS_FD fd, VFS_DirEnt* out_ent);

#ifdef __cplusplus
}
#endif
#endif /* WALLOS_VFS_H */