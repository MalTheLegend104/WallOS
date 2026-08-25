#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <filesystem/fat/fat.h>
#include <filesystem/fat/fat_internal.h>
#include <memory/kernel_alloc.h>


uint16_t fat_internal_read16(const uint8_t* buf, const size_t offset) {
	return (uint16_t) buf[offset] | ((uint16_t) buf[offset + 1] << 8);
}

uint32_t fat_internal_read32(const uint8_t* buf, const size_t offset) {
	return (uint32_t) buf[offset] |
		((uint32_t) buf[offset + 1] << 8) |
		((uint32_t) buf[offset + 2] << 16) |
		((uint32_t) buf[offset + 3] << 24);
}

void fat_internal_write16(uint8_t* buf, const size_t offset, const uint16_t val) {
	buf[offset] = (uint8_t) (val & 0xFF);
	buf[offset + 1] = (uint8_t) ((val >> 8) & 0xFF);
}

void fat_internal_write32(uint8_t* buf, const size_t offset, const uint32_t val) {
	buf[offset] = (uint8_t) (val & 0xFF);
	buf[offset + 1] = (uint8_t) ((val >> 8) & 0xFF);
	buf[offset + 2] = (uint8_t) ((val >> 16) & 0xFF);
	buf[offset + 3] = (uint8_t) ((val >> 24) & 0xFF);
}

void fill_bpb(const uint8_t* buf, fat_bpb_t* bpb) {
	// Offset 3: OEM ID (8 bytes)
	memcpy(&bpb->oem_id, &buf[3], 8);
	bpb->oem_id[8] = '\0';

	bpb->bytes_per_sector = fat_internal_read16(buf, 11);
	bpb->sectors_per_cluster = buf[13];
	bpb->reserved_sectors = fat_internal_read16(buf, 14);
	bpb->fat_allcation_tables = buf[16];
	bpb->root_dir_entries = fat_internal_read16(buf, 17);
	bpb->sectors = fat_internal_read16(buf, 19);
	bpb->media_descriptor_type = buf[21];
	bpb->sectors_per_fat = fat_internal_read16(buf, 22);
	bpb->sectors_per_track = fat_internal_read16(buf, 24);
	bpb->heads = fat_internal_read16(buf, 26);
	bpb->hidden_sectors = fat_internal_read32(buf, 28);
	bpb->large_sector_count = fat_internal_read32(buf, 32);
}

void print_bpb(const fat_bpb_t* bpb) {
	printf("\n=== BIOS Parameter Block ===\n");
	printf("OEM ID               : %s\n", bpb->oem_id);
	printf("Bytes/Sector         : %u\n", bpb->bytes_per_sector);
	printf("Sectors/Cluster      : %u\n", bpb->sectors_per_cluster);
	printf("Reserved Sectors     : %u\n", bpb->reserved_sectors);
	printf("FAT Count            : %u\n", bpb->fat_allcation_tables);
	printf("Root Dir Entries     : %u\n", bpb->root_dir_entries);
	printf("Small Sector Count   : %u\n", bpb->sectors);
	printf("Media Descriptor     : 0x%02X\n", bpb->media_descriptor_type);
	printf("Sectors/FAT (16)     : %u\n", bpb->sectors_per_fat);
	printf("Sectors/Track        : %u\n", bpb->sectors_per_track);
	printf("Heads                : %u\n", bpb->heads);
	printf("Hidden Sectors       : %u\n", bpb->hidden_sectors);
	printf("Large Sector Count   : %u\n", bpb->large_sector_count);
}

