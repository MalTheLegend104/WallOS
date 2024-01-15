#ifndef SYSTEMCOMMANDS_H
#define SYSTEMCOMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif

	void registerSystemCommands();

	int time_command(int argc, char** argv);
	int time_help(int argc, char** argv);

	int meminfo(int argc, char** argv);
	int meminfo_help(int argc, char** argv);

#ifdef __cplusplus
}
#endif

#endif // SYSTEMCOMMANDS_H