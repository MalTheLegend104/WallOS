/**
 * @file wallos_gpt.c
 * @author Malcolm
 * @brief
 * @version
 * @date 7/5/2026
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <filesystem/partitions/wallos_gpt.h>
#include <memory/kernel_alloc.h>
#include <klibc/crc32.h>

/**
 * @brief Read a little-endian 64-bit value out of a buffer.
 * @param buf Source buffer.
 * @param offset Byte offset to read from.
 * @return The decoded 64-bit value.
 */
static inline uint64_t read64_gpt(const uint8_t* buf, const size_t offset) {
	return (uint64_t) buf[offset] |
		((uint64_t) buf[offset + 1] << 8) |
		((uint64_t) buf[offset + 2] << 16) |
		((uint64_t) buf[offset + 3] << 24) |
		((uint64_t) buf[offset + 4] << 32) |
		((uint64_t) buf[offset + 5] << 40) |
		((uint64_t) buf[offset + 6] << 48) |
		((uint64_t) buf[offset + 7] << 56);
}

/**
 * @brief Read a little-endian 32-bit value out of a buffer.
 * @param buf Source buffer.
 * @param offset Byte offset to read from.
 * @return The decoded 32-bit value.
 */
static inline uint32_t read32_gpt(const uint8_t* buf, const size_t offset) {
	return (uint32_t) buf[offset] |
		((uint32_t) buf[offset + 1] << 8) |
		((uint32_t) buf[offset + 2] << 16) |
		((uint32_t) buf[offset + 3] << 24);
}

/**
 * @brief Read a little-endian 16-bit value out of a buffer.
 * @param buf Source buffer.
 * @param offset Byte offset to read from.
 * @return The decoded 16-bit value.
 */
static inline uint16_t read16_gpt(const uint8_t* buf, const size_t offset) {
	return (uint16_t) buf[offset] |
		((uint16_t) buf[offset + 1] << 8);
}

/**
 * @brief Write a 64-bit value into a buffer in little-endian byte order.
 * @param buf Destination buffer. Must have at least 8 bytes available.
 * @param value Value to write.
 */
static inline void write64_gpt(uint8_t* buf, uint64_t value) {
	buf[0] = value & 0xFF;
	buf[1] = (value >> 8) & 0xFF;
	buf[2] = (value >> 16) & 0xFF;
	buf[3] = (value >> 24) & 0xFF;
	buf[4] = (value >> 32) & 0xFF;
	buf[5] = (value >> 40) & 0xFF;
	buf[6] = (value >> 48) & 0xFF;
	buf[7] = (value >> 56) & 0xFF;
}

/**
 * @brief Write a 32-bit value into a buffer in little-endian byte order.
 * @param buf Destination buffer. Must have at least 4 bytes available.
 * @param value Value to write.
 */
static inline void write32_gpt(uint8_t* buf, uint32_t value) {
	buf[0] = value & 0xFF;
	buf[1] = (value >> 8) & 0xFF;
	buf[2] = (value >> 16) & 0xFF;
	buf[3] = (value >> 24) & 0xFF;
}

/**
 * @brief Write a 16-bit value into a buffer in little-endian byte order.
 * @param buf Destination buffer. Must have at least 2 bytes available.
 * @param value Value to write.
 */
static inline void write16_gpt(uint8_t* buf, uint16_t value) {
	buf[0] = value & 0xFF;
	buf[1] = (value >> 8) & 0xFF;
}

void gpt_name_to_ascii(const uint16_t* utf16_name, char* out_ascii, size_t max_len) {
	for (size_t i = 0; i < max_len - 1 && utf16_name[i] != 0; i++) {
		// Only convert basic ASCII
		// replace high-order characters with '?'
		out_ascii[i] = (utf16_name[i] < 128) ? (char) utf16_name[i] : '?';
	}
	out_ascii[max_len - 1] = '\0';
}

/**
 * @brief Convert a single ASCII hex character to its 4-bit value.
 * @param c Character to convert ('0'-'9', 'a'-'f', 'A'-'F').
 * @return Value 0-15 on success, -1 if c is not a valid hex digit.
 */
