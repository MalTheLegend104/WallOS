#include <string.h>
size_t strlen(const char* str) {
	if (str == NULL) return 0;
	size_t len = 0;
	while (str[len])
		len++;
	return len;
}
