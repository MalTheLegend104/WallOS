#include <stdio.h>
#include <stdarg.h>

//converts a number to a base of choice
char* convert(unsigned int num, int base) {
	static char Representation[] = "0123456789ABCDEF";
	static char buffer[50];
	char* ptr;

	ptr = &buffer[49];
	*ptr = '\0';

	do {
		*--ptr = Representation[num % base];
		num /= base;
	} while (num != 0);
	return(ptr);
}

//straight print until \0 is hit
void print_until_null(const char* data) {
	while (*data != '\0') {
		putc_vga(*data);
		data++;
	}
}

int puts(const char* string) {
	return printf("%s\n", string);
}


int vprintf(const char* format, va_list arg) {
	const char* traverse = format;
	int i;
	char* s;

	while (*traverse != '\0') {
		if (*traverse == '%') {
			traverse++;
			switch (*traverse) {
				case 'c': i = va_arg(arg, int);
					print_until_null((const char*) &i);
					break;

				case 'd': i = va_arg(arg, int);
					if (i < 0) {
						i = -i;
						print_until_null("-\0");
					}
					print_until_null(convert(i, 10));
					break;

				case 'o': i = va_arg(arg, unsigned int);
					print_until_null(convert(i, 8));
					break;

				case 's': s = va_arg(arg, char*);
					print_until_null(s);
					break;

				case 'x': i = va_arg(arg, unsigned int);
					print_until_null(convert(i, 16));
					break;

				default:
					break;
			}
		} else if (*traverse == '\t') {
			print_until_null("    \0");
		} else {
			putc_vga(*traverse);
		}
		traverse++;
	}
	return i;
}

// Print a formatted string.
int printf(const char* format, ...) {
	va_list arg;
	int ret;
	va_start(arg, format);
	ret = vprintf(format, arg);
	// ret = temp(format, arg);
	va_end(arg);
	return ret;
}