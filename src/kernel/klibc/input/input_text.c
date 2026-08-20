#include <input/input_text.h>

// ------------------------------------------------------------------------------------------------
// key -> cp437
// ------------------------------------------------------------------------------------------------

uint8_t wallos_key_to_cp437(wallos_key_t key, uint32_t modifiers) {
	bool shift = (modifiers & WALLOS_MOD_SHIFT) != 0;
	bool caps = (modifiers & WALLOS_MOD_CAPS) != 0;

	// Caps lock only affects letters, and shift overrides it
	// I apparently wasn't handling this correctly in the past
	bool uppercase = shift ^ caps;

	switch (key) {
		// Alphabet
		case WALLOS_KEY_A: return uppercase ? 'A' : 'a';
		case WALLOS_KEY_B: return uppercase ? 'B' : 'b';
		case WALLOS_KEY_C: return uppercase ? 'C' : 'c';
		case WALLOS_KEY_D: return uppercase ? 'D' : 'd';
		case WALLOS_KEY_E: return uppercase ? 'E' : 'e';
		case WALLOS_KEY_F: return uppercase ? 'F' : 'f';
		case WALLOS_KEY_G: return uppercase ? 'G' : 'g';
		case WALLOS_KEY_H: return uppercase ? 'H' : 'h';
		case WALLOS_KEY_I: return uppercase ? 'I' : 'i';
		case WALLOS_KEY_J: return uppercase ? 'J' : 'j';
		case WALLOS_KEY_K: return uppercase ? 'K' : 'k';
		case WALLOS_KEY_L: return uppercase ? 'L' : 'l';
		case WALLOS_KEY_M: return uppercase ? 'M' : 'm';
		case WALLOS_KEY_N: return uppercase ? 'N' : 'n';
		case WALLOS_KEY_O: return uppercase ? 'O' : 'o';
		case WALLOS_KEY_P: return uppercase ? 'P' : 'p';
		case WALLOS_KEY_Q: return uppercase ? 'Q' : 'q';
		case WALLOS_KEY_R: return uppercase ? 'R' : 'r';
		case WALLOS_KEY_S: return uppercase ? 'S' : 's';
		case WALLOS_KEY_T: return uppercase ? 'T' : 't';
		case WALLOS_KEY_U: return uppercase ? 'U' : 'u';
		case WALLOS_KEY_V: return uppercase ? 'V' : 'v';
		case WALLOS_KEY_W: return uppercase ? 'W' : 'w';
		case WALLOS_KEY_X: return uppercase ? 'X' : 'x';
		case WALLOS_KEY_Y: return uppercase ? 'Y' : 'y';
		case WALLOS_KEY_Z: return uppercase ? 'Z' : 'z';

		// Number row 
		// caps lock has no effect here, only shift
		case WALLOS_KEY_NUM0: return shift ? ')' : '0';
		case WALLOS_KEY_NUM1: return shift ? '!' : '1';
		case WALLOS_KEY_NUM2: return shift ? '@' : '2';
		case WALLOS_KEY_NUM3: return shift ? '#' : '3';
		case WALLOS_KEY_NUM4: return shift ? '$' : '4';
		case WALLOS_KEY_NUM5: return shift ? '%' : '5';
		case WALLOS_KEY_NUM6: return shift ? '^' : '6';
		case WALLOS_KEY_NUM7: return shift ? '&' : '7';
		case WALLOS_KEY_NUM8: return shift ? '*' : '8';
		case WALLOS_KEY_NUM9: return shift ? '(' : '9';

		// Punctuation / special chars
		case WALLOS_KEY_MINUS:        return shift ? '_' : '-';
		case WALLOS_KEY_EQUALS:       return shift ? '+' : '=';
		case WALLOS_KEY_LEFTBRACKET:  return shift ? '{' : '[';
		case WALLOS_KEY_RIGHTBRACKET: return shift ? '}' : ']';
		case WALLOS_KEY_BACKSLASH:    return shift ? '|' : '\\';
		case WALLOS_KEY_SEMICOLON:    return shift ? ':' : ';';
		case WALLOS_KEY_APOSTROPHE:   return shift ? '"' : '\'';
		case WALLOS_KEY_TILDE:        return shift ? '~' : '`';
		case WALLOS_KEY_COMMA:        return shift ? '<' : ',';
		case WALLOS_KEY_PERIOD:       return shift ? '>' : '.';
		case WALLOS_KEY_SLASH:        return shift ? '?' : '/';

		// Whitespace / control chars
		case WALLOS_KEY_SPACE:        return ' ';
		case WALLOS_KEY_TAB:          return '\t';
		case WALLOS_KEY_ENTER: // fallthrough
		case WALLOS_KEY_NUMPAD_ENTER: return '\n';
		case WALLOS_KEY_BACKSPACE:    return '\b';
		case WALLOS_KEY_ESCAPE:       return '\x1b';
		case WALLOS_KEY_DELETE:       return '\x7f';

		// Keypad
		// shift/caps don't affect these
		case WALLOS_KEY_NUMPAD_0: return '0';
		case WALLOS_KEY_NUMPAD_1: return '1';
		case WALLOS_KEY_NUMPAD_2: return '2';
		case WALLOS_KEY_NUMPAD_3: return '3';
		case WALLOS_KEY_NUMPAD_4: return '4';
		case WALLOS_KEY_NUMPAD_5: return '5';
		case WALLOS_KEY_NUMPAD_6: return '6';
		case WALLOS_KEY_NUMPAD_7: return '7';
		case WALLOS_KEY_NUMPAD_8: return '8';
		case WALLOS_KEY_NUMPAD_9: return '9';
		case WALLOS_KEY_NUMPAD_DIVIDE:   return '/';
		case WALLOS_KEY_NUMPAD_MULTIPLY: return '*';
		case WALLOS_KEY_NUMPAD_MINUS:    return '-';
		case WALLOS_KEY_NUMPAD_PLUS:     return '+';
		case WALLOS_KEY_NUMPAD_DECIMAL:  return '.';

		// Everything else has no text representation
		default: return 0;
	}
}

