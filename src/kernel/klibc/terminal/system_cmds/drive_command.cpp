#include <terminal/commands/system_commands.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <filesystem/wdm.h>
#include <filesystem/vfs.h>
#include <filesystem/fat/fat.h>
#include <filesystem/fat/fat32_vfs.h>


#include <system/timer.h>
#include <klibc/kprint.h>


/*
 * Tracks every VFS_Mount() call made through this command layer.
 * This is so unmount can find the right context to free and the right VFS path to pass to VFS_Unmount().
 */

#define CMD_MOUNT_MAX VFS_MOUNT_MAX

typedef struct {
	bool             active;
	char             vfs_path[VFS_PATH_MAX]; /**< Mount point, e.g. "/"        */
	vfs_fat32_ctx_t* ctx;                    /**< Binding-layer context        */
	// uint8_t          pdrv;                   /**< FatFs pdrv (for ls/tree)     */
} mount_entry_t;

static mount_entry_t mount_table[CMD_MOUNT_MAX];

/** Find a mount entry by VFS path, or NULL. */
mount_entry_t* find_mount(const char* vfs_path) {
	for (int i = 0; i < CMD_MOUNT_MAX; i++) {
		if (mount_table[i].active && strcmp(mount_table[i].vfs_path, vfs_path) == 0)
			return &mount_table[i];
	}
	return NULL;
}

/** Allocate a free slot, or NULL if the table is full. */
mount_entry_t* alloc_mount_slot(void) {
	for (int i = 0; i < CMD_MOUNT_MAX; i++) {
		if (!mount_table[i].active) return &mount_table[i];
	}
	return NULL;
}

static const char* vfs_strerror(VFS_Status st) {
	switch (st) {
		case VFS_OK:             return "OK";
		case VFS_ERR_INVALID:    return "invalid argument";
		case VFS_ERR_IO:         return "I/O error";
		case VFS_ERR_NOENT:      return "no such file or directory";
		case VFS_ERR_EXIST:      return "already exists";
		case VFS_ERR_NOTDIR:     return "not a directory";
		case VFS_ERR_ISDIR:      return "is a directory";
		case VFS_ERR_NOTEMPTY:   return "directory not empty";
		case VFS_ERR_NOMNT:      return "path not mounted";
		case VFS_ERR_BADF:       return "bad file descriptor";
		case VFS_ERR_BUSY:       return "resource busy";
		case VFS_ERR_NOSPACE:    return "no space left";
		case VFS_ERR_TOOLONG:    return "path too long";
		case VFS_ERR_OVERFLOW:   return "offset overflow";
		case VFS_ERR_MNTFULL:    return "mount table full";
		case VFS_ERR_FDFULL:     return "file descriptor table full";
		case VFS_ERR_UNSUPPORTED: return "unsupported";
		default:                 return "unknown error";
	}
}

void print_file_size(uint64_t size) {
	if (size < 1024) {
		printf("%u B", (unsigned int) size);
	} else if (size < (1024 * 1024)) {
		printf("%u KB", (unsigned int) (size / 1024));
	} else if (size < (1024ULL * 1024 * 1024)) {
		printf("%u MB", (unsigned int) (size / (1024 * 1024)));
	} else {
		printf("%u GB", (unsigned int) (size / (1024ULL * 1024 * 1024)));
	}
}

