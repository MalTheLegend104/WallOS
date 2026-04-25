// atapi_pio.cpp
#include <drivers/sata/atapi_pio.h>
#include <drivers/sata/pio.h>
#include <klibc/kprint.h>
#include <cpu_io.h>
#include <stdio.h>
#include <string.h>

// Get the correct set of registers based on the drive number
bool atapi_resolve_regs(int drive_number,
	uint16_t* data_reg,
	uint16_t* sec_count,
	uint16_t* lba_low,
	uint16_t* lba_mid,
	uint16_t* lba_high,
	uint16_t* drv_reg,
	uint16_t* cmd_reg) {
	switch (drive_number) {
		case 0:
		case 1:
			*data_reg = PRIMARY_DATA_REGISTER;
			*sec_count = PRIMARY_SECTOR_COUNT;
			*lba_low = PRIMARY_LBA_LOW;
			*lba_mid = PRIMARY_LBA_MID;
			*lba_high = PRIMARY_LBA_HIGH;
			*drv_reg = PRIMARY_DRIVE_REGISTER;
			*cmd_reg = PRIMARY_COMMAND_REGISTER;
			return true;

		case 2:
		case 3:
			*data_reg = SECONDARY_DATA_REGISTER;
			*sec_count = SECONDARY_SECTOR_COUNT;
			*lba_low = SECONDARY_LBA_LOW;
			*lba_mid = SECONDARY_LBA_MID;
			*lba_high = SECONDARY_LBA_HIGH;
			*drv_reg = SECONDARY_DRIVE_REGISTER;
			*cmd_reg = SECONDARY_COMMAND_REGISTER;
			return true;

		default:
			return false;
	}
}

// Control ports for primary or secondary buses
#define PRIMARY_DEVICE_CONTROL   0x3F6
#define SECONDARY_DEVICE_CONTROL 0x376
// Features register (write-only, same port as error register on read)
#define PRIMARY_FEATURES         0x1F1
#define SECONDARY_FEATURES       0x171
bool atapi_get_control_regs(int drive_number, uint16_t* features_reg, uint16_t* device_control) {
	switch (drive_number) {
		case 0:
		case 1:
			*features_reg = PRIMARY_FEATURES;
			*device_control = PRIMARY_DEVICE_CONTROL;
			return true;
		case 2:
		case 3:
			*features_reg = SECONDARY_FEATURES;
			*device_control = SECONDARY_DEVICE_CONTROL;
			return true;
		default:
			return false;
	}
}

// This should probably just use the same IO wait we use elsewhere
// This code is copied like 4 times
static inline void atapi_io_wait() { for (int i = 0; i < 5; i++) outb(0x80, 0); }

// Poll until BSY clears
// Returns false on timeout.
bool atapi_poll_bsy(uint16_t cmd_reg) {
	int timeout = 100000;
	while (inb(cmd_reg) & 0x80) {
		if (--timeout == 0) {
			printf("[ATAPI][WARN] TIMEOUT while polling BSY\n");
			return false;
		}
	}
	return true;
}

// Poll until DRQ sets
// Returns false on timeout or ERR.
bool atapi_poll_drq(uint16_t cmd_reg) {
	int timeout = 100000;
	uint8_t status;
	for (;;) {
		status = inb(cmd_reg);
		if (status & 0x01) { // ERR
			printf("[ATAPI][ERROR] ERR bit set, status=0x%02x\n", status);
			return false;
		}
		// DRQ
		if (status & 0x08) return true;
		if (--timeout == 0) {
			printf("[ATAPI][WARN] TIMEOUT, status=0x%02x\n", status);
			return false;
		}
	}
}

const char* get_sense_string(uint8_t err) {
	switch (err) {
		case 0x0: return "No sense";
		case 0x2: return "Not Ready";
		case 0x3: return "Medium Error";
		case 0x5: return "Illegal Request";
		case 0x6: return "Unit Attention";
		default:  return "Unknown";
	}
}

