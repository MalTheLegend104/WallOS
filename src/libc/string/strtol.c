#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define LONG_MAX ((long)(~0UL>>1))
#define LONG_MIN (~LONG_MAX)

long strtol(const char* restrict nptr, char** restrict endptr, int base) {
	const char* p = nptr, * endp;
	_Bool is_neg = 0, overflow = 0;
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
