#ifndef PANIC_H
#define PANIC_H
#include <stdint.h>
#ifdef __is_kernel_
#ifdef __cplusplus
extern "C" {
#endif
	// Common Kernel Panics
	enum KERNEL_PANIC {
		GENERAL_PURPOSE_PANIC,

	};

	void panic();
	void panic_s(const char* buf);
	void panic_i(uint8_t panic);
#ifdef __cplusplus
}
#endif
#endif // __is_kernel_
#endif //PANIC_H