fat_type_t get_fat_type(const uint8_t* sector_buf) {
	if (!sector_buf) return FAT_TYPE_UNKNOWN;
	// Verify the boot sector signature (0xAA55 at offset 510)
	if (fat_internal_read16(sector_buf, 510) != 0xAA55) return FAT_TYPE_UNKNOWN;

	// Check for exFAT (OEM Name field at offset 3 contains "EXFAT   ")
	if (memcmp(&sector_buf[3], "EXFAT   ", 8) == 0) return FAT_TYPE_EXFAT;

	// Extract necessary FAT BPB fields
	uint16_t bytes_per_sector = fat_internal_read16(sector_buf, 11);
	uint8_t  sectors_per_cluster = sector_buf[13];
	uint16_t reserved_sectors = fat_internal_read16(sector_buf, 14);
	uint8_t  num_fats = sector_buf[16];
	uint16_t root_dir_entries = fat_internal_read16(sector_buf, 17);

	uint16_t sectors_per_fat16 = fat_internal_read16(sector_buf, 22);
	uint16_t total_sectors16 = fat_internal_read16(sector_buf, 19);

	// Reject anything that cant be a real BPB before doing anything that depends on these fields
	if (bytes_per_sector == 0 || (bytes_per_sector & (bytes_per_sector - 1)) != 0) return FAT_TYPE_UNKNOWN;
	if (sectors_per_cluster == 0) return FAT_TYPE_UNKNOWN;

	// Check for structural proof of FAT32
	// If 16-bit FAT size or root dir entries are zero, it physically cannot be FAT12/16.
	uint32_t sectors_per_fat = 0;
	uint32_t total_sectors = 0;
	int force_fat32 = 0;

	if (sectors_per_fat16 == 0 || root_dir_entries == 0) {
		sectors_per_fat = fat_internal_read32(sector_buf, 36); // FAT32 field
		total_sectors = fat_internal_read32(sector_buf, 32);   // FAT32 field
		force_fat32 = 1;

		// FAT32 with a zero FAT size is structurally invalid
		if (sectors_per_fat == 0) {
			return FAT_TYPE_UNKNOWN;
		}
	} else {
		sectors_per_fat = sectors_per_fat16;
		total_sectors = (total_sectors16 != 0) ? total_sectors16 : fat_internal_read32(sector_buf, 32);
	}

	// fallback: calculate data clusters to differentiate FAT12 vs FAT16
	uint32_t fat_sectors = num_fats * sectors_per_fat;
	uint32_t root_dir_sectors = ((root_dir_entries * 32) + (bytes_per_sector - 1)) / bytes_per_sector;
	uint32_t non_data_sectors = reserved_sectors + fat_sectors + root_dir_sectors;

	if (total_sectors < non_data_sectors) {
		return FAT_TYPE_UNKNOWN; // Corrupted layout metadata
	}

	uint32_t data_sectors = total_sectors - non_data_sectors;
	uint32_t total_clusters = data_sectors / sectors_per_cluster;

	if (force_fat32 || total_clusters >= 65525) return FAT_TYPE_FAT32;
	if (total_clusters >= 4085) return FAT_TYPE_FAT16;
	return FAT_TYPE_FAT12;
}

const char* print_fat_type(const fat_type_t type) {
	switch (type) {
		case FAT_TYPE_FAT12: return "FAT12";
		case FAT_TYPE_FAT16: return "FAT16";
		case FAT_TYPE_FAT32: return "FAT32";
		case FAT_TYPE_EXFAT: return "EXFAT";
		default: return "UNKNOWN";
	}
}

void fat_format_short_name(const uint8_t raw11[11], char out[13]) {
	/* Short name format: "HELLO   TXT" (11 raw bytes, space padded)
	 * It has no dot and no null terminator.
	 * Trailing spaces in each 8-byte/3-byte half must be trimmed.
	 */

	char base[9];
	char ext[4];
	int base_len = 0;
	int ext_len = 0;

	for (int i = 0; i < 8; i++) {
		if (raw11[i] == ' ') break;
		base[base_len++] = (char) raw11[i];
	}
	base[base_len] = '\0';

	for (int i = 0; i < 3; i++) {
		if (raw11[8 + i] == ' ') break;
		ext[ext_len++] = (char) raw11[8 + i];
	}
	ext[ext_len] = '\0';

	if (ext_len > 0) {
		snprintf(out, 13, "%s.%s", base, ext);
	} else {
		snprintf(out, 13, "%s", base);
	}
}

uint8_t fat_short_name_checksum(const uint8_t raw11[11]) {
	// This is the standard VFAT short name checksum algorithm from the Microsoft FAT spec
	uint8_t sum = 0;
	for (int i = 0; i < 11; i++) {
		// Rotate right by 1 bit and add current byte
		sum = (uint8_t) (((sum & 1) ? 0x80 : 0) + (sum >> 1) + raw11[i]);
	}
	return sum;
}

void fat_lfn_run_reset(fat_lfn_run_t* run) {
	memset(run->present, 0, sizeof(run->present));
	run->highest_seq = 0;
	run->checksum = 0;
}

