#include <filesystem/iso9660/iso9660.h>
#include <filesystem/vfs.h>
#include <filesystem/wdm.h>

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <memory/kernel_alloc.h>

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Interanl Helpers
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/**
 * Read @p count sectors from the drive in @p ctx into @p buf.
 * Returns true on success; false maps to WDM_ERR_*.
 */
static bool _iso9660_read_sectors(iso9660_ctx_t* ctx, uint32_t lba, uint32_t count, void* buf) {
	WDM_Status s = WDM_Read(ctx->drive, (WDM_LBA) lba, count, buf, WDM_FLAG_NONE);
	return s == WDM_OK;
}

static void _iso9660_trim_trailing_spaces(char* str) {
	int len = (int) strlen(str);
	while (len > 0 && str[len - 1] == ' ') {
		str[--len] = '\0';
	}
}

static inline void _iso9660_read_iso9660_string(char* dst, const uint8_t* buf, int offset, int len) {
	for (int i = 0; i < len; i++) {
		dst[i] = (char) buf[offset + i];
	}
	dst[len] = '\0';
	_iso9660_trim_trailing_spaces(dst);
}

static inline int _iso9660_parse_str_int(const uint8_t* buf, int offset, int len) {
	int value = 0;
	for (int i = 0; i < len; i++) {
		value = value * 10 + (buf[offset + i] - '0');
	}
	return value;
}

static inline iso9660_datetime_t _iso9660_parse_iso9660_datetime(const uint8_t* buf, int offset) {
	iso9660_datetime_t dt;

	dt.year = (uint16_t) _iso9660_parse_str_int(buf, offset + 0, 4);
	dt.month = (uint8_t) _iso9660_parse_str_int(buf, offset + 4, 2);
	dt.day = (uint8_t) _iso9660_parse_str_int(buf, offset + 6, 2);
	dt.hour = (uint8_t) _iso9660_parse_str_int(buf, offset + 8, 2);
	dt.minute = (uint8_t) _iso9660_parse_str_int(buf, offset + 10, 2);
	dt.second = (uint8_t) _iso9660_parse_str_int(buf, offset + 12, 2);
	dt.hundredths = (uint8_t) _iso9660_parse_str_int(buf, offset + 14, 2);

	/* Stored as unsigned 0-100; convert to signed -48..+52 */
	dt.tz_offset = (int8_t) buf[offset + 16] - 48;

	return dt;
}

static inline bool _iso9660_iso_datetime_is_unspecified(const uint8_t* buf, int offset) {
	for (int i = 0; i < 16; i++) {
		if (buf[offset + i] != '0') return false;
	}
	return true;
}

static bool _iso9660_strcmp_ignore_case(const char* a, const char* b) {
	size_t la = strlen(a), lb = strlen(b);
	if (la != lb) return false;
	for (size_t i = 0; i < la; i++) {
		if (tolower((unsigned char) a[i]) != tolower((unsigned char) b[i])) return false;
	}
	return true;
}

/**
 * Compare an ISO 9660 file identifier (possibly with ";1" version suffix) against a plain name.
 */
static bool _iso9660_names_match(const char* iso_name, const char* search_name) {
	size_t iso_len = strlen(iso_name);
	size_t search_len = strlen(search_name);

	/* '.' and '..' are stored as 0x00 / 0x01 bytes. We handle those before calling this */
	if (iso_len == 0 || search_len == 0) return iso_len == search_len;

	// printf("[debug] comparing %s to %s\n", iso_name, search_name);

	/* Strip ";N" version suffix if present */
	if (iso_len >= 2 && iso_name[iso_len - 2] == ';') {
		if (iso_len - 2 == search_len) {
			for (size_t i = 0; i < search_len; i++) {
				if (tolower((unsigned char) iso_name[i]) != tolower((unsigned char) search_name[i])) return false;
			}
			return true;
		}
		return false;
	}

	return _iso9660_strcmp_ignore_case(iso_name, search_name);
}

/**
 * Validate that a sector buffer read from ISO_PVD_LBA looks like a Primary Volume Descriptor.
 * The standard identifier is "CD001" and the volume descriptor type byte marks it as primary.
 */
static bool _iso9660_validate_pvd_signature(const uint8_t* buf) {
	if (buf[1] != 'C' || buf[2] != 'D' || buf[3] != '0' || buf[4] != '0' || buf[5] != '1') return false;
	if (buf[0] != ISO9660_VOLUME_TYPE_PRIMARY) return false;
	return true;
}



