#include <string.h>
#include <stddef.h>
#include <stdbool.h>

float strtof(const char* restrict nptr, char** restrict endptr) {
	if (nptr == NULL) return 0.0f;
	const char* p = nptr;
	bool is_neg = false;

	while (*p == ' ') p++;

	if (*p == '+') {
		p++;
	} else if (*p == '-') {
		is_neg = true;
		p++;
	}

	const char* start_digits = p;
	float result = 0.0f;

	// Parse integer part
	while (*p >= '0' && *p <= '9') {
		result = result * 10.0f + (*p - '0');
		p++;
	}

	// Parse fractional part
	if (*p == '.') {
		p++;
		float factor = 0.1f;
		while (*p >= '0' && *p <= '9') {
			result += (*p - '0') * factor;
			factor *= 0.1f;
			p++;
		}
	}

	// If no digits were processed, parsing failed
	if (p == start_digits || (p == start_digits + 1 && *(p - 1) == '.')) {
		if (endptr) *endptr = (char*) nptr;
		return 0.0f;
	}

	// Parse exponent part (e.g., e-5, E+10)
	if (*p == 'e' || *p == 'E') {
		const char* exp_start = p;
		p++;
		bool exp_neg = false;
		if (*p == '+') {
			p++;
		} else if (*p == '-') {
			exp_neg = true;
			p++;
		}

		if (*p >= '0' && *p <= '9') {
			int exp = 0;
			while (*p >= '0' && *p <= '9') {
				exp = exp * 10 + (*p - '0');
				p++;
			}
			float base = 10.0f;
			float scale = 1.0f;
			while (exp > 0) {
				if (exp & 1) scale *= base;
				base *= base;
				exp >>= 1;
			}
			result = exp_neg ? (result / scale) : (result * scale);
		} else {
			// Invalid exponent string (e.g., "123eABC"), roll back 'e'
			p = exp_start;
		}
	}

	if (endptr) *endptr = (char*) p;
	return is_neg ? -result : result;
}

double strtod(const char* restrict nptr, char** restrict endptr) {
	if (nptr == NULL) return 0.0;
	const char* p = nptr;
	bool is_neg = false;

	while (*p == ' ') p++;

	if (*p == '+') {
		p++;
	} else if (*p == '-') {
		is_neg = true;
		p++;
	}

	const char* start_digits = p;
	double result = 0.0;

	// Parse integer part
	while (*p >= '0' && *p <= '9') {
		result = result * 10.0 + (*p - '0');
		p++;
	}

	// Parse fractional part
	if (*p == '.') {
		p++;
		double factor = 0.1;
		while (*p >= '0' && *p <= '9') {
			result += (*p - '0') * factor;
			factor *= 0.1;
			p++;
		}
	}

	// If no digits were processed, parsing failed
	if (p == start_digits || (p == start_digits + 1 && *(p - 1) == '.')) {
		if (endptr) *endptr = (char*) nptr;
		return 0.0;
	}

	// Parse exponent part (e.g., e-5, E+10)
	if (*p == 'e' || *p == 'E') {
		const char* exp_start = p;
		p++;
		bool exp_neg = false;
		if (*p == '+') {
			p++;
		} else if (*p == '-') {
			exp_neg = true;
			p++;
		}

		if (*p >= '0' && *p <= '9') {
			int exp = 0;
			while (*p >= '0' && *p <= '9') {
				exp = exp * 10 + (*p - '0');
				p++;
			}
			double base = 10.0;
			double scale = 1.0;
			while (exp > 0) {
				if (exp & 1) scale *= base;
				base *= base;
				exp >>= 1;
			}
			result = exp_neg ? (result / scale) : (result * scale);
		} else {
			// Invalid exponent string (e.g., "123eABC"), roll back 'e'
			p = exp_start;
		}
	}

	if (endptr) *endptr = (char*) p;
	return is_neg ? -result : result;
}