/**
 * @file wallshell_config.h
 * @author MalTheLegend104
 * @brief Freestanding Example
 *
 * This file is an example of how a freestanding environment may set up its config.
 * Unlike the normal wallshell_config.h, this file *does* have a license, since it's an example.
 *
 * @version v1.0.0
 * @copyright
 * Copyright 2024 MalTheLegend104
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef WS_CONFIG_H
#define WS_CONFIG_H

#include <input/input_handler.h>
#include <input/input_text.h>
#include <klibc/display.h>
#include <stdio.h>

#define NO_WS_LOGGING // no need for this in the OS
#define NO_LOGGING
#define NO_CLEAR_COMMAND

/* Console Setup */
#define CUSTOM_WS_SETUP

// We don't support different terminal output types. Strict Code Page 437 :)
#define SET_TERMINAL_LOCALE

#define ws_internal_get_char_blocking(stream) input_getc()
#define ws_internal_get_char_nonblocking(stream) input_getc_nonblocking()

#define CLEAR_ROW display_clear_row()

/* Console Colors */
#define CUSTOM_WS_COLORS

#ifdef CUSTOM_WS_COLORS
/* Custom colors are fairly simple to do.
 * You only need two defines, but you will likely have to implement your own functions.
 * Currently WallShell doesn't allow you to include the enums relating to colors in this file.
 * The linker should hopefully resolve the function, but you may need to declare it here using ints instead.
 * For the purpose of this example, we'll just define the function "set_color()" and pretend it
 * sets the consoles colors to those provided.
 */
void display_set_colors_wrapper(int fg, int bg);
#define SET_WS_COLORS(a, b) display_set_colors_wrapper(a, b)

#define RESET_CONSOLE display_set_colors_default()
#endif 

/* We dont have "regular" cursor control. */
#define CUSTOM_CURSOR_CONTROL
#define DISABLE_MALLOC

#endif // WS_CONFIG_H