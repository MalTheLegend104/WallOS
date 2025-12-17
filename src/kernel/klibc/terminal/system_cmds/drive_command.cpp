#include <drivers/sata/pio.h>
#include <drivers/serial.h>
#include <terminal/commands/system_commands.h>
#include <string.h>
#include <stdio.h>
#include <ff.h>

#include <system/ktime.h>
#include <klibc/kprint.h>

// Color Definitions
#define COLOR_DIR      VGA_COLOR_LIGHT_CYAN
#define COLOR_FILE     VGA_COLOR_LIGHT_GREEN
#define COLOR_METADATA VGA_COLOR_LIGHT_GREY
#define COLOR_ERROR    VGA_COLOR_RED
#define COLOR_DEFAULT  VGA_DEFAULT_FG
#define COLOR_BRANCH   VGA_COLOR_DARK_GREY
#define COLOR_SIZE     VGA_COLOR_LIGHT_GREY

// FatFs Global Objects
FATFS fs_objects[FF_VOLUMES]; // Array of FATFS structures for each drive (0-3)
bool drive_mounted[FF_VOLUMES] = { false };

// Simple file size formatting helper (using itoa/printf)
static void print_file_size(FSIZE_t size) {
	if (size < 1024) {
		printf("%u B", (unsigned int) size);
	} else if (size < (1024 * 1024)) {
		printf("%u KB", (unsigned int) (size / 1024));
	} else if (size < (1024 * 1024 * 1024)) {
		printf("%u MB", (unsigned int) (size / (1024 * 1024)));
	} else {
		printf("%u GB", (unsigned int) (size / (1024 * 1024 * 1024)));
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Drive ls Command Implementation
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Output Flags (Bitmask)
#define LS_FLAG_NONE    0x00
#define LS_FLAG_ATTRIB  0x01 // Enable attributes ([D], [-])
#define LS_FLAG_SIZE    0x02 // Enable file size (<DIR>, 10KB)
#define LS_FLAG_TIME    0x04 // Enable modification date/time
#define LS_FLAG_DEFAULT (LS_FLAG_SIZE | LS_FLAG_ATTRIB) // Default behavior

static uint8_t parse_ls_flags_string(const char* flag_str) {
	uint8_t flags = LS_FLAG_NONE;

	// Start at index 1 to skip the leading '-'
	for (int i = 1; flag_str[i] != '\0'; i++) {
		switch (flag_str[i]) {
			case 'a': // Attributes
				flags |= LS_FLAG_ATTRIB;
				break;
			case 's': // Size
				flags |= LS_FLAG_SIZE;
				break;
			case 't': // Time
				flags |= LS_FLAG_TIME;
				break;
			case 'l': // Long format (same as -ast)
				flags |= (LS_FLAG_ATTRIB | LS_FLAG_SIZE | LS_FLAG_TIME);
				break;
			default:
				// Optionally print an error for unknown flag
				printf("Warning: Unknown flag '-%c' ignored.\n", flag_str[i]);
				break;
		}
	}
	return flags;
}

int drive_ls_cmd(int argc, char** argv) {
	uint8_t flags = LS_FLAG_NONE;
	const char* path = NULL;
	bool path_set = false;

	// Check for minimum arguments
	if (argc < 2) {
		printf("Usage: drive ls <path> [flags]\n");
		printf("Example: drive ls 0:/ -lst\n");
		printf("Example: drive ls -t 0:/ -a\n");
		return 0;
	}

	// Default to the basic flags if no flags are explicitly provided
	flags = LS_FLAG_DEFAULT;

	// Parse all arguments (starting from argv[1], which is the first argument after "ls")
	for (int i = 1; i < argc; i++) {
		const char* arg = argv[i];

		if (arg[0] == '-') {
			// Found a flag string (e.g., "-lst" or "-a")
			flags |= parse_ls_flags_string(arg);
		} else if (!path_set) {
			// Found the path argument
			path = arg;
			path_set = true;
		} else {
			// Found a second non-flag argument
			printf("Warning: Too many non-flag arguments provided. Ignoring '%s'.\n", arg);
		}
	}

	// Final validation
	if (!path_set) {
		printf("Error: No directory path provided.\n");
		return 0;
	}

	// Rest of the execution logic (same as before)

	FRESULT res;
	DIR dir;
	FILINFO fno;

	set_colors(VGA_COLOR_WHITE, VGA_DEFAULT_BG);
	printf("Listing directory: %s\n", path);
	printf("----------------------------------------\n");
	set_to_last();

	res = f_opendir(&dir, path);
	if (res != FR_OK) {
		set_colors(COLOR_ERROR, VGA_DEFAULT_BG);
		printf("Error opening directory. FatFs code: %d\n", res);
		set_to_last();
		return 0;
	}

	while (1) {
		res = f_readdir(&dir, &fno);

		if (res != FR_OK || fno.fname[0] == 0) break;
		if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0) continue;

		vga_color item_color = (fno.fattrib & AM_DIR) ? COLOR_DIR : COLOR_FILE;

		// Print Attribute
		if (flags & LS_FLAG_ATTRIB) {
			set_colors(COLOR_METADATA, VGA_DEFAULT_BG);
			printf("%s", (fno.fattrib & AM_DIR) ? "[D] " : "[-] ");
			set_to_last();
		}

		// Print Size
		if (flags & LS_FLAG_SIZE) {
			set_colors(COLOR_METADATA, VGA_DEFAULT_BG);
			if (!(fno.fattrib & AM_DIR)) {
				print_file_size(fno.fsize);
				printf("\t");
			} else {
				printf("<DIR>\t");
			}
			set_to_last();
		}

		// Print Time
		if (flags & LS_FLAG_TIME) {
			set_colors(COLOR_METADATA, VGA_DEFAULT_BG);
			print_fattime(fno.fdate, fno.ftime);
			printf("\t");
			set_to_last();
		}

		// Print Filename
		set_colors(item_color, VGA_DEFAULT_BG);
		printf("%s\n", fno.fname);
		set_to_last();
	}

	f_closedir(&dir);
	printf("----------------------------------------\n");

	return 0;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Drive Tree Command Implementation
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

#define MAX_PATH_LEN 256

#define INDENT_BRANCH  "\xC3\xC4\xC4 "  // ├──  T-junction for non-last items
#define INDENT_LAST    "\xC0\xC4\xC4 "  // └──  Corner for the last item
#define INDENT_CONT    "\xB3   "            // │    Vertical line continuing
#define INDENT_SPACE   "    "               // Blank space for continuation

/**
 * @brief Prints the contents of a directory and its subdirectories recursively.
 * * @param path The current directory path (e.g., "0:/SYSTEM").
 * @param depth The current recursion depth (used for indentation).
 */
static void print_tree_recursive(char* path, const char* indent_prefix) {
	FRESULT res;
	DIR dir;
	FILINFO fno;

	char next_path[MAX_PATH_LEN];
	char current_path_copy[MAX_PATH_LEN];

	// Setup and Open Directory

	strcpy(current_path_copy, path);
	res = f_opendir(&dir, current_path_copy);
	if (res != FR_OK) {
		set_colors(VGA_COLOR_RED, VGA_DEFAULT_BG);
		printf("%s└── READ ERROR (%d)\n", indent_prefix, res);
		set_to_last();
		return;
	}

	// First Pass: Count the total number of items to identify the 'last' one
	int total_entries = 0;
	while (1) {
		res = f_readdir(&dir, &fno);
		if (res != FR_OK || fno.fname[0] == 0) break;
		if (strcmp(fno.fname, ".") != 0 && strcmp(fno.fname, "..") != 0) {
			total_entries++;
		}
	}

	// Rewind directory pointer to the start
	f_rewinddir(&dir);

	// Second Pass: Print and Recurse

	int entry_count = 0;

	while (1) {
		res = f_readdir(&dir, &fno);

		if (res != FR_OK || fno.fname[0] == 0) break;

		// Skip current ('.') and parent ('..') entries
		if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0) continue;

		entry_count++;

		// Determine if this is the last entry in the current directory
		bool is_last = (entry_count == total_entries);

		// Set the appropriate branch string (├── or └──)
		const char* branch_prefix = is_last ? INDENT_LAST : INDENT_BRANCH;

		// Determine the prefix for the next recursive call's indentation
		// If it's the last item, the continuation line is just space.
		// Otherwise, it's the vertical line.
		const char* next_indent_segment = is_last ? INDENT_SPACE : INDENT_CONT;

		// Build the new indentation prefix for the next level
		char next_indent_prefix[MAX_PATH_LEN];
		strcpy(next_indent_prefix, indent_prefix);
		strcat(next_indent_prefix, next_indent_segment);

		// Print Current Entry 
		set_colors(COLOR_BRANCH, VGA_DEFAULT_BG);
		printf("%s%s", indent_prefix, branch_prefix);
		set_to_last();

		// Path Construction for Next Level (same logic as before)
		strcpy(next_path, path);
		if (path[strlen(path) - 1] != ':' && path[strlen(path) - 1] != '/') {
			strcat(next_path, "/");
		}
		strcat(next_path, fno.fname);

		// Output and Recurse
		if (fno.fattrib & AM_DIR) {
			// Directory: Print the name, then recurse
			set_colors(COLOR_DIR, VGA_DEFAULT_BG);
			printf("%s\n", fno.fname);
			set_to_last();
			print_tree_recursive(next_path, next_indent_prefix);
		} else {
			// File: Print name and size
			set_colors(COLOR_FILE, VGA_DEFAULT_BG);
			printf("%s", fno.fname);
			set_to_last();

			// Print size metadata in a separate color
			set_colors(COLOR_SIZE, VGA_DEFAULT_BG);
			printf(" (");
			print_file_size(fno.fsize);
			printf(")\n");
			set_to_last();
		}
	}

	f_closedir(&dir);
}

int drive_tree_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: drive tree <path>\n");
		printf("Example: drive tree 1:/ \n");
		return 0;
	}

	char mutable_path[MAX_PATH_LEN];
	strncpy(mutable_path, argv[1], MAX_PATH_LEN - 1);
	mutable_path[MAX_PATH_LEN - 1] = '\0';

	printf("Starting file tree for %s\n", mutable_path);
	printf("========================================\n");

	// Print the root node explicitly
	set_colors(COLOR_BRANCH, VGA_DEFAULT_BG);
	printf("[ROOT] %s\n", mutable_path);
	set_to_last();

	// Initial call: Start recursion from the root path with an empty indent prefix.
	// We pass the root path again, and the root itself will handle the rest.
	print_tree_recursive(mutable_path, "");

	printf("========================================\n");
	return 0;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Drive Mount Implementation
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

