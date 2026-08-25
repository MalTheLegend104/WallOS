#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <filesystem/fat/fat.h>
#include <filesystem/fat/fat_internal.h>
#include <memory/kernel_alloc.h>


void fill_fat32(const uint8_t* buf, fat32_ebr_t* fat32) {
	// First, grab the base BPB
	fill_bpb(buf, &fat32->bpb);

	// FAT32 extended fields start at offset 36
	fat32->sectors_per_fat = fat_internal_read32(buf, 36);
	fat32->flags = fat_internal_read16(buf, 40);
	fat32->fat_version = fat_internal_read16(buf, 42);
	fat32->root_cluster_number = fat_internal_read32(buf, 44);
	fat32->fsinfo_sector = fat_internal_read16(buf, 48);
	fat32->backup_boot_sector = fat_internal_read16(buf, 50);

	// Skip 12 reserved bytes (offsets 52 to 63)

	// FAT32 drive parameters start at offset 64
	fat32->drive_number = buf[64];
	fat32->windows_nt_flags = buf[65];
	fat32->signature = buf[66];
	fat32->volume_id = fat_internal_read32(buf, 67);

	memcpy(fat32->volume_label, &buf[71], 11);
	fat32->volume_label[11] = '\0';

	memcpy(fat32->system_string_id, &buf[82], 8);
	fat32->system_string_id[8] = '\0';

	// Grab the magic signature from the very end of the 512-byte sector
	fat32->partition_signature = fat_internal_read16(buf, 510);
}

void fill_fat32_fsinfo(const uint8_t* buf, fat32_fsinfo_t* fs_info) {
	if (!fs_info) return;

	fs_info->signature = fat_internal_read32(buf, 0);
	fs_info->signature2 = fat_internal_read32(buf, 0x1E4);
	fs_info->last_known_free_cluster = fat_internal_read32(buf, 0x1E8);
	fs_info->search_cluster_num = fat_internal_read32(buf, 0x1EC);
	fs_info->trail_signature = fat_internal_read32(buf, 0x1FC);
}

void print_fat32(const fat32_ebr_t* fat) {
	print_bpb(&fat->bpb);

	printf("\n=== FAT32 Extended Boot Record ===\n");
	printf("Sectors/FAT          : %u\n", fat->sectors_per_fat);
	printf("Flags                : 0x%04X\n", fat->flags);
	printf("FAT Version          : %u.%u\n", (fat->fat_version >> 8) & 0xFF, fat->fat_version & 0xFF);
	printf("Root Cluster         : %u\n", fat->root_cluster_number);
	printf("FSInfo Sector        : %u\n", fat->fsinfo_sector);
	printf("Backup Boot Sector   : %u\n", fat->backup_boot_sector);
	printf("Drive Number         : 0x%02X\n", fat->drive_number);
	printf("NT Flags             : 0x%02X\n", fat->windows_nt_flags);
	printf("Signature            : 0x%02X\n", fat->signature);
	printf("Volume ID            : 0x%08X\n", fat->volume_id);
	printf("Volume Label         : %s\n", (char*) fat->volume_label);
	printf("System ID            : %s\n", (char*) fat->system_string_id);
	printf("Boot Signature       : 0x%04X\n", fat->partition_signature);
}

void print_fsinfo(const fat32_fsinfo_t* fs) {
	printf("\n=== FAT32 FSInfo ===\n");
	printf("Lead Signature      : 0x%08X %s\n", fs->signature, fs->signature == 0x41615252 ? "(valid)" : "(INVALID)");
	printf("Struct Signature    : 0x%08X %s\n", fs->signature2, fs->signature2 == 0x61417272 ? "(valid)" : "(INVALID)");

	if (fs->last_known_free_cluster == 0xFFFFFFFF) printf("Free Clusters       : Unknown\n");
	else printf("Free Clusters       : %u\n", fs->last_known_free_cluster);

	if (fs->search_cluster_num == 0xFFFFFFFF) printf("Next Free Cluster   : Auto (start at 2)\n");
	else printf("Next Free Cluster   : %u\n", fs->search_cluster_num);

	printf("Trail Signature     : 0x%08X %s\n", fs->trail_signature, fs->trail_signature == 0xAA550000 ? "(valid)" : "(INVALID)");
}

uint32_t fat32_get_fat_entry(WDM_DriveHandle drive, const fat32_ebr_t* fat32, uint32_t cluster) {
	if (cluster < 2 || cluster >= FAT32_BAD_CLUSTER) return 0;

	uint32_t fat_start_sector = fat32->bpb.reserved_sectors;
	uint32_t fat_offset = cluster * 4;
	uint32_t fat_sector = fat_start_sector + (fat_offset / fat32->bpb.bytes_per_sector);
	uint32_t entry_offset = fat_offset % fat32->bpb.bytes_per_sector;

	uint8_t sector[512];
	WDM_Read(drive, fat_sector, 1, sector, WDM_FLAG_NONE);

	uint32_t entry = fat_internal_read32(sector, entry_offset);
	return entry & 0x0FFFFFFF;
}

bool fat32_list_directory(WDM_DriveHandle drive, const fat32_ebr_t* fat32, uint32_t dir_cluster, fat_dirent_list_t* out_list) {
	if (!drive || !fat32 || !out_list) return false;

	fat_dirent_list_init(out_list);

	uint16_t bytes_per_sector = fat32->bpb.bytes_per_sector;
	uint8_t sectors_per_cluster = fat32->bpb.sectors_per_cluster;

	if (bytes_per_sector == 0 || sectors_per_cluster == 0) return false;

	uint8_t* sector = kalloc(bytes_per_sector);
	if (!sector) return false;

	fat_lfn_run_t lfn_run;
	fat_lfn_run_reset(&lfn_run);

	uint32_t cluster = dir_cluster;
	bool end_of_directory = false;
	// Cap the total clusters walked to protect against cyclic chains
	// 0x0FFFFFF8 is teh largest cluster count on a real FAT32 volume
	uint32_t clusters_walked = 0;
	const uint32_t MAX_CLUSTERS_WALKED = 0x0FFFFFF8;

	while (!end_of_directory && cluster >= 2 && cluster < FAT32_BAD_CLUSTER) {
		if (++clusters_walked > MAX_CLUSTERS_WALKED) break;

		uint32_t start_sector = fat32_cluster_to_sector(fat32, cluster);

		for (uint8_t s = 0; s < sectors_per_cluster && !end_of_directory; s++) {
			WDM_Status status = WDM_Read(drive, start_sector + s, 1, sector, WDM_FLAG_NONE);
			if (status != WDM_OK) {
				kfree(sector);
				fat_dirent_list_free(out_list);
				return false;
			}

			for (uint32_t off = 0; off + 32 <= bytes_per_sector; off += 32) {
				const uint8_t* raw32 = sector + off;
				uint8_t first_byte = raw32[0];

				if (first_byte == FAT_DIRENT_END_MARKER) {
					// 0x00 means no further entries in this dir
					end_of_directory = true;
					break;
				}

				if (first_byte == FAT_DIRENT_FREE_MARKER) {
					// Deleted Entry
					fat_lfn_run_reset(&lfn_run);
					continue;
				}

				uint8_t attributes = raw32[11];

				if (attributes == FAT_ATTR_LFN) {
					fat_lfn_run_accumulate(&lfn_run, raw32);
					continue;
				}

				if (attributes & FAT_ATTR_VOLUME_ID) {
					// Volume Label Entry, not a real file/dir
					fat_lfn_run_reset(&lfn_run);
					continue;
				}

				// Real short entry
				fat_resolved_dirent_t resolved;
				memset(&resolved, 0, sizeof(resolved));

				// Escaped 0xE5
				// name[0] in the raw bytes may be 0x05 standing in for a literal 0xE5 character
				uint8_t raw11[11];
				memcpy(raw11, raw32, 11);
				if (raw11[0] == FAT_DIRENT_ESCAPED_E5) raw11[0] = 0xE5;

				fat_fill_dirent_raw(raw32, &resolved.raw);
				memcpy(resolved.raw.name, raw11, 11);
				resolved.raw.name[11] = '\0';

				fat_format_short_name(raw11, resolved.short_name);

				uint8_t expected_checksum = fat_short_name_checksum(raw11);
				fat_lfn_run_resolve(&lfn_run, expected_checksum, resolved.long_name);
				fat_lfn_run_reset(&lfn_run);

				resolved.first_cluster = ((uint32_t) resolved.raw.first_cluster_high << 16) | (uint32_t) resolved.raw.first_cluster_low;

				if (!fat_dirent_list_push(out_list, &resolved)) {
					kfree(sector);
					fat_dirent_list_free(out_list);
					return false;
				}
			}
		}

		if (!end_of_directory) {
			cluster = fat32_get_fat_entry(drive, fat32, cluster);
		}
	}

	kfree(sector);
	return true;
}

