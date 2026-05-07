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

#ifdef __cplusplus
}
#endif
#endif // WALLOS_WDM_H