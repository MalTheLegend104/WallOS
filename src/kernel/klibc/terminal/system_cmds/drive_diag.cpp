#include <terminal/wall_shell.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <filesystem/wdm.h>
#include <filesystem/vfs.h>
#include <filesystem/filesystems.h>

/* Max mounts to pull back from filesystem_enumerate_mounts() into a local stack buffer.  */
#define FS_MOUNT_QUERY_MAX VFS_MOUNT_MAX

/* Only the subset of filesystem_status_t that filesystem_probe() can actually return. */
static const char* probe_status_strerror(filesystem_status_t status) {
	switch (status) {
		case FILESYSTEM_NO_FILESYSTEM: return "no recognized filesystem found";
		case FILESYSTEM_INVALID_DRIVE: return "invalid drive";
		default:                       return "internal error";
	}
}

static const char* wdm_strerror(WDM_Status st) {
	switch (st) {
		case WDM_OK:                 return "OK";
		case WDM_ERR_INVALID:        return "invalid argument or handle";
		case WDM_ERR_IO:             return "I/O error";
		case WDM_ERR_TIMEOUT:        return "operation timed out";
		case WDM_ERR_BUSY:           return "drive busy";
		case WDM_ERR_NO_MEDIA:       return "no media present";
		case WDM_ERR_WRITE_PROT:     return "drive is write-protected";
		case WDM_ERR_NOT_FOUND:      return "drive not registered";
		case WDM_ERR_ALIGN:          return "buffer/LBA alignment violation";
		case WDM_ERR_OVERFLOW:       return "LBA + count exceeds drive end";
		case WDM_ERR_UNSUPPORTED:    return "not supported by this driver";
		case WDM_ERR_DMA:            return "DMA transfer failure";
		case WDM_ERR_ALREADY_EXISTS: return "already exists";
		default:                     return "unknown error";
	}
}

