#include <drivers/sata/pio.h>
#include <terminal/commands/system_commands.h>
#include <string.h>
#include <stdio.h>
#include <ff.h>

// --- FatFs Global Objects ---
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

int drive_ls_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: drive ls <path>\n");
		printf("Example: drive ls 0:/ \n");
		return 0;
	}

	FRESULT res;
	DIR dir;         // Directory object
	FILINFO fno;     // File Information structure
	const char* path = argv[1];

	printf("Listing directory: %s\n", path);
	printf("----------------------------------------\n");

	// 1. Open the directory
	res = f_opendir(&dir, path);
	if (res != FR_OK) {
		printf("Error opening directory. FatFs code: %d\n", res);
		return 0;
	}

	// 2. Read directory contents loop
	while (1) {
		res = f_readdir(&dir, &fno);

		// Check for error or end of directory (null filename)
		if (res != FR_OK || fno.fname[0] == 0) break;

		// Skip the current ('.') and parent ('..') entries
		if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0) continue;

		// --- Print Entry Info ---

		// Print attributes
		printf("%s", (fno.fattrib & AM_DIR) ? "[D] " : "[-] ");

		// Print size
		if (!(fno.fattrib & AM_DIR)) {
			print_file_size(fno.fsize);
			printf("\t\t");
		} else {
			printf("<DIR>\t\t");
		}

		// Print time (Optional but recommended)
		// If you want to print time, use fno.ftime and fno.fdate here.

		// Print filename
		printf("%s\n", fno.fname);
	}

	// 3. Close the directory
	f_closedir(&dir);
	printf("----------------------------------------\n");

	return 0;
}

#define MAX_PATH_LEN 256

/**
 * @brief Prints the contents of a directory and its subdirectories recursively.
 * * @param path The current directory path (e.g., "0:/SYSTEM").
 * @param depth The current recursion depth (used for indentation).
 */
static void print_tree_recursive(char* path, int depth) {
	FRESULT res;
	DIR dir;
	FILINFO fno;

	// Buffer to hold the path for the next recursive call
	char next_path[MAX_PATH_LEN];

	// --- Print current directory path representation ---
	for (int i = 0; i < depth; i++) {
		printf("|   ");
	}
	// We print the path only for the directories we are entering
	if (depth > 0) {
		// Find the last segment of the path to print (e.g., just "SYSTEM" not "0:/SYSTEM")
		char* name_start = strrchr(path, '/');
		if (name_start == NULL) name_start = path; // Handle root
		else name_start++; // Skip the last slash

		printf("|-- [DIR] %s\n", name_start);
	} else {
		printf("|-- [ROOT] %s\n", path);
	}


	// 1. Open the current directory
	res = f_opendir(&dir, path);
	if (res != FR_OK) {
		for (int i = 0; i < depth + 1; i++) printf("|   ");
		printf("|-- READ ERROR (%d)\n", res);
		return;
	}

	// 2. Read directory contents loop
	while (1) {
		res = f_readdir(&dir, &fno);

		if (res != FR_OK || fno.fname[0] == 0) break;

		// Skip current ('.') and parent ('..') entries
		if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0) continue;

		// --- Recursive Call or Print File ---

		// Construct the full path for the next level: "current_path/new_entry"
		strcpy(next_path, path);

		// Check if a separator is needed (only needed if path isn't just "0:")
		if (path[strlen(path) - 1] != '/') {
			strcat(next_path, "/");
		}
		strcat(next_path, fno.fname);

		// Boundary check (optional, but good practice)
		if (strlen(next_path) >= MAX_PATH_LEN) {
			printf("\nPath too long: %s\n", next_path);
			continue;
		}

		if (fno.fattrib & AM_DIR) {
			// Recursive call for subdirectory
			print_tree_recursive(next_path, depth + 1);
		} else {
			// Print file entry (at this depth)
			for (int i = 0; i < depth + 1; i++) {
				printf("|   ");
			}
			printf("|-- %s\n", fno.fname);
		}

	} // End while

	f_closedir(&dir);
}

int drive_tree_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf("Usage: drive tree <path>\n");
		printf("Example: drive tree 0:/ \n");
		return 0;
	}

	// Copy the path argument to a mutable buffer since the recursive function needs a modifiable string (char*) for path manipulation.
	char mutable_path[MAX_PATH_LEN];
	strncpy(mutable_path, argv[1], MAX_PATH_LEN - 1);
	mutable_path[MAX_PATH_LEN - 1] = '\0';

	printf("Starting file tree for %s\n", mutable_path);
	printf("========================================\n");

	// The initial call starts at depth 0
	print_tree_recursive(mutable_path, 0);

	printf("========================================\n");
	return 0;
}

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

	printf("\n--- End of file ---\n");
	return 0;
}

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