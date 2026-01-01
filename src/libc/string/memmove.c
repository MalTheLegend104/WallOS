#include <stddef.h>

void* memmove(void* dest, const void* src, size_t n) {
	unsigned char* d = (unsigned char*) dest;
	const unsigned char* s = (const unsigned char*) src;

	if (d == s || n == 0) {
		return dest;
	}

	// Check if regions overlap and src comes before dest
	if (d > s && d < s + n) {
		// Copy backwards to avoid overwriting source data
		d += n;
		s += n;
		while (n--) {
			*--d = *--s;
		}
	} else {
		// Copy forwards (either no overlap or dest comes before src)
		while (n--) {
			*d++ = *s++;
		}
	}

	return dest;
}