static inline int hex_digit_value(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/**
 * @brief Parse exactly two ASCII hex characters into a byte.
 * @param str Pointer to the first hex character.
 * @param out Destination for the parsed byte.
 * @return true on success, false if either character isn't valid hex.
 */
static bool hex_byte(const char* str, uint8_t* out) {
	int hi = hex_digit_value(str[0]);
	int lo = hex_digit_value(str[1]);
	if (hi < 0 || lo < 0) return false;

	*out = (uint8_t) ((hi << 4) | lo);
	return true;
}

/**
 * @brief Parse a run of hex characters into a big-endian unsigned value.
 * @param str Pointer to the first hex character.
 * @param num_chars Number of hex characters to consume (8 for a uint32_t for example).
 * @param out Destination for the parsed value.
 * @return true on success, false on any invalid hex character.
 */
static bool hex_value(const char* str, int num_chars, uint32_t* out) {
	uint32_t value = 0;

	for (int i = 0; i < num_chars; i++) {
		int digit = hex_digit_value(str[i]);
		if (digit < 0) return false;
		value = (value << 4) | (uint32_t) digit;
	}

	*out = value;
	return true;
}

/**
 * @brief Parse a canonical GUID string ("XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX") back into raw on-disk (mixed-endian) bytes.
 *
 * @param guid Destination 16-byte buffer.
 * @param str GUID string. Must be exactly 36 characters. Does not need to be null-terminated as long as at least 36 valid bytes are readable.
 * @return GPT_NO_ERROR on success, GPT_BAD_PARAM on malformed input.
 */
gpt_error_t guid_from_string(uint8_t guid[16], const char* str) {
	if (!guid || !str) return GPT_BAD_PARAM;

	// Dashes must land at these fixed positions.
	if (str[8] != '-' || str[13] != '-' || str[18] != '-' || str[23] != '-') {
		return GPT_BAD_PARAM;
	}

	uint32_t data1, data2, data3;
	if (!hex_value(str, 8, &data1)) return GPT_BAD_PARAM;
	if (!hex_value(str + 9, 4, &data2)) return GPT_BAD_PARAM;
	if (!hex_value(str + 14, 4, &data3)) return GPT_BAD_PARAM;

	write32_gpt(guid, data1);
	write16_gpt(guid + 4, (uint16_t) data2);
	write16_gpt(guid + 6, (uint16_t) data3);

	// Remaining 8 bytes are two dash-separated groups: 2 hex chars + 12 hex chars.
	static const int byte_offsets[8] = { 19, 21, 24, 26, 28, 30, 32, 34 };
	for (int i = 0; i < 8; i++) {
		if (!hex_byte(str + byte_offsets[i], &guid[8 + i])) return GPT_BAD_PARAM;
	}

	return GPT_NO_ERROR;
}

/**
 * @brief Format a raw on-disk GUID as a canonical mixed-endian string.
 *
 * GPT/UEFI GUIDs store the first three fields little-endian and the last two big-endian.
 * This is the "correct" textual representation (matches what tools like blkid/gdisk print).
 *
 * @param guid 16-byte on-disk GUID.
 * @param out Destination buffer, must be at least 37 bytes (36 chars + NUL).
 */
void guid_to_string(const uint8_t guid[16], char* out) {
	uint32_t data1 = read32_gpt(guid, 0);
	uint16_t data2 = read16_gpt(guid, 4);
	uint16_t data3 = read16_gpt(guid, 6);

	// Yes this is awful, just trust that it's correct
	snprintf(out, 37,
		"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
		data1, data2, data3, guid[8], guid[9], guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]);
}

/**
 * @brief Check if a GUID is all-zero (an unused/null GUID).
 * @param guid 16-byte GUID buffer.
 * @return true if every byte is zero.
 */
static inline bool guid_is_null(const uint8_t guid[16]) {
	static const uint8_t zero_guid[16] = { 0 };
	return memcmp(guid, zero_guid, 16) == 0;
}

/**
 * @brief Compare two GUIDs for equality.
 * @param a First 16-byte GUID.
 * @param b Second 16-byte GUID.
 * @return true if they match byte-for-byte.
 */
static inline bool guid_equal(const uint8_t a[16], const uint8_t b[16]) {
	return memcmp(a, b, 16) == 0;
}

/**
 * @brief Identify a partition type GUID as one of the well-known enum values.
 * @param guid 16-byte on-disk partition type GUID to identify.
 * @return The matching gpt_partition_type_id_t, or GPT_TYPE_UNKNOWN if not recognized.
 */
gpt_partition_type_id_t gpt_partition_type_from_guid(const uint8_t guid[16]) {
	for (size_t i = 0; i < GPT_KNOWN_PARTITION_TYPE_COUNT; i++) {
		if (guid_equal(guid, gpt_known_partition_types[i].guid)) {
			return gpt_known_partition_types[i].id;
		}
	}
	return GPT_TYPE_UNKNOWN;
}

/**
 * @brief Get the human-readable name for a partition type enum value.
 * @param id A gpt_partition_type_id_t value.
 * @return Display name, or "Unknown" if id is GPT_TYPE_UNKNOWN or unrecognized.
 */
const char* gpt_partition_type_name(gpt_partition_type_id_t id) {
	for (size_t i = 0; i < GPT_KNOWN_PARTITION_TYPE_COUNT; i++) {
		if (gpt_known_partition_types[i].id == id) {
			return gpt_known_partition_types[i].name;
		}
	}
	return "Unknown";
}

