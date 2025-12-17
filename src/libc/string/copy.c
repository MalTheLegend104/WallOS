#include <string.h>

// void* __restrict, const void* __restrict, size_t
void* memcpy(void* restrict dstptr, const void* restrict srcptr, size_t size) {
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

	// Copy character by character, including the null terminator
	do {
		dest[index] = src[index];
		index++;
	} while (src[index - 1] != '\0');

	return dest;
}


char* strncpy(char* dest, const char* src, size_t n) {
	if (dest == NULL || src == NULL || n == 0) {
		return dest;
	}

	for (size_t i = 0; i < n; i++) {
		dest[i] = src[i];
		if (src[i] == '\0') {
			for (size_t j = i; j < n; j++) {
				dest[j] = '\0';
			}
			break;
		}
	}

	return dest;
}