// ------------------------------------------------------------------------------------------------
// getc / getc_nonblocking / gets
// ------------------------------------------------------------------------------------------------

char input_getc(void) {
	wallos_input_event_t event;

	while (true) {
		if (!input_wait_event(WALLOS_INPUT_DEVICE_KEYBOARD, &event)) {
			// input_wait_event is blocking, so this shouldn't normally happen
			// We don't want to spin silently forever on a malformed/missing event though
			continue;
		}

		// Only presses/repeats produce text, key-up doesn't
		if (event.data.keyboard.state == WALLOS_INPUT_STATE_RELEASED) {
			continue;
		}

		uint8_t c = wallos_key_to_cp437(event.data.keyboard.key, event.data.keyboard.modifiers);
		if (c != 0) {
			return (char) c;
		}

		// Anything that is non-text we just ignore for now.
	}
}

char input_getc_nonblocking(void) {
	wallos_input_event_t event;

	while (input_poll_event(WALLOS_INPUT_DEVICE_KEYBOARD, &event)) {
		if (event.data.keyboard.state == WALLOS_INPUT_STATE_RELEASED) {
			continue;
		}

		uint8_t c = wallos_key_to_cp437(event.data.keyboard.key, event.data.keyboard.modifiers);
		if (c != 0) {
			return (char) c;
		}

		// Anything that is non-text we just ignore for now.
	}

	return -1; // nothing text-worthy queued right now, everything that uses non-blocking assumes -1 if nothing happened
}

char* input_gets(char* out, size_t out_size) {
	if (out == NULL || out_size == 0) {
		return out;
	}

	size_t len = 0;
	while (true) {
		char c = input_getc(); // blocking

		if (c == '\n') {
			break;
		}

		if (c == '\b') {
			if (len > 0) {
				len--;
			}
			continue;
		}

		if (len + 1 < out_size) { // always leave room for the null terminator
			out[len++] = c;
		}
		// Buffer's full 
		// We keep consuming input (so backspace/enter still work) but drop further printable chars instead of overflowing.
	}

	out[len] = '\0';
	return out;
}