#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include <stdbool.h>

#include <drivers/serial.h>
#include <klibc/kprint.h>
#include <cpu_io.h>


// Structure to track which ports actually exist
typedef struct {
	uint16_t base;
	bool present;
} serial_port_t;

static serial_port_t active_ports[] = {
	{COM1, false},
	{COM2, false},
	{COM3, false},
	{COM4, false}
};

#define PORT_COUNT (sizeof(active_ports) / sizeof(active_ports[0]))

int init_serial(uint16_t base_port) {
	outb(REG_IER(base_port), 0x00);    // Disable all interrupts
	outb(REG_LCR(base_port), 0x80);    // Enable DLAB (set baud rate divisor)
	outb(REG_DATA(base_port), 0x03);   // Set divisor to 3 (lo byte) 38400 baud
	outb(REG_IER(base_port), 0x00);    //                  (hi byte)
	outb(REG_LCR(base_port), 0x03);    // 8 bits, no parity, one stop bit
	outb(REG_IIR_FCR(base_port), 0xC7);// Enable FIFO, clear them, 14-byte threshold
	outb(REG_MCR(base_port), 0x0B);    // IRQs enabled, RTS/DSR set

	// Loopback test
	outb(REG_MCR(base_port), 0x1E);    // Set in loopback mode
	outb(REG_DATA(base_port), 0xAE);   // Send test byte

	if (inb(REG_DATA(base_port)) != 0xAE) {
		return 1; // Faulty hardware or incorrect port
	}

	// Normal operation mode
	outb(REG_MCR(base_port), 0x0F);
	return 0;
}

bool detect_uart(uint16_t port) {
	// We use the scratch register (port + 7 offset) to see if a write "sticks"
	// If it doesn't, that COM doesn't exist.

	// We use both 0x55 and 0xAA to ensure we aren't just getting garbage back that *happens* to be correct.
	outb(port + 7, 0x55);
	if (inb(port + 7) != 0x55) return false;

	outb(port + 7, 0xAA);
	if (inb(port + 7) != 0xAA) return false;

	return true;
}

void init_all_serial() {
	for (int i = 0; i < PORT_COUNT; i++) {
		if (detect_uart(active_ports[i].base)) {
			init_serial(active_ports[i].base);
			active_ports[i].present = true;
		}
	}
}

int serial_received(uint16_t base_port) {
	return inb(REG_LSR(base_port)) & 1;
}

char read_serial(uint16_t base_port) {
	while (serial_received(base_port) == 0);
	return inb(REG_DATA(base_port));
}

int is_transmit_empty(uint16_t base_port) {
	return inb(REG_LSR(base_port)) & 0x20;
}

void write_serial_mirrored(char a) {
	for (int i = 0; i < PORT_COUNT; i++) {
		if (!active_ports[i].present) continue;

		uint16_t port = active_ports[i].base;
		// Wait for Transmit Holding Register Empty (THRE)
		while ((inb(port + 5) & 0x20) == 0);
		outb(port, a);
	}
}

void write_string_serial_mirrored(const char* str) {
	for (size_t i = 0; str[i] != '\0'; i++) {
		write_serial_mirrored(str[i]);
	}
}

void write_serial(char a) {
	write_serial_mirrored(a);
}

void write_string_serial(char* str) {
	write_string_serial_mirrored(str);
}


void write_serial_port(uint16_t base_port, char a) {
	while (is_transmit_empty(base_port) == 0);
	outb(REG_DATA(base_port), a);
}

