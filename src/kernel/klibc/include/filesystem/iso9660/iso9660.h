#ifndef WALLOS_ISO9660_H
#define WALLOS_ISO9660_H
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Structures
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

#include <stdint.h>
#include <stdbool.h>
#include <filesystem/wdm.h>
#include <filesystem/vfs.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ISO 9660 sector size is always 2048 bytes. */
#define ISO_SECTOR_SIZE 2048u
/* LBA of the first Volume Descriptor Set (always 16 for ISO 9660). */
#define ISO_PVD_LBA 16u

	typedef enum {
		ISO9660_VOLUME_TYPE_BOOT_RECORD = 0,
		ISO9660_VOLUME_TYPE_PRIMARY = 1,
		ISO9660_VOLUME_TYPE_SUPPLEMENTARY = 2,
		ISO9660_VOLUME_TYPE_ENHANCED = 2,
		ISO9660_VOLUME_TYPE_TERMINATOR = 255
	} iso9660_descriptors;

	typedef struct {
		uint8_t type;
		uint8_t identifier[6]; // should always be "CD001"
		uint8_t version;
		uint8_t* data;
	} iso9660_sector_header_t;

	typedef struct {
		uint16_t year;        // 1-9999
		uint8_t  month;       // 1-12
		uint8_t  day;         // 1-31
		uint8_t  hour;        // 0-23
		uint8_t  minute;      // 0-59
		uint8_t  second;      // 0-59
		uint8_t  hundredths;  // 0-99

		int8_t   tz_offset;   // signed: -48 .. +52 (15-min intervals)
	} iso9660_datetime_t;

	// This is a different descriptor to the PVD datetime...
	// Presumably to preserve space in the directory record.
	typedef struct {
		uint8_t years;  // Since 1900
		uint8_t month;  // 1-12
		uint8_t day;    // 1-31
		uint8_t hour;   // 0-23
		uint8_t minute; // 0-59
		uint8_t second; // 0-59

		uint8_t tz_offset; // Offset in 15min intervals, -48 to 52
	} iso9660_directory_datetime_t;

	typedef struct {
		uint8_t hidden : 1;       // If set, the existence of this file need not be made known to the user.
		uint8_t directory : 1;    // If set, this record describes a directory.
		uint8_t associated : 1;   // If set, this file is an "Associated File".
		uint8_t record : 1;       // The extended attribute record contains information about the format of this file.
		uint8_t protection : 1;   // If set, the extended attribute record contains information about the format of this file.
		uint8_t reserved : 2;     // Bits 5 & 6 are reserved.
		uint8_t multi_extent : 1; // If set, this is not the final directory record for this file.
	} __attribute__((packed)) iso9660_file_flags_t;

	typedef struct {
		uint8_t length;
		uint8_t extended_attribute_record_length;
		uint32_t location_of_extent;
		uint32_t data_length;
		iso9660_directory_datetime_t recording_time;
		iso9660_file_flags_t file_flags;
		uint8_t file_unit_size;
		uint8_t interleave_gap_size;
		uint16_t volume_seq_num;
		uint8_t file_id_len;
		char* file_id; // Heap-allocated; caller must free.
	} iso9660_directory_record_t;


	typedef struct {
		char system_id[33];
		char volume_id[33];
		uint32_t volume_space_size;
		uint16_t volume_set_size;
		uint16_t volume_seq_num;
		uint16_t logical_block_size;
		uint32_t path_table_size;
		uint32_t type_l_path_table;
		uint32_t optional_type_l_path_table;
		uint32_t type_m_path_table;
		uint32_t optional_type_m_path_table;
		iso9660_directory_record_t dir_record;
		char volume_set_id[129];
		char publisher_id[129];
		bool publisher_id_filename;
		char data_preparer_id[129];
		bool data_preparer_id_filename;
		char application_id[129];
		bool application_id_filename;
		char copyright_file_id[38];
		char abstract_file_id[38];
		char bibliographic_id[38];
		iso9660_datetime_t creation_time;
		bool creation_time_is_unspecified;
		iso9660_datetime_t modification_time;
		bool modification_time_is_unspecified;
		iso9660_datetime_t expiration_time;
		bool expiration_time_is_unspecified;
		iso9660_datetime_t effective_time;
		bool effective_time_is_unspecified;
		uint8_t file_structure_ver;
		uint8_t offset_882_reserved;
	} iso9660_primary_volume_descriptor_t;

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Driver context
	//
	// Holds all state needed to operate on one mounted ISO 9660 volume.
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/** Maximum number of simultaneously open file/directory descriptors within one iso9660 context. */
#define ISO9660_MAX_OPEN 32

	typedef struct {
		WDM_DriveHandle drive;       /**< Underlying block device.                        */
		uint32_t        sector_size; /**< Bytes per sector (always 2048 for ISO 9660).    */
		uint32_t        root_lba;    /**< LBA of the root directory extent.               */
		uint32_t        root_size;   /**< Byte size of the root directory extent.         */
	} iso9660_ctx_t;

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Helpers
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

