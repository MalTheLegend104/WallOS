/**
 * Wallos WDM (WallosOS Disk Manager) implementation.
 *
 * @note This is NOT thread safe. The registry should have spinlocks.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <filesystem/wdm.h>
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Registry
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/** Maximum number of concurrently registered drives. */
#ifndef WDM_MAX_DRIVES
#define WDM_MAX_DRIVES 32
#endif

/** Internal representation of a registered drive. */
struct WDM_Drive {
	bool active;
	const WDM_DriverOps* ops;
	void* ctx;
	WDM_DriveInfo info;
};

// The registry is statically allocated. 
// This has both advantages and disadvantages.
// I dont see this being a problem (32 drives is plenty), but the cap could in theory be a problem.
static struct WDM_Drive wdm_drives[WDM_MAX_DRIVES];
static bool             wdm_initialized = false;

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Internal helpers
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static bool validate_handle(WDM_DriveHandle handle) {
	if (!handle) {
		return false;
	}

	/* Confirm the pointer falls within the static array. */
	if (handle < wdm_drives || handle >= wdm_drives + WDM_MAX_DRIVES) {
		return false;
	}

	return handle->active;
}

static WDM_Status flush_drive(struct WDM_Drive* d) {
	if (!d->ops->flush) {
		return WDM_OK;
	}

	WDM_Status st = d->ops->flush(d->ctx);

	if (st == WDM_ERR_UNSUPPORTED) {
		return WDM_OK;
	}

	return st;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

WDM_Status WDM_Init(void) {
	if (wdm_initialized) {
		return WDM_OK;
	}

	memset(wdm_drives, 0, sizeof(wdm_drives));
	wdm_initialized = true;
	return WDM_OK;
}

void WDM_Shutdown(void) {
	if (!wdm_initialized) {
		return;
	}

	for (int i = 0; i < WDM_MAX_DRIVES; i++) {
		struct WDM_Drive* d = &wdm_drives[i];

		if (!d->active) {
			continue;
		}

		// This is our attempt at being nice
		// we dont care about error state because if we're shutting down something went wrong 
		// (or we've already shutdown VFS which flushed everything anyway)
		flush_drive(d);

		if (d->ops->on_detach) {
			d->ops->on_detach(d->ctx);
		}

		memset(d, 0, sizeof(*d));
	}

	wdm_initialized = false;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Registration
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

WDM_Status WDM_Register(const WDM_DriverOps* ops, void* ctx, const WDM_DriveInfo* info, WDM_DriveHandle* out) {
	if (!ops || !info || !out) {
		return WDM_ERR_INVALID;
	}

	/* read and write are mandatory. */
	if (!ops->read || !ops->write) {
		return WDM_ERR_INVALID;
	}

	/* Find a free slot. */
	struct WDM_Drive* slot = NULL;

	for (int i = 0; i < WDM_MAX_DRIVES; i++) {
		if (!wdm_drives[i].active) {
			slot = &wdm_drives[i];
			break;
		}
	}

	if (!slot) {
		/* Registry is full. */
		return WDM_ERR_BUSY;
	}

	slot->ops = ops;
	slot->ctx = ctx;
	slot->info = *info;

	if (ops->on_attach) {
		WDM_Status st = ops->on_attach(ctx);

		if (st != WDM_OK) {
			/* Attachment failed, we leave slot zeroed. */
			memset(slot, 0, sizeof(*slot));
			return WDM_ERR_IO;
		}
	}

	slot->active = true;
	*out = slot;
	return WDM_OK;
}

WDM_Status WDM_Unregister(WDM_DriveHandle handle) {
	if (!validate_handle(handle)) {
		return WDM_ERR_NOT_FOUND;
	}

	WDM_Status flush_st = flush_drive(handle);

	if (handle->ops->on_detach) {
		handle->ops->on_detach(handle->ctx);
	}

	memset(handle, 0, sizeof(*handle));

	/* Return flush errors after completing teardown so the slot is always cleaned up regardless of I/O health. */
	return flush_st;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Enumeration and info
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

WDM_Status WDM_Enumerate(WDM_DriveHandle* handles, uint32_t max, uint32_t* total) {
	if (!total) {
		return WDM_ERR_INVALID;
	}

	uint32_t count = 0;

	for (int i = 0; i < WDM_MAX_DRIVES; i++) {
		if (!wdm_drives[i].active) {
			continue;
		}

		if (handles && count < max) {
			handles[count] = &wdm_drives[i];
		}

		count++;
	}

	*total = count;
	return WDM_OK;
}

WDM_Status WDM_GetInfo(WDM_DriveHandle handle, WDM_DriveInfo* out) {
	if (!validate_handle(handle) || !out) {
		return WDM_ERR_NOT_FOUND;
	}

	*out = handle->info;
	return WDM_OK;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// I/O helpers
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

WDM_Status check_io_args(WDM_DriveHandle handle, WDM_LBA lba, uint32_t count, const void* buf, bool check_buf) {
	if (!validate_handle(handle)) {
		return WDM_ERR_INVALID;
	}

	if (check_buf && !buf) {
		return WDM_ERR_INVALID;
	}

	// Zero-length transfer
	// nothing to do, not an error
	if (count == 0) return WDM_OK;

	// Check for overflow
	// ensure lba + count does not wrap and does not exceed the drive's sector_count
	if (lba >= handle->info.sector_count) {
		return WDM_ERR_OVERFLOW;
	}

	uint64_t end = lba + (uint64_t) count;

	if (end > handle->info.sector_count) {
		return WDM_ERR_OVERFLOW;
	}

	return WDM_OK;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Read / Write / Trim / Flush
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

WDM_Status WDM_Read(WDM_DriveHandle handle, WDM_LBA lba, uint32_t count, void* buf, WDM_IOFlags flags) {
	WDM_Status st = check_io_args(handle, lba, count, buf, true);
	if (st != WDM_OK) return st;
	if (count == 0) return WDM_OK;

	if (!handle->info.dma_capable && (flags & WDM_FLAG_DMA)) {
		flags = (WDM_IOFlags) (flags & ~WDM_FLAG_DMA);
	}

	return handle->ops->read(handle->ctx, lba, count, buf, flags);
}

WDM_Status WDM_Write(WDM_DriveHandle handle, WDM_LBA lba, uint32_t count, const void* buf, WDM_IOFlags flags) {
	WDM_Status st = check_io_args(handle, lba, count, buf, true);

	if (st != WDM_OK) return st;
	if (handle->info.read_only) return WDM_ERR_WRITE_PROT;
	if (count == 0) return WDM_OK;


	if (!handle->info.dma_capable && (flags & WDM_FLAG_DMA)) {
		flags = (WDM_IOFlags) (flags & ~WDM_FLAG_DMA);
	}

	return handle->ops->write(handle->ctx, lba, count, buf, flags);
}

WDM_Status WDM_Trim(WDM_DriveHandle handle, WDM_LBA lba, uint32_t count) {
	WDM_Status st = check_io_args(handle, lba, count, NULL, false);

	if (st != WDM_OK) return st;
	if (count == 0) return WDM_OK;

	/*
	 * TRIM is a hint.
	 * If the driver does not implement it, return OK as the spec specifies "silently ignored."
	 */
	if (!handle->ops->trim) {
		return WDM_OK;
	}

	return handle->ops->trim(handle->ctx, lba, count);
}

WDM_Status WDM_Flush(WDM_DriveHandle handle) {
	if (!validate_handle(handle)) {
		return WDM_ERR_NOT_FOUND;
	}

	return flush_drive(handle);
}