/**
 * @brief Look up the raw GUID bytes for a "well-known" partition type
 * @param id A gpt_partition_type_id_t value.
 * @return Pointer to the 16-byte on-disk GUID, or NULL if id is unrecognized.
 */
const uint8_t* gpt_guid_from_partition_type(gpt_partition_type_id_t id) {
	for (size_t i = 0; i < GPT_KNOWN_PARTITION_TYPE_COUNT; i++) {
		if (gpt_known_partition_types[i].id == id) {
			return gpt_known_partition_types[i].guid;
		}
	}
	return NULL;
}

gpt_error_t detect_gpt(gpt_partition_table_t* gpt, const uint8_t* data, const size_t size, gpt_flags flags) {
	// MBR requires the sector is at least 512 bytes
	if (!data || !gpt || size < 512) return GPT_BAD_PARAM;

	parse_mbr(&gpt->protective_mbr, data, size);

	const bool allow_malformed_table = flags & GPT_ALLOW_MALFORMED_TABLE;
	const bool strict_pmbr = flags & GPT_STRICT_PMBR;

	// We check every partition entry for the GPT signature and verify it.
	// Technically a GPT drive is supposed to only have a single MBR partition entry and no other partitions
	// We only care about this case if GPT_STRICT_PMBR is set.
	uint8_t partition_count = 0;
	for (uint8_t i = 0; i < 4; i++) {
		if (gpt->protective_mbr.partition_entries[i].partition_type != 0x00)
			partition_count++;
		if (gpt->protective_mbr.partition_entries[i].partition_type == GPT_PROTECTIVE_MBR) {
			bool valid = gpt->protective_mbr.partition_entries[i].chs_address == 0x000200;
			if (!valid && strict_pmbr) return GPT_INVALID_PMBR;
			if (!valid && !allow_malformed_table) continue; // rather than hard stopping, we just try to parse the rest of the entries
			// ending CHS is not technically required to be 0xFFFFFF
			// If the storage media can fit in the range for CHS it can technically be set properly
			// valid = gpt->protective_mbr.partition_entries[i].chs_address_last == 0xFFFFFF;
			// if (!valid) continue;

			// Starting LBA *should* be 1 for all GPT
			valid = gpt->protective_mbr.partition_entries[i].lba_start == 0x1;
			if (!valid && strict_pmbr) return GPT_INVALID_PMBR;
			if (!valid && !allow_malformed_table) continue;

			// Ending LBA doesnt matter
			gpt->gpt_partition_entry = i;
			return GPT_NO_ERROR;
		}
	}

	if (partition_count != 1) return GPT_INVALID_PMBR;

	return GPT_NO_PMBR;
}

/**
 * @internal
 * @brief Read the on-disk partition entry array and verify its CRC32 against the value stored in the GPT header (partition_array_crc32).
 *
 * @param drive Drive handle to read from.
 * @param header Already-parsed GPT header (must have valid num_partition_entries, partition_entry_size, and start_partition_entries_lba fields).
 * @param drive_info Drive geometry info, used to compute how many sectors to read.
 * @retval GPT_NO_ERROR on match
 * @retval GPT_BAD_CRC on mismatch
 * @retval GPT_IO_ERROR on read failure
 * @retval GPT_OUT_OF_MEMORY if the scratch buffer couldn't be allocated
 */
static gpt_error_t verify_partition_array_crc32(WDM_DriveHandle drive, const gpt_partition_header_t* header, const WDM_DriveInfo* drive_info) {
	// Nothing to verify if there are no entries
	if (header->num_partition_entries == 0) return GPT_NO_ERROR;

	uint32_t total_entries_size = header->num_partition_entries * header->partition_entry_size;
	uint32_t total_sectors = (total_entries_size + drive_info->sector_size - 1) / drive_info->sector_size;

	// We need the whole array in one contiguous buffer since crc32() here isn't incremental
	// The user provided buf is only guaranteed to be size of the sector
	uint8_t* array_buf = (uint8_t*) kalloc((size_t) total_sectors * drive_info->sector_size);
	if (!array_buf) return GPT_OUT_OF_MEMORY;

	WDM_Status status = WDM_Read(drive, header->start_partition_entries_lba, total_sectors, array_buf, WDM_FLAG_NONE);
	if (status != WDM_OK) {
		kfree(array_buf);
		return GPT_IO_ERROR;
	}

	uint32_t crc = crc32(array_buf, total_entries_size);
	kfree(array_buf);

	if (crc != header->partition_array_crc32) return GPT_BAD_CRC;

	return GPT_NO_ERROR;
}

