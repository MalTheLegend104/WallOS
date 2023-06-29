#include <panic.h>
#include <klibc/kprint.h>

/**
 * @brief Kernel Panic! You should know what this does.
 */
void panic() {
	panic_i(GENERAL_PURPOSE_PANIC);
}

/**
 * @brief Kernel Panic! You should know what this does.
 *
 * @param buf String to be printed on the crash screen.
 */
void panic_s(const char* buf) {
	pink_screen(buf);
	__asm volatile ("hlt");
}

/**
 * @brief Kernel Panic! You should know what this does.
 *
 * @param panic KERNEL_PANIC of the panic, this will be printed on the crash screen.
 */
void panic_i(uint8_t panic) {
	switch (panic) {
		case GENERAL_PURPOSE_PANIC: panic_s("General Purpose Error."); break;
		default: panic_s("General Purpose Error."); break;
	}
}
