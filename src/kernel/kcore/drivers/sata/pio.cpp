#include <drivers/sata/pio.h>
#include <drivers/serial.h>
#include <klibc/kprint.h>
#include <cpu_io.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <filesystem/wdm.h>

#define PRIMARY_FIRST    0
#define PRIMARY_SECOND	 1
#define SECONDARY_FIRST  2
#define SECONDARY_SECOND 3

// Timeout value for busy-wait loops
#define ATA_TIMEOUT 10000

drive_info_t drive_zero{};
drive_info_t drive_one{};
drive_info_t drive_two{};
drive_info_t drive_three{};

void io_wait() {
	// Writing to an unused port is commonly used to induce very small delays
	for (int i = 0; i < 5; i++) {
		outb(0x80, 0);
	}
}

bool identify(int drive_number) {
	// drive_info_t* drive_info;
	sata_device_identify* current;
	uint8_t drive_select;
	uint16_t drive_register;
	uint16_t command_register;
	uint16_t data_register;
	bool primary = true;

	// printf("identify: Starting identification for drive %d\n", drive_number);

	// We have to clear a few things before we can call identify
	switch (drive_number) {
		case 0:
			current = &(drive_zero.identify);
			drive_select = 0xA0;
			drive_register = PRIMARY_DRIVE_REGISTER;
			command_register = PRIMARY_COMMAND_REGISTER;
			data_register = PRIMARY_DATA_REGISTER;
			break;
		case 1:
			current = &(drive_one.identify);
			drive_select = 0xB0;
			drive_register = PRIMARY_DRIVE_REGISTER;
			command_register = PRIMARY_COMMAND_REGISTER;
			data_register = PRIMARY_DATA_REGISTER;
			break;
		case 2:
			current = &(drive_two.identify);
			drive_select = 0xA0;
			drive_register = SECONDARY_DRIVE_REGISTER;
			command_register = SECONDARY_COMMAND_REGISTER;
			data_register = SECONDARY_DATA_REGISTER;
			primary = false;
			break;
		case 3:
			current = &(drive_three.identify);
			drive_select = 0xB0;
			drive_register = SECONDARY_DRIVE_REGISTER;
			command_register = SECONDARY_COMMAND_REGISTER;
			data_register = SECONDARY_DATA_REGISTER;
			primary = false;
			break;
		default:
			printf("identify: Invalid drive number %d\n", drive_number);
			return false;
	}

	outb(drive_register, drive_select);
	io_wait();

	// we need to clear sector count and LBA regs
	if (primary) {
		outb(PRIMARY_SECTOR_COUNT, 0x00);
		io_wait();

		outb(PRIMARY_LBA_LOW, 0x00);
		io_wait();

		outb(PRIMARY_LBA_MID, 0x00);
		io_wait();

		outb(PRIMARY_LBA_HIGH, 0x00);
		io_wait();
	} else {
		outb(SECONDARY_SECTOR_COUNT, 0x00);
		io_wait();

		outb(SECONDARY_LBA_LOW, 0x00);
		io_wait();

		outb(SECONDARY_LBA_MID, 0x00);
		io_wait();

		outb(SECONDARY_LBA_HIGH, 0x00);
		io_wait();
	}

	outb(command_register, COMMAND_IDENTIFY);
	io_wait();

	uint8_t status = inb(command_register);

	if (status == 0) {
		printf("identify: No drive present (status = 0)\n");
		return false;
	}

	// Wait for BSY bit to clear
	int timeout = ATA_TIMEOUT;
	while (status & 0x80) {
		status = inb(command_register);
		if (--timeout <= 0) {
			// printf("identify: TIMEOUT waiting for BSY to clear! Final status = 0x%02x\n", status);
			return false;
		}
	}
	// printf("identify: BSY cleared, status = 0x%02x\n", status);

	// Check LBA Mid and High for ATAPI devices
	uint8_t lba_mid, lba_high;
	if (primary) {
		lba_mid = inb(PRIMARY_LBA_MID);
		lba_high = inb(PRIMARY_LBA_HIGH);
	} else {
		lba_mid = inb(SECONDARY_LBA_MID);
		lba_high = inb(SECONDARY_LBA_HIGH);
	}

	if (lba_mid != 0 || lba_high != 0) {
		switch (drive_number) {
			case 0:
				drive_zero.atapi = true;
				break;
			case 1:
				drive_one.atapi = true;
				break;
			case 2:
				drive_two.atapi = true;
				break;
			case 3:
				drive_three.atapi = true;
				break;
			default: break;
		}

		return true;
	}

	// Continue polling until ERR is set or DRQ is set
	// printf("identify: Waiting for DRQ or ERR...\n");
	timeout = ATA_TIMEOUT;
	while (((status = inb(command_register)) & (0x40 | 0x01)) == 0) {
		if (--timeout == 0) {
			printf("identify: TIMEOUT waiting for DRQ/ERR! Final status = 0x%02x\n", status);
			return false;
		}
		if (timeout % 10000 == 0) {
			printf("identify: Still waiting for DRQ/ERR... status = 0x%02x\n", status);
		}
	}

	// printf("identify: Got DRQ or ERR, status = 0x%02x\n", status);

	if (status & 0x01) {
		printf("identify: ERR bit set, command failed\n");
		return false;
	}

	// 256 WORDs from the data_register
	// printf("identify: Reading 256 words from data register\n");
	for (int i = 0; i < 256; i++) {
		((uint16_t*) current)[i] = inw(data_register);
	}

	// printf("identify: Successfully read IDENTIFY data\n");
	return true;
}