// Decode one raw 32-byte buffer as an LFN entry and fold it into run.
void fat_lfn_run_accumulate(fat_lfn_run_t* run, const uint8_t* raw32) {
	uint8_t order = raw32[0];
	uint8_t checksum = raw32[13];
	int seq = order & FAT_LFN_SEQ_MASK;

	if (seq < 1 || seq > FAT_LFN_MAX_ENTRIES) {
		// Sequence number is garbage
		fat_lfn_run_reset(run);
		return;
	}

	if (order & FAT_LFN_LAST_ENTRY_FLAG) {
		fat_lfn_run_reset(run);
		run->highest_seq = seq;
		run->checksum = checksum;
	} else if (run->highest_seq == 0 || checksum != run->checksum) {
		// An LFN entry showed up with no valid "last entry" or belongs to a different checksum
		fat_lfn_run_reset(run);
		return;
	}

	int idx = seq - 1;
	fat_lfn_entry_t e;
	e.name1[0] = fat_internal_read16(raw32, 1);
	e.name1[1] = fat_internal_read16(raw32, 3);
	e.name1[2] = fat_internal_read16(raw32, 5);
	e.name1[3] = fat_internal_read16(raw32, 7);
	e.name1[4] = fat_internal_read16(raw32, 9);
	e.name2[0] = fat_internal_read16(raw32, 14);
	e.name2[1] = fat_internal_read16(raw32, 16);
	e.name2[2] = fat_internal_read16(raw32, 18);
	e.name2[3] = fat_internal_read16(raw32, 20);
	e.name2[4] = fat_internal_read16(raw32, 22);
	e.name2[5] = fat_internal_read16(raw32, 24);
	e.name3[0] = fat_internal_read16(raw32, 28);
	e.name3[1] = fat_internal_read16(raw32, 30);

	uint16_t* dst = run->chars[idx];
	dst[0] = e.name1[0]; dst[1] = e.name1[1]; dst[2] = e.name1[2];
	dst[3] = e.name1[3]; dst[4] = e.name1[4];
	dst[5] = e.name2[0]; dst[6] = e.name2[1]; dst[7] = e.name2[2];
	dst[8] = e.name2[3]; dst[9] = e.name2[4]; dst[10] = e.name2[5];
	dst[11] = e.name3[0]; dst[12] = e.name3[1];
	run->present[idx] = true;
}

void fat_lfn_run_resolve(const fat_lfn_run_t* run, const uint8_t short_checksum, char* out) {
	out[0] = '\0';

	if (run->highest_seq == 0) return; // no run at all
	if (run->checksum != short_checksum) return; // stale/mismatched run

	for (int i = 0; i < run->highest_seq; i++) {
		if (!run->present[i]) return; // truncated/corrupt run
	}

	int out_pos = 0;
	for (int i = 0; i < run->highest_seq && out_pos < FAT_LFN_MAX_NAME_CHARS; i++) {
		for (int j = 0; j < FAT_LFN_MAX_CHARS_PER_ENTRY && out_pos < FAT_LFN_MAX_NAME_CHARS; j++) {
			uint16_t ch = run->chars[i][j];

			if (ch == 0x0000) {
				// Null terminator within the name data implies end of name.
				// Remaining slots in this and later entries are padding and must be ignored.
				out[out_pos] = '\0';
				return;
			}

			out[out_pos++] = (ch < 0x80) ? (char) ch : '?';
		}
	}
	out[out_pos] = '\0';
}

void fat_dirent_list_init(fat_dirent_list_t* list) {
	list->entries = NULL;
	list->count = 0;
	list->capacity = 0;
}

void fat_dirent_list_free(fat_dirent_list_t* list) {
	kfree(list->entries);
	list->entries = NULL;
	list->count = 0;
	list->capacity = 0;
}

bool fat_dirent_list_push(fat_dirent_list_t* list, const fat_resolved_dirent_t* item) {
	if (list->count == list->capacity) {
		size_t new_cap = (list->capacity == 0) ? 16 : (list->capacity * 2);

		/* Allocate memory for the new larger array */
		fat_resolved_dirent_t* grown = (fat_resolved_dirent_t*) kalloc(new_cap * sizeof(fat_resolved_dirent_t));
		if (!grown) return false;

		/* Copy existing entries to the new buffer and free the old one */
		if (list->entries) {
			memcpy(grown, list->entries, list->count * sizeof(fat_resolved_dirent_t));
			kfree(list->entries);
		}

		list->entries = grown;
		list->capacity = new_cap;
	}
	list->entries[list->count++] = *item;
	return true;
}

