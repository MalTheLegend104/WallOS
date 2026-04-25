#include <drivers/sata/pio.h>
#include <drivers/serial.h>
#include <klibc/kprint.h>
#include <cpu_io.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

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
	drive_info_t* drive_info;
	sata_device_identify* current;
	uint8_t drive_select;
	uint16_t drive_register;
	uint16_t command_register;
	uint16_t data_register;
	bool primary = true;

	printf("identify: Starting identification for drive %d\n", drive_number);

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
			printf("identify: TIMEOUT waiting for BSY to clear! Final status = 0x%02x\n", status);
			return false;
		}
	}
	printf("identify: BSY cleared, status = 0x%02x\n", status);

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
	printf("identify: Waiting for DRQ or ERR...\n");
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

	printf("identify: Got DRQ or ERR, status = 0x%02x\n", status);

	if (status & 0x01) {
		printf("identify: ERR bit set, command failed\n");
		return false;
	}

	// 256 WORDs from the data_register
	printf("identify: Reading 256 words from data register\n");
	for (int i = 0; i < 256; i++) {
		((uint16_t*) current)[i] = inw(data_register);
	}

	printf("identify: Successfully read IDENTIFY data\n");
	return true;
}

void detect_ide_drives() {
	printf("Checking for drives...\n");

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
extern "C" {
	// This is in some distant place in the FatFS binary.
	extern drive_info_t* get_drive_info_ptr(unsigned char pdrv);
}

#include <drivers/sata/atapi_pio.h>
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