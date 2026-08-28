#include <terminal/wall_shell.h>
#include <klibc/display.h>

static display_color_t ws_fg_to_display_color(ws_fg_color_t color) {
	switch (color) {
		case WS_FG_DEFAULT:        return DISPLAY_DEFAULT_FG;

		case WS_FG_BLACK:          return DISPLAY_COLOR_BLACK;
		case WS_FG_RED:            return DISPLAY_COLOR_RED;
		case WS_FG_GREEN:          return DISPLAY_COLOR_GREEN;
		case WS_FG_YELLOW:         return DISPLAY_COLOR_BROWN;
		case WS_FG_BLUE:           return DISPLAY_COLOR_BLUE;
		case WS_FG_MAGENTA:        return DISPLAY_COLOR_PURPLE;
		case WS_FG_CYAN:           return DISPLAY_COLOR_CYAN;
		case WS_FG_WHITE:           return DISPLAY_COLOR_LIGHT_GREY;

		case WS_FG_BRIGHT_BLACK:   return DISPLAY_COLOR_DARK_GREY;
		case WS_FG_BRIGHT_RED:     return DISPLAY_COLOR_LIGHT_RED;
		case WS_FG_BRIGHT_GREEN:   return DISPLAY_COLOR_LIGHT_GREEN;
		case WS_FG_BRIGHT_YELLOW:  return DISPLAY_COLOR_YELLOW;
		case WS_FG_BRIGHT_BLUE:    return DISPLAY_COLOR_LIGHT_BLUE;
		case WS_FG_BRIGHT_MAGENTA: return DISPLAY_COLOR_PINK;
		case WS_FG_BRIGHT_CYAN:    return DISPLAY_COLOR_LIGHT_CYAN;
		case WS_FG_BRIGHT_WHITE:   return DISPLAY_COLOR_WHITE;

		default: return DISPLAY_DEFAULT_FG;
	}
}

static display_color_t ws_bg_to_display_color(ws_bg_color_t color) {
	switch (color) {
		case WS_BG_DEFAULT:        return DISPLAY_DEFAULT_BG;

		case WS_BG_BLACK:          return DISPLAY_COLOR_BLACK;
		case WS_BG_RED:            return DISPLAY_COLOR_RED;
		case WS_BG_GREEN:          return DISPLAY_COLOR_GREEN;
		case WS_BG_YELLOW:         return DISPLAY_COLOR_BROWN;
		case WS_BG_BLUE:           return DISPLAY_COLOR_BLUE;
		case WS_BG_MAGENTA:        return DISPLAY_COLOR_PURPLE;
		case WS_BG_CYAN:           return DISPLAY_COLOR_CYAN;
		case WS_BG_WHITE:           return DISPLAY_COLOR_LIGHT_GREY;

		case WS_BG_BRIGHT_BLACK:   return DISPLAY_COLOR_DARK_GREY;
		case WS_BG_BRIGHT_RED:     return DISPLAY_COLOR_LIGHT_RED;
		case WS_BG_BRIGHT_GREEN:   return DISPLAY_COLOR_LIGHT_GREEN;
		case WS_BG_BRIGHT_YELLOW:  return DISPLAY_COLOR_YELLOW;
		case WS_BG_BRIGHT_BLUE:    return DISPLAY_COLOR_LIGHT_BLUE;
		case WS_BG_BRIGHT_MAGENTA: return DISPLAY_COLOR_PINK;
		case WS_BG_BRIGHT_CYAN:    return DISPLAY_COLOR_LIGHT_CYAN;
		case WS_BG_BRIGHT_WHITE:   return DISPLAY_COLOR_WHITE;

		default: return DISPLAY_DEFAULT_BG;
	}
}

void display_set_colors_wrapper(int fg, int bg) {
	display_color_t new_fg = ws_fg_to_display_color((ws_fg_color_t) fg);
	display_color_t new_bg = ws_bg_to_display_color((ws_bg_color_t) bg);

	display_set_colors(new_fg, new_bg);
}

ws_error_t setConsoleMode() { return WS_NO_ERROR; }

void ws_moveCursor(ws_cursor_t direction) {
	/* This is expected to move the cursor once in the provided direction. */
	int x = 0, y = 0;

	display_get_cursor(&x, &y);

	switch (direction) {
		case WS_CURSOR_LEFT:
			if (x > 0) x--;
			break;

		case WS_CURSOR_RIGHT:
			x++;
			break;

		case WS_CURSOR_UP:
			if (y > 0) y--;
			break;

		case WS_CURSOR_DOWN:
			y++;
			break;

		default:
			return;
	}

	display_update_cursor(x, y);
}

void ws_moveCursor_n(ws_cursor_t direction, size_t num) {
	/* This is expected to move the cursor num times in the provided direction. */
	if (num == 0) return;

	int x = 0, y = 0;

	display_get_cursor(&x, &y);

	switch (direction) {
		case WS_CURSOR_LEFT:
			x -= (int) num;
			if (x < 0)
				x = 0;
			break;

		case WS_CURSOR_RIGHT:
			x += (int) num;
			break;

		case WS_CURSOR_UP:
			y -= (int) num;
			if (y < 0)
				y = 0;
			break;

		case WS_CURSOR_DOWN:
			y += (int) num;
			break;

		default:
			return;
	}

	display_update_cursor(x, y);
}