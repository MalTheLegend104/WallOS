/**
 * @file wallos_mbr.c
 * @author Malcolm
 * @brief Implementation of MBR partition table parsing/writing helpers.
 * @version
 * @date 7/1/2026
 */
#include <filesystem/partitions/wallos_mbr.h>

#include <stdio.h>
#include <string.h>

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Read/Write helpers
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/**
 * @brief Read a little-endian 32-bit value out of a buffer.
 * @param buf Source buffer.
 * @param offset Byte offset to read from.
 * @return The decoded 32-bit value.
 */
static inline uint32_t read32_mbr(const uint8_t* buf, const size_t offset) {
    return (uint32_t) buf[offset] |
        ((uint32_t) buf[offset + 1] << 8) |
        ((uint32_t) buf[offset + 2] << 16) |
        ((uint32_t) buf[offset + 3] << 24);
}

/**
 * @brief Read a little-endian 24-bit value out of a buffer, zero-extended to 32 bits.
 * @param buf Source buffer.
 * @param offset Byte offset to read from.
 * @return The decoded 24-bit value in the low 3 bytes of a uint32_t. The top byte is always 0.
 */
static inline uint32_t read24_mbr(const uint8_t* buf, const size_t offset) {
    return (uint32_t) buf[offset] |
        ((uint32_t) buf[offset + 1] << 8) |
        ((uint32_t) buf[offset + 2] << 16) |
        ((uint32_t) 0x00 << 24);
}

/**
 * @brief Read a little-endian 16-bit value out of a buffer.
 * @param buf Source buffer.
 * @param offset Byte offset to read from.
 * @return The decoded 16-bit value.
 */
static inline uint16_t read16_mbr(const uint8_t* buf, const size_t offset) {
    return (uint16_t) buf[offset] | ((uint16_t) buf[offset + 1] << 8);
}

/**
 * @brief Write a 32-bit value into a buffer in little-endian byte order.
 * @param buf Destination buffer. Must have at least 4 bytes available.
 * @param value Value to write.
 */
static inline void write32_LSB(uint8_t* buf, uint32_t value) {
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
    buf[2] = (value >> 16) & 0xFF;
    buf[3] = (value >> 24) & 0xFF;
}

/**
 * @brief Write the low 24 bits of a value into a buffer in little-endian byte order.
 * @param buf Destination buffer. Must have at least 3 bytes available.
 * @param value Value whose low 24 bits are written. The top 8 bits are ignored.
 */
static inline void write24_LSB(uint8_t* buf, uint32_t value) {
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
    buf[2] = (value >> 16) & 0xFF;
}

/**
 * @brief Write a 16-bit value into a buffer in little-endian byte order.
 * @param buf Destination buffer. Must have at least 2 bytes available.
 * @param value Value to write.
 */
static inline void write16_LSB(uint8_t* buf, uint16_t value) {
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
}

void parse_mbr_partition(mbr_partition_entry_t* entry, const uint8_t* buf, const size_t offset) {
    if (!entry || !buf) return; // null protection

    // We take the rest of the params as given, assuming they are correct...
    // The MBR partition entry is only 16 bytes total
    entry->drive_attributes = buf[offset];
    entry->chs_address = read24_mbr(buf, offset + 0x01);
    entry->partition_type = buf[offset + 0x04];
    entry->chs_address_last = read24_mbr(buf, offset + 0x05);
    entry->lba_start = read32_mbr(buf, offset + 0x08);
    entry->sector_count = read32_mbr(buf, offset + 0x0C);
}

void parse_mbr(mbr_partition_table_t* mbr_table, const uint8_t* buf, size_t len) {
    if (!mbr_table || !buf) return;
    if (len < 512) return;

    mbr_table->signature = read32_mbr(buf, 0x1B8);
    mbr_table->reserved_bytes = read16_mbr(buf, 0x1BC);
    parse_mbr_partition(&(mbr_table->first_entry), buf, 0x1BE);
    parse_mbr_partition(&(mbr_table->second_entry), buf, 0x1CE);
    parse_mbr_partition(&(mbr_table->third_entry), buf, 0x1DE);
    parse_mbr_partition(&(mbr_table->fourth_entry), buf, 0x1EE);

    mbr_table->sig_bytes = read16_mbr(buf, 0x1FE);

    if (mbr_table->sig_bytes != 0xAA55) {
        // fprintf(stderr, "Bad signature\n");
    }
}

