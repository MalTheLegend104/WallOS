#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define LONG_MAX ((long)(~0UL>>1))
#define LONG_MIN (~LONG_MAX)

long strtol(const char* restrict nptr, char** restrict endptr, int base) {
	if (nptr == NULL) return 0;
	const char* p = nptr, * endp;
	bool is_neg = 0, overflow = 0;
	/* Need unsigned so (-LONG_MIN) can fit in these: */
	unsigned long n = 0UL, cutoff;
	int cutlim;
	if (base < 0 || base == 1 || base > 36) {
		return 0L;
	}
	endp = nptr;
	while (*p == ' ')
		p++;
	if (*p == '+') {
		p++;
	} else if (*p == '-') {
		is_neg = 1, p++;
	}
	if (*p == '0') {
		p++;
		/* For strtol(" 0xZ", &endptr, 16), endptr should point to 'x';
		 * pointing to ' ' or '0' is non-compliant.
		 * (Many implementations do this wrong.) */
		endp = p;
		if (base == 16 && (*p == 'X' || *p == 'x')) {
			p++;
		} else if (base == 0) {
			if (*p == 'X' || *p == 'x') {
				base = 16, p++;
			} else {
				base = 8;
			}
		}
	} else if (base == 0) {
		base = 10;
	}
	cutoff = (is_neg) ? -(LONG_MIN / base) : LONG_MAX / base;
	cutlim = (is_neg) ? -(LONG_MIN % base) : LONG_MAX % base;
	while (1) {
		int c;
		if (*p >= 'A')
			c = ((*p - 'A') & (~('a' ^ 'A'))) + 10;
		else if (*p <= '9')
			c = *p - '0';
		else
			break;
		if (c < 0 || c >= base) break;
		endp = ++p;
		if (overflow) {
			/* endptr should go forward and point to the non-digit character
			 * (of the given base); required by ANSI standard. */
			if (endptr) continue;
			break;
		}
		if (n > cutoff || (n == cutoff && c > cutlim)) {
			overflow = 1; continue;
		}
		n = n * base + c;
	}
	if (endptr) *endptr = (char*) endp;
	if (overflow) {
		return ((is_neg) ? LONG_MIN : LONG_MAX);
	}
	return (long) ((is_neg) ? -n : n);
}

#undef LONG_MAX 
#undef LONG_MIN 


#define LLONG_MAX ((long long)(~0ULL>>1))
#define LLONG_MIN (~LLONG_MAX)
long long strtoll(const char* restrict nptr, char** restrict endptr, int base) {
	if (nptr == NULL) return 0;
	const char* p = nptr, * endp;
	bool is_neg = 0, overflow = 0;
	/* Need unsigned so (-LLONG_MIN) can fit in these: */
	unsigned long long n = 0ULL, cutoff;
	int cutlim;
	if (base < 0 || base == 1 || base > 36) {
		return 0LL;
	}
	endp = nptr;
	while (*p == ' ')
		p++;
	if (*p == '+') {
		p++;
	} else if (*p == '-') {
		is_neg = 1, p++;
	}
	if (*p == '0') {
		p++;
		/* For strtoll(" 0xZ", &endptr, 16), endptr should point to 'x';
		 * pointing to ' ' or '0' is non-compliant.
		 * (Many implementations do this wrong.) */
		endp = p;
		if (base == 16 && (*p == 'X' || *p == 'x')) {
			p++;
		} else if (base == 0) {
			if (*p == 'X' || *p == 'x') {
				base = 16, p++;
			} else {
				base = 8;
			}
		}
	} else if (base == 0) {
		base = 10;
	}
	cutoff = (is_neg) ? -(LLONG_MIN / base) : LLONG_MAX / base;
	cutlim = (is_neg) ? -(LLONG_MIN % base) : LLONG_MAX % base;
	while (1) {
		int c;
		if (*p >= 'A')
			c = ((*p - 'A') & (~('a' ^ 'A'))) + 10;
		else if (*p <= '9')
			c = *p - '0';
		else
			break;
		if (c < 0 || c >= base) break;
		endp = ++p;
		if (overflow) {
			/* endptr should go forward and point to the non-digit character
			 * (of the given base); required by ANSI standard. */
			if (endptr) continue;
			break;
		}
		if (n > cutoff || (n == cutoff && c > cutlim)) {
			overflow = 1; continue;
		}
		n = n * base + c;
	}
	if (endptr) *endptr = (char*) endp;
	if (overflow) {
		return ((is_neg) ? LLONG_MIN : LLONG_MAX);
	}
	return (long long) ((is_neg) ? -n : n);
}
#undef LLONG_MAX
#undef LLONG_MIN

