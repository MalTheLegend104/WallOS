#include <math.h>
#include <stdint.h>

/* DISCLAIMER:
 * This code is from mlibc:
 * https://github.com/managarm/mlibc/blob/master/options/ansi/musl-generic-math/modf.c
 *
 * I needed a modf function for doubles in printf
 * This file falls under MLIBC's LICENSE:
 * https://github.com/managarm/mlibc/blob/master/LICENSE
 */

double modf(double x, double* iptr) {
	union { double f; uint64_t i; } u = { x };
	uint64_t mask;
	int e = (int) (u.i >> 52 & 0x7ff) - 0x3ff;

	/* no fractional part */
	if (e >= 52) {
		*iptr = x;
		if (e == 0x400 && u.i << 12 != 0) /* nan */
			return x;
		u.i &= 1ULL << 63;
		return u.f;
	}

	/* no integral part*/
	if (e < 0) {
		u.i &= 1ULL << 63;
		*iptr = u.f;
		return x;
	}

	mask = -1ULL >> 12 >> e;
	if ((u.i & mask) == 0) {
		*iptr = x;
		u.i &= 1ULL << 63;
		return u.f;
	}
	u.i &= ~mask;
	*iptr = u.f;
	return x - u.f;
}