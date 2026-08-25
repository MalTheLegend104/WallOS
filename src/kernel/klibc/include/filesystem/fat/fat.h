#ifndef WALLOS_FAT_H
#define WALLOS_FAT_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <filesystem/fat/fat_config.h>

#include <filesystem/wdm.h>

#ifdef __cplusplus
extern "C" {
#endif

	typedef struct {
		uint8_t oem_id[9]; // 8 + null terminator
		uint16_t bytes_per_sector;
		uint8_t sectors_per_cluster;
		uint16_t reserved_sectors;
		uint8_t fat_allcation_tables;
		uint16_t root_dir_entries;
		uint16_t sectors;
		uint8_t media_descriptor_type;
		uint16_t sectors_per_fat; // FAT12/FAT16 only
		uint16_t sectors_per_track;
		uint16_t heads;
		uint32_t hidden_sectors;
		uint32_t large_sector_count;
	} fat_bpb_t;

	void fill_bpb(const uint8_t* buf, fat_bpb_t* bpb);
	void print_bpb(const fat_bpb_t* bpb);

	typedef struct {
		uint8_t  name[12]; // 11 + \0
		uint8_t  attributes;
		uint8_t  nt_reserved;
		uint8_t  creation_time_tenths;
		uint16_t creation_time;
		uint16_t creation_date;
		uint16_t last_access_date;
		uint16_t first_cluster_high;
		uint16_t write_time;
		uint16_t write_date;
		uint16_t first_cluster_low;
		uint32_t file_size;
	} fat_dirent_t;

/* FAT directory attribute bits */
#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LFN       0x0F /* (READ_ONLY|HIDDEN|SYSTEM|VOLUME_ID) - marks an LFN entry */

#define FAT_DIRENT_FREE_MARKER      0xE5
#define FAT_DIRENT_END_MARKER       0x00
#define FAT_DIRENT_ESCAPED_E5       0x05 /* in name[0]: real char is 0xE5, not a deleted marker */

#define FAT_LFN_LAST_ENTRY_FLAG     0x40
#define FAT_LFN_SEQ_MASK            0x1F
#define FAT_LFN_MAX_CHARS_PER_ENTRY 13
#define FAT_LFN_MAX_NAME_CHARS      255 /* spec ceiling. we cap our buffer here */

/* One raw on-disk LFN entry, 32 bytes, distinct layout from fat_dirent_t. */
	typedef struct {
		uint8_t  order;      /* sequence number, bit 0x40 = last logical entry */
		uint16_t name1[5];   /* UTF-16LE chars 1-5 */
		uint8_t  attributes; /* always 0x0F */
		uint8_t  type;       /* always 0 */
		uint8_t  checksum;   /* checksum of the associated short name */
		uint16_t name2[6];   /* UTF-16LE chars 6-11 */
		uint16_t zero;       /* always 0 */
		uint16_t name3[2];   /* UTF-16LE chars 12-13 */
	} fat_lfn_entry_t;

	/* A fully resolved directory entry: the raw short entry plus, if present and checksum-valid, the reassembled long name.
	 * long_name[0] == '\0' means no valid LFN was found and short_name (8.3, dot-formatted, e.g. "HELLO.TXT") should be used instead.
	 */
	typedef struct {
		fat_dirent_t raw;                           /* raw short entry, as on disk */
		char short_name[13];                        /* "HELLO.TXT" form, null terminated */
		char long_name[FAT_LFN_MAX_NAME_CHARS + 1]; /* UTF-8, empty if none/invalid */
		uint32_t first_cluster;                     /* high<<16 | low, convenience */
	} fat_resolved_dirent_t;

	/* A growable list of resolved directory entries. */
	typedef struct {
		fat_resolved_dirent_t* entries;
		size_t count;
		size_t capacity;
	} fat_dirent_list_t;

	void fat_dirent_list_init(fat_dirent_list_t* list);
	void fat_dirent_list_free(fat_dirent_list_t* list);

	/*
	 * Looks up a single name (case-insensitive) within an already-populated listing, matching against long_name first, then short_name.
	 * Returns a pointer into out_list's storage (do not free independently), or NULL if not found.
	 */
	const fat_resolved_dirent_t* fat_dirent_list_find(const fat_dirent_list_t* list, const char* name);

	typedef enum {
		FAT_TYPE_UNKNOWN = 0,
		FAT_TYPE_FAT12,
		FAT_TYPE_FAT16,
		FAT_TYPE_FAT32,
		FAT_TYPE_EXFAT
	} fat_type_t;

	fat_type_t get_fat_type(const uint8_t* sector_buf);
	const char* print_fat_type(fat_type_t type);

	typedef enum {
		FAT_LOOKUP_OK = 0,
		FAT_LOOKUP_NOT_FOUND,
		FAT_LOOKUP_WRONG_TYPE,    /* found it, but file vs directory mismatch */
		FAT_LOOKUP_IO_ERROR,
		FAT_LOOKUP_BAD_PATH       /* empty component, e.g. "FOO//BAR" or "" */
	} fat_lookup_status_t;

/* Family-specific headers */
#include <filesystem/fat/fat1216.h>
#include <filesystem/fat/fat32.h>
#include <filesystem/fat/fat_exfat.h>

#ifdef __cplusplus
}
#endif
#endif //WALLOS_FAT_H