/* Parses one raw 32-byte short dirent record into fat_dirent_t. */
void fat_fill_dirent_raw(const uint8_t* raw32, fat_dirent_t* d) {
	memcpy(d->name, raw32, 11);
	d->name[11] = '\0';

	d->attributes = raw32[11];
	d->nt_reserved = raw32[12];
	d->creation_time_tenths = raw32[13];
	d->creation_time = fat_internal_read16(raw32, 14);
	d->creation_date = fat_internal_read16(raw32, 16);
	d->last_access_date = fat_internal_read16(raw32, 18);
	d->first_cluster_high = fat_internal_read16(raw32, 20);
	d->write_time = fat_internal_read16(raw32, 22);
	d->write_date = fat_internal_read16(raw32, 24);
	d->first_cluster_low = fat_internal_read16(raw32, 26);
	d->file_size = fat_internal_read32(raw32, 28);
}

bool fat_name_matches(const fat_resolved_dirent_t* e, const char* name) {
	if (e->long_name[0] != '\0') {
		if (strcasecmp(e->long_name, name) == 0) return true;
	}
	return strcasecmp(e->short_name, name) == 0;
}

const fat_resolved_dirent_t* fat_dirent_list_find(const fat_dirent_list_t* list, const char* name) {
	if (!list || !name) return NULL;

	for (size_t i = 0; i < list->count; i++) {
		if (fat_name_matches(&list->entries[i], name)) {
			return &list->entries[i];
		}
	}
	return NULL;
}

bool fat_is_dot_entry(const fat_resolved_dirent_t* e) {
	return strcmp(e->short_name, ".") == 0 || strcmp(e->short_name, "..") == 0;
}

/* Shared per-sector dirent/LFN parser. */
bool fat_parse_dirent_sector(const uint8_t* sector, uint16_t bytes_per_sector, fat_lfn_run_t* lfn_run, fat_dirent_list_t* out_list, bool* end_of_directory) {
	for (uint32_t off = 0; off + 32 <= bytes_per_sector; off += 32) {
		const uint8_t* raw32 = sector + off;
		uint8_t first_byte = raw32[0];

		if (first_byte == FAT_DIRENT_END_MARKER) {
			*end_of_directory = true;
			return true;
		}

		if (first_byte == FAT_DIRENT_FREE_MARKER) {
			fat_lfn_run_reset(lfn_run);
			continue;
		}

		uint8_t attributes = raw32[11];

		if (attributes == FAT_ATTR_LFN) {
			fat_lfn_run_accumulate(lfn_run, raw32);
			continue;
		}

		if (attributes & FAT_ATTR_VOLUME_ID) {
			fat_lfn_run_reset(lfn_run);
			continue;
		}

		fat_resolved_dirent_t resolved;
		memset(&resolved, 0, sizeof(resolved));

		uint8_t raw11[11];
		memcpy(raw11, raw32, 11);
		if (raw11[0] == FAT_DIRENT_ESCAPED_E5) raw11[0] = 0xE5;

		fat_fill_dirent_raw(raw32, &resolved.raw);
		memcpy(resolved.raw.name, raw11, 11);
		resolved.raw.name[11] = '\0';

		fat_format_short_name(raw11, resolved.short_name);

		uint8_t expected_checksum = fat_short_name_checksum(raw11);
		fat_lfn_run_resolve(lfn_run, expected_checksum, resolved.long_name);
		fat_lfn_run_reset(lfn_run);

		resolved.first_cluster = ((uint32_t) resolved.raw.first_cluster_high << 16) | (uint32_t) resolved.raw.first_cluster_low;

		if (!fat_dirent_list_push(out_list, &resolved)) {
			return false;
		}
	}
	return true;
}