gpt_error_t parse_gpt_header(gpt_partition_header_t* header, const uint8_t* buf, const size_t size) {
	if (!buf || !header || size < 512) return GPT_BAD_PARAM;

	memcpy(header->signature, buf, 8);
	header->signature[8] = '\0';

	header->gpt_revision = read32_gpt(buf, 0x08);
	header->header_size = read32_gpt(buf, 0x0C);
	header->crc32 = read32_gpt(buf, 0x10);
	header->reserved = read32_gpt(buf, 0x14);
	header->header_lba = read64_gpt(buf, 0x18);
	header->alt_header_lba = read64_gpt(buf, 0x20);
	header->first_usable_lba = read64_gpt(buf, 0x28);
	header->last_usable_lba = read64_gpt(buf, 0x30);
	memcpy(header->disk_guid, buf + 0x38, 16);
	header->start_partition_entries_lba = read64_gpt(buf, 0x48);
	header->num_partition_entries = read32_gpt(buf, 0x50);
	header->partition_entry_size = read32_gpt(buf, 0x54);
	header->partition_array_crc32 = read32_gpt(buf, 0x58);

	return GPT_NO_ERROR;
}

gpt_error_t parse_gpt_partition_entry(gpt_partition_entry_t* entry, const uint8_t* buf, const size_t size, uint64_t entry_lba, uint64_t entry_offset) {
	if (!entry || !buf) return GPT_BAD_PARAM;
	if (size < 128) return GPT_BAD_PARAM;

	// We zero it so we don't have garabage data on accident.
	// Not strictly necessary but whatever
	memset(entry, 0, sizeof(*entry));

	entry->entry_info.lba = entry_lba;
	entry->entry_info.offset = entry_offset;

	memcpy(entry->partition_type_guid, buf + 0x00, 16);
	memcpy(entry->unique_partition_guid, buf + 0x10, 16);

	entry->first_lba = read64_gpt(buf, 0x20);
	entry->last_lba = read64_gpt(buf, 0x28);
	entry->attributes = read64_gpt(buf, 0x30);

	memcpy(entry->partition_name, buf + 0x38, 72);

	return GPT_NO_ERROR;
}

void write_gpt_header(const gpt_partition_header_t* header, uint8_t* buf, size_t buf_size) {
	if (!header || !buf || buf_size < GPT_SECTOR_SIZE) return;

	// Spec requires the rest of the header sector to be zero
	memset(buf, 0, buf_size);

	memcpy(buf + 0x00, header->signature, 8); // signature[8] is just our own NUL terminator, not on-disk
	write32_gpt(buf + 0x08, header->gpt_revision);
	write32_gpt(buf + 0x0C, header->header_size);
	write32_gpt(buf + 0x10, header->crc32);
	write32_gpt(buf + 0x14, header->reserved);
	write64_gpt(buf + 0x18, header->header_lba);
	write64_gpt(buf + 0x20, header->alt_header_lba);
	write64_gpt(buf + 0x28, header->first_usable_lba);
	write64_gpt(buf + 0x30, header->last_usable_lba);
	memcpy(buf + 0x38, header->disk_guid, 16);
	write64_gpt(buf + 0x48, header->start_partition_entries_lba);
	write32_gpt(buf + 0x50, header->num_partition_entries);
	write32_gpt(buf + 0x54, header->partition_entry_size);
	write32_gpt(buf + 0x58, header->partition_array_crc32);
	// 0x5C onward is reserved and already zeroed above.
}

void write_gpt_partition_entry(const gpt_partition_entry_t* entry, uint8_t* buf) {
	if (!entry || !buf) return;

	// entry_info is for bookkeeping only, we ignore it here
	memset(buf, 0, 128);
	memcpy(buf + 0x00, entry->partition_type_guid, 16);
	memcpy(buf + 0x10, entry->unique_partition_guid, 16);
	write64_gpt(buf + 0x20, entry->first_lba);
	write64_gpt(buf + 0x28, entry->last_lba);
	write64_gpt(buf + 0x30, entry->attributes);
	memcpy(buf + 0x38, entry->partition_name, 72);
}

gpt_error_t gpt_serialize_partition_array(const gpt_partition_table_t* gpt, uint8_t** out_buf, uint64_t* out_size) {
	if (!gpt || !out_buf || !out_size) return GPT_BAD_PARAM;

	const gpt_partition_header_t* header = &gpt->header;

	// gpt->entries doesn't need to be "dense" (sized to header.num_partition_entries)
	// Everything from num_entries up to num_partition_entries is synthesized here as a blank/kfree entry
	if (gpt->num_entries > header->num_partition_entries) return GPT_BAD_PARTITION_TABLE;
	if (gpt->num_entries > 0 && !gpt->entries) return GPT_BAD_PARAM;
	if (header->partition_entry_size < 128) return GPT_BAD_PARTITION_TABLE;

	uint64_t total_size = (uint64_t) header->num_partition_entries * header->partition_entry_size;

	if (total_size == 0) {
		*out_buf = NULL;
		*out_size = 0;
		return GPT_NO_ERROR;
	}

	uint8_t* buf = (uint8_t*) kcalloc(1, (size_t) total_size);
	if (!buf) return GPT_OUT_OF_MEMORY;

	// Only the first gpt->num_entries slots get written.
	for (uint32_t i = 0; i < gpt->num_entries; i++) {
		write_gpt_partition_entry(&gpt->entries[i], buf + (uint64_t) i * header->partition_entry_size);
	}

	*out_buf = buf;
	*out_size = total_size;
	return GPT_NO_ERROR;
}

