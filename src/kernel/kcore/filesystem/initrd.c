/*
 * initrd_wdm.cpp — WDM driver for the initrd RAM disk.
 *
 * The initrd is a 2 MB FAT12 blob loaded into memory by the bootloader and
 * exposed via the linker symbols _initrd_data / _initrd_size.
 *
 * Call initrd_wdm_init() once during early boot (after WDM_Init()) to
 * register the device with the WDM and obtain a handle suitable for passing
 * to mount_drive() / ff_register_drive().
 *
 * Writes are permitted and go back into the RAM buffer, but do not persist
 * across reboots.  Pass INITRD_FLAG_READ_ONLY to initrd_wdm_init() if you
 * want WDM to reject writes at the driver level.
 */

#include <filesystem/initrd.h>
#include <string.h>

// -------------------------------------------------------------------------
// Externals provided by the bootloader / linker script
// -------------------------------------------------------------------------

extern uint8_t* _initrd_data;
extern uint64_t  _initrd_size;

// -------------------------------------------------------------------------
// Driver context
// -------------------------------------------------------------------------

typedef struct {
	uint8_t* data;
	uint64_t  size;        // total bytes
	uint32_t  sector_size; // always 512 for FAT12
} initrd_ctx_t;

// One static instance — there is only ever one initrd.
static initrd_ctx_t initrd_ctx;

// -------------------------------------------------------------------------
// WDM_DriverOps callbacks
// -------------------------------------------------------------------------

static WDM_Status initrd_on_attach(void* ctx) {
	initrd_ctx_t* c = (initrd_ctx_t*) ctx;

	if (!c->data || c->size == 0) {
		return WDM_ERR_NO_MEDIA;
	}

	// Sector size must divide the blob evenly.
	if (c->size % c->sector_size != 0) {
		return WDM_ERR_INVALID;
	}

	return WDM_OK;
}

static void initrd_on_detach(void* ctx) {
	// Nothing to free — the blob is owned by the bootloader.
	(void) ctx;
}

static WDM_Status initrd_read(void* ctx, WDM_LBA lba, uint32_t count, void* buf, WDM_IOFlags flags) {
	(void) flags;
	initrd_ctx_t* c = (initrd_ctx_t*) ctx;

	uint64_t sector_count = c->size / c->sector_size;
	if (lba + count > sector_count) return WDM_ERR_OVERFLOW;

	memcpy(buf, c->data + (lba * c->sector_size), (size_t) (count * c->sector_size));
	return WDM_OK;
}

static WDM_Status initrd_write(void* ctx, WDM_LBA lba, uint32_t count,
	const void* buf, WDM_IOFlags flags) {
	(void) flags;
	initrd_ctx_t* c = (initrd_ctx_t*) ctx;

	uint64_t sector_count = c->size / c->sector_size;
	if (lba + count > sector_count) return WDM_ERR_OVERFLOW;

	memcpy(c->data + (lba * c->sector_size), buf, (size_t) (count * c->sector_size));
	return WDM_OK;
}

// flush / trim are no-ops for a RAM disk.
static WDM_Status initrd_flush(void* ctx) {
	(void) ctx;
	return WDM_OK;
}

// -------------------------------------------------------------------------
// Driver ops tables — one for each read-write mode
// -------------------------------------------------------------------------

static const WDM_DriverOps initrd_ops_rw = {
	.read = initrd_read,
	.write = initrd_write,
	.flush = initrd_flush,
	.trim = NULL,
	.on_attach = initrd_on_attach,
	.on_detach = initrd_on_detach,
};

static const WDM_DriverOps initrd_ops_ro = {
	.read = initrd_read,
	.write = NULL,          // WDM will return WDM_ERR_WRITE_PROT
	.flush = initrd_flush,
	.trim = NULL,
	.on_attach = initrd_on_attach,
	.on_detach = initrd_on_detach,
};

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

WDM_DriveHandle initrd_wdm_init(initrd_flags_t flags) {
	initrd_ctx.data = _initrd_data;
	initrd_ctx.size = _initrd_size;
	initrd_ctx.sector_size = 512;

	bool read_only = (flags & INITRD_FLAG_READ_ONLY) != 0;

	WDM_DriveInfo info = {};
	info.sector_count = initrd_ctx.size / initrd_ctx.sector_size;
	info.sector_size = initrd_ctx.sector_size;
	info.physical_sector = initrd_ctx.sector_size;
	info.optimal_xfer = 1;
	info.removable = false;
	info.read_only = read_only;
	info.dma_capable = false;
	strncpy(info.model, "initrd RAM disk", sizeof(info.model) - 1);
	strncpy(info.serial, "INITRD-0", sizeof(info.serial) - 1);

	const WDM_DriverOps* ops = read_only ? &initrd_ops_ro : &initrd_ops_rw;

	WDM_DriveHandle handle = NULL;
	WDM_Status st = WDM_Register(ops, &initrd_ctx, &info, &handle);
	if (st != WDM_OK) return NULL;

	return handle;
}