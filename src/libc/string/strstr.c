#include <string.h>

char* strstr(const char* str, const char* sub) {
	size_t i, j;

	if (*sub == '\0') return (char*) str;
	for (i = 0; str[i] != '\0'; i++) {
		for (j = 0; sub[j] != '\0' && str[i + j] == sub[j]; j++);
		if (sub[j] == '\0') return (char*) &str[i];
		if (str[i + j] == '\0') break;
	}
	return NULL;
}