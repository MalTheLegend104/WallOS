#ifndef PANIC_H
#define PANIC_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
	// Common Kernel Panics
	enum KERNEL_PANIC {
		GENERAL_PURPOSE_PANIC,
		WHAT_JUST_HAPPEN_PANIC
	};

	void panic();
	void panic_s(const char* buf);
	void panic_sa(const char** buf, uint8_t length);
	void panic_i(uint8_t panic);
#ifdef __cplusplus
}
#endif

#endif //PANIC_H