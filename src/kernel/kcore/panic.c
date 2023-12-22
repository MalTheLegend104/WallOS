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
	asm volatile("cli");
	pink_screen(buf);
	asm volatile ("hlt");
}

void panic_sa(const char** buf, uint8_t length) {
	asm volatile("cli");
	pink_screen_sa(buf, length);
	asm volatile ("hlt");
}

/**
 * @brief Kernel Panic! You should know what this does.
 *
 * @param panic KERNEL_PANIC of the panic, this will be printed on the crash screen.
 */
void panic_i(uint8_t panic) {
	switch (panic) {
		case GENERAL_PURPOSE_PANIC: panic_s("General Purpose Error."); break;
		case WHAT_JUST_HAPPEN_PANIC: panic_s("your computor have virus"); break;
		default: panic_s("General Purpose Error."); break;
	}
}
