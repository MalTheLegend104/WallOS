#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <filesystem/fat/fat.h>
#include <filesystem/fat/fat_internal.h>
#include <memory/kernel_alloc.h>


void fill_fat1216(const uint8_t* buf, fat1216_ebr_t* fat16) {
	// First, grab the base BPB
	fill_bpb(buf, &fat16->bpb);

	// FAT12/16 extended fields start at offset 36
	fat16->drive_number = buf[36];
	fat16->windows_nt_flags = buf[37];
	fat16->signature = buf[38];
	fat16->volume_id = fat_internal_read32(buf, 39);

	// Fixed widths on disk
	// Label is 11 bytes, Sys String is 8 bytes
	memcpy(fat16->volume_label, &buf[43], 11);
	fat16->volume_label[11] = '\0'; // Safeguard your 12th element null terminator

	memcpy(fat16->system_string_id, &buf[54], 8);
	fat16->system_string_id[8] = '\0'; // Safeguard your 9th element null terminator

	// Grab the magic signature from the very end of the 512-byte sector
	fat16->partition_signature = fat_internal_read16(buf, 510);
}

void print_fat1216(const fat1216_ebr_t* fat) {
	print_bpb(&fat->bpb);

	printf("\n=== FAT12/16 Extended Boot Record ===\n");
	printf("Drive Number         : 0x%02X\n", fat->drive_number);
	printf("NT Flags             : 0x%02X\n", fat->windows_nt_flags);
	printf("Signature            : 0x%02X\n", fat->signature);
	printf("Volume ID            : 0x%08X\n", fat->volume_id);
	printf("Volume Label         : %s\n", fat->volume_label);
	printf("System ID            : %s\n", fat->system_string_id);
	printf("Boot Signature       : 0x%04X\n", fat->partition_signature);
}

uint32_t fat1216_get_fat_entry(WDM_DriveHandle drive, const fat1216_ebr_t* fat, fat_type_t type, uint32_t cluster) {
	if (cluster < 2) {
		return (type == FAT_TYPE_FAT12) ? FAT12_BAD_CLUSTER : FAT16_BAD_CLUSTER;
	}

	uint32_t fat_start_sector = fat->bpb.reserved_sectors;
	uint16_t bps = fat->bpb.bytes_per_sector;

	if (type == FAT_TYPE_FAT16) {
		uint32_t byte_offset = cluster * 2;
		uint32_t sector = fat_start_sector + (byte_offset / bps);
		uint32_t offset_in_sector = byte_offset % bps;

		uint8_t buf[512];
		if (bps > sizeof(buf)) return FAT16_BAD_CLUSTER; // TODO: We assume the sector size here, this should probably be cleaned up.
		if (WDM_Read(drive, sector, 1, buf, WDM_FLAG_NONE) != WDM_OK) return FAT16_BAD_CLUSTER;

		return fat_internal_read16(buf, offset_in_sector);
	}

	if (type == FAT_TYPE_FAT12) {
		// floor(cluster * 1.5)
		uint32_t byte_offset = cluster + (cluster / 2);
		uint32_t sector = fat_start_sector + (byte_offset / bps);
		uint32_t offset_in_sector = byte_offset % bps;

		uint8_t buf[1024]; // TODO: Yet again, we assume the sector size
		if ((uint32_t) bps * 2 > sizeof(buf)) return FAT12_BAD_CLUSTER;

		// Always read the sector containing the first byte and the next sector
		// Simpler than branching on offset_in_sector == bps - 1
		if (WDM_Read(drive, sector, 1, buf, WDM_FLAG_NONE) != WDM_OK) return FAT12_BAD_CLUSTER;
		if (WDM_Read(drive, sector + 1, 1, buf + bps, WDM_FLAG_NONE) != WDM_OK) return FAT12_BAD_CLUSTER;

		uint16_t raw16 = fat_internal_read16(buf, offset_in_sector);

		// Even cluster: value is in the LOW 12 bits
		// Odd cluster: value is in the HIGH 12 bits (>>4)
		if (cluster & 1) {
			return (raw16 >> 4) & 0x0FFF;
		}

		return raw16 & 0x0FFF;
	}

	return FAT12_BAD_CLUSTER;
}

