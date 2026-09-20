/* Kilo -- A very simple editor in less than 1-kilo lines of code (as counted
 *         by "cloc"). Does not depend on libcurses, directly emits VT100
 *         escapes on the terminal.
 *
 * -----------------------------------------------------------------------
 *
 * Copyright (C) 2016 Salvatore Sanfilippo <antirez at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**************************************************************************************************
 * This file is a port of the Kilo editor to run as a debug command (editor) inside the WallOS kernel terminal.
 * This file, including changes, still falls under the BSD-2 terms above.
 * Most changes relate to changing POSIX and VT100 interfaces to support the kernel API.
 * Due to my formatter, a lot of lines have changed whitespace.
 * I have also added a few simple languages to the syntax highlighting. Most are self explanitory.
 * I plan on eventually using lua as a scripting language.
 *************************************************************************************************/

#define KILO_VERSION "0.0.1"

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <filesystem/vfs.h>
#include <input/input_handler.h>
#include <input/input_text.h>
#include <klibc/display.h>
#include <memory/kernel_alloc.h>
#include <system/timer.h>
#include <terminal/wall_shell.h>

/* Syntax highlight types */
#define HL_NORMAL    0
#define HL_NONPRINT  1
#define HL_COMMENT   2   /* Single line comment. */
#define HL_MLCOMMENT 3   /* Multi-line comment. */
#define HL_KEYWORD1  4
#define HL_KEYWORD2  5
#define HL_STRING    6
#define HL_NUMBER    7
#define HL_MATCH     8   /* Search match. */

#define HL_HIGHLIGHT_STRINGS (1 << 0)
#define HL_HIGHLIGHT_NUMBERS (1 << 1)

struct editorSyntax {
	char** filematch;
	char** keywords;
	char singleline_comment_start[2];
	char multiline_comment_start[3];
	char multiline_comment_end[3];
	int flags;
};

/* This structure represents a single line of the file we are editing. */
typedef struct erow {
	int idx;            /* Row index in the file, zero-based. */
	int size;           /* Size of the row, excluding the null term. */
	int rsize;          /* Size of the rendered row. */
	char* chars;        /* Row content. */
	char* render;       /* Row content "rendered" for screen (for TABs). */
	unsigned char* hl;  /* Syntax highlight type for each character in render.*/
	int hl_oc;          /* Row had open comment at end in last syntax highlight check. */
} erow;

typedef struct hlcolor {
	int r, g, b;
} hlcolor;

struct editorConfig {
	int cx, cy;  /* Cursor x and y position in characters */
	int rowoff;     /* Offset of row displayed. */
	int coloff;     /* Offset of column displayed. */
	int screenrows; /* Number of rows that we can show */
	int screencols; /* Number of cols that we can show */
	int numrows;    /* Number of rows */
	erow* row;      /* Rows */
	int dirty;      /* File modified but not saved. */
	char* filename; /* Currently open filename */
	char statusmsg[80];
	uint64_t statusmsg_time; /* system_uptime_ms() when statusmsg was set. */
	struct editorSyntax* syntax;    /* Current syntax highlight, or NULL. */
	int quit;       /* Set to stop the main loop and return from main(), in place of exit(). */
};

static struct editorConfig E;

