#ifndef WALLOS_WDM_H
#define WALLOS_WDM_H

#include <stdint.h>
#include <stdbool.h>

// This define lets us enable/disable ASYNC Disk I/O
// I wanted to define ASYNC operations in the spec, but have zero reason to implement them at the moment.
// #define WALLOS_WDM_ASYNC_IO 

// This define lets us enable/disable the DMA support 
// Currently we have *zero* kernel level support for DMA for *anything*
// I wanted this as part of the spec, honestly just so I'd remember to add it eventually.
// #define WALLOS_WDM_DMA 

#ifdef __cplusplus
extern "C" {
#endif

	/** Opaque handle to a registered drive. */
	typedef struct WDM_Drive* WDM_DriveHandle;

	/** Sector address (logical block address). */
	typedef uint64_t WDM_LBA;

	/** Error codes returned by all WDM functions. */
	typedef enum {
		WDM_OK = 0,                /**< Success                              */
		WDM_ERR_INVALID = -1,      /**< Invalid argument or handle           */
		WDM_ERR_IO = -2,           /**< Hardware / bus I/O error             */
		WDM_ERR_TIMEOUT = -3,      /**< Operation exceeded deadline          */
		WDM_ERR_BUSY = -4,         /**< Drive or resource temporarily busy   */
		WDM_ERR_NO_MEDIA = -5,     /**< No media present (removable drives)  */
		WDM_ERR_WRITE_PROT = -6,   /**< Drive is write-protected             */
		WDM_ERR_NOT_FOUND = -7,    /**< Drive not registered                 */
		WDM_ERR_ALIGN = -8,        /**< Buffer or LBA alignment violation    */
		WDM_ERR_OVERFLOW = -9,     /**< LBA + sector count exceeds drive end */
		WDM_ERR_UNSUPPORTED = -10, /**< Operation not supported by driver    */
		WDM_ERR_DMA = -11,         /**< DMA mapping or transfer failure      */
		WDM_ERR_ALREADY_EXISTS = -12, /**< Something already exists          */
	} WDM_Status;

	/** Flags passed to read/write operations. */
	typedef enum {
		WDM_FLAG_NONE = 0x00,
		WDM_FLAG_SYNC = 0x01,     /**< Wait for media write, not just cache */
		WDM_FLAG_DMA = 0x02,      /**< Use DMA transfer if available        */
		WDM_FLAG_NO_CACHE = 0x04, /**< Bypass any driver-level cache        */
	} WDM_IOFlags;

	/** Geometry and capability information for a registered drive. */
	typedef struct {
		uint64_t    sector_count;    /**< Total number of logical sectors    */
		uint32_t    sector_size;     /**< Bytes per logical sector           */
		uint32_t    physical_sector; /**< Physical sector size (for 4K-n)    */
		uint32_t    optimal_xfer;    /**< Preferred transfer size in sectors */
		bool        removable;       /**< True if media may be swapped       */
		bool        read_only;       /**< True if drive is write-protected   */
		bool        dma_capable;     /**< True if DMA transfers are allowed  */
		char        model[64];       /**< Human-readable model string        */
		char        serial[32];      /**< Serial number string               */
	} WDM_DriveInfo;

	/**
	 * @brief Synchronous sector read
	 *
	 * Reads `count` sectors starting at `lba` into `buf`.
	 * `buf` must be at least `count * sector_size` bytes and aligned to `sector_size`.
	 * The call blocks until the transfer completes or an error occurs.
	 *
	 * @param handle registered drive handle
	 * @param lba starting logical block address
	 * @param count number of sectors to read
	 * @param buf destination buffer (must be sector-aligned)
	 * @param flags WDM_FLAG_DMA, WDM_FLAG_NO_CACHE, etc.
	 *
	 * @return WDM_Status
	 * @retval WDM_OK on success
	 * @retval WDM_ERR_IO on hardware failure
	 * @retval WDM_ERR_TIMEOUT if the drive did not respond in time
	 * @retval WDM_ERR_OVERFLOW if lba + count > sector_count
	 * @retval WDM_ERR_ALIGN if buf or lba violates alignment requirements
	 */
	WDM_Status WDM_Read(
		WDM_DriveHandle handle,
		WDM_LBA         lba,
		uint32_t        count,
		void* buf,
		WDM_IOFlags     flags
	);

	/**
	 * @brief Synchronous sector write
	 *
	 * Writes `count` sectors from `buf` to the drive starting at `lba`.
	 * With WDM_FLAG_SYNC the call blocks until the device confirms the data is on stable storage (not just in its write cache).
	 * Without WDM_FLAG_SYNC the call returns once data is accepted by the drive's internal buffer; call WDM_Flush to commit.
	 *
	 * @param handle registered drive handle
	 * @param lba starting logical block address
	 * @param count number of sectors to write
	 * @param buf source buffer
	 * @param flags I/O flags
	 *
	 * @return WDM_Status
	 * @retval WDM_ERR_WRITE_PROT if the drive is read-only
	 * @retval WDM_ERR_IO on hardware failure
	 * @retval WDM_ERR_TIMEOUT if the drive did not respond in time
	 * @retval WDM_ERR_OVERFLOW if lba + count > sector_count
	 * @retval WDM_ERR_ALIGN if buf or lba violates alignment requirements
	 */
	WDM_Status WDM_Write(
		WDM_DriveHandle handle,
		WDM_LBA         lba,
		uint32_t        count,
		const void* buf,
		WDM_IOFlags     flags
	);

	/**
	 * @brief Advise the drive that a range of sectors is unused
	 *
	 * Issues a TRIM (ATA) or Deallocate (NVMe) hint for sectors [lba, lba + count).
	 * This is a hint only, the drive may ignore it.
	 * Has no effect on drives that do not support discard (e.g., HDDs).
	 *
	 * @param handle registered drive handle
	 * @param lba starting logical block address
	 * @param count number of sectors
	 *
	 * @return
	 * @retval WDM_OK on success or silently ignored
	 * @retval WDM_ERR_OVERFLOW if lba + count > sector_count
	 * @retval WDM_ERR_IO on hardware failure
	 */
	WDM_Status WDM_Trim(
		WDM_DriveHandle handle,
		WDM_LBA         lba,
		uint32_t        count
	);

	/**
	 * @brief Flush write-back cache to stable storage
	 *
	 * Issues a cache-flush command (e.g., ATA FLUSH CACHE, NVMe Flush).
	 * Blocks until the drive confirms all pending writes are persisted.
	 * This is a no-op for drives that do not have a write-back cache.
	 *
	 * @return
	 * @retval WDM_OK on success
	 * @retval WDM_ERR_IO on hardware failure
	 * @retval WDM_ERR_TIMEOUT if the flush did not complete in time
	 */
	WDM_Status WDM_Flush(WDM_DriveHandle handle);

	/**
	 * @brief Initialise the drive manager subsystem
	 *
	 * Must be called once before any other WDM function. Initialises
	 * internal structures and prepares interrupt routing.
	 *
	 * @return
	 * @retval WDM_OK on success
	 * @retval WDM_ERR_IO if hardware initialisation failed
	 */
	WDM_Status WDM_Init(void);

	/**
	 * @brief Tear down the drive manager
	 *
	 * Flushes all registered drives, cancels pending async I/O, and unregisters all drivers.
	 * After this call, all handles are invalid.
	 */
	void WDM_Shutdown(void);

	/**
 * @brief Driver operations vtable for drivers to bind to.
 *
 * A driver fills out this struct and passes it to WDM_Register().
 * The manager holds a pointer to the ops table for the lifetime of
 * the registration, so the struct must remain valid until WDM_Unregister()
 * returns.
 *
 * Required function pointers (@p read and @p write) must never be NULL.
 * All optional pointers may be set to NULL to indicate no support;
 * the manager will handle the fallback gracefully.
 */
	typedef struct {
		/**
		 * @brief Read sectors from the device. **Required.**
		 *
		 * @param ctx   Opaque driver context supplied at registration.
		 * @param lba   Starting logical block address.
		 * @param count Number of sectors to read.
		 * @param buf   Destination buffer; at least @p count * sector_size bytes,
		 *              aligned to sector_size.
		 * @param flags I/O flags (e.g. WDM_FLAG_DMA, WDM_FLAG_NO_CACHE).
		 *
		 * @return WDM_OK on success, or an appropriate WDM_ERR_* code.
		 */
		WDM_Status(*read)(void* ctx, WDM_LBA lba, uint32_t count, void* buf, WDM_IOFlags flags);

		/**
		 * @brief Write sectors to the device. **Required.**
		 *
		 * Without WDM_FLAG_SYNC the driver may return once data reaches the
		 * device's internal buffer. With WDM_FLAG_SYNC the driver must block
		 * until the device confirms the data is on stable storage.
		 *
		 * @param ctx   Opaque driver context supplied at registration.
		 * @param lba   Starting logical block address.
		 * @param count Number of sectors to write.
		 * @param buf   Source buffer; at least @p count * sector_size bytes.
		 * @param flags I/O flags (e.g. WDM_FLAG_SYNC, WDM_FLAG_DMA).
		 *
		 * @return WDM_OK on success, or an appropriate WDM_ERR_* code.
		 */
		WDM_Status(*write)(void* ctx, WDM_LBA lba, uint32_t count, const void* buf, WDM_IOFlags flags);

		/**
		 * @brief Flush the device write-back cache to stable storage. **Optional.**
		 *
		 * Should issue the appropriate flush command (e.g. ATA FLUSH CACHE,
		 * NVMe Flush) and block until the device confirms completion.
		 * Drivers for devices without a write-back cache may either set this
		 * to NULL or return WDM_OK immediately.
		 *
		 * Set to NULL if unsupported; the manager treats NULL as a no-op.
		 *
		 * @param ctx Opaque driver context supplied at registration.
		 *
		 * @return WDM_OK on success.
		 * @retval WDM_ERR_IO      on hardware failure.
		 * @retval WDM_ERR_TIMEOUT if the flush did not complete in time.
		 */
		WDM_Status(*flush)(void* ctx);

		/**
		 * @brief Issue a discard/TRIM hint for a sector range. **Optional.**
		 *
		 * Maps to ATA TRIM or NVMe Deallocate. This is a hint only; the
		 * device may ignore it. Drivers for devices that do not support
		 * discard (e.g. HDDs) should set this to NULL.
		 *
		 * Set to NULL if unsupported; the manager will return WDM_OK silently.
		 *
		 * @param ctx   Opaque driver context supplied at registration.
		 * @param lba   Starting logical block address of the discarded range.
		 * @param count Number of sectors in the range.
		 *
		 * @return WDM_OK on success or if the hint was silently ignored.
		 * @retval WDM_ERR_IO on hardware failure.
		 */
		WDM_Status(*trim)(void* ctx, WDM_LBA lba, uint32_t count);

		/**
		 *
		 * @brief Called synchronously by WDM_Register() after the slot is
		 *        reserved. **Optional.**
		 *
		 * Use this to power on the device, perform reset sequences, or
		 * allocate driver-private resources. If this returns anything other
		 * than WDM_OK, registration is aborted and the slot is freed.
		 *
		 * Set to NULL if no attach-time initialisation is required.
		 *
		 * @param ctx Opaque driver context supplied at registration.
		 *
		 * @return WDM_OK to allow registration to proceed, or any WDM_ERR_*
		 *         code to abort.
		 */
		WDM_Status(*on_attach)(void* ctx);

		/**
		 * @brief Called synchronously by WDM_Unregister() after the final
		 *        flush completes. **Optional.**
		 *
		 * Use this to power down the device or release driver-private
		 * resources. The handle is invalidated immediately after this call
		 * returns, so no further I/O will be issued through @p ctx.
		 *
		 * Set to NULL if no detach-time cleanup is required.
		 *
		 * @param ctx Opaque driver context supplied at registration.
		 */
		void (*on_detach)(void* ctx);

#ifdef WALLOS_WDM_ASYNC_IO
		/**
		 * @brief Initiate an asynchronous sector read. **Optional.**
		 *
		 * The call must return immediately after queuing the transfer.
		 * @p cb is invoked (with @p user_data) once the transfer completes
		 * or fails. The caller guarantees @p buf remains valid until the
		 * callback fires.
		 *
		 * Set to NULL if the driver does not support async I/O; the manager
		 * will fall back to the synchronous @p read path.
		 *
		 * @param ctx       Opaque driver context.
		 * @param lba       Starting logical block address.
		 * @param count     Number of sectors to read.
		 * @param buf       Destination buffer (must outlive the operation).
		 * @param flags     I/O flags.
		 * @param cb        Completion callback.
		 * @param user_data Caller context forwarded to @p cb.
		 *
		 * @return WDM_OK if the transfer was queued successfully.
		 * @retval WDM_ERR_BUSY if the driver's queue is full.
		 */
		WDM_Status(*read_async)(void* ctx, WDM_LBA lba, uint32_t count,
			void* buf, WDM_IOFlags flags,
			WDM_IOCallback cb, void* user_data);

		/**
		 * @brief Initiate an asynchronous sector write. **Optional.**
		 *
		 * Symmetric to @p read_async. The caller guarantees @p buf remains
		 * valid until @p cb fires.
		 *
		 * Set to NULL if the driver does not support async I/O; the manager
		 * will fall back to the synchronous @p write path.
		 *
		 * @param ctx       Opaque driver context.
		 * @param lba       Starting logical block address.
		 * @param count     Number of sectors to write.
		 * @param buf       Source buffer (must outlive the operation).
		 * @param flags     I/O flags.
		 * @param cb        Completion callback.
		 * @param user_data Caller context forwarded to @p cb.
		 *
		 * @return WDM_OK if the transfer was queued successfully.
		 * @retval WDM_ERR_BUSY if the driver's queue is full.
		 */
		WDM_Status(*write_async)(void* ctx, WDM_LBA lba, uint32_t count,
			const void* buf, WDM_IOFlags flags,
			WDM_IOCallback cb, void* user_data);
#endif /* WALLOS_WDM_ASYNC_IO */
	} WDM_DriverOps;

	/**
	 * @brief Register a drive with the manager
	 *
	 * The manager calls `ops->on_attach(ctx)` synchronously before returning.
	 * If on_attach returns a non-OK status, registration fails.
	 *
	 * @param ops pointer to the driver's vtable (must remain valid)
	 * @param ctx opaque driver context
	 * @param info static geometry / capability information
	 * @param out receives the handle on success
	 *
	 * @return
	 * @retval WDM_OK on success
	 * @retval WDM_ERR_INVALID if ops, info, or out is NULL
	 * @retval WDM_ERR_IO if on_attach fails
	 */
	WDM_Status WDM_Register(
		const WDM_DriverOps* ops,
		void* ctx,
		const WDM_DriveInfo* info,
		WDM_DriveHandle* out
	);

	/**
	 * @brief Remove a drive from the manager
	 *
	 * Flushes any pending I/O, calls `ops->on_detach(ctx)`, and
	 * invalidates `handle`. After this call, `handle` must not be used.
	 *
	 * @param handle drive handle to unregister
	 *
	 * @return
	 * @retval WDM_OK on success
	 * @retval WDM_ERR_NOT_FOUND if handle is not registered
	 * @retval WDM_ERR_IO if the final flush failed
	 */
	WDM_Status WDM_Unregister(WDM_DriveHandle handle);

	/**
	 * @brief Register a name to the given drive.
	 * This name is copied into an internal structure, and will be destroyed on WDM_UnregisterName() or WDM_Unregister() on the drive.
	 *
	 * @param handle Handle of the drive to name.
	 * @param name String of the drive name. Anything over 32 length will be truncated.
	 * @retval WDM_OK on sucess
	 * @retval WDM_ERR_NOT_FOUND if handle is not found
	 * @retval WDM_ERR_ALREADY_EXISTS if name is already attached to another drive, or this drive already has a name
	 */
	WDM_Status WDM_RegisterName(WDM_DriveHandle handle, const char* name);

	/**
	 * @brief Unregister the name for the provided handle.
	 * This function will return OK even there was no name associated with the drive.
	 *
	 * @param handle handle to remove the registered name from
	 * @retavl WDM_OK on success
	 * @retval WDM_ERR_INVALID if handle is invalid
	 */
	WDM_Status WDM_UnregisterName(WDM_DriveHandle handle);

	/**
	 * @brief Get the drive with the given name.
	 *
	 * @param name Name of the drive you want to retrieve
	 * @return handle of the drive if found, NULL otherwise
	 */
	WDM_DriveHandle WDM_GetDriveFromName(const char* name);

	/**
	 * @brief Copies the name of the given drive into the output buffer, if the drive exists and there is a name associated with it.
	 *
	 * WDM stores drive names of at most 32 characters long.
	 *
	 * @param handle Drive to get the name of
	 * @param name_out Provided string to copy the name into
	 * @param len Length of the provided buffer
	 * @retval WDM_OK if successfully copied.
	 * @retval WDM_ERR_INVALID if handle is invalid
	 * @retval WDM_ERR_OVERFLOW if buffer is not large enough to get the entire name
	 */
	WDM_Status WDM_GetNameFromDrive(WDM_DriveHandle handle, char* name_out, uint8_t len);

	/**
	 * @brief List all currently registered drive handles
	 *
	 * Fills `handles` with up to `max` entries and stores the total registered count in `*total` (even if it exceeds `max`).
	 * Pass `handles = NULL` and `max = 0` to query the count only.
	 *
	 * @param handles output array of handles (or NULL)
	 * @param max maximum number of entries to fill
	 * @param total receives total registered count
	 *
	 * @return WDM_OK always
	 */
	WDM_Status WDM_Enumerate(
		WDM_DriveHandle* handles,
		uint32_t         max,
		uint32_t* total
	);

	/**
	 * @brief Retrieve geometry and capability info for a drive
	 *
	 * @param handle drive handle
	 * @param out output info struct
	 *
	 * @return
	 * @retval WDM_OK on success
	 * @retval WDM_ERR_NOT_FOUND if handle is invalid
	 */
	WDM_Status WDM_GetInfo(
		WDM_DriveHandle handle,
		WDM_DriveInfo* out
	);

#ifdef WALLOS_WDM_DMA
	/**
	 * @brief Allocate a DMA-safe buffer
	 *
	 * Allocates `size` bytes suitable for DMA transfers.
	 *
	 * @param size allocation size in bytes
	 * @param phys_out receives physical address
	 *
	 * @return pointer to buffer, or NULL on failure
	 */
	void* WDM_DMAAlloc(size_t size, uintptr_t* phys_out);

	/**
	 * @brief Release a DMA buffer
	 *
	 * @param buf buffer pointer
	 * @param size size of allocation
	 */
	void WDM_DMAFree(void* buf, size_t size);
#endif // WALLOS_WDM_DMA
#ifdef WALLOS_WDM_ASYNC_IO
	/** Completion callback used by async I/O. */
	typedef void (*WDM_IOCallback)(
		WDM_DriveHandle handle,
		WDM_Status      status,
		void* user_data
		);

	/**
	 * @brief Asynchronous sector read
	 *
	 * Initiates a read and returns immediately.
	 *
	 * @param handle drive handle
	 * @param lba starting LBA
	 * @param count sector count
	 * @param buf destination buffer
	 * @param flags I/O flags
	 * @param callback completion callback
	 * @param user_data user context
	 *
	 * @return
	 * @retval WDM_OK if queued
	 * @retval WDM_ERR_BUSY if queue full
	 * @retval (other errors) same as WDM_Read
	 */
	WDM_Status WDM_ReadAsync(
		WDM_DriveHandle handle,
		WDM_LBA         lba,
		uint32_t        count,
		void* buf,
		WDM_IOFlags     flags,
		WDM_IOCallback  callback,
		void* user_data
	);

	/**
	 * @brief Asynchronous sector write
	 *
	 * Initiates a write and returns immediately.
	 *
	 * @param handle drive handle
	 * @param lba starting LBA
	 * @param count sector count
	 * @param buf source buffer
	 * @param flags I/O flags
	 * @param callback completion callback
	 * @param user_data user context
	 *
	 * @return
	 * @retval WDM_OK if queued
	 * @retval WDM_ERR_BUSY if queue full
	 * @retval (other errors) same as WDM_Write
	 */
	WDM_Status WDM_WriteAsync(
		WDM_DriveHandle handle,
		WDM_LBA         lba,
		uint32_t        count,
		const void* buf,
		WDM_IOFlags     flags,
		WDM_IOCallback  callback,
		void* user_data
	);
#endif

	typedef struct {
		uint8_t  type_guid[16];
		uint8_t  unique_guid[16];
		char     partition_name[72]; // Sized to hold 36 UTF-16 chars / 72 bytes
		uint64_t attributes;
	} WDM_PartitionMeta;

	/**
	 * @brief Register a partition associated with a parent drive.
	 *
	 * Behaves like WDM_Register, but links the new drive handle to a parent.
	 * If the parent is unregistered (via WDM_Unregister), this partition will automatically be torn down.
	 *
	 * @param parent handle of the parent drive
	 * @param ops    pointer to the driver's vtable
	 * @param ctx    opaque driver context
	 * @param info   partition geometry / capability information
	 * @param out    receives the handle on success
	 */
	WDM_Status WDM_RegisterPartition(
		WDM_DriveHandle      parent,
		const WDM_DriverOps* ops,
		void* ctx,
		const WDM_DriveInfo* info,
		WDM_DriveHandle* out
	);

	/**
	 * @brief Creates a logical block device mapped to a sub-region of a parent drive.
	 *
	 * @param parent The physical drive handle.
	 * @param start  Starting LBA of the partition.
	 * @param length Number of sectors in the partition.
	 * @param meta   Optional partition metadata (e.g., from GPT). Pass NULL for MBR/Raw.
	 * @return WDM_DriveHandle The logical drive handle, or NULL on failure.
	 */
	WDM_DriveHandle WDM_AddPartition(WDM_DriveHandle parent, WDM_LBA start, WDM_LBA length, const WDM_PartitionMeta* meta);

	/**
	 * @brief Retrieves partition-specific metadata if the handle is a logical partition.
	 *
	 * @param handle The drive handle.
	 * @param meta   Pointer to the metadata structure to populate.
	 * @return WDM_Status WDM_OK if metadata was populated, WDM_ERR_UNSUPPORTED if
	 *                    the handle is a physical drive or lacks metadata.
	 */
	WDM_Status WDM_GetPartitionMetadata(WDM_DriveHandle handle, WDM_PartitionMeta* meta);

	// We forward declare this here. This is actually in the device manager
	struct wallos_device;

	/**
	 * Scans the parent drive for MBR/GPT partition entries, and mounts any new ones that are not already registered.
	 *
	 * @param wdm_parent Parent drive to scan
	 * @param dev_parent Parent device
	 * @retval WDM_ERR_INVALID if parameters are null
	 * @retval WDM_OK on success
	 */
	WDM_Status WDM_RescanPartitions(WDM_DriveHandle wdm_parent, struct wallos_device* dev_parent);

	/**
	 * Scans the parent drive for MBR/GPT partition entries, and mounts them if they exist.
	 * It will also register them with the device registry, as children of the provided parent.
	 *
	 * @param wdm_parent Parent drive to scan
	 * @param dev_parent Parent device
	 * @retval WDM_ERR_INVALID
	 * @retval
	 */
	WDM_Status WDM_ScanAndRegisterPartitions(WDM_DriveHandle wdm_parent, struct wallos_device* dev_parent);

	// This needs to be given more thought. This is really left up to the individual drivers.
	// Maybe we add a field to DeviceInfo relating it to a device
	// WDM_DriveHandle WDM_GetDriveByDevice(struct wallos_device* dev);

	/**
	 * @brief List all logical partitions registered under a parent drive.
	 *
	 * Fills `handles` with up to `max` entries and stores the total partition count in `*total` (even if it exceeds `max`).
	 * Pass `handles = NULL`  and `max = 0` to query the count only.
	 *
	 * @param parent  The parent drive handle (e.g., the physical disk).
	 * @param handles output array of partition handles (or NULL)
	 * @param max     maximum number of entries to fill
	 * @param total   receives total partition count
	 * @retval WDM_OK on success
	 * @retval WDM_ERR_INVALID if parent is invalid
	 */
	WDM_Status WDM_EnumeratePartitions(
		WDM_DriveHandle  parent,
		WDM_DriveHandle* handles,
		uint32_t         max,
		uint32_t* total
	);

#ifdef __cplusplus
}
#endif
#endif // WALLOS_WDM_H