// Forward delcare because I dont want to reorder this entire file
void pio_wdm_register_all(void);

void detect_ide_drives() {
	printf("Checking for IDE drives...\n");

	drive_zero.hardware_id = 0;  // PRIMARY_FIRST
	drive_one.hardware_id = 1;   // PRIMARY_SECOND
	drive_two.hardware_id = 2;   // SECONDARY_FIRST
	drive_three.hardware_id = 3; // SECONDARY_SECOND

	drive_zero.exists = identify(PRIMARY_FIRST);
	if (drive_zero.exists) {
		printf("Drive 0 detected.\n");
	} else {
		printf("Drive 0 not connected.\n");
	}

	drive_one.exists = identify(PRIMARY_SECOND);
	if (drive_one.exists) {
		printf("Drive 1 detected.\n");
	} else {
		printf("Drive 1 not connected.\n");
	}

	drive_two.exists = identify(SECONDARY_FIRST);
	if (drive_two.exists) {
		printf("Drive 2 detected.\n");
	} else {
		printf("Drive 2 not connected.\n");
	}

	drive_three.exists = identify(SECONDARY_SECOND);
	if (drive_three.exists) {
		printf("Drive 3 detected.\n");
	} else {
		printf("Drive 3 not connected.\n");
	}

	// Register all detected drives with the WDM (with the exception of the ATAPI devices)
	// ATAPI registration is done elsewhere, since it's a completely different interface
	pio_wdm_register_all();
}

uint64_t get_capacity_bytes(const sata_device_identify* device) {
	uint64_t sectors = device->user_addressable_sectors;

	if (device->command_set_active.big_lba) {
		sectors = ((uint64_t) device->max48_bit_lba[1] << 32) | device->max48_bit_lba[0];
	}

	return (sectors * 512);
}

uint64_t get_capacity_mb(const sata_device_identify* device) {
	return (get_capacity_bytes(device) / 1024 / 1024);
}

