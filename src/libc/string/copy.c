#include <string.h>

void* memcpy(void* dstptr, const void* srcptr, size_t size) {
	unsigned char* dst = (unsigned char*) dstptr;
	const unsigned char* src = (const unsigned char*) srcptr;
	for (size_t i = 0; i < size; i++)
		dst[i] = src[i];
	return dstptr;
}

char* strcpy(char* dest, const char* src) {
	if (dest == NULL || src == NULL) {
		return dest;
	}

	int index = 0;
	do {
		dest[index] = src[index];
	} while (src[index] != '\0');

	return dest;
}


char* strncpy(char* dest, const char* src, size_t n) {
	if (dest == NULL || src == NULL || n == 0) {
		return dest;
	}

	for (int i = 0; i < n; i++) {
		dest[i] = src[i];
		if (src[i] == '\0') {
			for (int j = i; j < n; j++) {
				dest[j] = '\0';
			}
			break;
		}
	}

	return dest;
}

