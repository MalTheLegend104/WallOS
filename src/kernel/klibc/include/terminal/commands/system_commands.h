#ifndef SYSTEMCOMMANDS_H
#define SYSTEMCOMMANDS_H

#include <stdbool.h>
#include <filesystem/wdm.h>

#ifdef __cplusplus
extern "C" {
#endif

	void registerSystemCommands();

	int time_command(int argc, char** argv);
	int time_help(int argc, char** argv);

	int meminfo(int argc, char** argv);
	int meminfo_help(int argc, char** argv);

	int sysinfo(int argc, char** argv);
	void sysinfo_boot();

	// bool mount_drive(int pdrv);
	// bool mount_drive(int pdrv, WDM_DriveHandle handle);
	bool mount_drive(const char* vfs_path, WDM_DriveHandle handle, int pdrv);
	int drive_command(int argc, char** argv);
	int drive_command_help(int argc, char** argv);

	int shutdown_command(int argc, char** argv);
	int reboot_command(int argc, char** argv);
#ifdef __cplusplus
}
#endif

#endif // SYSTEMCOMMANDS_H