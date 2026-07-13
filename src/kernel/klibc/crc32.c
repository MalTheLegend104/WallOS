/**
 * @file crc32.c
 * @author Malcolm
 * @brief
 * @version
 * @date 7/5/2026
 */
#include <klibc/crc32.h>

static uint32_t crc32_table[256];
static int crc32_table_initialized = 0;

static void crc32_init_table(void) {
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t crc = i;

		for (uint32_t j = 0; j < 8; j++) {
			if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
			else crc >>= 1;
		}

		crc32_table[i] = crc;
	}
	crc32_table_initialized = 1;
}

uint32_t crc32(const void* data, size_t len) {
	if (!crc32_table_initialized)crc32_init_table();

	const uint8_t* buf = (const uint8_t*) data;
	uint32_t crc = 0xFFFFFFFF;

	for (size_t i = 0; i < len; i++) {
		uint8_t index = (uint8_t) ((crc ^ buf[i]) & 0xFF);
		crc = (crc >> 8) ^ crc32_table[index];
	}

	return crc ^ 0xFFFFFFFF;
}