#include <drivers/sata/atapi_pio.h>
void print_sata_device_info(drive_info_t* info, int drive_number) {
	if (!info->exists) {
		printf("Drive not connected.\n");
		return;
	}

	if (info->atapi) {
		print_atapi_device_info(drive_number);
		return;
	}

	sata_device_identify* device = &(info->identify);
	// Serial number, firmware, model, etc.
	printf("Serial Number: %.20s\n", device->serial_number);
	printf("Firmware Revision: %.8s\n", device->firmware_revision);
	printf("Model Number: %.40s\n", device->model_number);
	printf("Cylinders: %u\n", device->num_cylinders);
	printf("Heads: %u\n", device->num_heads);
	printf("Sectors per Track: %u\n", device->num_sectors_per_track);

	printf("Trusted Computing: %s\n", (device->trusted_computing.feature_supported) ? "Yes" : "No");
	printf("LBA Support: %s\n", (device->capabilities.lba_supported) ? "Yes" : "No");
	printf("DMA Support: %s\n", (device->capabilities.dma_supported) ? "Yes" : "No");

	if (device->serial_ata_capabilities.sata_gen3) {
		printf("SATA 3 supported.\n");
	} else if (device->serial_ata_capabilities.sata_gen2) {
		printf("SATA 2 supported.\n");
	} else if (device->serial_ata_capabilities.sata_gen1) {
		printf("SATA 1 supported.\n");
	} else {
		printf("Serial ATA not supported. Assuming PATA only.\n");
	}

	printf("Major Rev: %d\n", device->major_revision);
	printf("Minor Rev: %d\n", device->minor_revision);

	printf("Max Capacity: %d MB - (%d bytes)\n", get_capacity_mb(device), get_capacity_bytes(device));
}

static void ata_wait_bsy(const uint16_t command_register) {
	uint8_t status;
	int timeout = ATA_TIMEOUT;
	do {
		status = inb(command_register);
		if (--timeout == 0) {
			printf("ata_wait_bsy: TIMEOUT! status = 0x%02x\n", status);
			return;
		}
	} while (status & 0x80); // BSY
}

static void ata_wait_drq(const uint16_t command_register) {
	uint8_t status;
	int timeout = ATA_TIMEOUT;
	do {
		status = inb(command_register);
		if (--timeout == 0) {
			printf("ata_wait_drq: TIMEOUT! status = 0x%02x\n", status);
			return;
		}
	} while (!(status & 0x08)); // DRQ
}

/* This is kinda ugly, but it's the easiest way I could think of to do this. */
static bool resolve_drive_registers(
	const int drive_number,
	uint16_t& data_register,
	uint16_t& sector_count,
	uint16_t& lba_low,
	uint16_t& lba_mid,
	uint16_t& lba_high,
	uint16_t& drive_register,
	uint16_t& command_register
) {
	switch (drive_number) {
		case PRIMARY_FIRST:
			data_register = PRIMARY_DATA_REGISTER;
			sector_count = PRIMARY_SECTOR_COUNT;
			lba_low = PRIMARY_LBA_LOW;
			lba_mid = PRIMARY_LBA_MID;
			lba_high = PRIMARY_LBA_HIGH;
			drive_register = PRIMARY_DRIVE_REGISTER;
			command_register = PRIMARY_COMMAND_REGISTER;
			return true;

		case PRIMARY_SECOND:
			data_register = PRIMARY_DATA_REGISTER;
			sector_count = PRIMARY_SECTOR_COUNT;
			lba_low = PRIMARY_LBA_LOW;
			lba_mid = PRIMARY_LBA_MID;
			lba_high = PRIMARY_LBA_HIGH;
			drive_register = PRIMARY_DRIVE_REGISTER;
			command_register = PRIMARY_COMMAND_REGISTER;
			return true;

		case SECONDARY_FIRST:
			data_register = SECONDARY_DATA_REGISTER;
			sector_count = SECONDARY_SECTOR_COUNT;
			lba_low = SECONDARY_LBA_LOW;
			lba_mid = SECONDARY_LBA_MID;
			lba_high = SECONDARY_LBA_HIGH;
			drive_register = SECONDARY_DRIVE_REGISTER;
			command_register = SECONDARY_COMMAND_REGISTER;
			return true;

		case SECONDARY_SECOND:
			data_register = SECONDARY_DATA_REGISTER;
			sector_count = SECONDARY_SECTOR_COUNT;
			lba_low = SECONDARY_LBA_LOW;
			lba_mid = SECONDARY_LBA_MID;
			lba_high = SECONDARY_LBA_HIGH;
			drive_register = SECONDARY_DRIVE_REGISTER;
			command_register = SECONDARY_COMMAND_REGISTER;
			return true;

		default:
			return false;
	}
}