enum KEY_ACTION {
	KEY_NULL = 0,       /* NULL */
	CTRL_C = 3,         /* Ctrl-c */
	CTRL_D = 4,         /* Ctrl-d */
	CTRL_F = 6,         /* Ctrl-f */
	CTRL_H = 8,         /* Ctrl-h */
	TAB = 9,            /* Tab */
	CTRL_L = 12,        /* Ctrl+l */
	ENTER = 13,         /* Enter */
	CTRL_Q = 17,        /* Ctrl-q */
	CTRL_S = 19,        /* Ctrl-s */
	CTRL_U = 21,        /* Ctrl-u */
	ESC = 27,           /* Escape */
	BACKSPACE = 127,    /* Backspace */
	/* The following are just soft codes, not really reported by the terminal directly. */
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

void editorSetStatusMessage(const char* fmt, ...);

/* =========================== Syntax highlights DB =========================
 *
 * In order to add a new syntax, define two arrays with a list of file name
 * matches and keywords. The file name matches are used in order to match
 * a given syntax with a given file name: if a match pattern starts with a
 * dot, it is matched as the last past of the filename, for example ".c".
 * Otherwise the pattern is just searched inside the filenme, like "Makefile").
 *
 * The list of keywords to highlight is just a list of words, however if they
 * a trailing '|' character is added at the end, they are highlighted in
 * a different color, so that you can have two different sets of keywords.
 *
 * Finally add a stanza in the HLDB global variable with two two arrays
 * of strings, and a set of flags in order to enable highlighting of
 * comments and numbers.
 *
 * The characters for single and multi line comments must be exactly two
 * and must be provided as well (see the C language example).
 *
 * There is no support to highlight patterns currently. */

/* C / C++ */
char* C_HL_extensions[] = {".c", ".h", ".cpp", ".hpp", ".cc", NULL};
char* C_HL_keywords[] = {
	/* C Keywords */
	"auto",
	"break",
	"case",
	"continue",
	"default",
	"do",
	"else",
	"enum",
	"extern",
	"for",
	"goto",
	"if",
	"register",
	"return",
	"sizeof",
	"static",
	"struct",
	"switch",
	"typedef",
	"union",
	"volatile",
	"while",
	"NULL",

	/* C++ Keywords */
	"alignas",
	"alignof",
	"and",
	"and_eq",
	"asm",
	"bitand",
	"bitor",
	"class",
	"compl",
	"constexpr",
	"const_cast",
	"deltype",
	"delete",
	"dynamic_cast",
	"explicit",
	"export",
	"false",
	"friend",
	"inline",
	"mutable",
	"namespace",
	"new",
	"noexcept",
	"not",
	"not_eq",
	"nullptr",
	"operator",
	"or",
	"or_eq",
	"private",
	"protected",
	"public",
	"reinterpret_cast",
	"static_assert",
	"static_cast",
	"template",
	"this",
	"thread_local",
	"throw",
	"true",
	"try",
	"typeid",
	"typename",
	"virtual",
	"xor",
	"xor_eq",

	/* C types */
	"int|",
	"long|",
	"double|",
	"float|",
	"char|",
	"unsigned|",
	"signed|",
	"void|",
	"short|",
	"auto|",
	"const|",
	"bool|",
	NULL
};

/* Lua */
char* LUA_HL_extensions[] = {".lua", NULL};
char* LUA_HL_keywords[] = {
	"and",
	"break",
	"do",
	"else",
	"elseif",
	"end",
	"false",
	"for",
	"function",
	"goto",
	"if",
	"in",
	"local",
	"nil",
	"not",
	"or",
	"repeat",
	"return",
	"then",
	"true",
	"until",
	"while",

	/* common library / type-ish names, highlighted as second class */
	"string|",
	"table|",
	"math|",
	"io|",
	"os|",
	"coroutine|",
	"self|",
	NULL
};

/* INI
 * INI has no real keywords. Booleans are the closest thing.
 * I kinda wanted sections to be differently colored, but it would've been too much work
 */
// char* INI_HL_extensions[] = {".ini", ".cfg", ".conf", NULL};
// char* INI_HL_keywords[] = {
// 	"true|",
// 	"false|",
// 	"yes|",
// 	"no|",
// 	"on|",
// 	"off|",
// 	NULL
// };

/* Makefile */
char* MAKE_HL_extensions[] = {"Makefile", "makefile", ".mk", NULL};
char* MAKE_HL_keywords[] = {
	"ifeq",
	"ifneq",
	"ifdef",
	"ifndef",
	"else",
	"endif",
	"define",
	"endef",
	"include",
	"export",
	"unexport",
	"override",
	"vpath",

	"PHONY|",
	"CC|",
	"CFLAGS|",
	"LDFLAGS|",
	"OBJS|",
	NULL
};

/* GNU Assembly (AT&T Syntax) */
char* ASM_HL_extensions[] = {".s", ".S", NULL};
char* ASM_HL_keywords[] = {
	".text",
	".data",
	".bss",
	".section",
	".global",
	".globl",
	".extern",
	".align",
	".byte",
	".word",
	".long",
	".quad",
	".asciz",
	".ascii",
	".macro",
	".endm",
	".if",
	".endif",
	".include",

	/* registers, highlighted as second class */
	"eax|",
	"ebx|",
	"ecx|",
	"edx|",
	"esi|",
	"edi|",
	"esp|",
	"ebp|",
	"rax|",
	"rbx|",
	"rcx|",
	"rdx|",
	"rsi|",
	"rdi|",
	"rsp|",
	"rbp|",
	"r8|",
	"r9|",
	"r10|",
	"r11|",
	"r12|",
	"r13|",
	"r14|",
	"r15|",
	NULL
};

/* NASM (Intel Syntax) */
char* NASM_HL_extensions[] = {".nasm", ".asm", NULL};
char* NASM_HL_keywords[] = {
	/* Directives */
	"section",
	"segment",
	"global",
	"extern",
	"bits",
	"org",
	"align",
	"default",
	"struc",
	"endstruc",
	"istruc",
	"iend",
	"at",
	"%include",
	"%define",
	"%undef",
	"%ifdef",
	"%ifndef",
	"%else",
	"%endif",
	"%macro",
	"%endmacro",
	"%rep",
	"%endrep",

	/* Common Instructions */
	"mov",
	"push",
	"pop",
	"lea",
	"call",
	"ret",
	"jmp",
	"je",
	"jne",
	"jz",
	"jnz",
	"jg",
	"jl",
	"jge",
	"jle",
	"cmp",
	"test",
	"add",
	"sub",
	"mul",
	"imul",
	"div",
	"idiv",
	"and",
	"or",
	"xor",
	"not",
	"shl",
	"shr",
	"nop",
	"int",
	"syscall",
	"in",
	"out",
	"cli",
	"sti",
	"hlt",
	"loop",

	/* Data declaration pseudo-instructions, second class */
	"db|",
	"dw|",
	"dd|",
	"dq|",
	"dt|",
	"resb|",
	"resw|",
	"resd|",
	"resq|",
	"equ|",
	"times|",

	/* Registers, second class */
	"eax|",
	"ebx|",
	"ecx|",
	"edx|",
	"esi|",
	"edi|",
	"esp|",
	"ebp|",
	"ax|",
	"bx|",
	"cx|",
	"dx|",
	"al|",
	"bl|",
	"cl|",
	"dl|",
	"rax|",
	"rbx|",
	"rcx|",
	"rdx|",
	"rsi|",
	"rdi|",
	"rsp|",
	"rbp|",
	"r8|",
	"r9|",
	"r10|",
	"r11|",
	"r12|",
	"r13|",
	"r14|",
	"r15|",
	NULL
};

/* Here we define an array of syntax highlights by extensions, keywords,
 * comments delimiters and flags. */
struct editorSyntax HLDB[] = {
	{  /* C / C++ */
	 C_HL_extensions,
	 C_HL_keywords,
	 "//",
	 "/*",
	 "*/",
	 HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS
	},
	{  /* Lua */
	 LUA_HL_extensions,
	 LUA_HL_keywords,
	 "--",
	 "--[[",
	 "]]",
	 HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS
	},
	// {  /* INI */
	//  INI_HL_extensions,
	//  INI_HL_keywords,
	//  ";",
	//  "",
	//  "",
	//  HL_HIGHLIGHT_STRINGS
	// },
	{  /* Makefile */
	 MAKE_HL_extensions,
	 MAKE_HL_keywords,
	 "#",
	 "",
	 "",
	 HL_HIGHLIGHT_STRINGS
	},
	{  /* GNU Assembly */
	 ASM_HL_extensions,
	 ASM_HL_keywords,
	 "#",
	 "/*",
	 "*/",
	 HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS
	},
	{  /* NASM */
	 NASM_HL_extensions,
	 NASM_HL_keywords,
	 ";",
	 "",
	 "",
	 HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS
	}
};
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/**************************************************************************************************
 * WallOS shims & input handling
 *
 * This replaces Kilo's original "Low level terminal handling" section.
 * WallOS is always in "raw input mode" and the terminal size is fixed for the process lifetime,
 * so termios/ioctl/SIGWINCH have no equivalent here and are removed rather than stubbed.
 *************************************************************************************************/

// we take control of the terminal for a long time here
// we need to poll the system loop to get acpi and usb input events
extern void system_poll_loop(void);

int editorReadKey(void) {
	wallos_input_event_t ev;

	for (;;) {
		system_poll_loop();
		busy_wait_ms(1); // just so we don't absolutely spam the CPU
		if (!input_poll_event(WALLOS_INPUT_DEVICE_KEYBOARD, &ev)) continue;
		if (ev.data.keyboard.state == WALLOS_INPUT_STATE_RELEASED) continue;

		wallos_key_t key = ev.data.keyboard.key;
		uint32_t mods = ev.data.keyboard.modifiers;

		switch (key) {
			case WALLOS_KEY_LEFT:         return ARROW_LEFT;
			case WALLOS_KEY_RIGHT:        return ARROW_RIGHT;
			case WALLOS_KEY_UP:           return ARROW_UP;
			case WALLOS_KEY_DOWN:         return ARROW_DOWN;
			case WALLOS_KEY_DELETE:       return DEL_KEY;
			case WALLOS_KEY_HOME:         return HOME_KEY;
			case WALLOS_KEY_END:          return END_KEY;
			case WALLOS_KEY_PAGEUP:       return PAGE_UP;
			case WALLOS_KEY_PAGEDOWN:     return PAGE_DOWN;
			case WALLOS_KEY_BACKSPACE:    return BACKSPACE;
			case WALLOS_KEY_ENTER:
			case WALLOS_KEY_NUMPAD_ENTER: return ENTER;
			case WALLOS_KEY_ESCAPE:       return ESC;
			case WALLOS_KEY_TAB:          return TAB;
			default:                      break;
		}

		if (mods & WALLOS_MOD_CTRL) {
			switch (key) {
				case WALLOS_KEY_C: return CTRL_C;
				case WALLOS_KEY_Q: return CTRL_Q;
				case WALLOS_KEY_S: return CTRL_S;
				case WALLOS_KEY_F: return CTRL_F;
				case WALLOS_KEY_H: return CTRL_H;
				case WALLOS_KEY_L: return CTRL_L;
				default:           break;
			}
		}


		uint8_t ch = wallos_key_to_cp437(key, mods);
		if (ch) return (int) ch;
	}
}

/* ====================== Syntax highlight color scheme  ==================== */

int is_separator(int c) {
	return c == '\0' || isspace(c) || strchr(",.()+-/*=~%[];", c) != NULL;
}

/* Return true if the specified row last char is part of a multi line comment
 * that starts at this row or at one before, and does not end at the end
 * of the row but spawns to the next row. */
int editorRowHasOpenComment(erow* row) {
	if (row->hl && row->rsize && row->hl[row->rsize - 1] == HL_MLCOMMENT && (row->rsize < 2 || (row->render[row->rsize - 2] != '*' || row->render[row->rsize - 1] != '/'))) return 1;
	return 0;
}

/* Set every byte of row->hl (that corresponds to every character in the line)
 * to the right syntax highlight type (HL_* defines). */
void editorUpdateSyntax(erow* row) {
	/* Kalloc is quicker than krealloc, and we're memsetting afterwards anyway*/
	kfree(row->hl);
	row->hl = kalloc(row->rsize);
	memset(row->hl, HL_NORMAL, row->rsize);

	if (E.syntax == NULL) return; /* No syntax, everything is HL_NORMAL. */

	int i, prev_sep, in_string, in_comment;
	char* p;
	char** keywords = E.syntax->keywords;
	char* scs = E.syntax->singleline_comment_start;
	char* mcs = E.syntax->multiline_comment_start;
	char* mce = E.syntax->multiline_comment_end;

	/* Point to the first non-space char. */
	p = row->render;
	i = 0; /* Current char offset */
	while (*p && isspace(*p)) {
		p++;
		i++;
	}
	prev_sep = 1; /* Tell the parser if 'i' points to start of word. */
	in_string = 0; /* Are we inside "" or '' ? */
	in_comment = 0; /* Are we inside multi-line comment? */

	/* If the previous line has an open comment, this line starts
	 * with an open comment state. */
	if (row->idx > 0 && editorRowHasOpenComment(&E.row[row->idx - 1]))
		in_comment = 1;

	while (*p) {
		/* Handle // comments. */
		if (prev_sep && *p == scs[0] && *(p + 1) == scs[1]) {
			/* From here to end is a comment */
			memset(row->hl + i, HL_COMMENT, row->size - i);
			return;
		}

		/* Handle multi line comments. */
		if (in_comment) {
			row->hl[i] = HL_MLCOMMENT;
			if (*p == mce[0] && *(p + 1) == mce[1]) {
				row->hl[i + 1] = HL_MLCOMMENT;
				p += 2;
				i += 2;
				in_comment = 0;
				prev_sep = 1;
				continue;
			} else {
				prev_sep = 0;
				p++;
				i++;
				continue;
			}
		} else if (*p == mcs[0] && *(p + 1) == mcs[1]) {
			row->hl[i] = HL_MLCOMMENT;
			row->hl[i + 1] = HL_MLCOMMENT;
			p += 2;
			i += 2;
			in_comment = 1;
			prev_sep = 0;
			continue;
		}

		/* Handle "" and '' */
		if (in_string) {
			row->hl[i] = HL_STRING;
			if (*p == '\\') {
				row->hl[i + 1] = HL_STRING;
				p += 2;
				i += 2;
				prev_sep = 0;
				continue;
			}
			if (*p == in_string) in_string = 0;
			p++;
			i++;
			continue;
		} else {
			if (*p == '"' || *p == '\'') {
				in_string = *p;
				row->hl[i] = HL_STRING;
				p++;
				i++;
				prev_sep = 0;
				continue;
			}
		}

		/* Handle non printable chars. */
		if (!isprint(*p)) {
			row->hl[i] = HL_NONPRINT;
			p++;
			i++;
			prev_sep = 0;
			continue;
		}

		/* Handle numbers */
		if ((isdigit(*p) && (prev_sep || row->hl[i - 1] == HL_NUMBER)) || (*p == '.' && i > 0 && row->hl[i - 1] == HL_NUMBER)) {
			row->hl[i] = HL_NUMBER;
			p++;
			i++;
			prev_sep = 0;
			continue;
		}

		/* Handle keywords and lib calls */
		if (prev_sep) {
			int j;
			for (j = 0; keywords[j]; j++) {
				int klen = strlen(keywords[j]);
				int kw2 = keywords[j][klen - 1] == '|';
				if (kw2) klen--;

				if (!memcmp(p, keywords[j], klen) && is_separator(*(p + klen))) {
					/* Keyword */
					memset(row->hl + i, kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
					p += klen;
					i += klen;
					break;
				}
			}
			if (keywords[j] != NULL) {
				prev_sep = 0;
				continue; /* We had a keyword match */
			}
		}

		/* Not special chars */
		prev_sep = is_separator(*p);
		p++;
		i++;
	}

	/* Propagate syntax change to the next row if the open commen
	 * state changed. This may recursively affect all the following rows
	 * in the file. */
	int oc = editorRowHasOpenComment(row);
	if (row->hl_oc != oc && row->idx + 1 < E.numrows)
		editorUpdateSyntax(&E.row[row->idx + 1]);
	row->hl_oc = oc;
}

/* Maps syntax highlight token types to a display.h color.
 * I made these colors *very* roughly reflect what I'm used to in my own IDE themes.
 */
int editorSyntaxToColor(int hl) {
	switch (hl) {
		case HL_COMMENT:
		case HL_MLCOMMENT: return DISPLAY_COLOR_DARK_GREY;
		case HL_KEYWORD1:  return DISPLAY_COLOR_LIGHT_GREEN;
		case HL_KEYWORD2:  return DISPLAY_COLOR_LIGHT_CYAN;
		case HL_STRING:    return DISPLAY_COLOR_YELLOW;
		case HL_NUMBER:    return DISPLAY_COLOR_PINK;
		case HL_MATCH:     return DISPLAY_COLOR_LIGHT_BLUE;
		default:           return DISPLAY_COLOR_LIGHT_GREY;
	}
}

/* Select the syntax highlight scheme depending on the filename,
 * setting it in the global state E.syntax. */
void editorSelectSyntaxHighlight(char* filename) {
	for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
		struct editorSyntax* s = HLDB + j;
		unsigned int i = 0;
		while (s->filematch[i]) {
			char* p;
			int patlen = strlen(s->filematch[i]);
			if ((p = strstr(filename, s->filematch[i])) != NULL) {
				if (s->filematch[i][0] != '.' || p[patlen] == '\0') {
					E.syntax = s;
					return;
				}
			}
			i++;
		}
	}
}

/* ======================= Editor rows implementation ======================= */

/* Update the rendered version and the syntax highlight of a row. */
void editorUpdateRow(erow* row) {
	unsigned int tabs = 0, nonprint = 0;
	int j, idx;

	/* Create a version of the row we can directly print on the screen,
	 * respecting tabs, substituting non printable characters with '?'. */
	kfree(row->render);
	for (j = 0; j < row->size; j++)
		if (row->chars[j] == TAB) tabs++;

	unsigned long long allocsize = (unsigned long long) row->size + tabs * 8 + nonprint * 9 + 1;
	if (allocsize > UINT32_MAX) {
		printf("Some line of the edited file is too long for kilo\n");
		/* We don't have exit(), so we leave the row in a safe, empty, state, and tell main() to exit. */
		row->render = NULL;
		row->rsize = 0;
		E.quit = 1;
		return;
	}

	row->render = kalloc(row->size + tabs * 8 + nonprint * 9 + 1);
	idx = 0;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == TAB) {
			row->render[idx++] = ' ';
			while ((idx + 1) % 8 != 0) row->render[idx++] = ' ';
		} else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->rsize = idx;
	row->render[idx] = '\0';