void write_string_serial_port(uint16_t base_port, const char* str) {
	for (size_t i = 0; str[i] != '\0'; i++) {
		write_serial_port(base_port, str[i]);
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Serial CLI command
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static int cmd_serial_status() {
	bool any = false;
	for (int i = 0; i < PORT_COUNT; i++) {
		if (!active_ports[i].present) continue;
		printf("  COM%d @ 0x%X [active]\n", i + 1, active_ports[i].base);
		any = true;
	}
	if (!any)
		printf("  No active serial ports detected.\n");
	return 0;
}

static int cmd_serial_init(const char* addr_str) {
	char* end;
	uint16_t addr = (uint16_t) strtol(addr_str, &end, 16);
	if (*end != '\0' || addr == 0) {
		printf("serial: invalid address\n");
		return 1;
	}

	if (!detect_uart(addr)) {
		printf("serial: no UART detected at address\n");
		return 1;
	}

	if (init_serial(addr) != 0) {
		printf("serial: init failed (loopback test)\n");
		return 1;
	}

	for (int i = 0; i < PORT_COUNT; i++) {
		if (active_ports[i].base == addr) {
			active_ports[i].present = true;
			printf("serial: port re-initialised\n");
			return 0;
		}
	}
	for (int i = 0; i < PORT_COUNT; i++) {
		if (!active_ports[i].present) {
			active_ports[i].base = addr;
			active_ports[i].present = true;
			printf("serial: port initialised and registered\n");
			return 0;
		}
	}

	printf("serial: port initialised (tracking table full)\n");
	return 0;
}

static int cmd_serial_send(const char* target, const char* msg) {
	if (strcmp(target, "all") == 0) {
		printf("%s", msg);  // Print to console for "all"
		return 0;
	}

	char* end;
	uint16_t addr = (uint16_t) strtol(target, &end, 16);
	if (*end != '\0' || addr == 0) {
		printf("serial: invalid address\n");
		return 1;
	}

	for (int i = 0; i < PORT_COUNT; i++) {
		if (active_ports[i].present && active_ports[i].base == addr) {
			write_string_serial_port(addr, msg);  // Hardware send
			return 0;
		}
	}

	printf("serial: port not active\n");
	return 1;
}

int serial_cli_cmd(int argc, char** argv) {
	if (argc < 2) {
		printf(
			"Usage:\n"
			"  serial status\n"
			"  serial init <addr>\n"
			"  serial send <addr|all> <msg>\n"
		);
		return 1;
	}

	if (strcmp(argv[1], "status") == 0) {
		return cmd_serial_status();
	}

	if (strcmp(argv[1], "init") == 0) {
		if (argc < 3) {
			printf("serial: init requires <addr>\n");
			return 1;
		}
		return cmd_serial_init(argv[2]);
	}

	if (strcmp(argv[1], "send") == 0) {
		if (argc < 4) {
			printf("serial: send requires <addr|all> <msg>\n");
			return 1;
		}
		return cmd_serial_send(argv[2], argv[3]);
	}

	printf("serial: unknown subcommand\n");
	return 1;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Everything below here is just a copy from print.c
// I could probably make print.c actually do this for me, since the only changed thing is where
// it outputs to. im too lazy rn.
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <float.h>

typedef enum {
	TYPE_REGULAR,
	TYPE_SHORT_SHORT,
	TYPE_SHORT,
	TYPE_LONG,
	TYPE_LONG_LONG,
	TYPE_INTMAX_T,
	TYPE_SIZE_T,
	TYPE_LONG_DOUBLE,
	TYPE_PTRDIFF
} printf_modifier;

typedef enum {
	BASE_DECIMAL = 10,
	BASE_HEX = 16,
	BASE_OCTAL = 8
} base_type;

int print_string_serial(char* str, size_t precision, bool precision_specified, size_t field_width, bool left_justify) {
	int amount = 0;
	size_t len = strlen(str);

	if (!left_justify) {
		if (field_width && len < field_width) {
			for (size_t i = 0; i < field_width - len; i++) {
				write_serial(' ');
			}
		}
	}

	if (!precision_specified) {
		while (*str != '\0') {
			write_serial(*str);
			str++;
			amount++;
		}
	} else {
		for (size_t i = 0; i < precision; i++) {
			if (*str == '\0') break;
			write_serial(*str);
			str++;
			amount++;
		}
	}

	if (left_justify && field_width && amount < field_width) {
		for (size_t i = amount; i < field_width; i++) {
			write_serial(' ');
		}
	}

	return amount;
}

int print_wstring_serial(wchar_t* str, size_t precision, bool precision_specified) {
	int amount = 0;
	if (!precision_specified) {
		while (*str != '\0') {
			write_serial(*str);
			str++;
			amount++;
		}
	} else {
		for (size_t i = 0; i < precision; i++) {
			if (*str == '\0') break;
			write_serial(*str);
			str++;
			amount++;
		}
	}

	return amount;
}

size_t int_to_string_serial(intmax_t value, int base, char* buf, size_t buflen) {
	if (buflen == 0 || buflen == 1) return 0;
	if (base < 0 || base > 36) return 0;

	int negative = 0;
	size_t index = 0;

	char temp[24];

	if (value == 0) {
		buf[0] = '0';
		buf[1] = '\0';
		return 1;
	}

	bool int_min = value == INTMAX_MIN;

	// INTMAX_MIN is really annoying. It's hard to work with, so we're using a janky workaround.
	// We're going to take away 1, convert it like normal, then decrease the final result by 1.
	// This *should* work in all bases, but printf_serial only does base 10 for signed ints.
	if (int_min) {
		value += 1;
	}

	if (value < 0 && base == 10) {
		value = -value;
		negative = 1;
	}

	const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

	while (value != 0 && index < (buflen - (1 + negative))) {
		temp[index] = digits[value % base];
		index++;
		value /= base;
	}

	// Correct the last digit on int min
	if (int_min) {
		if (index == 0 || temp[0] == digits[base - 1]) {
			return 0; // This should never happen, I just want to be sure.
		}
		temp[0] += 1;
	}

	if (negative) {
		if (index >= buflen) return 0; // This should never happen, I just want to be sure.
		temp[index] = '-';
		index++;
	}

	for (size_t j = 0; j < index; ++j) {
		buf[j] = temp[index - j - 1];
	}
	buf[index] = '\0';

	return index;
}

size_t uint_to_string_serial(uintmax_t value, int base, char* buf, size_t buflen, bool capital) {
	if (buflen == 0 || buflen == 1) return 0;
	if (base < 2 || base > 36) return 0;

	size_t index = 0;
	char temp[24];

	if (value == 0) {
		if (buflen < 2) return 0;
		buf[0] = '0';
		buf[1] = '\0';
		return 1;
	}

	const char* digits;

	if (!capital) {
		digits = "0123456789abcdefghijklmnopqrstuvwxyz";
	} else {
		digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	}

	// Convert the number to the specified base
	while (value != 0 && index < (buflen - 1)) {
		temp[index++] = digits[value % base];
		value /= base;
	}

	// This should never happen. I still want to check.
	if (index >= buflen) return 0;

	// Reverse the string and copy it to the buffer
	for (size_t j = 0; j < index; ++j) {
		buf[j] = temp[index - j - 1];
	}
	buf[index] = '\0';

	return index;
}

void shift_right_serial(char* buf, size_t buflen) {
	for (size_t i = buflen - 1; i > 0; i--) {
		buf[i] = buf[i - 1];
	}
}

size_t print_signed_int_serial(intmax_t value, base_type base, size_t precision, size_t field_width, size_t padding, bool left_justified, bool prepend_space, bool prepend_sign) {
	size_t written = 0;

	if (precision > 19) precision = 19;
	if (padding > 19) padding = 19;
	if (field_width > 19) field_width = 19;

	// The max length of any possible value is 19 digits, with a signed 64 bit int. We add two to this, one for '\0' and one for sign.
	char buf[21];
	buf[0] = '\0';

	// "If both the converted value and the precision are 0 the conversion results in no characters."
	if (value == 0 && precision == 0) return 0;

	if (prepend_space && value > 0) { buf[0] = ' '; written++; }
	if (prepend_sign && value > 0) { buf[0] = '+'; written++; }

	size_t length = 0;
	if ((prepend_space || prepend_sign) && value > 0) {
		length += int_to_string_serial(value, base, buf + 1, 20);
	} else {
		length += int_to_string_serial(value, base, buf, 21);
	}

	// We now have the value in buf. We need to worry about padding and stuff now.
	// We're going to do precision first.

	// This case, we add characters if field_width is greater than precision
	// If length is greater than precision, we dont care.
	if (length < precision) {
		for (size_t i = length; i < precision; i++) {
			shift_right_serial(buf, sizeof(buf));
			buf[0] = '0';
			length++;
		}
	}

	// Padding is zeros
	if (length < padding) {
		if (left_justified) {
			for (size_t i = length; i < padding; i++) {
				buf[i] = '0';
				length++;
			}
			buf[padding] = '\0';
		} else {
			for (size_t i = length; i < padding; i++) {
				shift_right_serial(buf, sizeof(buf));
				buf[0] = '0';
				length++;
			}
		}
	}

	written += print_string_serial(buf, 0, false, field_width, left_justified);

	return written;
}

size_t print_unsigned_int_serial(uintmax_t value, base_type base, size_t precision, size_t field_width, size_t padding, bool captial, bool alternate, bool left_justified) {
	size_t written = 0;

	if (precision > 19) precision = 19;
	if (padding > 19) padding = 19;
	if (field_width > 19) field_width = 19;

	// The max length of any possible value is 23 digits, with an unsigned 64 bit octal in alternate form.
	char buf[24];
	buf[0] = '\0';

	// "If both the converted value and the precision are 0 the conversion results in no characters."
	// Octals have a weird case, if they have 0 precision and 0 value, they still print 0.
	if (value == 0 && precision == 0 && base != BASE_OCTAL) return 0;

	size_t length = 0;

	if (alternate && base == BASE_HEX) {
		buf[0] = '0';
		if (captial) {
			buf[1] = 'X';
		} else {
			buf[1] = 'x';
		}
		written += 2;

		length += uint_to_string_serial(value, base, buf + 2, sizeof(buf) - 2, captial);
	} else if (alternate && base == BASE_OCTAL) {
		buf[0] = '0';
		written += 2;

		length += uint_to_string_serial(value, base, buf + 1, sizeof(buf) - 1, captial);
	} else {
		length += uint_to_string_serial(value, base, buf, sizeof(buf), captial);
	}

	// We now have the value in buf. We need to worry about padding and stuff now.
	// We're going to do precision first.

	// This case, we add characters if field_width is greater than precision
	// If length is greater than precision, we dont care.
	if (length < precision) {
		for (size_t i = length; i < precision; i++) {
			shift_right_serial(buf, sizeof(buf));
			buf[0] = '0';
			length++;
		}
	}

	// Padding is zeros
	if (length < padding) {
		if (left_justified) {
			for (size_t i = length; i < padding; i++) {
				buf[i] = '0';
				length++;
			}
			buf[padding] = '\0';
		} else {
			for (size_t i = length; i < padding; i++) {
				shift_right_serial(buf, sizeof(buf));
				buf[0] = '0';
				length++;
			}
		}
	}

	written += print_string_serial(buf, 0, false, field_width, left_justified);

	return written;
}

typedef enum {
	FLOAT_REGULAR,
	FLOAT_SCIENTIFIC,
	FLOAT_HEX
} float_type;

// Most of this code was taken from here:
// https://www.exploringbinary.com/quick-and-dirty-floating-point-to-decimal-conversion/
// I modified it to use "value" instead of fp, and added checks for NAN and INFINITY
// Most of the rest of it is the same.
// This printf_serial is really only for internal kernel (and serial) purposes, so I don't really care about floats.
// I wanted something small that would output something that was in the ballpark of a double's value.
#pragma GCC diagnostic ignored "-Wunused-parameter"
int print_float_serial(long double value, float_type base, size_t precision, size_t field_width, size_t padding, bool captial, bool alternate, bool left_justified) {
	char conversion[1076], intPart_reversed[311];
	int i, charCount = 0;
	double fp_int, fp_frac;

	// If it's bigger than max it's got to be infinity.
	if (value > DBL_MAX) {
		print_string_serial("inf", 0, false, field_width, left_justified);
		goto end;
	}

	// NAN does not equal NAN
	// This should in theory work (but might get optimized out)
	// I dont really care either way, I'm staying away from floats in the kernel.
	if (value != value) {
		print_string_serial("nan", 0, false, field_width, left_justified);
		goto end;
	}

	fp_frac = modf(value, &fp_int); //Separate integer/fractional parts

	while (fp_int > 0) { //Convert integer part, if any
		intPart_reversed[charCount++] = '0' + (int) fmod(fp_int, 10);
		fp_int = floor(fp_int / 10);
	}

	//Reverse the integer part, if any
	for (i = 0; i < charCount; i++) conversion[i] = intPart_reversed[charCount - i - 1];

	conversion[charCount++] = '.'; //Decimal point

	while (fp_frac > 0) { //Convert fractional part, if any
		fp_frac *= 10;
		fp_frac = modf(fp_frac, &fp_int);
		conversion[charCount++] = '0' + (int) fp_int;
	}

	conversion[charCount] = '\0'; //String terminator
	print_string_serial(conversion, 0, false, field_width, left_justified);

end:
	return charCount;
}

float_type calculate_float_shortest_serial(long double value) {

	return 0;
}

/* Writes the results to the output stream stdout. */
int printf_serial(const char* restrict format, ...) {
	va_list arg;
	int ret;
	va_start(arg, format);
	ret = vprintf_serial(format, arg);
	va_end(arg);
	return ret;
}

int vprintf_serial(const char* restrict format, va_list list) {
	const char* current = format;
	size_t written = 0;

	// 22 digits is the max width of a 64 bit octal.
	char precision_buf[3];
	precision_buf[0] = '\0';
	size_t precision_buf_index = 0;
	size_t precision = 1;
	bool precision_specified = false;

	// same as precision, 22 digits is the max length
	char field_width_buf[3];
	field_width_buf[0] = '\0';
	size_t field_width_index = 0;
	size_t field_width = 0;

	// any padding over 22 digits is ignored.
	// The padding buf only holds the provided value of the padding in the string not the actual padding itself.
	char padding_buf[3];
	padding_buf[0] = '\0';
	size_t padding_index = 0;
	size_t padding = 0;

	// Random flags for formatting
	bool left_justified = false;
	bool prepend_sign = false;
	bool prepend_space = false;
	bool alternate_form = false;

	bool check_current = false;
	printf_modifier current_modifier = TYPE_REGULAR;

	while (*current != '\0') {
		if (*current == '%' || check_current) {
			if (check_current) {
				check_current = false;
			} else {
				current++;
			}

			if (*current == '\0') break;

			// This is a bit messy, especially with the "fallthrough" comments.
			// GCC tends to warn on fallthrough, but ignores it if you have the comment.
			switch (*current) {
				// ------------------------------------------------------------------------------------------------
				// Normal Integers
				// ------------------------------------------------------------------------------------------------
				// signed int
				case 'd': // fallthrough
				case 'i': {
						switch (current_modifier) {
							case TYPE_SHORT_SHORT: {
									written += print_signed_int_serial((intmax_t) va_arg(list, int), BASE_DECIMAL, precision, field_width, padding, left_justified, prepend_space, prepend_sign);
									break;
								}
							case TYPE_SHORT: {
									written += print_signed_int_serial((intmax_t) va_arg(list, int), BASE_DECIMAL, precision, field_width, padding, left_justified, prepend_space, prepend_sign);
									break;
								}
							case TYPE_LONG: {
									written += print_signed_int_serial((intmax_t) va_arg(list, long), BASE_DECIMAL, precision, field_width, padding, left_justified, prepend_space, prepend_sign);
									break;
								}
							case TYPE_LONG_LONG: {
									written += print_signed_int_serial((intmax_t) va_arg(list, long long), BASE_DECIMAL, precision, field_width, padding, left_justified, prepend_space, prepend_sign);
									break;
								}
							case TYPE_INTMAX_T: {
									written += print_signed_int_serial(va_arg(list, intmax_t), BASE_DECIMAL, precision, field_width, padding, left_justified, prepend_space, prepend_sign);
									break;
								}
								// I legit dont think I can even get a signed size_t to be platform independent.
								// I'm just going to pass it through as signed and see what happens.
							case TYPE_SIZE_T: {
									written += print_signed_int_serial((intmax_t) va_arg(list, size_t), BASE_DECIMAL, precision, field_width, padding, left_justified, prepend_space, prepend_sign);
									break;
								}
							case TYPE_PTRDIFF: {
									written += print_signed_int_serial((intmax_t) va_arg(list, ptrdiff_t), BASE_DECIMAL, precision, field_width, padding, left_justified, prepend_space, prepend_sign);
									break;
								}
								// We have Regular and Long Double here.
								// We just pretend long double doesn't exist.
							default: {
									written += print_signed_int_serial((intmax_t) va_arg(list, int), BASE_DECIMAL, precision, field_width, padding, left_justified, prepend_space, prepend_sign);
									break;
								}
						}
						break;
					}
				// All of these have the same unsigned base type.
				// We just change a few values to the pass to print_unsigned_int_serial
				case 'u': // fallthrough
				case 'o': // fallthrough
				case 'x': // fallthrough
				case 'X': {
						char c = *current;
						base_type base = BASE_DECIMAL;
						bool capital = false;

						if (c == 'X') {
							base = BASE_HEX;
							capital = true;
						} else if (c == 'x') {
							base = BASE_HEX;
						} else if (c == 'o') {
							base = BASE_OCTAL;
						}

						switch (current_modifier) {
							case TYPE_SHORT_SHORT: {
									written += print_unsigned_int_serial(va_arg(list, unsigned int), base, precision, field_width, padding, capital, alternate_form, left_justified);
									break;
								}
							case TYPE_SHORT: {
									written += print_unsigned_int_serial((uintmax_t) va_arg(list, unsigned int), base, precision, field_width, padding, capital, alternate_form, left_justified);
									break;
								}
							case TYPE_LONG: {
									written += print_unsigned_int_serial((uintmax_t) va_arg(list, unsigned long), base, precision, field_width, padding, capital, alternate_form, left_justified);
									break;
								}
							case TYPE_LONG_LONG: {
									written += print_unsigned_int_serial((uintmax_t) va_arg(list, unsigned long long), base, precision, field_width, padding, capital, alternate_form, left_justified);
									break;
								}
							case TYPE_INTMAX_T: {
									written += print_unsigned_int_serial(va_arg(list, uintmax_t), base, precision, field_width, padding, capital, alternate_form, left_justified);
									break;
								}
							// I legit dont think I can even get a signed size_t to be platform independent.
							// I'm just going to pass it through as signed and see what happens.
							case TYPE_SIZE_T: {
									written += print_unsigned_int_serial((uintmax_t) va_arg(list, size_t), base, precision, field_width, padding, capital, alternate_form, left_justified);
									break;
								}
							case TYPE_PTRDIFF: {
									written += print_unsigned_int_serial((uintmax_t) va_arg(list, ptrdiff_t), base, precision, field_width, padding, capital, alternate_form, left_justified);
									break;
								}
								// We have Regular and Long Double here.
								// We just pretend long double doesn't exist.
							default: {
									written += print_unsigned_int_serial((uintmax_t) va_arg(list, unsigned int), base, precision, field_width, padding, capital, alternate_form, left_justified);
									break;
								}
						}
						break;
					}
					// ------------------------------------------------------------------------------------------------
					// Floating point
					// ------------------------------------------------------------------------------------------------
					// These all fall through to the same one.
					// The implementations are all practically identical.
				case 'f': // fallthrough
				case 'F': // fallthrough
				case 'e': // fallthrough
				case 'E': // fallthrough
				case 'a': // fallthrough
				case 'A': // fallthrough
				case 'g': // fallthrough
				case 'G': {
						char c = *current;
						float_type type = FLOAT_REGULAR;
						bool capital = false;
						long double value;
						if (current_modifier == TYPE_LONG_DOUBLE) {
							value = va_arg(list, long double);
						} else {
							value = (long double) va_arg(list, double);
						}

						switch (c) {
							case 'F': {
									capital = true;
									break;
								}

							case 'e': capital = true; // fallthrough
							case 'E': {
									type = FLOAT_SCIENTIFIC;
									break;
								}

							case 'a': capital = true; // fallthrough
							case 'A': {
									type = FLOAT_HEX;
									break;
								}

							case 'g': capital = true; // fallthrough
							case 'G': {
									type = calculate_float_shortest_serial(value);
								}
							default: break;
						}

						//printf_serial("Value: %Lf", value);

						written += print_float_serial(value, type, precision, field_width, padding, capital, alternate_form, left_justified);

						break;
					}
					// ------------------------------------------------------------------------------------------------
					// Chars, Strings, Pointers, and Current Written
					// ------------------------------------------------------------------------------------------------
				case 'c': {
						if (current_modifier == TYPE_LONG) {
							wchar_t c = (wchar_t) va_arg(list, int);
							wchar_t str[] = { c, '\0' };
							written += print_wstring_serial(str, 0, false);
						} else {
							// The standard calls for us to take an int and convert to unsigned char
							write_serial((unsigned char) va_arg(list, int));
							written++;
						}
						break;
					}
				case 's': {
					// TODO: This is technically supposed to call wcrtomb
					// Im not doing that, probably ever.
						if (current_modifier == TYPE_LONG) {
							wchar_t* str = va_arg(list, wchar_t*);
							written += print_wstring_serial(str, precision, precision_specified);
						} else {
							// The standard calls for us to take an int and convert to unsigned char
							char* str = va_arg(list, char*);
							written += print_string_serial(str, precision, precision_specified, field_width, left_justified);
						}
						break;
					}
				case 'p': {
					// Can only be regular type. We're just going to ignore modifiers.
					// This is actually implementation defined.
					// We're going to write the hex for it.
						void* p = va_arg(list, void*);
						written += print_unsigned_int_serial((uintptr_t) p, BASE_HEX, 0, 0, 0, true, true, left_justified);
						break;
					}
#ifdef WALLOS_ENABLE_PRINTF_N
				// A lot of implementations disable this for "security" reasons *cough* *cough* windows.
				// I'm disabling it by default, but it's still supported and easy to enable.
				case 'n': {
					// This one a lil weird.
					// We write the current written amount (not including flags, field width, or precision) to the provided pointer.
					// The provided pointer is determined by the modifier
						switch (current_modifier) {
							case TYPE_SHORT_SHORT: {
									signed char* dest = va_arg(list, signed char*);
									*(dest) = (signed char) written;
									break;
								}
							case TYPE_SHORT: {
									short* dest = va_arg(list, short*);
									*(dest) = (short) written;
									break;
								}
							case TYPE_LONG: {
									long* dest = va_arg(list, long*);
									*(dest) = (long) written;
									break;
								}
							case TYPE_LONG_LONG: {
									long long* dest = va_arg(list, long long*);
									*(dest) = (long long) written;
									break;
								}
							case TYPE_INTMAX_T: {
									intmax_t* dest = va_arg(list, intmax_t*);
									*(dest) = (intmax_t) written;
									break;
								}
								// The standard calls for a signed size_t???
								// I dont think any system has a signed size_t
							case TYPE_SIZE_T: {
									size_t* dest = va_arg(list, size_t*);
									*(dest) = (size_t) written;
									break;
								}
							case TYPE_PTRDIFF: {
									ptrdiff_t* dest = va_arg(list, ptrdiff_t*);
									*(dest) = (ptrdiff_t) written;
									break;
								}
								// Type Regular is here, as is Long Double.
								// Long double should never be used for this and isn't part of the standard.
								// We're just going to assume it's an int for this case.
							default: {
									int* dest = va_arg(list, int*);
									*(dest) = (int) written;
									break;
								}
						}
						break;
					}
#endif // WALLOS_ENABLE_PRINTF_N
				// ------------------------------------------------------------------------------------------------
				// Flags
				// ------------------------------------------------------------------------------------------------
				// Justify Left
				case '-': {
						left_justified = true;
						current++;
						check_current = true;
						break;
						}
						// Signed Conventions
				case '+': {
						prepend_sign = true;
						current++;
						check_current = true;
						break;
					}
					// I legit didn't know space was a valid format character.
					// If no sign is going to be written, a space is inserted before the value
				case ' ': {
						prepend_space = true;
						current++;
						check_current = true;
						break;
					}
					// Alternate forms
				case '#': {
						alternate_form = true;
						current++;
						check_current = true;
						break;
					}
					// ------------------------------------------------------------------------------------------------
					// Width/Precision
					// ------------------------------------------------------------------------------------------------
					// Padding
				case '0': {
						current++;
						if (*current == '\0') break;

						bool invalid = false;

						// We ignore padding if left justified
						if (left_justified) invalid = true;

						while (*current == '-' || (*current >= '0' && *current <= '9')) {
							if (*current == '-') {
								padding = 0;
								invalid = true;
							}

							if (!invalid) {
								if (padding_index >= 2) {
									current++;
									continue;
								}
								padding_buf[padding_index] = *current;
								padding_buf[padding_index + 1] = '\0';
								padding_index++;
							}

							current++;
						}

						if (!invalid) {
							padding = (int) strtol(padding_buf, NULL, 10);
							memset(padding_buf, 0, 3);
							padding_index = 0;
						}

						check_current = true;
						break;
					}
					// I have no better way of doing this.
					// These are all field width. 0 is for padding, so that's why it's excluded.
				case '*': {
						field_width = va_arg(list, int);
						check_current = true;
						current++;
						break;
					}
				case '1': // fallthrough
				case '2': // fallthrough
				case '3': // fallthrough
				case '4': // fallthrough
				case '5': // fallthrough
				case '6': // fallthrough
				case '7': // fallthrough
				case '8': // fallthrough
				case '9': {
						while ((*current >= '0' && *current <= '9')) {
							// We just ignore anything outside the range.
							if (field_width_index >= 2) {
								current++;
								continue;
							}
							field_width_buf[field_width_index] = *current;
							field_width_buf[field_width_index + 1] = '\0';
							field_width_index++;
							current++;
						}

						field_width = (int) strtol(field_width_buf, NULL, 10);
						memset(field_width_buf, 0, 3);
						field_width_index = 0;

						check_current = true;
						break;
					}
					// Precision
				case '.': {
						current++;

						if (*current == '\0') break;

						// If not one of these, it's supposed to be taken as 0
						if (*current != '*' && *current != '-' && !(*current >= '0' && *current <= '9')) {
							precision = 0;
							precision_specified = true;
							current++;
							check_current = true;
							break;
						}

						bool param = false;
						if (*current == '*') {
							precision = va_arg(list, int);
							param = true;
						}

						bool negative = false;
						// The standard tells us to skip any negative precision.
						while (*current == '-' || (*current >= '0' && *current <= '9')) {
							if (*current == '-') {
								negative = true;
								precision = 0;
							}

							if (!negative) {
								if (precision_buf_index >= 2) {
									current++;
									continue;
								}
								precision_buf[precision_buf_index] = *current;
								precision_buf[precision_buf_index + 1] = '\0';
								precision_buf_index++;
							}
							current++;
						}

						if (!negative && !param) {
							precision = (int) strtol(precision_buf, NULL, 10);
							memset(precision_buf, 0, 3);
							precision_buf_index = 0;
						}

						precision_specified = true;
						check_current = true;
						break;
					}

					// ------------------------------------------------------------------------------------------------
					// Length
					// ------------------------------------------------------------------------------------------------
					// short
				case 'h': {
						current++;
						if (*current == '\0') break;

						if (*current == 'h') {
							current_modifier = TYPE_SHORT_SHORT;
							current++;
						} else {
							current_modifier = TYPE_SHORT;
						}

						check_current = true;
						break;
					}
					// long
				case 'l': {
						current++;
						if (*current == '\0') break;

						if (*current == 'l') {
							current_modifier = TYPE_LONG_LONG;
							current++;
						} else {
							current_modifier = TYPE_LONG;
						}

						check_current = true;
						break;
					}
					// intmax_t or uintmax_t
				case 'j': {
						current_modifier = TYPE_INTMAX_T;
						current++;
						check_current = true;
						break;
					}
					// size_t or ssize_t
				case 'z': {
						current_modifier = TYPE_SIZE_T;
						current++;
						check_current = true;
						break;
					}
					// ptrdiff_t
				case 't': {
						current_modifier = TYPE_PTRDIFF;
						current++;
						check_current = true;
						break;
					}
				case 'L': {
						current_modifier = TYPE_LONG_DOUBLE;
						current++;
						check_current = true;
						break;
					}
				default: {
						write_serial(*current);
						written++;
						break;
					}
					}
			} else {
			write_serial(*current);
			written++;
		}

		if (*current == '\0') break;
		if (!check_current) {
			current_modifier = TYPE_REGULAR;
			precision_specified = false;
			left_justified = false;
			alternate_form = false;
			prepend_space = false;
			prepend_sign = false;
			field_width = 0;
			precision = 1;
			padding = 0;
			current++;
		}
		}

	return (int) written;
	}