bool sata_pio_read28(const int drive_number, const uint32_t lba, const uint8_t sector_count, void* buffer) {
	uint16_t data_reg;
	uint16_t sec_count;
	uint16_t lba_low;
	uint16_t lba_mid;
	uint16_t lba_high;
	uint16_t drv_reg;
	uint16_t cmd_reg;


	if (!resolve_drive_registers(drive_number, data_reg, sec_count, lba_low, lba_mid, lba_high, drv_reg, cmd_reg))
		return false;

	// Determine master/slave based on drive number
	uint8_t drive_select = (drive_number == 0 || drive_number == 2) ? 0xE0 : 0xF0;
	drive_select |= (lba >> 24) & 0x0F;

	outb(drv_reg, drive_select);
	io_wait();

	outb(sec_count, sector_count);
	outb(lba_low, (uint8_t) (lba));
	outb(lba_mid, (uint8_t) (lba >> 8));
	outb(lba_high, (uint8_t) (lba >> 16));

	outb(cmd_reg, COMMAND_READ_SECTOR);
	io_wait();

	uint16_t* buf16 = (uint16_t*) buffer;

	for (int s = 0; s < sector_count; s++) {
		ata_wait_bsy(cmd_reg);
		ata_wait_drq(cmd_reg);

		for (int i = 0; i < 256; i++) {
			buf16[i] = inw(data_reg);
		}

		buf16 += 256;
	}

	return true;
}