/**
 * @internal
 * @brief Serialize and write one table's partition entry array (primary or backup) to disk.
 *
 * @param drive Drive to write to.
 * @param gpt Table whose array should be written (uses header.start_partition_entries_lba as the destination).
 * @param drive_info Drive geometry, used to round the array up to whole sectors.
 * @param io_flags Forwarded to WDM_Write().
 */
static gpt_error_t gpt_write_partition_array(WDM_DriveHandle drive, const gpt_partition_table_t* gpt, const WDM_DriveInfo* drive_info, WDM_IOFlags io_flags) {
	uint8_t* array_buf = NULL;
	uint64_t array_size = 0;

	gpt_error_t err = gpt_serialize_partition_array(gpt, &array_buf, &array_size);
	if (err != GPT_NO_ERROR) return err;

	if (array_size == 0) return GPT_NO_ERROR; // num_partition_entries == 0, nothing to write

	uint32_t total_sectors = (uint32_t) ((array_size + drive_info->sector_size - 1) / drive_info->sector_size);
	uint64_t padded_size = (uint64_t) total_sectors * drive_info->sector_size;

	// The array won't always land on a sector boundary
	// Pad with zeros so we can write whole sectors
	uint8_t* write_buf = array_buf;
	if (padded_size != array_size) {
		write_buf = (uint8_t*) kcalloc(1, (size_t) padded_size);
		if (!write_buf) { kfree(array_buf); return GPT_OUT_OF_MEMORY; }
		memcpy(write_buf, array_buf, (size_t) array_size);
		kfree(array_buf);
	}

	WDM_Status status = WDM_Write(drive, gpt->header.start_partition_entries_lba, total_sectors, write_buf, io_flags);
	kfree(write_buf);

	return (status == WDM_OK) ? GPT_NO_ERROR : GPT_IO_ERROR;
}

gpt_error_t gpt_write(WDM_DriveHandle drive, const gpt_partition_table_t* primary, const gpt_partition_table_t* backup, gpt_write_flags_t flags) {
	if (!drive || !primary) return GPT_BAD_PARAM;

	WDM_DriveInfo drive_info;
	if (WDM_GetInfo(drive, &drive_info) != WDM_OK) return GPT_IO_ERROR;
	if (drive_info.sector_size < GPT_SECTOR_SIZE) return GPT_BAD_PARAM;

	const WDM_IOFlags io_flags = (flags & GPT_WRITE_SYNC) ? WDM_FLAG_SYNC : WDM_FLAG_NONE;
	gpt_error_t result = GPT_NO_ERROR;

	uint8_t* sector_buf = (uint8_t*) kalloc(drive_info.sector_size);
	if (!sector_buf) return GPT_OUT_OF_MEMORY;

	// PMBR is always LBA 0
	write_mbr(&primary->protective_mbr, sector_buf, drive_info.sector_size);
	if (WDM_Write(drive, 0, 1, sector_buf, io_flags) != WDM_OK) { result = GPT_IO_ERROR; goto cleanup; }

	// Header & array
	write_gpt_header(&primary->header, sector_buf, drive_info.sector_size);
	if (WDM_Write(drive, primary->header.header_lba, 1, sector_buf, io_flags) != WDM_OK) { result = GPT_IO_ERROR; goto cleanup; }

	result = gpt_write_partition_array(drive, primary, &drive_info, io_flags);
	if (result != GPT_NO_ERROR) goto cleanup;

	// Backup header + partition array
	if (backup) {
		write_gpt_header(&backup->header, sector_buf, drive_info.sector_size);
		if (WDM_Write(drive, backup->header.header_lba, 1, sector_buf, io_flags) != WDM_OK) { result = GPT_IO_ERROR; goto cleanup; }

		result = gpt_write_partition_array(drive, backup, &drive_info, io_flags);
		if (result != GPT_NO_ERROR) goto cleanup;
	}

	if (flags & GPT_WRITE_SYNC) {
		if (WDM_Flush(drive) != WDM_OK) result = GPT_IO_ERROR;
	}

cleanup:
	kfree(sector_buf);
	return result;
}

