/**
 * @file wallos_mbr.h
 * @author Malcolm
 * @brief Parsing/writing utilities for classic MBR (Master Boot Record) partition tables.
 * @version
 * @date 7/1/2026
 */
#ifndef WDM_MOCK_WALLOS_MBR_H
#define WDM_MOCK_WALLOS_MBR_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Well-known MBR partition type ID bytes.
 *
 * There is no standard for this. Everyone kinda just chose a random one and stuck to it.
 * This is the only good spot with info on it: https://aeb.win.tue.nl/partitions/partition_types-1.html
 * I tried to keep a balance of common and legacy types, mostly trying to keep it to ones you might actually come across.
 * I don't really know where this tool will be used, I am developing it in a way that it should be easy to port.
 */
    typedef enum {
        EMPTY = 0x00, /**< No partition / unused entry. */
        DOS_FAT12 = 0x01, /**< DOS FAT12. */
        XENIX_ROOT = 0x02, /**< XENIX root filesystem. */
        XENIX_USER = 0x03, /**< XENIX user filesystem. */
        DOS_3_FAT16 = 0x04, /**< DOS 3.x FAT16, partitions smaller than 32 MB. */
        DOS_EXTENDED = 0x05, /**< DOS extended partition (CHS). */
        DOS_3_31_FAT16 = 0x06, /**< DOS 3.31+ FAT16. */

        /* A ton of filesystems decided on 0x07... */
        OS2_IFS = 0x07, /**< OS/2 "installable file system", usually HPFS. */
        HPFS = 0x07, /**< Alias of OS2_IFS: High Performance File System. */
        WINDOWS_NTFS = 0x07, /**< Alias of OS2_IFS: NTFS. */
        EXFAT = 0x07, /**< Alias of OS2_IFS: exFAT. */
        OS2_V1_v1_3 = 0x08, /**< OS/2 v1.0-1.3 or AIX boot partition. */
        AIX_BOOT = 0x08, /**< Alias of OS2_V1_v1_3: AIX boot partition. */
        AIX_DATA = 0x09, /**< AIX data partition. */
        OS2_BOOT_MANAGER = 0x0A, /**< OS/2 Boot Manager. */
        FAT32_CHS = 0x0B, /**< FAT32, CHS addressing. */
        FAT32_LBA = 0x0C, /**< FAT32, LBA addressing. */
        FAT16_LBA = 0x0E, /**< FAT16, LBA addressing. */
        EXTENDED_LBA = 0x0F, /**< Extended partition, LBA addressing. */

        HIDDEN_FAT12 = 0x11, /**< Hidden DOS FAT12. */
        COMPAQ_DIAGNOSTICS = 0x12, /**< Compaq diagnostics partition. */
        HIDDEN_FAT16_SMALL = 0x14, /**< Hidden FAT16, partitions smaller than 32 MB. */
        HIDDEN_FAT16 = 0x16, /**< Hidden FAT16. */
        HIDDEN_NTFS = 0x17, /**< Hidden NTFS/HPFS. */
        HIDDEN_FAT32 = 0x1B, /**< Hidden FAT32, CHS addressing. */
        HIDDEN_FAT32_LBA = 0x1C, /**< Hidden FAT32, LBA addressing. */
        HIDDEN_FAT16_LBA = 0x1E, /**< Hidden FAT16, LBA addressing. */

        NEC_DOS = 0x24, /**< NEC DOS. */
        HIDDEN_WINDOWS_RE = 0x27, /**< Hidden Windows Recovery Environment (NTFS). GPT GUID: DE94BBA4-06D1-4D40-A16A-BFD50179D6AC */
        OS2_ECS_JFS = 0x35, /**< OS/2 eComStation JFS. */
        PLAN9 = 0x39, /**< Plan 9. */
        VENIX = 0x40, /**< VENIX 80286. */
        CPM = 0x52, /**< CP/M. */
        UNIX_SYSTEM_V = 0x63, /**< UNIX System V. */

        LINUX_MINIX = 0x80, /**< Linux/Minix (modern). */
        LINUX_MINIX_OLD = 0x81, /**< Linux/Minix (old). */
        LINUX_SWAP = 0x82, /**< Linux swap partition. */
        LINUX_FILESYSTEM = 0x83, /**< Linux native filesystem. */
        LINUX_EXTENDED = 0x85, /**< Linux extended partition. */
        LINUX_RAID_OLD = 0x86, /**< Linux RAID (old, superblock v0.90). */
        LINUX_LVM = 0x8E, /**< Linux LVM. */

        FREEBSD = 0xA5, /**< FreeBSD slice. */
        OPENBSD = 0xA6, /**< OpenBSD slice. */
        NEXTSTEP = 0xA7, /**< NeXTSTEP. */
        NETBSD = 0xA9, /**< NetBSD slice. */

        APPLE_BOOT = 0xAB, /**< Apple boot partition. */
        APPLE_HFS = 0xAF, /**< Apple HFS/HFS+. */

        SOLARIS_X86 = 0xBF, /**< Solaris x86. */

        DRDOS_FAT16 = 0xC4, /**< DR-DOS FAT16 (secured). */
        DRDOS_FAT32 = 0xC6, /**< DR-DOS FAT32 (secured), CHS addressing. */
        DRDOS_FAT32_LBA = 0xC7, /**< DR-DOS FAT32 (secured), LBA addressing. */

        RUFUS_EXTRA_PART = 0xEA, /**< Rufus extra partition. */
        BEOS_BFS = 0xEB, /**< BeOS BFS. */

        GPT_PROTECTIVE_MBR = 0xEE, /**< GPT protective MBR marker. */
        EFI_SYSTEM_MBR = 0xEF, /**< EFI system partition (in an MBR context). */

        BOCHS = 0xFA, /**< Bochs. */
        VMWARE_VMFS = 0xFB, /**< VMware VMFS. */
        VMWARE_SWAP = 0xFC, /**< VMware swap. */
        LINUX_RAID = 0xFD, /**< Linux RAID (autodetect). */
    } partition_type_t;

    /**
     * @brief A single 16-byte MBR partition table entry.
     *
     * Mirrors the on-disk layout of one of the four primary partition entries found at
     * offsets 0x1BE, 0x1CE, 0x1DE, and 0x1EE in the boot sector.
     */
    typedef struct {
        uint8_t drive_attributes;   /**< Boot/status flag byte. Bit 7 set means active/bootable. */
        uint32_t chs_address;       /**< First sector CHS address. Only the low 24 bits are meaningful. */
        uint8_t partition_type;     /**< Partition type ID byte. See partition_type_t (non-standard, see enum docs). */
        uint32_t chs_address_last;  /**< Last sector CHS address. Only the low 24 bits are meaningful. */
        uint32_t lba_start;         /**< LBA of the first sector of the partition. */
        uint32_t sector_count;      /**< Number of sectors in the partition. */
    } mbr_partition_entry_t;

    /**
     * @brief Full 512-byte MBR sector.
     *
     * This does not store the 440/446-byte bootstrap code region.
     * See write_mbr_bootstrap() for writing that portion separately.
     */
    typedef struct {
        uint32_t signature;                     /**< Disk signature at offset 0x1B8. */
        uint16_t reserved_bytes;                /**< Reserved bytes at offset 0x1BC. Some systems set this to 0x5A5A to mean read-only. */
        // mbr_partition_entry_t first_entry;      /**< Partition entry 1 (offset 0x1BE). */
        // mbr_partition_entry_t second_entry;     /**< Partition entry 2 (offset 0x1CE). */
        // mbr_partition_entry_t third_entry;      /**< Partition entry 3 (offset 0x1DE). */
        // mbr_partition_entry_t fourth_entry;     /**< Partition entry 4 (offset 0x1EE). */

        union {
            struct {
                mbr_partition_entry_t first_entry;
                mbr_partition_entry_t second_entry;
                mbr_partition_entry_t third_entry;
                mbr_partition_entry_t fourth_entry;
            };

            mbr_partition_entry_t partition_entries[4];
        };
        uint16_t sig_bytes;                     /**< Boot signature at offset 0x1FE. Must be 0xAA55 to be valid. */
    } mbr_partition_table_t;

    /**
     * @brief Parse the MBR at LBA 0.
     *
     * Reads the disk signature, reserved bytes, all four primary partition entries, and the boot signature out of a raw 512-byte sector buffer.
     * If the boot signature is not 0xAA55, a warning is printed to stderr but parsing still proceeds with whatever was read.
     * This will ignore anything past 512 bytes if the sector is larger.
     *
     * @param mbr_table Pointer to the table where we load the MBR. Must not be NULL.
     * @param buf Buffer of LBA 0. Must not be NULL.
     * @param len Length of the buffer. Expected to be at least 512 bytes.
     *            The function returns without doing anything if it is shorter.
     */
    void parse_mbr(mbr_partition_table_t* mbr_table, const uint8_t* buf, size_t len);

    /**
     * @brief Parse a single 16-byte MBR partition entry out of a buffer.
     *
     * This is intended to be an internal function, but is provided to the API in case it's needed.
     *
     * @param entry Pointer to entry to be saved. Must not be NULL.
     * @param buf Buffer containing the 16 bytes of the partition entry. NOT THE ENTIRE SECTOR. Must not be NULL.
     * @param offset Byte offset within @p buf at which the 16-byte entry begins.
     */
    void parse_mbr_partition(mbr_partition_entry_t* entry, const uint8_t* buf, const size_t offset);

    /**
     * @brief Get a human-readable name for an MBR partition type ID byte.
     * @param type The raw partition type ID byte (see partition_type_t).
     * @return A short, static, human-readable description of the partition type. Returns "Unknown" for any ID not explicitly recognized.
     */
    const char* partition_type_tostring(uint8_t type);

    /**
     * @brief Print a full MBR partition table to stdout in a human-readable format.
     * @param mbr The table to print. If NULL, the function returns without printing anything.
     */
    void print_mbr(const mbr_partition_table_t* mbr);

    /**
     * @brief Serialize a single partition entry into its 16-byte on-disk representation.
     *
     * This should only really be called internally from write_mbr().
     * This is provided publicly to allow changes to only a partition entry.
     *
     * @param entry Entry to serialize. If NULL, the function returns without writing anything.
     * @param buf Destination buffer for the 16-byte entry. Must have at least 16 bytes available.
     *            If NULL, the function returns without writing anything.
     */
    void write_mbr_partition(const mbr_partition_entry_t* entry, uint8_t* buf);

    /**
     * @brief Serialize an MBR partition table into a raw 512-byte sector buffer.
     *
     * Writes the disk signature, reserved bytes, all four primary partition entries, and the boot signature at their standard offsets.
     * Does not touch the bootstrap code region. Use write_mbr_bootstrap() to fill that in separately.
     *
     * @param mbr Table to serialize. If NULL, the function returns without writing anything.
     * @param buf Destination buffer. If NULL, the function returns without writing anything.
     * @param buf_size Size of @p buf in bytes. Must be at least 512, or the function returns without writing anything.
     */
    void write_mbr(const mbr_partition_table_t* mbr, uint8_t* buf, size_t buf_size);

    /**
     * @brief Write bootstrap (bootloader) code into the start of an MBR sector buffer.
     *
     * @param bootstrap_code Buffer containing the bootstrap code to copy in.
     *                        If NULL, the function returns without writing anything.
     * @param bootstrap_code_size Size of @p bootstrap_code in bytes.
     *                            Must be at most 446 (technically only 440 bytes are guaranteed usable.
     *                            Some bootstrap code omits the signature/reserved bytes for a few extra instructions).
     *                            If larger, the function returns without writing anything.
     * @param buf Destination sector buffer. If NULL, the function returns without writing anything.
     * @param buf_size Size of @p buf in bytes. Must be at least 512, or the function returns without writing anything.
     */
    void write_mbr_bootstrap(const uint8_t* bootstrap_code, size_t bootstrap_code_size, uint8_t* buf, size_t buf_size);


    void print_mbr_partition(const mbr_partition_entry_t* entry, int index);
    void print_mbr(const mbr_partition_table_t* mbr);
    /**
     * @brief Option flags controlling how mbr_construct() builds a fresh MBR.
     */
    typedef enum {
        MBR_CONSTRUCT_STANDARD = 0,        /**< No special behavior. Build a plain, blank MBR. */
        MBR_CONSTRUCT_GPT = (1 << 0), /**< Write a GPT protective partition entry into the first entry slot. */
        MBR_CONSTRUCT_RANDOM_SIG = (1 << 1), /**< Generate a random disk signature instead of leaving it zeroed. Not yet implemented. */
        MBR_CONSTRUCT_READ_ONLY = (1 << 3), /**< Mark the disk read-only by setting the reserved bytes field to 0x5A5A. Takes precedence over MBR_CONSTRUCT_ZERO_RESERVED if both are set. */
    } mbr_constructor_flags_t;

    /**
     * @brief Option flags controlling how mbr_partition_construct() fills in fields.
     */
    typedef enum {
        MBR_PARTITION_CONSTRUCT_STANDARD = 0,        /**< Not bootable. @p start/@p count are LBA start/sector count only, CHS fields get LBA-only placeholders (0x000200 / 0xFFFFFF). */
        MBR_PARTITION_CONSTRUCT_USE_CHS = (1 << 0), /**< Write @p start/@p count into the CHS start/end fields (masked to 24 bits) instead of using the LBA-only placeholders. */
        MBR_PARTITION_CONSTRUCT_BOOTABLE = (1 << 1), /**< Mark the partition bootable/active. */
    } mbr_partition_constructor_flags_t;

    /**
     * @brief Build a fresh, blank MBR partition table in memory.
     *
     * Zeroes @p mbr, sets the boot signature to the required 0xAA55, and leaves the disk signature and reserved bytes at their defaults unless overridden by @p flags.
     *
     * @param mbr Table to populate. If NULL, the function returns without doing anything.
     * @param flags Bitwise-OR of mbr_constructor_flags_t options controlling construction. Pass MBR_CONSTRUCT_STANDARD for default behavior.
     *
     * @note If @p flags includes MBR_CONSTRUCT_GPT, the first partition entry is overwritten with a GPT protective partition (type GPT_PROTECTIVE_MBR, LBA start 1, sector count 0xFFFFFFFF).
     *       The caller is expected to shrink the sector count afterward to match the actual disk size.
     */
    void mbr_construct(mbr_partition_table_t* mbr, mbr_constructor_flags_t flags);

    /**
     * @brief Populate a partition entry with sane defaults.
     *
     * Zeroes @p entry, then fills in the bootable flag, partition type, LBA start/count, and CHS fields.
     *
     * @param entry Entry to populate. If NULL, the function returns without doing anything.
     * @param type Partition type ID to assign (see partition_type_t).
     * @param start LBA of the first sector of the partition. If @p flags includes MBR_PARTITION_CONSTRUCT_USE_CHS, this value is also written (masked to 24 bits) into the CHS start field.
     * @param count Number of sectors in the partition. If @p flags includes MBR_PARTITION_CONSTRUCT_USE_CHS, this value is also written (masked to 24 bits) into the CHS end field.
     * @param flags Bitwise-OR of ::mbr_partition_constructor_flags_t options. Pass MBR_PARTITION_CONSTRUCT_STANDARD for a non-bootable, LBA-only entry.
     */
    void mbr_partition_construct(mbr_partition_entry_t* entry, partition_type_t type, uint32_t start, uint32_t count, mbr_partition_constructor_flags_t flags);

#ifdef __cplusplus
}
#endif

#endif //WDM_MOCK_WALLOS_MBR_H