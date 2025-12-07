#include <string.h>

char* strchr(const char* str, int ch) {
	if (str == NULL) return NULL;

	// Convert ch to unsigned char to avoid sign issues
	unsigned char target = (unsigned char) ch;

	// Iterate through the string
	while (*str != '\0') {
		if ((unsigned char) *str == target) {
			return (char*) str; // Cast away const for compatibility
		}
		str++;
	}

	// If searching for '\0', return pointer to string terminator
	if (target == '\0') {
		return (char*) str;
	}

	return NULL;
}