#ifndef WALLOS_FAT_INTERNAL_H
#define WALLOS_FAT_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <filesystem/fat/fat.h>

#ifdef __cplusplus
extern "C" {

#endif

	/**
	 * @brief Reads a 16-bit little-endian integer from a byte buffer.
	 * @param buf Source byte array.
	 * @param offset Byte offset to read from.
	 * @return The parsed 16-bit value.
	 */
	uint16_t fat_internal_read16(const uint8_t* buf, size_t offset);

	/**
	 * @brief Reads a 32-bit little-endian integer from a byte buffer.
	 * @param buf Source byte array.
	 * @param offset Byte offset to read from.
	 * @return The parsed 32-bit value.
	 */
	uint32_t fat_internal_read32(const uint8_t* buf, size_t offset);

	/**
	 * @brief Writes a 16-bit integer into a byte buffer in little-endian format.
	 * @param buf Destination byte array.
	 * @param offset Byte offset to write at.
	 * @param val Value to write.
	 */
	void fat_internal_write16(uint8_t* buf, size_t offset, uint16_t val);

	/**
	 * @brief Writes a 32-bit integer into a byte buffer in little-endian format.
	 * @param buf Destination byte array.
	 * @param offset Byte offset to write at.
	 * @param val Value to write.
	 */
	void fat_internal_write32(uint8_t* buf, size_t offset, uint32_t val);

	/**
	 * @brief Formats a raw 11-byte on-disk short name into a null-terminated 8.3 string.
	 *
	 * Trims trailing spaces from the base and extension, inserts the dot, and writes the result into @p out.
	 * Example: "HELLO   TXT" → "HELLO.TXT".
	 *
	 * @param raw11 Raw 11-byte directory entry name field.
	 * @param out Output buffer, must be at least 13 bytes.
	 */
	void fat_format_short_name(const uint8_t raw11[11], char out[13]);

	/**
	 * @brief Computes the standard VFAT short-name checksum.
	 *
	 * Used to validate that LFN entries belong to the short entry that follows them.
	 *
	 * @param raw11 Raw 11-byte short name.
	 * @return The checksum byte.
	 */
	uint8_t fat_short_name_checksum(const uint8_t raw11[11]);

	/** @name Default Timestamp Constants
	 * Fallback timestamps used when stamping new directory entries (maps to 1980-01-01 00:00:00).
	 */
#define FAT_EPOCH_DATE 0x0021
#define FAT_EPOCH_TIME 0x0000

	/**
	 * @brief Validates and packs a "NAME.EXT" string into an 11-byte raw short name.
	 *
	 * Rejects names that don't fit 8.3, contain illegal characters, or start with '.' or ' '.
	 * Uppercases the name in place.
	 * Escapes a literal 0xE5 first byte to 0x05 per spec.
	 *
	 * @param name Null-terminated input filename.
	 * @param raw11 Output buffer for the packed 11-byte name.
	 * @retval true Name fits 8.3 and was packed successfully.
	 * @retval false Name is invalid or can't be represented in 8.3.
	 */
	bool fat_pack_short_name_raw(const char* name, uint8_t raw11[11]);

	/**
	 * @brief Packs fields into a raw 32-byte short directory entry.
	 *
	 * Timestamps are set to FAT_EPOCH_DATE / FAT_EPOCH_TIME.
	 * The first_cluster is split across the high (offset 20) and low (offset 26) fields automatically.
	 *
	 * @param raw11 Pre-validated 11-byte short name.
	 * @param attributes FAT attribute byte (e.g. FAT_ATTR_ARCHIVE, FAT_ATTR_DIRECTORY).
	 * @param first_cluster Starting cluster of the file or directory.
	 * @param file_size File size in bytes (0 for directories).
	 * @param raw32 Output buffer for the 32-byte entry.
	 */
	void fat_pack_dirent_raw(const uint8_t raw11[11], uint8_t attributes, uint32_t first_cluster, uint32_t file_size, uint8_t raw32[32]);

	typedef struct fat_dir_writer fat_dir_writer_t;

	/**
	 * @brief Vtable for directory I/O operations.
	 *
	 * Abstracts the difference between a cluster-chain directory (FAT32) and a fixed-size flat root region (FAT12/16), so the slot-scanning logic in fat_dir_locate_or_reserve_slot / fat_dir_reserve_slot_run can work on both.
	 */
	typedef struct {
		/** Returns the current number of sectors in the directory. */
		uint32_t(*sector_count)(fat_dir_writer_t* w);

		/**
		 * Reads sector @p idx (relative to the start of the directory) into @p buf.
		 * Returns false on I/O error or out-of-range index.
		 */
		bool (*read_sector)(fat_dir_writer_t* w, uint32_t idx, uint8_t* buf);

		/**
		 * Writes @p buf to sector @p idx (relative to the start of the directory).
		 * Returns false on I/O error or out-of-range index.
		 */
		bool (*write_sector)(fat_dir_writer_t* w, uint32_t idx, const uint8_t* buf);

		/**
		 * Extends the directory by one cluster (cluster-chain directories only).
		 * The new cluster must be zeroed by the implementation.
		 * Returns false if the volume is full or this is a fixed-size root region.
		 */
		bool (*extend)(fat_dir_writer_t* w);
	} fat_dir_writer_ops_t;

	/**
	 * @brief Context passed to all fat_dir_* functions.
	 *
	 * Holds the ops vtable, an implementation-specific context pointer, and the sector size.
	 * Initialise with fat_dir_writer_init().
	 */
	struct fat_dir_writer {
		const fat_dir_writer_ops_t* ops;
		void* ctx;
		uint16_t bytes_per_sector;
	};

	/**
	 * @brief Initialises a fat_dir_writer_t.
	 * @param w Writer to initialise.
	 * @param ops Vtable for the underlying directory storage.
	 * @param ctx Implementation-specific context (e.g. fat32_dir_writer_ctx_t*).
	 * @param bytes_per_sector Sector size of the volume.
	 */
	void fat_dir_writer_init(fat_dir_writer_t* w, const fat_dir_writer_ops_t* ops, void* ctx, uint16_t bytes_per_sector);

	/**
	 * @brief Coordinates of a single 32-byte directory slot on disk.
	 *
	 * sector_index is relative to the start of the directory (as seen by the fat_dir_writer_ops_t vtable),
	 * and offset is the byte offset within that sector — always a multiple of 32.
	 */
	typedef struct {
		uint32_t sector_index;
		uint32_t offset;
	} fat_dirent_slot_t;

	/**
	 * @brief Finds a slot matching @p target_raw11, or reserves a free slot if not found.
	 *
	 * Scans the directory for a live short entry whose 11-byte name matches @p target_raw11.
	 * If found, @p out_slot and @p out_existed are set and the existing raw entry is optionally copied into @p existing_raw32.
	 *
	 * If not found, the first free (0xE5) or end-of-directory (0x00) slot is reserved and @p out_existed is set to false.
	 * If the directory is completely full, it is extended by one cluster first.
	 *
	 * @param w Directory writer context.
	 * @param target_raw11 11-byte short name to search for.
	 * @param out_slot Receives the coordinates of the matched or reserved slot.
	 * @param out_existed Set to true if the entry already existed, false if newly reserved.
	 * @param existing_raw32 If non-NULL and the entry existed, receives a copy of its 32 bytes.
	 * @retval true Slot found or reserved successfully.
	 * @retval false I/O error or directory extension failed.
	 */
	bool fat_dir_locate_or_reserve_slot(fat_dir_writer_t* w, const uint8_t target_raw11[11], fat_dirent_slot_t* out_slot, bool* out_existed, uint8_t existing_raw32[32]);

	/**
	 * @brief Writes 32 bytes into a directory slot (read-modify-write on the sector).
	 * @param w Directory writer context.
	 * @param slot Slot to write into.
	 * @param raw32 Data to write.
	 * @retval true Write succeeded.
	 * @retval false I/O error.
	 */
	bool fat_dir_write_slot(fat_dir_writer_t* w, fat_dirent_slot_t slot, const uint8_t raw32[32]);

	/**
	 * @brief Accumulator for a multi-slot VFAT LFN entry chain.
	 *
	 * Call fat_lfn_run_accumulate() for each LFN slot as you scan a directory,
	 * then fat_lfn_run_resolve() once the trailing short entry is reached to extract the assembled long name.
	 * fat_lfn_run_reset() clears state between entries.
	 */
	typedef struct {
		uint16_t chars[FAT_LFN_MAX_ENTRIES][FAT_LFN_MAX_CHARS_PER_ENTRY]; ///< UTF-16LE characters, indexed [seq-1][char].
		bool present[FAT_LFN_MAX_ENTRIES]; ///< Which sequence slots have been seen.
		uint8_t checksum; ///< Checksum from the first (last-entry) LFN slot.
		int highest_seq; ///< Highest sequence number seen (= total slot count).
	} fat_lfn_run_t;

	/** @brief Resets an LFN run accumulator to empty state. */
	void fat_lfn_run_reset(fat_lfn_run_t* run);

	/**
	 * @brief Folds one raw LFN directory slot into the accumulator.
	 *
	 * Must be called in on-disk order (highest sequence number first).
	 * Resets the run if the slot looks corrupt or belongs to a different chain.
	 *
	 * @param run Accumulator to update.
	 * @param raw32 Raw 32-byte LFN directory slot.
	 */
	void fat_lfn_run_accumulate(fat_lfn_run_t* run, const uint8_t* raw32);

	/**
	 * @brief Assembles the accumulated LFN slots into a UTF-8/ASCII string.
	 *
	 * Validates that the checksum matches @p short_checksum and that all expected sequence slots are present.
	 * On any failure, @p out[0] is set to '\0' and the caller should fall back to the short name.
	 *
	 * @param run Completed accumulator (after all LFN slots have been fed in).
	 * @param short_checksum Checksum of the short entry that terminates the LFN chain.
	 * @param out Output buffer, must be at least FAT_LFN_MAX_NAME_CHARS + 1 bytes.
	 */
	void fat_lfn_run_resolve(const fat_lfn_run_t* run, uint8_t short_checksum, char* out);

	/**
	 * @brief Parses a raw 32-byte short directory entry into a fat_dirent_t.
	 * @param raw32 Source 32-byte entry.
	 * @param d Structure to fill.
	 */
	void fat_fill_dirent_raw(const uint8_t* raw32, fat_dirent_t* d);

	/**
	 * @brief Appends an entry to a fat_dirent_list_t, growing the backing array if needed.
	 * @param list List to append to.
	 * @param item Entry to copy in.
	 * @retval true Entry added.
	 * @retval false Allocation failed.
	 */
	bool fat_dirent_list_push(fat_dirent_list_t* list, const fat_resolved_dirent_t* item);

	/**
	 * @brief Case-insensitive name match against a resolved directory entry.
	 *
	 * Checks the long name first (if present), then falls back to the short name.
	 *
	 * @param e Entry to test.
	 * @param name Name to match against.
	 * @retval true Match found.
	 * @retval false No match.
	 */
	bool fat_name_matches(const fat_resolved_dirent_t* e, const char* name);

	/**
	 * @brief Returns true if the entry is "." or "..".
	 * @param e Entry to test.
	 */
	bool fat_is_dot_entry(const fat_resolved_dirent_t* e);

	/**
	 * @brief Parses one sector's worth of directory entries, folding LFN runs as it goes.
	 *
	 * Appends resolved entries to @p out_list.
	 * Sets @p *end_of_directory to true and returns early if a 0x00 end-of-directory marker is encountered.
	 * @p lfn_run must persist across sector boundaries for the same directory.
	 *
	 * @param sector Raw sector data.
	 * @param bytes_per_sector Sector size.
	 * @param lfn_run Persistent LFN accumulator (shared across calls for this directory).
	 * @param out_list List to append resolved entries into.
	 * @param end_of_directory Set to true if an end-of-directory marker was found.
	 * @retval true Parsed successfully.
	 * @retval false Allocation failed while appending to out_list.
	 */
	bool fat_parse_dirent_sector(const uint8_t* sector, uint16_t bytes_per_sector, fat_lfn_run_t* lfn_run, fat_dirent_list_t* out_list, bool* end_of_directory);

	/**
	 * @brief Packs a long filename into a sequence of raw 32-byte LFN directory entries.
	 *
	 * Entries are written in on-disk order (highest sequence number first,
	 * so the last logical chunk of the name lands in out_entries[0]).
	 * @p out_entries must be at least `ceil(strlen(long_name) / 13) * 32` bytes.
	 *
	 * @param long_name Null-terminated filename (ASCII/UTF-8 subset).
	 * @param raw11 The corresponding short name, used to compute the checksum.
	 * @param out_entries Output buffer for the packed LFN entries.
	 * @return Number of LFN entries written, or 0 on failure.
	 */
	int fat_pack_lfn_entries(const char* long_name, const uint8_t raw11[11], uint8_t* out_entries);

	/**
	 * @brief Generates a collision-free 8.3 short name for a long filename.
	 *
	 * Uses the standard ~N numeric tail algorithm (LONGFI~1.TXT, LONGFI~2.TXT, ...).
	 * Checks each candidate against @p existing to avoid duplicates.
	 *
	 * @param long_name Source long filename.
	 * @param existing Directory listing to check for collisions (may be NULL).
	 * @param raw11 Output buffer for the packed 11-byte short name.
	 * @retval true Short name generated successfully.
	 * @retval false All ~N slots exhausted (extremely unlikely in practice).
	 */
	bool fat_generate_short_name(const char* long_name, const fat_dirent_list_t* existing, uint8_t raw11[11]);

	/**
	 * @brief Reserves a contiguous run of @p need consecutive free/end-of-directory slots.
	 *
	 * Needed when writing an LFN entry, which requires (lfn_count + 1) consecutive slots.
	 * Extends the directory by one cluster if no run large enough exists.
	 * If the run consumes the existing end-of-directory marker, a new one is written immediately after the reserved run.
	 *
	 * @param w Directory writer context.
	 * @param need Number of consecutive slots to reserve.
	 * @param out_first_slot Receives the coordinates of the first slot in the run.
	 * @retval true Run reserved successfully.
	 * @retval false I/O error or directory extension failed.
	 */
	bool fat_dir_reserve_slot_run(fat_dir_writer_t* w, int need, fat_dirent_slot_t* out_first_slot);

#ifdef __cplusplus
}
#endif

#endif // WALLOS_FAT_INTERNAL_H