	/* Update the syntax highlighting attributes of the row. */
	editorUpdateSyntax(row);
}

/* Insert a row at the specified position, shifting the other rows on the bottom
 * if required. */
void editorInsertRow(int at, char* s, size_t len) {
	if (at > E.numrows) return;
	E.row = krealloc(E.row, sizeof(erow) * (E.numrows + 1));
	if (at != E.numrows) {
		memmove(E.row + at + 1, E.row + at, sizeof(E.row[0]) * (E.numrows - at));
		for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;
	}
	E.row[at].size = len;
	E.row[at].chars = kalloc(len + 1);
	memcpy(E.row[at].chars, s, len + 1);
	E.row[at].hl = NULL;
	E.row[at].hl_oc = 0;
	E.row[at].render = NULL;
	E.row[at].rsize = 0;
	E.row[at].idx = at;
	editorUpdateRow(E.row + at);
	E.numrows++;
	E.dirty++;
}

/* Free row's heap allocated stuff. */
void editorFreeRow(erow* row) {
	kfree(row->render);
	kfree(row->chars);
	kfree(row->hl);
}

/* Remove the row at the specified position, shifting the remainign on the
 * top. */
void editorDelRow(int at) {
	erow* row;

	if (at >= E.numrows) return;
	row = E.row + at;
	editorFreeRow(row);
	memmove(E.row + at, E.row + at + 1, sizeof(E.row[0]) * (E.numrows - at - 1));
	for (int j = at; j < E.numrows - 1; j++) E.row[j].idx++;
	E.numrows--;
	E.dirty++;
}

