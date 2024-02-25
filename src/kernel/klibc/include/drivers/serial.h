#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	int init_serial();
	int serial_received();
	char read_serial();
	int is_transmit_empty();
	void write_serial(char a);
	void write_string_serial(char* str);

	int printf_serial(const char* format, ...);
	int vprintf_serial(const char* format, va_list arg);

#ifdef __cplusplus
}
#endif
#endif // SERIAL_H