gpt_error_t parse_gpt(WDM_DriveHandle drive, uint8_t* buf, const size_t size, gpt_partition_table_t* gpt, gpt_flags flags) {
	if (!gpt || !buf || size < 512) return GPT_BAD_PARAM;

	WDM_DriveInfo drive_info;
	WDM_Status status = WDM_GetInfo(drive, &drive_info); // we need the size of the sectors for a lot of things
	if (status != WDM_OK) return GPT_IO_ERROR;

	// We expect the buffer to fit a single sector
	// If it doesn't it was a bad param
	if (size < drive_info.sector_size) return GPT_BAD_PARAM;

	// Get the first sector to check for Protective MBR
	status = WDM_Read(drive, 0, 1, buf, WDM_FLAG_NONE);
	if (status != WDM_OK) return GPT_IO_ERROR;

	gpt_error_t res = detect_gpt(gpt, buf, size, flags);
	if (res < 0) return res;

	status = WDM_Read(drive, 1, 1, buf, WDM_FLAG_NONE);
	if (status != WDM_OK) return GPT_IO_ERROR;

	res = parse_gpt_header(&(gpt->header), buf, size);
	if (res < 0) return res;

	if (!(flags & GPT_SKIP_CRC_CHECKS)) {
		// We compute and check the CRC32 here because it's simpler than the main parse function.
		write32_gpt((uint8_t*) (buf + 0x10), 0);
		uint32_t crc = crc32(buf, gpt->header.header_size);
		write32_gpt((uint8_t*) (buf + 0x10), gpt->header.crc32); // restore the buf
		if (crc != gpt->header.crc32 && !(flags & GPT_ALLOW_MALFORMED_TABLE)) return GPT_BAD_CRC;
	}

	if (memcmp(buf, "EFI PART", 8) != 0) return GPT_INVALID_SIGNATURE;
	if ((gpt->header.header_size < 92 || gpt->header.header_size > 512) && !(flags & GPT_ALLOW_MALFORMED_TABLE)) return GPT_BAD_HEADER;
	// This is either malformed or some insanely weird partition scheme that we don't want to deal with.
	if (gpt->header.num_partition_entries > 128) return GPT_BAD_PARTITION_TABLE;
	if (gpt->header.partition_entry_size < 128) return GPT_BAD_PARTITION_TABLE;

	/* Everything below this is a mess of parsing the partition table entries.
	 * This takes a two stage approach:
	 * 1. Go through and find the "valid" (non-zero) entries. We do this to avoid having to allocate a massive array that's mostly empty.
	 * 2. Fully parse and keep only the valid entries.
	 */
	// TODO: GPT_PARSE_UNUSED_ENTRIES would be a good flag for debugging

	/* Slight bit more verification. */
	if (gpt->header.partition_entry_size > drive_info.sector_size) return GPT_BAD_PARTITION_TABLE;
	if (!(flags & GPT_SKIP_CRC_CHECKS)) {
		res = verify_partition_array_crc32(drive, &gpt->header, &drive_info);
		if (res != GPT_NO_ERROR && !(flags & GPT_ALLOW_MALFORMED_TABLE)) return res;
	}

	uint32_t total_entries_size = gpt->header.num_partition_entries * gpt->header.partition_entry_size;
	uint32_t total_sectors = (total_entries_size + drive_info.sector_size - 1) / drive_info.sector_size;
	// This is used at the very end for our return status.
	gpt_error_t cleanup_error = GPT_NO_ERROR;
	static const uint8_t zero_guid[16] = { 0 };
	uint32_t entry = 0;
	uint32_t used_entries = 0;

	for (uint32_t sector = 0; sector < total_sectors; sector++) {
		status = WDM_Read(drive, gpt->header.start_partition_entries_lba + sector, 1, buf, WDM_FLAG_NONE);
		if (status != WDM_OK) {
			cleanup_error = GPT_IO_ERROR;
			goto end;
		}

		uint32_t offset = 0;

		while (offset + gpt->header.partition_entry_size <= drive_info.sector_size && entry < gpt->header.num_partition_entries) {
			if (memcmp(buf + offset, zero_guid, 16) != 0) used_entries++;

			offset += gpt->header.partition_entry_size;
			entry++;
		}
	}

	gpt->num_entries = used_entries;

	gpt->entries = (gpt_partition_entry_t*) kalloc(sizeof(gpt_partition_entry_t) * used_entries);
	if (!gpt->entries && used_entries != 0) {
		cleanup_error = GPT_OUT_OF_MEMORY;
		goto end;
	}

	/* For the second loop, we actually properly parse the existing entries.
	 * The logic is mostly the same as the first loop, just actually parsing them.
	 */

	entry = 0;
	uint32_t parsed = 0;
	for (uint32_t sector = 0; sector < total_sectors; sector++) {
		status = WDM_Read(drive, gpt->header.start_partition_entries_lba + sector, 1, buf, WDM_FLAG_NONE);

		if (status != WDM_OK) {
			cleanup_error = GPT_IO_ERROR;
			goto end;
		}

		uint32_t offset = 0;

		while (offset + gpt->header.partition_entry_size <= drive_info.sector_size && entry < gpt->header.num_partition_entries) {
			const uint8_t* entry_buf = buf + offset;

			// Skip unused entries
			if (memcmp(entry_buf, zero_guid, 16) != 0) {
				res = parse_gpt_partition_entry(&gpt->entries[parsed], entry_buf, gpt->header.partition_entry_size, gpt->header.start_partition_entries_lba + sector, offset);

				if (res != GPT_NO_ERROR) {
					cleanup_error = res;
					goto end;
				}

				parsed++;
			}

			offset += gpt->header.partition_entry_size;
			entry++;
		}
	}

	// This *should* be impossible, but we still sanity check it
	if (!(flags & GPT_ALLOW_MALFORMED_TABLE)) {
		if (entry != gpt->header.num_partition_entries) cleanup_error = GPT_BAD_PARTITION_TABLE;
		if (parsed != used_entries) cleanup_error = GPT_BAD_PARTITION_TABLE;
	}

end:
	if (cleanup_error != GPT_NO_ERROR) {
		kfree(gpt->entries);
		gpt->entries = NULL;
	}

	return cleanup_error;
}

