#ifndef PRINT_TYPES_H
#define PRINT_TYPES_H

#define PRINT_COLOR_BLACK        0
#define PRINT_COLOR_BLUE         1
#define PRINT_COLOR_GREEN        2
#define PRINT_COLOR_CYAN         3
#define PRINT_COLOR_RED          4
#define PRINT_COLOR_PURPLE       5
#define PRINT_COLOR_BROWN        6
#define PRINT_COLOR_LIGHT_GREY   7
#define PRINT_COLOR_DARK_GREY    8
#define PRINT_COLOR_LIGHT_BLUE   9
#define PRINT_COLOR_LIGHT_GREEN  10
#define PRINT_COLOR_LIGHT_CYAN   11
#define PRINT_COLOR_LIGHT_RED    12
#define PRINT_COLOR_PINK         13
#define PRINT_COLOR_YELLOW       14
#define PRINT_COLOR_WHITE        15
#define PRINT_DEFAULT_FG         7
#define PRINT_DEFAULT_BG         0

#if __cplusplus
extern "C" {
#endif

	extern void display_set_colors(int fg, int bg);
	extern void display_set_colors_default(void);

	extern void display_putc(const unsigned char c);
	extern int printf_color(int fg, int bg, const char* fmt, ...);
	extern int vprintf_color(int fg, int bg, const char* fmt, va_list arg);

#if __cplusplus
}
#endif

#endif