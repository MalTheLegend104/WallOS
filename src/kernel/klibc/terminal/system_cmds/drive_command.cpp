#include <terminal/wall_shell.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <filesystem/wdm.h>
#include <filesystem/vfs.h>
#include <filesystem/filesystems.h>
#include <filesystem/fat/fat.h>
#include <filesystem/fat/fat32_vfs.h>
#include <filesystem/fat/fat1216_vfs.h>
#include <filesystem/iso9660/iso9660.h>

#include <system/timer.h>
#include <klibc/kprint.h>

// This file was basically unreadable before.
// This mostly hosts the generic filesystem CLI commands like cd/ls/tree/etc
// Also handles mounting/unmounting
// It should be more readable, but still kinda an unorganized mess

/* Max mounts to pull back from filesystem_enumerate_mounts() into a local stack buffer. */
#define FS_MOUNT_QUERY_MAX VFS_MOUNT_MAX

/* Registers all the filesystems with the generic filesystem layer.
 * This probably shouldn't be here, but meh, I'll deal with that later.
 */
static void register_drive_filesystems(void) {
	filesystem_register(FILESYSTEM_FAT12_16, &vfs_fat1216_ops);
	filesystem_register(FILESYSTEM_FAT32, &vfs_fat32_ops);
	filesystem_register(FILESYSTEM_ISO9660, &iso9660_vfs_ops);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Current working directory
//
// Current working directory. Used for the terminal prefix and relative VFS paths.
// All the code in this section is awful for readability. 
// There is a lot of abusing strings and printf. You were warned.
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static char cwd[VFS_PATH_MAX] = "/";
static char prefix_buf[VFS_PATH_MAX + 4] = "/> ";

/** Rebuild prefix_buf from the current cwd and push it to the terminal. */
static void update_terminal_prefix(void) {
	snprintf(prefix_buf, sizeof(prefix_buf), "%s> ", cwd);
	ws_setConsolePrefix(prefix_buf);
}

/**
 * @brief Resolve 'input' (absolute or relative to cwd) into a normalized absolute VFS path, collapsing "." and ".." segments.
 * Does not touch the filesystem or validate existence.
 *
 * @return false if the resolved path would not fit in 'out' (out is left unspecified on failure).
 */
static bool resolve_path(const char* input, char* out, size_t out_size) {
	char combined[VFS_PATH_MAX * 2];

	if (input[0] == '/') {
		if (strlen(input) >= sizeof(combined)) return false;
		strcpy(combined, input);
	} else {
		int n = snprintf(combined, sizeof(combined), "%s%s%s", cwd, (strcmp(cwd, "/") == 0) ? "" : "/", input);
		if (n < 0 || (size_t) n >= sizeof(combined)) return false;
	}

	char work[VFS_PATH_MAX * 2];
	strncpy(work, combined, sizeof(work) - 1);
	work[sizeof(work) - 1] = '\0';

	// Manually split on '/' and resolve "." / ".." as we go.
	// Would be a good time to have some form of strtok() but I *really* don't want to deal with that.
	char* segments[VFS_PATH_MAX / 2];
	int   seg_count = 0;

	char* p = work;
	while (*p != '\0') {
		while (*p == '/') p++; // skip separators
		if (*p == '\0') break;

		char* token = p;
		while (*p != '/' && *p != '\0') p++;
		bool at_end = (*p == '\0');
		if (!at_end) *p++ = '\0';

		if (strcmp(token, ".") == 0) {
			// no-op
		} else if (strcmp(token, "..") == 0) {
			if (seg_count > 0) seg_count--;
		} else {
			if (seg_count >= (int) (sizeof(segments) / sizeof(segments[0]))) return false;
			segments[seg_count++] = token;
		}
	}

	if (seg_count == 0) {
		if (out_size < 2) return false;
		out[0] = '/';
		out[1] = '\0';
		return true;
	}

	size_t pos = 0;
	for (int i = 0; i < seg_count; i++) {
		size_t len = strlen(segments[i]);
		if (pos + 1 + len >= out_size) return false; // +1 for '/', leaves room for '\0'
		out[pos++] = '/';
		memcpy(out + pos, segments[i], len);
		pos += len;
	}
	out[pos] = '\0';
	return true;
}

/** Resolve 'raw' into 'out' (size out_size), printing an error and returning false on failure. */
static bool resolve_path_or_report(const char* raw, char* out, size_t out_size) {
	if (!resolve_path(raw, out, out_size)) {
		printf("Error: resolved path too long.\n");
		return false;
	}
	return true;
}

static const char* vfs_strerror(VFS_Status st) {
	switch (st) {
		case VFS_OK:              return "OK";
		case VFS_ERR_INVALID:     return "invalid argument";
		case VFS_ERR_IO:          return "I/O error";
		case VFS_ERR_NOENT:       return "no such file or directory";
		case VFS_ERR_EXIST:       return "already exists";
		case VFS_ERR_NOTDIR:      return "not a directory";
		case VFS_ERR_ISDIR:       return "is a directory";
		case VFS_ERR_NOTEMPTY:    return "directory not empty";
		case VFS_ERR_NOMNT:       return "path not mounted";
		case VFS_ERR_BADF:        return "bad file descriptor";
		case VFS_ERR_BUSY:        return "resource busy";
		case VFS_ERR_NOSPACE:     return "no space left";
		case VFS_ERR_TOOLONG:     return "path too long";
		case VFS_ERR_OVERFLOW:    return "offset overflow";
		case VFS_ERR_MNTFULL:     return "mount table full";
		case VFS_ERR_FDFULL:      return "file descriptor table full";
		case VFS_ERR_UNSUPPORTED: return "unsupported";
		default:                  return "unknown error";
	}
}

/** Same idea as vfs_strerror(), for the filesystem management layer's own status codes. */
static const char* fs_status_strerror(filesystem_status_t status) {
	switch (status) {
		case FILESYSTEM_SUCCESS:                return "success";
		case FILESYSTEM_NO_FILESYSTEM:          return "drive does not contain that filesystem";
		case FILESYSTEM_NO_DRIVE:               return "drive not found";
		case FILESYSTEM_DRIVE_IN_USE:           return "drive already in use";
		case FILESYSTEM_INVALID_DRIVE:          return "invalid drive";
		case FILESYSTEM_INVALID_FILESYSTEM:     return "invalid filesystem driver";
		case FILESYSTEM_UNSUPPORTED_FILESYSTEM: return "filesystem type not supported";
		case FILESYSTEM_CORRUPT_FILESYSTEM:     return "filesystem appears corrupt";
		case FILESYSTEM_ALREADY_MOUNTED:        return "already mounted";
		case FILESYSTEM_NOT_MOUNTED:            return "not currently mounted";
		case FILESYSTEM_MOUNT_FAILED:           return "mount failed";
		case FILESYSTEM_OUT_OF_MEMORY:          return "out of memory";
		case FILESYSTEM_NO_MOUNTPOINT:          return "invalid mount point";
		case FILESYSTEM_MOUNTPOINT_IN_USE:      return "mount point busy or already in use";
		case FILESYSTEM_DEVICE_ERROR:           return "device error";
		case FILESYSTEM_IO_ERROR:               return "I/O error";
		case FILESYSTEM_DEVICE_NOT_READY:       return "device not ready";
		case FILESYSTEM_READ_ONLY:              return "read-only";
		case FILESYSTEM_ACCESS_DENIED:          return "access denied";
		case FILESYSTEM_UNKNOWN_ERROR:
		default:                                return "unknown error";
	}
}

/** Same idea as vfs_strerror(), for WDM's own status codes (used for WDM_ResolveAddress() failures below). */
static const char* wdm_strerror(WDM_Status st) {
	switch (st) {
		case WDM_OK:                 return "OK";
		case WDM_ERR_INVALID:        return "invalid argument or handle";
		case WDM_ERR_IO:             return "I/O error";
		case WDM_ERR_TIMEOUT:        return "operation timed out";
		case WDM_ERR_BUSY:           return "drive busy";
		case WDM_ERR_NO_MEDIA:       return "no media present";
		case WDM_ERR_WRITE_PROT:     return "drive is write-protected";
		case WDM_ERR_NOT_FOUND:      return "no such drive/partition index";
		case WDM_ERR_ALIGN:          return "buffer/LBA alignment violation";
		case WDM_ERR_OVERFLOW:       return "LBA + count exceeds drive end";
		case WDM_ERR_UNSUPPORTED:    return "not supported by this driver";
		case WDM_ERR_DMA:            return "DMA transfer failure";
		case WDM_ERR_ALREADY_EXISTS: return "already exists";
		default:                     return "unknown error";
	}
}

void print_file_size(uint64_t size) {
	if (size < 1024)  printf("%u B", (unsigned int) size);
	else if (size < (1024 * 1024)) printf("%u KB", (unsigned int) (size / 1024));
	else if (size < (1024ULL * 1024 * 1024)) printf("%u MB", (unsigned int) (size / (1024 * 1024)));
	else printf("%u GB", (unsigned int) (size / (1024ULL * 1024 * 1024)));
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Mount / Unmount
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

int drive_mount_cmd(int argc, char** argv) {
	ws_context_t* ws_ctx = ws_getCurrentContext();
	if (!ws_parse_args(ws_ctx, argc, argv)) return 1;

	const char* vfs_path = ws_get_generic(ws_ctx, "path");
	const char* addr = ws_get_generic(ws_ctx, "addr");
	bool auto_detect = !ws_has_arg(ws_ctx, "fs");

	known_fs_types_t type = (known_fs_types_t) 0;
	if (!auto_detect) {
		const char* fs_name = ws_get_generic(ws_ctx, "fs");
		if (!find_fs_type_by_name(fs_name, &type)) {
			printf("Error: unknown filesystem '%s'.\n", fs_name);
			print_fs_name_list();
			return 0;
		}
	}

	WDM_DriveHandle h;
	WDM_Status addr_status = WDM_ResolveAddress(addr, &h);
	if (addr_status != WDM_OK) {
		printf("Error: '%s' - %s\n", addr, wdm_strerror(addr_status));
		return 0;
	}

	filesystem_status_t status;
	if (auto_detect) {
		// This is purely cosmetic. We probe again ourselves just to name what we're about to mount as.
		// filesystem_mount() does its own probe regardless of whether this succeeds
		known_fs_types_t detected;
		if (filesystem_probe(h, &detected) == FILESYSTEM_SUCCESS) {
			printf("Mounting drive %s (detected: %s) at '%s'...\n", addr, fs_type_name(detected), vfs_path);
		} else {
			printf("Mounting drive %s (auto-detecting filesystem) at '%s'...\n", addr, vfs_path);
		}
		status = filesystem_mount(h, vfs_path);
	} else {
		printf("Mounting drive %s (%s) at '%s'...\n", addr, ws_get_generic(ws_ctx, "fs"), vfs_path);
		status = filesystem_mount_explicit(h, vfs_path, type);
	}

	if (status != FILESYSTEM_SUCCESS) {
		printf("Error: %s\n", fs_status_strerror(status));
		return 0;
	}

	// Mounting root gives us something for cwd.
	// Should probably check to see if we were already cd'd into something though.
	if (strcmp(vfs_path, "/") == 0) {
		strncpy(cwd, "/", sizeof(cwd) - 1);
		cwd[sizeof(cwd) - 1] = '\0';
		update_terminal_prefix();
	}

	printf("Mounted successfully at '%s'.\n", vfs_path);
	return 0;
}

int drive_unmount_cmd(int argc, char** argv) {
	ws_context_t* ws_ctx = ws_getCurrentContext();
	if (!ws_parse_args(ws_ctx, argc, argv)) return 1;

	const char* vfs_path = ws_get_generic(ws_ctx, "path");

	filesystem_status_t status = filesystem_unmount(vfs_path);
	if (status != FILESYSTEM_SUCCESS) {
		printf("Error: %s\n", fs_status_strerror(status));
		return 0;
	}

	printf("Unmounted '%s'.\n", vfs_path);

	// If cwd was at or below the volume we just unmounted, it's now pointing at storage that was just torn down 
	// Bail back to a known-safe location rather than leaving cwd dangling
	char prefix[VFS_PATH_MAX];
	if (strcmp(vfs_path, "/") == 0) {
		strncpy(prefix, "/", sizeof(prefix));
	} else {
		snprintf(prefix, sizeof(prefix), "%s/", vfs_path);
	}
	bool cwd_orphaned = (strcmp(cwd, vfs_path) == 0) || (strncmp(cwd, prefix, strlen(prefix)) == 0);

	if (cwd_orphaned) {
		printf("Note: current directory was inside '%s'; returning to /\n", vfs_path);
		strncpy(cwd, "/", sizeof(cwd) - 1);
		cwd[sizeof(cwd) - 1] = '\0';
		update_terminal_prefix();
	}

	return 0;
}

#define LS_FLAG_NONE    0x00
#define LS_FLAG_ATTRIB  0x01
#define LS_FLAG_SIZE    0x02
#define LS_FLAG_TIME    0x04
#define LS_FLAG_DEFAULT (LS_FLAG_SIZE | LS_FLAG_ATTRIB)

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// lsblk
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

#define LSBLK_MAX_HANDLES WDM_MAX_DRIVES

// In theory, this *should* just be ASCII anyway, if it was parsed by us.
// I don't really feel like digging through the GPT code to make sure
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

static void format_capacity(uint64_t sector_count, uint32_t sector_size, char* out, size_t out_size) {
	uint64_t bytes = sector_count * sector_size;
	if (bytes < (1024ULL * 1024)) {
		snprintf(out, out_size, "%llu KB", (unsigned long long) (bytes / 1024));
	} else if (bytes < (1024ULL * 1024 * 1024)) {
		snprintf(out, out_size, "%llu MB", (unsigned long long) (bytes / (1024 * 1024)));
	} else {
		snprintf(out, out_size, "%llu GB", (unsigned long long) (bytes / (1024ULL * 1024 * 1024)));
	}
}

// GPT partition name if it exists, registered name otherwise. If neither, we print "-"
static void get_display_name(WDM_DriveHandle h, char* out, size_t out_size) {
	WDM_PartitionMeta meta;
	if (WDM_GetPartitionMetadata(h, &meta) == WDM_OK) {
		char label[37];
		decode_partition_name(meta.partition_name, sizeof(meta.partition_name), label, sizeof(label));
		if (label[0] != '\0') {
			strncpy(out, label, out_size - 1);
			out[out_size - 1] = '\0';
			return;
		}
	}

	if (WDM_GetNameFromDrive(h, out, (uint8_t) out_size) == WDM_OK && out[0] != '\0') return;

	strncpy(out, "-", out_size - 1);
	out[out_size - 1] = '\0';
}

/* Look up the mountpoint for 'h' among 'mounts', flagging it in 'matched' so leftovers can still be reported. Returns NULL if nothing is mounted on 'h'. */
static const char* find_mountpoint(WDM_DriveHandle h, filesystem_mount_info_t* mounts, size_t mount_count, bool* matched) {
	for (size_t i = 0; i < mount_count; i++) {
		if (mounts[i].drive == h) {
			matched[i] = true;
			return mounts[i].mountpoint;
		}
	}
	return NULL;
}

static void print_lsblk_row(const char* addr, const char* type, const char* size_str, bool read_only, const char* name, const char* mountpoint) {
	printf("%-7s %-4s %10s  %-2s  %-23s  %s\n", addr, type, size_str, read_only ? "RO" : "RW", name, mountpoint ? mountpoint : "");
}

int drive_lsblk_cmd(int argc, char** argv) {
	ws_context_t* ws_ctx = ws_getCurrentContext();
	if (!ws_parse_args(ws_ctx, argc, argv)) return 1;

	bool has_filter = ws_has_arg(ws_ctx, "idx");
	uint32_t filter_idx = has_filter ? (uint32_t) atoi(ws_get_generic(ws_ctx, "idx")) : 0;

	uint32_t total_drives = 0;
	WDM_Enumerate(NULL, 0, &total_drives);
	if (total_drives == 0) {
		printf("No drives registered with WDM.\n");
		return 0;
	}

	WDM_DriveHandle drives[LSBLK_MAX_HANDLES];
	uint32_t fetched_drives = total_drives < LSBLK_MAX_HANDLES ? total_drives : LSBLK_MAX_HANDLES;
	WDM_Enumerate(drives, fetched_drives, &total_drives);

	if (has_filter && filter_idx >= fetched_drives) {
		printf("Error: drive index %u out of range (max %u).\n", filter_idx, fetched_drives - 1);
		return 1;
	}

	filesystem_mount_info_t mounts[FS_MOUNT_QUERY_MAX];
	size_t mount_count = 0;
	filesystem_enumerate_mounts(mounts, FS_MOUNT_QUERY_MAX, &mount_count);
	if (mount_count > FS_MOUNT_QUERY_MAX) mount_count = FS_MOUNT_QUERY_MAX;

	// Tracks which mounts get matched to a drive/partition we actually print below
	// We bind LSBLK_MAX_HANDLES to the WDM_MAX_DRIVES, so in theory we shouldn't need this
	// I still did it just in case we for some reason filter it out or something
	bool matched[FS_MOUNT_QUERY_MAX] = { false };

	printf("ADDR    TYPE       SIZE  RO  NAME/LABEL               MOUNTPOINT\n");
	printf("------- ---- ----------  --  -----------------------  ----------\n");

	for (uint32_t i = 0; i < fetched_drives; i++) {
		if (has_filter && i != filter_idx) continue;

		WDM_DriveInfo info;
		bool have_info = (WDM_GetInfo(drives[i], &info) == WDM_OK);

		char addr[16];
		snprintf(addr, sizeof(addr), "%u", i);

		char size_str[16] = "?";
		if (have_info) format_capacity(info.sector_count, info.sector_size, size_str, sizeof(size_str));

		char name[64] = "-";
		if (have_info) {
			strncpy(name, info.model, sizeof(name) - 1);
			name[sizeof(name) - 1] = '\0';
		}

		print_lsblk_row(addr, "disk", size_str, have_info && info.read_only, name, find_mountpoint(drives[i], mounts, mount_count, matched));

		uint32_t total_parts = 0;
		WDM_EnumeratePartitions(drives[i], NULL, 0, &total_parts);
		if (total_parts == 0) continue;

		WDM_DriveHandle parts[LSBLK_MAX_HANDLES];
		uint32_t fetched_parts = total_parts < LSBLK_MAX_HANDLES ? total_parts : LSBLK_MAX_HANDLES;
		WDM_EnumeratePartitions(drives[i], parts, fetched_parts, &total_parts);

		for (uint32_t j = 0; j < fetched_parts; j++) {
			WDM_DriveInfo pinfo;
			bool have_pinfo = (WDM_GetInfo(parts[j], &pinfo) == WDM_OK);

			char paddr[16];
			snprintf(paddr, sizeof(paddr), "%u:%u", i, j);

			char psize[16] = "?";
			if (have_pinfo) format_capacity(pinfo.sector_count, pinfo.sector_size, psize, sizeof(psize));

			char pname[64];
			get_display_name(parts[j], pname, sizeof(pname));

			print_lsblk_row(paddr, "part", psize, have_pinfo && pinfo.read_only, pname, find_mountpoint(parts[j], mounts, mount_count, matched));
		}

		// in case we didn't actually get all the drives
		// this means something is messed up in WDM
		if (total_parts > fetched_parts) {
			printf("        ... and %u more partitions on drive %u (not shown)\n", total_parts - fetched_parts, i);
		}
	}

	if (!has_filter && total_drives > fetched_drives) {
		printf("... and %u more drives (not shown)\n", total_drives - fetched_drives);
	}

	bool have_leftovers = false;
	for (size_t i = 0; i < mount_count; i++) {
		if (!matched[i] && !have_leftovers) {
			printf("\nOther mounted volumes (drive not shown above%s):\n", has_filter ? " or excluded by 'idx'" : "");
			have_leftovers = true;
		}
		if (!matched[i]) printf("  %s (%s)\n", mounts[i].mountpoint, fs_type_name(mounts[i].type));
	}

	return 0;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Mount points as synthetic ls entries
//
// VFS_Readdir only sees the filesystem being listed, so we cross-reference the mount table to make nested mount-point entries.
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

#define LS_MAX_SYNTH_ENTRIES 32
#define LS_SYNTH_NAME_MAX    64

typedef struct {
	char name[LS_SYNTH_NAME_MAX];
	bool is_mount_point; /**< true if a filesystem is mounted exactly here. false = just an unmounted ancestor segment on the way to a deeper mount. */
	bool shown;          /**< set once we've printed this entry, so we don't print it twice. */
} ls_synth_entry_t;

/**
 * @brief Collect deduplicated immediate child mount-point names under dir_path.
 *
 * Uses filesystem_enumerate_mounts() only, does not access the filesystem.
 */
static int collect_synthetic_mount_entries(const char* dir_path, ls_synth_entry_t* out, int max_out) {
	int count = 0;

	char prefix[VFS_PATH_MAX];
	if (strcmp(dir_path, "/") == 0) {
		strncpy(prefix, "/", sizeof(prefix));
	} else {
		snprintf(prefix, sizeof(prefix), "%s/", dir_path);
	}
	size_t prefix_len = strlen(prefix);

	filesystem_mount_info_t mounts[FS_MOUNT_QUERY_MAX];
	size_t mount_count = 0;
	filesystem_enumerate_mounts(mounts, FS_MOUNT_QUERY_MAX, &mount_count);
	if (mount_count > FS_MOUNT_QUERY_MAX) mount_count = FS_MOUNT_QUERY_MAX;

	for (size_t m = 0; m < mount_count; m++) {
		const char* mp = mounts[m].mountpoint;
		if (strncmp(mp, prefix, prefix_len) != 0) continue; // not under dir_path at all
		if (strcmp(mp, dir_path) == 0) continue; // dir_path IS this mount, not a child of it

		const char* remainder = mp + prefix_len;
		if (remainder[0] == '\0') continue; // shouldn't happen given the check above, but be safe

		const char* slash = strchr(remainder, '/');
		size_t seg_len = slash ? (size_t) (slash - remainder) : strlen(remainder);
		bool   is_exact = (slash == NULL); // no further '/' -> the mount sits exactly at this segment

		bool found = false;
		for (int j = 0; j < count; j++) {
			if (strlen(out[j].name) == seg_len && strncmp(out[j].name, remainder, seg_len) == 0) {
				if (is_exact) out[j].is_mount_point = true; // upgrade ancestor -> real mount if applicable
				found = true;
				break;
			}
		}
		if (found || count >= max_out) continue;

		size_t copy_len = seg_len < (LS_SYNTH_NAME_MAX - 1) ? seg_len : (LS_SYNTH_NAME_MAX - 1);
		memcpy(out[count].name, remainder, copy_len);
		out[count].name[copy_len] = '\0';
		out[count].is_mount_point = is_exact;
		out[count].shown = false;
		count++;
	}

	return count;
}

static void print_ls_dir_entry(const char* name, bool is_mount, uint8_t flags) {
	if (flags & LS_FLAG_ATTRIB) {
		printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "%s", is_mount ? "[M] " : "[D] ");
	}
	if (flags & LS_FLAG_SIZE) {
		display_set_colors(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG);
		printf(is_mount ? "<MNT>\t" : "<DIR>\t");
		display_set_colors_default();
	}
	vga_color color = is_mount ? VGA_COLOR_YELLOW : VGA_COLOR_LIGHT_CYAN;
	printf_color(color, PRINT_DEFAULT_BG, "%s\n", name);
}

int drive_ls_cmd(int argc, char** argv) {
	ws_context_t* ws_ctx = ws_getCurrentContext();
	if (!ws_parse_args(ws_ctx, argc, argv)) return 1;

	uint8_t flags = LS_FLAG_NONE;
	bool has_any_flag = false;
	if (ws_get_flag(ws_ctx, "attrib")) { flags |= LS_FLAG_ATTRIB; has_any_flag = true; }
	if (ws_get_flag(ws_ctx, "size")) { flags |= LS_FLAG_SIZE;   has_any_flag = true; }
	if (ws_get_flag(ws_ctx, "time")) { flags |= LS_FLAG_TIME;   has_any_flag = true; }
	if (ws_get_flag(ws_ctx, "long")) { flags |= (LS_FLAG_ATTRIB | LS_FLAG_SIZE | LS_FLAG_TIME); has_any_flag = true; }
	if (!has_any_flag) flags = LS_FLAG_DEFAULT;

	// No path given means we list the current directory
	char resolved[VFS_PATH_MAX];
	if (ws_has_arg(ws_ctx, "path")) {
		if (!resolve_path_or_report(ws_get_generic(ws_ctx, "path"), resolved, sizeof(resolved))) return 0;
	} else {
		strncpy(resolved, cwd, sizeof(resolved) - 1);
		resolved[sizeof(resolved) - 1] = '\0';
	}
	const char* vfs_path = resolved;

	ls_synth_entry_t synth[LS_MAX_SYNTH_ENTRIES];
	int synth_count = collect_synthetic_mount_entries(vfs_path, synth, LS_MAX_SYNTH_ENTRIES);

	VFS_FD fd;
	VFS_Status st = VFS_Opendir(vfs_path, &fd);
	if (st != VFS_OK) {
		// Nothing actually mounted at vfs_path itself, but if there are mounts nested underneath it 
		// Make a directory purely out of the mount table
		if (st != VFS_ERR_NOMNT || synth_count == 0) {
			printf_color(PRINT_COLOR_RED, PRINT_DEFAULT_BG, "Error opening directory: %s\n", vfs_strerror(st));
			return 0;
		}

		display_set_colors(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG);
		printf("Listing: %s\n", vfs_path);
		printf("----------------------------------------\n");
		display_set_colors_default();

		for (int i = 0; i < synth_count; i++) {
			print_ls_dir_entry(synth[i].name, synth[i].is_mount_point, flags);
		}

		printf("----------------------------------------\n");
		return 0;
	}

	display_set_colors(PRINT_COLOR_WHITE, PRINT_DEFAULT_BG);
	printf("Listing: %s\n", vfs_path);
	printf("----------------------------------------\n");
	display_set_colors_default();

	VFS_DirEnt ent;
	while (VFS_Readdir(fd, &ent) == VFS_OK) {
		// Empty name signals end of directory
		if (ent.name[0] == '\0') break;

		// Skip . and ..
		if (strcmp(ent.name, ".") == 0 || strcmp(ent.name, "..") == 0) continue;

		// If a real mount sits exactly at this name, let the synthetic pass below own it so it gets mount styling
		bool defer_to_synth = false;
		for (int i = 0; i < synth_count; i++) {
			if (synth[i].is_mount_point && strcmp(synth[i].name, ent.name) == 0) {
				synth[i].shown = true;
				defer_to_synth = true;
				break;
			}
		}
		if (defer_to_synth) continue;

		// A plain ancestor segment (not itself a mount) is still real filesystem data here, so just mark it shown and let the normal listing below print it.
		for (int i = 0; i < synth_count; i++) {
			if (!synth[i].is_mount_point && strcmp(synth[i].name, ent.name) == 0) {
				synth[i].shown = true;
				break;
			}
		}

		vga_color item_color = ent.is_directory ? VGA_COLOR_LIGHT_CYAN : VGA_COLOR_LIGHT_GREEN;

		if (flags & LS_FLAG_ATTRIB) {
			printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "%s", ent.is_directory ? "[D] " : "[-] ");
		}

		if (flags & LS_FLAG_SIZE) {
			display_set_colors(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG);
			if (ent.is_directory) {
				printf("<DIR>\t");
			} else {
				print_file_size(ent.size);
				printf("\t");
			}
			display_set_colors_default();
		}

		// TODO: We don't have time flags...

		printf_color(item_color, PRINT_DEFAULT_BG, "%s\n", ent.name);
	}

	// Any mount-derived entries the real filesystem didn't already show 
	for (int i = 0; i < synth_count; i++) {
		if (!synth[i].shown) print_ls_dir_entry(synth[i].name, synth[i].is_mount_point, flags);
	}

	VFS_Close(fd);
	printf("----------------------------------------\n");
	return 0;
}

#define MAX_PATH_LEN   256
#define INDENT_BRANCH  "\xC3\xC4\xC4 "
#define INDENT_LAST    "\xC0\xC4\xC4 "
#define INDENT_CONT    "\xB3   "
#define INDENT_SPACE   "    "

void print_tree_recursive(const char* vfs_path, const char* indent_prefix);

static void print_tree_dir_entry(const char* parent_path, const char* indent_prefix, bool is_last, const char* name, bool is_mount) {
	const char* branch_seg = is_last ? INDENT_LAST : INDENT_BRANCH;
	const char* next_indent_seg = is_last ? INDENT_SPACE : INDENT_CONT;

	printf_color(PRINT_COLOR_DARK_GREY, PRINT_DEFAULT_BG, "%s%s", indent_prefix, branch_seg);
	vga_color color = is_mount ? VGA_COLOR_YELLOW : VGA_COLOR_LIGHT_CYAN;
	printf_color(color, PRINT_DEFAULT_BG, "%s%s\n", name, is_mount ? " [mount]" : "");

	char child_path[VFS_PATH_MAX];
	snprintf(child_path, sizeof(child_path), "%s/%s", (strcmp(parent_path, "/") == 0) ? "" : parent_path, name);

	char next_indent[MAX_PATH_LEN];
	snprintf(next_indent, sizeof(next_indent), "%s%s", indent_prefix, next_indent_seg);

	print_tree_recursive(child_path, next_indent);
}

void print_tree_recursive(const char* vfs_path, const char* indent_prefix) {
	ls_synth_entry_t synth[LS_MAX_SYNTH_ENTRIES];
	int synth_count = collect_synthetic_mount_entries(vfs_path, synth, LS_MAX_SYNTH_ENTRIES);

	VFS_FD fd;
	VFS_Status st = VFS_Opendir(vfs_path, &fd);

	if (st != VFS_OK) {
		if (st != VFS_ERR_NOMNT || synth_count == 0) {
			printf_color(PRINT_COLOR_RED, PRINT_DEFAULT_BG, "%sREAD ERROR (%s)\n", indent_prefix, vfs_strerror(st));
			return;
		}

		// Nothing real mounted here, but the mount table says there's something below 
		for (int i = 0; i < synth_count; i++) {
			print_tree_dir_entry(vfs_path, indent_prefix, i == synth_count - 1, synth[i].name, synth[i].is_mount_point);
		}
		return;
	}

	// Real filesystem mounted here.
	// Count real entries and track mount points that already exist to avoid duplicates.
	bool synth_matched[LS_MAX_SYNTH_ENTRIES] = { false };
	int total_entries = 0;
	VFS_DirEnt ent;
	while (VFS_Readdir(fd, &ent) == VFS_OK && ent.name[0] != '\0') {
		if (strcmp(ent.name, ".") == 0 || strcmp(ent.name, "..") == 0) continue;
		total_entries++;
		for (int i = 0; i < synth_count; i++) {
			if (strcmp(synth[i].name, ent.name) == 0) synth_matched[i] = true;
		}
	}

	// Rewind isn't in VFS, so close and reopen
	VFS_Close(fd);
	if (VFS_Opendir(vfs_path, &fd) != VFS_OK) return;

	int extra_synth = 0;
	for (int i = 0; i < synth_count; i++) if (!synth_matched[i]) extra_synth++;
	int total_display = total_entries + extra_synth;

	int entry_count = 0;
	while (VFS_Readdir(fd, &ent) == VFS_OK && ent.name[0] != '\0') {
		if (strcmp(ent.name, ".") == 0 || strcmp(ent.name, "..") == 0) continue;

		entry_count++;
		bool is_last = (entry_count == total_display);

		// If an actual mount sits exactly at this name, style/recurse it as a mount
		bool is_exact_mount = false;
		for (int i = 0; i < synth_count; i++) {
			if (synth[i].is_mount_point && strcmp(synth[i].name, ent.name) == 0) { is_exact_mount = true; break; }
		}

		if (is_exact_mount || ent.is_directory) {
			print_tree_dir_entry(vfs_path, indent_prefix, is_last, ent.name, is_exact_mount);
		} else {
			const char* branch_seg = is_last ? INDENT_LAST : INDENT_BRANCH;
			printf_color(PRINT_COLOR_DARK_GREY, PRINT_DEFAULT_BG, "%s%s", indent_prefix, branch_seg);
			printf_color(PRINT_COLOR_LIGHT_GREEN, PRINT_DEFAULT_BG, "%s", ent.name);
			display_set_colors(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG);
			printf(" (");
			print_file_size(ent.size);
			printf(")\n");
			display_set_colors_default();
		}
	}

	VFS_Close(fd);

	// Any mount-table entries the real filesystem didn't already show
	for (int i = 0; i < synth_count; i++) {
		if (synth_matched[i]) continue;
		entry_count++;
		print_tree_dir_entry(vfs_path, indent_prefix, entry_count == total_display, synth[i].name, synth[i].is_mount_point);
	}
}


int drive_tree_cmd(int argc, char** argv) {
	ws_context_t* ws_ctx = ws_getCurrentContext();
	if (!ws_parse_args(ws_ctx, argc, argv)) return 1;

	char resolved[VFS_PATH_MAX];
	if (!resolve_path_or_report(ws_get_generic(ws_ctx, "path"), resolved, sizeof(resolved))) return 0;
	const char* vfs_path = resolved;

	printf("File tree for %s\n", vfs_path);
	printf("========================================\n");
	printf_color(PRINT_COLOR_DARK_GREY, PRINT_DEFAULT_BG, "[ROOT] %s\n", vfs_path);

	print_tree_recursive(vfs_path, "");

	printf("========================================\n");
	return 0;
}

int drive_mkdir_cmd(int argc, char** argv) {
	ws_context_t* ws_ctx = ws_getCurrentContext();
	if (!ws_parse_args(ws_ctx, argc, argv)) return 1;

	char resolved[VFS_PATH_MAX];
	if (!resolve_path_or_report(ws_get_generic(ws_ctx, "path"), resolved, sizeof(resolved))) return 0;

	VFS_Status st = VFS_Mkdir(resolved);
	if (st == VFS_OK) {
		printf("Directory created: %s\n", resolved);
	} else if (st == VFS_ERR_EXIST) {
		printf("Error: '%s' already exists.\n", resolved);
	} else {
		printf("Error: %s\n", vfs_strerror(st));
	}

	return 0;
}

int drive_touch_cmd(int argc, char** argv) {
	ws_context_t* ws_ctx = ws_getCurrentContext();
	if (!ws_parse_args(ws_ctx, argc, argv)) return 1;

	char resolved[VFS_PATH_MAX];
	if (!resolve_path_or_report(ws_get_generic(ws_ctx, "path"), resolved, sizeof(resolved))) return 0;

	VFS_FD fd;
	VFS_Status st = VFS_Open(resolved, (VFS_OpenFlags) (VFS_O_WRONLY | VFS_O_CREAT), &fd);
	if (st != VFS_OK) {
		printf("Error: %s\n", vfs_strerror(st));
		return 0;
	}

	VFS_Close(fd);
	printf("Touched: %s\n", resolved);
	return 0;
}

int drive_write_cmd(int argc, char** argv) {
	// 'text' is a variadic trailing argument, so we have to parse this command manually instead of using ws_parse_args(). 

	if (argc < 3) {
		printf("Usage: write [-a|--append] <vfs_path> <text...>\n");
		printf("Example: write /note.txt Hello world\n");
		printf("Example: write -a /note.txt More text\n");
		return 0;
	}

	bool append = false;
	int path_idx = 1;

	if (strcmp(argv[1], "-a") == 0 || strcmp(argv[1], "--append") == 0) {
		if (argc < 4) {
			printf("Error: not enough arguments for append mode.\n");
			return 0;
		}
		append = true;
		path_idx = 2;
	}

	char resolved[VFS_PATH_MAX];
	if (!resolve_path_or_report(argv[path_idx], resolved, sizeof(resolved))) return 0;
	const char* path = resolved;
	int text_start = path_idx + 1;

	VFS_OpenFlags flags = (VFS_OpenFlags) (VFS_O_WRONLY | VFS_O_CREAT | (append ? VFS_O_APPEND : VFS_O_TRUNC));

	VFS_FD fd;
	VFS_Status st = VFS_Open(path, flags, &fd);
	if (st != VFS_OK) {
		printf("Error opening '%s': %s\n", path, vfs_strerror(st));
		return 0;
	}

	for (int i = text_start; i < argc; i++) {
		if (i > text_start) {
			size_t written = 0;
			st = VFS_Write(fd, " ", 1, &written);
			if (st != VFS_OK) goto write_error;
		}
		size_t len = strlen(argv[i]);
		size_t written = 0;
		st = VFS_Write(fd, argv[i], len, &written);
		if (st != VFS_OK) goto write_error;
	}

	{
		size_t written = 0;
		VFS_Write(fd, "\n", 1, &written);
	}

	VFS_Close(fd);
	printf("%s to: %s\n", append ? "Appended" : "Written", path);
	return 0;

write_error:
	printf("Write error: %s\n", vfs_strerror(st));
	VFS_Close(fd);
	return 0;
}

#define CAT_BUFFER_SIZE 512

int drive_cat_cmd(int argc, char** argv) {
	ws_context_t* ws_ctx = ws_getCurrentContext();
	if (!ws_parse_args(ws_ctx, argc, argv)) return 1;

	char resolved[VFS_PATH_MAX];
	if (!resolve_path_or_report(ws_get_generic(ws_ctx, "path"), resolved, sizeof(resolved))) return 0;

	VFS_FD fd;
	VFS_Status st = VFS_Open(resolved, VFS_O_RDONLY, &fd);
	if (st != VFS_OK) {
		printf("Error opening '%s': %s\n", resolved, vfs_strerror(st));
		return 0;
	}

	char buf[CAT_BUFFER_SIZE];
	size_t bytes_read = 0;

	printf("\n--- Start of file ---\n");
	do {
		st = VFS_Read(fd, buf, sizeof(buf) - 1, &bytes_read);
		if (st != VFS_OK) {
			printf("\nRead error: %s\n", vfs_strerror(st));
			break;
		}
		if (bytes_read > 0) {
			buf[bytes_read] = '\0';
			printf("%s", buf);
		}
	} while (bytes_read == sizeof(buf) - 1);

	VFS_Close(fd);
	printf("\n---  End of file  ---\n");
	return 0;
}

int drive_cd_cmd(int argc, char** argv) {
	ws_context_t* ws_ctx = ws_getCurrentContext();
	if (!ws_parse_args(ws_ctx, argc, argv)) return 1;

	if (!ws_has_arg(ws_ctx, "path")) {
		printf("Current directory: %s\n", cwd);
		return 0;
	}

	char resolved[VFS_PATH_MAX];
	if (!resolve_path_or_report(ws_get_generic(ws_ctx, "path"), resolved, sizeof(resolved))) return 0;

	// Make sure it actually exists and is a directory before committing to it.
	VFS_FD fd;
	VFS_Status st = VFS_Opendir(resolved, &fd);
	if (st == VFS_OK) {
		VFS_Close(fd);
	} else if (st == VFS_ERR_NOMNT) {
		ls_synth_entry_t synth[LS_MAX_SYNTH_ENTRIES];
		if (collect_synthetic_mount_entries(resolved, synth, LS_MAX_SYNTH_ENTRIES) == 0) {
			printf("Error: %s\n", vfs_strerror(st));
			return 0;
		}
		// Else: no real filesystem here, but it leads to a mount somewhere below - allow it.
	} else {
		printf("Error: %s\n", vfs_strerror(st));
		return 0;
	}

	strncpy(cwd, resolved, sizeof(cwd) - 1);
	cwd[sizeof(cwd) - 1] = '\0';

	// prefix_buf is static storage, so this pointer stays valid until the next call.
	update_terminal_prefix();
	return 0;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Command registration
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static void mount_help(int argc, char** argv);
static const ws_command_argument_t mount_args[] = {
	{ WS_ARG_TYPE_GENERIC, true,  "path", NULL, "Absolute VFS mount point (\"/\" \"/mnt/usb\", etc.)." },
	{ WS_ARG_TYPE_GENERIC, true,  "addr", NULL, "Device address: 'N' or 'N:M' (use lsblk)." },
	{ WS_ARG_TYPE_GENERIC, false, "fs",   NULL, "Filesystem to bind (see the list below). Omit to auto-detect." },
};
static const ws_command_t mount_command = {
	.command_name = "mount",
	.aliases = NULL,
	.alias_count = 0,
	.main_void = NULL,
	.main_func = drive_mount_cmd,
	.env_func = NULL,
	.help_func = mount_help,
	.major = 0, .minor = 0, .patch = 0,
	.arguments = mount_args,
	.arguments_count = sizeof(mount_args) / sizeof(mount_args[0]),
};
static void mount_help(int argc, char** argv) {
	(void) argc; (void) argv;
	ws_printCommandHelp(&mount_command);
	print_fs_name_list();
}

static const ws_command_argument_t lsblk_args[] = {
	{ WS_ARG_TYPE_GENERIC, false, "idx", NULL, "Only show this top-level drive index (and its partitions)." },
};
static const ws_command_t lsblk_command = {
	.command_name = "lsblk",
	.aliases = NULL,
	.alias_count = 0,
	.main_void = NULL,
	.main_func = drive_lsblk_cmd,
	.env_func = NULL,
	.help_func = NULL,
	.major = 0, .minor = 0, .patch = 0,
	.arguments = lsblk_args,
	.arguments_count = sizeof(lsblk_args) / sizeof(lsblk_args[0]),
};

static const ws_command_argument_t unmount_args[] = {
	{ WS_ARG_TYPE_GENERIC, true, "path", NULL, "VFS path to unmount." },
};
static const ws_command_t unmount_command = {
	.command_name = "unmount",
	.aliases = NULL,
	.alias_count = 0,
	.main_void = NULL,
	.main_func = drive_unmount_cmd,
	.env_func = NULL,
	.help_func = NULL,
	.major = 0, .minor = 0, .patch = 0,
	.arguments = unmount_args,
	.arguments_count = sizeof(unmount_args) / sizeof(unmount_args[0]),
};

static const ws_command_argument_t cd_args[] = {
	{ WS_ARG_TYPE_GENERIC, false, "path", NULL, "Directory to change into. Omit to print the current directory." },
};
static const ws_command_t cd_command = {
	.command_name = "cd",
	.aliases = NULL,
	.alias_count = 0,
	.main_void = NULL,
	.main_func = drive_cd_cmd,
	.env_func = NULL,
	.help_func = NULL,
	.major = 0, .minor = 0, .patch = 0,
	.arguments = cd_args,
	.arguments_count = sizeof(cd_args) / sizeof(cd_args[0]),
};

static const ws_command_argument_t tree_args[] = {
	{ WS_ARG_TYPE_GENERIC, true, "path", NULL, "Directory to print the tree of." },
};
static const ws_command_t tree_command = {
	.command_name = "tree",
	.aliases = NULL,
	.alias_count = 0,
	.main_void = NULL,
	.main_func = drive_tree_cmd,
	.env_func = NULL,
	.help_func = NULL,
	.major = 0, .minor = 0, .patch = 0,
	.arguments = tree_args,
	.arguments_count = sizeof(tree_args) / sizeof(tree_args[0]),
};

static const ws_command_argument_t ls_args[] = {
	{ WS_ARG_TYPE_GENERIC, false, "path",     NULL, "Directory to list. Defaults to the current directory." },
	{ WS_ARG_TYPE_FLAG,    false, "--attrib", "-a", "Show the [D]/[M]/[-] type column." },
	{ WS_ARG_TYPE_FLAG,    false, "--size",   "-s", "Show the size/<DIR>/<MNT> column." },
	{ WS_ARG_TYPE_FLAG,    false, "--time",   "-t", "Show modification time (TODO)." },
	{ WS_ARG_TYPE_FLAG,    false, "--long",   "-l", "Shorthand for -a -s -t." },
};
static const ws_command_t ls_command = {
	.command_name = "ls",
	.aliases = NULL,
	.alias_count = 0,
	.main_void = NULL,
	.main_func = drive_ls_cmd,
	.env_func = NULL,
	.help_func = NULL,
	.major = 0, .minor = 0, .patch = 0,
	.arguments = ls_args,
	.arguments_count = sizeof(ls_args) / sizeof(ls_args[0]),
};

static const ws_command_argument_t cat_args[] = {
	{ WS_ARG_TYPE_GENERIC, true, "path", NULL, "File to print the contents of." },
};
static const ws_command_t cat_command = {
	.command_name = "cat",
	.aliases = NULL,
	.alias_count = 0,
	.main_void = NULL,
	.main_func = drive_cat_cmd,
	.env_func = NULL,
	.help_func = NULL,
	.major = 0, .minor = 0, .patch = 0,
	.arguments = cat_args,
	.arguments_count = sizeof(cat_args) / sizeof(cat_args[0]),
};

static const ws_command_argument_t mkdir_args[] = {
	{ WS_ARG_TYPE_GENERIC, true, "path", NULL, "Directory to create." },
};
static const ws_command_t mkdir_command = {
	.command_name = "mkdir",
	.aliases = NULL,
	.alias_count = 0,
	.main_void = NULL,
	.main_func = drive_mkdir_cmd,
	.env_func = NULL,
	.help_func = NULL,
	.major = 0, .minor = 0, .patch = 0,
	.arguments = mkdir_args,
	.arguments_count = sizeof(mkdir_args) / sizeof(mkdir_args[0]),
};

static const ws_command_argument_t touch_args[] = {
	{ WS_ARG_TYPE_GENERIC, true, "path", NULL, "File to create, or update if it already exists." },
};
static const ws_command_t touch_command = {
	.command_name = "touch",
	.aliases = NULL,
	.alias_count = 0,
	.main_void = NULL,
	.main_func = drive_touch_cmd,
	.env_func = NULL,
	.help_func = NULL,
	.major = 0, .minor = 0, .patch = 0,
	.arguments = touch_args,
	.arguments_count = sizeof(touch_args) / sizeof(touch_args[0]),
};

static void write_help(int argc, char** argv);
static const ws_command_argument_t write_args[] = {
	{ WS_ARG_TYPE_GENERIC, true,  "path", NULL, "File to write to." },
	{ WS_ARG_TYPE_FLAG,    false, "--append", "-a", "Append to the file instead of overwriting it." },
};
static const ws_command_t write_command = {
	.command_name = "write",
	.aliases = NULL,
	.alias_count = 0,
	.main_void = NULL,
	.main_func = drive_write_cmd,
	.env_func = NULL,
	.help_func = write_help,
	.major = 0, .minor = 0, .patch = 0,
	.arguments = write_args,
	.arguments_count = sizeof(write_args) / sizeof(write_args[0]),
};
static void write_help(int argc, char** argv) {
	(void) argc; (void) argv;
	ws_printCommandHelp(&write_command);
	printf("Everything after <path> is written as the file's contents.\n");
}

extern "C" {
	extern void register_drive_diag_commands();
}

/** Register every drive/filesystem command with WallShell. Call once during startup. */
extern "C" void register_drive_commands(void) {
	register_drive_filesystems();

	ws_registerCommand(mount_command);
	ws_registerCommand(unmount_command);
	ws_registerCommand(lsblk_command);
	ws_registerCommand(cd_command);
	ws_registerCommand(ls_command);
	ws_registerCommand(tree_command);
	ws_registerCommand(cat_command);
	ws_registerCommand(mkdir_command);
	ws_registerCommand(touch_command);
	ws_registerCommand(write_command);
	register_drive_diag_commands();
}