void construct_drive_path(int pdrv, char* path_buffer) {
	// FatFs paths require the format "D:" where D is the drive number (0-3).
	// Example: Drive 0 -> "0:"
	path_buffer[0] = (char) (pdrv + '0');
	path_buffer[1] = ':';
	path_buffer[2] = '\0';
}

int drive_mount_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: drive mount <drive_number>\n");
		return 0;
	}

	int pdrv = argv[1][0] - '0';
	if (pdrv < 0 || pdrv >= FF_VOLUMES) {
		printf("Invalid drive number (0-%d).\n", FF_VOLUMES - 1);
		return 0;
	}

	if (drive_mounted[pdrv]) {
		printf("Drive %d is already mounted.\n", pdrv);
		return 0;
	}

	// --- REPLACEMENT FOR SPRINTF ---
	char path[3]; // Needs 3 bytes: 'D', ':', '\0'
	construct_drive_path(pdrv, path);
	// --------------------------------

	printf("Attempting to mount Drive %d...\n", pdrv);

	FRESULT res = f_mount(&fs_objects[pdrv], path, 1);

	if (res == FR_OK) {
		drive_mounted[pdrv] = true;
		printf("Drive %d mounted successfully.\n", pdrv);
	} else {
		printf("Failed to mount Drive %d. FatFs error: %d\n", pdrv, res);
		printf("(Check if 'drive info %d' shows it's present.)\n", pdrv);
	}

	return 0;
}