int drive_info_cmd(int argc, char** argv) {
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

	// Specific Drive Requested (e.g., 'drive info 1')
	if (argc > 1) {
		uint32_t idx = (uint32_t) atoi(argv[1]);
		if (idx >= fetched) {
			printf("Error: Drive index %u out of range (max %u).\n", idx, fetched - 1);
			return 1;
		}

		WDM_DriveInfo info;
		if (WDM_GetInfo(handles[idx], &info) != WDM_OK) {
			printf("Drive %u: <failed to query details>\n", idx);
			return 1;
		}

		uint64_t capacity_mb = (info.sector_count * info.sector_size) / (1024 * 1024);

		printf("Drive %u:\n", idx);
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
		printf("%-2u  %-19.19s  %6llu MB  %s%s\n",
			i,
			info.model,
			(unsigned long long)capacity_mb,
			info.read_only ? "RO" : "RW",
			info.removable ? " (Removable)" : "");
	}

	// Mounted drive summary
	bool any = false;
	for (int i = 0; i < CMD_MOUNT_MAX; i++) {
		if (!mount_table[i].active) continue;
		if (!any) {
			printf("Mounted Volumes:\n");
			any = true;
		}
		// printf("  pdrv %u -> %s\n", (unsigned) mount_table[i].pdrv, mount_table[i].vfs_path);
		printf("\t%s\n", mount_table[i].vfs_path);
	}
	if (!any) printf("No volumes currently mounted.\n");

	return 0;
}

// I have ZERO clue what this was ever supposed to do...
int drive_test_cmd(int argc, char** argv) {
	(void) argc; (void) argv;
	return 0;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Mount / Unmount
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

int drive_mount_cmd(int argc, char** argv) {
	/*
	 * Usage: drive mount <vfs_path> <wdm_index> [pdrv]
	 *
	 *   vfs_path  : Absolute VFS mount point, e.g. "/"  or "/mnt/usb"
	 *   wdm_index : Index into WDM_Enumerate results (0-based)
	 *   pdrv      : This is a "hidden" field, it's not really meant to be used outside of very specific circumstances
	 */
	if (argc < 3) {
		printf("Usage: drive mount <vfs_path> <wdm_index> [pdrv]\n");
		printf("  vfs_path  : absolute mount point (e.g. / or /mnt/usb)\n");
		printf("  wdm_index : index into 'drive info' WDM list\n");
		// printf("  pdrv      : FatFs physical drive slot (optional, default=wdm_index)\n");
		return 0;
	}

	const char* vfs_path = argv[1];
	int         wdm_idx = argv[2][0] - '0';

	if (strlen(vfs_path) >= VFS_PATH_MAX) {
		printf("Error: mount path too long.\n");
		return 0;
	}
	if (find_mount(vfs_path)) {
		printf("Error: '%s' is already mounted.\n", vfs_path);
		return 0;
	}

	/* Resolve WDM handle. */
	uint32_t total = 0;
	WDM_Enumerate(NULL, 0, &total);
	if ((uint32_t) wdm_idx >= total) {
		printf("Error: WDM index %d out of range (have %u drives).\n", wdm_idx, (unsigned) total);
		return 0;
	}
	WDM_DriveHandle handles[16];
	uint32_t fetched = total < 16 ? total : 16;
	WDM_Enumerate(handles, fetched, &total);
	WDM_DriveHandle h = handles[wdm_idx];

	/* Allocate the fatfs binding. */
	vfs_fat32_ctx_t* ctx = vfs_fat32_alloc();
	if (!ctx) {
		printf("Error: failed to allocate FAT32 context.\n");
		return 0;
	}

	/* Reserve a slot in our local mount table. */
	mount_entry_t* slot = alloc_mount_slot();
	if (!slot) {
		printf("Error: local mount table full.\n");
		vfs_fat32_free(ctx);
		return 0;
	}

	printf("Mounting drive %d at '%s'...\n", wdm_idx, vfs_path);

	VFS_Status st = VFS_Mount(vfs_path, h, &vfs_fat32_ops, ctx);
	if (st != VFS_OK) {
		printf("Error: VFS_Mount failed: %s\n", vfs_strerror(st));
		vfs_fat32_free(ctx);
		return 0;
	}

	slot->active = true;
	slot->ctx = ctx;
	strncpy(slot->vfs_path, vfs_path, VFS_PATH_MAX - 1);
	slot->vfs_path[VFS_PATH_MAX - 1] = '\0';

	printf("Mounted successfully at '%s'.\n", vfs_path);
	return 0;
}

int drive_unmount_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: drive unmount <vfs_path>\n");
		printf("Example: drive unmount /mnt/usb\n");
		return 0;
	}

	const char* vfs_path = argv[1];

	mount_entry_t* slot = find_mount(vfs_path);
	if (!slot) {
		printf("Error: '%s' is not currently mounted.\n", vfs_path);
		return 0;
	}

	VFS_Status st = VFS_Unmount(vfs_path);
	if (st != VFS_OK) {
		printf("Error: VFS_Unmount failed: %s\n", vfs_strerror(st));
		return 0;
	}

	vfs_fat32_free(slot->ctx);
	slot->active = false;
	slot->ctx = NULL;

	printf("Unmounted '%s'.\n", vfs_path);
	return 0;
}