bool fat1216_list_directory(WDM_DriveHandle drive, const fat1216_ebr_t* fat, fat_type_t type, uint32_t dir_cluster, fat_dirent_list_t* out_list) {
	if (!drive || !fat || !out_list) return false;
	if (type != FAT_TYPE_FAT12 && type != FAT_TYPE_FAT16) return false;

	fat_dirent_list_init(out_list);

	uint16_t bytes_per_sector = fat->bpb.bytes_per_sector;
	uint8_t sectors_per_cluster = fat->bpb.sectors_per_cluster;
	if (bytes_per_sector == 0 || sectors_per_cluster == 0) return false;

	uint8_t* sector = kalloc(bytes_per_sector);
	if (!sector) return false;

	fat_lfn_run_t lfn_run;
	fat_lfn_run_reset(&lfn_run);
	bool end_of_directory = false;

	if (dir_cluster == 0) {
		// Root has a fixed sector range, no chain, and no EOC
		uint32_t start = fat1216_root_dir_start_sector(fat);
		uint32_t count = fat1216_root_dir_sector_count(fat);

		for (uint32_t s = 0; s < count && !end_of_directory; s++) {
			if (WDM_Read(drive, start + s, 1, sector, WDM_FLAG_NONE) != WDM_OK) {
				kfree(sector);
				fat_dirent_list_free(out_list);
				return false;
			}

			if (!fat_parse_dirent_sector(sector, bytes_per_sector, &lfn_run, out_list, &end_of_directory)) {
				kfree(sector);
				fat_dirent_list_free(out_list);
				return false;
			}
		}
	} else { // Subdirctory
		uint32_t cluster = dir_cluster;
		uint32_t clusters_walked = 0;
		const uint32_t MAX_CLUSTERS_WALKED = 0x0FFF8; // This is a VERY large ceiling. I don't remember the significance of this number either...

		while (!end_of_directory && cluster >= 2 && !fat1216_is_bad_cluster(type, cluster) && !fat1216_is_eoc(type, cluster)) {
			if (++clusters_walked > MAX_CLUSTERS_WALKED) break;

			uint32_t start_sector = fat1216_cluster_to_sector(fat, cluster);

			for (uint8_t s = 0; s < sectors_per_cluster && !end_of_directory; s++) {
				if (WDM_Read(drive, start_sector + s, 1, sector, WDM_FLAG_NONE) != WDM_OK) {
					kfree(sector);
					fat_dirent_list_free(out_list);
					return false;
				}

				if (!fat_parse_dirent_sector(sector, bytes_per_sector, &lfn_run, out_list, &end_of_directory)) {
					kfree(sector);
					fat_dirent_list_free(out_list);
					return false;
				}
			}

			if (!end_of_directory) {
				cluster = fat1216_get_fat_entry(drive, fat, type, cluster);
			}
		}
	}

	kfree(sector);
	return true;
}

fat_lookup_status_t fat1216_resolve_path(WDM_DriveHandle drive, const fat1216_ebr_t* fat, fat_type_t type, const char* path, bool want_directory, fat_resolved_dirent_t* out_entry) {
	if (!path || path[0] == '\0') return FAT_LOOKUP_BAD_PATH;

	char path_copy[1024];
	if (strlen(path) >= sizeof(path_copy)) return FAT_LOOKUP_BAD_PATH;
	strcpy(path_copy, path);

	uint32_t current_cluster = 0; // 0 == root
	bool have_entry = false;
	fat_resolved_dirent_t current_entry;
	memset(&current_entry, 0, sizeof(current_entry));

	char* cursor = path_copy;
	if (*cursor == '/') cursor++;

	if (*cursor == '\0') return FAT_LOOKUP_BAD_PATH; // only passed root. root requires special handling that I dont want to deal with

	while (*cursor != '\0') {
		char* slash = strchr(cursor, '/');
		bool is_last_component = (slash == NULL);
		if (slash) *slash = '\0';

		if (*cursor == '\0') return FAT_LOOKUP_BAD_PATH;

		fat_dirent_list_t listing;
		if (!fat1216_list_directory(drive, fat, type, current_cluster, &listing)) {
			return FAT_LOOKUP_IO_ERROR;
		}

		const fat_resolved_dirent_t* found = fat_dirent_list_find(&listing, cursor);
		if (!found) {
			fat_dirent_list_free(&listing);
			return FAT_LOOKUP_NOT_FOUND;
		}

		bool found_is_dir = (found->raw.attributes & FAT_ATTR_DIRECTORY) != 0;

		if (!is_last_component && !found_is_dir) {
			fat_dirent_list_free(&listing);
			return FAT_LOOKUP_WRONG_TYPE;
		}
		if (is_last_component && found_is_dir != want_directory) {
			fat_dirent_list_free(&listing);
			return FAT_LOOKUP_WRONG_TYPE;
		}

		current_entry = *found;
		have_entry = true;

		// TODO: need to guard against a corrupt dir where first_cluster = 0
		current_cluster = found->first_cluster;

		fat_dirent_list_free(&listing);

		if (is_last_component) break;
		cursor = slash + 1;
	}

	if (!have_entry) return FAT_LOOKUP_NOT_FOUND;

	*out_entry = current_entry;
	return FAT_LOOKUP_OK;
}

fat_lookup_status_t fat1216_find_file(WDM_DriveHandle drive, const fat1216_ebr_t* fat, fat_type_t type, const char* path, fat_resolved_dirent_t* out_entry) {
	return fat1216_resolve_path(drive, fat, type, path, /*want_directory=*/false, out_entry);
}