const char* partition_type_tostring(uint8_t type) {
    switch (type) {
        case EMPTY:                  return "Empty";
        case DOS_FAT12:              return "DOS FAT12";
        case XENIX_ROOT:             return "XENIX Root";
        case XENIX_USER:             return "XENIX User";
        case DOS_3_FAT16:            return "DOS FAT16 (<32 MB)";
        case DOS_EXTENDED:           return "DOS Extended";
        case DOS_3_31_FAT16:         return "DOS FAT16";

        case OS2_IFS:                return "HPFS / NTFS / exFAT";
        case OS2_V1_v1_3:            return "OS/2 or AIX Boot";
        case AIX_DATA:               return "AIX Data";
        case OS2_BOOT_MANAGER:       return "OS/2 Boot Manager";

        case FAT32_CHS:              return "FAT32 (CHS)";
        case FAT32_LBA:              return "FAT32 (LBA)";
        case FAT16_LBA:              return "FAT16 (LBA)";
        case EXTENDED_LBA:           return "Extended (LBA)";

        case HIDDEN_FAT12:           return "Hidden FAT12";
        case COMPAQ_DIAGNOSTICS:     return "Compaq Diagnostics";
        case HIDDEN_FAT16_SMALL:     return "Hidden FAT16 (<32 MB)";
        case HIDDEN_FAT16:           return "Hidden FAT16";
        case HIDDEN_NTFS:            return "Hidden NTFS";
        case HIDDEN_FAT32:           return "Hidden FAT32";
        case HIDDEN_FAT32_LBA:       return "Hidden FAT32 (LBA)";
        case HIDDEN_FAT16_LBA:       return "Hidden FAT16 (LBA)";

        case NEC_DOS:                return "NEC DOS";
        case HIDDEN_WINDOWS_RE:      return "Hidden Windows Recovery";
        case OS2_ECS_JFS:            return "OS/2 eCS JFS";
        case PLAN9:                  return "Plan 9";
        case VENIX:                  return "VENIX";
        case CPM:                    return "CP/M";
        case UNIX_SYSTEM_V:          return "UNIX System V";

        case LINUX_MINIX:            return "Linux/Minix";
        case LINUX_MINIX_OLD:        return "Linux/Minix (Old)";
        case LINUX_SWAP:             return "Linux Swap";
        case LINUX_FILESYSTEM:       return "Linux Filesystem";
        case LINUX_EXTENDED:         return "Linux Extended";
        case LINUX_RAID_OLD:         return "Linux RAID (Old)";
        case LINUX_LVM:              return "Linux LVM";

        case FREEBSD:                return "FreeBSD";
        case OPENBSD:                return "OpenBSD";
        case NEXTSTEP:               return "NeXTSTEP";
        case NETBSD:                 return "NetBSD";

        case APPLE_BOOT:             return "Apple Boot";
        case APPLE_HFS:              return "Apple HFS/HFS+";

        case SOLARIS_X86:            return "Solaris x86";

        case DRDOS_FAT16:            return "DR-DOS FAT16";
        case DRDOS_FAT32:            return "DR-DOS FAT32";
        case DRDOS_FAT32_LBA:        return "DR-DOS FAT32 (LBA)";

        case RUFUS_EXTRA_PART:       return "Rufus Extra Partition";
        case BEOS_BFS:               return "BeOS BFS";

        case GPT_PROTECTIVE_MBR:     return "GPT Protective MBR";
        case EFI_SYSTEM_MBR:         return "EFI System Partition";

        case BOCHS:                  return "Bochs";
        case VMWARE_VMFS:            return "VMware VMFS";
        case VMWARE_SWAP:            return "VMware Swap";
        case LINUX_RAID:             return "Linux RAID";

        default:                     return "Unknown";
    }
}