uint32_t validate_gpt(const gpt_partition_table_t* gpt) {
	if (!gpt) return GPT_VALID;

	uint32_t result = GPT_VALID;
	const gpt_partition_header_t* header = &gpt->header;

	/* Header Checks */

	if (header->gpt_revision != 0x00010000) result |= GPT_VALIDATION_BAD_REVISION;

	if (header->reserved != 0) result |= GPT_VALIDATION_RESERVED_NONZERO;

	// parse_gpt() always reads the primary header from LBA 1, so that's what
	// header_lba should self-report if it's internally consistent.
	if (header->header_lba != 1) result |= GPT_VALIDATION_HEADER_LBA_MISMATCH;

	if (header->first_usable_lba > header->last_usable_lba) result |= GPT_VALIDATION_USABLE_RANGE_INVERTED;

	if (guid_is_null(header->disk_guid)) result |= GPT_VALIDATION_NULL_DISK_GUID;

	// We can't validate that this is actually correct without taking in drive_info, which we don't really need want to do.
	// We know that a value of zero or that matches the header_lba is inherently wrong.
	if (header->alt_header_lba == 0 || header->alt_header_lba == header->header_lba) {
		result |= GPT_VALIDATION_ALT_HEADER_LBA_INVALID;
	}

	// Spec requires partition_entry_size to be a power-of-two multiple of 128.
	uint32_t entry_size = header->partition_entry_size;
	bool entry_size_ok = false;
	if (entry_size >= 128 && entry_size % 128 == 0) {
		uint32_t multiple = entry_size / 128;
		entry_size_ok = (multiple & (multiple - 1)) == 0; // power-of-two check
	}
	if (!entry_size_ok) result |= GPT_VALIDATION_ENTRY_SIZE_NOT_POW2;


	/* Entry Checks */

	if (gpt->entries) {
		for (uint32_t i = 0; i < gpt->num_entries; i++) {
			const gpt_partition_entry_t* e = &gpt->entries[i];

			if (e->first_lba > e->last_lba) result |= GPT_VALIDATION_ENTRY_LBA_INVERTED;

			if (e->first_lba < header->first_usable_lba || e->last_lba > header->last_usable_lba) {
				result |= GPT_VALIDATION_ENTRY_OUT_OF_RANGE;
			}

			// O(n^2), but n is capped at 128 entries so this isn't horrible
			for (uint32_t k = i + 1; k < gpt->num_entries; k++) {
				const gpt_partition_entry_t* other = &gpt->entries[k];

				if (memcmp(e->unique_partition_guid, other->unique_partition_guid, 16) == 0) {
					result |= GPT_VALIDATION_DUPLICATE_GUID;
				}

				if (e->first_lba <= other->last_lba && other->first_lba <= e->last_lba) {
					result |= GPT_VALIDATION_ENTRY_OVERLAP;
				}
			}
		}
	}

	return result;
}


static void print_guid(const uint8_t guid[16]) {
	for (int i = 0; i < 16; i++) {
		printf("%02X", guid[i]);

		// Canonical GUID formatting
		if (i == 3 || i == 5 || i == 7 || i == 9) printf("-");
	}
}

static void print_utf16le_name(const uint16_t name[36]) {
	for (int i = 0; i < 36 && name[i] != 0; i++) {
		if (name[i] < 0x80) printf("%c", (char) name[i]);
		else printf("?");
	}
}