fat_lookup_status_t fat1216_find_directory(WDM_DriveHandle drive, const fat1216_ebr_t* fat, fat_type_t type, const char* path, fat_resolved_dirent_t* out_entry) {
	return fat1216_resolve_path(drive, fat, type, path, /*want_directory=*/true, out_entry);
}

uint8_t* fat1216_read_file(WDM_DriveHandle drive, const fat1216_ebr_t* fat, fat_type_t type, const fat_resolved_dirent_t* entry, uint32_t* out_size) {
	if (!drive || !fat || !entry || !out_size) return NULL;
	if (entry->raw.attributes & FAT_ATTR_DIRECTORY) return NULL;

	*out_size = 0;

	if (entry->raw.file_size == 0) {
		uint8_t* empty = kalloc(1);
		return empty;
	}

	if (entry->first_cluster < 2) return NULL;

	uint16_t bytes_per_sector = fat->bpb.bytes_per_sector;
	uint8_t sectors_per_cluster = fat->bpb.sectors_per_cluster;
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
	const uint32_t MAX_CLUSTERS_WALKED = 0x0FFF8;

	while (bytes_written < entry->raw.file_size && cluster >= 2 && !fat1216_is_bad_cluster(type, cluster) && !fat1216_is_eoc(type, cluster)) {
		if (++clusters_walked > MAX_CLUSTERS_WALKED) break;

		uint32_t start_sector = fat1216_cluster_to_sector(fat, cluster);

		for (uint8_t s = 0; s < sectors_per_cluster && bytes_written < entry->raw.file_size; s++) {
			if (WDM_Read(drive, start_sector + s, 1, sector_buf, WDM_FLAG_NONE) != WDM_OK) {
				kfree(sector_buf);
				kfree(out_buf);
				return NULL;
			}

			uint32_t remaining = entry->raw.file_size - bytes_written;
			uint32_t to_copy = (remaining < bytes_per_sector) ? remaining : bytes_per_sector;

			memcpy(out_buf + bytes_written, sector_buf, to_copy);
			bytes_written += to_copy;
		}

		if (bytes_written >= entry->raw.file_size) break;

		cluster = fat1216_get_fat_entry(drive, fat, type, cluster);
	}

	kfree(sector_buf);

	if (bytes_written != entry->raw.file_size) {
		kfree(out_buf);
		return NULL;
	}

	*out_size = entry->raw.file_size;
	return out_buf;
}



static void fat1216_tree_recurse(WDM_DriveHandle drive, const fat1216_ebr_t* fat, fat_type_t type, uint32_t dir_cluster, int depth, const char* prefix) {
	if (depth > FAT1216_TREE_MAX_DEPTH) {
		printf("%s[max depth reached, stopping]\n", prefix);
		return;
	}

	fat_dirent_list_t listing;
	if (!fat1216_list_directory(drive, fat, type, dir_cluster, &listing)) {
		printf("%s[failed to read directory]\n", prefix);
		return;
	}

	size_t visible_count = 0;
	for (size_t i = 0; i < listing.count; i++) {
		if (!fat_is_dot_entry(&listing.entries[i])) visible_count++;
	}

	size_t visible_index = 0;
	for (size_t i = 0; i < listing.count; i++) {
		const fat_resolved_dirent_t* e = &listing.entries[i];
		if (fat_is_dot_entry(e)) continue;

		bool is_last = (++visible_index == visible_count);
		const char* branch = is_last ? "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 " : "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80 ";

		const char* display_name = (e->long_name[0] != '\0') ? e->long_name : e->short_name;
		bool is_dir = (e->raw.attributes & FAT_ATTR_DIRECTORY) != 0;

		if (is_dir) {
			printf("%s%s%s/\n", prefix, branch, display_name);
		} else {
			printf("%s%s%s (%u bytes)\n", prefix, branch, display_name, e->raw.file_size);
		}

		if (is_dir) {
			if (e->first_cluster < 2) {
				char child_prefix[1024];
				snprintf(child_prefix, sizeof(child_prefix), "%s%s", prefix, is_last ? "    " : "\xE2\x94\x82   ");
				printf("%s[invalid cluster %u, skipping]\n", child_prefix, e->first_cluster);
				continue;
			}

			char child_prefix[1024];
			snprintf(child_prefix, sizeof(child_prefix), "%s%s", prefix, is_last ? "    " : "\xE2\x94\x82   ");

			fat1216_tree_recurse(drive, fat, type, e->first_cluster, depth + 1, child_prefix);
		}
	}

	fat_dirent_list_free(&listing);
}

void fat1216_tree(WDM_DriveHandle drive, const fat1216_ebr_t* fat, fat_type_t type) {
	printf("/\n");
	fat1216_tree_recurse(drive, fat, type, 0, 1, ""); // 0 == root
}