#include <string.h>
size_t strlen(const char* str) {
	if (str == NULL) return 0;
	size_t len = 0;
	while (str[len])
		len++;
	return len;
}

size_t strnlen(const char* str, size_t max) {
	const char* str1;

	for (str1 = str; max-- && *str1; str1++);

	return str1 - str;
}