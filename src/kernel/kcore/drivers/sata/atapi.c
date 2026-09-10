#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include <drivers/serial.h>
#include <drivers/sata/ahci.h>
#include <drivers/sata/atapi.h>
#include <drivers/scsi.h>

#include <memory/kernel_alloc.h>

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// SCSI
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

typedef struct {
	ahci_port_t* port;
	uint32_t     slot_count;
} atapi_ctx_t;

static int atapi_scsi_exec(void* ctx_, const uint8_t* cdb, uint8_t cdb_len, void* buf, uint32_t buf_len, scsi_data_dir_t dir) {
	atapi_ctx_t* c = (atapi_ctx_t*) ctx_;
	int is_write = (dir == SCSI_DIR_WRITE);
	return ahci_issue_atapi(c->port, c->slot_count, cdb, cdb_len, buf, buf_len, is_write);
}

static void atapi_make_scsi_dev(scsi_device_t* dev, atapi_ctx_t* ctx) {
	scsi_device_init(dev, atapi_scsi_exec, ctx, AHCI_ATAPI_CDB_LEN);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// IDENTIFY PACKET DEVICE & Capacity
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// These should probably be broken out into the header, they are the same as regular ahci identify...
#define ATAPI_IDENT_SERIAL_OFF 10  // words 10-19
#define ATAPI_IDENT_MODEL_OFF  27  // words 27-46

#define ATAPI_DEFAULT_BLOCK_SIZE 2048  // Standard CD/DVD sector size

// I basically copied this from AHCI port_identify() so if something looks weird it's because of that
int atapi_identify(ahci_port_t* port, uint32_t slot_count) {
	if (!port) return -1;

	static uint16_t id_buf[256];
	memset(id_buf, 0, sizeof(id_buf));

	ahci_fis_h2d_t fis;
	memset(&fis, 0, sizeof(fis));
	fis.fis_type = AHCI_FIS_TYPE_H2D;
	fis.pmport_c = AHCI_FIS_H2D_C_BIT;
	fis.command = ATA_CMD_IDENTIFY_PACKET;
	fis.device = 0;

	if (ahci_issue_ata(port, slot_count, &fis, id_buf, 512, 0) != 0) {
		printf_serial("\r\n[ATAPI][ERROR] port %u IDENTIFY PACKET DEVICE failed\r\n", port->port_idx);
		return -1;
	}

	swap_ata_string(&id_buf[ATAPI_IDENT_MODEL_OFF], port->model, 20);
	swap_ata_string(&id_buf[ATAPI_IDENT_SERIAL_OFF], port->serial, 10);

	printf_serial("(model='%s' serial='%s') ", port->model, port->serial);

	// Capacity doesn't come from IDENTIFY for ATAPI
	// We ask the SCSI layer to get that for us
	atapi_ctx_t ctx = { .port = port, .slot_count = slot_count };
	scsi_device_t scsi;
	atapi_make_scsi_dev(&scsi, &ctx);

	// Freshly powered drives sometimes need TEST UNIT READY (or a couple) before they will answer reliably.
	// Failure here isn't fatal bc of that, and an empty drive isn't a huge deal
	scsi_test_unit_ready(&scsi);

	uint64_t last_lba = 0;
	uint32_t block_size = ATAPI_DEFAULT_BLOCK_SIZE;
	if (scsi_read_capacity10(&scsi, &last_lba, &block_size) == 0 && block_size != 0) {
		port->sector_count = last_lba + 1;
		port->sector_size = (uint16_t) block_size;
		printf_serial("(sectors=%llu, block_size=%u) ", (unsigned long long) port->sector_count, block_size);
	} else {
		port->sector_count = 0;
		port->sector_size = (uint16_t) ATAPI_DEFAULT_BLOCK_SIZE;
		printf_serial("(no media / capacity unavailable) ");
	}

	return 0;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// WDM Layer
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

typedef struct {
	ahci_port_t* port;
	uint32_t     slot_count;
} atapi_wdm_ctx_t;

// READ(10)/WRITE(10)'s transfer length field is 16 bits, and a single AHCI PRD entry is 4MiB at most.
// We split it to deal with that.
static uint32_t atapi_max_sectors_per_chunk(uint32_t block_size) {
	if (block_size == 0) block_size = ATAPI_DEFAULT_BLOCK_SIZE;
	uint32_t by_prd = AHCI_PRD_MAX_BYTES / block_size;
	uint32_t by_cdb = 0xFFFF;
	return (by_prd < by_cdb) ? by_prd : by_cdb;
}

static WDM_Status atapi_wdm_read(void* ctx_, WDM_LBA lba, uint32_t count, void* buf, WDM_IOFlags flags) {
	(void) flags; // TODO: Polling only for now
	atapi_wdm_ctx_t* c = (atapi_wdm_ctx_t*) ctx_;
	ahci_port_t* port = c->port;
	if (!port || !port->present || !buf || count == 0) return WDM_ERR_INVALID;

	atapi_ctx_t tctx = { .port = port, .slot_count = c->slot_count };
	scsi_device_t scsi;
	atapi_make_scsi_dev(&scsi, &tctx);

	uint32_t block_size = port->sector_size ? port->sector_size : ATAPI_DEFAULT_BLOCK_SIZE;
	uint32_t max_chunk = atapi_max_sectors_per_chunk(block_size);
	uint8_t* out = (uint8_t*) buf;

	while (count > 0) {
		uint32_t chunk = (count > max_chunk) ? max_chunk : count;
		if (scsi_read10(&scsi, (uint32_t) lba, (uint16_t) chunk, block_size, out) != 0) return WDM_ERR_IO;

		out += (size_t) chunk * block_size;
		lba += chunk;
		count -= chunk;
	}
	return WDM_OK;
}

static WDM_Status atapi_wdm_write(void* ctx_, WDM_LBA lba, uint32_t count, const void* buf, WDM_IOFlags flags) {
	(void) ctx_; (void) lba; (void) count; (void) buf; (void) flags;
	// Everything is read only. 
	// Technically we *could* write, if we have a CD/DVD burner
	// That's a problem for the future (if I even decide that's worth it)
	return WDM_ERR_WRITE_PROT;
}

static void atapi_wdm_on_detach(void* ctx_) {
	// Port memory is owned by ahci_ctrl_t and freed by ahci_detach()
	// All we own here is the context struct itself
	kfree(ctx_);
}

static const WDM_DriverOps atapi_wdm_ops = {
	.read = atapi_wdm_read,
	.write = atapi_wdm_write,
	.flush = NULL,
	.trim = NULL,
	.on_attach = NULL,
	.on_detach = atapi_wdm_on_detach,
};

WDM_DriveHandle atapi_wdm_register_port(ahci_port_t* port, uint32_t slot_count) {
	if (!port) return NULL;

	atapi_wdm_ctx_t* ctx = (atapi_wdm_ctx_t*) kalloc(sizeof(atapi_wdm_ctx_t));
	if (!ctx) {
		printf_serial("[ATAPI][ERROR] port %u: failed to alloc WDM context\r\n", port->port_idx);
		return NULL;
	}
	ctx->port = port;
	ctx->slot_count = slot_count;

	WDM_DriveInfo info;
	memset(&info, 0, sizeof(info));
	info.sector_count = port->sector_count;
	info.sector_size = port->sector_size;
	info.physical_sector = port->sector_size;
	info.optimal_xfer = 32; // 64K at 2048B/sector, conservative
	info.removable = true;
	info.read_only = true;
	info.dma_capable = true;
	strncpy(info.model, port->model, sizeof(info.model) - 1);
	strncpy(info.serial, port->serial, sizeof(info.serial) - 1);

	WDM_DriveHandle handle = NULL;
	WDM_Status st = WDM_Register(&atapi_wdm_ops, ctx, &info, &handle);
	if (st != WDM_OK) {
		printf_serial("[ATAPI][ERROR] port %u: WDM_Register failed (%d)\r\n", port->port_idx, (int) st);
		kfree(ctx);
		return NULL;
	}

	printf_serial("[ATAPI] port %u registered with WDM\r\n", port->port_idx);
	return handle;
}