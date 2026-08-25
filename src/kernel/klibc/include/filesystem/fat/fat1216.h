#ifndef WALLOS_FAT1216_H
#define WALLOS_FAT1216_H
#include <stdint.h>
#include <filesystem/fat/fat.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FAT12/16 Cluster Markers
 * Special cluster values used in the FAT.
 */
#define FAT12_FREE_CLUSTER 0x0000
#define FAT12_BAD_CLUSTER  0x0FF7
#define FAT12_EOC_MIN      0x0FF8
#define FAT12_EOC_MAX      0x0FFF

#define FAT16_FREE_CLUSTER 0x0000
#define FAT16_BAD_CLUSTER  0xFFF7
#define FAT16_EOC_MIN      0xFFF8
#define FAT16_EOC_MAX      0xFFFF


	/**
	 * @brief Represents the FAT12/16 Extended Boot Record (EBR) and base BPB.
	 */
	typedef struct {
		fat_bpb_t bpb;

		uint8_t drive_number;
		uint8_t windows_nt_flags; ///< This really isn't important, included for completeness
		uint8_t signature;        ///< Should be 0x28 or 0x29
		uint32_t volume_id;       ///< Serial number (can usually be ignored)
		uint8_t volume_label[12]; ///< 11 bytes with trailing null terminator
		uint8_t system_string_id[9]; ///< 8 bytes with trailing null terminator (spec says this should be ignored)
		// 448 bytes of "boot code" that we ignore.
		uint16_t partition_signature; ///< Final 2 bytes of the partition, should be 0xAA55
	} fat1216_ebr_t;

	/**
	 * @brief Parses a raw buffer into a FAT12/16 EBR structure.
	 * @param buf Raw sector buffer containing the boot sector.
	 * @param fat16 Pointer to the structure to populate.
	 */
	void fill_fat1216(const uint8_t* buf, fat1216_ebr_t* fat16);

	/**
	 * @brief Prints the parsed FAT12/16 EBR to standard output for debugging.
	 * @param fat Pointer to the parsed FAT12/16 structure.
	 */
	void print_fat1216(const fat1216_ebr_t* fat);

	/**
	 * @brief Calculates the sector offset of the root directory.
	 * @param fat Pointer to the FAT12/16 volume information.
	 * @return The absolute sector number where the root directory begins.
	 */
	inline uint32_t fat1216_root_dir_start_sector(const fat1216_ebr_t* fat) {
		return (uint32_t) fat->bpb.reserved_sectors + ((uint32_t) fat->bpb.fat_allcation_tables * (uint32_t) fat->bpb.sectors_per_fat);
	}

	/**
	 * @brief Calculates the number of sectors occupied by the root directory.
	 * @param fat Pointer to the FAT12/16 volume information.
	 * @return The number of sectors required to store all root directory entries.
	 */
	inline uint32_t fat1216_root_dir_sector_count(const fat1216_ebr_t* fat) {
		uint32_t root_bytes = (uint32_t) fat->bpb.root_dir_entries * 32;
		return (root_bytes + fat->bpb.bytes_per_sector - 1) / fat->bpb.bytes_per_sector;
	}

	/**
	 * @brief Calculates the sector offset of the first data cluster.
	 * @param fat Pointer to the FAT12/16 volume information.
	 * @return The absolute sector number where data clusters begin.
	 */
	inline uint32_t fat1216_first_data_sector(const fat1216_ebr_t* fat) {
		return fat1216_root_dir_start_sector(fat) + fat1216_root_dir_sector_count(fat);
	}

	/**
	 * @brief Converts a logical cluster number to a physical LBA sector number.
	 * @param fat Pointer to the FAT12/16 volume information.
	 * @param cluster The logical cluster number (must be >= 2).
	 * @return The physical sector number, or 0 if the cluster is invalid (< 2).
	 */
	inline uint32_t fat1216_cluster_to_sector(const fat1216_ebr_t* fat, uint32_t cluster) {
		// Clusters 0 and 1 are reserved
		if (cluster < 2) return 0;

		return fat1216_first_data_sector(fat) + ((cluster - 2) * fat->bpb.sectors_per_cluster);
	}

	/**
	 * @brief Converts a physical LBA sector number to a logical cluster number.
	 * @param fat Pointer to the FAT12/16 volume information.
	 * @param sector The physical sector number.
	 * @return The logical cluster number, or 0 if the sector is outside the data area (could be in root directory, reserved region, or FAT region).
	 */
	inline uint32_t fat1216_sector_to_cluster(const fat1216_ebr_t* fat, uint32_t sector) {
		uint32_t first_data_sector = fat1216_first_data_sector(fat);

		if (sector < first_data_sector) return 0;

		return ((sector - first_data_sector) / fat->bpb.sectors_per_cluster) + 2;
	}

	/**
	 * @brief Calculates the total size of a single cluster in bytes.
	 * @param fat Pointer to the FAT12/16 volume information.
	 * @return Size of a cluster in bytes.
	 */
	inline uint32_t fat1216_cluster_size_bytes(const fat1216_ebr_t* fat) {
		return fat->bpb.bytes_per_sector * fat->bpb.sectors_per_cluster;
	}

	/**
	 * @brief Checks if a cluster value represents an End of Chain (EOC) marker.
	 * @param type The FAT type (FAT12 or FAT16).
	 * @param entry The value read from the File Allocation Table.
	 * @retval true The value is an EOC marker for the given FAT type.
	 * @retval false The value is not an EOC marker.
	 */
	inline bool fat1216_is_eoc(fat_type_t type, uint32_t entry) {
		if (type == FAT_TYPE_FAT12) return entry >= FAT12_EOC_MIN && entry <= FAT12_EOC_MAX;
		if (type == FAT_TYPE_FAT16) return entry >= FAT16_EOC_MIN && entry <= FAT16_EOC_MAX;
		return true;
	}

	/**
	 * @brief Checks if a cluster value represents a bad cluster marker.
	 * @param type The FAT type (FAT12 or FAT16).
	 * @param entry The value read from the File Allocation Table.
	 * @retval true The value is a bad cluster marker for the given FAT type.
	 * @retval false The value is not a bad cluster marker.
	 */
	inline bool fat1216_is_bad_cluster(fat_type_t type, uint32_t entry) {
		if (type == FAT_TYPE_FAT12) return entry == FAT12_BAD_CLUSTER;
		if (type == FAT_TYPE_FAT16) return entry == FAT16_BAD_CLUSTER;
		return false;
	}

	/**
	 * @brief Gets the End of Chain marker value for the given FAT type.
	 * @param type The FAT type (FAT12 or FAT16).
	 * @return FAT12_EOC_MAX if type is FAT12, FAT16_EOC_MAX if type is FAT16.
	 */
	inline uint32_t fat1216_eoc_value(fat_type_t type) {
		return (type == FAT_TYPE_FAT12) ? FAT12_EOC_MAX : FAT16_EOC_MAX;
	}

	/**
	 * @brief Calculates the total number of sectors on the volume.
	 * @param fat Pointer to the FAT12/16 volume information.
	 * @return Total sector count, using the 16-bit field if non-zero, otherwise the 32-bit field.
	 */
	inline uint32_t fat1216_total_sector_count(const fat1216_ebr_t* fat) {
		return (fat->bpb.sectors != 0) ? (uint32_t) fat->bpb.sectors : fat->bpb.large_sector_count;
	}

	/**
	 * @brief Calculates the total number of clusters on the volume.
	 * @param fat Pointer to the FAT12/16 volume information.
	 * @return Total cluster count (including reserved clusters 0 and 1), or 0 if invalid.
	 */
	inline uint32_t fat1216_cluster_count(const fat1216_ebr_t* fat) {
		if (fat->bpb.sectors_per_cluster == 0) return 0;

		uint32_t total_sectors = fat1216_total_sector_count(fat);
		uint32_t first_data_sector = fat1216_first_data_sector(fat);
		if (total_sectors <= first_data_sector) return 0;

		uint32_t data_sectors = total_sectors - first_data_sector;
		return (data_sectors / fat->bpb.sectors_per_cluster) + 2;
	}

	/**
	 * @brief Retrieves the next cluster value from the File Allocation Table.
	 * Handles both FAT12 (12-bit entries with complex packing) and FAT16 (16-bit entries).
	 * @param drive Handle to the storage device.
	 * @param fat Pointer to the FAT12/16 volume information.
	 * @param type The FAT type (FAT12 or FAT16).
	 * @param cluster The current cluster number.
	 * @return FAT entry value The next cluster, EOC marker, bad cluster marker, or free marker.
	 * @retval FAT12_BAD_CLUSTER If cluster is invalid (< 2) or on I/O error for FAT12.
	 * @retval FAT16_BAD_CLUSTER If cluster is invalid (< 2) or on I/O error for FAT16.
	 */
	uint32_t fat1216_get_fat_entry(WDM_DriveHandle drive, const fat1216_ebr_t* fat, fat_type_t type, uint32_t cluster);

	/**
	 * @brief Lists all live entries in a directory's cluster chain.
	 * For the root directory (cluster 0), reads the fixed root directory region.
	 * For subdirectories, traverses the cluster chain. Skips deleted entries and LFN entries.
	 * @param drive Handle to the storage device.
	 * @param fat Pointer to the FAT12/16 volume information.
	 * @param type The FAT type (FAT12 or FAT16).
	 * @param dir_cluster The starting cluster of the directory (0 for root).
	 * @param out_list Pointer to a list structure that will be populated. Caller must free it.
	 * @retval true Success, even if directory is empty.
	 * @retval false I/O error, allocation failure, invalid FAT type, or invalid parameters.
	 */
	bool fat1216_list_directory(WDM_DriveHandle drive, const fat1216_ebr_t* fat, fat_type_t type, uint32_t dir_cluster, fat_dirent_list_t* out_list);

	/**
	 * @brief Recursively prints the directory tree of the volume starting from the root.
	 * @param drive Handle to the storage device.
	 * @param fat Pointer to the FAT12/16 volume information.
	 * @param type The FAT type (FAT12 or FAT16).
	 */
	void fat1216_tree(WDM_DriveHandle drive, const fat1216_ebr_t* fat, fat_type_t type);

	/**
	 * @brief Resolves an absolute or relative path to a directory entry.
	 * Paths with only the root component are rejected.
	 * Callers must handle root specially.
	 * @param drive Handle to the storage device.
	 * @param fat Pointer to the FAT12/16 volume information.
	 * @param type The FAT type (FAT12 or FAT16).
	 * @param path The path to resolve (forward-slash separated, leading slash optional).
	 * @param want_directory If true, resolution fails if the target is not a directory.
	 * @param out_entry Pointer to a structure to hold the resolved directory entry.
	 * @retval FAT_LOOKUP_OK Success, out_entry is populated with the resolved entry.
	 * @retval FAT_LOOKUP_NOT_FOUND The path does not exist.
	 * @retval FAT_LOOKUP_WRONG_TYPE The resolved entry's type (file/directory) does not match want_directory.
	 * @retval FAT_LOOKUP_BAD_PATH The path is invalid, empty, contains empty components, or is root-only.
	 * @retval FAT_LOOKUP_IO_ERROR I/O failure while reading directory entries.
	 */
	fat_lookup_status_t fat1216_resolve_path(WDM_DriveHandle drive, const fat1216_ebr_t* fat, fat_type_t type, const char* path, bool want_directory, fat_resolved_dirent_t* out_entry);

	/**
	 * @brief Finds a specific file by path.
	 * @param drive Handle to the storage device.
	 * @param fat Pointer to the FAT12/16 volume information.
	 * @param type The FAT type (FAT12 or FAT16).
	 * @param path The path to the file.
	 * @param out_entry Pointer to a structure to hold the resolved directory entry.
	 * @retval FAT_LOOKUP_OK Success, out_entry is populated.
	 * @retval FAT_LOOKUP_NOT_FOUND File path does not exist.
	 * @retval FAT_LOOKUP_WRONG_TYPE The target exists but is a directory, not a file.
	 * @retval FAT_LOOKUP_BAD_PATH The path is invalid or contains invalid path components.
	 * @retval FAT_LOOKUP_IO_ERROR I/O failure while reading directories.
	 */
	fat_lookup_status_t fat1216_find_file(WDM_DriveHandle drive, const fat1216_ebr_t* fat, fat_type_t type, const char* path, fat_resolved_dirent_t* out_entry);

	/**
	 * @brief Finds a specific directory by path.
	 * @param drive Handle to the storage device.
	 * @param fat Pointer to the FAT12/16 volume information.
	 * @param type The FAT type (FAT12 or FAT16).
	 * @param path The path to the directory.
	 * @param out_entry Pointer to a structure to hold the resolved directory entry.
	 * @retval FAT_LOOKUP_OK Success, out_entry is populated.
	 * @retval FAT_LOOKUP_NOT_FOUND Directory path does not exist.
	 * @retval FAT_LOOKUP_WRONG_TYPE The target exists but is a file, not a directory.
	 * @retval FAT_LOOKUP_BAD_PATH The path is invalid or contains invalid path components.
	 * @retval FAT_LOOKUP_IO_ERROR I/O failure while reading directories.
	 */
	fat_lookup_status_t fat1216_find_directory(WDM_DriveHandle drive, const fat1216_ebr_t* fat, fat_type_t type, const char* path, fat_resolved_dirent_t* out_entry);

	/**
	 * @brief Reads the full contents of a file into a dynamically allocated buffer.
	 * For zero-byte files, returns a valid non-NULL allocation with *out_size == 0.
	 * @param drive Handle to the storage device.
	 * @param fat Pointer to the FAT12/16 volume information.
	 * @param type The FAT type (FAT12 or FAT16).
	 * @param entry The resolved directory entry of the file to read (must be a regular file).
	 * @param out_size Pointer to an integer that will receive the exact file size on success.
	 * @retval non-NULL Malloc'd buffer containing the file data (exact size, not cluster-rounded).
	 *                  For zero-byte files, returns a valid non-NULL 1-byte allocation with *out_size == 0.
	 *                  Caller must free the returned buffer.
	 * @retval NULL Invalid parameters, I/O error, allocation failure, or cluster chain corruption.
	 */
	uint8_t* fat1216_read_file(WDM_DriveHandle drive, const fat1216_ebr_t* fat, fat_type_t type, const fat_resolved_dirent_t* entry, uint32_t* out_size);

#ifdef __cplusplus
}
#endif
#endif //WALLOS_FAT1216_H