/* Turn the editor rows into a single heap-allocated string.
 * Returns the pointer to the heap-allocated string and populate the
 * integer pointed by 'buflen' with the size of the string, escluding
 * the final nulterm. */
char* editorRowsToString(int* buflen) {
	char *buf = NULL, *p;
	int totlen = 0;
	int j;

	/* Compute count of bytes */
	for (j = 0; j < E.numrows; j++)
		totlen += E.row[j].size + 1; /* +1 is for "\n" at end of every row */
	*buflen = totlen;
	totlen++; /* Also make space for nulterm */

	p = buf = kalloc(totlen);
	for (j = 0; j < E.numrows; j++) {
		memcpy(p, E.row[j].chars, E.row[j].size);
		p += E.row[j].size;
		*p = '\n';
		p++;
	}
	*p = '\0';
	return buf;
}

/* Insert a character at the specified position in a row, moving the remaining
 * chars on the right if needed. */
void editorRowInsertChar(erow* row, int at, int c) {
	if (at > row->size) {
		/* Pad the string with spaces if the insert location is outside the
		 * current length by more than a single character. */
		int padlen = at - row->size;
		/* In the next line +2 means: new char and null term. */
		row->chars = krealloc(row->chars, row->size + padlen + 2);
		memset(row->chars + row->size, ' ', padlen);
		row->chars[row->size + padlen + 1] = '\0';
		row->size += padlen + 1;
	} else {
		/* If we are in the middle of the string just make space for 1 new
		 * char plus the (already existing) null term. */
		row->chars = krealloc(row->chars, row->size + 2);
		memmove(row->chars + at + 1, row->chars + at, row->size - at + 1);
		row->size++;
	}
	row->chars[at] = c;
	editorUpdateRow(row);
	E.dirty++;
}

