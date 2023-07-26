// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// This file is a mess. Be prepared for long switch statements and lots of chars. 
// It's also c++, for some reason. (It's all written in C but gets angry if you change it).
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

#include <drivers/keyboard.h>
#include <idt.h>
#include <stdio.h>
#include <klibc/logger.h>
#include <string.h>

// The double extern is cursed. (Also screw gcc attributes, always ruining my nice code).
extern "C" {
	extern __attribute__((interrupt)) void keyboard_handler(struct interrupt_frame* frame);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// General Keyboard & Scancode stuff
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Keyboard state go brrrrr
static KeyboardState currentState = {};

KeyboardState getKeyboardState() {
	return currentState;
}

char convertCharacter(char input) {
	if (currentState.shifted || currentState.caps) {
		// Capital characters are 0x20 (32) away from their lowercase counterpart.
		return (input - 0x20);
	} else {
		return input;
	}
}

char scancode_to_char(uint8_t sc) {
	// Handle escaped keys here. These are mostly media keys although there are some weird ones.
	if (currentState.last_scancode == SC_ESCAPED_0 || currentState.last_scancode == SC_ESCAPED_1) {
		switch (sc) {
			default: return '\0';
		}
	}

	// Handle the special cases of shifted keys here.
	// It's mostly number row and special chars
	if (currentState.shifted) {
		switch (sc) {
			case SC_BACKTICK: 		return '~';
			case SC_1: 				return '!';
			case SC_2: 				return '@';
			case SC_3: 				return '#';
			case SC_4: 				return '$';
			case SC_5: 				return '%';
			case SC_6: 				return '^';
			case SC_7: 				return '&';
			case SC_8: 				return '*';
			case SC_9: 				return '(';
			case SC_0: 				return ')';
			case SC_MINUS: 			return '_';
			case SC_EQUAL: 			return '+';
			case SC_LEFT_BRACKET:	return '{';
			case SC_RIGHT_BRACKET: 	return '}';
			case SC_BACKSLASH: 		return '|';
			case SC_SEMICOLON: 		return ':';
			case SC_APOSTROPHE:		return '"';
			case SC_COMMA:			return '<';
			case SC_PERIOD:			return '>';
			case SC_SLASH:			return '?';
			default: 				break;
		}
	}

	switch (sc) {
		// Special Chars
		case SC_LEFT_BRACKET: 		return '[';
		case SC_RIGHT_BRACKET: 		return ']';
		case SC_MINUS: 				return '-';
		case SC_EQUAL: 				return '=';
		case SC_SEMICOLON: 			return ';';
		case SC_APOSTROPHE: 		return '\'';
		case SC_BACKTICK: 			return '`';
		case SC_COMMA: 				return ',';
		case SC_PERIOD: 			return '.';
		case SC_SLASH: 				return '/';
		case SC_BACKSLASH: 			return '\\';
		case SC_SPACE: 				return ' ';

			// Alphabet
		case SC_A: 					return convertCharacter('a');
		case SC_B: 					return convertCharacter('b');
		case SC_C: 					return convertCharacter('c');
		case SC_D: 					return convertCharacter('d');
		case SC_E: 					return convertCharacter('e');
		case SC_F: 					return convertCharacter('f');
		case SC_G: 					return convertCharacter('g');
		case SC_H: 					return convertCharacter('h');
		case SC_I: 					return convertCharacter('i');
		case SC_J: 					return convertCharacter('j');
		case SC_K: 					return convertCharacter('k');
		case SC_L: 					return convertCharacter('l');
		case SC_M: 					return convertCharacter('m');
		case SC_N: 					return convertCharacter('n');
		case SC_O: 					return convertCharacter('o');
		case SC_P: 					return convertCharacter('p');
		case SC_Q: 					return convertCharacter('q');
		case SC_R: 					return convertCharacter('r');
		case SC_S: 					return convertCharacter('s');
		case SC_T: 					return convertCharacter('t');
		case SC_U: 					return convertCharacter('u');
		case SC_V: 					return convertCharacter('v');
		case SC_W: 					return convertCharacter('w');
		case SC_X: 					return convertCharacter('x');
		case SC_Y: 					return convertCharacter('y');
		case SC_Z: 					return convertCharacter('z');

			// Numbers
		case SC_0:
		case SC_KEYPAD_0: 			return '0';
		case SC_1:
		case SC_KEYPAD_1: 			return '1';
		case SC_2:
		case SC_KEYPAD_2: 			return '2';
		case SC_3:
		case SC_KEYPAD_3: 			return '3';
		case SC_4:
		case SC_KEYPAD_4: 			return '4';
		case SC_5:
		case SC_KEYPAD_5: 			return '5';
		case SC_6:
		case SC_KEYPAD_6: 			return '6';
		case SC_7:
		case SC_KEYPAD_7: 			return '7';
		case SC_8:
		case SC_KEYPAD_8: 			return '8';
		case SC_9:
		case SC_KEYPAD_9: 			return '9';

		case SC_KEYPAD_MINUS: 		return '-';
		case SC_KEYPAD_ASTERISK: 	return '*';
		case SC_KEYPAD_PERIOD: 		return '.';
		case SC_KEYPAD_PLUS: 		return '+';

		case SC_ENTER: 				return '\n';
		case SC_TAB: 				return '\t';
		case SC_BACKSPACE: 			return '\b';
		default: 					return '\0';
	}
}

void printKeyboardState() {
	printf("Left Shift:  %d\n", currentState.l_shift);
	printf("Left Alt:    %d\n", currentState.l_alt);
	printf("Left Ctrl:   %d\n", currentState.l_ctrl);
	printf("Right Shift: %d\n", currentState.r_shift);
	printf("Right Alt:   %d\n", currentState.r_alt);
	printf("Right Shift: %d\n", currentState.r_shift);
	printf("Right Ctrl:  %d\n", currentState.r_ctrl);
	printf("Caps Lock:   %d\n", currentState.caps);
	printf("Num Lock:    %d\n", currentState.numlock);
	printf("Scroll Lock: %d\n", currentState.scroll_lock);
	printf("Alt Pressed: %d\n", currentState.alt_pressed);
	printf("Shifted:     %d\n", currentState.shifted);
	printf("Control:     %d\n", currentState.ctrl_pressed);
	printf("Last SC:     %d\n", currentState.last_scancode);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Gets and Getc
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------


char buf[GETS_SIZE];
char lastbuf[GETS_SIZE];
bool getc_gotten = false;

/**
 * @brief Get the last string sent from the keyboard.
 * This does NOT print anything to the screen.
 * Normal gets() should be implemented in the terminal.
 *
 * @return char* String of what was entered before enter was pressed. (including enter)
 */
char* kb_gets() {
	while (true) {
		char current = kb_getc();
		strcat_c(buf, current, GETS_SIZE);
		if (current == '\n') {
			memcpy(lastbuf, buf, GETS_SIZE * sizeof(char));
			memset(buf, 0, GETS_SIZE);
			buf[0] = '\0';
			return lastbuf;
		}
	}
}

/**
 * @brief Get the last character sent from the keyboard.
 *
 * @return char
 */
char kb_getc() {
	while (true) {
		if (!getc_gotten) {
			getc_gotten = true;
			return scancode_to_char(currentState.last_scancode);
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Hooks
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// cursed ass line of code
void (*hooks[HOOK_BUF_SIZE])(uint8_t);
int currentHookAmount = 0;

/**
 * @brief Register a command that hooks into the keyboard.
 * This will get ran whenever a keypress happens.
 * This is honestly A REALLY BAD WAY TO HANDLE THIS.
 * This is mostly for development. When we get to userland we should
 * implement a way better way of doing this.
 *
 * @param f Function to be called.
 */
void registerKeyboardHook(void(*f)(uint8_t)) {
	hooks[currentHookAmount] = f;
	currentHookAmount++;
}

/**
 * @brief Deregisters a hook.
 *
 * @param f Pointer to the hook.
 */
void deregisterKeyboardHook(void(*f)(uint8_t)) {
	int found_index = -1;

	// Find the index of the hook to be removed
	for (int i = 0; i < HOOK_BUF_SIZE; i++) {
		if (hooks[i] == f) {
			found_index = i;
			break;
		}
	}

	// If the hook is not found, do nothing
	if (found_index == -1) {
		return;
	}

	// Shift the elements to the left to fill the gap
	for (int i = found_index; i < HOOK_BUF_SIZE - 1; i++) {
		hooks[i] = hooks[i + 1];
	}

	// Clear the last element so we dont call something twice.
	hooks[HOOK_BUF_SIZE - 1] = NULL;
	currentHookAmount--;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Init and handler.
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------


void handle_scancode(uint8_t sc) {
	// escaped codes are pain in the ass
	if (sc == SC_ESCAPED_0 || sc == SC_ESCAPED_1) {
		currentState.last_scancode = sc;
		return;
	}
	switch (sc) {
		case SC_LEFT_SHIFT:  		currentState.l_shift = true; 		break;
		case SC_LEFT_SHIFT + 0x80:	currentState.l_shift = false; 		break;// Key released
		case SC_RIGHT_SHIFT:		currentState.r_shift = true; 		break;
		case SC_RIGHT_SHIFT + 0x80: currentState.r_shift = false; 		break; // Key released
		case SC_CAPS_LOCK: 			currentState.caps = true;			break;
		case SC_CAPS_LOCK + 0x80: 	currentState.caps = false; 			break; // Key released
		case SC_NUM_LOCK: 			currentState.numlock = true;		break;
		case SC_NUM_LOCK + 0x80: 	currentState.numlock = false; 		break; // Key released
		case SC_SCROLL_LOCK: 		currentState.scroll_lock = true;	break;
		case SC_SCROLL_LOCK + 0x80: currentState.scroll_lock = false; 	break; // Key released
			// Right alt ruins everything
		case SC_LEFT_ALT: {
				if (currentState.last_scancode != SC_ESCAPED_0)
					currentState.l_alt = true;
				else
					currentState.r_alt = true;
				break;
			}
		case SC_LEFT_ALT + 0x80: {
				if (currentState.last_scancode != SC_ESCAPED_0)
					currentState.l_alt = false;
				else
					currentState.r_alt = false;
				break;
			}
							   // Right control also ruins everything.
		case SC_LEFT_CONTROL: {
				if (currentState.last_scancode != SC_ESCAPED_0)
					currentState.l_ctrl = true;
				else
					currentState.r_ctrl = true;
				break;
			}
		case SC_LEFT_CONTROL + 0x80: {
				if (currentState.last_scancode != SC_ESCAPED_0)
					currentState.l_ctrl = false;
				else
					currentState.r_ctrl = false;
				break;
			}
		default: break;
	}

	// Update the state for Alt, Ctrl, and Shift
	if (currentState.l_alt || currentState.r_alt) 		currentState.alt_pressed = true;
	if (!currentState.l_alt && !currentState.r_alt) 	currentState.alt_pressed = false;
	if (currentState.l_shift || currentState.r_shift) 	currentState.shifted = true;
	if (!currentState.l_shift && !currentState.r_shift) currentState.shifted = false;
	if (currentState.l_ctrl || currentState.r_ctrl) 	currentState.ctrl_pressed = true;
	if (!currentState.l_ctrl && !currentState.r_ctrl) 	currentState.ctrl_pressed = false;

	//printf("\nCurrent SC:  %d (%c)\n", sc, scancode_to_char(sc));
	//printKeyboardState();
	//printf("%c", scancode_to_char(sc));
	if (strlen(buf) >= 256) {
		// decide what to do if the buffer goes over.
	}
	//strcat_c(buf, scancode_to_char(sc), 256);

	getc_gotten = false;
	currentState.last_scancode = sc;

	// Call all the hooks.
	for (int i = 0; i < currentHookAmount; i++) {
		hooks[i](sc);
	}
}

void keyboard_init() {
	// If we need to enable the 8042 we should probably do it here
	// In qemu we dont, so I wont.
	add_interrupt_handler(0x21, keyboard_handler, 0, 0x8E);
	currentState.last_scancode = 0x00;

	// Everything needs to come before this, these start interrupt for the keyboard
	irq_enable(1);
}