void print_gpt_validation_result(uint32_t result) {
	if (result == GPT_VALID) {
		printf("GPT Validation            : OK (no issues found)\n");
		return;
	}

	printf("=== GPT Validation Issues ===\n");
	if (result & GPT_VALIDATION_BAD_REVISION)           printf("- GPT revision is not 0x00010000\n");
	if (result & GPT_VALIDATION_RESERVED_NONZERO)       printf("- Header reserved field is non-zero\n");
	if (result & GPT_VALIDATION_HEADER_LBA_MISMATCH)    printf("- header_lba doesn't match the LBA it was read from\n");
	if (result & GPT_VALIDATION_USABLE_RANGE_INVERTED)  printf("- first_usable_lba is greater than last_usable_lba\n");
	if (result & GPT_VALIDATION_NULL_DISK_GUID)         printf("- Disk GUID is all zero\n");
	if (result & GPT_VALIDATION_ALT_HEADER_LBA_INVALID) printf("- alt_header_lba is zero or matches header_lba\n");
	if (result & GPT_VALIDATION_ENTRY_SIZE_NOT_POW2)    printf("- partition_entry_size isn't a power-of-two multiple of 128\n");
	if (result & GPT_VALIDATION_ENTRY_OUT_OF_RANGE)     printf("- One or more entries fall outside the usable LBA range\n");
	if (result & GPT_VALIDATION_ENTRY_LBA_INVERTED)     printf("- One or more entries have first_lba > last_lba\n");
	if (result & GPT_VALIDATION_DUPLICATE_GUID)         printf("- Duplicate unique_partition_guid found across entries\n");
	if (result & GPT_VALIDATION_ENTRY_OVERLAP)          printf("- Two or more entries have overlapping LBA ranges\n");
}


void print_gpt_partition_header(const gpt_partition_header_t* header) {
	printf("=== GPT Header ===\n");
	printf("Signature                 : %s\n", header->signature);
	printf("GPT Revision              : 0x%08X\n", header->gpt_revision);
	printf("Header Size               : %u\n", header->header_size);
	printf("CRC32                     : 0x%08X\n", header->crc32);
	printf("Reserved                  : 0x%08X\n", header->reserved);

	printf("Current Header LBA        : %lu\n", header->header_lba);
	printf("Alternate Header LBA      : %lu\n", header->alt_header_lba);
	printf("First Usable LBA          : %lu\n", header->first_usable_lba);
	printf("Last Usable LBA           : %lu\n", header->last_usable_lba);

	printf("Disk GUID                 : ");
	print_guid(header->disk_guid);
	printf("\n");

	printf("Partition Entry Array LBA : %lu\n", header->start_partition_entries_lba);
	printf("Partition Entry Count     : %i\n", header->num_partition_entries);
	printf("Partition Entry Size      : %i\n", header->partition_entry_size);
	printf("Partition Array CRC32     : 0x%08X\n", header->partition_array_crc32);
}

void print_gpt_partition_entry(const gpt_partition_entry_t* entry, size_t index) {
	printf("=== GPT Partition Entry %zu ===\n", index);
	printf("Entry Info                : LBA=%lu offset=%lu\n", entry->entry_info.lba, entry->entry_info.offset);
	printf("Partition Type GUID       : ");
	print_guid(entry->partition_type_guid);
	printf(" (%s)\n", gpt_partition_type_name(gpt_partition_type_from_guid(entry->partition_type_guid)));

	printf("Unique Partition GUID     : ");
	print_guid(entry->unique_partition_guid);
	printf("\n");

	printf("First LBA                 : %lu\n", entry->first_lba);
	printf("Last LBA                  : %lu\n", entry->last_lba);
	printf("Attributes                : 0x%016X\n", entry->attributes);

	printf("Partition Name            : ");
	print_utf16le_name(entry->partition_name);
	printf("\n");
}

void print_gpt_partition_table(const gpt_partition_table_t* table) {
	printf("========================================\n");
	printf("GPT Partition Table\n");
	printf("========================================\n");

	printf("Protective MBR Entry      : %u\n", table->gpt_partition_entry);

	// print_mbr_partition(&table->protective_mbr, table->gpt_partition_entry);
	print_mbr(&table->protective_mbr);

	printf("\n");

	print_gpt_partition_header(&table->header);

	printf("\n");

	if (table->entries) {
		for (uint32_t i = 0; i < table->num_entries; i++) {
			const gpt_partition_entry_t* entry = &table->entries[i];

			/* Skip completely unused entries. */
			if (!guid_is_null(entry->partition_type_guid)) {
				print_gpt_partition_entry(entry, i);
				printf("\n");
			}
		}
	}
}