/* Append the string 's' at the end of a row */
void editorRowAppendString(erow* row, char* s, size_t len) {
	row->chars = krealloc(row->chars, row->size + len + 1);
	memcpy(row->chars + row->size, s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty++;
}

/* Delete the character at offset 'at' from the specified row. */
void editorRowDelChar(erow* row, int at) {
	if (row->size <= at) return;
	memmove(row->chars + at, row->chars + at + 1, row->size - at);
	editorUpdateRow(row);
	row->size--;
	E.dirty++;
}

/* Insert the specified char at the current prompt position. */
void editorInsertChar(int c) {
	int filerow = E.rowoff + E.cy;
	int filecol = E.coloff + E.cx;
	erow* row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

	/* If the row where the cursor is currently located does not exist in our
	 * logical representaion of the file, add enough empty rows as needed. */
	if (!row) {
		while (E.numrows <= filerow)
			editorInsertRow(E.numrows, "", 0);
	}
	row = &E.row[filerow];
	editorRowInsertChar(row, filecol, c);
	if (E.cx == E.screencols - 1)
		E.coloff++;
	else
		E.cx++;
	E.dirty++;
}

/* Inserting a newline is slightly complex as we have to handle inserting a
 * newline in the middle of a line, splitting the line as needed. */
void editorInsertNewline(void) {
	int filerow = E.rowoff + E.cy;
	int filecol = E.coloff + E.cx;
	erow* row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

	if (!row) {
		if (filerow == E.numrows) {
			editorInsertRow(filerow, "", 0);
			goto fixcursor;
		}
		return;
	}
	/* If the cursor is over the current line size, we want to conceptually
	 * think it's just over the last character. */
	if (filecol >= row->size) filecol = row->size;
	if (filecol == 0) {
		editorInsertRow(filerow, "", 0);
	} else {
		/* We are in the middle of a line. Split it between two rows. */
		editorInsertRow(filerow + 1, row->chars + filecol, row->size - filecol);
		row = &E.row[filerow];
		row->chars[filecol] = '\0';
		row->size = filecol;
		editorUpdateRow(row);
	}
fixcursor:
	if (E.cy == E.screenrows - 1) {
		E.rowoff++;
	} else {
		E.cy++;
	}
	E.cx = 0;
	E.coloff = 0;
}

/* Delete the char at the current prompt position. */
void editorDelChar(void) {
	int filerow = E.rowoff + E.cy;
	int filecol = E.coloff + E.cx;
	erow* row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

	if (!row || (filecol == 0 && filerow == 0)) return;
	if (filecol == 0) {
		/* Handle the case of column 0, we need to move the current line
		 * on the right of the previous one. */
		filecol = E.row[filerow - 1].size;
		editorRowAppendString(&E.row[filerow - 1], row->chars, row->size);
		editorDelRow(filerow);
		row = NULL;
		if (E.cy == 0)
			E.rowoff--;
		else
			E.cy--;
		E.cx = filecol;
		if (E.cx >= E.screencols) {
			int shift = (E.screencols - E.cx) + 1;
			E.cx -= shift;
			E.coloff += shift;
		}
	} else {
		editorRowDelChar(row, filecol - 1);
		if (E.cx == 0 && E.coloff)
			E.coloff--;
		else
			E.cx--;
	}
	if (row) editorUpdateRow(row);
	E.dirty++;
}

/* Load the specified program in the editor memory and returns 0 on success
 * or 1 on error.
 *
 * Ported from fopen()/getline() to VFS_Open()/VFS_Read().
 * The VFS has no getline() equivalent, so the whole file is read into a growable buffer and split based on '\n'
 */
int editorOpen(char* filename) {
	VFS_FD fd;
	VFS_Status st;
	char* filebuf = NULL;
	size_t filebuf_len = 0, filebuf_cap = 0;
	char chunk[512];
	size_t n;

	E.dirty = 0;
	kfree(E.filename);
	size_t fnlen = strlen(filename) + 1;
	E.filename = kalloc(fnlen);
	memcpy(E.filename, filename, fnlen);

	st = VFS_Open(filename, VFS_O_RDONLY, &fd);
	if (st != VFS_OK) {
		if (st != VFS_ERR_NOENT) {
			editorSetStatusMessage("Error opening '%s' (code %d)", filename, (int) st);
		}
		return 1;
	}

	for (;;) {
		st = VFS_Read(fd, chunk, sizeof(chunk), &n);
		if (st != VFS_OK || n == 0) break;
		if (filebuf_len + n > filebuf_cap) {
			size_t newcap = filebuf_cap ? filebuf_cap * 2 : 4096;
			while (newcap < filebuf_len + n) newcap *= 2;
			filebuf = krealloc(filebuf, newcap);
			filebuf_cap = newcap;
		}
		memcpy(filebuf + filebuf_len, chunk, n);
		filebuf_len += n;
	}
	VFS_Close(fd);

	/* Split into rows on '\n' (and trim a trailing '\r') */
	{
		size_t linestart = 0, i;
		for (i = 0; i <= filebuf_len; i++) {
			if (i == filebuf_len || filebuf[i] == '\n') {
				size_t linelen = i - linestart;
				char* linebuf;
				if (i == filebuf_len && linelen == 0) break;
				if (linelen && filebuf[linestart + linelen - 1] == '\r')
					linelen--;
				linebuf = kalloc(linelen + 1);
				memcpy(linebuf, filebuf + linestart, linelen);
				linebuf[linelen] = '\0';
				editorInsertRow(E.numrows, linebuf, linelen);
				kfree(linebuf);
				linestart = i + 1;
				/* editorInsertRow() -> editorUpdateRow() may have hit the "line too long" error and requested that we exit */
				if (E.quit) break;
			}
		}
	}
	kfree(filebuf);
	E.dirty = 0;
	return 0;
}

/* Save the current file on disk. Return 0 on success, 1 on error. */
int editorSave(void) {
	int len;
	char* buf = editorRowsToString(&len);
	VFS_FD fd;
	VFS_Status st;
	size_t written;

	st = VFS_Open(E.filename, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, &fd);
	if (st != VFS_OK) goto writeerr;

	st = VFS_Write(fd, buf, (size_t) len, &written);
	if (st != VFS_OK || (int) written != len) {
		VFS_Close(fd);
		goto writeerr;
	}

	VFS_Close(fd);
	kfree(buf);
	E.dirty = 0;
	editorSetStatusMessage("%d bytes written on disk", len);
	return 0;

writeerr:
	kfree(buf);
	editorSetStatusMessage("Can't save! I/O error (code %d)", (int) st);
	return 1;
}

/* ======================= Terminal =======================
 *
 * The original append buffer batched VT100 escape sequences to avoid flicker.
 * WallOS has direct cell-addressed display control, so editorRefreshScreen() now draws directly to the display layer.
 *
 * To avoid redrawing the entire screen on every keypress, we cache the current screen contents (character/highlight pairs), status bar, and message bar.
 * Only rows whose contents changed are redrawn.
 * Cursor updates and display_flush() are likewise skipped unless the screen or cursor actually changed.
 */

static char* screen_chars = NULL;       /* (screenrows+2) * screencols cache of chars on screen */
static unsigned char* screen_hl = NULL; /* HL_* per cell (content rows only) */
static char* line_chars = NULL;         /* one row's worth of scratch, reused every frame */
static unsigned char* line_hl = NULL;   /* one row's worth of scratch, reused every frame */
static int screen_cache_valid = 0;      /* 0 forces every row to redraw once (first frame) */
static int prev_cursor_cx = -1, prev_cursor_cy = -1;

/* (Re)allocates the screen cache for the current E.screenrows/screencols.
 * The screen never changes size (at least for the kernel terminal) so this only gets called once.
 */
static void initScreenCache(void) {
	kfree(screen_chars);
	kfree(screen_hl);
	kfree(line_chars);
	kfree(line_hl);

	size_t total_rows = (size_t) E.screenrows + 2; /* + status bar + message bar */
	screen_chars = kalloc(total_rows * (size_t) E.screencols);
	screen_hl = kalloc(total_rows * (size_t) E.screencols);
	line_chars = kalloc((size_t) E.screencols);
	line_hl = kalloc((size_t) E.screencols);
	screen_cache_valid = 0;
	prev_cursor_cx = -1;
	prev_cursor_cy = -1;
}

/* Converts a character index in row->chars to its corresponding screen column */
int editorRowCxToRx(erow* row, int cx) {
	int rx = 0;
	for (int j = 0; j < cx; j++) {
		if (j < row->size && row->chars[j] == TAB) {
			rx++;
			while ((rx + 1) % 8 != 0) rx++;
		} else {
			rx++;
		}
	}
	return rx;
}

/* Draws the logical editor state in 'E', but only where it's actually changed since the last call. */
void editorRefreshScreen(void) {
	int y;
	int any_changed = 0;

	for (y = 0; y < E.screenrows; y++) {
		int filerow = E.rowoff + y;

		/* Build the row's full-width screen content into scratch, including blank HL_NORMAL columns, then compare it against the cache before redrawing. */
		memset(line_chars, ' ', E.screencols);
		memset(line_hl, HL_NORMAL, E.screencols);

		if (filerow >= E.numrows) {
			if (E.numrows == 0 && y == E.screenrows / 3) {
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- verison %s", KILO_VERSION);
				int padding = (E.screencols - welcomelen) / 2;
				int col = 0;
				if (padding > 0) {
					line_chars[col++] = '~';
					padding--;
				}
				while (padding-- > 0 && col < E.screencols) line_chars[col++] = ' ';
				for (int k = 0; k < welcomelen && col < E.screencols; k++)
					line_chars[col++] = welcome[k];
			} else {
				line_chars[0] = '~';
			}
		} else {
			erow* r = &E.row[filerow];
			int len = r->rsize - E.coloff;
			if (len > 0) {
				if (len > E.screencols) len = E.screencols;
				char* c = r->render + E.coloff;
				unsigned char* hl = r->hl + E.coloff;
				for (int j = 0; j < len; j++) {
					line_chars[j] = c[j];
					line_hl[j] = hl[j];
				}
			}
		}

		char* cache_c = screen_chars + (size_t) y * E.screencols;
		unsigned char* cache_h = screen_hl + (size_t) y * E.screencols;
		if (screen_cache_valid && memcmp(cache_c, line_chars, E.screencols) == 0 && memcmp(cache_h, line_hl, E.screencols) == 0) {
			continue; /* Identical to what's already on screen, nothing to do. */
		}
		any_changed = 1;

		display_update_cursor(0, y);
		display_clear_row();
		display_set_colors_default();
		{
			int current_color = -1;
			int lim = E.screencols;
			/* Trailing columns are already blank/HL_NORMAL from clear_row().
			 * Only draw hrough the last non-blank column. */
			while (lim > 0 && line_chars[lim - 1] == ' ' && line_hl[lim - 1] == HL_NORMAL) lim--;
			for (int j = 0; j < lim; j++) {
				unsigned char hlcode = line_hl[j];
				char ch = line_chars[j];
				if (hlcode == HL_NONPRINT) {
					char sym;
					display_set_colors(DISPLAY_COLOR_BLACK, DISPLAY_DEFAULT_FG);
					if (ch <= 26)
						sym = '@' + ch;
					else
						sym = '?';
					display_putc_unfiltered(sym);
					current_color = -1; /* force re-set on next colored char */
				} else if (hlcode == HL_NORMAL) {
					if (current_color != -1) {
						display_set_colors_default();
						current_color = -1;
					}
					display_putc_unfiltered(ch);
				} else {
					int color = editorSyntaxToColor(hlcode);
					if (color != current_color) {
						display_set_colors(color, DISPLAY_DEFAULT_BG);
						current_color = color;
					}
					display_putc_unfiltered(ch);
				}
			}
		}
		display_set_colors_default();
		memcpy(cache_c, line_chars, E.screencols);
		memcpy(cache_h, line_hl, E.screencols);
	}

	/* Status bar - rendered in "reverse" by swapping fg/bg. */
	{
		memset(line_chars, ' ', E.screencols);
		char status[80], rstatus[80];
		int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.filename, E.numrows, E.dirty ? "(modified)" : "");
		int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.rowoff + E.cy + 1, E.numrows);
		int x;
		if (len > E.screencols) len = E.screencols;
		for (x = 0; x < len; x++) line_chars[x] = status[x];
		while (x < E.screencols) {
			if (E.screencols - x == rlen) {
				int k;
				for (k = 0; k < rlen; k++, x++) line_chars[x] = rstatus[k];
				break;
			} else {
				x++; /* already a space via memset above */
			}
		}

		char* cache_c = screen_chars + (size_t) E.screenrows * E.screencols;
		if (!screen_cache_valid || memcmp(cache_c, line_chars, E.screencols) != 0) {
			any_changed = 1;
			display_update_cursor(0, E.screenrows);
			display_clear_row();
			display_set_colors(DISPLAY_DEFAULT_BG, DISPLAY_DEFAULT_FG);
			for (x = 0; x < E.screencols; x++) display_putc_unfiltered(line_chars[x]);
			display_set_colors_default();
			memcpy(cache_c, line_chars, E.screencols);
		}
	}

	/* Status message row, expires 5 seconds after being set. */
	{
		memset(line_chars, ' ', E.screencols);
		int msglen = strlen(E.statusmsg);
		int show = 0;
		if (msglen && (timer_uptime_ms() - E.statusmsg_time) < 5000) {
			show = msglen <= E.screencols ? msglen : E.screencols;
			for (int x = 0; x < show; x++) line_chars[x] = E.statusmsg[x];
		}

		char* cache_c = screen_chars + (size_t) (E.screenrows + 1) * E.screencols;
		if (!screen_cache_valid || memcmp(cache_c, line_chars, E.screencols) != 0) {
			any_changed = 1;
			display_update_cursor(0, E.screenrows + 1);
			display_clear_row();
			for (int x = 0; x < show; x++) display_putc_unfiltered(E.statusmsg[x]);
			memcpy(cache_c, line_chars, E.screencols);
		}
	}

	/* Put cursor at its current position. Note that the horizontal position
	 * at which the cursor is displayed may be different compared to 'E.cx'
	 * because of TABs. */
	/* Above is an original comment. I fixed this, see editorRowCxToRx(). */
	int cx = 0;
	{
		int filerow = E.rowoff + E.cy;
		erow* row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
		if (row) cx = editorRowCxToRx(row, E.cx + E.coloff) - editorRowCxToRx(row, E.coloff);
	}

	if (any_changed || cx != prev_cursor_cx || E.cy != prev_cursor_cy || !screen_cache_valid) {
		display_disable_cursor();
		display_update_cursor(cx, E.cy);
		display_enable_cursor(0, 15);
		display_flush();
		prev_cursor_cx = cx;
		prev_cursor_cy = E.cy;
	}

	screen_cache_valid = 1;
}