void fat32_tree_recurse(WDM_DriveHandle drive, const fat32_ebr_t* fat32, uint32_t dir_cluster, int depth, const char* prefix) {
	if (depth > FAT_TREE_MAX_DEPTH) {
		printf("%s[max depth reached, stopping]\n", prefix);
		return;
	}

	fat_dirent_list_t listing;
	if (!fat32_list_directory(drive, fat32, dir_cluster, &listing)) {
		printf("%s[failed to read directory]\n", prefix);
		return;
	}

	// Count visible entries up front so the last one gets "└──" instead of "├──"
	size_t visible_count = 0;
	for (size_t i = 0; i < listing.count; i++) if (!fat_is_dot_entry(&listing.entries[i])) visible_count++;

	size_t visible_index = 0;
	for (size_t i = 0; i < listing.count; i++) {
		const fat_resolved_dirent_t* e = &listing.entries[i];
		if (fat_is_dot_entry(e)) continue;

		bool is_last = (++visible_index == visible_count);
		const char* branch = is_last ? "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 " /* "└── " */ : "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80 "; /* "├── " */

		const char* display_name = (e->long_name[0] != '\0') ? e->long_name : e->short_name;
		bool is_dir = (e->raw.attributes & FAT_ATTR_DIRECTORY) != 0;

		if (is_dir) {
			printf("%s%s%s/\n", prefix, branch, display_name);
		} else {
			printf("%s%s%s (%u bytes)\n", prefix, branch, display_name, e->raw.file_size);
		}

		if (is_dir) {
			// first_cluster == 0 only happens on "..", which we've already skipped above
			// Having it here would mean corruption that we do *not* want to deal with
			if (e->first_cluster < 2) {
				char child_prefix[1024];
				snprintf(child_prefix, sizeof(child_prefix), "%s%s", prefix, is_last ? "    " : "\xE2\x94\x82   " /* "│   " */);
				printf("%s[invalid cluster %u, skipping]\n", child_prefix, e->first_cluster);
				continue;
			}

			char child_prefix[1024];
			snprintf(child_prefix, sizeof(child_prefix), "%s%s", prefix, is_last ? "    " : "\xE2\x94\x82   " /* "│   " */);

			fat32_tree_recurse(drive, fat32, e->first_cluster, depth + 1, child_prefix);
		}
	}

	fat_dirent_list_free(&listing);
}

void fat32_tree(WDM_DriveHandle drive, const fat32_ebr_t* fat32) {
	printf("/\n");
	fat32_tree_recurse(drive, fat32, fat32->root_cluster_number, 1, "");
}

fat_lookup_status_t fat32_resolve_path(WDM_DriveHandle drive, const fat32_ebr_t* fat32, const char* path, bool want_directory, fat_resolved_dirent_t* out_entry) {
	if (!path || path[0] == '\0') return FAT_LOOKUP_BAD_PATH;

	char path_copy[1024];
	if (strlen(path) >= sizeof(path_copy)) return FAT_LOOKUP_BAD_PATH;
	strcpy(path_copy, path);

	uint32_t current_cluster = fat32->root_cluster_number;
	bool have_entry = false;
	fat_resolved_dirent_t current_entry;
	memset(&current_entry, 0, sizeof(current_entry));

	char* cursor = path_copy;
	if (*cursor == '/') cursor++; // ignore a leading slash

	// Path was root, only valid if the caller wanted a directory.
	// Technically a bad path, FAT32 root has no entry in its own parent
	if (*cursor == '\0') return FAT_LOOKUP_BAD_PATH;

	while (*cursor != '\0') {
		char* slash = strchr(cursor, '/');
		bool is_last_component = (slash == NULL);
		if (slash) *slash = '\0';

		if (*cursor == '\0') {
			// Empty Component ("FOO//BAR")
			return FAT_LOOKUP_BAD_PATH;
		}

		fat_dirent_list_t listing;
		if (!fat32_list_directory(drive, fat32, current_cluster, &listing)) {
			return FAT_LOOKUP_IO_ERROR;
		}

		const fat_resolved_dirent_t* found = fat_dirent_list_find(&listing, cursor);
		if (!found) {
			fat_dirent_list_free(&listing);
			return FAT_LOOKUP_NOT_FOUND;
		}

		bool found_is_dir = (found->raw.attributes & FAT_ATTR_DIRECTORY) != 0;

		if (!is_last_component && !found_is_dir) {
			// Tried to decend through a file
			// Example: "/HELLO.TXT/DIR2/ANOTHER_FILE.TXT"
			fat_dirent_list_free(&listing);
			return FAT_LOOKUP_WRONG_TYPE;
		}

		if (is_last_component && found_is_dir != want_directory) {
			fat_dirent_list_free(&listing);
			return FAT_LOOKUP_WRONG_TYPE;
		}

		current_entry = *found;
		have_entry = true;
		current_cluster = found->first_cluster;

		fat_dirent_list_free(&listing);

		if (is_last_component) break;
		cursor = slash + 1;
	}

	if (!have_entry) return FAT_LOOKUP_NOT_FOUND; // shouldn't happen, make sure it doesn't anyway

	*out_entry = current_entry;
	return FAT_LOOKUP_OK;
}

fat_lookup_status_t fat32_find_file(WDM_DriveHandle drive, const fat32_ebr_t* fat32, const char* path, fat_resolved_dirent_t* out_entry) {
	return fat32_resolve_path(drive, fat32, path, false, out_entry);
}

fat_lookup_status_t fat32_find_directory(WDM_DriveHandle drive, const fat32_ebr_t* fat32, const char* path, fat_resolved_dirent_t* out_entry) {
	return fat32_resolve_path(drive, fat32, path, true, out_entry);
}

