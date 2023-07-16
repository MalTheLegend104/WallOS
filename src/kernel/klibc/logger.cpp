#include "klibc/logger.h"
#include "klibc/kprint.h"
#include <stdio.h>

void set_green() {
	set_colors(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
}

void set_red() {
	set_colors(VGA_COLOR_RED, VGA_COLOR_BLACK);
}

void printTime() {
	// TODO print good stuff here 
}

void set_default() {
	set_to_last();
}

void print(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}

void  Logger::vlogf(const char* format, va_list args) {
	set_colors(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
	printTime();
	printf("[LOG]   ");
	vprintf(format, args);
	set_default();
}
void  Logger::vinfof(const char* format, va_list args) {
	set_colors(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
	printTime();
	printf("[INFO]  ");
	vprintf(format, args);
	set_default();
}
void  Logger::vwarnf(const char* format, va_list args) {
	set_colors(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
	printTime();
	printf("[WARN]  ");
	vprintf(format, args);
	set_default();
}
void  Logger::verrorf(const char* format, va_list args) {
	set_colors(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
	printTime();
	printf("[ERROR] ");
	vprintf(format, args);
	set_default();
}
void  Logger::vfatalf(const char* format, va_list args) {
	set_colors(VGA_COLOR_RED, VGA_COLOR_BLACK);
	printTime();
	printf("[FATAL] ");
	vprintf(format, args);
	set_default();
}

void Logger::logf(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vlogf(format, args);
	va_end(args);
}
void  Logger::infof(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vinfof(format, args);
	va_end(args);
}
void  Logger::warnf(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vwarnf(format, args);
	va_end(args);
}
void  Logger::errorf(const char* format, ...) {
	va_list args;
	va_start(args, format);
	verrorf(format, args);
	va_end(args);
}
void  Logger::fatalf(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfatalf(format, args);
	va_end(args);
}

void Logger::Checklist::blankEntry(const char* format, ...) {
	print("[ ] ");
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	print("\n");
}
void Logger::Checklist::checkEntry(const char* format, ...) {
	print("[");
	set_green();
	putc_vga(0xfb);
	set_default();
	print("] ");
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	print("\n");
}
void Logger::Checklist::noCheckEntry(const char* format, ...) {
	print("[");
	set_red();
	putc_vga('X');
	set_default();
	print("] ");
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	print("\n");
}

void Logger::Checklist::v_blankEntry(const char* format, va_list args) {
	print("[ ] ");
	vprintf(format, args);
	print("\n");
}
void Logger::Checklist::v_checkEntry(const char* format, va_list args) {
	print("[");
	set_green();
	putc_vga(0xfb);
	set_default();
	print("] ");
	vprintf(format, args);
	print("\n");
}
void Logger::Checklist::v_noCheckEntry(const char* format, va_list args) {
	print("[");
	set_red();
	putc_vga('X');
	set_default();
	print("] ");
	vprintf(format, args);
	print("\n");
}

void logger(LogType type, const char* format, ...) {
	va_list args;
	va_start(args, format);
	switch (type) {
		case LOG: 	Logger::vlogf(format, args); 	break;
		case INFO: 	Logger::vinfof(format, args); 	break;
		case WARN: 	Logger::vwarnf(format, args); 	break;
		case ERROR: Logger::verrorf(format, args); 	break;
		case FATAL: Logger::vfatalf(format, args); 	break;

		case CHECKLIST_BLANK: 	Logger::Checklist::v_blankEntry(format, args); 	 break;
		case CHECKLIST_CHECK: 	Logger::Checklist::v_checkEntry(format, args); 	 break;
		case CHECKLIST_NOCHECK: Logger::Checklist::v_noCheckEntry(format, args); break;
		default: vprintf(format, args);	break;
	}
	va_end(args);
}

void vlogger(LogType type, const char* format, va_list args) {
	switch (type) {
		case LOG: 	Logger::vlogf(format, args); 	break;
		case INFO: 	Logger::vinfof(format, args); 	break;
		case WARN: 	Logger::vwarnf(format, args); 	break;
		case ERROR: Logger::verrorf(format, args); 	break;
		case FATAL: Logger::vfatalf(format, args); 	break;

		case CHECKLIST_BLANK: 	Logger::Checklist::v_blankEntry(format, args); 	 break;
		case CHECKLIST_CHECK: 	Logger::Checklist::v_checkEntry(format, args); 	 break;
		case CHECKLIST_NOCHECK: Logger::Checklist::v_noCheckEntry(format, args); break;
		default: vprintf(format, args);	break;
	}
}