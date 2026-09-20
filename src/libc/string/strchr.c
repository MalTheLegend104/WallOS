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

char* strrchr(const char* s, int c) {
	if (s == NULL) return 0;
	const unsigned char* p = (const unsigned char*) s;
	unsigned char ch = (unsigned char) c;

	const char* last = NULL;

	while (*p) {
		if (*p == ch) {
			last = (const char*) p;
		}
		p++;
	}

	// Check terminating null if c == '\0'
	if (ch == '\0') {
		return (char*) p;
	}

	return (char*) last;
}