bool sata_pio_write28(const int drive_number, const uint32_t lba, const uint8_t sector_count, const void* buffer) {
	uint16_t data_reg;
	uint16_t sec_count;
	uint16_t lba_low;
	uint16_t lba_mid;
	uint16_t lba_high;
	uint16_t drv_reg;
	uint16_t cmd_reg;

	if (!resolve_drive_registers(drive_number, data_reg, sec_count, lba_low, lba_mid, lba_high, drv_reg, cmd_reg))
		return false;

	uint8_t drive_select = (drive_number == 0 || drive_number == 2) ? 0xE0 : 0xF0;
	drive_select |= (lba >> 24) & 0x0F;

	outb(drv_reg, drive_select);
	io_wait();

	outb(sec_count, sector_count);
	outb(lba_low, (uint8_t) (lba));
	outb(lba_mid, (uint8_t) (lba >> 8));
	outb(lba_high, (uint8_t) (lba >> 16));

	outb(cmd_reg, COMMAND_WRITE_SECTOR);
	io_wait();

	const uint16_t* buf16 = (const uint16_t*) buffer;

	for (int s = 0; s < sector_count; s++) {
		ata_wait_bsy(cmd_reg);
		ata_wait_drq(cmd_reg);

		for (int i = 0; i < 256; i++) {
			outw(data_reg, buf16[i]);
		}

		buf16 += 256;
	}

	// Flush cache
	outb(cmd_reg, COMMAND_FLUSH_CACHE);
	ata_wait_bsy(cmd_reg);

	return true;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// LBA48 PIO Read / Write
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

bool sata_pio_read48(int drive_number, uint64_t lba, uint16_t sector_count, void* buffer) {
	if (sector_count == 0) return true; // Nothing to do

	uint16_t data_reg, sec_count_reg, lba_low_reg, lba_mid_reg, lba_high_reg, drv_reg, cmd_reg;
	if (!resolve_drive_registers(drive_number, data_reg, sec_count_reg, lba_low_reg, lba_mid_reg, lba_high_reg, drv_reg, cmd_reg)) {
		return false;
	}

	uint8_t drive_select = (drive_number == PRIMARY_FIRST || drive_number == SECONDARY_FIRST) ? 0x40 : 0x50;
	outb(drv_reg, drive_select);
	io_wait();

	// High-byte pass (previous)
	outb(sec_count_reg, (uint8_t) (sector_count >> 8)); // sector count [15:8]
	outb(lba_low_reg, (uint8_t) (lba >> 24));           // LBA [31:24]
	outb(lba_mid_reg, (uint8_t) (lba >> 32));           // LBA [39:32]
	outb(lba_high_reg, (uint8_t) (lba >> 40));          // LBA [47:40]

	// Low-byte pass (current)
	outb(sec_count_reg, (uint8_t) (sector_count)); // sector count [7:0]
	outb(lba_low_reg, (uint8_t) (lba));            // LBA [7:0]
	outb(lba_mid_reg, (uint8_t) (lba >> 8));       // LBA [15:8]
	outb(lba_high_reg, (uint8_t) (lba >> 16));     // LBA [23:16]

	outb(cmd_reg, COMMAND_READ_SECTOR_EXT);
	io_wait();

	uint16_t* buf16 = (uint16_t*) buffer;
	for (uint16_t s = 0; s < sector_count; s++) {
		ata_wait_bsy(cmd_reg);
		ata_wait_drq(cmd_reg);

		for (int i = 0; i < 256; i++) {
			buf16[i] = inw(data_reg);
		}
		buf16 += 256;
	}

	return true;
}

bool sata_pio_write48(int drive_number, uint64_t lba, uint16_t sector_count, const void* buffer) {
	if (sector_count == 0) return true;

	uint16_t data_reg, sec_count_reg, lba_low_reg, lba_mid_reg, lba_high_reg, drv_reg, cmd_reg;
	if (!resolve_drive_registers(drive_number, data_reg, sec_count_reg, lba_low_reg, lba_mid_reg, lba_high_reg, drv_reg, cmd_reg)) {
		return false;
	}

	uint8_t drive_select = (drive_number == PRIMARY_FIRST || drive_number == SECONDARY_FIRST) ? 0x40 : 0x50;
	outb(drv_reg, drive_select);
	io_wait();

	// High-byte pass
	outb(sec_count_reg, (uint8_t) (sector_count >> 8));
	outb(lba_low_reg, (uint8_t) (lba >> 24));
	outb(lba_mid_reg, (uint8_t) (lba >> 32));
	outb(lba_high_reg, (uint8_t) (lba >> 40));

	// Low-byte pass
	outb(sec_count_reg, (uint8_t) (sector_count));
	outb(lba_low_reg, (uint8_t) (lba));
	outb(lba_mid_reg, (uint8_t) (lba >> 8));
	outb(lba_high_reg, (uint8_t) (lba >> 16));

	outb(cmd_reg, COMMAND_WRITE_SECTOR_EXT);
	io_wait();

	const uint16_t* buf16 = (const uint16_t*) buffer;
	for (uint16_t s = 0; s < sector_count; s++) {
		ata_wait_bsy(cmd_reg);
		ata_wait_drq(cmd_reg);

		for (int i = 0; i < 256; i++) {
			outw(data_reg, buf16[i]);
		}
		buf16 += 256;
	}

	// LBA48 flush
	outb(cmd_reg, COMMAND_FLUSH_CACHE_EXT);
	ata_wait_bsy(cmd_reg);

	return true;
}

int sata_test_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: sata-test <drive>\n");
		return 0;
	}

	int drive = argv[1][0] - '0';
	if (drive < 0 || drive > 3) {
		printf("Invalid drive number.\n");
		return 0;
	}

	if (!drive_zero.exists && drive == 0) { printf("Drive0 not present.\n"); return 0; }
	if (!drive_one.exists && drive == 1) { printf("Drive1 not present.\n"); return 0; }
	if (!drive_two.exists && drive == 2) { printf("Drive2 not present.\n"); return 0; }
	if (!drive_three.exists && drive == 3) { printf("Drive3 not present.\n"); return 0; }

	printf("Running ATA PIO test suite on drive %d...\n", drive);

	test_read_sector(drive, 0);      // Read MBR
	test_mbr_dump(drive);            // Dump MBR
	test_lba_walk(drive);            // Walk first 16 LBAs
	test_write_sector(drive, 100);   // Safe test write at LBA100
	test_read_sector(drive, 100);    // Re-read for sanity

	printf("\nAll tests completed.\n");
	return 0;
}

