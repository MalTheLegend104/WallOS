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
		default: return false;
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

	uint8_t status = inb(command_register); // get the status from the command port
	if (status == 0) return false; // no drive

	// Wait for BSY bit to clear
	while (status & 0x80) {
		status = inb(command_register);
	}

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
	while (((status = inb(command_register)) & (0x40 | 0x01)) == 0) { }

	if (status & 0x01) return false; // It failed to give the command, we just ignore it.

	// 256 WORDs from the data_register
	for (int i = 0; i < 256; i++) {
		((uint16_t*) current)[i] = inw(data_register);
	}
	return true;
}

void detect_ide_drives() {
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

int get_capacity_bytes(const sata_device_identify* device) {
	uint64_t sectors = device->user_addressable_sectors;  // Assume 28-bit by default

	// If 48-bit LBA is supported, use the 48-bit sector count
	if (device->command_set_active.big_lba) {
		sectors = ((uint64_t) device->max48_bit_lba[1] << 32) | device->max48_bit_lba[0];
	}

	// Sector size is typically 512 bytes
	return (sectors * 512);
}

int get_capacity_mb(const sata_device_identify* device) {
	return (get_capacity_bytes(device) / 1024 / 1024);
}

void print_sata_device_info(drive_info_t* info) {
	if (!info->exists) {
		printf("Drive not connected.\n");
		return;
	}

	if (info->atapi) {
		printf("ATAPI device connected.\n");
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
	do {
		status = inb(command_register);
	} while (status & 0x80); // BSY
}

static void ata_wait_drq(const uint16_t command_register) {
	uint8_t status;
	do {
		status = inb(command_register);
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

int get_drive_info(const int argc, char** argv) {
	if (argc > 1) {
		if (strcmp(argv[1], "0") == 0) print_sata_device_info(&drive_zero);
		else if (strcmp(argv[1], "1") == 0) print_sata_device_info(&drive_one);
		else if (strcmp(argv[1], "2") == 0) print_sata_device_info(&drive_two);
		else if (strcmp(argv[1], "3") == 0) print_sata_device_info(&drive_three);
		else printf("Unrecognized drive number.\n");
		return 0;
	}

	printf("The following drives are connected:\n");

	if (drive_zero.exists) {
		printf("drive0 -> ");
		if (!drive_zero.atapi) printf("%d Bytes", get_capacity_bytes(&(drive_zero.identify)));
		else printf("ATAPI");
		printf("\n");
	}

	if (drive_one.exists) {
		printf("drive1 -> ");
		if (!drive_one.atapi) printf("%d Bytes", get_capacity_bytes(&(drive_one.identify)));
		else printf("ATAPI");
		printf("\n");
	}

	if (drive_two.exists) {
		printf("drive2 -> ");
		if (!drive_two.atapi) printf("%d Bytes", get_capacity_bytes(&(drive_two.identify)));
		else printf("ATAPI");
		printf("\n");
	}

	if (drive_three.exists) {
		printf("drive3 -> ");
		if (!drive_three.atapi) printf("%d Bytes", get_capacity_bytes(&(drive_three.identify)));
		else printf("ATAPI");
		printf("\n");
	}

	return 0;
}