bool fat_pack_short_name_raw(const char* name, uint8_t raw11[11]) {
	if (!name || name[0] == '\0') return false;

	if (strchr(name, '/') || strchr(name, '\\')) return false;
	if (name[0] == ' ' || name[0] == '.') return false;


	static const char* invalid_chars = "\"*+,/:;<=>?[\\]|";

	const char* dot = strrchr(name, '.');
	/* A leading dot was already rejected above, so any dot found here is a real extension separator. */

	size_t base_len = dot ? (size_t) (dot - name) : strlen(name);
	const char* ext = dot ? dot + 1 : "";
	size_t ext_len = strlen(ext);

	if (base_len == 0 || base_len > 8 || ext_len > 3) return false;
	if (dot && ext_len == 0) return false; // trailing dot, no extension

	memset(raw11, ' ', 11);

	for (size_t i = 0; i < base_len; i++) {
		char c = name[i];
		if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
		if ((unsigned char) c < 0x20 || strchr(invalid_chars, c)) return false;
		raw11[i] = (uint8_t) c;
	}

	for (size_t i = 0; i < ext_len; i++) {
		char c = ext[i];
		if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
		if ((unsigned char) c < 0x20 || strchr(invalid_chars, c)) return false;
		raw11[8 + i] = (uint8_t) c;
	}

	// 0xE5 is supposed to be the "deleted entry" marker.
	// The spec's workaround is to store the escape byte 0x05 instead
	if (raw11[0] == 0xE5) raw11[0] = FAT_DIRENT_ESCAPED_E5;

	return true;
}

#include <system/timer.h>

void fat_pack_dirent_raw(const uint8_t raw11[11], uint8_t attributes, uint32_t first_cluster, uint32_t file_size, uint8_t raw32[32]) {
	memset(raw32, 0, 32);
	memcpy(raw32, raw11, 11);

	raw32[11] = attributes;
	raw32[12] = 0;  // nt_reserved
	raw32[13] = 0;  // creation_time_tenths

	wall_time_t time;
	wallclock_read(&time);

	// Default to FAT epoch if the wall clock is invalid/unavailable
	uint16_t fat_time = FAT_EPOCH_TIME;
	uint16_t fat_date = FAT_EPOCH_DATE;

	// We validate that the returned time makes sense
	if (time.year != 0 && time.month >= 1 && time.month <= 12 && time.day >= 1 && time.day <= 31 && time.hour <= 23 && time.minute <= 59 && time.second <= 59) {
		// FAT stores seconds in 2-second increments.
		fat_time = ((uint16_t) time.hour << 11) | ((uint16_t) time.minute << 5) | ((uint16_t) time.second / 2);

		// FAT year is stored relative to 1980.
		if (time.year >= 1980 && time.year <= 2107) {
			fat_date = ((uint16_t) (time.year - 1980) << 9) | ((uint16_t) time.month << 5) | (uint16_t) time.day;
		} else {
			fat_time = FAT_EPOCH_TIME;
			fat_date = FAT_EPOCH_DATE;
		}
	}

	fat_internal_write16(raw32, 14, fat_time);  // creation_time
	fat_internal_write16(raw32, 16, fat_date);  // creation_date
	fat_internal_write16(raw32, 18, fat_date);  // last_access_date

	fat_internal_write16(raw32, 20, (uint16_t) (first_cluster >> 16));  // first_cluster_high

	fat_internal_write16(raw32, 22, fat_time);  // write_time
	fat_internal_write16(raw32, 24, fat_date);  // write_date

	fat_internal_write16(raw32, 26, (uint16_t) (first_cluster & 0xFFFF)); // first_cluster_low

	fat_internal_write32(raw32, 28, file_size);
}

void fat_dir_writer_init(fat_dir_writer_t* w, const fat_dir_writer_ops_t* ops, void* ctx, uint16_t bytes_per_sector) {
	w->ops = ops;
	w->ctx = ctx;
	w->bytes_per_sector = bytes_per_sector;
}

