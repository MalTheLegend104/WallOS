#ifndef WALLOS_INPUT_TEXT_H
#define WALLOS_INPUT_TEXT_H

#include <stddef.h>
#include <stdint.h>
#include <input/input_handler.h>

#ifdef __cplusplus
extern "C" {
#endif

	/**
	 * @brief Translates a single key + modifier bitmask into a code page 437 character.
	 * This returns 0 if the key has no text representation.
	 *
	 * @param key Key to get the character for
	 * @param modifiers Modifiers (wallos_modifier_flags_t)
	 * @return uint8_t Code Page 437 character, or 0 if there is none.
	 */
	uint8_t wallos_key_to_cp437(wallos_key_t key, uint32_t modifiers);

	/**
	 * @brief Blocks until a key event with a text representation occurs, and returns that character.
	 *
	 * @return char Char that was pressed
	 */
	char input_getc(void);

	/**
	 * @brief Non-blocking version of input_getc().
	 *
	 * @note Drains queued keyboard events, discarding any that don't map to text, and returns the first one that does.
	 *
	 * @return char Returns -1 if no text character is available right now
	 */
	char input_getc_nonblocking(void);

	/**
	 * @brief Blocking line read. Reads characters until Enter is pressed or `out_size - 1` characters have been collected.
	 * Returned string is null terminated. Backspace will delete the last collected character rather than being part of the string.
	 * Does not echo.
	 *
	 * @param out Buffer to store the string into.
	 * @param out_size Size of buffer. Must be at least 2. (1 char + null terminator). Will return NULL otherwise.
	 * @return char* Same char* as out, unless `out` or `out_size` were invalid.
	 */
	char* input_gets(char* out, size_t out_size);

#ifdef __cplusplus
}
#endif
#endif // WALLOS_INPUT_TEXT_H