/**
 * @brief Print a single partition entry to stdout in a human-readable format.
 * @param entry The entry to print. If NULL, the function returns without printing anything.
 * @param index Display index (e.g. 1-4) printed alongside the entry's fields.
 */
void print_mbr_partition(const mbr_partition_entry_t* entry, int index) {
    if (!entry) return;

    printf("Partition %d\n", index);
    printf("  Bootable         : %s (0x%02X)\n", (entry->drive_attributes & 0x80) ? "Yes" : "No", entry->drive_attributes);
    printf("  Partition Type   : 0x%02X (%s)\n", entry->partition_type, partition_type_tostring(entry->partition_type));
    printf("  CHS Start (raw)  : 0x%06X\n", entry->chs_address);
    printf("  CHS End (raw)    : 0x%06X\n", entry->chs_address_last);
    printf("  LBA Start        : %u (0x%08X)\n", entry->lba_start, entry->lba_start);
    printf("  Sector Count     : %u (0x%08X)\n", entry->sector_count, entry->sector_count);
    printf("  LBA End          : %u (0x%08X)\n", entry->lba_start + entry->sector_count - 1, entry->lba_start + entry->sector_count - 1);
    printf("\n");
}

void print_mbr(const mbr_partition_table_t* mbr) {
    if (!mbr) return;

    printf("Master Boot Record\n");
    printf("------------------\n");
    printf("Disk Signature : 0x%08X\n", mbr->signature);
    printf("Boot Signature : 0x%04X (%s)\n", mbr->sig_bytes, (mbr->sig_bytes == 0xAA55) ? "Valid" : "Invalid");
    printf("Reserved       : 0x%04X %s\n\n", mbr->reserved_bytes, mbr->reserved_bytes == 0x5A5A ? "(Read Only)" : "");

    print_mbr_partition(&mbr->first_entry, 1);
    print_mbr_partition(&mbr->second_entry, 2);
    print_mbr_partition(&mbr->third_entry, 3);
    print_mbr_partition(&mbr->fourth_entry, 4);
}

void write_mbr_partition(const mbr_partition_entry_t* entry, uint8_t* buf) {
    // This should only really be called internally from write_mbr
    // This *is* exposed globally, so we still check buf & entry
    if (!entry || !buf) return;

    buf[0] = entry->drive_attributes;
    write24_LSB(buf + 0x1, entry->chs_address);
    buf[4] = entry->partition_type;
    write24_LSB(buf + 0x5, entry->chs_address_last);
    write32_LSB(buf + 0x8, entry->lba_start);
    write32_LSB(buf + 0xC, entry->sector_count);
}

void write_mbr(const mbr_partition_table_t* mbr, uint8_t* buf, size_t buf_size) {
    if (!mbr || !buf) return;
    if (buf_size < 512) return;

    memset(buf, 0, buf_size); // we can't blindly trust that the buffer is properly zero'd

    write32_LSB(buf + 0x1B8, mbr->signature);
    write16_LSB(buf + 0x1BC, mbr->reserved_bytes);
    write_mbr_partition(&mbr->first_entry, buf + 0x1BE);
    write_mbr_partition(&mbr->second_entry, buf + 0x1CE);
    write_mbr_partition(&mbr->third_entry, buf + 0x1DE);
    write_mbr_partition(&mbr->fourth_entry, buf + 0x1EE);
    write16_LSB(buf + 0x1FE, mbr->sig_bytes);
}

void write_mbr_bootstrap(const uint8_t* bootstrap_code, size_t bootstrap_code_size, uint8_t* buf, size_t buf_size) {
    if (!bootstrap_code || !buf) return;
    // This is technically supposed to only be 440 bytes
    // Some bootstrap code may omit the signature and reserved bytes for a few extra instructions
    if (bootstrap_code_size > 446) return;
    if (buf_size < 512) return;

    // We don't overwrite the next 6 bytes, since we are likely calling this AFTER we call write_mbr
    // We don't want to overwrite the signature and reserved bytes if present
    memset(buf, 0, 440);
    memcpy(buf, bootstrap_code, bootstrap_code_size);
}