/*
 * Issues an ATAPI Packet command and transfers the resulting PIO data into buf.
 * buf_len is the maximum number of bytes we are willing to accept.
 * The drive may transfer fewer (it tells us the actual byte count via lba_mid:lba_high after DRQ asserts for data).
 *
 * CDB must be exactly 12 bytes (ATAPI-6 CD/DVD devices).
 */
bool atapi_send_packet(int drive_number, const uint8_t* cdb, void* buf, uint16_t buf_len) {
	uint16_t data_reg, sec_count, lba_low, lba_mid, lba_high, drv_reg, cmd_reg;
	uint16_t features_reg, device_control;

	// This function call is ugly...
	if (!atapi_resolve_regs(drive_number, &data_reg, &sec_count, &lba_low, &lba_mid, &lba_high, &drv_reg, &cmd_reg))
		return false;

	if (!atapi_get_control_regs(drive_number, &features_reg, &device_control)) return false;

	// Disable interrupts 
	// We are polling, not using IRQs
	outb(device_control, 0x02);
	atapi_io_wait();

	// Select drive
	uint8_t drive_select = (drive_number == 0 || drive_number == 2) ? 0xA0 : 0xB0;
	outb(drv_reg, drive_select);
	atapi_io_wait();

	// Wait for drive to be ready before touching any other registers
	if (!atapi_poll_bsy(cmd_reg)) return false;

	outb(features_reg, 0x00); // Features: 0x00 = PIO mode (no DMA, no OVERLAP)
	outb(sec_count, 0x00);    // Sector count must be 0 for ATAPI
	outb(lba_low, 0x00);      // lba_low must be 0 for ATAPI

	// Byte count limit (max bytes per DRQ burst)
	outb(lba_mid, (uint8_t) (buf_len & 0xFF));
	outb(lba_high, (uint8_t) (buf_len >> 8));

	// Issue PACKET command
	outb(cmd_reg, COMMAND_PACKET);

	// Spec says to wait 400ns before reading status after a command write
	atapi_io_wait();

	// Wait for BSY to clear, then DRQ to assert (CDB transfer phase)
	// Some drives need a few microseconds here so we poll with BSY awareness
	{
		int timeout = 100000;
		uint8_t status;
		for (;;) {
			status = inb(cmd_reg);
			// ERR
			if (status & 0x01) {
				printf("[ATAPI][ERROR] ERR after PACKET cmd, status=0x%02x\n", status);
				return false;
			}
			// BSY=0, DRQ=1
			if (!(status & 0x80) && (status & 0x08)) break;
			if (--timeout == 0) {
				printf("[ATAPI][WARN] TIMEOUT waiting for CDB DRQ, status=0x%02x\n", status);
				return false;
			}
		}
	}

	// Send the 12-byte CDB as six 16-bit words
	const uint16_t* cdb16 = (const uint16_t*) cdb;
	for (int i = 0; i < 6; i++) outw(data_reg, cdb16[i]);

	// Read data bursts until BSY=0 DRQ=0 (transfer complete)
	uint16_t* buf16 = (uint16_t*) buf;
	uint32_t  remaining = buf_len;

	while (true) {
		// Must wait for BSY to drop between the CDB write and the data phase,
		// and between each subsequent DRQ burst.
		{
			int timeout = 100000;
			uint8_t status;
			while (true) {
				status = inb(cmd_reg);
				if (status & 0x01) {
					uint8_t err = inb(features_reg);
					printf("[ATAPI][ERROR] ERR in data phase.\n");
					printf("\tstatus=0x%02x\n\terror=0x%02x\n\tSENSE KEY: %s (0x%x)\n", status, err, get_sense_string((err >> 4) & 0x0F), (err >> 4) & 0x0F);
					return false;
				}
				// BSY cleared
				if (!(status & 0x80)) {
					// DRQ=0 means transfer is done
					if (!(status & 0x08)) goto transfer_done;
					// DRQ=1 means another burst is ready
					break;
				}
				if (--timeout == 0) {
					printf("[ATAPI][WARN] TIMEOUT in data phase, status=0x%02x\n", status);
					return false;
				}
			}
		}

		// Read the byte count the drive is offering this burst
		uint16_t byte_count = ((uint16_t) inb(lba_high) << 8) | inb(lba_mid);

		if (byte_count == 0) break;

		uint16_t words = byte_count / 2;

		if (byte_count > remaining) {
			// Drain the excess so the drive doesn't stall
			uint16_t safe_words = remaining / 2;
			for (uint16_t i = 0; i < safe_words; i++) *buf16++ = inw(data_reg);
			for (uint16_t i = safe_words; i < words; i++) inw(data_reg);
			remaining = 0;
			break;
		}

		for (uint16_t i = 0; i < words; i++) *buf16++ = inw(data_reg);

		remaining -= byte_count;
	}

transfer_done:
	// Final BSY drain
	atapi_poll_bsy(cmd_reg);
	return true;
}