#define ULONG_MAX (~0UL)
unsigned long strtoul(const char* restrict nptr, char** restrict endptr, int base) {
	if (nptr == NULL) return 0;
	const char* p = nptr, * endp;
	bool overflow = 0, is_neg = 0;
	unsigned long n = 0UL, cutoff;
	int cutlim;
	if (base < 0 || base == 1 || base > 36) {
		return 0UL;
	}
	endp = nptr;
	while (*p == ' ')
		p++;
	/* Handle optional sign; per C standard, if '-' is present, the parsed value is converted to unsigned and negated (-n). */
	if (*p == '+') {
		p++;
	} else if (*p == '-') {
		is_neg = 1;
		p++;
	}
	if (*p == '0') {
		p++;
		endp = p;
		if (base == 16 && (*p == 'X' || *p == 'x')) {
			p++;
		} else if (base == 0) {
			if (*p == 'X' || *p == 'x') {
				base = 16, p++;
			} else {
				base = 8;
			}
		}
	} else if (base == 0) {
		base = 10;
	}
	cutoff = ULONG_MAX / base;
	cutlim = ULONG_MAX % base;
	while (1) {
		int c;
		if (*p >= 'A')
			c = ((*p - 'A') & (~('a' ^ 'A'))) + 10;
		else if (*p <= '9')
			c = *p - '0';
		else
			break;
		if (c < 0 || c >= base) break;
		endp = ++p;
		if (overflow) {
			if (endptr) continue;
			break;
		}
		if (n > cutoff || (n == cutoff && c > cutlim)) {
			overflow = 1; continue;
		}
		n = n * base + c;
	}
	if (endptr) *endptr = (char*) endp;
	if (overflow) {
		return ULONG_MAX;
	}
	return is_neg ? -n : n;
}
#undef ULONG_MAX

#define ULLONG_MAX (~0ULL)
unsigned long long strtoull(const char* restrict nptr, char** restrict endptr, int base) {
	if (nptr == NULL) return 0;
	const char* p = nptr, * endp;
	bool overflow = 0;
	unsigned long long n = 0ULL, cutoff;
	int cutlim;
	if (base < 0 || base == 1 || base > 36) {
		return 0ULL;
	}
	endp = nptr;
	while (*p == ' ')
		p++;
	/* Ignore a leading '+'; consuming a '-' is defined behavior for strtoull,
	 * the result is just the negation of the parsed value as unsigned. */
	if (*p == '+') {
		p++;
	} else if (*p == '-') {
		p++;
	}
	if (*p == '0') {
		p++;
		/* For strtoull(" 0xZ", &endptr, 16), endptr should point to 'x';
		 * pointing to ' ' or '0' is non-compliant.
		 * (Many implementations do this wrong.) */
		endp = p;
		if (base == 16 && (*p == 'X' || *p == 'x')) {
			p++;
		} else if (base == 0) {
			if (*p == 'X' || *p == 'x') {
				base = 16, p++;
			} else {
				base = 8;
			}
		}
	} else if (base == 0) {
		base = 10;
	}
	cutoff = ULLONG_MAX / base;
	cutlim = ULLONG_MAX % base;
	while (1) {
		int c;
		if (*p >= 'A')
			c = ((*p - 'A') & (~('a' ^ 'A'))) + 10;
		else if (*p <= '9')
			c = *p - '0';
		else
			break;
		if (c < 0 || c >= base) break;
		endp = ++p;
		if (overflow) {
			/* endptr should go forward and point to the non-digit character
			 * (of the given base); required by ANSI standard. */
			if (endptr) continue;
			break;
		}
		if (n > cutoff || (n == cutoff && c > cutlim)) {
			overflow = 1; continue;
		}
		n = n * base + c;
	}
	if (endptr) *endptr = (char*) endp;
	if (overflow) {
		return ULLONG_MAX;
	}
	return n;
}
#undef ULLONG_MAX