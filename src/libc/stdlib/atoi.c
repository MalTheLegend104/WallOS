#include <stdbool.h>
#include <stdlib.h>

int atoi(const char* str) {
	int result = 0;
	bool negative = false;

	// Handle optional sign
	if (*str == '-') {
		negative = true;
		str++;
	} else if (*str == '+') {
		str++;
	}

	// Convert the digits to integer
	while (*str >= '0' && *str <= '9') {
		result = result * 10 + (*str - '0');
		str++;
	}

	// Apply the sign
	return negative ? -result : result;
}