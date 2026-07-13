#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <filesystem/wdm.h>
#include <filesystem/partitions/wallos_gpt.h>
#include <klibc/kernel_rng.h>
#include <memory/kernel_alloc.h>
#include <klibc/crc32.h>

/**
 * @internal
 * @brief Check whether two inclusive LBA ranges overlap.
 */
static inline bool gpt_ranges_overlap(uint64_t a_first, uint64_t a_last, uint64_t b_first, uint64_t b_last) {
	return a_first <= b_last && b_first <= a_last;
}

bool gpt_partition_entry_is_free(const gpt_partition_entry_t* entry) {
	if (!entry) return true; // If there is no entry, that means it's free

	static const uint8_t zero_guid[16] = { 0 };
	return memcmp(entry->partition_type_guid, zero_guid, sizeof(zero_guid)) == 0;
}

gpt_error_t gpt_find_free_entry_slot(const gpt_partition_table_t* gpt, uint32_t* out_index) {
	if (!gpt || !gpt->entries || !out_index) return GPT_BAD_PARAM;

	for (uint32_t i = 0; i < gpt->num_entries; i++) {
		if (gpt_partition_entry_is_free(&gpt->entries[i])) {
			*out_index = i;
			return GPT_NO_ERROR;
		}
	}

	return GPT_TABLE_FULL;
}

uint32_t gpt_count_used_entries(const gpt_partition_table_t* gpt) {
	if (!gpt || !gpt->entries) return 0;

	uint32_t count = 0;
	for (uint32_t i = 0; i < gpt->num_entries; i++) {
		if (!gpt_partition_entry_is_free(&gpt->entries[i])) count++;
	}
	return count;
}

gpt_error_t gpt_add_partition_entry(gpt_partition_table_t* gpt, gpt_partition_type_id_t type, uint64_t first_lba, uint64_t last_lba, uint64_t attributes, const char* name, gpt_partition_constructor_flags_t construct_flags, gpt_add_entry_flags_t add_flags, uint32_t requested_index, uint32_t* out_index) {
	if (!gpt || !gpt->entries) return GPT_BAD_PARAM;
	if (gpt->num_entries == 0) return GPT_BAD_PARAM;

	if (first_lba > last_lba) return GPT_ENTRY_LBA_INVERTED;

	if (!(add_flags & GPT_ADD_ENTRY_ALLOW_OUT_OF_RANGE)) {
		if (first_lba < gpt->header.first_usable_lba || last_lba > gpt->header.last_usable_lba) {
			return GPT_ENTRY_OUT_OF_RANGE;
		}
	}

	if (add_flags & GPT_ADD_ENTRY_REQUIRE_ALIGNMENT) {
		if (first_lba % GPT_DEFAULT_ALIGNMENT_LBA != 0) return GPT_ENTRY_ALIGNMENT;
	}

	// Slot selection
	uint32_t index;
	if (add_flags & GPT_ADD_ENTRY_AUTO_INDEX) {
		gpt_error_t find_err = gpt_find_free_entry_slot(gpt, &index);
		if (find_err != GPT_NO_ERROR) return find_err; // GPT_TABLE_FULL
	} else {
		if (requested_index >= gpt->num_entries) return GPT_BAD_PARAM;
		index = requested_index;

		if (!(add_flags & GPT_ADD_ENTRY_OVERWRITE) && !gpt_partition_entry_is_free(&gpt->entries[index])) {
			return GPT_ENTRY_SLOT_OCCUPIED;
		}
	}

	// Overlap check against every other occupied entry
	if (!(add_flags & GPT_ADD_ENTRY_ALLOW_OVERLAP)) {
		for (uint32_t i = 0; i < gpt->num_entries; i++) {
			if (i == index) continue;

			const gpt_partition_entry_t* other = &gpt->entries[i];
			if (gpt_partition_entry_is_free(other)) continue;

			if (gpt_ranges_overlap(first_lba, last_lba, other->first_lba, other->last_lba)) {
				return GPT_ENTRY_OVERLAP;
			}
		}
	}

	// All checks passed
	gpt_partition_entry_construct(&gpt->entries[index], type, first_lba, last_lba, attributes, name, construct_flags);

	if (out_index) *out_index = index;
	return GPT_NO_ERROR;
}