/* Set an editor status message for the second line of the status, at the
 * end of the screen. */
void editorSetStatusMessage(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = timer_uptime_ms();
}

/* =============================== Find mode ================================ */

#define KILO_QUERY_LEN 256

void editorFind(void) {
	char query[KILO_QUERY_LEN + 1] = {0};
	int qlen = 0;
	int last_match = -1; /* Last line where a match was found. -1 for none. */
	int find_next = 0; /* if 1 search next, if -1 search prev. */
	int saved_hl_line = -1;  /* No saved HL */
	char* saved_hl = NULL;

#define FIND_RESTORE_HL \
	do { \
		if (saved_hl) { \
			memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize); \
			kfree(saved_hl); \
			saved_hl = NULL; \
		} \
	} while (0)

	/* Save the cursor position in order to restore it later. */
	int saved_cx = E.cx, saved_cy = E.cy;
	int saved_coloff = E.coloff, saved_rowoff = E.rowoff;

	while (1) {
		editorSetStatusMessage(
			"Search: %s (Use ESC/Arrows/Enter)", query
		);
		editorRefreshScreen();

		int c = editorReadKey();
		if (c == DEL_KEY || c == CTRL_H || c == BACKSPACE) {
			if (qlen != 0) query[--qlen] = '\0';
			last_match = -1;
		} else if (c == ESC || c == ENTER) {
			if (c == ESC) {
				E.cx = saved_cx;
				E.cy = saved_cy;
				E.coloff = saved_coloff;
				E.rowoff = saved_rowoff;
			}
			FIND_RESTORE_HL;
			editorSetStatusMessage("");
			return;
		} else if (c == ARROW_RIGHT || c == ARROW_DOWN) {
			find_next = 1;
		} else if (c == ARROW_LEFT || c == ARROW_UP) {
			find_next = -1;
		} else if (isprint(c)) {
			if (qlen < KILO_QUERY_LEN) {
				query[qlen++] = c;
				query[qlen] = '\0';
				last_match = -1;
			}
		}

		/* Search occurrence. */
		if (last_match == -1) find_next = 1;
		if (find_next) {
			char* match = NULL;
			int match_offset = 0;
			int i, current = last_match;

			for (i = 0; i < E.numrows; i++) {
				current += find_next;
				if (current == -1) current = E.numrows - 1;
				else if (current == E.numrows) current = 0;
				match = strstr(E.row[current].render, query);
				if (match) {
					match_offset = match - E.row[current].render;
					break;
				}
			}
			find_next = 0;

			/* Highlight */
			FIND_RESTORE_HL;

			if (match) {
				erow* row = &E.row[current];
				last_match = current;
				if (row->hl) {
					saved_hl_line = current;
					saved_hl = kalloc(row->rsize);
					memcpy(saved_hl, row->hl, row->rsize);
					memset(row->hl + match_offset, HL_MATCH, qlen);
				}
				E.cy = 0;
				E.cx = match_offset;
				E.rowoff = current;
				E.coloff = 0;
				/* Scroll horizontally as needed. */
				if (E.cx > E.screencols) {
					int diff = E.cx - E.screencols;
					E.cx -= diff;
					E.coloff += diff;
				}
			}
		}
	}
}