/**
 * @brief Programmatically mount a drive for early-boot use.
 *
 * Equivalent to drive_mount_cmd but callable directly from C.
 * The caller must have already called WDM_Register() to obtain @p handle.
 *
 * @param vfs_path  Absolute VFS mount point (e.g. "/").
 * @param handle    Live WDM_DriveHandle for the underlying device.
 * @param pdrv      FatFs physical drive slot to use (0 ... VFS_FAT32_OPEN_MAX-1).
 *
 * @return true on success, false on any failure.
 */

bool mount_drive(const char* vfs_path, WDM_DriveHandle handle, int pdrv) {
	(void) pdrv; // TODO: remove this
	mount_entry_t* slot = alloc_mount_slot();
	vfs_fat32_ctx_t* ctx = vfs_fat32_alloc();

	if (!ctx) {
		printf("NO CONTEXT!\n");
		return false;
	}

	VFS_Status st = VFS_Mount(vfs_path, handle, &vfs_fat32_ops, ctx);
	if (st != VFS_OK) {
		printf("VFS_Status: %d!\n", st);
		vfs_fat32_free(ctx);
		return false;
	}

	slot->ctx = ctx;
	slot->active = true;
	strncpy(slot->vfs_path, vfs_path, VFS_PATH_MAX - 1);
	slot->vfs_path[VFS_PATH_MAX - 1] = '\0';
	return true;
}

#define LS_FLAG_NONE    0x00
#define LS_FLAG_ATTRIB  0x01
#define LS_FLAG_SIZE    0x02
#define LS_FLAG_TIME    0x04
#define LS_FLAG_DEFAULT (LS_FLAG_SIZE | LS_FLAG_ATTRIB)

uint8_t parse_ls_flags(const char* flag_str) {
	uint8_t flags = LS_FLAG_NONE;
	for (int i = 1; flag_str[i] != '\0'; i++) {
		switch (flag_str[i]) {
			case 'a': flags |= LS_FLAG_ATTRIB; break;
			case 's': flags |= LS_FLAG_SIZE;   break;
			case 't': flags |= LS_FLAG_TIME;   break;
			case 'l': flags |= (LS_FLAG_ATTRIB | LS_FLAG_SIZE | LS_FLAG_TIME); break;
			default:
				printf("Warning: unknown flag '-%c' ignored.\n", flag_str[i]);
				break;
		}
	}
	return flags;
}

int drive_ls_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: drive ls <vfs_path> [flags]\n");
		return 0;
	}

	uint8_t flags = LS_FLAG_DEFAULT;
	const char* vfs_path = NULL;

	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			flags |= parse_ls_flags(argv[i]);
		} else if (!vfs_path) {
			vfs_path = argv[i];
		}
	}

	if (!vfs_path) {
		printf("Error: no path provided.\n");
		return 0;
	}

	VFS_FD fd;
	VFS_Status st = VFS_Opendir(vfs_path, &fd);
	if (st != VFS_OK) {
		printf_color(PRINT_COLOR_RED, PRINT_DEFAULT_BG, "Error opening directory: %s\n", vfs_strerror(st));
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

	VFS_Close(fd);
	printf("----------------------------------------\n");
	return 0;
}