#define SPINUP_RETRIES    200   // 200 * 10000 io_waits — roughly 20-30s max
#define SPINUP_DELAY_IOS 10000  // ~10-40ms per iteration on real hardware
/*
 * Spins up the drive and blocks until it reports ready,
 * up to a maximum number of retries with a delay between each.
 */
bool atapi_wait_for_ready(int drive_number) {
	uint8_t cdb[12] = { 0 };
	cdb[0] = 0x1B;  // START STOP UNIT
	cdb[4] = 0x01;  // Start=1

	// Send directly — no retry wrapper. The drive may return 0x2 here
	// and that's fine, we just want to kick it into spinning up.
	// We intentionally ignore the return value.
	atapi_send_packet(drive_number, cdb, NULL, 0);

	printf("Waiting for drive %d to spin up", drive_number);

	for (int attempt = 0; attempt < SPINUP_RETRIES; attempt++) {
		for (int i = 0; i < SPINUP_DELAY_IOS; i++)
			atapi_io_wait();

		printf(".");

		// Also send TEST UNIT READY directly, not through the retry wrapper,
		// so a 0x2 response doesn't recurse back into us.
		uint8_t tur_cdb[12] = { 0 };
		tur_cdb[0] = 0x00;  // TEST UNIT READY
		if (atapi_send_packet(drive_number, tur_cdb, NULL, 0)) {
			printf(" ready!\n");
			return true;
		}

		uint16_t features_reg, device_control;
		if (!atapi_get_control_regs(drive_number, &features_reg, &device_control))
			return false;

		uint8_t sense_key = (inb(features_reg) >> 4) & 0x0F;

		// 0x6 (Unit Attention) during spin-up is normal — the drive is
		// signalling it has reset. Clear it and keep polling.
		if (sense_key == 0x06) {
			uint8_t rs_cdb[12] = { 0 };
			rs_cdb[0] = 0x03;  // REQUEST SENSE
			rs_cdb[4] = 18;
			uint8_t rs_buf[18] = { 0 };
			atapi_send_packet(drive_number, rs_cdb, rs_buf, sizeof(rs_buf));
			continue;
		}

		if (sense_key != 0x02) {
			printf("\n[ATAPI][ERROR] unexpected sense 0x%x during spin-up, giving up\n",
				sense_key);
			return false;
		}
	}

	printf("\n[ATAPI][ERROR] TIMEOUT after %d attempts\n", SPINUP_RETRIES);
	return false;
}

/*
 * REQUEST SENSE (opcode 0x03)
 * Reads and clears the pending sense data from the drive.
 *
 * Must be issued after any Unit Attention (sense key 0x6) to clear it before retrying the original command.
 */
bool atapi_request_sense(int drive_number) {
	uint8_t cdb[12] = { 0 };
	cdb[0] = 0x03;  // REQUEST SENSE
	cdb[4] = 18;    // Allocation length (standard fixed sense data)

	uint8_t buf[18] = { 0 };
	return atapi_send_packet(drive_number, cdb, buf, sizeof(buf));
}

