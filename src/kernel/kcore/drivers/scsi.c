#include <string.h>
#include <stdio.h>

#include <endian_bits.h>
#include <drivers/serial.h>
#include <drivers/scsi.h>

#define SCSI_CMD_TEST_UNIT_READY   0x00
#define SCSI_CMD_REQUEST_SENSE     0x03
#define SCSI_CMD_INQUIRY           0x12
#define SCSI_CMD_START_STOP_UNIT   0x1B
#define SCSI_CMD_READ_CAPACITY_10  0x25
#define SCSI_CMD_READ_10           0x28
#define SCSI_CMD_WRITE_10          0x2A

#define SCSI_SENSE_ALLOC_LEN  18


// Raw REQUEST SENSE, bypassing scsi_exec_checked() (which would otherwise try to recover sense data for a failed REQUEST SENSE by... issuing REQUEST SENSE)
static int scsi_pull_sense(scsi_device_t* dev) {
	uint8_t cdb[16];
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = SCSI_CMD_REQUEST_SENSE;
	cdb[4] = SCSI_SENSE_ALLOC_LEN;

	uint8_t buf[SCSI_SENSE_ALLOC_LEN];
	memset(buf, 0, sizeof(buf));

	if (dev->exec(dev->ctx, cdb, dev->cdb_len, buf, sizeof(buf), SCSI_DIR_READ) != 0) {
		dev->last_sense.valid = 0;
		return -1;
	}

	dev->last_sense.valid = 1;
	dev->last_sense.sense_key = buf[2] & 0x0F;
	dev->last_sense.asc = buf[12];
	dev->last_sense.ascq = buf[13];
	return 0;
}

// Runs a CDB through the transport.
// On failure, automatically pulls sense data and logs it
static int scsi_exec_checked(scsi_device_t* dev, const uint8_t* cdb, void* buf, uint32_t buf_len, scsi_data_dir_t dir) {
	dev->last_sense.valid = 0;

	int r = dev->exec(dev->ctx, cdb, dev->cdb_len, buf, buf_len, dir);
	if (r == 0) return 0;

	if (scsi_pull_sense(dev) == 0) {
		printf_serial("[SCSI][ERROR] cmd 0x%02x failed: sense key=0x%x asc=0x%02x ascq=0x%02x\r\n", cdb[0], dev->last_sense.sense_key, dev->last_sense.asc, dev->last_sense.ascq);
	} else {
		printf_serial("[SCSI][ERROR] cmd 0x%02x failed (and REQUEST SENSE also failed)\r\n", cdb[0]);
	}

	return -1;
}

int scsi_test_unit_ready(scsi_device_t* dev) {
	uint8_t cdb[16];
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = SCSI_CMD_TEST_UNIT_READY;

	return scsi_exec_checked(dev, cdb, NULL, 0, SCSI_DIR_NONE);
}

int scsi_request_sense(scsi_device_t* dev, scsi_sense_t* out) {
	if (scsi_pull_sense(dev) != 0) return -1;
	if (out) *out = dev->last_sense;
	return 0;
}

int scsi_inquiry(scsi_device_t* dev, scsi_inquiry_data_t* out) {
	uint8_t cdb[16];
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = SCSI_CMD_INQUIRY;
	cdb[4] = 36; // Allocation length

	uint8_t buf[36];
	memset(buf, 0, sizeof(buf));

	if (scsi_exec_checked(dev, cdb, buf, sizeof(buf), SCSI_DIR_READ) != 0) return -1;

	if (out) {
		memset(out, 0, sizeof(*out));
		out->peripheral_type = buf[0] & 0x1F;
		out->removable = (buf[1] & 0x80) ? 1 : 0;
		memcpy(out->vendor, &buf[8], 8);    out->vendor[8] = '\0';
		memcpy(out->product, &buf[16], 16); out->product[16] = '\0';
		memcpy(out->revision, &buf[32], 4); out->revision[4] = '\0';
	}
	return 0;
}

int scsi_read_capacity10(scsi_device_t* dev, uint64_t* last_lba_out, uint32_t* block_size_out) {
	uint8_t cdb[16];
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = SCSI_CMD_READ_CAPACITY_10;

	uint8_t buf[8];
	memset(buf, 0, sizeof(buf));

	if (scsi_exec_checked(dev, cdb, buf, sizeof(buf), SCSI_DIR_READ) != 0) return -1;

	uint32_t last_lba = read32be(&buf[0]);
	uint32_t block_size = read32be(&buf[4]);

	if (last_lba_out) *last_lba_out = last_lba;
	if (block_size_out) *block_size_out = block_size;
	return 0;
}

int scsi_read10(scsi_device_t* dev, uint32_t lba, uint16_t count, uint32_t block_size, void* buf) {
	if (!buf || count == 0) return -1;

	uint8_t cdb[16];
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = SCSI_CMD_READ_10;
	write32be(&cdb[2], lba);
	write16be(&cdb[7], count);

	return scsi_exec_checked(dev, cdb, buf, (uint32_t) count * block_size, SCSI_DIR_READ);
}

int scsi_write10(scsi_device_t* dev, uint32_t lba, uint16_t count, uint32_t block_size, const void* buf) {
	if (!buf || count == 0) return -1;

	uint8_t cdb[16];
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = SCSI_CMD_WRITE_10;
	write32be(&cdb[2], lba);
	write16be(&cdb[7], count);

	return scsi_exec_checked(dev, cdb, (void*) (uintptr_t) buf, (uint32_t) count * block_size, SCSI_DIR_WRITE);
}

int scsi_start_stop_unit(scsi_device_t* dev, int start, int load_eject) {
	uint8_t cdb[16];
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = SCSI_CMD_START_STOP_UNIT;
	cdb[4] = (uint8_t) ((load_eject ? 0x02 : 0x00) | (start ? 0x01 : 0x00));

	return scsi_exec_checked(dev, cdb, NULL, 0, SCSI_DIR_NONE);
}