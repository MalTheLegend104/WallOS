#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

// Standard I/O Port Addresses
#define COM1 0x3f8
#define COM2 0x2f8
#define COM3 0x3e8
#define COM4 0x2e8

// Helper for port offsets to make the code readable
#define REG_DATA(p)          (p + 0)
#define REG_IER(p)           (p + 1)
#define REG_IIR_FCR(p)       (p + 2)
#define REG_LCR(p)           (p + 3)
#define REG_MCR(p)           (p + 4)
#define REG_LSR(p)           (p + 5)

	void init_all_serial();

	int serial_cli_cmd(int argc, char** argv);

	int init_serial(uint16_t base_port);
	int serial_received(uint16_t base_port);
	char read_serial(uint16_t base_port);
	int is_transmit_empty(uint16_t base_port);
	void write_serial(char a);
	void write_string_serial(char* str);

	int printf_serial(const char* __restrict format, ...);
	int vprintf_serial(const char* __restrict format, va_list list);

#ifdef __cplusplus
}
#endif
#endif // SERIAL_H