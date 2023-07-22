#include <string.h>
#include <stdarg.h>
// TODO implement a va_args version of strcat
/*const char* strcat(const char* s1, const char* s2) {
	int s1size = strlen(s1);
	int s2size = strlen(s2);
	int size = (s1size + s2size) - 1;
	char buf[size];
	int currentIndex = 0;
	while (currentIndex < s1size - 2) { // we dont want the first \0
		buf[currentIndex] = s1[currentIndex];
		currentIndex++;
	}
	for (int i = currentIndex; i < s2size - 1; i++) {
		buf[i] = s2[i];
	}
	return &buf;
}*/

char* strcat(char* dest, const char* src) {
	char* rdest = dest;
	while (*dest)
		dest++;
	while ((*dest++ = *src++))
		;
	return rdest;
}

/**
 * @brief Concat a single char to the end of a string.
 *  If the string is already at max length (including room for '\0')
 *  then the string is returned unchanged.
 * @param string String to add the character to.
 * @param c Character to add to the string.
 * @param size Size of the entire buffer.
 * @return char* Same as the string passed through.
 */
char* strcat_c(char* string, char c, size_t size) {
	// Find the current length of the string
	size_t current_length = strlen(string);

	// If adding something would make the string too long, we return unchanged
	if (current_length + 1 >= size) return string;

	// Add the character to the end of the string
	string[current_length] = c;
	string[current_length + 1] = '\0';

	return string;
}