#define MAX_PATH_LEN   256
#define INDENT_BRANCH  "\xC3\xC4\xC4 "
#define INDENT_LAST    "\xC0\xC4\xC4 "
#define INDENT_CONT    "\xB3   "
#define INDENT_SPACE   "    "

void print_tree_recursive(const char* vfs_path, const char* indent_prefix) {
	VFS_FD fd;
	VFS_Status st = VFS_Opendir(vfs_path, &fd);
	if (st != VFS_OK) {
		printf_color(PRINT_COLOR_RED, PRINT_DEFAULT_BG, "%sREAD ERROR (%s)\n", indent_prefix, vfs_strerror(st));
		return;
	}

	int total_entries = 0;
	VFS_DirEnt ent;
	while (VFS_Readdir(fd, &ent) == VFS_OK && ent.name[0] != '\0') {
		if (strcmp(ent.name, ".") != 0 && strcmp(ent.name, "..") != 0) total_entries++;
	}

	// Rewind isn't in VFS, so close and reopen 
	VFS_Close(fd);
	if (VFS_Opendir(vfs_path, &fd) != VFS_OK) return;

	int entry_count = 0;
	while (VFS_Readdir(fd, &ent) == VFS_OK && ent.name[0] != '\0') {
		if (strcmp(ent.name, ".") == 0 || strcmp(ent.name, "..") == 0) continue;

		entry_count++;
		bool is_last = (entry_count == total_entries);

		const char* branch_seg = is_last ? INDENT_LAST : INDENT_BRANCH;
		const char* next_indent_seg = is_last ? INDENT_SPACE : INDENT_CONT;

		printf_color(PRINT_COLOR_DARK_GREY, PRINT_DEFAULT_BG, "%s%s", indent_prefix, branch_seg);

		if (ent.is_directory) {
			printf_color(PRINT_COLOR_LIGHT_CYAN, PRINT_DEFAULT_BG, "%s\n", ent.name);

			/* Build child path */
			char child_path[VFS_PATH_MAX];
			snprintf(child_path, sizeof(child_path), "%s/%s", (strcmp(vfs_path, "/") == 0) ? "" : vfs_path, ent.name);

			char next_indent[MAX_PATH_LEN];
			snprintf(next_indent, sizeof(next_indent), "%s%s", indent_prefix, next_indent_seg);

			print_tree_recursive(child_path, next_indent);
		} else {
			printf_color(PRINT_COLOR_LIGHT_GREEN, PRINT_DEFAULT_BG, "%s", ent.name);
			display_set_colors(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG);
			printf(" (");
			print_file_size(ent.size);
			printf(")\n");
			display_set_colors_default();
		}
	}

	VFS_Close(fd);
}

int drive_tree_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: drive tree <vfs_path>\n");
		printf("Example: drive tree /\n");
		printf("Example: drive tree /mnt/usb\n");
		return 0;
	}

	const char* vfs_path = argv[1];

	/* We no longer need resolve_mount() or vfs_to_fatfs_path() here.
	 * VFS_Opendir will return VFS_ERR_NOMNT if the path isn't valid.
	 */

	printf("File tree for %s\n", vfs_path);
	printf("========================================\n");
	printf_color(PRINT_COLOR_DARK_GREY, PRINT_DEFAULT_BG, "[ROOT] %s\n", vfs_path);

	print_tree_recursive(vfs_path, "");

	printf("========================================\n");
	return 0;
}

int drive_mkdir_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: drive mkdir <vfs_path>\n");
		printf("Example: drive mkdir /docs\n");
		return 0;
	}

	VFS_Status st = VFS_Mkdir(argv[1]);
	if (st == VFS_OK) {
		printf("Directory created: %s\n", argv[1]);
	} else if (st == VFS_ERR_EXIST) {
		printf("Error: '%s' already exists.\n", argv[1]);
	} else {
		printf("Error: %s\n", vfs_strerror(st));
	}

	return 0;
}