// This is *not* the correct way to do this.
// That said, this is the easiest way to do this...
extern uint8_t* _initrd_data;
extern uint64_t _initrd_size;
// extern "C" {
// 	// This is in some distant place in the FatFS binary.
// 	extern drive_info_t* get_drive_info_ptr(unsigned char pdrv);
// }

#include <drivers/sata/atapi_pio.h>


static drive_info_t* disk_map[] = {
	NULL, // pdrv 0
	&drive_zero,    // pdrv 1
	&drive_one,     // pdrv 2
	&drive_two,     // pdrv 3
	&drive_three    // pdrv 4
};

#define NUM_DRIVES (sizeof(disk_map) / sizeof(disk_map[0]))

drive_info_t* get_drive_info_ptr(uint8_t pdrv) {
	if (pdrv >= NUM_DRIVES) return NULL;
	return disk_map[pdrv];
}

int get_drive_info(const int argc, char** argv) {
	// If a specific drive number is provided
	if (argc > 1) {
		int pdrv = argv[1][0] - '0';

		if (pdrv == 0) {
			printf("Drive 0: RamFS (initrd)\n");
			printf("Size: %llu Bytes\n", _initrd_size);
			return 0;
		}

		// Map FatFS pdrv 1-4 back to hardware pointers for detailed info
		drive_info_t* drive = get_drive_info_ptr(pdrv);
		if (drive && drive->exists) {
			// We pass drive->hardware_id so the low-level print knows which SATA port it is
			print_sata_device_info(drive, drive->hardware_id);
		} else {
			printf("Drive %d not connected or invalid.\n", pdrv);
		}
		return 0;
	}

	printf("The following drives are mapped:\n");

	// Loop through our FatFS mapping (0 to 4)
	for (int i = 0; i < 5; i++) {
		drive_info_t* drive = get_drive_info_ptr(i);

		if (i == 0) {
			printf("drive0 (RamFS) -> %llu Bytes\n", _initrd_size);
			continue;
		}

		if (drive && drive->exists) {
			printf("drive%d (SATA)  -> ", i);
			if (!drive->atapi) {
				printf("%llu Bytes", get_capacity_bytes(&(drive->identify)));
			} else {
				uint32_t last_lba, block_size;
				// Use the hardware_id for the ATAPI call
				if (atapi_read_capacity(drive->hardware_id, &last_lba, &block_size))
					printf("ATAPI, %llu Bytes", (uint64_t) (last_lba + 1) * block_size);
				else
					printf("ATAPI (no media)");
			}
			printf("\n");
		}
	}

	return 0;
}


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// WDM Glue
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

#define PIO_SECTOR_SIZE  512u
#define PIO_LBA28_MAX    0x0FFFFFFFull  // Highest LBA addressable in 28-bit mode
#define PIO_LBA48_MAX    0x0000FFFFFFFFFFFFull  // 48-bit ceiling (2^48 - 1)

// Per-drive context. Static array since we have exactly 4 possible PIO drives.
typedef struct {
	drive_info_t* drive;
	int           drive_number;
	bool          use_lba48;    // Cached from IDENTIFY at registration time
} pio_wdm_ctx_t;

static pio_wdm_ctx_t   pio_wdm_contexts[4];
static WDM_DriveHandle pio_wdm_handles[4];

/* Decides whether a given (lba, count) pair must use LBA48. */
static inline bool needs_lba48(pio_wdm_ctx_t* c, uint64_t lba, uint32_t count) {
	uint64_t end = lba + (uint64_t) count - 1;
	return (end > PIO_LBA28_MAX) || c->use_lba48;
}

// ------------------------------------------------------------------------------------------------
// VTable callbacks
// ------------------------------------------------------------------------------------------------