/*
 * Sends a packet with automatic retry handling for:
 *   Not Ready (0x2): issues START STOP UNIT then retries once
 *   Unit Attention (0x6): issues REQUEST SENSE then retries once
 *
 * Everything should ideally call this instead of atapi_send_packet() directly
 */
bool atapi_send_packet_retry(int drive_number, const uint8_t* cdb, void* buf, uint16_t buf_len) {
	// First attempt
	if (atapi_send_packet(drive_number, cdb, buf, buf_len)) return true;

	// Read the error register to get the sense key.
	// We need the features_reg port to do this.
	uint16_t features_reg, device_control;
	if (!atapi_get_control_regs(drive_number, &features_reg, &device_control)) return false;

	uint8_t err = inb(features_reg);
	uint8_t sense_key = (err >> 4) & 0x0F;

	// printf("atapi_send_packet_retry: sense key 0x%x, attempting recovery\n", sense_key);

	switch (sense_key) {
		case 0x02:  // Not Ready: spin the drive up then retry
			if (!atapi_wait_for_ready(drive_number)) {
				printf("[ATAPI][ERROR] Drive failed to become ready\n");
				return false;
			}
			break;

		case 0x06:  // Unit Attention: clear it then retry
			if (!atapi_request_sense(drive_number)) {
				printf("[ATAPI][ERROR] REQUEST SENSE failed\n");
				return false;
			}
			break;

		default:
			printf("[ATAPI][ERROR] unrecoverable sense key 0x%x\n", sense_key);
			return false;
	}

	// Single retry after recovery
	// printf("atapi_send_packet_retry: retrying command after recovery\n");
	return atapi_send_packet(drive_number, cdb, buf, buf_len);
}

/*
 * START STOP UNIT (opcode 0x1B)
 * Tells the drive to spin up and load the disc.
 *
 * This has to be called on real hardware, QEMU/Virtualbox seemingly dont care.
 */
bool atapi_start_unit(int drive_number) {
	uint8_t cdb[12] = { 0 };
	cdb[0] = 0x1B;
	cdb[4] = 0x01;
	return atapi_send_packet_retry(drive_number, cdb, NULL, 0);
}

/*
 * TEST UNIT READY (opcode 0x00)
 * Returns true if the drive is ready to accept commands.
 * Returns false if not ready (sense 0x2) or any other error.
 */
bool atapi_test_unit_ready(int drive_number) {
	uint8_t cdb[12] = { 0 };
	cdb[0] = 0x00;
	return atapi_send_packet_retry(drive_number, cdb, NULL, 0);
}

/*
 * Sends READ CAPACITY (10) (opcode 0x25).
 * Returns the LBA of the last addressable block and the block size in bytes.
 */
bool atapi_read_capacity(int drive_number, uint32_t* lba_out, uint32_t* block_size_out) {
	uint8_t cdb[12] = {};
	cdb[0] = ATAPI_CMD_READ_CAPACITY; // READ CAPACITY (10)
	// All other bytes are zero. Theres's no PMI and no LBA hint

	uint8_t response[8] = {};
	if (!atapi_send_packet_retry(drive_number, cdb, response, sizeof(response))) return false;

	// Response is big endian
	// This is horrible to look at, just trust that it works
	*lba_out = ((uint32_t) response[0] << 24)
		| ((uint32_t) response[1] << 16)
		| ((uint32_t) response[2] << 8)
		| (uint32_t) response[3];

	*block_size_out = ((uint32_t) response[4] << 24)
		| ((uint32_t) response[5] << 16)
		| ((uint32_t) response[6] << 8)
		| (uint32_t) response[7];

	return true;
}

/*
 * Sends READ (12) (opcode 0xA8)
 * Reads `count` sectors starting at `lba`.
 * buffer must be at least count * ATAPI_SECTOR_SIZE bytes.
 *
 * We read one sector per packet command to keep the byte count limit simple and avoid issues with drives that don't support multi-sector ATAPI reads.
 */