// This is the same logic as in drive_command.cpp
// I'm like 90% sure it's just ASCII already
static void decode_partition_name(const char* raw, size_t raw_size, char* out, size_t out_size) {
	const uint8_t* bytes = (const uint8_t*) raw;
	size_t out_i = 0;

	for (size_t i = 0; i + 1 < raw_size && out_i + 1 < out_size; i += 2) {
		uint16_t code_unit = (uint16_t) bytes[i] | ((uint16_t) bytes[i + 1] << 8);
		if (code_unit == 0) break;
		out[out_i++] = (code_unit < 0x80) ? (char) code_unit : '?';
	}
	out[out_i] = '\0';
}
int drive_info_cmd(int argc, char** argv) {
	// Since this is a "subcommand" we don't necessarily have the same ease of parsing
	// Means we have to manually parse instead, will likely update wallshell in the future for this...
	bool has_index = argc > 1;

	uint32_t total = 0;
	WDM_Enumerate(NULL, 0, &total);

	if (total == 0) {
		printf("No drives registered with WDM.\n");
		return 0;
	}

	// Limit to 16 handles for our local buffer
	WDM_DriveHandle handles[16];
	uint32_t fetched = total < 16 ? total : 16;
	WDM_Enumerate(handles, fetched, &total);

	// Specific Drive Requested 
	// Accepts a plain 'N' index, or an 'N:M' partition address
	if (has_index) {
		WDM_DriveHandle h;
		WDM_Status addr_status = WDM_ResolveAddress(argv[1], &h);
		if (addr_status != WDM_OK) {
			printf("Error: '%s' - %s\n", argv[1], wdm_strerror(addr_status));
			return 1;
		}

		WDM_DriveInfo info;
		if (WDM_GetInfo(h, &info) != WDM_OK) {
			printf("Drive %s: <failed to query details>\n", argv[1]);
			return 1;
		}

		uint64_t capacity_mb = (info.sector_count * info.sector_size) / (1024 * 1024);

		printf("Drive %s:\n", argv[1]);
		printf("\tModel:           %s\n", info.model);
		printf("\tSerial:          %s\n", info.serial);
		printf("\tSectors:         %llu\n", (unsigned long long)info.sector_count);
		printf("\tSector size:     %u B\n", info.sector_size);
		printf("\tPhysical sector: %u B\n", info.physical_sector);
		printf("\tOptimal xfer:    %u sectors\n", info.optimal_xfer);
		printf("\tCapacity:        ~%llu MB\n", (unsigned long long)capacity_mb);
		printf("\tRemovable:       %s\n", info.removable ? "yes" : "no");
		printf("\tRead-only:       %s\n", info.read_only ? "yes" : "no");
		printf("\tDMA capable:     %s\n", info.dma_capable ? "yes" : "no");
		return 0;
	}

	// Minimal Summary List (default 'drive info')
	printf("ID  Model                Capacity    Status\n");
	printf("--  -------------------  ----------  -------\n");

	for (uint32_t i = 0; i < fetched; i++) {
		WDM_DriveInfo info;
		if (WDM_GetInfo(handles[i], &info) != WDM_OK) {
			printf("%-2u  <query failed>\n", i);
			continue;
		}

		uint64_t capacity_mb = (info.sector_count * info.sector_size) / (1024 * 1024);

		// Print summary, in this form:
		// ## Model XXXX MB RO/RW (Removable)
		printf("%-2u  %-19.19s  %6llu MB  %s%s\n", i, info.model, (unsigned long long)capacity_mb, info.read_only ? "RO" : "RW", info.removable ? " (Removable)" : "");
	}

	// Mounted drive summary
	filesystem_mount_info_t mounts[FS_MOUNT_QUERY_MAX];
	size_t mount_count = 0;
	filesystem_enumerate_mounts(mounts, FS_MOUNT_QUERY_MAX, &mount_count);
	if (mount_count > FS_MOUNT_QUERY_MAX) mount_count = FS_MOUNT_QUERY_MAX;

	if (mount_count == 0) {
		printf("No volumes currently mounted.\n");
	} else {
		printf("Mounted Volumes:\n");
		for (size_t i = 0; i < mount_count; i++) {
			printf("\t%s (%s)\n", mounts[i].mountpoint, fs_type_name(mounts[i].type));
		}
	}

	return 0;
}

// I have ZERO clue what this was ever supposed to do...
int drive_test_cmd(int argc, char** argv) {
	(void) argc; (void) argv;
	return 0;
}

static int wdm_probe_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: drive probe <addr>\n");
		return 1;
	}

	WDM_DriveHandle h;
	WDM_Status addr_status = WDM_ResolveAddress(argv[1], &h);
	if (addr_status != WDM_OK) {
		printf("Error: '%s' - %s\n", argv[1], wdm_strerror(addr_status));
		return 0;
	}

	known_fs_types_t type;
	filesystem_status_t status = filesystem_probe(h, &type);
	if (status != FILESYSTEM_SUCCESS) {
		printf("Drive %s: %s\n", argv[1], probe_status_strerror(status));
		return 0;
	}

	printf("Drive %s: detected %s\n", argv[1], fs_type_name(type));
	return 0;
}