WDM_Status pio_wdm_read(void* ctx, WDM_LBA lba, uint32_t count, void* buf, WDM_IOFlags flags) {
	(void) flags;
	pio_wdm_ctx_t* c = (pio_wdm_ctx_t*) ctx;
	uint8_t* dst = (uint8_t*) buf;
	uint32_t remaining = count;
	uint64_t cur_lba = lba;

	while (remaining > 0) {
		if (needs_lba48(c, cur_lba, remaining)) {
			// LBA48 sector_count is uint16_t, max 65535 per call.
			// In practice a single WDM call won't exceed AHCI_PRD_MAX_BYTES worth of sectors anyway.
			uint16_t batch = (remaining > 0xFFFF) ? 0xFFFF : (uint16_t) remaining;

			if (!sata_pio_read48(c->drive_number, cur_lba, batch, dst)) {
				printf_serial("[PIO][ERROR] drive %d: read48 failed at LBA %llu\r\n", c->drive_number, (unsigned long long)cur_lba);
				return WDM_ERR_IO;
			}

			dst += (size_t) batch * PIO_SECTOR_SIZE;
			cur_lba += batch;
			remaining -= batch;
		} else {
			// LBA28 sector_count is uint8_t, max 255 per call.
			// I think technically passing 0 is supposed to be 256, but that's a bug I don't feel like fixing.
			uint8_t batch = (remaining > 255) ? 255 : (uint8_t) remaining;

			if (!sata_pio_read28(c->drive_number, (uint32_t) cur_lba, batch, dst)) {
				printf_serial("[PIO][ERROR] drive %d: read28 failed at LBA %u\r\n", c->drive_number, (uint32_t) cur_lba);
				return WDM_ERR_IO;
			}

			dst += (size_t) batch * PIO_SECTOR_SIZE;
			cur_lba += batch;
			remaining -= batch;
		}
	}

	return WDM_OK;
}

WDM_Status pio_wdm_write(void* ctx, WDM_LBA lba, uint32_t count, const void* buf, WDM_IOFlags flags) {
	// WDM_FLAG_SYNC is a no-op. Both write paths flush unconditionally.
	(void) flags;
	pio_wdm_ctx_t* c = (pio_wdm_ctx_t*) ctx;
	const uint8_t* src = (const uint8_t*) buf;
	uint32_t remaining = count;
	uint64_t cur_lba = lba;

	while (remaining > 0) {
		if (needs_lba48(c, cur_lba, remaining)) {
			uint16_t batch = (remaining > 0xFFFF) ? 0xFFFF : (uint16_t) remaining;

			if (!sata_pio_write48(c->drive_number, cur_lba, batch, src)) {
				printf_serial("[PIO][ERROR] drive %d: write48 failed at LBA %llu\r\n", c->drive_number, (unsigned long long)cur_lba);
				return WDM_ERR_IO;
			}

			src += (size_t) batch * PIO_SECTOR_SIZE;
			cur_lba += batch;
			remaining -= batch;
		} else {
			uint8_t batch = (remaining > 255) ? 255 : (uint8_t) remaining;

			if (!sata_pio_write28(c->drive_number, (uint32_t) cur_lba, batch, src)) {
				printf_serial("[PIO][ERROR] drive %d: write28 failed at LBA %u\r\n", c->drive_number, (uint32_t) cur_lba);
				return WDM_ERR_IO;
			}

			src += (size_t) batch * PIO_SECTOR_SIZE;
			cur_lba += batch;
			remaining -= batch;
		}
	}

	return WDM_OK;
}

WDM_Status pio_wdm_flush(void* ctx) {
	pio_wdm_ctx_t* c = (pio_wdm_ctx_t*) ctx;

	uint16_t data_reg, sec_count_reg, lba_low_reg, lba_mid_reg, lba_high_reg, drv_reg, cmd_reg;
	if (!resolve_drive_registers(c->drive_number, data_reg, sec_count_reg, lba_low_reg, lba_mid_reg, lba_high_reg, drv_reg, cmd_reg)) {
		return WDM_ERR_INVALID;
	}

	// Use the extended flush if the drive supports LBA48, basic otherwise.
	// Both block until BSY clears.
	uint8_t flush_cmd = c->use_lba48 ? COMMAND_FLUSH_CACHE_EXT : COMMAND_FLUSH_CACHE;
	outb(cmd_reg, flush_cmd);
	ata_wait_bsy(cmd_reg);

	return WDM_OK;
}

