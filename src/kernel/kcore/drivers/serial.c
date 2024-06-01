#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include <drivers/serial.h>
#include <klibc/kprint.h>
#define PORT 0x3f8          // COM1

int init_serial() {
	outb(PORT + 1, 0x00);    // Disable all interrupts
	outb(PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
	outb(PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
	outb(PORT + 1, 0x00);    //                  (hi byte)
	outb(PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
	outb(PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
	outb(PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
	outb(PORT + 4, 0x1E);    // Set in loopback mode, test the serial chip
	outb(PORT + 0, 0xAE);    // Test serial chip (send byte 0xAE and check if serial returns same byte)

	// Check if serial is faulty (i.e: not same byte as sent)
	if (inb(PORT + 0) != 0xAE) {
		return 1;
	}

	// If serial is not faulty set it in normal operation mode
	// (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
	outb(PORT + 4, 0x0F);
	return 0;
}

int serial_received() {
	return inb(PORT + 5) & 1;
}

char read_serial() {
	while (serial_received() == 0);

	return inb(PORT);
}

int is_transmit_empty() {
	return inb(PORT + 5) & 0x20;
}

void write_serial(char a) {
	while (is_transmit_empty() == 0);

	outb(PORT, a);
}

void write_string_serial(char* str) {
	for (size_t i = 0; i < strlen(str); i++) {
		write_serial(str[i]);
	}
}

#include <stdarg.h>
#define UINT_BUF_SIZE 11
#define INT_BUF_SIZE 12
#define LL_BUF_SIZE 27
#define ULL_BUF_SIZE LL_BUF_SIZE
#define FLOAT_BUF_SIZE 49

int print_until_null_serial(const char* data) {
	int amount = 0;
	while (*data != '\0') {
		write_serial(*data);
		data++;
		amount++;
	}
	return amount;
}

extern char* convert_i(int num, int base);
extern char* convert_ui(unsigned int num, int base);
extern char* convert_ll(long long num, int base);
extern char* convert_ull(unsigned long long num, int base);

int vprintf_serial(const char* format, va_list arg) {
	const char* traverse = format;
	int ret = 0;
	while (*traverse != '\0') {
		if (*traverse == '%') {
			traverse++;
			switch (*traverse) {
				case 'c': {
						signed int i = va_arg(arg, int);
						ret += print_until_null_serial((const char*) &i);
						break;
					}
				case 'd': {
						ret += print_until_null_serial(convert_i(va_arg(arg, int), 10));
						break;
					}
				case 'f': {
						char buf[FLOAT_BUF_SIZE];
						ftoa(va_arg(arg, double), buf, 10);
						ret += print_until_null_serial(buf);
						break;
					}
				case 'u':
				case 'i': {
						ret += print_until_null_serial(convert_ui(va_arg(arg, unsigned int), 10));
						break;
					}
				case 'o': {
						ret += print_until_null_serial(convert_ui(va_arg(arg, unsigned int), 8));
						break;
					}
				case 's': {
						ret += print_until_null_serial(va_arg(arg, char*));
						break;
					}
				case 'x':
				case 'X': {
						ret += print_until_null_serial(convert_ui(va_arg(arg, unsigned long long), 16));
						break;
					}
				case 'n': {
						int* b = va_arg(arg, int*);
						*b = ret;
						break;
					}
				case '%': {
						ret += print_until_null_serial("%\0");
						break;
					}
				case 'l': {
						traverse++;
						switch (*traverse) {
							case 'i':
							case 'd': {
									ret += print_until_null_serial(convert_i(va_arg(arg, long), 10));
									break;
								}
							case 'f': {
									char buf[FLOAT_BUF_SIZE];
									ftoa(va_arg(arg, long double), buf, 10);
									ret += print_until_null_serial(buf);
									break;
								}
							case 'u': {
									ret += print_until_null_serial(convert_ui(va_arg(arg, unsigned int), 10));
									break;
								}
							case 'l': {
									traverse++;
									switch (*traverse) {
										case 'i':
										case 'd': {
												ret += print_until_null_serial(convert_ll(va_arg(arg, long long), 10));
												break;
											}
										case 'u': {
												ret += print_until_null_serial(convert_ull(va_arg(arg, unsigned long long), 10));
												break;
											}
										case 'x':
										case 'X': {
												ret += print_until_null_serial(convert_ull(va_arg(arg, unsigned long long), 16));
												break;
											}
										default: break;
									}
									break;
								}
							default: break;
						}
						break;
					}
				case 'L': {
						traverse++;
						if (*traverse == 'f') {
							char buf[FLOAT_BUF_SIZE];
							ftoa(va_arg(arg, long double), buf, 10);
							ret += print_until_null_serial(buf);
							break;
						}
						break;
					}
				default: break;
			}
		} else if (*traverse == '\t') {
			ret += print_until_null_serial("    \0");
		} else {
			write_serial(*traverse);
			ret++;
		}
		traverse++;
	}
	return ret;
}

int printf_serial(const char* format, ...) {
	va_list arg;
	int ret;
	va_start(arg, format);
	ret = vprintf_serial(format, arg);
	//ret = temp(format, arg);
	va_end(arg);
	return ret;
}