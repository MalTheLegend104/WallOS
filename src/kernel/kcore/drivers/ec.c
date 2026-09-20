/* This is a *very* small driver for the "embedded controller" on x86_64. ACPICA and uACPI use `extern ec_generic_handler()`. There is no header for this. */

#include <stdint.h>
#include <stdbool.h>
#include <cpu_io.h>
#include <arch.h>

// You're technically supposed to parse the ACPI namespace to get these, 99.9% of the time it's at these spots.
// The only time we use the EC at all is for shutdown.
// We do the old windows 95 "it is now safe to power off your computer" if these don't work, so I don't feel like going through the effort for it.
#define EC_DATA_PORT  0x62
#define EC_CMD_PORT   0x66
#define EC_STAT_PORT  0x66

#define EC_STAT_OBF   (1 << 0) // Output Buffer Full
#define EC_STAT_IBF   (1 << 1) // Input Buffer Full

#define EC_CMD_READ   0x80
#define EC_CMD_WRITE  0x81

#define EC_TIMEOUT    100000

static bool ec_wait_ibf_clear(void) {
	for (int i = 0; i < EC_TIMEOUT; i++) {
		if (!(inb(EC_STAT_PORT) & EC_STAT_IBF)) return true;
		cpu_relax();
	}
	return false;
}

static bool ec_wait_obf_set(void) {
	for (int i = 0; i < EC_TIMEOUT; i++) {
		if (inb(EC_STAT_PORT) & EC_STAT_OBF) return true;
		cpu_relax();
	}
	return false;
}

static bool ec_read_byte(uint8_t address, uint8_t* data) {
	if (!ec_wait_ibf_clear()) return false;
	outb(EC_CMD_PORT, EC_CMD_READ);

	if (!ec_wait_ibf_clear()) return false;
	outb(EC_DATA_PORT, address);

	if (!ec_wait_obf_set()) return false;
	*data = inb(EC_DATA_PORT);

	return true;
}

static bool ec_write_byte(uint8_t address, uint8_t data) {
	if (!ec_wait_ibf_clear()) return false;
	outb(EC_CMD_PORT, EC_CMD_WRITE);

	if (!ec_wait_ibf_clear()) return false;
	outb(EC_DATA_PORT, address);

	if (!ec_wait_ibf_clear()) return false;
	outb(EC_DATA_PORT, data);

	return true;
}

bool ec_generic_handler(bool is_read, uint64_t address, uint32_t bit_width, uint64_t* value) {
	if (bit_width % 8 != 0) return false;

	uint32_t byte_width = bit_width / 8;
	uint64_t final_value = 0;

	for (uint32_t i = 0; i < byte_width; i++) {
		uint8_t byte_addr = (uint8_t) (address + i);
		uint8_t byte_data = 0;

		if (is_read) {
			if (!ec_read_byte(byte_addr, &byte_data)) return false;
			final_value |= ((uint64_t) byte_data << (i * 8));
		} else {
			byte_data = (uint8_t) ((*value >> (i * 8)) & 0xFF);
			if (!ec_write_byte(byte_addr, byte_data)) return false;
		}
	}

	if (is_read) {
		*value = final_value;
	}

	return true;
}