gpt_error_t gpt_remove_partition_entry(gpt_partition_table_t* gpt, uint32_t index) {
	if (!gpt || !gpt->entries) return GPT_BAD_PARAM;
	if (index >= gpt->num_entries) return GPT_BAD_PARAM;

	gpt_partition_entry_clear(&gpt->entries[index]);
	return GPT_NO_ERROR;
}

gpt_error_t gpt_find_free_space(const gpt_partition_table_t* gpt, uint64_t min_size_sectors, uint64_t alignment, uint64_t* out_first_lba, uint64_t* out_last_lba) {
	if (!gpt || !gpt->entries || !out_first_lba || !out_last_lba) return GPT_BAD_PARAM;
	if (alignment == 0) alignment = 1;

	uint64_t best_start = 0, best_end = 0, best_size = 0;
	uint64_t required = (min_size_sectors == 0) ? 1 : min_size_sectors;

	// Candidate start points
	// The beginning of the usable range, plus just past the end of every occupied entry.
	// This is O(n^2), but at 128 entries it doesn't really matter
	for (int64_t c = -1; c < (int64_t) gpt->num_entries; c++) {
		uint64_t candidate;

		if (c == -1) {
			candidate = gpt->header.first_usable_lba;
		} else {
			const gpt_partition_entry_t* e = &gpt->entries[c];
			if (gpt_partition_entry_is_free(e)) continue;
			candidate = e->last_lba + 1;
		}

		// Align up
		uint64_t rem = candidate % alignment;
		if (rem != 0) candidate += (alignment - rem);

		if (candidate < gpt->header.first_usable_lba || candidate > gpt->header.last_usable_lba) continue;

		// Alignment may have pushed the candidate into the middle of some other entry
		// Reject it if so
		bool inside_existing = false;
		for (uint32_t i = 0; i < gpt->num_entries; i++) {
			const gpt_partition_entry_t* e = &gpt->entries[i];
			if (gpt_partition_entry_is_free(e)) continue;
			if (candidate >= e->first_lba && candidate <= e->last_lba) {
				inside_existing = true;
				break;
			}
		}
		if (inside_existing) continue;

		// Shrink the candidate's end down to just before the nearest entry that starts after it
		uint64_t candidate_end = gpt->header.last_usable_lba;
		for (uint32_t i = 0; i < gpt->num_entries; i++) {
			const gpt_partition_entry_t* e = &gpt->entries[i];
			if (gpt_partition_entry_is_free(e)) continue;
			if (e->first_lba > candidate && (e->first_lba - 1) < candidate_end) {
				candidate_end = e->first_lba - 1;
			}
		}

		if (candidate_end < candidate) continue; // No room

		uint64_t size = candidate_end - candidate + 1;
		if (size >= required && size > best_size) {
			best_size = size;
			best_start = candidate;
			best_end = candidate_end;
		}
	}

	if (best_size == 0) return GPT_NO_FREE_SPACE;

	*out_first_lba = best_start;
	*out_last_lba = best_end;
	return GPT_NO_ERROR;
}


/**
 * @internal
 * @brief Fill a 16-byte buffer with random bytes via rng_next().
 * @param guid Destination buffer, must have at least 16 bytes available.
 */
void gpt_fill_random_guid(uint8_t guid[16]) {
	for (size_t i = 0; i < 16; i += 4) {
		uint32_t r = (uint32_t) rng_next();
		guid[i + 0] = (uint8_t) (r & 0xFF);
		guid[i + 1] = (uint8_t) ((r >> 8) & 0xFF);
		guid[i + 2] = (uint8_t) ((r >> 16) & 0xFF);
		guid[i + 3] = (uint8_t) ((r >> 24) & 0xFF);
	}
}

/**
 * @internal
 * @brief Convert an ASCII string into the entry's UTF-16LE partition_name field.
 * @param name Destination array of 36 uint16_t (72 bytes on-disk). Assumed already zeroed.
 * @param ascii_name Source ASCII string. May be NULL, in which case the name is left all-zero.
 */