bool fat_dir_locate_or_reserve_slot(fat_dir_writer_t* w, const uint8_t target_raw11[11], fat_dirent_slot_t* out_slot, bool* out_existed, uint8_t existing_raw32[32]) {
	if (!w || !w->ops || !target_raw11 || !out_slot || !out_existed) return false;

	uint16_t bps = w->bytes_per_sector;
	if (bps == 0 || bps % 32 != 0) return false;

	uint8_t* sector = kalloc(bps);
	if (!sector) return false;

	bool have_free_slot = false;
	fat_dirent_slot_t free_slot = { 0, 0 };
	bool free_slot_is_terminator = false;
	bool hit_terminator = false;

	uint32_t total_sectors = w->ops->sector_count(w);

	for (uint32_t sector_index = 0; sector_index < total_sectors && !hit_terminator; sector_index++) {
		if (!w->ops->read_sector(w, sector_index, sector)) {
			kfree(sector);
			return false;
		}

		for (uint32_t off = 0; off + 32 <= bps; off += 32) {
			uint8_t* raw32 = sector + off;
			uint8_t first_byte = raw32[0];

			if (first_byte == FAT_DIRENT_END_MARKER) {
				if (!have_free_slot) {
					free_slot.sector_index = sector_index;
					free_slot.offset = off;
					free_slot_is_terminator = true;
					have_free_slot = true;
				}
				hit_terminator = true;
				break;
			}

			if (first_byte == FAT_DIRENT_FREE_MARKER) {
				if (!have_free_slot) {
					free_slot.sector_index = sector_index;
					free_slot.offset = off;
					free_slot_is_terminator = false;
					have_free_slot = true;
				}
				continue;
			}

			uint8_t attributes = raw32[11];
			if (attributes == FAT_ATTR_LFN) continue; // not a short entry

			uint8_t raw11[11];
			memcpy(raw11, raw32, 11);
			if (raw11[0] == FAT_DIRENT_ESCAPED_E5) raw11[0] = 0xE5;

			if (memcmp(raw11, target_raw11, 11) == 0) {
				out_slot->sector_index = sector_index;
				out_slot->offset = off;
				*out_existed = true;
				if (existing_raw32) memcpy(existing_raw32, raw32, 32);
				kfree(sector);
				return true;
			}
		}
	}

	kfree(sector);
	*out_existed = false;

	if (!have_free_slot) {
		// Scanned every currently allocated sector and every slot held a live entry
		// Grow the directory by one cluster and use its first slot
		// This does not apply to fixed-size root (fat12/16)
		if (!w->ops->extend(w)) return false;
		free_slot.sector_index = total_sectors;
		free_slot.offset = 0;
		free_slot_is_terminator = true;
	}

	*out_slot = free_slot;

	if (free_slot_is_terminator) {
		// We overwrite the directory terminator with a real entry
		// We need to make the slot after it the new terminator
		uint32_t next_sector = free_slot.sector_index;
		uint32_t next_offset = free_slot.offset + 32;
		if (next_offset >= bps) {
			next_sector++;
			next_offset = 0;
		}

		uint32_t current_total = w->ops->sector_count(w);
		if (next_sector < current_total) {
			uint8_t* nbuf = kalloc(bps);
			if (!nbuf) return false;
			if (!w->ops->read_sector(w, next_sector, nbuf)) { kfree(nbuf); return false; }
			if (nbuf[next_offset] != FAT_DIRENT_END_MARKER) {
				memset(nbuf + next_offset, 0, 32);
				if (!w->ops->write_sector(w, next_sector, nbuf)) { kfree(nbuf); return false; }
			}
			kfree(nbuf);
		}
	}

	return true;
}