uint8_t* fat32_read_file(WDM_DriveHandle drive, const fat32_ebr_t* fat32, const fat_resolved_dirent_t* entry, uint32_t* out_size) {
	if (!drive || !fat32 || !entry || !out_size) return NULL;
	if (entry->raw.attributes & FAT_ATTR_DIRECTORY) return NULL; // not a file

	*out_size = 0;

	// Empty file, we don't need to touch the cluster chain at all
	if (entry->raw.file_size == 0) {
		uint8_t* empty = kalloc(1);
		return empty; // may be NULL on alloc failure, which is what we want anyway
	}

	if (entry->first_cluster < 2) return NULL; // non-empty file with no valid cluster implies it's corrupt

	uint16_t bytes_per_sector = fat32->bpb.bytes_per_sector;
	uint8_t sectors_per_cluster = fat32->bpb.sectors_per_cluster;
	if (bytes_per_sector == 0 || sectors_per_cluster == 0) return NULL;

	uint8_t* out_buf = kalloc(entry->raw.file_size);
	if (!out_buf) return NULL;

	uint8_t* sector_buf = kalloc(bytes_per_sector);
	if (!sector_buf) {
		kfree(out_buf);
		return NULL;
	}

	uint32_t cluster = entry->first_cluster;
	uint32_t bytes_written = 0;
	uint32_t clusters_walked = 0;
	const uint32_t MAX_CLUSTERS_WALKED = 0x0FFFFFF8; // corruption guard

	while (bytes_written < entry->raw.file_size && cluster >= 2 && cluster < FAT32_BAD_CLUSTER) {
		if (++clusters_walked > MAX_CLUSTERS_WALKED) break;

		uint32_t start_sector = fat32_cluster_to_sector(fat32, cluster);

		for (uint8_t s = 0; s < sectors_per_cluster && bytes_written < entry->raw.file_size; s++) {
			WDM_Status status = WDM_Read(drive, start_sector + s, 1, sector_buf, WDM_FLAG_NONE);
			if (status != WDM_OK) {
				kfree(sector_buf);
				kfree(out_buf);
				return NULL;
			}

			// Only cope as many bytes as remain in the file.
			// The final sector of the final cluster is usually only partially used.
			// We don't want to cause a buffer overflow
			uint32_t remaining = entry->raw.file_size - bytes_written;
			uint32_t to_copy = (remaining < bytes_per_sector) ? remaining : bytes_per_sector;

			memcpy(out_buf + bytes_written, sector_buf, to_copy);
			bytes_written += to_copy;
		}

		if (bytes_written >= entry->raw.file_size) break;

		cluster = fat32_get_fat_entry(drive, fat32, cluster);
	}

	kfree(sector_buf);

	if (bytes_written != entry->raw.file_size) {
		// Chain ended (EOC, corruption, or loop-cap) before we got to file_size bytes
		// Either file_size lied or the chain is broken.
		// We return NULL to indicate that *something* went wrong.
		kfree(out_buf);
		return NULL;
	}

	*out_size = entry->raw.file_size;
	return out_buf;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Write Functions
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

bool fat32_set_fat_entry(WDM_DriveHandle drive, const fat32_ebr_t* fat32, uint32_t cluster, uint32_t value) {
	if (!drive || !fat32) return false;
	if (cluster < 2) return false; // reserved, never written through this path

	uint16_t bps = fat32->bpb.bytes_per_sector;
	if (bps == 0) return false;

	uint8_t buf[4096];
	if (bps > sizeof(buf)) return false;

	uint32_t fat_offset = cluster * 4;
	uint32_t sector_in_fat = fat_offset / bps;
	uint32_t offset_in_sector = fat_offset % bps;

	bool any_written = false;

	for (uint8_t fat_idx = 0; fat_idx < fat32->bpb.fat_allcation_tables; fat_idx++) {
		uint32_t sector = fat32->bpb.reserved_sectors + (uint32_t) fat_idx * fat32->sectors_per_fat + sector_in_fat;

		if (WDM_Read(drive, sector, 1, buf, WDM_FLAG_NONE) != WDM_OK) return false;

		// Top 4 bits of a FAT32 entry are reserved
		// We keep whatever was alrady there in case some other FAT driver put something there
		uint32_t existing = fat_internal_read32(buf, offset_in_sector);
		uint32_t new_entry = (existing & 0xF0000000) | (value & 0x0FFFFFFF);
		fat_internal_write32(buf, offset_in_sector, new_entry);

		if (WDM_Write(drive, sector, 1, buf, WDM_FLAG_NONE) != WDM_OK) return false;
		any_written = true;
	}

	return any_written;
}

bool fat32_write_fsinfo(WDM_DriveHandle drive, const fat32_ebr_t* fat32, const fat32_fsinfo_t* fsinfo) {
	if (!drive || !fat32 || !fsinfo) return false;

	uint32_t fsinfo_sector = fat32->fsinfo_sector;

	if (fsinfo_sector == 0) return false;

	uint8_t sector[512];
	memset(sector, 0, sizeof(sector));

	fat_internal_write32(sector, 0, fsinfo->signature);
	fat_internal_write32(sector, 484, fsinfo->signature2);
	fat_internal_write32(sector, 488, fsinfo->last_known_free_cluster);
	fat_internal_write32(sector, 492, fsinfo->search_cluster_num);
	fat_internal_write32(sector, 508, fsinfo->trail_signature);

	return WDM_Write(drive, fsinfo_sector, 1, sector, WDM_FLAG_NONE) == WDM_OK;
}

bool fat32_find_free_cluster(WDM_DriveHandle drive, const fat32_ebr_t* fat32, uint32_t start_hint, uint32_t* out_cluster) {
	if (!drive || !fat32 || !out_cluster) return false;

	uint16_t bps = fat32->bpb.bytes_per_sector;
	if (bps == 0 || fat32->sectors_per_fat == 0) return false;

	uint8_t buf[4096];
	if (bps > sizeof(buf)) return false;

	uint32_t entries_per_sector = bps / 4;
	if (entries_per_sector == 0) return false;
	uint32_t max_entries = fat32->sectors_per_fat * entries_per_sector;
	if (max_entries <= 2) return false;

	uint32_t cluster = (start_hint >= 2 && start_hint < max_entries) ? start_hint : 2;
	uint32_t fat_start_sector = fat32->bpb.reserved_sectors;
	uint32_t loaded_sector = (uint32_t) -1;

	for (uint32_t scanned = 0; scanned < max_entries - 2; scanned++, cluster++) {
		if (cluster >= max_entries) cluster = 2; /* wrap once */

		uint32_t fat_sector = fat_start_sector + (cluster / entries_per_sector);
		uint32_t offset_in_sector = (cluster % entries_per_sector) * 4;

		if (fat_sector != loaded_sector) {
			if (WDM_Read(drive, fat_sector, 1, buf, WDM_FLAG_NONE) != WDM_OK) return false;
			loaded_sector = fat_sector;
		}

		uint32_t entry = fat_internal_read32(buf, offset_in_sector) & 0x0FFFFFFF;
		if (entry == FAT32_FREE_CLUSTER) {
			*out_cluster = cluster;
			return true;
		}
	}

	return false; // volume full
}

bool fat32_free_cluster_chain(WDM_DriveHandle drive, const fat32_ebr_t* fat32, fat32_fsinfo_t* fsinfo, uint32_t start_cluster) {
	if (start_cluster < 2) return true; // nothing to free
	if (!drive || !fat32) return false;

	uint32_t cluster = start_cluster;
	uint32_t walked = 0;
	const uint32_t MAX_CLUSTERS_WALKED = 0x0FFFFFF8;

	// We need to track how many we've freed so that we can write back the total free clusters to fsinfo
	uint32_t clusters_freed = 0;

	while (cluster >= 2 && cluster < FAT32_BAD_CLUSTER) {
		if (++walked > MAX_CLUSTERS_WALKED) return false;
		uint32_t next = fat32_get_fat_entry(drive, fat32, cluster);
		if (!fat32_set_fat_entry(drive, fat32, cluster, FAT32_FREE_CLUSTER)) return false;
		cluster = next;
		clusters_freed++;
	}

	if (fsinfo && clusters_freed > 0) {
		if (fsinfo->last_known_free_cluster != 0xFFFFFFFF) {
			fsinfo->last_known_free_cluster += clusters_freed;
		}
		fat32_write_fsinfo(drive, fat32, fsinfo);
	}

	return true;
}

bool fat32_write_file_data(WDM_DriveHandle drive, const fat32_ebr_t* fat32, fat32_fsinfo_t* fsinfo, uint32_t existing_first_cluster, const uint8_t* data, uint32_t size, uint32_t* out_first_cluster) {
	if (!drive || !fat32 || !out_first_cluster) return false;
	if (size > 0 && !data) return false;

	uint16_t bps = fat32->bpb.bytes_per_sector;
	uint8_t spc = fat32->bpb.sectors_per_cluster;
	if (bps == 0 || spc == 0) return false;

	uint8_t* sector_buf = kalloc(bps);
	if (!sector_buf) return false;

	uint32_t cluster = existing_first_cluster;
	uint32_t prev_cluster = 0;
	uint32_t first_cluster = 0;
	uint32_t bytes_written = 0;
	bool cluster_in_old_chain = (cluster >= 2 && cluster < FAT32_BAD_CLUSTER);

	while (bytes_written < size) {
		uint32_t target_cluster;

		if (cluster_in_old_chain) {
			target_cluster = cluster;
		} else {
			uint32_t new_cluster;

			if (!fat32_find_free_cluster(drive, fat32, 2, &new_cluster)) {
				kfree(sector_buf);
				// Cap whatever we've written so far with EOC rather than leaving it open
				if (prev_cluster) fat32_set_fat_entry(drive, fat32, prev_cluster, FAT32_EOC_MAX);
				return false;
			}

			/* Mark a newly allocated cluster as EOC immediately so it can't be reallocated before the chain is linked.
			 * If another cluster follows, this temporary EOC is replaced properly.
			 */
			if (!fat32_set_fat_entry(drive, fat32, new_cluster, FAT32_EOC_MAX)) {
				kfree(sector_buf);
				return false;
			}

			target_cluster = new_cluster;

			if (fsinfo) {
				fsinfo->search_cluster_num = new_cluster + 1;
				if (fsinfo->last_known_free_cluster != 0xFFFFFFFF &&
					fsinfo->last_known_free_cluster > 0)
					fsinfo->last_known_free_cluster--;
				fat32_write_fsinfo(drive, fat32, fsinfo);
			}

		}

		if (first_cluster == 0) first_cluster = target_cluster;
		if (prev_cluster != 0) {
			if (!fat32_set_fat_entry(drive, fat32, prev_cluster, target_cluster)) {
				kfree(sector_buf);
				return false;
			}
		}

		bool came_from_old_chain = cluster_in_old_chain && (cluster == target_cluster);
		uint32_t next_in_old_chain = 0;
		if (came_from_old_chain) {
			// Must read the old "next" pointer before this cluster's FAT entry gets overwritten
			next_in_old_chain = fat32_get_fat_entry(drive, fat32, cluster);
		}

		uint32_t start_sector = fat32_cluster_to_sector(fat32, target_cluster);
		for (uint8_t s = 0; s < spc && bytes_written < size; s++) {
			uint32_t remaining = size - bytes_written;
			uint32_t to_copy = (remaining < bps) ? remaining : bps;

			// Zero-pad a partial final sector
			if (to_copy < bps) memset(sector_buf, 0, bps);
			memcpy(sector_buf, data + bytes_written, to_copy);

			if (WDM_Write(drive, start_sector + s, 1, sector_buf, WDM_FLAG_NONE) != WDM_OK) {
				kfree(sector_buf);
				return false;
			}
			bytes_written += to_copy;
		}

		prev_cluster = target_cluster;
		if (came_from_old_chain) {
			cluster = next_in_old_chain;
			cluster_in_old_chain = (cluster >= 2 && cluster < FAT32_BAD_CLUSTER);
		} else {
			cluster_in_old_chain = false;
		}
	}

	kfree(sector_buf);

	if (size == 0) {
		// Empty file. We free whatever chain existed
		if (!fat32_free_cluster_chain(drive, fat32, fsinfo, existing_first_cluster)) return false;
		*out_first_cluster = 0;
		return true;
	}

	if (!fat32_set_fat_entry(drive, fat32, prev_cluster, FAT32_EOC_MAX)) return false;

	// If the chain was longer than needed the cluster holds the first chain of the unused tail
	if (cluster_in_old_chain) {
		if (!fat32_free_cluster_chain(drive, fat32, fsinfo, cluster)) return false;
	}

	*out_first_cluster = first_cluster;
	return true;
}

typedef struct {
	WDM_DriveHandle drive;
	const fat32_ebr_t* fat32;
	fat32_fsinfo_t* fsinfo;
	uint32_t start_cluster;
} fat32_dir_writer_ctx_t;

static uint32_t fat32_dw_sector_count(fat_dir_writer_t* w) {
	fat32_dir_writer_ctx_t* ctx = (fat32_dir_writer_ctx_t*) w->ctx;
	uint8_t spc = ctx->fat32->bpb.sectors_per_cluster;

	uint32_t clusters = 0;
	uint32_t cluster = ctx->start_cluster;
	uint32_t walked = 0;
	const uint32_t MAX_CLUSTERS_WALKED = 0x0FFFFFF8;

	while (cluster >= 2 && cluster < FAT32_BAD_CLUSTER) {
		if (++walked > MAX_CLUSTERS_WALKED) break;
		clusters++;
		cluster = fat32_get_fat_entry(ctx->drive, ctx->fat32, cluster);
	}

	return clusters * spc;
}

static bool fat32_dw_cluster_for_sector(fat32_dir_writer_ctx_t* ctx, uint32_t sector_index, uint32_t* out_cluster, uint8_t* out_sector_in_cluster) {
	uint8_t spc = ctx->fat32->bpb.sectors_per_cluster;
	if (spc == 0) return false;

	uint32_t target_cluster_idx = sector_index / spc;
	uint8_t sector_in_cluster = (uint8_t) (sector_index % spc);

	uint32_t cluster = ctx->start_cluster;
	uint32_t walked = 0;
	const uint32_t MAX_CLUSTERS_WALKED = 0x0FFFFFF8;

	for (uint32_t i = 0; i < target_cluster_idx; i++) {
		if (cluster < 2 || cluster >= FAT32_BAD_CLUSTER) return false;
		if (++walked > MAX_CLUSTERS_WALKED) return false;
		cluster = fat32_get_fat_entry(ctx->drive, ctx->fat32, cluster);
	}
	if (cluster < 2 || cluster >= FAT32_BAD_CLUSTER) return false;

	*out_cluster = cluster;
	*out_sector_in_cluster = sector_in_cluster;
	return true;
}

static bool fat32_dw_read_sector(fat_dir_writer_t* w, uint32_t sector_index, uint8_t* buf) {
	fat32_dir_writer_ctx_t* ctx = (fat32_dir_writer_ctx_t*) w->ctx;
	uint32_t cluster;
	uint8_t s_in_c;
	if (!fat32_dw_cluster_for_sector(ctx, sector_index, &cluster, &s_in_c)) return false;
	uint32_t lba = fat32_cluster_to_sector(ctx->fat32, cluster) + s_in_c;
	return WDM_Read(ctx->drive, lba, 1, buf, WDM_FLAG_NONE) == WDM_OK;
}

static bool fat32_dw_write_sector(fat_dir_writer_t* w, uint32_t sector_index, const uint8_t* buf) {
	fat32_dir_writer_ctx_t* ctx = (fat32_dir_writer_ctx_t*) w->ctx;
	uint32_t cluster;
	uint8_t s_in_c;
	if (!fat32_dw_cluster_for_sector(ctx, sector_index, &cluster, &s_in_c)) return false;
	uint32_t lba = fat32_cluster_to_sector(ctx->fat32, cluster) + s_in_c;
	return WDM_Write(ctx->drive, lba, 1, buf, WDM_FLAG_NONE) == WDM_OK;
}

static bool fat32_dw_extend(fat_dir_writer_t* w) {
	fat32_dir_writer_ctx_t* ctx = (fat32_dir_writer_ctx_t*) w->ctx;
	uint16_t bps = ctx->fat32->bpb.bytes_per_sector;
	uint8_t spc = ctx->fat32->bpb.sectors_per_cluster;

	// Find the current last cluster of the clain
	uint32_t cluster = ctx->start_cluster;
	if (cluster < 2 || cluster >= FAT32_BAD_CLUSTER) return false;
	uint32_t walked = 0;
	const uint32_t MAX_CLUSTERS_WALKED = 0x0FFFFFF8;
	while (true) {
		uint32_t next = fat32_get_fat_entry(ctx->drive, ctx->fat32, cluster);
		if (next < 2 || next >= FAT32_BAD_CLUSTER) break;
		cluster = next;
		if (++walked > MAX_CLUSTERS_WALKED) return false;
	}

	uint32_t start_hint = 2;
	if (ctx->fsinfo && ctx->fsinfo->search_cluster_num >= 2) {
		start_hint = ctx->fsinfo->search_cluster_num;
	}

	uint32_t new_cluster;
	if (!fat32_find_free_cluster(ctx->drive, ctx->fat32, start_hint, &new_cluster)) return false;

	if (ctx->fsinfo) {
		ctx->fsinfo->search_cluster_num = new_cluster + 1;
		if (ctx->fsinfo->last_known_free_cluster != 0xFFFFFFFF &&
			ctx->fsinfo->last_known_free_cluster > 0) {
			ctx->fsinfo->last_known_free_cluster--;
		}
		fat32_write_fsinfo(ctx->drive, ctx->fat32, ctx->fsinfo);
	}

	uint8_t* zero = kcalloc(1, bps);
	if (!zero) return false;
	uint32_t lba = fat32_cluster_to_sector(ctx->fat32, new_cluster);
	for (uint8_t s = 0; s < spc; s++) {
		if (WDM_Write(ctx->drive, lba + s, 1, zero, WDM_FLAG_NONE) != WDM_OK) {
			kfree(zero);
			return false;
		}
	}
	kfree(zero);

	if (!fat32_set_fat_entry(ctx->drive, ctx->fat32, new_cluster, FAT32_EOC_MAX)) return false;
	if (!fat32_set_fat_entry(ctx->drive, ctx->fat32, cluster, new_cluster)) return false;

	return true;
}

static const fat_dir_writer_ops_t fat32_dir_writer_ops = {
	.sector_count = fat32_dw_sector_count,
	.read_sector = fat32_dw_read_sector,
	.write_sector = fat32_dw_write_sector,
	.extend = fat32_dw_extend,
};

fat_lookup_status_t fat32_write_file(WDM_DriveHandle drive, fat32_ebr_t* fat32, fat32_fsinfo_t* fsinfo, const char* path, const uint8_t* data, uint32_t size, fat_resolved_dirent_t* out_entry) {
	if (!drive || !fat32 || !path || path[0] == '\0') return FAT_LOOKUP_BAD_PATH;
	if (size > 0 && !data) return FAT_LOOKUP_BAD_PATH;

	char path_copy[1024];
	if (strlen(path) >= sizeof(path_copy)) return FAT_LOOKUP_BAD_PATH;
	strcpy(path_copy, path);

	char* cursor = path_copy;
	if (*cursor == '/') cursor++;
	if (*cursor == '\0') return FAT_LOOKUP_BAD_PATH;

	// Split into parent-directory path and the final filename component
	char* last_slash = strrchr(cursor, '/');
	const char* filename;
	uint32_t parent_cluster;

	if (last_slash) {
		*last_slash = '\0';
		filename = last_slash + 1;

		if (*cursor == '\0') {
			parent_cluster = fat32->root_cluster_number;
		} else {
			fat_resolved_dirent_t parent;
			fat_lookup_status_t st = fat32_resolve_path(drive, fat32, cursor, true, &parent);
			if (st != FAT_LOOKUP_OK) return st;
			parent_cluster = parent.first_cluster;
			if (parent_cluster < 2) return FAT_LOOKUP_IO_ERROR; // corrupt parent dirent
		}
	} else {
		filename = cursor;
		parent_cluster = fat32->root_cluster_number;
	}

	if (filename[0] == '\0') return FAT_LOOKUP_BAD_PATH;

	uint8_t raw11[11];
	bool is_83 = fat_pack_short_name_raw(filename, raw11);

	if (!is_83) {
		// The name doesn't fit 8.3, we need the parent dir listing to check for collisions
		fat_dirent_list_t parent_listing;
		fat_dirent_list_init(&parent_listing);
		if (!fat32_list_directory(drive, fat32, parent_cluster, &parent_listing)) {
			return FAT_LOOKUP_IO_ERROR;
		}
		bool ok = fat_generate_short_name(filename, &parent_listing, raw11);
		fat_dirent_list_free(&parent_listing);
		if (!ok) return FAT_LOOKUP_BAD_PATH;
	}

	// Figure out how many slots we need.
	// LFN needs one slot per 13 chars + 1 short entry
	int lfn_count = is_83 ? 0 : (int) ((strlen(filename) + FAT_LFN_MAX_CHARS_PER_ENTRY - 1) / FAT_LFN_MAX_CHARS_PER_ENTRY);
	int slots_needed = lfn_count + 1;

	fat32_dir_writer_ctx_t ctx = {
		.drive = drive,
		.fat32 = fat32,
		.fsinfo = fsinfo,
		.start_cluster = parent_cluster,
	};
	fat_dir_writer_t writer;
	fat_dir_writer_init(&writer, &fat32_dir_writer_ops, &ctx, fat32->bpb.bytes_per_sector);

	fat_dirent_slot_t short_slot;

	if (lfn_count == 0) {
		// Determine if the file already exists
		uint32_t existing_first_cluster = 0;
		fat_dirent_list_t parent_listing;
		fat_dirent_list_init(&parent_listing);

		if (!fat32_list_directory(drive, fat32, parent_cluster, &parent_listing)) {
			return FAT_LOOKUP_IO_ERROR;
		}

		const fat_resolved_dirent_t* found = fat_dirent_list_find(&parent_listing, filename);
		if (found) {
			if (found->raw.attributes & FAT_ATTR_DIRECTORY) {
				fat_dirent_list_free(&parent_listing);
				return FAT_LOOKUP_WRONG_TYPE;
			}
			existing_first_cluster = found->first_cluster;

			// Unlink the old entry entirely to wipe out old LFN footprints
			if (!fat32_unlink_entry(drive, fat32, parent_cluster, found->raw.name)) {
				fat_dirent_list_free(&parent_listing);
				return FAT_LOOKUP_IO_ERROR;
			}
		}
		fat_dirent_list_free(&parent_listing);

		uint32_t new_first_cluster;
		if (!fat32_write_file_data(drive, fat32, fsinfo, existing_first_cluster, data, size, &new_first_cluster)) {
			return FAT_LOOKUP_IO_ERROR;
		}

		// Add the unified directory entry chain (LFNs + Short Entry)
		if (!fat32_add_dirent_chain(drive, fat32, fsinfo, parent_cluster, filename, FAT_ATTR_ARCHIVE, new_first_cluster, size, out_entry)) {
			// Rollback data cluster allocation on failure
			fat32_free_cluster_chain(drive, fat32, fsinfo, new_first_cluster);
			return FAT_LOOKUP_IO_ERROR;
		}

		return FAT_LOOKUP_OK;
	}

	// If we get here we're dealing with LFN

	// Check for an existing entry and unlink it if present.
	uint32_t existing_first_cluster = 0;
	fat_dirent_list_t parent_listing;
	fat_dirent_list_init(&parent_listing);
	if (!fat32_list_directory(drive, fat32, parent_cluster, &parent_listing)) return FAT_LOOKUP_IO_ERROR;

	const fat_resolved_dirent_t* found = fat_dirent_list_find(&parent_listing, filename);
	if (found) {
		if (found->raw.attributes & FAT_ATTR_DIRECTORY) {
			fat_dirent_list_free(&parent_listing);
			return FAT_LOOKUP_WRONG_TYPE;
		}
		existing_first_cluster = found->first_cluster;
		if (!fat32_unlink_entry(drive, fat32, parent_cluster, found->raw.name)) {
			fat_dirent_list_free(&parent_listing);
			return FAT_LOOKUP_IO_ERROR;
		}
	}
	fat_dirent_list_free(&parent_listing);

	fat_dirent_slot_t run_start;
	if (!fat_dir_reserve_slot_run(&writer, slots_needed, &run_start)) return FAT_LOOKUP_IO_ERROR;

	// Write file data, reusing the old cluster chain if there was one.
	uint32_t new_first_cluster;
	if (!fat32_write_file_data(drive, fat32, fsinfo, existing_first_cluster, data, size, &new_first_cluster)) return FAT_LOOKUP_IO_ERROR;

	// Pack and write the LFN entries.
	uint8_t lfn_buf[FAT_LFN_MAX_ENTRIES * 32];
	int written_lfn = fat_pack_lfn_entries(filename, raw11, lfn_buf);
	if (written_lfn != lfn_count) {
		fat32_free_cluster_chain(drive, fat32, fsinfo, new_first_cluster);
		return FAT_LOOKUP_IO_ERROR;
	}

	uint16_t bps = fat32->bpb.bytes_per_sector;
	uint32_t entries_ps = bps / 32;

	for (int i = 0; i < lfn_count; i++) {
		fat_dirent_slot_t slot;
		uint32_t flat = run_start.sector_index * entries_ps + run_start.offset / 32 + (uint32_t) i;
		slot.sector_index = flat / entries_ps;
		slot.offset = (flat % entries_ps) * 32;

		if (!fat_dir_write_slot(&writer, slot, lfn_buf + i * 32)) {
			fat32_free_cluster_chain(drive, fat32, fsinfo, new_first_cluster);
			return FAT_LOOKUP_IO_ERROR;
		}
	}

	// Write the short entry
	uint32_t flat = run_start.sector_index * entries_ps + run_start.offset / 32 + (uint32_t) lfn_count;
	short_slot.sector_index = flat / entries_ps;
	short_slot.offset = (flat % entries_ps) * 32;

	uint8_t new_raw32[32];
	fat_pack_dirent_raw(raw11, FAT_ATTR_ARCHIVE, new_first_cluster, size, new_raw32);
	if (!fat_dir_write_slot(&writer, short_slot, new_raw32)) {
		fat32_free_cluster_chain(drive, fat32, fsinfo, new_first_cluster);
		return FAT_LOOKUP_IO_ERROR;
	}

	if (out_entry) {
		memset(out_entry, 0, sizeof(*out_entry));
		fat_fill_dirent_raw(new_raw32, &out_entry->raw);
		fat_format_short_name(raw11, out_entry->short_name);
		strncpy(out_entry->long_name, filename, FAT_LFN_MAX_NAME_CHARS);
		out_entry->long_name[FAT_LFN_MAX_NAME_CHARS] = '\0';
		out_entry->first_cluster = new_first_cluster;
	}

	return FAT_LOOKUP_OK;
}

bool fat32_write_directory(WDM_DriveHandle drive, const fat32_ebr_t* fat32, fat32_fsinfo_t* fsinfo, uint32_t parent_cluster, uint32_t* out_cluster) {
	if (!drive || !fat32 || !out_cluster) return false;

	uint16_t bps = fat32->bpb.bytes_per_sector;
	uint8_t  spc = fat32->bpb.sectors_per_cluster;
	if (bps == 0 || spc == 0) return false;

	uint32_t start_hint = 2;
	if (fsinfo && fsinfo->search_cluster_num >= 2) start_hint = fsinfo->search_cluster_num;

	uint32_t new_cluster = 0;
	if (!fat32_find_free_cluster(drive, fat32, start_hint, &new_cluster)) return false;
	if (!fat32_set_fat_entry(drive, fat32, new_cluster, FAT32_EOC_MAX)) return false;

	if (fsinfo) {
		fsinfo->search_cluster_num = new_cluster + 1;
		if (fsinfo->last_known_free_cluster != 0xFFFFFFFF && fsinfo->last_known_free_cluster > 0) {
			fsinfo->last_known_free_cluster--;
		}
		fat32_write_fsinfo(drive, fat32, fsinfo);
	}

	uint32_t cluster_bytes = (uint32_t) bps * spc;
	uint8_t* cluster_buf = (uint8_t*) kcalloc(1, cluster_bytes);
	if (!cluster_buf) {
		// Roll back
		fat32_set_fat_entry(drive, fat32, new_cluster, FAT32_FREE_CLUSTER);
		return false;
	}

	uint8_t dot11[11];
	memset(dot11, ' ', 11);
	dot11[0] = '.';
	fat_pack_dirent_raw(dot11, FAT_ATTR_DIRECTORY, new_cluster, 0, cluster_buf);

	uint32_t dotdot_cluster = (parent_cluster == fat32->root_cluster_number) ? 0 : parent_cluster;

	uint8_t dotdot11[11];
	memset(dotdot11, ' ', 11);
	dotdot11[0] = '.';
	dotdot11[1] = '.';
	fat_pack_dirent_raw(dotdot11, FAT_ATTR_DIRECTORY, dotdot_cluster, 0, cluster_buf + 32);

	uint32_t base_lba = fat32_cluster_to_sector(fat32, new_cluster);
	bool write_ok = true;

	for (uint8_t s = 0; s < spc && write_ok; s++) {
		if (WDM_Write(drive, base_lba + s, 1, cluster_buf + (uint32_t) s * bps, WDM_FLAG_NONE) != WDM_OK) {
			write_ok = false;
		}
	}

	kfree(cluster_buf);

	if (!write_ok) {
		// Roll back FAT entry on failure
		fat32_set_fat_entry(drive, fat32, new_cluster, FAT32_FREE_CLUSTER);
		return false;
	}

	*out_cluster = new_cluster;
	return true;
}

bool fat32_write_dir_entry(WDM_DriveHandle drive, const fat32_ebr_t* fat32, fat32_fsinfo_t* fsinfo, uint32_t parent_cluster, const uint8_t raw11[11], const uint8_t raw32[32]) {
	if (!drive || !fat32 || !raw11 || !raw32) return false;
	if (parent_cluster < 2) return false;

	uint16_t bps = fat32->bpb.bytes_per_sector;
	uint8_t  spc = fat32->bpb.sectors_per_cluster;
	if (bps == 0 || spc == 0) return false;

	uint32_t entries_per_sector = bps / 32;

	uint8_t* sector_buf = (uint8_t*) kalloc(bps);
	if (!sector_buf) return false;

	uint32_t target_lba = 0;
	uint32_t target_offset = 0; // byte offset within sector, multiple of 32
	bool slot_found = false;

	uint32_t cur = parent_cluster;
	uint32_t prev = 0;

	while (cur >= 2 && cur < FAT32_BAD_CLUSTER) {
		uint32_t base_lba = fat32_cluster_to_sector(fat32, cur);

		for (uint8_t s = 0; s < spc; s++) {
			uint32_t lba = base_lba + s;
			if (WDM_Read(drive, lba, 1, sector_buf, WDM_FLAG_NONE) != WDM_OK) goto io_fail;

			for (uint32_t e = 0; e < entries_per_sector; e++) {
				uint8_t* slot = sector_buf + e * 32;
				uint8_t  first = slot[0];

				// End-of-directory marker, we can use this slot
				if (first == FAT_DIRENT_END_MARKER) {
					target_lba = lba;
					target_offset = e * 32;
					slot_found = true;
					goto write_slot;
				}

				// LFN Entry, we skip
				if ((slot[11] & FAT_ATTR_LFN) == FAT_ATTR_LFN) continue;

				// Deleted slot. This is a candidate, but keep scanning for name match.
				if (first == FAT_DIRENT_FREE_MARKER) {
					if (!slot_found) {
						target_lba = lba;
						target_offset = e * 32;
						slot_found = true;
					}
					continue;
				}

				// Live slot, check for short-name match.
				uint8_t cmp[11];
				memcpy(cmp, slot, 11);
				if (cmp[0] == FAT_DIRENT_ESCAPED_E5) cmp[0] = 0xE5;
				if (memcmp(cmp, raw11, 11) == 0) {
					target_lba = lba;
					target_offset = e * 32;
					slot_found = true;
					goto write_slot;
				}
			}
		}

		prev = cur;
		cur = fat32_get_fat_entry(drive, fat32, cur);
	}

	// Exhausted the chain without a usable slot, extend by one cluster
	uint32_t start_hint = 2;
	if (fsinfo && fsinfo->search_cluster_num >= 2) start_hint = fsinfo->search_cluster_num;

	uint32_t new_cluster = 0;
	if (!fat32_find_free_cluster(drive, fat32, start_hint, &new_cluster)) goto io_fail;
	if (!fat32_set_fat_entry(drive, fat32, new_cluster, FAT32_EOC_MAX)) goto io_fail;
	if (prev >= 2) {
		if (!fat32_set_fat_entry(drive, fat32, prev, new_cluster)) goto io_fail;
	}

	if (fsinfo) {
		fsinfo->search_cluster_num = new_cluster + 1;
		if (fsinfo->last_known_free_cluster != 0xFFFFFFFF && fsinfo->last_known_free_cluster > 0) fsinfo->last_known_free_cluster--;
		fat32_write_fsinfo(drive, fat32, fsinfo);
	}

	uint32_t new_base = fat32_cluster_to_sector(fat32, new_cluster);
	memset(sector_buf, 0, bps);
	for (uint8_t s = 0; s < spc; s++) {
		if (WDM_Write(drive, new_base + s, 1, sector_buf, WDM_FLAG_NONE) != WDM_OK) goto io_fail;
	}

	// Entry goes into slot 0 of the new cluster.
	target_lba = new_base;
	target_offset = 0;
	slot_found = true;

	// Re-read the (now zeroed) sector so write_slot has a clean buffer.
	if (WDM_Read(drive, target_lba, 1, sector_buf, WDM_FLAG_NONE) != WDM_OK) goto io_fail;

write_slot:
	if (!slot_found) goto io_fail;

	// Read the target sector if we haven't just written/read it above.
	if (WDM_Read(drive, target_lba, 1, sector_buf, WDM_FLAG_NONE) != WDM_OK) goto io_fail;
	memcpy(sector_buf + target_offset, raw32, 32);
	if (WDM_Write(drive, target_lba, 1, sector_buf, WDM_FLAG_NONE) != WDM_OK) goto io_fail;

	kfree(sector_buf);
	return true;

io_fail:
	kfree(sector_buf);
	return false;
}


bool fat32_unlink_entry(WDM_DriveHandle drive, const fat32_ebr_t* fat32, uint32_t parent_cluster, const uint8_t target_raw11[11]) {
	if (!drive || !fat32 || !target_raw11) return false;

	fat32_dir_writer_ctx_t ctx = {
		.drive = drive,
		.fat32 = fat32,
		.fsinfo = NULL,
		.start_cluster = parent_cluster
	};

	fat_dir_writer_t w;
	fat_dir_writer_init(&w, &fat32_dir_writer_ops, &ctx, fat32->bpb.bytes_per_sector);

	uint32_t total_sectors = w.ops->sector_count(&w);
	uint8_t* sector = kalloc(w.bytes_per_sector);
	if (!sector) return false;

	fat_dirent_slot_t lfn_slots[FAT_LFN_MAX_ENTRIES];
	int lfn_count = 0;
	bool found = false;

	for (uint32_t si = 0; si < total_sectors && !found; si++) {
		if (!w.ops->read_sector(&w, si, sector)) break;

		for (uint32_t off = 0; off + 32 <= w.bytes_per_sector; off += 32) {
			uint8_t* slot = sector + off;
			uint8_t first_byte = slot[0];

			if (first_byte == FAT_DIRENT_END_MARKER) {
				found = true; // Hit the end without finding it
				break;
			}
			if (first_byte == FAT_DIRENT_FREE_MARKER) {
				lfn_count = 0; // Chain broken by a deleted entry
				continue;
			}

			// Track LFN slots in our sliding window
			if (slot[11] == FAT_ATTR_LFN) {
				if (lfn_count < FAT_LFN_MAX_ENTRIES) {
					lfn_slots[lfn_count].sector_index = si;
					lfn_slots[lfn_count].offset = off;
					lfn_count++;
				}
				continue;
			}

			// Real short entry evaluation
			uint8_t cmp_name[11];
			memcpy(cmp_name, slot, 11);
			if (cmp_name[0] == FAT_DIRENT_ESCAPED_E5) cmp_name[0] = 0xE5;

			if (memcmp(cmp_name, target_raw11, 11) == 0) {
				// Match found
				// Erase the short entry.
				slot[0] = FAT_DIRENT_FREE_MARKER;

				// Erase tracked LFN slots
				for (int i = 0; i < lfn_count; i++) {
					if (lfn_slots[i].sector_index == si) {
						sector[lfn_slots[i].offset] = FAT_DIRENT_FREE_MARKER;
					} else {
						// LFN slot is in an older sector, fetch and patch it
						uint8_t* old_sec = kalloc(w.bytes_per_sector);
						if (w.ops->read_sector(&w, lfn_slots[i].sector_index, old_sec)) {
							old_sec[lfn_slots[i].offset] = FAT_DIRENT_FREE_MARKER;
							w.ops->write_sector(&w, lfn_slots[i].sector_index, old_sec);
						}
						kfree(old_sec);
					}
				}

				if (w.ops->write_sector(&w, si, sector)) {
					found = true;
				}
				break;
			}

			// If it wasn't a match, this short entry breaks the LFN run.
			lfn_count = 0;
		}
	}

	kfree(sector);
	return found;
}

// holy function signature batman....

bool fat32_add_dirent_chain(WDM_DriveHandle drive, const fat32_ebr_t* fat32, fat32_fsinfo_t* fsinfo, uint32_t parent_cluster, const char* filename, uint8_t attributes, uint32_t first_cluster, uint32_t file_size, fat_resolved_dirent_t* out_entry) {
	uint8_t raw11[11];
	bool is_83 = fat_pack_short_name_raw(filename, raw11);

	if (!is_83) {
		fat_dirent_list_t parent_listing;
		fat_dirent_list_init(&parent_listing);
		if (!fat32_list_directory(drive, fat32, parent_cluster, &parent_listing)) return false;
		bool ok = fat_generate_short_name(filename, &parent_listing, raw11);
		fat_dirent_list_free(&parent_listing);
		if (!ok) return false;
	}

	int lfn_count = is_83 ? 0 : (int) ((strlen(filename) + FAT_LFN_MAX_CHARS_PER_ENTRY - 1) / FAT_LFN_MAX_CHARS_PER_ENTRY);
	int slots_needed = lfn_count + 1;

	fat32_dir_writer_ctx_t ctx = { .drive = drive, .fat32 = fat32, .fsinfo = fsinfo, .start_cluster = parent_cluster };
	fat_dir_writer_t w;
	fat_dir_writer_init(&w, &fat32_dir_writer_ops, &ctx, fat32->bpb.bytes_per_sector);

	fat_dirent_slot_t run_start;
	if (!fat_dir_reserve_slot_run(&w, slots_needed, &run_start)) return false;

	uint16_t bps = fat32->bpb.bytes_per_sector;
	uint32_t entries_ps = bps / 32;

	// Pack & Write LFNs
	if (lfn_count > 0) {
		uint8_t lfn_buf[FAT_LFN_MAX_ENTRIES * 32];
		if (fat_pack_lfn_entries(filename, raw11, lfn_buf) != lfn_count) return false;

		for (int i = 0; i < lfn_count; i++) {
			fat_dirent_slot_t slot;
			uint32_t flat = run_start.sector_index * entries_ps + run_start.offset / 32 + (uint32_t) i;
			slot.sector_index = flat / entries_ps;
			slot.offset = (flat % entries_ps) * 32;
			if (!fat_dir_write_slot(&w, slot, lfn_buf + i * 32)) return false;
		}
	}

	// Pack & Write Short Entry
	fat_dirent_slot_t short_slot;
	uint32_t flat = run_start.sector_index * entries_ps + run_start.offset / 32 + (uint32_t) lfn_count;
	short_slot.sector_index = flat / entries_ps;
	short_slot.offset = (flat % entries_ps) * 32;

	uint8_t new_raw32[32];
	fat_pack_dirent_raw(raw11, attributes, first_cluster, file_size, new_raw32);
	if (!fat_dir_write_slot(&w, short_slot, new_raw32)) return false;

	if (out_entry) {
		memset(out_entry, 0, sizeof(*out_entry));
		fat_fill_dirent_raw(new_raw32, &out_entry->raw);
		fat_format_short_name(raw11, out_entry->short_name);
		if (!is_83) {
			strncpy(out_entry->long_name, filename, FAT_LFN_MAX_NAME_CHARS);
			out_entry->long_name[FAT_LFN_MAX_NAME_CHARS] = '\0';
		}
		out_entry->first_cluster = first_cluster;
	}

	return true;
}

bool fat32_patch_dirent(WDM_DriveHandle drive, const fat32_ebr_t* fat32, uint32_t parent_cluster, const uint8_t raw11[11], const uint8_t raw32[32]) {
	if (!drive || !fat32 || !raw11 || !raw32) return false;

	uint16_t bps = fat32->bpb.bytes_per_sector;
	uint8_t  spc = fat32->bpb.sectors_per_cluster;
	uint32_t cur = parent_cluster;
	uint8_t* sector_buf = (uint8_t*) kalloc(bps);
	if (!sector_buf) return false;

	bool found = false;

	while (!found && cur >= 2 && cur < FAT32_BAD_CLUSTER) {
		uint32_t base_lba = fat32_cluster_to_sector(fat32, cur);

		for (uint8_t s = 0; s < spc && !found; s++) {
			if (WDM_Read(drive, base_lba + s, 1, sector_buf, WDM_FLAG_NONE) != WDM_OK) goto done;

			uint32_t entries_per_sector = bps / 32;
			for (uint32_t e = 0; e < entries_per_sector; e++) {
				uint8_t* slot = sector_buf + e * 32;

				if (slot[0] == FAT_DIRENT_END_MARKER) goto done; /* End of dir marker. */
				if (slot[0] == FAT_DIRENT_FREE_MARKER) continue; /* Deleted entry */
				if ((slot[11] & FAT_ATTR_LFN) == FAT_ATTR_LFN) continue; /* LFN entry */

				/* Exactly compare raw bytes - do not perform E5 un-escaping translation here! */
				uint8_t cmp_name[11];
				memcpy(cmp_name, slot, 11);
				if (cmp_name[0] == FAT_DIRENT_ESCAPED_E5) cmp_name[0] = 0xE5;

				if (memcmp(cmp_name, raw11, 11) == 0) {
					// Overwrite the slot and flush the sector.
					memcpy(slot, raw32, 32);
					if (WDM_Write(drive, base_lba + s, 1, sector_buf, WDM_FLAG_NONE) != WDM_OK) goto done;
					found = true;
					break;
				}
			}
		}

		if (!found) cur = fat32_get_fat_entry(drive, fat32, cur);
	}

done:
	kfree(sector_buf);
	return found;
}
