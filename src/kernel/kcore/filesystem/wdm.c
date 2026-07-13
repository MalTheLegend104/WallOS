/**
 * Wallos WDM (WallosOS Disk Manager) implementation.
 *
 * @note This is NOT thread safe. The registry should have spinlocks.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <filesystem/wdm.h>
#include <memory/virtual_mem.h>
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Registry
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/** Maximum number of concurrently registered drives. */
#ifndef WDM_MAX_DRIVES
#define WDM_MAX_DRIVES 32
#endif

/** Internal representation of a registered drive. */
struct WDM_Drive {
	bool active;
	const WDM_DriverOps* ops;
	void* ctx;
	WDM_DriveInfo info;
	struct WDM_Drive* parent; // NULL if this is a physical / root drive

	// Metadata caching
	bool has_metadata;
	WDM_PartitionMeta meta;
};

// The registry is statically allocated.
// This has both advantages and disadvantages.
// I dont see this being a problem (32 drives is plenty), but the cap could in theory be a problem.
static struct WDM_Drive wdm_drives[WDM_MAX_DRIVES];
static bool             wdm_initialized = false;

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Internal helpers
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static bool validate_handle(WDM_DriveHandle handle) {
	if (!handle) {
		return false;
	}

	/* Confirm the pointer falls within the static array. */
	if (handle < wdm_drives || handle >= wdm_drives + WDM_MAX_DRIVES) {
		return false;
	}

	return handle->active;
}