/* ========================= Editor events handling  ======================== */

/* Handle cursor position change because arrow keys were pressed. */
void editorMoveCursor(int key) {
	int filerow = E.rowoff + E.cy;
	int filecol = E.coloff + E.cx;
	int rowlen;
	erow* row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

	switch (key) {
		case ARROW_LEFT:
			if (E.cx == 0) {
				if (E.coloff) {
					E.coloff--;
				} else {
					if (filerow > 0) {
						E.cy--;
						E.cx = E.row[filerow - 1].size;
						if (E.cx > E.screencols - 1) {
							E.coloff = E.cx - E.screencols + 1;
							E.cx = E.screencols - 1;
						}
					}
				}
			} else {
				E.cx -= 1;
			}
			break;
		case ARROW_RIGHT:
			if (row && filecol < row->size) {
				if (E.cx == E.screencols - 1) {
					E.coloff++;
				} else {
					E.cx += 1;
				}
			} else if (row && filecol == row->size) {
				E.cx = 0;
				E.coloff = 0;
				if (E.cy == E.screenrows - 1) {
					E.rowoff++;
				} else {
					E.cy += 1;
				}
			}
			break;
		case ARROW_UP:
			if (E.cy == 0) {
				if (E.rowoff) E.rowoff--;
			} else {
				E.cy -= 1;
			}
			break;
		case ARROW_DOWN:
			if (filerow < E.numrows) {
				if (E.cy == E.screenrows - 1) {
					E.rowoff++;
				} else {
					E.cy += 1;
				}
			}
			break;
	}
	/* Fix cx if the current line has not enough chars. */
	filerow = E.rowoff + E.cy;
	filecol = E.coloff + E.cx;
	row = (filerow >= E.numrows) ? NULL : &E.row[filerow];
	rowlen = row ? row->size : 0;
	if (filecol > rowlen) {
		E.cx -= filecol - rowlen;
		if (E.cx < 0) {
			E.coloff += E.cx;
			E.cx = 0;
		}
	}
}