#include <filesystem/partitions/wallos_gpt.h>
static int wdm_partitions_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: drive partitions <addr>\n");
		return 1;
	}

	WDM_DriveHandle parent;
	WDM_Status addr_status = WDM_ResolveAddress(argv[1], &parent);
	if (addr_status != WDM_OK) {
		printf("Error: '%s' - %s\n", argv[1], wdm_strerror(addr_status));
		return 0;
	}

	uint32_t total = 0;
	WDM_EnumeratePartitions(parent, NULL, 0, &total);
	if (total == 0) {
		printf("No partitions registered under drive %s.\n", argv[1]);
		return 0;
	}

	WDM_DriveHandle handles[WDM_MAX_DRIVES];
	uint32_t fetched = total < WDM_MAX_DRIVES ? total : WDM_MAX_DRIVES;
	WDM_EnumeratePartitions(parent, handles, fetched, &total);

	printf("Partitions on drive %s:\n", argv[1]);

	for (uint32_t i = 0; i < fetched; i++) {
		char wdm_name[33];
		if (WDM_GetNameFromDrive(handles[i], wdm_name, sizeof(wdm_name)) != WDM_OK) {
			strncpy(wdm_name, "<unnamed>", sizeof(wdm_name) - 1);
			wdm_name[sizeof(wdm_name) - 1] = '\0';
		}

		WDM_DriveInfo info;
		bool have_info = (WDM_GetInfo(handles[i], &info) == WDM_OK);

		// Main partition header line
		printf("  %s:%u: %s\n", argv[1], i, wdm_name);

		// Size on its own line
		if (have_info) {
			uint64_t capacity_mb = (info.sector_count * info.sector_size) / (1024 * 1024);
			printf("\tSize:        %llu MB\n", (unsigned long long)capacity_mb);
		} else {
			printf("\tSize:        <info unavailable>\n");
		}

		WDM_PartitionMeta meta;
		if (WDM_GetPartitionMetadata(handles[i], &meta) == WDM_OK) {
			char label[37]; // 36 characters + 1 for null terminator
			char type_guid[37] = "";
			char unique_guid[37] = "";

			// Decode the name and manually enforce null-termination
			gpt_name_to_ascii((const uint16_t*) meta.partition_name, label, sizeof(label) - 1);
			label[36] = '\0';

			// Label on its own line
			if (label[0] != '\0') {
				printf("\tLabel:       \"%s\"\n", label);
			}

			guid_to_string(meta.type_guid, type_guid);
			guid_to_string(meta.unique_guid, unique_guid);

			// Fetch the human-readable type name using the new APIs
			gpt_partition_type_id_t type_id = gpt_partition_type_from_guid(meta.type_guid);
			const char* type_name = gpt_partition_type_name(type_id);
			if (!type_name) {
				type_name = "Unknown Type";
			}

			// Standardized metadata lines
			printf("\tType GUID:   %s (%s)\n", type_guid, type_name);
			printf("\tUnique GUID: %s\n", unique_guid);
			printf("\tAttributes:  0x%016llx\n", (unsigned long long)meta.attributes);
		}

		printf("\n");
	}

	// theoretically not possible
	// I added this before I realized I didn't have WDM_MAX_DRIVES in the header
	// Since we can only ever pull at most WDM_MAX_DRIVES from the WDM_EnumeratePartitions anyway, this should tell us something is wrong in WDM
	if (total > fetched) {
		printf("  ... and %u more (not shown)\n", total - fetched);
	}

	return 0;
}

static int wdm_flush_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: drive flush <addr>\n");
		return 1;
	}

	WDM_DriveHandle h;
	WDM_Status addr_status = WDM_ResolveAddress(argv[1], &h);
	if (addr_status != WDM_OK) {
		printf("Error: '%s' - %s\n", argv[1], wdm_strerror(addr_status));
		return 0;
	}

	WDM_Status status = WDM_Flush(h);
	if (status != WDM_OK) {
		printf("Error: %s\n", wdm_strerror(status));
		return 0;
	}

	printf("Drive %s flushed.\n", argv[1]);
	return 0;
}

static int wdm_trim_cmd(int argc, char** argv) {
	if (argc < 4) {
		printf("Usage: drive trim <addr> <lba> <count>\n");
		return 1;
	}

	char* endptr;

	uint64_t lba = strtoull(argv[2], &endptr, 0);
	if (*endptr != '\0') {
		printf("Error: invalid LBA '%s'.\n", argv[2]);
		return 0;
	}

	uint64_t count64 = strtoull(argv[3], &endptr, 0);
	if (*endptr != '\0' || count64 > UINT32_MAX) {
		printf("Error: invalid sector count '%s'.\n", argv[3]);
		return 0;
	}

	WDM_DriveHandle h;
	WDM_Status addr_status = WDM_ResolveAddress(argv[1], &h);
	if (addr_status != WDM_OK) {
		printf("Error: '%s' - %s\n", argv[1], wdm_strerror(addr_status));
		return 0;
	}

	WDM_Status status = WDM_Trim(h, (WDM_LBA) lba, (uint32_t) count64);
	if (status != WDM_OK) {
		printf("Error: %s\n", wdm_strerror(status));
		return 0;
	}

	printf("Trimmed %llu sector(s) starting at LBA %llu on drive %s.\n",
		(unsigned long long) count64, (unsigned long long) lba, argv[1]);
	return 0;
}