void gpt_write_partition_name(uint16_t name[36], const char* ascii_name) {
	if (!ascii_name) return;

	// 36 UTF-16LE units total, leave room for NUL
	size_t i = 0;
	for (; i < 35 && ascii_name[i] != '\0'; i++) {
		name[i] = (uint16_t) (unsigned char) ascii_name[i];
	}
	name[i] = 0x0000;
}

void gpt_partition_entry_clear(gpt_partition_entry_t* entry) {
	if (!entry) return;
	memset(entry, 0, sizeof(*entry));
}

void gpt_partition_entry_construct(gpt_partition_entry_t* entry, gpt_partition_type_id_t type, uint64_t first_lba, uint64_t last_lba, uint64_t attributes, const char* name, gpt_partition_constructor_flags_t flags) {
	if (!entry) return;

	memset(entry, 0, sizeof(*entry));

	const uint8_t* type_guid = gpt_guid_from_partition_type(type);
	if (type_guid) {
		memcpy(entry->partition_type_guid, type_guid, sizeof(entry->partition_type_guid));
	}

	if (flags & GPT_PARTITION_CONSTRUCT_RANDOM_GUID) {
		gpt_fill_random_guid(entry->unique_partition_guid);
	}

	entry->first_lba = first_lba;
	entry->last_lba = last_lba;
	entry->attributes = attributes;

	gpt_write_partition_name(entry->partition_name, name);
}