/* Process events arriving from the standard input, which is, the user
 * is typing stuff on the terminal. */
#define KILO_QUIT_TIMES 1
void editorProcessKeypress(void) {
	/* When the file is modified, requires Ctrl-q to be pressed N times
	 * before actually quitting. */
	static int quit_times = KILO_QUIT_TIMES;

	int c = editorReadKey();
	switch (c) {
		case ENTER:         /* Enter */
			editorInsertNewline();
			break;
		case CTRL_C:        /* Ctrl-c */
		/* We ignore ctrl-c, it can't be so simple to lose the changes
			 * to the edited file. */
			break;
		case CTRL_Q:        /* Ctrl-q */
		/* Quit if the file was already saved. */
			if (E.dirty && quit_times) {
				// Needed shorter message, my testing happens on an 800x600 screen, and this would cause weird renderering
				// Should probably make sure in setStatusMessage that this can't happen, but whatever
				editorSetStatusMessage("File has unsaved changes. Press Ctrl-Q %d more times to quit.", quit_times);
				quit_times--;
				return;
			}
			E.quit = 1;
			break;
		case CTRL_S:        /* Ctrl-s */
			editorSave();
			break;
		case CTRL_F:
			editorFind();
			break;
		case BACKSPACE:     /* Backspace */
		case CTRL_H:        /* Ctrl-h */
		case DEL_KEY:
			editorDelChar();
			break;
		case PAGE_UP:
		case PAGE_DOWN:
			if (c == PAGE_UP && E.cy != 0)
				E.cy = 0;
			else if (c == PAGE_DOWN && E.cy != E.screenrows - 1)
				E.cy = E.screenrows - 1;
			{
				int times = E.screenrows;
				while (times--)
					editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
		case CTRL_L: /* ctrl+l, clear screen */
		/* Just refresht the line as side effect. */
			break;
		case ESC:
			/* Nothing to do for ESC in this mode. */
			break;
		default:
			editorInsertChar(c);
			break;
	}

	quit_times = KILO_QUIT_TIMES; /* Reset it to the original value. */
}

int editorFileWasModified(void) {
	return E.dirty;
}

/* The kernel terminal's size is fixed for the process's lifetime , so this is just queried once at startup */
void updateWindowSize(void) {
	display_get_dimensions_chars(&E.screencols, &E.screenrows);
	E.screenrows -= 2; /* Get room for status bar. */
}

void initEditor(void) {
	E.cx = 0;
	E.cy = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;
	E.dirty = 0;
	E.filename = NULL;
	E.syntax = NULL;
	E.quit = 0;
	updateWindowSize();
	initScreenCache();
	display_clear();
}

int kilo_main(int argc, char** argv) {
	if (argc != 2) {
		printf("Usage: kilo <filename>\n");
		return 1;
	}

	// resolve the relative path against CWD
	char resolved_path[VFS_PATH_MAX];
	if (!ws_resolvePath(argv[1], resolved_path, sizeof(resolved_path))) {
		printf("Error: path too long: %s\n", argv[1]);
		return 1;
	}

	initEditor();
	editorSelectSyntaxHighlight(resolved_path);
	editorOpen(resolved_path);
	editorSetStatusMessage(
		"HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find"
	);
	/* No exit() in kernel context (for now) */
	while (!E.quit) {
		editorRefreshScreen();
		editorProcessKeypress();
	}

	display_clear();
	display_update_cursor(0, 0);

	return 0;
}
const ws_command_argument_t kilo_args[] = {
	{WS_ARG_TYPE_GENERIC, true, "<filename>", NULL, "File to open for editing."},
};
const size_t kilo_args_count = sizeof(kilo_args) / sizeof(kilo_args[0]);

ws_command_t kilo_cmd = {
	.command_name = "kilo",
	.alias_count = 0,
	.aliases = NULL,
	.arguments = kilo_args,
	.arguments_count = kilo_args_count,
	.env_func = NULL,
	.help_func = NULL,
	.main_void = NULL,
	.main_func = kilo_main,
	.major = 0,
	.minor = 0,
	.patch = 1
};