// -------------------------------------------------------------------

int drive_unmount_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: drive unmount <drive_number>\n");
		return 0;
	}

	int pdrv = argv[1][0] - '0';
	if (pdrv < 0 || pdrv >= FF_VOLUMES) {
		printf("Invalid drive number (0-%d).\n", FF_VOLUMES - 1);
		return 0;
	}

	if (!drive_mounted[pdrv]) {
		printf("Drive %d is not currently mounted.\n", pdrv);
		return 0;
	}

	// --- REPLACEMENT FOR SPRINTF ---
	char path[3];
	construct_drive_path(pdrv, path);
	// --------------------------------

	// Unmount by passing NULL for the FATFS object
	FRESULT res = f_mount(NULL, path, 0);

	if (res == FR_OK) {
		drive_mounted[pdrv] = false;
		printf("Drive %d unmounted successfully.\n", pdrv);
	} else {
		printf("Failed to unmount Drive %d. FatFs error: %d\n", pdrv, res);
	}

	return 0;
}


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Drive Cat Command Implementation
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
#define CAT_BUFFER_SIZE 512

int drive_cat_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: drive cat <path>\n");
		printf("Example: drive cat 0:README.TXT\n");
		return 0;
	}

	FIL fil;             // File object
	FRESULT res;         // FatFs result code
	char read_buffer[CAT_BUFFER_SIZE];
	UINT bytes_read;

	// The path should look like "0:FILENAME.TXT"
	const char* path = argv[1];

	printf("Reading file: %s\n", path);

	// 1. Open the file for reading
	res = f_open(&fil, path, FA_READ);
	if (res != FR_OK) {
		printf("Error opening file. FatFs code: %d\n", res);
		if (res == FR_NO_FILE) printf("   Error: File not found.\n");
		else if (res == FR_NOT_READY) printf("   Error: Drive not mounted or ready.\n");
		return 0;
	}
	printf("\n--- Start of file ---\n");

	// 2. Read and display the content loop
	do {
		// Read a chunk of data into the buffer
		res = f_read(&fil, read_buffer, CAT_BUFFER_SIZE - 1, &bytes_read);

		if (res != FR_OK) {
			printf("\nRead error: %d\n", res);
			break;
		}

		if (bytes_read > 0) {
			// Null-terminate the buffer slice for safe printing
			read_buffer[bytes_read] = '\0';
			printf("%s", read_buffer);
		}

	} while (bytes_read == CAT_BUFFER_SIZE - 1); // Continue while we read a full buffer

	// 3. Close the file
	f_close(&fil);

	printf("\n---  End of file  ---\n");
	return 0;
}


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Main Drive Command Implementation
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
int drive_command(int argc, char** argv) {
	if (argc < 2 || (argc > 1 && strcmp(argv[1], "help") == 0)) {
		printf("I'm too lazy to add the help menu to this right now.\n");
		printf("Usage: drive <info|test|mount|unmount|cat>\n");
		return 0;
	}

	// Shift arguments for sub-commands (e.g., drive mount 0 -> mount 0)
	int sub_argc = argc - 1;
	char** sub_argv = &argv[1];

	// General Drive Commands
	if (strcmp(sub_argv[0], "info") == 0) {
		return get_drive_info(sub_argc, sub_argv);
	} else if (strcmp(sub_argv[0], "test") == 0) {
		return sata_test_cmd(sub_argc, sub_argv);
	}

	// FatFs Mount/Unmount Commands
	else if (strcmp(sub_argv[0], "mount") == 0) {
		return drive_mount_cmd(sub_argc, sub_argv);
	} else if (strcmp(sub_argv[0], "unmount") == 0) {
		return drive_unmount_cmd(sub_argc, sub_argv);
	}

	// File Commands
	else if (strcmp(sub_argv[0], "cat") == 0) {
		return drive_cat_cmd(sub_argc, sub_argv);
	}

	else if (strcmp(sub_argv[0], "ls") == 0) {
		return drive_ls_cmd(sub_argc, sub_argv);
	}

	else if (strcmp(sub_argv[0], "tree") == 0) {
		return drive_tree_cmd(sub_argc, sub_argv);
	}

	// Error for unrecognized sub-command
	else {
		printf("Unrecognized drive command: %s\n", sub_argv[0]);
		printf("Usage: drive <info|test|mount|unmount|cat>\n");
		return 0;
	}
}

//TODO: Proper help implementation
// This is probably the most important command besides time.
int drive_command_help(int argc, char** argv) {
	return drive_command(1, argv);
}