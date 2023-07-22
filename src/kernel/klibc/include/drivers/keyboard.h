#ifndef KEYBOARD_HPP
#define KEYBOARD_HPP
#include <idt.h>

#define GETS_SIZE 256
#define HOOK_BUF_SIZE 10

#ifdef __cplusplus
extern "C" {
#endif
	typedef enum {
		// Not actual chars
		SC_ESCAPE = 0x01, SC_BACKSPACE = 0x0E,
		SC_TAB = 0x0F, SC_LEFT_CONTROL = 0x1D,
		SC_ENTER = 0x1C, SC_LEFT_SHIFT = 0x2A,
		SC_RIGHT_SHIFT = 0x36, SC_LEFT_ALT = 0x38,
		SC_CAPS_LOCK = 0x3A, SC_NUM_LOCK = 0x45,
		SC_SCROLL_LOCK = 0x46,

		// Weird stuff
		SC_ALT_SYSRQ = 0x54, // Alt + SysRq (Alt + Print Screen)
		SC_UNUSED = 0x55,    // (key does not generate a scancode)

		// Special Chars
		SC_LEFT_BRACKET = 0x1A,
		SC_RIGHT_BRACKET = 0x1B,
		SC_MINUS = 0x0C,
		SC_EQUAL = 0x0D,
		SC_SEMICOLON = 0x27,
		SC_APOSTROPHE = 0x28,
		SC_BACKTICK = 0x29,
		SC_COMMA = 0x33,
		SC_PERIOD = 0x34,
		SC_SLASH = 0x35,
		SC_BACKSLASH = 0x2B,
		SC_SPACE = 0x39,

		// Alphabet
		SC_A = 0x1E, SC_B = 0x30, SC_C = 0x2E, SC_D = 0x20,
		SC_E = 0x12, SC_F = 0x21, SC_G = 0x22, SC_H = 0x23,
		SC_I = 0x17, SC_J = 0x24, SC_K = 0x25, SC_L = 0x26,
		SC_M = 0x32, SC_N = 0x31, SC_O = 0x18, SC_P = 0x19,
		SC_Q = 0x10, SC_R = 0x13, SC_S = 0x1F, SC_T = 0x14,
		SC_U = 0x16, SC_V = 0x2F, SC_W = 0x11, SC_X = 0x2D,
		SC_Y = 0x15, SC_Z = 0x2C,

		// Function Keys
		SC_F1 = 0x3B, SC_F2 = 0x3C, SC_F3 = 0x3D, SC_F4 = 0x3E, SC_F5 = 0x3F,
		SC_F6 = 0x40, SC_F7 = 0x41, SC_F8 = 0x42, SC_F9 = 0x43, SC_F10 = 0x44,
		SC_F11 = 0x57, SC_F12 = 0x58,

		// Numbers
		SC_0 = 0x0B, SC_1 = 0x02, SC_2 = 0x03, SC_3 = 0x04,
		SC_4 = 0x05, SC_5 = 0x06, SC_6 = 0x07, SC_7 = 0x08,
		SC_8 = 0x09, SC_9 = 0x0A,

		// Keypad
		SC_KEYPAD_0 = 0x52, SC_KEYPAD_1 = 0x4F, SC_KEYPAD_2 = 0x50,
		SC_KEYPAD_3 = 0x51, SC_KEYPAD_4 = 0x4B, SC_KEYPAD_5 = 0x4C,
		SC_KEYPAD_6 = 0x4D, SC_KEYPAD_7 = 0x47, SC_KEYPAD_8 = 0x48,
		SC_KEYPAD_9 = 0x49,
		SC_KEYPAD_MINUS = 0x4A, SC_KEYPAD_ASTERISK = 0x37,
		SC_KEYPAD_PERIOD = 0x53, SC_KEYPAD_PLUS = 0x4E,

		SC_ESCAPED_0 = 0xE0, SC_ESCAPED_1 = 0xE1,
	} Scancode;

	typedef struct {
		bool l_shift;
		bool l_alt;
		bool r_shift;
		bool r_alt;
		bool l_ctrl;
		bool r_ctrl;
		bool caps;
		bool numlock;
		bool scroll_lock;
		bool alt_pressed;
		bool ctrl_pressed;
		bool shifted;
		uint8_t last_scancode;
	} KeyboardState;

	char scancode_to_char(uint8_t sc);
	KeyboardState getKeyboardState();
	void printKeyboardState();
	void keyboard_init();
	void handle_scancode(uint8_t sc);
	int  getCommandBufferSize();
	void registerKeyboardHook(void(*f)(uint8_t));
	void deregisterKeyboardHook(void(*f)(uint8_t));
	char kb_getc();
	char* kb_gets();
#ifdef __cplusplus
}

#endif
#endif