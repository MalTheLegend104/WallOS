#include <string.h>
int strcmp(const char* str1, const char* str2) {
	while (*str1 && (*str1 == *str2)) {
		str1++;
		str2++;
	}

	return *(unsigned char*) str1 - *(unsigned char*) str2;
}

int strncmp(const char* str1, const char* str2, size_t n) {
	while (n && *str1 && (*str1 == *str2)) {
		str1++;
		str2++;
		n--;
	}

	if (n == 0) {
		return 0;
	} else {
		return *(unsigned char*) str1 - *(unsigned char*) str2;
	}
}

int memcmp(const void* s1, const void* s2, size_t n) {
	const unsigned char* p1 = s1;
	const unsigned char* p2 = s2;

	while (n--) {
		if (*p1 != *p2) {
			return *p1 - *p2;
		}
		p1++;
		p2++;
	}
	return 0;
}

int strcasecmp(const char* s1, const char* s2) {
	const unsigned char* p1 = (const unsigned char*) s1;
	const unsigned char* p2 = (const unsigned char*) s2;
	int c1, c2;

	do {
		c1 = *p1++;
		c2 = *p2++;

		/* Convert uppercase to lowercase using ASCII offset */
		if (c1 >= 'A' && c1 <= 'Z') {
			c1 += ('a' - 'A'); // +32
		}
		if (c2 >= 'A' && c2 <= 'Z') {
			c2 += ('a' - 'A'); // +32
		}

		/* If we reached the end of the strings, or characters differ */
	} while (c1 == c2 && c1 != '\0');

	return c1 - c2;
}