void gpt_construct(gpt_partition_table_t* gpt, uint64_t disk_sectors, uint32_t num_entries, gpt_constructor_flags_t flags) {
	if (!gpt) return;

	memset(gpt, 0, sizeof(*gpt));

	// Protective MBR
	mbr_construct(&gpt->protective_mbr, MBR_CONSTRUCT_GPT);

	// Fix the sector count that mbr_construct leaves blank
	uint32_t protective_sector_count = (disk_sectors - 1 > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (uint32_t) (disk_sectors - 1);
	gpt->protective_mbr.first_entry.sector_count = protective_sector_count;

	// The protective entry always lands in the MBR's first slot from mbr_construct().
	gpt->gpt_partition_entry = 0;

	// Partition entry array sizing
	uint32_t entry_size = GPT_ON_DISK_ENTRY_SIZE;
	uint64_t array_bytes = (uint64_t) num_entries * entry_size;
	uint64_t array_sectors = (array_bytes + (GPT_SECTOR_SIZE - 1)) / GPT_SECTOR_SIZE;
	if (array_sectors == 0) array_sectors = 1; // Usually a zero-entry table reserves at least one sector

	// Header
	gpt_partition_header_t* header = &gpt->header;

	memcpy(header->signature, "EFI PART", 8);
	header->signature[8] = '\0';

	header->gpt_revision = 0x00010000;
	header->header_size = GPT_ON_DISK_HEADER_SIZE;
	header->crc32 = 0; // Caller must compute after the header is fully finalized
	header->reserved = 0;

	if (flags & GPT_CONSTRUCT_BACKUP_HEADER) {
		header->header_lba = disk_sectors - 1;
		header->alt_header_lba = 1;
		header->start_partition_entries_lba = disk_sectors - 1 - array_sectors;
	} else {
		header->header_lba = 1;
		header->alt_header_lba = disk_sectors - 1;
		header->start_partition_entries_lba = 2;
	}

	// Usable range excludes: PMBR (LBA 0) + primary header (LBA 1) + primary array on one side, and the backup array + backup header on the other, regardless of which header this is.
	uint64_t primary_reserved = 1 + array_sectors; // the one is for the headers
	uint64_t backup_reserved = array_sectors + 1;

	header->first_usable_lba = 1 + primary_reserved;
	header->last_usable_lba = (disk_sectors - 1) - backup_reserved;

	gpt_fill_random_guid(header->disk_guid);

	header->num_partition_entries = num_entries;
	header->partition_entry_size = entry_size;
	header->partition_array_crc32 = 0; // Caller must compute once every entry is populated

	// Partition entries
	gpt->num_entries = num_entries;

	if (flags & GPT_CONSTRUCT_ALLOCATE_ENTRIES) {
		gpt->entries = (gpt_partition_entry_t*) kcalloc(num_entries, sizeof(gpt_partition_entry_t));
	} else {
		gpt->entries = NULL;
	}
}

gpt_error_t gpt_create_backup(const gpt_partition_table_t* primary, gpt_partition_table_t* backup, gpt_backup_flags_t flags) {
	if (!primary || !backup) return GPT_BAD_PARAM;

	memset(backup, 0, sizeof(*backup));

	// Fields that must stay identical between primary and backup
	backup->protective_mbr = primary->protective_mbr;
	backup->gpt_partition_entry = primary->gpt_partition_entry;

	const gpt_partition_header_t* ph = &primary->header;
	gpt_partition_header_t* bh = &backup->header;

	memcpy(bh->signature, ph->signature, sizeof(bh->signature));
	bh->gpt_revision = ph->gpt_revision;
	bh->header_size = ph->header_size;
	bh->reserved = ph->reserved;
	memcpy(bh->disk_guid, ph->disk_guid, sizeof(bh->disk_guid));
	bh->first_usable_lba = ph->first_usable_lba;
	bh->last_usable_lba = ph->last_usable_lba;
	bh->num_partition_entries = ph->num_partition_entries;
	bh->partition_entry_size = ph->partition_entry_size;

	// Fields that differ

	// The backup's own LBA is the primary's alt_header_lba
	bh->header_lba = ph->alt_header_lba;
	bh->alt_header_lba = ph->header_lba;

	// The backup's partition entry array sits immediately before the backup header itself
	uint32_t entry_size = ph->partition_entry_size ? ph->partition_entry_size : GPT_ON_DISK_ENTRY_SIZE;
	uint64_t array_bytes = (uint64_t) ph->num_partition_entries * entry_size;
	uint64_t array_sectors = (array_bytes + (GPT_SECTOR_SIZE - 1)) / GPT_SECTOR_SIZE;
	if (array_sectors == 0) array_sectors = 1; // We reserve at least one sector

	bh->start_partition_entries_lba = bh->header_lba - array_sectors;

	// Both CRCs depend on this being "finalized".
	// We can't assume this table won't change, so we set them to zero
	bh->crc32 = 0;
	bh->partition_array_crc32 = 0;

	// Partition entries
	backup->num_entries = primary->num_entries;

	if ((flags & GPT_BACKUP_ALLOCATE_ENTRIES) && primary->entries) {
		backup->entries = (gpt_partition_entry_t*) kcalloc(primary->num_entries, sizeof(gpt_partition_entry_t));
		if (!backup->entries) return GPT_OUT_OF_MEMORY;

		memcpy(backup->entries, primary->entries, (size_t) primary->num_entries * sizeof(gpt_partition_entry_t));
	} else {
		backup->entries = NULL; // Caller must assign/copy this themselves
	}

	return GPT_NO_ERROR;
}

gpt_error_t gpt_finalize(gpt_partition_table_t* gpt, gpt_finalize_flags_t flags) {
	if (!gpt) return GPT_BAD_PARAM;

	gpt_partition_header_t* header = &gpt->header;

	if (header->header_size > GPT_SECTOR_SIZE) return GPT_BAD_HEADER;

	if (flags & GPT_FINALIZE_VALIDATE) {
		uint32_t result = validate_gpt(gpt);

		static const uint32_t fatal_mask = GPT_VALIDATION_USABLE_RANGE_INVERTED
			| GPT_VALIDATION_ENTRY_LBA_INVERTED
			| GPT_VALIDATION_ENTRY_OUT_OF_RANGE
			| GPT_VALIDATION_ENTRY_OVERLAP
			| GPT_VALIDATION_DUPLICATE_GUID;

		if (result & fatal_mask) return GPT_BAD_PARTITION_TABLE;
	}

	uint8_t* array_buf = NULL;
	uint64_t total_entries_size = 0;

	gpt_error_t serialize_err = gpt_serialize_partition_array(gpt, &array_buf, &total_entries_size);
	if (serialize_err != GPT_NO_ERROR) return serialize_err;

	header->partition_array_crc32 = (total_entries_size > 0) ? crc32(array_buf, (size_t) total_entries_size) : 0;
	kfree(array_buf);

	uint8_t header_buf[GPT_SECTOR_SIZE];

	header->crc32 = 0;
	write_gpt_header(header, header_buf, sizeof(header_buf));
	header->crc32 = crc32(header_buf, header->header_size);

	return GPT_NO_ERROR;
}