bool fat_dir_write_slot(fat_dir_writer_t* w, fat_dirent_slot_t slot, const uint8_t raw32[32]) {
	if (!w || !w->ops) return false;

	uint8_t* sector = kalloc(w->bytes_per_sector);
	if (!sector) return false;

	if (!w->ops->read_sector(w, slot.sector_index, sector)) {
		kfree(sector);
		return false;
	}

	memcpy(sector + slot.offset, raw32, 32);

	bool ok = w->ops->write_sector(w, slot.sector_index, sector);
	kfree(sector);
	return ok;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// LFN write support
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/*
 * Returns the number of LFN directory entries needed to store long_name, or 0 if the name is empty or too long.
 */
static int lfn_entry_count(const char* long_name) {
	size_t len = strlen(long_name);
	if (len == 0 || len > FAT_LFN_MAX_NAME_CHARS) return 0;
	return (int) ((len + FAT_LFN_MAX_CHARS_PER_ENTRY - 1) / FAT_LFN_MAX_CHARS_PER_ENTRY);
}

int fat_pack_lfn_entries(const char* long_name, const uint8_t raw11[11], uint8_t* out_entries) {
	if (!long_name || !raw11 || !out_entries) return 0;

	int n = lfn_entry_count(long_name);
	if (n == 0) return 0;

	uint8_t checksum = fat_short_name_checksum(raw11);
	size_t  name_len = strlen(long_name);

	for (int seq = n; seq >= 1; seq--) {
		// Characters covered by this LFN slot (0-based into the name).
		int char_start = (seq - 1) * FAT_LFN_MAX_CHARS_PER_ENTRY;

		// out_entries is in disk order
		// slot n goes first, slot 1 last
		uint8_t* slot = out_entries + (n - seq) * 32;
		memset(slot, 0xFF, 32); // unused char positions are 0xFFFF per spec

		slot[0] = (uint8_t) seq | (seq == n ? FAT_LFN_LAST_ENTRY_FLAG : 0);
		slot[11] = FAT_ATTR_LFN;
		slot[12] = 0x00;
		slot[13] = checksum;
		fat_internal_write16(slot, 26, 0x0000); // Cluster is always 0 for LFN

		/* Offsets of the three name fields within the LFN entry. */
		static const int field_offsets[] = { 1, 3, 5, 7, 9,          /* name1: 5 chars */
											 14, 16, 18, 20, 22, 24, /* name2: 6 chars */
											 28, 30 };               /* name3: 2 chars */

		for (int i = 0; i < FAT_LFN_MAX_CHARS_PER_ENTRY; i++) {
			int name_idx = char_start + i;
			uint16_t ch;

			if ((size_t) name_idx < name_len) {
				ch = (uint16_t) (unsigned char) long_name[name_idx];
			} else if ((size_t) name_idx == name_len) {
				ch = 0x0000; // null terminator, my IDE didn't like this being '\0' for some reason
			} else {
				ch = 0xFFFF; // padding
			}

			fat_internal_write16(slot, (size_t) field_offsets[i], ch);
		}
	}

	return n;
}

/**
 * Generates a valid FAT 8.3 short name for long_name that does not collide with any entry already in existing_listing
 * Uses the standard ~N numeric tail algorithm.
 * @param long_name The full long name
 * @param existing existing dirent list
 * @param raw11 receives the packed 11-byte result
 * @return true on success, false if no free tail slot was found (N > 999999)
 */
bool fat_generate_short_name(const char* long_name, const fat_dirent_list_t* existing, uint8_t raw11[11]) {
	if (!long_name || !raw11) return false;

	/* Build a sanitized uppercase base:
	 *  - strip leading dots/spaces,
	 *  - replace illegal chars with '_'
	 *  - keep only the last '.' as the
	 *  - extension separator
	 */
	static const char* illegal = "\"*+,/:;<=>?[\\]| ";

	// Find the last dot to split base / ext.
	const char* last_dot = strrchr(long_name, '.');
	// A leading dot is not an extension separator.
	if (last_dot == long_name) last_dot = NULL;

	char base_raw[9] = { 0 };
	char ext_raw[4] = { 0 };
	int  base_raw_len = 0;
	int  ext_raw_len = 0;

	// Fill ext (up to 3 uppercase legal chars after the last dot).
	if (last_dot) {
		for (const char* p = last_dot + 1; *p && ext_raw_len < 3; p++) {
			char c = *p;
			if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
			if ((unsigned char) c < 0x20 || strchr(illegal, c)) c = '_';
			ext_raw[ext_raw_len++] = c;
		}
	}

	// Fill base (uppercase legal chars up to last_dot or end, skip leading dots and spaces).
	const char* name_end = last_dot ? last_dot : (long_name + strlen(long_name));
	for (const char* p = long_name; p < name_end && base_raw_len < 8; p++) {
		char c = *p;
		if (c == '.') continue; // embedded dots are dropped
		if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
		if ((unsigned char) c < 0x20 || strchr(illegal, c)) c = '_';
		base_raw[base_raw_len++] = c;
	}

	if (base_raw_len == 0) base_raw[base_raw_len++] = '_';

	// Truncate the base to 6 chars to leave room for "~N"
	if (base_raw_len > 6) base_raw_len = 6;
	base_raw[base_raw_len] = '\0';

	// Try ~1 .. ~999999
	for (int n = 1; n <= 999999; n++) {
		char tail[8];
		int  tail_len = snprintf(tail, sizeof(tail), "~%d", n);

		// base prefix length = min(6, 8 - tail_len)
		int prefix_len = 8 - tail_len;
		if (prefix_len > base_raw_len) prefix_len = base_raw_len;
		if (prefix_len < 0) prefix_len = 0;

		// Assemble candidate short name string for collision check.
		char candidate[13] = { 0 };
		int  cand_len = 0;
		for (int i = 0; i < prefix_len; i++)
			candidate[cand_len++] = base_raw[i];
		for (int i = 0; i < tail_len; i++)
			candidate[cand_len++] = tail[i];
		if (ext_raw_len > 0) {
			candidate[cand_len++] = '.';
			for (int i = 0; i < ext_raw_len; i++)
				candidate[cand_len++] = ext_raw[i];
		}
		candidate[cand_len] = '\0';

		// Pack to raw11 and check for collisions.
		uint8_t candidate_raw11[11];
		if (!fat_pack_short_name_raw(candidate, candidate_raw11)) continue;

		bool collision = false;
		if (existing) {
			for (size_t i = 0; i < existing->count; i++) {
				if (memcmp(existing->entries[i].raw.name, candidate_raw11, 11) == 0) {
					collision = true;
					break;
				}
			}
		}

		if (!collision) {
			memcpy(raw11, candidate_raw11, 11);
			return true;
		}
	}

	return false; // exhausted all potential names
}

bool fat_dir_reserve_slot_run(fat_dir_writer_t* w, int need, fat_dirent_slot_t* out_first_slot) {
	if (!w || !w->ops || need <= 0 || !out_first_slot) return false;

	uint16_t bps = w->bytes_per_sector;
	uint32_t entries_ps = bps / 32;
	if (bps == 0 || entries_ps == 0) return false;

	uint8_t* sector = (uint8_t*) kalloc(bps);
	if (!sector) return false;

	uint32_t total_sectors = w->ops->sector_count(w);
	int      run_len = 0;
	fat_dirent_slot_t run_start = { 0, 0 };
	bool found = false;
	bool replaced_end_marker = false;
	bool in_end_zone = false;

	uint32_t si = 0;

	// Dynamically span sectors (and cluster extensions) until we satisfy `need`
	while (!found) {
		if (si >= total_sectors) {
			if (!w->ops->extend(w)) {
				kfree(sector);
				return false;
			}
			total_sectors = w->ops->sector_count(w);
		}

		if (!w->ops->read_sector(w, si, sector)) {
			kfree(sector);
			return false;
		}

		for (uint32_t off = 0; off + 32 <= bps; off += 32) {
			// If we hit an END_MARKER previously, everything after is implicitly an END_MARKER
			uint8_t first = in_end_zone ? FAT_DIRENT_END_MARKER : sector[off];

			if (first == FAT_DIRENT_FREE_MARKER || first == FAT_DIRENT_END_MARKER) {
				if (run_len == 0) {
					run_start.sector_index = si;
					run_start.offset = off;
				}
				if (first == FAT_DIRENT_END_MARKER) {
					in_end_zone = true;
					replaced_end_marker = true;
				}
				run_len++;
				if (run_len >= need) {
					found = true;
					break;
				}
			} else {
				run_len = 0;
				replaced_end_marker = false;
			}
		}
		si++;
	}

	kfree(sector);
	*out_first_slot = run_start;

	// Only cap the run with a new END_MARKER if we actively consumed the old one
	if (replaced_end_marker) {
		uint32_t term_flat = run_start.sector_index * entries_ps
			+ run_start.offset / 32
			+ (uint32_t) need;
		fat_dirent_slot_t term_slot;
		term_slot.sector_index = term_flat / entries_ps;
		term_slot.offset = (term_flat % entries_ps) * 32;

		uint32_t cur_total = w->ops->sector_count(w);
		if (term_slot.sector_index < cur_total) {
			uint8_t* nbuf = kalloc(bps);
			if (nbuf) {
				if (w->ops->read_sector(w, term_slot.sector_index, nbuf)) {
					// Prevent writing if it's already an end marker
					if (nbuf[term_slot.offset] != FAT_DIRENT_END_MARKER) {
						memset(nbuf + term_slot.offset, 0, 32);
						w->ops->write_sector(w, term_slot.sector_index, nbuf);
					}
				}
				kfree(nbuf);
			}
		}
	}

	return true;
}