void pio_wdm_on_detach(void* ctx) {
	// Contexts are static, there is nothing to free.
	// Zero the slot so it's clean if the drive is re-detected later (which it shouldn't be but whatever).
	memset(ctx, 0, sizeof(pio_wdm_ctx_t));
}

static const WDM_DriverOps pio_wdm_ops = {
	.read = pio_wdm_read,
	.write = pio_wdm_write,
	.flush = pio_wdm_flush,
	.trim = NULL, // ATA PIO drives don't support TRIM
	.on_attach = NULL, // detect_ide_drives already handled hardware init, PIO isn't plug n play
	.on_detach = pio_wdm_on_detach,
};

// ------------------------------------------------------------------------------------------------
// Registration
// ------------------------------------------------------------------------------------------------

void pio_wdm_register_drive(int drive_number, drive_info_t* info) {
	if (!info->exists) return;

	if (info->atapi) {
		// ATAPI needs SCSI PACKET commands, not raw sector R/W.
		// Register when ATAPI support is added.
		printf_serial("[PIO] drive %d is ATAPI, skipping WDM registration\r\n", drive_number);
		return;
	}

	pio_wdm_ctx_t* c = &pio_wdm_contexts[drive_number];
	c->drive = info;
	c->drive_number = drive_number;
	c->use_lba48 = (bool) info->identify.command_set_active.big_lba;

	WDM_DriveInfo wdm_info;
	memset(&wdm_info, 0, sizeof(wdm_info));

	if (c->use_lba48) {
		wdm_info.sector_count = ((uint64_t) info->identify.max48_bit_lba[1] << 32) | (uint64_t) info->identify.max48_bit_lba[0];
	} else {
		// Clamp to the 28-bit ceiling so WDM overflow checks are tight
		wdm_info.sector_count = info->identify.user_addressable_sectors;
		if (wdm_info.sector_count > PIO_LBA28_MAX + 1) wdm_info.sector_count = PIO_LBA28_MAX + 1;
	}

	wdm_info.sector_size = PIO_SECTOR_SIZE;
	wdm_info.physical_sector = PIO_SECTOR_SIZE;
	wdm_info.optimal_xfer = c->use_lba48 ? 0xFFFF : 255; // largest single-call batch we can issue
	wdm_info.removable = false;
	wdm_info.read_only = false;
	wdm_info.dma_capable = false; // PIO by definition

	// IDENTIFY strings are raw byte-swapped words
	// same layout as they appear in print_sata_device_info, so we can just copy directly.
	strncpy(wdm_info.model, (const char*) info->identify.model_number, sizeof(wdm_info.model) - 1);
	strncpy(wdm_info.serial, (const char*) info->identify.serial_number, sizeof(wdm_info.serial) - 1);

	WDM_DriveHandle handle = NULL;
	WDM_Status st = WDM_Register(&pio_wdm_ops, c, &wdm_info, &handle);
	if (st != WDM_OK) {
		printf_serial("[PIO][ERROR] drive %d: WDM_Register failed (%d)\r\n", drive_number, (int) st);
		return;
	}

	pio_wdm_handles[drive_number] = handle;
	printf_serial("[PIO] drive %d registered with WDM (LBA%s, %llu sectors)\r\n", drive_number, c->use_lba48 ? "48" : "28", (unsigned long long)wdm_info.sector_count);
}

void pio_wdm_register_all(void) {
	pio_wdm_register_drive(PRIMARY_FIRST, &drive_zero);
	pio_wdm_register_drive(PRIMARY_SECOND, &drive_one);
	pio_wdm_register_drive(SECONDARY_FIRST, &drive_two);
	pio_wdm_register_drive(SECONDARY_SECOND, &drive_three);
}