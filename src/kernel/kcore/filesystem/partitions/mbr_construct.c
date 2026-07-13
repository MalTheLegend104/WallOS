#include <stdbool.h>
#include <string.h>
#include <filesystem/partitions/wallos_mbr.h>
#include <klibc/kernel_rng.h>

void mbr_partition_construct(mbr_partition_entry_t* entry, partition_type_t type, uint32_t start, uint32_t count, mbr_partition_constructor_flags_t flags) {
	if (!entry) return;

	memset(entry, 0, sizeof(*entry));

	entry->drive_attributes = (flags & MBR_PARTITION_CONSTRUCT_BOOTABLE) ? 0x80 : 0x00;

	if (flags & MBR_PARTITION_CONSTRUCT_USE_CHS) {
		// Use start/count as CHS values. Only the low 24 bits are meaningful.
		entry->chs_address = start & 0xFFFFFF;
		entry->chs_address_last = count & 0xFFFFFF;
	} else {
		// Placeholder CHS values for LBA-only systems.
		entry->chs_address = 0x000200;
		entry->chs_address_last = 0xFFFFFF;
	}

	entry->partition_type = type;
	entry->lba_start = start;
	entry->sector_count = count;
}

void mbr_partition_clear(mbr_partition_entry_t* entry) {
	if (!entry) return;
	memset(entry, 0, sizeof(*entry));
}

void mbr_construct(mbr_partition_table_t* mbr, mbr_constructor_flags_t flags) {
	if (!mbr) return;

	memset(mbr, 0, sizeof(*mbr));

	mbr->signature = (uint32_t) rng_next();

	if (flags & MBR_CONSTRUCT_READ_ONLY) {
		mbr->reserved_bytes = 0x5A5A;
	} else {
		mbr->reserved_bytes = 0x0000;
	}

	// Every valid MBR ends with 0xAA55.
	mbr->sig_bytes = 0xAA55;

	if (flags & MBR_CONSTRUCT_GPT) {
		mbr_partition_entry_t* protective = &mbr->first_entry;

		protective->drive_attributes = 0x00;

		// Legacy CHS values indicating "use LBA".
		protective->chs_address = 0x000200;
		protective->chs_address_last = 0xFFFFFF;

		protective->partition_type = GPT_PROTECTIVE_MBR;

		// GPT always begins at LBA 1.
		protective->lba_start = 1;

		// The caller should set this to the proper value
		protective->sector_count = 0xFFFFFFFF;
	}
}