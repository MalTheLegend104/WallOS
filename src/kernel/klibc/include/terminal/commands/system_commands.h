#ifndef SYSTEMCOMMANDS_H
#define SYSTEMCOMMANDS_H

#include <stdbool.h>
#include <stddef.h>
#include <filesystem/wdm.h>
#include <terminal/wall_shell.h>

#ifdef __cplusplus
extern "C" {
#endif

	void registerSystemCommands();

	int time_command(int argc, char** argv);
	extern const ws_command_argument_t time_args[];
	extern const size_t time_args_count;

	int meminfo(int argc, char** argv);
	extern const ws_command_argument_t meminfo_args[];
	extern const size_t meminfo_args_count;

	int sysinfo(void);
	void sysinfo_boot();

	// bool mount_drive(int pdrv);
	// bool mount_drive(int pdrv, WDM_DriveHandle handle);
	bool mount_drive(const char* vfs_path, WDM_DriveHandle handle, int pdrv);
	int drive_command(int argc, char** argv);
	int drive_command_help(int argc, char** argv);

	int shutdown_command(int argc, char** argv);
	int reboot_command(int argc, char** argv);
	extern const ws_command_argument_t power_state_args[];
	extern const size_t power_state_args_count;
#ifdef __cplusplus
}
#endif

#endif // SYSTEMCOMMANDS_H