static WDM_Status flush_drive(struct WDM_Drive* d) {
	if (!d->ops->flush) {
		return WDM_OK;
	}

	WDM_Status st = d->ops->flush(d->ctx);

	if (st == WDM_ERR_UNSUPPORTED) {
		return WDM_OK;
	}

	return st;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

WDM_Status WDM_Init(void) {
	if (wdm_initialized) {
		return WDM_OK;
	}

	memset(wdm_drives, 0, sizeof(wdm_drives));
	wdm_initialized = true;
	return WDM_OK;
}

void WDM_Shutdown(void) {
	if (!wdm_initialized) {
		return;
	}

	for (int i = 0; i < WDM_MAX_DRIVES; i++) {
		struct WDM_Drive* d = &wdm_drives[i];

		if (!d->active) {
			continue;
		}

		// This is our attempt at being nice
		// we dont care about error state because if we're shutting down something went wrong
		// (or we've already shutdown VFS which flushed everything anyway)
		flush_drive(d);

		if (d->ops->on_detach) {
			d->ops->on_detach(d->ctx);
		}

		memset(d, 0, sizeof(*d));
	}

	wdm_initialized = false;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Registration
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

WDM_Status WDM_Register(const WDM_DriverOps* ops, void* ctx, const WDM_DriveInfo* info, WDM_DriveHandle* out) {
	if (!ops || !info || !out) {
		return WDM_ERR_INVALID;
	}

	/* read and write are mandatory. */
	if (!ops->read || !ops->write) {
		return WDM_ERR_INVALID;
	}

	/* Find a kfree slot. */
	struct WDM_Drive* slot = NULL;

	for (int i = 0; i < WDM_MAX_DRIVES; i++) {
		if (!wdm_drives[i].active) {
			slot = &wdm_drives[i];
			break;
		}
	}

	if (!slot) {
		/* Registry is full. */
		return WDM_ERR_BUSY;
	}

	slot->ops = ops;
	slot->ctx = ctx;
	slot->info = *info;
	slot->parent = NULL;

	if (ops->on_attach) {
		WDM_Status st = ops->on_attach(ctx);

		if (st != WDM_OK) {
			/* Attachment failed, we leave slot zeroed. */
			memset(slot, 0, sizeof(*slot));
			return WDM_ERR_IO;
		}
	}

	slot->active = true;
	*out = slot;
	return WDM_OK;
}

WDM_Status WDM_RegisterPartition(WDM_DriveHandle parent, const WDM_DriverOps* ops, void* ctx, const WDM_DriveInfo* info, WDM_DriveHandle* out) {
	if (!validate_handle(parent) || !ops || !info || !out) {
		return WDM_ERR_INVALID;
	}

	/* read and write are mandatory. */
	if (!ops->read || !ops->write) {
		return WDM_ERR_INVALID;
	}

	/* Find a kfree slot. */
	struct WDM_Drive* slot = NULL;
	for (int i = 0; i < WDM_MAX_DRIVES; i++) {
		if (!wdm_drives[i].active) {
			slot = &wdm_drives[i];
			break;
		}
	}

	if (!slot) {
		return WDM_ERR_BUSY;
	}

	slot->ops = ops;
	slot->ctx = ctx;
	slot->info = *info;
	slot->parent = parent;

	if (ops->on_attach) {
		WDM_Status st = ops->on_attach(ctx);
		if (st != WDM_OK) {
			memset(slot, 0, sizeof(*slot));
			return WDM_ERR_IO;
		}
	}

	slot->active = true;
	*out = slot;
	return WDM_OK;
}

WDM_Status WDM_Unregister(WDM_DriveHandle handle) {
	if (!validate_handle(handle)) {
		return WDM_ERR_NOT_FOUND;
	}

	/* Recursively unregister any child partitions belonging to this parent drive */
	for (int i = 0; i < WDM_MAX_DRIVES; i++) {
		if (wdm_drives[i].active && wdm_drives[i].parent == handle) {
			WDM_Unregister(&wdm_drives[i]);
		}
	}

	WDM_Status flush_st = flush_drive(handle);

	if (handle->ops->on_detach) {
		handle->ops->on_detach(handle->ctx);
	}

	memset(handle, 0, sizeof(*handle));

	/* Return flush errors after completing teardown so the slot is always cleaned up regardless of I/O health. */
	return flush_st;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Enumeration and info
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

WDM_Status WDM_Enumerate(WDM_DriveHandle* handles, uint32_t max, uint32_t* total) {
	if (!total) {
		return WDM_ERR_INVALID;
	}

	uint32_t count = 0;

	for (int i = 0; i < WDM_MAX_DRIVES; i++) {
		if (!wdm_drives[i].active) {
			continue;
		}

		if (handles && count < max) {
			handles[count] = &wdm_drives[i];
		}

		count++;
	}

	*total = count;
	return WDM_OK;
}

WDM_Status WDM_GetInfo(WDM_DriveHandle handle, WDM_DriveInfo* out) {
	if (!validate_handle(handle) || !out) {
		return WDM_ERR_NOT_FOUND;
	}

	*out = handle->info;
	return WDM_OK;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// I/O helpers
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

WDM_Status check_io_args(WDM_DriveHandle handle, WDM_LBA lba, uint32_t count, const void* buf, bool check_buf) {
	if (!validate_handle(handle)) {
		return WDM_ERR_INVALID;
	}

	if (check_buf && !buf) {
		return WDM_ERR_INVALID;
	}

	// Zero-length transfer
	// nothing to do, not an error
	if (count == 0) return WDM_OK;

	// Check for overflow
	// ensure lba + count does not wrap and does not exceed the drive's sector_count
	if (lba >= handle->info.sector_count) {
		return WDM_ERR_OVERFLOW;
	}

	uint64_t end = lba + (uint64_t) count;

	if (end > handle->info.sector_count) {
		return WDM_ERR_OVERFLOW;
	}

	return WDM_OK;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Read / Write / Trim / Flush
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

WDM_Status WDM_Read(WDM_DriveHandle handle, WDM_LBA lba, uint32_t count, void* buf, WDM_IOFlags flags) {
	WDM_Status st = check_io_args(handle, lba, count, buf, true);
	if (st != WDM_OK) return st;
	if (count == 0) return WDM_OK;

	if (!handle->info.dma_capable && (flags & WDM_FLAG_DMA)) {
		flags = (WDM_IOFlags) (flags & ~WDM_FLAG_DMA);
	}

	return handle->ops->read(handle->ctx, lba, count, buf, flags);
}

WDM_Status WDM_Write(WDM_DriveHandle handle, WDM_LBA lba, uint32_t count, const void* buf, WDM_IOFlags flags) {
	WDM_Status st = check_io_args(handle, lba, count, buf, true);

	if (st != WDM_OK) return st;
	if (handle->info.read_only) return WDM_ERR_WRITE_PROT;
	if (count == 0) return WDM_OK;


	if (!handle->info.dma_capable && (flags & WDM_FLAG_DMA)) {
		flags = (WDM_IOFlags) (flags & ~WDM_FLAG_DMA);
	}

	return handle->ops->write(handle->ctx, lba, count, buf, flags);
}

WDM_Status WDM_Trim(WDM_DriveHandle handle, WDM_LBA lba, uint32_t count) {
	WDM_Status st = check_io_args(handle, lba, count, NULL, false);

	if (st != WDM_OK) return st;
	if (count == 0) return WDM_OK;

	/*
	 * TRIM is a hint.
	 * If the driver does not implement it, return OK as the spec specifies "silently ignored."
	 */
	if (!handle->ops->trim) {
		return WDM_OK;
	}

	return handle->ops->trim(handle->ctx, lba, count);
}

WDM_Status WDM_Flush(WDM_DriveHandle handle) {
	if (!validate_handle(handle)) {
		return WDM_ERR_NOT_FOUND;
	}

	return flush_drive(handle);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Partition Abstraction Layer
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
#include <memory/kernel_alloc.h>

typedef struct {
	WDM_DriveHandle parent;
	WDM_LBA start_lba;
	WDM_LBA total_sectors;
} wdm_partition_bridge_t;

static WDM_Status part_bridge_read(void* ctx, WDM_LBA lba, uint32_t count, void* buf, WDM_IOFlags flags) {
	wdm_partition_bridge_t* p = (wdm_partition_bridge_t*) ctx;
	if (lba + count > p->total_sectors) return WDM_ERR_OVERFLOW;

	// Redirect read to the parent with the partition's starting block offset
	return WDM_Read(p->parent, p->start_lba + lba, count, buf, flags);
}

static WDM_Status part_bridge_write(void* ctx, WDM_LBA lba, uint32_t count, const void* buf, WDM_IOFlags flags) {
	wdm_partition_bridge_t* p = (wdm_partition_bridge_t*) ctx;
	if (lba + count > p->total_sectors) return WDM_ERR_OVERFLOW;

	// Redirect write to the parent with the partition's starting block offset
	return WDM_Write(p->parent, p->start_lba + lba, count, buf, flags);
}

static WDM_Status part_bridge_flush(void* ctx) {
	wdm_partition_bridge_t* p = (wdm_partition_bridge_t*) ctx;
	return WDM_Flush(p->parent);
}

static void part_bridge_detach(void* ctx) {
	// All that's left for us on detach is to clean up the context
	kfree(ctx);
}

static const WDM_DriverOps partition_bridge_ops = {
	.read = part_bridge_read,
	.write = part_bridge_write,
	.flush = part_bridge_flush,
	.trim = NULL,
	.on_attach = NULL,
	.on_detach = part_bridge_detach,
};

WDM_DriveHandle WDM_AddPartition(WDM_DriveHandle parent, WDM_LBA start, WDM_LBA length, const WDM_PartitionMeta* meta) {
	WDM_DriveInfo parent_info;
	if (WDM_GetInfo(parent, &parent_info) != WDM_OK) return NULL;

	wdm_partition_bridge_t* ctx = kalloc(sizeof(wdm_partition_bridge_t));
	if (!ctx) return NULL;

	ctx->parent = parent;
	ctx->start_lba = start;
	ctx->total_sectors = length;

	WDM_DriveInfo part_info = parent_info;
	part_info.sector_count = length;
	snprintf(part_info.model, sizeof(part_info.model), "Logical Partition");

	WDM_DriveHandle part_handle = NULL;
	if (WDM_RegisterPartition(parent, &partition_bridge_ops, ctx, &part_info, &part_handle) != WDM_OK) {
		kfree(ctx);
		return NULL;
	}

	if (meta) {
		part_handle->has_metadata = true;
		memcpy(&part_handle->meta, meta, sizeof(WDM_PartitionMeta));
	} else {
		part_handle->has_metadata = false;
		memset(&part_handle->meta, 0, sizeof(WDM_PartitionMeta));
	}

	return part_handle;
}

WDM_Status WDM_GetPartitionMetadata(WDM_DriveHandle handle, WDM_PartitionMeta* meta) {
	if (!validate_handle(handle) || !meta) {
		return WDM_ERR_INVALID;
	}

	if (!handle->has_metadata) {
		return WDM_ERR_UNSUPPORTED;
	}

	memcpy(meta, &handle->meta, sizeof(WDM_PartitionMeta));
	return WDM_OK;
}

#include <filesystem/partitions/wallos_gpt.h>
#include <device/device_manager.h>

#include <drivers/serial.h>

WDM_Status WDM_ScanAndRegisterPartitions(WDM_DriveHandle wdm_parent, struct wallos_device* dev_parent) {
	if (!wdm_parent || !dev_parent) {
		printf_serial("[WDM] Invalid parameters.\r\n");
		return WDM_ERR_INVALID;
	}

	printf_serial("[WDM] Scanning partitions on %s...\r\n", dev_parent->name);

	WDM_DriveInfo info;
	if (WDM_GetInfo(wdm_parent, &info) != WDM_OK) {
		printf_serial("[WDM] Failed to query drive information.\r\n");
		return WDM_ERR_INVALID;
	}

	printf_serial("[WDM] Sector size: %u bytes\r\n", info.sector_size);

	uint8_t* sector_buf = kalloc(info.sector_size);
	if (!sector_buf) {
		printf_serial("[WDM] Failed to allocate sector buffer.\r\n");
		return WDM_ERR_BUSY;
	}

	// Yes this is ugly, I just didn't want to replicate this code.
#define REGISTER_PARTITION_NODE(NAME, START, LENGTH, META) do { \
		printf_serial("[WDM] Registering %s (LBA=%llu, sectors=%llu)...\r\n", NAME, (unsigned long long)(START), (unsigned long long)(LENGTH)); \
		WDM_DriveHandle p_handle = WDM_AddPartition(wdm_parent, START, LENGTH, META); \
		if (p_handle) { \
			wallos_device_t* child_dev = create_device(dev_parent->interfaces, NAME); \
			if (child_dev) { \
				child_dev->parent = dev_parent; \
				child_dev->driver_data = p_handle; \
				child_dev->next_sibling = dev_parent->first_child; \
				dev_parent->first_child = child_dev; \
				register_device(child_dev); \
				printf_serial("[WDM] Registered device %s.\r\n", NAME); \
			} else { \
				printf_serial("[WDM] Failed to create device node for %s.\r\n", NAME); \
				WDM_Unregister(p_handle); \
				kfree((void*)(NAME)); \
			} \
		} else { \
			printf_serial("[WDM] Failed to register partition %s.\r\n", NAME); \
			kfree((void*)(NAME)); \
		} \
	} while (0)

	// Try GPT first
	printf_serial("[WDM] Attempting GPT detection...\r\n");

	gpt_partition_table_t gpt_table;
	memset(&gpt_table, 0, sizeof(gpt_table));

	if (parse_gpt(wdm_parent, sector_buf, info.sector_size, &gpt_table, GPT_FLAGS_NONE) == GPT_NO_ERROR) {
		printf_serial("[WDM] GPT detected.\r\n");

		uint32_t validation = validate_gpt(&gpt_table);

		if (validation == 0) {
			printf_serial("[WDM] GPT validation succeeded (%u entries).\r\n",
				gpt_table.num_entries);

			uint32_t part_idx = 0;

			for (uint32_t i = 0; i < gpt_table.num_entries; i++) {
				if (gpt_table.entries[i].first_lba != 0 && gpt_table.entries[i].last_lba != 0) {
					// We arbitrarily limit it to 64 characters.
					// There's really no reason for this, just what I felt like and should fit cleanly with our allocator
					char* part_name = kalloc(64);
					if (!part_name) {
						printf_serial("[WDM] Failed to allocate partition name buffer.\r\n");
						continue;
					}
					snprintf(part_name, 64, "%sp%u", dev_parent->name, part_idx);

					uint64_t total_sectors = (gpt_table.entries[i].last_lba - gpt_table.entries[i].first_lba) + 1;

					printf_serial("[WDM] GPT Partition %u: LBA %llu-%llu (%llu sectors)\r\n", part_idx, (unsigned long long)gpt_table.entries[i].first_lba, (unsigned long long)gpt_table.entries[i].last_lba, (unsigned long long)total_sectors);

					WDM_PartitionMeta meta;
					memset(&meta, 0, sizeof(meta));
					memcpy(meta.type_guid, gpt_table.entries[i].partition_type_guid, 16);
					memcpy(meta.unique_guid, gpt_table.entries[i].unique_partition_guid, 16);
					meta.attributes = gpt_table.entries[i].attributes;
					memcpy(meta.partition_name, gpt_table.entries[i].partition_name, sizeof(meta.partition_name));

					REGISTER_PARTITION_NODE(part_name, gpt_table.entries[i].first_lba, total_sectors, &meta);

					part_idx++;
				}
			}

			printf_serial("[WDM] GPT scan complete (%u partitions).\r\n", part_idx);

			kfree(gpt_table.entries);
			kfree(sector_buf);
			return WDM_OK;
		} else {
			printf_serial("[WDM] GPT validation failed (0x%08X). Not falling back to MBR.\r\n", validation);

			if (gpt_table.entries) kfree(gpt_table.entries);

			kfree(sector_buf);
			return WDM_OK;
		}
	}

	printf_serial("[WDM] No valid GPT found. Trying MBR...\r\n");

	if (gpt_table.entries) kfree(gpt_table.entries);

	// Fallback to MBR
	if (WDM_Read(wdm_parent, 0, 1, sector_buf, WDM_FLAG_NONE) == WDM_OK) {
		mbr_partition_table_t mbr_table;
		memset(&mbr_table, 0, sizeof(mbr_table));
		parse_mbr(&mbr_table, sector_buf, info.sector_size);

		uint32_t part_idx = 0;

		for (int i = 0; i < 4; i++) {
			if (mbr_table.partition_entries[i].partition_type != 0 &&
				mbr_table.partition_entries[i].sector_count > 0) {

				char* part_name = kalloc(64);
				if (!part_name) {
					printf_serial("[WDM] Failed to allocate partition name buffer.\r\n");
					continue;
				}
				snprintf(part_name, 64, "%sp%u", dev_parent->name, part_idx);

				printf_serial("[WDM] MBR Partition %u: Type=0x%02X LBA=%u Size=%u sectors\r\n", part_idx, mbr_table.partition_entries[i].partition_type, mbr_table.partition_entries[i].lba_start, mbr_table.partition_entries[i].sector_count);

				REGISTER_PARTITION_NODE(part_name, mbr_table.partition_entries[i].lba_start, mbr_table.partition_entries[i].sector_count, NULL);

				part_idx++;
			}
		}

		printf_serial("[WDM] MBR scan complete (%u partitions).\r\n", part_idx);

		kfree(sector_buf);
		return WDM_OK;
	}

	printf_serial("[WDM] Failed to read MBR sector.\r\n");

	kfree(sector_buf);
	return WDM_ERR_NOT_FOUND;
}