#define ISO_STR_READ(dest, buf, offset, len) _iso9660_read_iso9660_string(dest, buf, offset, len)
#define ISO_PRINT_STR(label, name) printf("\t%s: %s (%zu)\n", label, name, strlen(name))
#define ISO_PRINT_STR_FILENAME(label, name, filename) printf("\t%s: %s (%zu)(filename: %s)\n", label, name, strlen(name), filename ? "true" : "false")

#define LE16(buf, off) ((uint16_t)(buf)[off] | ((uint16_t)(buf)[off+1] << 8))
#define LE32(buf, off) ((uint32_t)(buf)[off] | ((uint32_t)(buf)[off+1] << 8) | ((uint32_t)(buf)[off+2] << 16) | ((uint32_t)(buf)[off+3] << 24))
#define BE32(buf, off) (((uint32_t)(buf)[off] << 24) | ((uint32_t)(buf)[off+1] << 16) |  ((uint32_t)(buf)[off+2] << 8) | (uint32_t)(buf)[off+3])

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Printing
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	void iso9660_print_iso_datetime(const char* label, iso9660_datetime_t dt);
	void iso9660_print_directory_record(const iso9660_directory_record_t* record);
	void iso9660_print_primary_volume_descriptor(const iso9660_primary_volume_descriptor_t* pvd);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Lifecycle
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/**
	 * @brief Initialize an iso9660 context against an already-registered WDM drive.
	 *
	 * Reads the Primary Volume Descriptor from the drive and populates @p ctx.
	 * Does NOT call WDM_Register(); the caller must do that first.
	 *
	 * @param ctx   Context to populate.  Must remain valid for the lifetime of the mount.
	 * @param drive WDM handle for the underlying CD-ROM / image device.
	 *
	 * @return true on success, false if the PVD cannot be found or the signature is wrong.
	 */
	bool iso9660_init(iso9660_ctx_t* ctx, WDM_DriveHandle drive);

	/**
	 * @brief Release any resources held inside @p ctx.
	 *
	 * Does NOT call WDM_Unregister(), the caller must do that afterwards.
	 */
	void iso9660_destroy(iso9660_ctx_t* ctx);


	/**
	 * Checks if the given drive is this filesystem.
	 * I wanted this separate from the VFS probe() in case I wanted to use it elsewhere.
	 */
	bool is_iso9660(WDM_DriveHandle drive);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// Main parsing functions
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/**
	 * @brief Reads the sector header into an already-allocated buffer.
	 *
	 * @param ctx    Initialised driver context.
	 * @param lba    Sector to read.
	 * @param buffer Caller-supplied buffer of at least ctx->sector_size bytes.
	 * @return Heap-allocated header (caller must free), or NULL on I/O error.
	 */
	iso9660_sector_header_t* iso9660_read_sector_header(iso9660_ctx_t* ctx, uint32_t lba, uint8_t* buffer);

	/**
	 * @brief Read the directory record from the dir_record_ptr into the provided struct.
	 *
	 * @param record        Struct to hold the data. The file_id field must be freed by the caller.
	 * @param dir_record_ptr Pointer to the start of the directory record.
	 */
	void iso9660_read_directory_record(iso9660_directory_record_t* record, uint8_t* dir_record_ptr);

	/**
	 * @brief Read the PVD. You must manually free the pvd->dir_record.file_id string.
	 */
	void iso9660_read_primary_volume_descriptor(iso9660_sector_header_t* header, uint8_t* buf, iso9660_primary_volume_descriptor_t* pvd);

	/**
	 * @brief Lists all directories and files in the filesystem, recursively.
	 *
	 * @param ctx   Initialised driver context.
	 */
	void iso9660_traverse(iso9660_ctx_t* ctx, uint32_t lba, uint32_t total_size, int depth);

	/**
	 * @brief Load a raw binary blob into memory.
	 *
	 * @param ctx  Initialised driver context.
	 * @param path Path to the file, starting at root '/'.
	 * @return Malloc'd pointer to the binary blob (null-terminated). Caller must free.
	 */
	uint8_t* iso9660_read_binary(iso9660_ctx_t* ctx, const char* path);

	/**
	 * @brief Finds a file or directory by its absolute path.
	 *
	 * @param ctx        Initialised driver context.
	 * @param path       Absolute path to look up.
	 * @param out_record Populated on success; caller must free out_record->file_id.
	 */
	bool iso9660_get_file(iso9660_ctx_t* ctx, const char* path, iso9660_directory_record_t* out_record);

/** VFS driver vtable for ISO 9660. */
	extern const VFS_FSOps iso9660_vfs_ops;

#ifdef __cplusplus
}
#endif
#endif // WALLOS_ISO9660_H