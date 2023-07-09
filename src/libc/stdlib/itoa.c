#include <stdlib.h>
#include <string.h>

/* Function that converts an int to a string. */
char* itoa(long long value, char* buffer, int base) {
	long long i = 0, r, negative = 0;
	if (value == 0) {
		buffer[i] = '0';
		buffer[i + 1] = '\0';
		return buffer;
	}

	if (value < 0 && base == 10) {
		value *= -1;
		negative = 1;
	}

	while (value != 0) {
		r = value % base;
		buffer[i] = (r > 9) ? (r - 10) + 'a' : r + '0';
		i++;
		value /= base;
	}

	if (negative) {
		buffer[i] = '-';
		i++;
	}

	strrev(buffer, 0, i - 1);

	buffer[i] = '\0';

	return buffer;
}