#ifndef WALLOS_FAT32_H
#define WALLOS_FAT32_H

#include <stdbool.h>
#include <stdint.h>
#include <filesystem/fat/fat.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @name FAT32 Cluster Markers
 * Special cluster values used in the FAT.
 * @{
 */
#define FAT32_FREE_CLUSTER      0x00000000
#define FAT32_BAD_CLUSTER       0x0FFFFFF7
#define FAT32_EOC_MIN           0x0FFFFFF8
#define FAT32_EOC_MAX           0x0FFFFFFF
/** @} */

/**
 * @brief Represents the FAT32 Extended Boot Record (EBR) and base BPB.
 */
    typedef struct {
        fat_bpb_t bpb;

        uint32_t sectors_per_fat;
        uint16_t flags;
        uint16_t fat_version;           ///< High byte is major, low byte is minor
        uint32_t root_cluster_number;
        uint16_t fsinfo_sector;
        uint16_t backup_boot_sector;
        uint8_t drive_number;
        uint8_t windows_nt_flags;
        uint8_t signature;
        uint32_t volume_id;
        uint8_t volume_label[12];
        uint8_t system_string_id[9];
        uint16_t partition_signature;
    } fat32_ebr_t;

    /**
     * @brief Represents the FAT32 FSInfo sector structure.
     * Used to optimize free space calculation and cluster allocation.
     */
    typedef struct {
        uint32_t signature;               ///< Should be 0x41615252
        // 480 unused bytes....
        uint32_t signature2;              ///< Should be 0x61417272
        uint32_t last_known_free_cluster; ///< 0xFFFFFFFF indicates count is unknown, should be range checked
        uint32_t search_cluster_num;      ///< Where the driver should start looking for free clusters. 0xFFFFFFFF means start at 2
        uint32_t trail_signature;         ///< 0xAA550000
    } fat32_fsinfo_t;

    /**
     * @brief Parses a raw buffer into a FAT32 EBR structure.
     * @param buf Raw sector buffer containing the boot sector.
     * @param fat32 Pointer to the structure to populate.
     */
    void fill_fat32(const uint8_t* buf, fat32_ebr_t* fat32);

    /**
     * @brief Parses a raw buffer into a FAT32 FSInfo structure.
     * @param buf Raw sector buffer containing the FSInfo sector.
     * @param fs_info Pointer to the structure to populate.
     */
    void fill_fat32_fsinfo(const uint8_t* buf, fat32_fsinfo_t* fs_info);

    /**
     * @brief Prints the parsed FAT32 EBR to standard output for debugging.
     * @param fat Pointer to the parsed FAT32 structure.
     */
    void print_fat32(const fat32_ebr_t* fat);

    /**
     * @brief Prints the parsed FSInfo structure to standard output for debugging.
     * @param fs Pointer to the parsed FSInfo structure.
     */
    void print_fsinfo(const fat32_fsinfo_t* fs);

    /**
     * @brief Converts a logical cluster number to a physical LBA sector number.
     * @param fat32 Pointer to the FAT32 volume information.
     * @param cluster The logical cluster number (must be >= 2).
     * @return The physical sector number, or 0 if the cluster is invalid.
     */
    static inline uint32_t fat32_cluster_to_sector(const fat32_ebr_t* fat32, uint32_t cluster) {
        if (cluster < 2) return 0; // Clusters 0 and 1 are reserved

        uint32_t first_data_sector = fat32->bpb.reserved_sectors + (fat32->bpb.fat_allcation_tables * fat32->sectors_per_fat);
        return first_data_sector + ((cluster - 2) * fat32->bpb.sectors_per_cluster);
    }

    /**
     * @brief Converts a physical LBA sector number to a logical cluster number.
     * @param fat32 Pointer to the FAT32 volume information.
     * @param sector The physical sector number.
     * @return The logical cluster number, or 0 if the sector is outside the data area.
     */
    static inline uint32_t fat32_sector_to_cluster(const fat32_ebr_t* fat32, uint32_t sector) {
        uint32_t first_data_sector = fat32->bpb.reserved_sectors + (fat32->bpb.fat_allcation_tables * fat32->sectors_per_fat);

        if (sector < first_data_sector) return 0; // Not in data area
        return ((sector - first_data_sector) / fat32->bpb.sectors_per_cluster) + 2;
    }

    /**
     * @brief Calculates the total size of a single cluster in bytes.
     * @param fat32 Pointer to the FAT32 volume information.
     * @return Size of a cluster in bytes.
     */
    static inline uint32_t fat32_cluster_size_bytes(const fat32_ebr_t* fat32) {
        return fat32->bpb.bytes_per_sector * fat32->bpb.sectors_per_cluster;
    }

    /**
     * @brief Checks if a cluster value represents an End of Chain (EOC) marker.
     * @param value The value read from the File Allocation Table.
     * @return True if the value is an EOC marker, false otherwise.
     */
    static inline bool fat32_is_eoc(const uint32_t value) {
        return value >= 0x0FFFFFF8;
    }

    /**
     * @brief Retrieves the next cluster value from the File Allocation Table.
     * @param drive Handle to the storage device.
     * @param fat32 Pointer to the FAT32 volume information.
     * @param cluster The current cluster number.
     * @retval FAT entry The lower 28 bits of the FAT entry (next cluster, EOC, bad cluster, or free).
     * @retval 0 If cluster is invalid (< 2 or >= BAD_CLUSTER).
     */
    uint32_t fat32_get_fat_entry(WDM_DriveHandle drive, const fat32_ebr_t* fat32, uint32_t cluster);

    /**
     * @brief Lists all live entries in a directory's cluster chain.
     * Skips deleted entries and LFN entries. The scan stops upon encountering an end marker (0x00).
     * @param drive Handle to the storage device.
     * @param fat32 Pointer to the FAT32 volume information.
     * @param dir_cluster The starting cluster of the directory to list.
     * @param out_list Pointer to a list structure that will be populated. Caller must free it.
     * @retval true Success, even if directory is empty.
     * @retval false I/O error, allocation failure, or invalid parameters.
     */
    bool fat32_list_directory(WDM_DriveHandle drive, const fat32_ebr_t* fat32, uint32_t dir_cluster, fat_dirent_list_t* out_list);

    /**
     * @brief Recursively prints the directory tree of the volume starting from the root.
     * @param drive Handle to the storage device.
     * @param fat32 Pointer to the FAT32 volume information.
     */
    void fat32_tree(WDM_DriveHandle drive, const fat32_ebr_t* fat32);

    /**
     * @brief Resolves an absolute or relative path to a directory entry.
     * @param drive Handle to the storage device.
     * @param fat32 Pointer to the FAT32 volume information.
     * @param path The path to resolve (forward-slash separated, leading slash optional).
     * @param want_directory If true, resolution fails if the target is not a directory.
     * @param out_entry Pointer to a structure to hold the resolved directory entry.
     * @retval FAT_LOOKUP_OK Success, out_entry is populated with the resolved entry.
     * @retval FAT_LOOKUP_NOT_FOUND The path does not exist.
     * @retval FAT_LOOKUP_WRONG_TYPE The resolved entry's type (file/directory) does not match want_directory.
     * @retval FAT_LOOKUP_BAD_PATH The path is invalid, empty, or contains invalid path components.
     * @retval FAT_LOOKUP_IO_ERROR I/O failure while reading directory entries.
     */
    fat_lookup_status_t fat32_resolve_path(WDM_DriveHandle drive, const fat32_ebr_t* fat32, const char* path, bool want_directory, fat_resolved_dirent_t* out_entry);

    /**
     * @brief Finds a specific file by path.
     * @param drive Handle to the storage device.
     * @param fat32 Pointer to the FAT32 volume information.
     * @param path The path to the file.
     * @param out_entry Pointer to a structure to hold the resolved directory entry.
     * @retval FAT_LOOKUP_OK Success, out_entry is populated.
     * @retval FAT_LOOKUP_NOT_FOUND File path does not exist.
     * @retval FAT_LOOKUP_WRONG_TYPE The target exists but is a directory, not a file.
     * @retval FAT_LOOKUP_BAD_PATH The path is invalid or invalid path components.
     * @retval FAT_LOOKUP_IO_ERROR I/O failure while reading directories.
     */
    fat_lookup_status_t fat32_find_file(WDM_DriveHandle drive, const fat32_ebr_t* fat32, const char* path, fat_resolved_dirent_t* out_entry);

    /**
     * @brief Finds a specific directory by path.
     * @param drive Handle to the storage device.
     * @param fat32 Pointer to the FAT32 volume information.
     * @param path The path to the directory.
     * @param out_entry Pointer to a structure to hold the resolved directory entry.
     * @retval FAT_LOOKUP_OK Success, out_entry is populated.
     * @retval FAT_LOOKUP_NOT_FOUND Directory path does not exist.
     * @retval FAT_LOOKUP_WRONG_TYPE The target exists but is a file, not a directory.
     * @retval FAT_LOOKUP_BAD_PATH The path is invalid or contains invalid path components.
     * @retval FAT_LOOKUP_IO_ERROR I/O failure while reading directories.
     */
    fat_lookup_status_t fat32_find_directory(WDM_DriveHandle drive, const fat32_ebr_t* fat32, const char* path, fat_resolved_dirent_t* out_entry);

    /**
     * @brief Reads the full contents of a file into a dynamically allocated buffer.
     * For zero-byte files, returns a valid non-NULL allocation with *out_size == 0.
     * @param drive Handle to the storage device.
     * @param fat32 Pointer to the FAT32 volume information.
     * @param entry The resolved directory entry of the file to read (must be a regular file).
     * @param out_size Pointer to an integer that will receive the exact file size on success.
     * @retval non-NULL Malloc'd buffer containing the file data (exact size, not cluster-rounded).
     *                  For zero-byte files, returns a valid non-NULL 1-byte allocation with *out_size == 0.
     *                  Caller must free the returned buffer.
     * @retval NULL Invalid parameters, I/O error, allocation failure, or cluster chain corruption.
     */
    uint8_t* fat32_read_file(WDM_DriveHandle drive, const fat32_ebr_t* fat32, const fat_resolved_dirent_t* entry, uint32_t* out_size);

    /**
     * @brief Writes a value to a specific cluster entry in the File Allocation Table.
     * Only updates the lower 28 bits of the entry, preserving the top 4 reserved bits.
     * Mirrors the write across all redundant FAT copies. Rejects reserved clusters 0 and 1.
     * @param drive Handle to the storage device.
     * @param fat32 Pointer to the FAT32 volume information.
     * @param cluster The cluster index to update (must be >= 2).
     * @param value The value to write (next cluster, EOC, free, etc.).
     * @retval true Entry was written to at least one FAT copy.
     * @retval false Invalid cluster (< 2), I/O error, or invalid parameters.
     */
    bool fat32_set_fat_entry(WDM_DriveHandle drive, const fat32_ebr_t* fat32, uint32_t cluster, uint32_t value);

    /**
     * @brief Scans the FAT for an available free cluster.
     * Performs a linear scan wrapping once, looking for an entry marked as FAT32_FREE_CLUSTER.
     * @param drive Handle to the storage device.
     * @param fat32 Pointer to the FAT32 volume information.
     * @param start_hint The cluster number to start searching from (clamped to >= 2).
     * @param out_cluster Pointer to store the found free cluster number.
     * @retval true Free cluster found, out_cluster is populated.
     * @retval false Volume is full, I/O error, or invalid parameters.
     */
    bool fat32_find_free_cluster(WDM_DriveHandle drive, const fat32_ebr_t* fat32, uint32_t start_hint, uint32_t* out_cluster);

    /**
     * @brief Traverses a cluster chain and marks every cluster as free.
     * If `fsinfo` is provided and valid, updates the volume's free cluster count to reflect the newly freed space.
     * Does NOT modify the directory entry that pointed to this chain, the caller must handle that separately.
     * @param drive Handle to the storage device.
     * @param fat32 Pointer to the FAT32 volume information.
     * @param fsinfo Pointer to the FSInfo sector structure to update (can be NULL to skip update).
     * @param start_cluster The first cluster of the chain to free.
     * @retval true Success, or start_cluster < 2 (no-op case is a success).
     * @retval false I/O error, chain corruption, or invalid parameters.
     */
    bool fat32_free_cluster_chain(WDM_DriveHandle drive, const fat32_ebr_t* fat32, fat32_fsinfo_t* fsinfo, uint32_t start_cluster);

    /**
     * @brief Writes raw data to a cluster chain, modifying allocations as necessary.
     * Reuses the existing cluster chain as much as possible.
     * If the new data is larger, the chain is extended by allocating new clusters.
     * If smaller, unused tail clusters are freed.
     * The final cluster is zero-padded.
     * If `fsinfo` is provided, the free cluster count is actively synchronized.
     * @param drive Handle to the storage device.
     * @param fat32 Pointer to the FAT32 volume information.
     * @param fsinfo Pointer to the FSInfo sector structure to update (can be NULL to skip update).
     * @param existing_first_cluster The starting cluster of an existing file (0 or <2 for a new file).
     * @param data Buffer containing the data to write.
     * @param size The number of bytes to write (if 0, frees the entire existing chain).
     * @param out_first_cluster Pointer to store the resulting starting cluster of the file.
     * @retval true Success. out_first_cluster is populated.
     * @retval false Failure. The chain may be partially written but is guaranteed to be EOC-terminated to maintain consistency.
     */
    bool fat32_write_file_data(WDM_DriveHandle drive, const fat32_ebr_t* fat32, fat32_fsinfo_t* fsinfo, uint32_t existing_first_cluster, const uint8_t* data, uint32_t size, uint32_t* out_first_cluster);

    /**
     * @brief High-level entry point to write data to a file by its path.
     * Creates the file if it does not exist, or overwrites it in-place if it does.
     * All intermediate directories in the path must exist. Only 8.3 short names are supported, paths requiring Long File Names (LFN) are rejected.
     * @param drive Handle to the storage device.
     * @param fat32 Pointer to the FAT32 volume information.
     * @param fsinfo Pointer to the FSInfo sector structure to update (can be NULL to skip update).
     * @param path The full path to the file (leading slash optional, forward-slash separated).
     * @param data Buffer containing the data to write.
     * @param size The number of bytes to write.
     * @param out_entry Optional pointer to receive the resulting directory entry.
     * @retval FAT_LOOKUP_OK Success, file created or overwritten.
     * @retval FAT_LOOKUP_BAD_PATH Path is invalid, empty, or requires LFN entries.
     * @retval FAT_LOOKUP_NOT_FOUND Parent directory does not exist.
     * @retval FAT_LOOKUP_WRONG_TYPE Trying to overwrite a directory as a file.
     * @retval FAT_LOOKUP_IO_ERROR Cluster allocation failure or I/O error.
     */
    fat_lookup_status_t fat32_write_file(WDM_DriveHandle drive, fat32_ebr_t* fat32, fat32_fsinfo_t* fsinfo, const char* path, const uint8_t* data, uint32_t size, fat_resolved_dirent_t* out_entry);

    /**
     * @brief Allocates and initializes a new directory cluster on disk.
     *
     * The caller is responsible for writing a directory dirent that points to *out_cluster into the parent directory (e.g. via fat32_write_dir_entry).
     *
     * @param drive          WDM handle for the block device.
     * @param fat32          Parsed FAT32 volume information.
     * @param fsinfo         FSInfo structure to update (may be NULL).
     * @param parent_cluster Starting cluster of the parent directory.
     *                       If this equals fat32->root_cluster_number, the '..' entry's first_cluster is written as 0.
     * @param out_cluster    Receives the allocated cluster number on success.
     * @retval true  Cluster allocated, zeroed, and dot entries written.
     * @retval false Volume full, I/O error, or invalid parameters.
     */
    bool fat32_write_directory(WDM_DriveHandle drive, const fat32_ebr_t* fat32, fat32_fsinfo_t* fsinfo, uint32_t parent_cluster, uint32_t* out_cluster);

    /**
     * @brief Write a pre-packed 32-byte directory entry into a parent directory.
     *
     * LFN entries are stepped over but never modified. This function only touches short-name (8.3) slots.
     *
     * @param drive          WDM handle for the block device.
     * @param fat32          Parsed FAT32 volume information.
     * @param fsinfo         FSInfo to update when a cluster extension occurs (may be NULL).
     * @param parent_cluster Starting cluster of the directory to write into.
     * @param raw11          Raw 11-byte short name used to match an existing slot.
     * @param raw32          Fully-formed 32-byte directory entry to write.
     * @retval true  Entry written successfully.
     * @retval false I/O error, allocation failure, or invalid parameters.
     */
    bool fat32_write_dir_entry(WDM_DriveHandle drive, const fat32_ebr_t* fat32, fat32_fsinfo_t* fsinfo, uint32_t parent_cluster, const uint8_t raw11[11], const uint8_t raw32[32]);

    bool fat32_unlink_entry(WDM_DriveHandle drive, const fat32_ebr_t* fat32, uint32_t parent_cluster, const uint8_t target_raw11[11]);

    bool fat32_add_dirent_chain(WDM_DriveHandle drive, const fat32_ebr_t* fat32, fat32_fsinfo_t* fsinfo, uint32_t parent_cluster, const char* filename, uint8_t attributes, uint32_t first_cluster, uint32_t file_size, fat_resolved_dirent_t* out_entry);


    /**
     * @brief Patch an existing 32-byte directory entry on disk in place.
     *
     * Scans the cluster chain of @p parent_cluster looking for a slot whose raw 11-byte short name matches @p raw11.
     * When found, overwrites the entire 32-byte slot with @p raw32 and writes the sector back.
     *
     * Used by the VFS write path to update file size after an eager write, and by remove_dir to mark an entry deleted (set raw32[0] = 0xE5).
     *
     * @param drive          WDM handle for the block device.
     * @param fat32          Parsed FAT32 volume information.
     * @param parent_cluster Starting cluster of the parent directory to search.
     * @param raw11          Raw 11-byte short name of the entry to find.
     * @param raw32          New 32-byte content to write into the matching slot.
     * @retval true  Entry found and sector updated successfully.
     * @retval false Entry not found, or I/O error.
     */
    bool fat32_patch_dirent(WDM_DriveHandle drive, const fat32_ebr_t* fat32, uint32_t parent_cluster, const uint8_t raw11[11], const uint8_t raw32[32]);
#ifdef __cplusplus
}
#endif

#endif //WALLOS_FAT32_H