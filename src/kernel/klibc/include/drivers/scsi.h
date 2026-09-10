#ifndef WALLOS_SCSI_H
#define WALLOS_SCSI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------------------------------------------
// This is the transport-agnostic SCSI command helpers
// It builds CBD, parses responses, and handles sense recovery.
// Transports provide scsi_exec_fn in scsi_device_t, allowing the same helpers to work regardless of what is using SCSI (ATAPI, USB, SAS, etc.)
// ------------------------------------------------------------------------------------------------

	typedef enum {
		SCSI_DIR_NONE = 0,  // No data phase (like TEST UNIT READY)
		SCSI_DIR_READ = 1,  // Device -> host
		SCSI_DIR_WRITE = 2, // Host -> device
	} scsi_data_dir_t;

	// Sense data, trimmed down to what we actually care about
	typedef struct {
		uint8_t valid; // 1 if this was actually populated
		uint8_t sense_key;
		uint8_t asc;
		uint8_t ascq;
	} scsi_sense_t;

	/*
	 * Transport callback
	 *
	 * Send a CDB with an optional data buffer and wait for completion
	 *
	 * Sense recovery is handled generically by this layer.
	 *
	 * Returns 0 on success, -1 on failure. Caller doesn't really care how it failed, just that it did.
	 */
	typedef int (*scsi_exec_fn)(void* ctx, const uint8_t* cdb, uint8_t cdb_len, void* buf, uint32_t buf_len, scsi_data_dir_t dir);

	typedef struct {
		scsi_exec_fn  exec;
		void* ctx; // Opaque, owned by the transport (for example, an AHCI port + slot count)
		uint8_t cdb_len; // Fixed CDB size the transport expects (12 for ATAPI, 31 for USB BOT/CBW's cap, etc.)

		// Best-effort sense data for logging/diagnostics. Check the command's return value.
		scsi_sense_t  last_sense;
	} scsi_device_t;

	// Parsed standard INQUIRY data
	typedef struct {
		uint8_t peripheral_type;  // Bits [4:0] of byte 0 (0x00 = direct-access, 0x05 = CD/DVD, ...)
		uint8_t removable;        // Byte 1 bit 7 (RMB)
		char    vendor[9];        // Bytes 8-15
		char    product[17];      // Bytes 16-31
		char    revision[5];      // Bytes 32-35
	} scsi_inquiry_data_t;

	// Fills in a scsi_device_t from a transport's exec function, context, and CDB size.
	static inline void scsi_device_init(scsi_device_t* dev, scsi_exec_fn exec, void* ctx, uint8_t cdb_len) {
		dev->exec = exec;
		dev->ctx = ctx;
		dev->cdb_len = cdb_len;
		dev->last_sense.valid = 0;
		dev->last_sense.sense_key = 0;
		dev->last_sense.asc = 0;
		dev->last_sense.ascq = 0;
	}

	int scsi_test_unit_ready(scsi_device_t* dev);
	int scsi_request_sense(scsi_device_t* dev, scsi_sense_t* out);
	int scsi_inquiry(scsi_device_t* dev, scsi_inquiry_data_t* out);

	// last_lba_out receives the address of the LAST valid LBA (i.e. capacity - 1), as returned by READ CAPACITY(10)
	int scsi_read_capacity10(scsi_device_t* dev, uint64_t* last_lba_out, uint32_t* block_size_out);

	int scsi_read10(scsi_device_t* dev, uint32_t lba, uint16_t count, uint32_t block_size, void* buf);
	int scsi_write10(scsi_device_t* dev, uint32_t lba, uint16_t count, uint32_t block_size, const void* buf);

	// load_eject. for optical drives, request the tray open/close along with the change.
	int scsi_start_stop_unit(scsi_device_t* dev, int start, int load_eject);

#ifdef __cplusplus
}
#endif
#endif // WALLOS_SCSI_H