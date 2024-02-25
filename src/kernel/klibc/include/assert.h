#ifndef ASSERT_H
#define ASSERT_H
//include panic h
#include <panic.h>
// include string h
#include <string.h>
// include std int h
#include <stdint.h>
// include  stdlib h
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif

	inline void kernelAssertFailed(const char* msg, const char* file, int line) {
		asm volatile("cli");

		char f[80];
		f[0] = '\0';
		strcat(f, "File: ");
		strcat(f, file);

		char buf[16];
		buf[0] = '\0';
		char l[80];
		l[0] = '\0';
		strcat(l, "Line: ");
		strcat(l, itoa(line, buf, 10));
		const char* panic[] = { "Kernel Assertion Failed", msg, f, l };

		panic_sa(panic, 4);
	}

#define assert(expr) (void)((expr) || (kernelAssertFailed(#expr, __FILE__, __LINE__), 0))

#ifdef __cplusplus
}
#endif
#endif // ASSERT_H