/* ------------------------------------------------------------------------------------------------
 * Printing
 * ------------------------------------------------------------------------------------------------ */

void iso9660_print_iso_datetime(const char* label, iso9660_datetime_t dt) {
	printf("\t%s: %04u-%02u-%02u %02u:%02u:%02u\n", label, dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
}

void iso9660_print_directory_record(const iso9660_directory_record_t* record) {
	printf("Directory Record:\n");
	printf("\tLength: %u\n", record->length);
	printf("\tExt Attr Record Length: %u\n", record->extended_attribute_record_length);
	printf("\tExtent LBA: %u (0x%x)\n", record->location_of_extent, record->location_of_extent);
	printf("\tData Length: %u bytes\n", record->data_length);
	printf("\tRecording Time: %04u-%02u-%02u %02u:%02u:%02u (GMT offset: %d * 15min)\n",
		record->recording_time.years + 1900,
		record->recording_time.month,
		record->recording_time.day,
		record->recording_time.hour,
		record->recording_time.minute,
		record->recording_time.second,
		(int8_t) record->recording_time.tz_offset
	);
	printf("\tFlags:\n");
	printf("\t\tHidden: %u\n", record->file_flags.hidden);
	printf("\t\tDirectory: %u\n", record->file_flags.directory);
	printf("\t\tAssociated: %u\n", record->file_flags.associated);
	printf("\t\tRecord Format Info: %u\n", record->file_flags.record);
	printf("\t\tProtection Info: %u\n", record->file_flags.protection);
	printf("\t\tMulti-Extent: %u\n", record->file_flags.multi_extent);
	printf("\tFile Unit Size: %u\n", record->file_unit_size);
	printf("\tInterleave Gap Size: %u\n", record->interleave_gap_size);
	printf("\tVolume Seq Num: %u\n", record->volume_seq_num);

	if (record->file_id_len == 1) {
		if ((uint8_t) record->file_id[0] == 0x00) { printf("\tFile ID: . (current directory)\n");  return; }
		if ((uint8_t) record->file_id[0] == 0x01) { printf("\tFile ID: .. (parent directory)\n"); return; }
	}
	printf("\tFile ID: %s (%u)\n", record->file_id, record->file_id_len);
}

void iso9660_print_primary_volume_descriptor(const iso9660_primary_volume_descriptor_t* pvd) {
	printf("Primary Volume Descriptor: \n");
	printf("\tSystem ID: %s\n", pvd->system_id);
	printf("\tVolume ID: %s (%zu)\n", pvd->volume_id, strlen(pvd->volume_id));
	printf("\tVolume Space Size: %u\n", pvd->volume_space_size);
	printf("\tVolume Set Size: %u\n", pvd->volume_set_size);
	printf("\tVolume Seq Num: %u\n", pvd->volume_seq_num);
	printf("\tLogical Block Size: %u\n", pvd->logical_block_size);
	printf("\tPath Table Size: %u\n", pvd->path_table_size);
	printf("\tType-L Path Table: LBA %u (0x%x)\n", pvd->type_l_path_table, pvd->type_l_path_table);
	printf("\tOptional Type-L Path Table: LBA %u (0x%x)\n", pvd->optional_type_l_path_table, pvd->optional_type_l_path_table);
	printf("\tType-M Path Table: LBA %u (0x%x)\n", pvd->type_m_path_table, pvd->type_m_path_table);
	printf("\tOptional Type-M Path Table: LBA %u (0x%x)\n", pvd->optional_type_m_path_table, pvd->optional_type_m_path_table);
	printf("\n");
	iso9660_print_directory_record(&pvd->dir_record);
	printf("\n");
	ISO_PRINT_STR("Volume Set ID", pvd->volume_set_id);
	ISO_PRINT_STR_FILENAME("Publisher ID", pvd->publisher_id, pvd->publisher_id_filename);
	ISO_PRINT_STR_FILENAME("Data Preparer", pvd->data_preparer_id, pvd->data_preparer_id_filename);
	ISO_PRINT_STR_FILENAME("Application ID", pvd->application_id, pvd->application_id_filename);
	ISO_PRINT_STR("Copyright Filename", pvd->copyright_file_id);
	ISO_PRINT_STR("Abstract Filename", pvd->abstract_file_id);
	ISO_PRINT_STR("Bibliographic Filename", pvd->bibliographic_id);
	if (!pvd->creation_time_is_unspecified)    iso9660_print_iso_datetime("Created", pvd->creation_time);
	if (!pvd->modification_time_is_unspecified) iso9660_print_iso_datetime("Modified", pvd->modification_time);
	if (!pvd->expiration_time_is_unspecified)  iso9660_print_iso_datetime("Expiration", pvd->expiration_time);
	if (!pvd->effective_time_is_unspecified)   iso9660_print_iso_datetime("Effective", pvd->effective_time);
	printf("\tFile structure version: %u\n", pvd->file_structure_ver);
	if (pvd->file_structure_ver != 0x01) printf("[ISO9660][WARN] File structure version not 0x01...\n");
	if (pvd->offset_882_reserved != 0x00) printf("[ISO9660][WARN] Offset 882 is not zero...\n");
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Init
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------


bool iso9660_init(iso9660_ctx_t* ctx, WDM_DriveHandle drive) {
	if (!ctx || !drive) return false;

	ctx->drive = drive;
	ctx->sector_size = ISO_SECTOR_SIZE;
	ctx->root_lba = 0;
	ctx->root_size = 0;

	/* Read the PVD at LBA 16 to find the root directory */
	uint8_t buf[ISO_SECTOR_SIZE];
	if (WDM_Read(drive, ISO_PVD_LBA, 1, buf, WDM_FLAG_NONE) != WDM_OK)
		return false;

	/* Verify identifier */
	if (!_iso9660_validate_pvd_signature(buf))
		return false;

	iso9660_sector_header_t header;
	header.type = buf[0];
	header.version = buf[6];
	memcpy(header.identifier, &buf[1], 5);
	header.identifier[5] = '\0';
	header.data = &buf[6];

	iso9660_primary_volume_descriptor_t pvd;
	iso9660_read_primary_volume_descriptor(&header, buf, &pvd);

	ctx->root_lba = pvd.dir_record.location_of_extent;
	ctx->root_size = pvd.dir_record.data_length;

	kfree(pvd.dir_record.file_id);
	return true;
}

bool is_iso9660(WDM_DriveHandle drive) {
	if (!drive) return false;
	uint8_t buf[ISO_SECTOR_SIZE];

	if (WDM_Read(drive, ISO_PVD_LBA, 1, buf, WDM_FLAG_NONE) != WDM_OK) return false;

	return _iso9660_validate_pvd_signature(buf);
}

void iso9660_destroy(iso9660_ctx_t* ctx) {
	/* Nothing heap-allocated inside the context itself for now. */
	(void) ctx;
}

iso9660_sector_header_t* iso9660_read_sector_header(iso9660_ctx_t* ctx, uint32_t lba, uint8_t* buffer) {
	if (!_iso9660_read_sectors(ctx, lba, 1, buffer)) return NULL;

	iso9660_sector_header_t* header = kalloc(sizeof(iso9660_sector_header_t));
	if (!header) return NULL;

	header->type = buffer[0];
	header->version = buffer[6];
	header->identifier[0] = buffer[1];
	header->identifier[1] = buffer[2];
	header->identifier[2] = buffer[3];
	header->identifier[3] = buffer[4];
	header->identifier[4] = buffer[5];
	header->identifier[5] = '\0';
	header->data = &buffer[6];

	// printf("Header for Sector (%u):\n\tType: 0x%x\n\tVersion: 0x%x\n\tIdentifier: %s\n", lba, header->type, header->version, header->identifier);
	return header;
}

void iso9660_read_directory_record(iso9660_directory_record_t* record, uint8_t* dir_record_ptr) {
	record->length = dir_record_ptr[0];
	record->extended_attribute_record_length = dir_record_ptr[1];
	record->location_of_extent = LE32(dir_record_ptr, 2);
	record->data_length = LE32(dir_record_ptr, 10);

	record->recording_time.years = dir_record_ptr[18];
	record->recording_time.month = dir_record_ptr[19];
	record->recording_time.day = dir_record_ptr[20];
	record->recording_time.hour = dir_record_ptr[21];
	record->recording_time.minute = dir_record_ptr[22];
	record->recording_time.second = dir_record_ptr[23];
	record->recording_time.tz_offset = dir_record_ptr[24];

	uint8_t file_flags = dir_record_ptr[25];
	record->file_flags.hidden = (file_flags & 0x01) != 0;
	record->file_flags.directory = (file_flags & 0x02) != 0;
	record->file_flags.associated = (file_flags & 0x04) != 0;
	record->file_flags.record = (file_flags & 0x08) != 0;
	record->file_flags.protection = (file_flags & 0x10) != 0;
	record->file_flags.reserved = (file_flags & 0x60) >> 5;
	record->file_flags.multi_extent = (file_flags & 0x80) != 0;

	record->file_unit_size = dir_record_ptr[26];
	record->interleave_gap_size = dir_record_ptr[27];
	record->volume_seq_num = LE16(dir_record_ptr, 28);
	record->file_id_len = dir_record_ptr[32];

	record->file_id = kalloc(record->file_id_len + 1);
	for (int i = 0; i < record->file_id_len; i++) record->file_id[i] = (char) dir_record_ptr[33 + i];
	record->file_id[record->file_id_len] = '\0';
}

void iso9660_read_primary_volume_descriptor(iso9660_sector_header_t* header, uint8_t* buf,
	iso9660_primary_volume_descriptor_t* pvd) {
	if (header->type != ISO9660_VOLUME_TYPE_PRIMARY) return;

	ISO_STR_READ(pvd->system_id, buf, 8, 32);
	ISO_STR_READ(pvd->volume_id, buf, 40, 32);

	pvd->volume_space_size = LE32(buf, 80);
	pvd->volume_set_size = LE16(buf, 120);
	pvd->volume_seq_num = LE16(buf, 124);
	pvd->logical_block_size = LE16(buf, 128);
	pvd->path_table_size = LE32(buf, 132);
	pvd->type_l_path_table = LE32(buf, 140);
	pvd->optional_type_l_path_table = LE32(buf, 144);
	pvd->type_m_path_table = BE32(buf, 148);
	pvd->optional_type_m_path_table = BE32(buf, 152);

	iso9660_read_directory_record(&pvd->dir_record, &buf[156]);

	ISO_STR_READ(pvd->volume_set_id, buf, 190, 128);
	ISO_STR_READ(pvd->publisher_id, buf, 318, 128);
	pvd->publisher_id_filename = pvd->publisher_id[0] == (char) 0x5F;

	ISO_STR_READ(pvd->data_preparer_id, buf, 446, 128);
	pvd->data_preparer_id_filename = pvd->data_preparer_id[0] == (char) 0x5F;

	ISO_STR_READ(pvd->application_id, buf, 574, 128);
	pvd->application_id_filename = pvd->application_id[0] == (char) 0x5F;

	ISO_STR_READ(pvd->copyright_file_id, buf, 702, 37);
	ISO_STR_READ(pvd->abstract_file_id, buf, 739, 37);
	ISO_STR_READ(pvd->bibliographic_id, buf, 776, 37);

	pvd->creation_time = _iso9660_parse_iso9660_datetime(buf, 813);
	pvd->creation_time_is_unspecified = _iso9660_iso_datetime_is_unspecified(buf, 813);

	pvd->modification_time = _iso9660_parse_iso9660_datetime(buf, 830);
	pvd->modification_time_is_unspecified = _iso9660_iso_datetime_is_unspecified(buf, 830);

	pvd->expiration_time = _iso9660_parse_iso9660_datetime(buf, 847);
	pvd->expiration_time_is_unspecified = _iso9660_iso_datetime_is_unspecified(buf, 847);

	pvd->effective_time = _iso9660_parse_iso9660_datetime(buf, 864);
	pvd->effective_time_is_unspecified = _iso9660_iso_datetime_is_unspecified(buf, 864);

	pvd->file_structure_ver = buf[881];
	pvd->offset_882_reserved = buf[882];
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Traversal
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

void iso9660_traverse(iso9660_ctx_t* ctx, uint32_t lba, uint32_t total_size, int depth) {
	uint32_t sectors = (total_size + ISO_SECTOR_SIZE - 1) / ISO_SECTOR_SIZE;
	uint8_t* buf = kalloc(sectors * ISO_SECTOR_SIZE);
	if (!buf) return;

	if (!_iso9660_read_sectors(ctx, lba, sectors, buf)) {
		kfree(buf);
		return;
	}

	uint32_t offset = 0;
	while (offset < total_size) {
		if (buf[offset] == 0) {
			/* Padding. Skip to next sector boundary */
			offset = (offset / ISO_SECTOR_SIZE + 1) * ISO_SECTOR_SIZE;
			if (offset >= total_size) break;
			continue;
		}

		iso9660_directory_record_t record;
		iso9660_read_directory_record(&record, &buf[offset]);

		bool is_special = (record.file_id_len == 1 && ((uint8_t) record.file_id[0] == 0x00 || (uint8_t) record.file_id[0] == 0x01));

		for (int i = 0; i < depth; i++) printf("  ");
		printf("|-- %s [%u bytes]\n", is_special ? ((uint8_t) record.file_id[0] == 0x00 ? "." : "..") : record.file_id, record.data_length);

		if (record.file_flags.directory && !is_special) iso9660_traverse(ctx, record.location_of_extent, record.data_length, depth + 1);

		offset += record.length;
		kfree(record.file_id);
	}

	kfree(buf);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Paths
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

bool iso9660_get_file(iso9660_ctx_t* ctx, const char* path, iso9660_directory_record_t* out_record) {
	if (!path || path[0] != '/') return false;

	/* Read the root directory record from the PVD at LBA 16 */
	uint8_t sector_buf[ISO_SECTOR_SIZE];
	if (!_iso9660_read_sectors(ctx, ISO_PVD_LBA, 1, sector_buf)) return false;
	iso9660_read_directory_record(out_record, &sector_buf[156]);

	if (path[1] == '\0') return true; /* path == "/" */

	const char* pos = path + 1;

	while (*pos != '\0') {
		const char* slash = strchr(pos, '/');
		size_t part_len = slash ? (size_t) (slash - pos) : strlen(pos);

		if (part_len == 0) { pos++; continue; } /* handle "//" */

		char part[256];
		if (part_len >= sizeof(part)) return false;
		memcpy(part, pos, part_len);
		part[part_len] = '\0';

		uint32_t cur_lba = out_record->location_of_extent;
		uint32_t dir_size = out_record->data_length;
		uint32_t sectors = (dir_size + ISO_SECTOR_SIZE - 1) / ISO_SECTOR_SIZE;

		uint8_t* dir_data = kalloc(sectors * ISO_SECTOR_SIZE);
		if (!dir_data) return false;

		if (!_iso9660_read_sectors(ctx, cur_lba, sectors, dir_data)) {
			kfree(dir_data);
			return false;
		}

		bool found = false;
		uint32_t off = 0;

		while (off < dir_size) {
			if (dir_data[off] == 0) {
				off = (off / ISO_SECTOR_SIZE + 1) * ISO_SECTOR_SIZE;
				continue;
			}

			iso9660_directory_record_t entry;
			iso9660_read_directory_record(&entry, &dir_data[off]);

			bool is_special = (entry.file_id_len == 1 && ((uint8_t) entry.file_id[0] == 0x00 || (uint8_t) entry.file_id[0] == 0x01));

			if (!is_special && _iso9660_names_match(entry.file_id, part)) {
				kfree(out_record->file_id);
				*out_record = entry;
				found = true;
				break;
			}

			off += entry.length;
			kfree(entry.file_id);
		}

		kfree(dir_data);
		if (!found) return false;

		pos = slash ? slash + 1 : pos + part_len;
	}

	return true;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Binary Blobs
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

uint8_t* iso9660_read_binary(iso9660_ctx_t* ctx, const char* path) {
	iso9660_directory_record_t record;
	if (!iso9660_get_file(ctx, path, &record)) return NULL;

	if (record.file_flags.directory) {
		kfree(record.file_id);
		return NULL;
	}

	uint32_t total = record.data_length;
	uint32_t sectors = (total + ISO_SECTOR_SIZE - 1) / ISO_SECTOR_SIZE;

	uint8_t* sector_buf = kalloc(sectors * ISO_SECTOR_SIZE);
	if (!sector_buf) { kfree(record.file_id); return NULL; }

	if (!_iso9660_read_sectors(ctx, record.location_of_extent, sectors, sector_buf)) {
		kfree(sector_buf);
		kfree(record.file_id);
		return NULL;
	}

	uint8_t* blob = kalloc(total + 1);
	if (!blob) { kfree(sector_buf); kfree(record.file_id); return NULL; }

	memcpy(blob, sector_buf, total);
	blob[total] = '\0';

	kfree(sector_buf);
	kfree(record.file_id);
	return blob;
}