bool atapi_read_sectors(int drive_number, uint32_t lba, uint32_t count, void* buffer) {
	uint8_t* dst = (uint8_t*) buffer;

	for (uint32_t s = 0; s < count; s++) {
		uint32_t current_lba = lba + s;

		uint8_t cdb[12] = {};
		cdb[0] = ATBC_CMD_READ12;
		cdb[2] = (uint8_t) (current_lba >> 24);
		cdb[3] = (uint8_t) (current_lba >> 16);
		cdb[4] = (uint8_t) (current_lba >> 8);
		cdb[5] = (uint8_t) (current_lba);
		cdb[6] = 0; // transfer length (MSB)
		cdb[7] = 0;
		cdb[8] = 0;
		cdb[9] = 1; // transfer length = 1 sector

		if (!atapi_send_packet_retry(drive_number, cdb, dst, ATAPI_SECTOR_SIZE)) {
			printf("[ATAPI][ERROR] failed at LBA %u\n", current_lba);
			return false;
		}

		dst += ATAPI_SECTOR_SIZE;
	}

	return true;
}

/*
 * Sends INQUIRY (opcode 0x12).
 * Fills out an atapi_inquiry_t with vendor/product strings and flags.
 */
bool atapi_inquiry(int drive_number, atapi_inquiry_t* out) {
	uint8_t cdb[12] = {};
	cdb[0] = ATAPI_CMD_INQUIRY;
	cdb[4] = sizeof(atapi_inquiry_t); // Allocation length

	return atapi_send_packet_retry(drive_number, cdb, out, sizeof(atapi_inquiry_t));
}

// ---------------------------------------------------------------------------
// Info printer
// ---------------------------------------------------------------------------

void print_atapi_device_info(int drive_number) {
	atapi_inquiry_t inq = {};
	if (!atapi_inquiry(drive_number, &inq)) {
		printf("[ATAPI][ERROR] INQUIRY failed for drive %d\n", drive_number);
		return;
	}

	// Null-terminate the fixed-width strings before printing
	char vendor[9] = {};
	char product[17] = {};
	char rev[5] = {};
	memcpy(vendor, inq.vendor_id, 8);
	memcpy(product, inq.product_id, 16);
	memcpy(rev, inq.product_rev, 4);

	printf("ATAPI Drive %d:\n", drive_number);
	printf("  Vendor:   %.8s\n", vendor);
	printf("  Product:  %.16s\n", product);
	printf("  Revision: %.4s\n", rev);
	printf("  Removable: %s\n", inq.removable ? "Yes" : "No");

	const char* device_type;
	switch (inq.peripheral_device_type) {
		case 0x00: device_type = "Direct-access block"; break;
		case 0x01: device_type = "Sequential-access";  break;
		case 0x02: device_type = "Printer";             break;
		case 0x03: device_type = "Processor";           break;
		case 0x04: device_type = "Write-once";          break;
		case 0x05: device_type = "CD/DVD-ROM";          break;
		case 0x07: device_type = "Optical memory";      break;
		case 0x08: device_type = "Medium changer";      break;
		case 0x0E: device_type = "Simplified direct";   break;
		default:   device_type = "Unknown";             break;
	}
	printf("  Device type: %s\n", device_type);

	uint32_t last_lba = 0;
	uint32_t block_size = 0;
	if (atapi_read_capacity(drive_number, &last_lba, &block_size)) {
		uint64_t total_bytes = (uint64_t) (last_lba + 1) * block_size;
		printf("  Capacity: %llu MB (%llu bytes)\n", total_bytes / 1024 / 1024, total_bytes);
		printf("  Block size: %u bytes\n", block_size);
		printf("  Last LBA: %u\n", last_lba);
	} else {
		printf("  Capacity: unavailable (no media or drive error)\n");
	}
}