int drive_touch_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: drive touch <vfs_path>\n");
		printf("Example: drive touch /hello.txt\n");
		return 0;
	}

	VFS_FD     fd;
	VFS_Status st = VFS_Open(argv[1], (VFS_OpenFlags) (VFS_O_WRONLY | VFS_O_CREAT), &fd);
	if (st != VFS_OK) {
		printf("Error: %s\n", vfs_strerror(st));
		return 0;
	}

	VFS_Close(fd);
	printf("Touched: %s\n", argv[1]);
	return 0;
}

int drive_write_cmd(int argc, char** argv) {
	if (argc < 3) {
		printf("Usage: drive write [-a] <vfs_path> <text...>\n");
		printf("Example: drive write /note.txt Hello world\n");
		printf("Example: drive write -a /note.txt More text\n");
		return 0;
	}

	bool append = false;
	int path_idx = 1;

	if (strcmp(argv[1], "-a") == 0) {
		if (argc < 4) {
			printf("Error: not enough arguments for append mode.\n");
			return 0;
		}
		append = true;
		path_idx = 2;
	}

	const char* path = argv[path_idx];
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
	if (argc < 2) {
		printf("Usage: drive cat <vfs_path>\n");
		printf("Example: drive cat /readme.txt\n");
		return 0;
	}

	VFS_FD fd;
	VFS_Status st = VFS_Open(argv[1], VFS_O_RDONLY, &fd);
	if (st != VFS_OK) {
		printf("Error opening '%s': %s\n", argv[1], vfs_strerror(st));
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

int drive_command(int argc, char** argv) {
	if (argc < 2 || strcmp(argv[1], "help") == 0) {
		printf("Usage: drive <command> [args]\n");
		printf("Commands:\n");
		printf("  info                               List WDM drives and mounted volumes\n");
		printf("  test                               Run a drive self-test (stub)\n");
		printf("  mount  <path> <wdm_idx> [pdrv]    Mount a volume at a VFS path\n");
		printf("  unmount <path>                     Unmount a VFS path\n");
		printf("  ls     <path> [flags]              List directory (-a -s -t -l)\n");
		printf("  tree   <path>                      Print directory tree\n");
		printf("  cat    <path>                      Print file contents\n");
		printf("  mkdir  <path>                      Create a directory\n");
		printf("  touch  <path>                      Create or touch a file\n");
		printf("  write  [-a] <path> <text>          Write (or append) text to a file\n");
		printf("\nPaths are absolute VFS paths, e.g. / or /mnt/usb/docs/file.txt\n");
		return 0;
	}

	int    sub_argc = argc - 1;
	char** sub_argv = &argv[1];
	const char* sub = sub_argv[0];

	if (strcmp(sub, "info") == 0) return drive_info_cmd(sub_argc, sub_argv);
	if (strcmp(sub, "test") == 0) return drive_test_cmd(sub_argc, sub_argv);
	else if (strcmp(sub, "mount") == 0) return drive_mount_cmd(sub_argc, sub_argv);
	else if (strcmp(sub, "unmount") == 0) return drive_unmount_cmd(sub_argc, sub_argv);
	else if (strcmp(sub, "ls") == 0) return drive_ls_cmd(sub_argc, sub_argv);
	else if (strcmp(sub, "tree") == 0) return drive_tree_cmd(sub_argc, sub_argv);
	else if (strcmp(sub, "cat") == 0) return drive_cat_cmd(sub_argc, sub_argv);
	else if (strcmp(sub, "mkdir") == 0) return drive_mkdir_cmd(sub_argc, sub_argv);
	else if (strcmp(sub, "touch") == 0) return drive_touch_cmd(sub_argc, sub_argv);
	else if (strcmp(sub, "write") == 0) return drive_write_cmd(sub_argc, sub_argv);

	printf("Unrecognized command: %s\n", sub);
	printf("Run 'drive help' for usage.\n");
	return 0;
}

int drive_command_help(int argc, char** argv) {
	(void) argc; (void) argv;
	char* help_argv[] = { (char*) "drive", (char*) "help" };
	return drive_command(2, help_argv);
}