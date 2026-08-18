/**
 * @file wallos_gpt.h
 * @author Malcolm
 * @date 7/5/2026
 */
#ifndef WDM_MOCK_WALLOS_GPT_H
#define WDM_MOCK_WALLOS_GPT_H

#include <stdint.h>
#include <stdbool.h>
#include "../wdm.h"
#include <filesystem/partitions/wallos_mbr.h>
#include "gpt_partition_type.h"


#ifdef __cplusplus
extern "C" {
#endif


	typedef struct {
		char signature[9]; // 8 + \0
		uint32_t gpt_revision;
		uint32_t header_size;
		uint32_t crc32;
		uint32_t reserved;
		uint64_t header_lba;
		uint64_t alt_header_lba;
		uint64_t first_usable_lba;
		uint64_t last_usable_lba;
		uint8_t disk_guid[16];
		uint64_t start_partition_entries_lba; // Starting LBA of the GUID Partition Entry Array
		uint32_t num_partition_entries;
		uint32_t partition_entry_size;
		uint32_t partition_array_crc32;

		// Rest of sector should be zeroed. We don't really care.
	} gpt_partition_header_t;

	typedef struct {
		struct {
			uint64_t lba;
			uint64_t offset;
		} entry_info;
		uint8_t partition_type_guid[16];
		uint8_t unique_partition_guid[16];
		uint64_t first_lba;
		uint64_t last_lba; // inclusive, usually ODD
		uint64_t attributes;
		uint16_t partition_name[36]; // UTF16LE string containing name... very annoying...
	} gpt_partition_entry_t;

	typedef struct {
		mbr_partition_table_t protective_mbr;
		uint8_t gpt_partition_entry; // keeps track of exactly which partition entry in the MBR contains the GPT information.

		gpt_partition_header_t header;

		// There's usually 128 entries of 128 bytes, but it's not guaranteed.
		// There's a good chance that this struct ends up being allocated on the stack, we don't want a 16KiB structure on the stack.
		uint32_t num_entries;
		gpt_partition_entry_t* entries;
	} gpt_partition_table_t;

	typedef enum {
		GPT_FLAGS_NONE = 0,
		GPT_ALLOW_MALFORMED_TABLE = 1 << 0, ///< Allow problems with the table that are wrong, but don't prevent parsing
		GPT_SKIP_CRC_CHECKS = 1 << 1, ///< Skip the CRC checks. Different from malformed, meant to reduce the computation load
		GPT_STRICT_PMBR = 1 << 2, ///< Ensure the MBR contains only a single entry
	} gpt_flags;
	// TODO: Some other flag ideas:
	// GPT_TRY_BACKUP_HEADER - if the main header fails any checks, use the backup header rather than failing
	// GPT_PREFER_BACKUP_HEADER - use the backup header by default instead of the main
	// GPT_STRICT - opposite of ALLOW_MALFORMED, basically adds the validation checks to regular parsing
	// GPT_HEADER_ONLY - skip the partition table parsing
	// GPT_ALLOW_ZERO_ENTRIES - 0 entries is odd but not technically wrong.
	//                          By default this is probably a problem, but a freshly initialized disk may legitimately have zero entries.
	// GPT_IGNORE_ATTRIBUTES - for when we actually worry about the attributes field
	// GPT_VERIFY_BACKUP_MATCHES - after parsing the primary header, verify the backup matches

	/**
	 * Bitflags describing spec-compliance issues found by validate_gpt().
	 * They flag things that are "technically wrong" per the UEFI spec but don't prevent the table from being read/interpreted.
	 */
	typedef enum {
		GPT_VALID = 0,
		GPT_VALIDATION_BAD_REVISION = 1 << 0,  // gpt_revision != 0x00010000
		GPT_VALIDATION_RESERVED_NONZERO = 1 << 1,  // header->reserved != 0
		GPT_VALIDATION_HEADER_LBA_MISMATCH = 1 << 2,  // header_lba != the LBA it was actually read from (1)
		GPT_VALIDATION_USABLE_RANGE_INVERTED = 1 << 3,  // first_usable_lba > last_usable_lba
		GPT_VALIDATION_NULL_DISK_GUID = 1 << 4,  // disk_guid is all zero
		GPT_VALIDATION_ALT_HEADER_LBA_INVALID = 1 << 5,  // alt_header_lba is 0 or equals header_lba
		GPT_VALIDATION_ENTRY_SIZE_NOT_POW2 = 1 << 6,  // partition_entry_size isn't a power-of-two multiple of 128
		GPT_VALIDATION_ENTRY_OUT_OF_RANGE = 1 << 7,  // an entry's LBA range falls outside [first_usable_lba, last_usable_lba]
		GPT_VALIDATION_ENTRY_LBA_INVERTED = 1 << 8,  // an entry has first_lba > last_lba
		GPT_VALIDATION_DUPLICATE_GUID = 1 << 9,  // two entries share the same unique_partition_guid
		GPT_VALIDATION_ENTRY_OVERLAP = 1 << 10, // two entries' LBA ranges overlap
		GPT_VALIDATION_INVALID_PMBR = 1 << 11, // The PMBR contains other partition entries
	} gpt_validation_flags;

	/**
	 * @brief Error codes returned by GPT parsing and validation functions.
	 *
	 * Negative values indicate failure conditions.
	 */
	typedef enum {
		GPT_NO_ERROR = 0,   ///< Operation completed successfully
		GPT_BAD_PARAM = -1,  ///< Invalid arguments were provided (NULL pointers, invalid sizes, etc.)
		GPT_NO_PMBR = -2,  ///< No valid GPT Protective MBR was found
		GPT_IO_ERROR = -3,  ///< I/O error occurred while reading from the underlying storage device
		GPT_INVALID_SIGNATURE = -4,  ///< GPT signature ("EFI PART") was missing or invalid
		GPT_BAD_HEADER = -5,  ///< GPT header is malformed or contains invalid fields (size, bounds, etc.)
		GPT_BAD_PARTITION_TABLE = -6,  ///< GPT partition table is malformed, corrupted, or structurally invalid
		GPT_OUT_OF_MEMORY = -7,  ///< Memory allocation failure occurred during parsing
		GPT_BAD_CRC = -8,  ///< CRC32 validation failure for either GPT header or partition array
		GPT_INVALID_PMBR = -9,  ///< No valid PMBR in LBA 0
		GPT_TABLE_FULL = -10, ///< No free entry slot (auto-index) / requested_index out of range use case
		GPT_ENTRY_OUT_OF_RANGE = -11, ///< Requested LBA range falls outside [first_usable_lba, last_usable_lba]
		GPT_ENTRY_LBA_INVERTED = -12, ///< first_lba > last_lba
		GPT_ENTRY_OVERLAP = -13, ///< Requested LBA range overlaps an existing, non-free entry
		GPT_ENTRY_ALIGNMENT = -14, ///< first_lba isn't aligned per GPT_ADD_ENTRY_REQUIRE_ALIGNMENT
		GPT_ENTRY_SLOT_OCCUPIED = -15, ///< requested_index names a slot that's already in use (and GPT_ADD_ENTRY_OVERWRITE wasn't set)
		GPT_NO_FREE_SPACE = -16, ///< gpt_find_free_space() found no run meeting min_size_sectors

	} gpt_error_t;

	/**
	 * Detect if a given sector (LBA 0) contains a GPT Protective MBR.
	 *
	 * @param gpt Pointer to where the GPT table is to be stored.
	 * @param data Buffer containing at least 512 bytes of the intended sector to be checked.
	 * @param size Size of the buffer
	 * @param flags GPT_ALLOW_MALFORMED_TABLE if you wish to allow parsing of a malformed entry (only requirement when set is that the MBR.entry.partition_type == GPT_PROTECTIVE_MBR)
	 * @retval GPT_NO_ERROR if GPT is found
	 * @retval GPT_NO_PMBR if no GPT is found
	 * @retval GPT_INVALID_PMBR if GPT_STRICT_PMBR is set and the PMBR invalid fields or more than one partition
	 */
	gpt_error_t detect_gpt(gpt_partition_table_t* gpt, const uint8_t* data, const size_t size, gpt_flags flags);

	/**
	 * Run deeper, spec-compliance style validation over an already-parsed GPT table.
	 * Everything checked here is "technically wrong" but doesn't prevent the table from being parsed
	 *
	 * @param gpt Already-parsed GPT table (via parse_gpt).
	 * @return A bitmask of gpt_validation_flags values (GPT_VALID if nothing wrong).
	 */
	uint32_t validate_gpt(const gpt_partition_table_t* gpt);

	/**
	 * Parse the given sector (buf) for a GPT header.
	 *
	 * @param header Structure to parse into
	 * @param buf Buffer containing the sector of the GPT header
	 * @param size Size of the buffer
	 * @retval GPT_BAD_PARAM pointers are NULL or size is too small
	 * @retval GPT_NO_ERROR if parsed successfully
	 */
	gpt_error_t parse_gpt_header(gpt_partition_header_t* header, const uint8_t* buf, const size_t size);

	/**
	 * Parse a single partition entry from the GPT. Buf is expected to be at the start of the partition.
	 *
	 * @param entry Structure to parse into
	 * @param buf Buffer containing the entry. buf[0] should be the exact start of the entry
	 * @param size Size of the buffer
	 * @param entry_lba Current LBA of the partition entry we are parsing
	 * @param entry_offset Offset into the LBA
	 * @retval GPT_BAD_PARAM if pointers are NULL or size is too short
	 * @retval GPT_NO_ERROR if successful
	 */
	gpt_error_t parse_gpt_partition_entry(gpt_partition_entry_t* entry, const uint8_t* buf, const size_t size, uint64_t entry_lba, uint64_t entry_offset);

	/**
	 * @brief Serialize a GPT header into its on-disk representation.
	 *
	 * Inverse of parse_gpt_header().
	 * Writes the fixed 92-byte header fields at their standard offsets and zeroes the rest of @p buf (up to @p buf_size) first.
	 *
	 * This does NOT compute header->crc32. gpt_finalize() does the final CRC32 calcs.
	 *
	 * @param header Header to serialize. If NULL, the function returns without writing anything.
	 * @param buf Destination buffer. If NULL, the function returns without writing anything.
	 * @param buf_size Size of @p buf in bytes. Must be at least GPT_SECTOR_SIZE (512), or the function returns without writing anything.
	 */
	void write_gpt_header(const gpt_partition_header_t* header, uint8_t* buf, size_t buf_size);

	/**
	 * @brief Serialize a single GPT partition entry into its on-disk representation.
	 *
	 * Inverse of parse_gpt_partition_entry().
	 * If the table's partition_entry_size is larger than 128, the caller is responsible for zeroing/writing the remaining bytes of that slot themselves.
	 *
	 * @param entry Entry to serialize. If NULL, the function returns without writing anything.
	 * @param buf Destination buffer for the entry. Must have at least 128 bytes available. If NULL, the function returns without writing anything.
	 */
	void write_gpt_partition_entry(const gpt_partition_entry_t* entry, uint8_t* buf);

	/**
	 * @brief Serialize a table's full partition entry array into a freshly allocated on-disk buffer.
	 *
	 * Slots gpt->entries[0..gpt->num_entries-1] are handled via write_partition_entry().
	 * All other empty slots are handled here, initialized to zero.
	 *
	 * @param gpt Table to serialize.
	 * @param out_buf On success, set to a calloc'd buffer of *out_size bytes. Caller must free() it.
	 *                Set to NULL if header.num_partition_entries is 0 (nothing to serialize).
	 * @param out_size On success, set to header.num_partition_entries * header.partition_entry_size (may be 0).
	 *
	 * @retval GPT_BAD_PARAM gpt, out_buf, or out_size is NULL, or gpt->num_entries > 0 but gpt->entries is NULL.
	 * @retval GPT_BAD_PARTITION_TABLE gpt->num_entries exceeds header.num_partition_entries, or header.partition_entry_size is smaller than 128.
	 * @retval GPT_OUT_OF_MEMORY the output buffer couldn't be allocated.
	 * @retval GPT_NO_ERROR serialized successfully.
	 */
	gpt_error_t gpt_serialize_partition_array(const gpt_partition_table_t* gpt, uint8_t** out_buf, uint64_t* out_size);

	/**
	 * Parses the given drive for GPT, and fills in the provided GPT structure.
	 *
	 * @param drive Drive handle for the drive we are parsing
	 * @param buf Buffer of at least a sector size for the corresponding disk
	 * @param size Size of the buffer
	 * @param gpt Structure to be filled in
	 * @param flags Flags to specify how to parse the GPT
	 * @retval GPT_BAD_PARAM if any pointers are NULL, or buffer size is to small to fit a whole sector
	 * @retval GPT_IO_ERROR if any drive functions fail (WDM_GetInfo, WDM_Read)
	 * @retval GPT_BAD_CRC if any CRC checks fail (and GPT_ALLOW_MALFORMED_TABLE or GPT_SKIP_CRC_CHECKS are not set)
	 * @retval GPT_INVALID_SIGNATURE if LBA 1 does not start with "EFI PART"
	 * @retval GPT_BAD_HEADER if the header size is abnormal (and GPT_ALLOW_MALFORMED_TABLE is not set)
	 * @retval GPT_BAD_PARTITION_TABLE if the partition table doesn't fit what's expected (partition_entry_size is either too small or too large, num_partition_entries is abnormal)
	 * @retval GPT_OUT_OF_MEMORY if a memory allocation fails
	 * @retval GPT_NO_PMBR if the MBR does not contain a GPT_PROTECTIVE_MBR
	 * @retval GPT_INVALID_PMBR if GPT_STRICT_PMBR is set and the PMBR invalid fields or more than one partition
	 */
	gpt_error_t parse_gpt(WDM_DriveHandle drive, uint8_t* buf, const size_t size, gpt_partition_table_t* gpt, gpt_flags flags);

	void print_gpt_validation_result(uint32_t result);
	void print_gpt_partition_table(const gpt_partition_table_t* table);
	void print_gpt_partition_entry(const gpt_partition_entry_t* entry, size_t index);
	void print_gpt_partition_header(const gpt_partition_header_t* header);

	void gpt_name_to_ascii(const uint16_t* utf16_name, char* out_ascii, size_t max_len);
	gpt_partition_type_id_t gpt_partition_type_from_guid(const uint8_t guid[16]);
	const char* gpt_partition_type_name(gpt_partition_type_id_t id);
	const uint8_t* gpt_guid_from_partition_type(gpt_partition_type_id_t id);

	gpt_error_t guid_from_string(uint8_t guid[16], const char* str);
	void guid_to_string(const uint8_t guid[16], char* out);

	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------
	// GPT Construct
	// ------------------------------------------------------------------------------------------------
	// ------------------------------------------------------------------------------------------------

	/**
	 * @brief Default partition alignment, in sectors, when GPT_ADD_ENTRY_REQUIRE_ALIGNMENT is set.
	 *        2048 sectors * 512 bytes/sector = 1 MiB, matching the convention used by gdisk/Windows/parted.
	 *        If you construct tables with a non-512 GPT_SECTOR_SIZE, recompute this accordingly.
	 */
#define GPT_DEFAULT_ALIGNMENT_LBA 2048ULL
#define GPT_ON_DISK_HEADER_SIZE 92u
#define GPT_ON_DISK_ENTRY_SIZE 128u
#define GPT_SECTOR_SIZE 512u

/**
 * @brief Option flags controlling how gpt_construct() builds a fresh GPT.
 */
	typedef enum {
		GPT_CONSTRUCT_STANDARD = 0,        ///< Build a plain primary header + protective MBR.
		GPT_CONSTRUCT_ALLOCATE_ENTRIES = (1 << 0), ///< Heap-allocate gpt->entries (num_entries * sizeof(gpt_partition_entry_t)), zeroed. Caller owns the memory and must free() it.
		GPT_CONSTRUCT_BACKUP_HEADER = (1 << 1), ///< Build this as the backup/alternate header (header_lba/alt_header_lba swapped, start_partition_entries_lba placed just before it) instead of the primary.
	} gpt_constructor_flags_t;

	/**
	 * @brief Option flags controlling how gpt_partition_entry_construct() fills in fields.
	 */
	typedef enum {
		GPT_PARTITION_CONSTRUCT_STANDARD = 0,        ///< Not bootable/no special attributes. unique_partition_guid is left zeroed for the caller to fill in.
		GPT_PARTITION_CONSTRUCT_RANDOM_GUID = (1 << 0), ///< Generate a random unique_partition_guid via rng_next() instead of leaving it zeroed.
	} gpt_partition_constructor_flags_t;

	/**
	 * @brief Option flags controlling how gpt_add_partition_entry() picks a slot and validates the request.
	 */
	typedef enum {
		GPT_ADD_ENTRY_STANDARD = 0,        ///< Use requested_index as given. Range must be in-bounds, non-overlapping, non-inverted. Slot must already be free.
		GPT_ADD_ENTRY_AUTO_INDEX = (1 << 0), ///< Ignore requested_index. Use the first free slot found via gpt_find_free_entry_slot().
		GPT_ADD_ENTRY_ALLOW_OVERLAP = (1 << 1), ///< Skip the overlap check against existing entries. Dangerous, should only be used for deliberately reconstructing a malformed/legacy table.
		GPT_ADD_ENTRY_ALLOW_OUT_OF_RANGE = (1 << 2), ///< Skip the [first_usable_lba, last_usable_lba] bounds check.
		GPT_ADD_ENTRY_REQUIRE_ALIGNMENT = (1 << 3), ///< Require first_lba to be a multiple of GPT_DEFAULT_ALIGNMENT_LBA.
		GPT_ADD_ENTRY_OVERWRITE = (1 << 4), ///< Allow requested_index to name an already-occupied slot (silently overwritten). Ignored when GPT_ADD_ENTRY_AUTO_INDEX is set.
	} gpt_add_entry_flags_t;

	typedef enum {
		GPT_BACKUP_STANDARD = 0,        ///< Copy the header only. @p backup->entries is left NULL (caller must assign/copy it themselves).
		GPT_BACKUP_ALLOCATE_ENTRIES = (1 << 0), ///< Heap-allocate backup->entries and deep-copy every entry from primary->entries into it. No-op (entries left NULL) if primary->entries is NULL. Caller owns the allocation and must free() it.
	} gpt_backup_flags_t;

	/**
	 * @brief Build a fresh, blank GPT partition table in memory, including its protective MBR.
	 *
	 * partition_array_crc32 and header.crc32 are left at 0.
	 * Both must be computed by the caller after the header and (fully populated) partition entry array are finalized.
	 *
	 * @param gpt Table to populate. If NULL, the function returns without doing anything.
	 * @param disk_sectors Total size of the target disk, in GPT_SECTOR_SIZE-byte sectors.
	 *                     Must be large enough to fit the primary header, backup header, and both partition entry arrays,
	 *                     or the resulting first_usable_lba/last_usable_lba range will be degenerate.
	 * @param num_entries Number of partition entries the table should have room for (commonly 128).
	 *                    Stored in header.num_partition_entries and used to size the partition entry array region.
	 * @param flags Bitwise-OR of gpt_constructor_flags_t options. Pass GPT_CONSTRUCT_STANDARD for default (primary header, entries left unallocated) behavior.
	 */
	void gpt_construct(gpt_partition_table_t* gpt, uint64_t disk_sectors, uint32_t num_entries, gpt_constructor_flags_t flags);

	/**
	 * @brief Populate a GPT partition entry with sane defaults.
	 *
	 * @param entry Entry to populate. If NULL, the function returns without doing anything.
	 * @param type Well-known partition type to assign. Its GUID is looked up via gpt_guid_from_partition_type().
	 * @param first_lba First LBA of the partition (inclusive).
	 * @param last_lba Last LBA of the partition (inclusive).
	 * @param attributes Raw 64-bit GPT attributes bitfield (see UEFI spec, e.g. bit 0 = "required partition").
	 * @param name Optional NUL-terminated ASCII partition name. May be NULL for an unnamed partition.
	 *             Truncated to 35 characters (36-entry UTF-16LE array, last slot reserved for NUL).
	 * @param flags Bitwise-OR of gpt_partition_constructor_flags_t options.
	 *              Pass GPT_PARTITION_CONSTRUCT_STANDARD to leave unique_partition_guid zeroed (the caller must then set it).
	 */
	void gpt_partition_entry_construct(gpt_partition_entry_t* entry, gpt_partition_type_id_t type, uint64_t first_lba, uint64_t last_lba, uint64_t attributes, const char* name, gpt_partition_constructor_flags_t flags);

	/**
	 * @brief Zero out a GPT partition entry, marking it unused.
	 * @param entry Entry to clear. If NULL, the function returns without doing anything.
	 */
	void gpt_partition_entry_clear(gpt_partition_entry_t* entry);

	/**
	 * @brief Check whether a GPT partition entry is unused (all-zero partition_type_guid).
	 * @param entry Entry to check. A NULL pointer is treated as free/absent and returns true.
	 * @return true if the entry is free/unused, false if it holds a real partition.
	 */
	bool gpt_partition_entry_is_free(const gpt_partition_entry_t* entry);

	/**
	 * @brief Find the first free (unused) entry slot in a GPT table.
	 * @param gpt Table to search. gpt->entries must be non-NULL (see GPT_CONSTRUCT_ALLOCATE_ENTRIES).
	 * @param out_index On success, set to the index of the first free slot.
	 * @retval GPT_BAD_PARAM if gpt, gpt->entries, or out_index is NULL.
	 * @retval GPT_TABLE_FULL if every entry slot is occupied.
	 * @retval GPT_NO_ERROR on success.
	 */
	gpt_error_t gpt_find_free_entry_slot(const gpt_partition_table_t* gpt, uint32_t* out_index);

	/**
	 * @brief Count how many entry slots in a GPT table are currently occupied.
	 * @param gpt Table to inspect.
	 * @return Number of non-free entries. Returns 0 if gpt or gpt->entries is NULL.
	 */
	uint32_t gpt_count_used_entries(const gpt_partition_table_t* gpt);

	/**
	 * @brief Validate and insert a new, fully-formed partition entry into a GPT table.
	 *
	 * This is the proper way to populate gpt->entries
	 * This function checks that the requested range is well-formed, in-bounds, non-overlapping, and (optionally) aligned,
	 * and that the destination slot is actually free, before writing anything.
	 *
	 * @param gpt Table to modify. gpt->entries must already be allocated (see GPT_CONSTRUCT_ALLOCATE_ENTRIES).
	 * @param type Partition entry type for the new entry (looked up via gpt_guid_from_partition_type()).
	 * @param first_lba First LBA of the partition (inclusive).
	 * @param last_lba Last LBA of the partition (inclusive).
	 * @param attributes Raw 64-bit GPT attributes bitfield.
	 * @param name Optional NUL-terminated ASCII partition name (may be NULL).
	 * @param construct_flags Forwarded to gpt_partition_entry_construct() (e.g. GPT_PARTITION_CONSTRUCT_RANDOM_GUID).
	 * @param add_flags Bitwise-OR of gpt_add_entry_flags_t controlling slot selection/validation strictness.
	 * @param requested_index Slot to use when GPT_ADD_ENTRY_AUTO_INDEX is not set. Ignored otherwise.
	 * @param out_index Optional. If non-NULL, set to the index the entry was actually written to.
	 *
	 * @retval GPT_BAD_PARAM gpt/gpt->entries is NULL, num_entries is 0, or requested_index is out of range.
	 * @retval GPT_ENTRY_LBA_INVERTED first_lba > last_lba.
	 * @retval GPT_ENTRY_OUT_OF_RANGE range falls outside [first_usable_lba, last_usable_lba] (unless GPT_ADD_ENTRY_ALLOW_OUT_OF_RANGE).
	 * @retval GPT_ENTRY_ALIGNMENT first_lba isn't aligned to GPT_DEFAULT_ALIGNMENT_LBA (only if GPT_ADD_ENTRY_REQUIRE_ALIGNMENT is set).
	 * @retval GPT_TABLE_FULL no free slot found (only when GPT_ADD_ENTRY_AUTO_INDEX is set).
	 * @retval GPT_ENTRY_SLOT_OCCUPIED requested_index names an in-use slot (unless GPT_ADD_ENTRY_OVERWRITE).
	 * @retval GPT_ENTRY_OVERLAP range overlaps an existing, non-free entry (unless GPT_ADD_ENTRY_ALLOW_OVERLAP).
	 * @retval GPT_NO_ERROR entry was constructed and written successfully.
	 */
	gpt_error_t gpt_add_partition_entry(gpt_partition_table_t* gpt, gpt_partition_type_id_t type, uint64_t first_lba, uint64_t last_lba, uint64_t attributes, const char* name, gpt_partition_constructor_flags_t construct_flags, gpt_add_entry_flags_t add_flags, uint32_t requested_index, uint32_t* out_index);

	/**
	 * @brief Remove (clear) a partition entry by index.
	 * @param gpt Table to modify.
	 * @param index Slot to clear.
	 * @retval GPT_BAD_PARAM gpt/gpt->entries is NULL or index is out of range.
	 * @retval GPT_NO_ERROR entry was cleared.
	 */
	gpt_error_t gpt_remove_partition_entry(gpt_partition_table_t* gpt, uint32_t index);

	/**
	 * @brief Find the largest available (unused) LBA run in a GPT table, at or above a minimum size.
	 *
	 * Scans the gaps between the usable-range boundaries and existing, non-free entries.
	 * Assumes the table's existing entries are already mutually non-overlapping (true for anything built solely through gpt_add_partition_entry().
	 * Not guaranteed for a table loaded via parse_gpt() without first running validate_gpt() and checking GPT_VALIDATION_ENTRY_OVERLAP).
	 *
	 * @param gpt Table to scan.
	 * @param min_size_sectors Minimum acceptable run length, in sectors. Pass 0 to just find the largest gap regardless of size.
	 * @param alignment Alignment (in sectors) candidate start points are rounded up to. Pass 1 (or 0, treated as 1) for no alignment requirement.
	 * @param out_first_lba On success, set to the first LBA of the largest qualifying run.
	 * @param out_last_lba On success, set to the last LBA (inclusive) of the largest qualifying run.
	 * @retval GPT_BAD_PARAM gpt, gpt->entries, out_first_lba, or out_last_lba is NULL.
	 * @retval GPT_NO_FREE_SPACE no run meeting min_size_sectors was found.
	 * @retval GPT_NO_ERROR a run was found. *out_first_lba / *out_last_lba are set.
	 */
	gpt_error_t gpt_find_free_space(const gpt_partition_table_t* gpt, uint64_t min_size_sectors, uint64_t alignment, uint64_t* out_first_lba, uint64_t* out_last_lba);

	/**
	 * @brief Derive a backup (alternate) GPT table from an already-constructed primary table.
	 *
	 * Copies the fields that must stay identical between primary and backup, then computes the fields that differ for a backup header:
	 *
	 *   - header_lba / alt_header_lba are swapped relative to @p primary
	 *   - start_partition_entries_lba is recomputed to sit immediately before the backup header itself
	 *
	 * header.crc32 and header.partition_array_crc32 are left at 0.
	 * Both must be (re)computed once the backup's header/entries are fully finalized.
	 *
	 * @param primary Already-constructed primary GPT table to copy from. Must not be NULL.
	 * @param backup Table to populate as the backup. Must not be NULL. Any existing contents are overwritten (memset to 0 first).
	 *               This function DOES NOT free an existing backup->entries before overwriting the pointer, so free it yourself first if you're reusing a table.
	 * @param flags Bitwise-OR of gpt_backup_flags_t options. Pass GPT_BACKUP_STANDARD to only copy the header.
	 *
	 * @retval GPT_BAD_PARAM primary or backup is NULL.
	 * @retval GPT_OUT_OF_MEMORY GPT_BACKUP_ALLOCATE_ENTRIES was set and the calloc() for backup->entries failed.
	 * @retval GPT_NO_ERROR backup was populated successfully.
	 */
	gpt_error_t gpt_create_backup(const gpt_partition_table_t* primary, gpt_partition_table_t* backup, gpt_backup_flags_t flags);

	/**
	 * @brief Option flags controlling how gpt_finalize() prepares a table for writing.
	 */
	typedef enum {
		GPT_FINALIZE_STANDARD = 0,        ///< Just (re)compute partition_array_crc32 and header.crc32.
		GPT_FINALIZE_VALIDATE = (1 << 0), ///< Before computing CRCs, run validate_gpt() and fail with GPT_BAD_PARTITION_TABLE if any structurally-fatal issue is set
	} gpt_finalize_flags_t;

	/**
	 * @brief Put an in-memory GPT table into a state ready to be serialized/written to disk.
	 *
	 * This is the required last step before calling a disk-writing function:
	 * it (re)computes the CRCs for both the header and partition array. Both CRCs are written back into @p gpt->header.
	 *
	 * @param gpt Table to finalize. If NULL, returns GPT_BAD_PARAM.
	 * @param flags Bitwise-OR of gpt_finalize_flags_t options. Pass GPT_FINALIZE_STANDARD for just the CRC work.
	 *
	 * @retval GPT_BAD_PARAM @p gpt is NULL, or gpt->num_entries > 0 but gpt->entries is NULL.
	 * @retval GPT_BAD_PARTITION_TABLE gpt->num_entries exceeds header.num_partition_entries,
	 *         header.partition_entry_size is smaller than 128,
	 *         or (with GPT_FINALIZE_VALIDATE) validate_gpt()  flagged a structurally-fatal issue.
	 * @retval GPT_BAD_HEADER header.header_size is larger than GPT_SECTOR_SIZE.
	 * @retval GPT_OUT_OF_MEMORY the scratch buffer for the partition array couldn't be allocated.
	 * @retval GPT_NO_ERROR table was finalized successfully; header.crc32 and header.partition_array_crc32 are up to date.
	 */
	gpt_error_t gpt_finalize(gpt_partition_table_t* gpt, gpt_finalize_flags_t flags);

	/**
	 * @brief Option flags controlling how gpt_write() performs its disk I/O.
	 */
	typedef enum {
		GPT_WRITE_STANDARD = 0,         ///< Issue writes with WDM_FLAG_NONE. No flush at the end.
		GPT_WRITE_SYNC = (1 << 0), ///< Pass WDM_FLAG_SYNC on every WDM_Write() call, and call WDM_Flush() once everything's written.
	} gpt_write_flags_t;

	/**
	 * @brief Write a finalized GPT table (and optionally its backup) out to disk.
	 *
	 * Writes, in order:
	 * - the protective MBR (LBA 0, from primary->protective_mbr)
	 * - the primary header and partition entry array,
	 * - if @p backup is non-NULL, the backup header and partition entry array.
	 * Each piece is serialized immediately before writing (write_mbr(), write_gpt_header(), gpt_serialize_partition_array()).
	 *
	 * @p primary and @p backup (if given) must already be finalized via gpt_finalize(), this function takes the structures at face value
	 *
	 * @param drive Drive to write to. Must not be NULL.
	 * @param primary Finalized primary table to write. Must not be NULL.
	 * @param backup Finalized backup table to write, or NULL to skip writing a backup
	 * @param flags Bitwise-OR of gpt_write_flags_t options. Pass GPT_WRITE_STANDARD for default behavior.
	 *
	 * @retval GPT_BAD_PARAM drive or primary is NULL, or the drive's sector size is smaller than GPT_SECTOR_SIZE.
	 * @retval GPT_IO_ERROR a WDM_GetInfo()/WDM_Write()/WDM_Flush() call failed.
	 * @retval GPT_BAD_PARTITION_TABLE/GPT_OUT_OF_MEMORY propagated from gpt_serialize_partition_array() if primary or backup wasn't left in a writable state.
	 * @retval GPT_NO_ERROR everything was written successfully.
	 */
	gpt_error_t gpt_write(WDM_DriveHandle drive, const gpt_partition_table_t* primary, const gpt_partition_table_t* backup, gpt_write_flags_t flags);

#ifdef __cplusplus
}
#endif
#endif //WDM_MOCK_WALLOS_GPT_H