// ------------------------------------------------------------------------------------------------
// Dispatch
// ------------------------------------------------------------------------------------------------

static void print_drive_usage(void) {
	printf("Usage: drive <subcommand> [args...]\n");
	printf("Subcommands:\n");
	printf("  info [addr]                List drives and mounted volumes, or details for one drive/partition.\n");
	printf("  test                       Diagnostic no-op.\n");
	printf("  probe <addr>               Detect a filesystem on a drive without mounting it.\n");
	printf("  partitions <addr>          List partitions registered under a drive.\n");
	printf("  flush <addr>               Flush a drive's write-back cache to stable storage.\n");
	printf("  trim <addr> <lba> <count>  Advise the drive that a sector range is unused.\n");
	printf("\n");
	printf("<addr> is 'N' for the N'th drive (WDM_Enumerate order, as printed by 'drive info'/'lsblk'),\n");
	printf("or 'N:M' for the M'th partition of drive N (as printed by 'drive partitions N'/'lsblk').\n");
}

int drive_cmd(int argc, char** argv) {
	if (argc < 2) {
		print_drive_usage();
		return 0;
	}

	const char* sub = argv[1];

	// Shift so each subcommand handler sees its own name at argv[0], same convention as if it had been invoked directly.
	if (strcmp(sub, "info") == 0)       return drive_info_cmd(argc - 1, argv + 1);
	if (strcmp(sub, "test") == 0)       return drive_test_cmd(argc - 1, argv + 1);
	if (strcmp(sub, "probe") == 0)      return wdm_probe_cmd(argc - 1, argv + 1);
	if (strcmp(sub, "partitions") == 0) return wdm_partitions_cmd(argc - 1, argv + 1);
	if (strcmp(sub, "flush") == 0)      return wdm_flush_cmd(argc - 1, argv + 1);
	if (strcmp(sub, "trim") == 0)       return wdm_trim_cmd(argc - 1, argv + 1);

	printf("Error: unknown drive subcommand '%s'.\n", sub);
	print_drive_usage();
	return 1;
}

// ------------------------------------------------------------------------------------------------
// Registration
// ------------------------------------------------------------------------------------------------
// I really need a way to deal with subcommands
// didnt think of it when rewriting wallshell
static const ws_command_argument_t drive_args[] = {
	{ WS_ARG_TYPE_GENERIC, true,  "subcommand", NULL, "info, test, probe, partitions, flush, trim." },
	{ WS_ARG_TYPE_GENERIC, false, "arg1",       NULL, "Subcommand-specific argument (usually a device address: 'N' or 'N:M', see 'drive' with no args)." },
	{ WS_ARG_TYPE_GENERIC, false, "arg2",       NULL, "Subcommand-specific argument (e.g. LBA for 'trim')." },
	{ WS_ARG_TYPE_GENERIC, false, "arg3",       NULL, "Subcommand-specific argument (e.g. count for 'trim')." },
};
static const ws_command_t drive_command = {
	.command_name = "drive",
	.aliases = NULL,
	.alias_count = 0,
	.main_void = NULL,
	.main_func = drive_cmd,
	.env_func = NULL,
	.help_func = NULL,
	.major = 0, .minor = 0, .patch = 0,
	.arguments = drive_args,
	.arguments_count = sizeof(drive_args) / sizeof(drive_args[0]),
};

extern "C" void register_drive_diag_commands(void) {
	ws_registerCommand(drive_command);
}