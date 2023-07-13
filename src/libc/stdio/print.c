#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#define UINT_BUF_SIZE 11
#define INT_BUF_SIZE 12
#define LL_BUF_SIZE 27
#define ULL_BUF_SIZE LL_BUF_SIZE
#define FLOAT_BUF_SIZE 49

char* convert_i(int num, int base) {
    static char Representation[] = "0123456789ABCDEF";
    static char buffer[INT_BUF_SIZE];
    char* ptr;

    ptr = &buffer[INT_BUF_SIZE - 1];
    *ptr = '\0';

    if (num < 0) {
        num = -num;
        *--ptr = '-';
    }

    do {
        *--ptr = Representation[num % base];
        num /= base;
    } while (num != 0);
    return(ptr);
}

char* convert_ui(unsigned int num, int base) {
    static char Representation[] = "0123456789ABCDEF";
    static char buffer[UINT_BUF_SIZE];
    char* ptr;

    ptr = &buffer[UINT_BUF_SIZE - 1];
    *ptr = '\0';

    do {
        *--ptr = Representation[num % base];
        num /= base;
    } while (num != 0);
    return(ptr);
}

char* convert_ll(long long num, int base) {
    static char Representation[] = "0123456789ABCDEF";
    static char buffer[LL_BUF_SIZE];
    char* ptr;

    ptr = &buffer[LL_BUF_SIZE - 1];
    *ptr = '\0';

    if (num < 0) {
        num = -num;
        *--ptr = '-';
    }

    do {
        *--ptr = Representation[num % base];
        num /= base;
    } while (num != 0);
    return(ptr);
}

char* convert_ull(unsigned long long num, int base) {
    static char Representation[] = "0123456789ABCDEF";
    static char buffer[ULL_BUF_SIZE];
    char* ptr;

    ptr = &buffer[ULL_BUF_SIZE - 1];
    *ptr = '\0';

    do {
        *--ptr = Representation[num % base];
        num /= base;
    } while (num != 0);
    return(ptr);
}


//straight print until \0 is hit
int print_until_null(const char* data) {
    int amount = 0;
	while (*data != '\0') {
		putc_vga(*data);
		data++;
        amount++;
	}
    return amount;
}

int puts(const char* string) {
	return printf("%s\n", string);
}

/**
 * This does not have full support for EVERY printf thing but has support for basic things.
 * THIS DOES NOT SUPPORT LONG DOUBLE OR DOUBLE TYPES. but does do basic floats because those are nice
 */
int vprintf(const char* format, va_list arg) {
    const char* traverse = format;
    int ret = 0;
    while (*traverse != '\0') {
        if (*traverse == '%') {
            traverse++;
            switch (*traverse) {
                case 'c': {
                    signed int i = va_arg(arg, int);
                    ret += print_until_null((const char *) &i);
                    break;
                }
                case 'd': {
                    ret += print_until_null(convert_i(va_arg(arg, int), 10));
                    break;
                }
                case 'f': {
                    char buf[FLOAT_BUF_SIZE];
                    ftoa(va_arg(arg, double), buf, 10);
                    ret += print_until_null(buf);
                    break;
                }
                case 'u':
                case 'i': {
                    ret += print_until_null(convert_ui(va_arg(arg, unsigned int), 10));
                    break;
                }
                case 'o': {
                    ret += print_until_null(convert_ui(va_arg(arg, unsigned int), 8));
                    break;
                }
                case 's': {
                    ret += print_until_null(va_arg(arg, char*));
                    break;
                }
                case 'x':
                case 'X': {
                    ret += print_until_null(convert_ui(va_arg(arg, unsigned long), 16));
                    break;
                }
                case 'n': {
                    int* b = va_arg(arg, int*);
                    *b = ret;
                    break;
                }
                case '%': {
                    ret += print_until_null("%\0");
                    break;
                }
                case 'l': {
                    traverse++;
                    switch (*traverse) {
                        case 'i':
                        case 'd': {
                            ret += print_until_null(convert_i(va_arg(arg, long), 10));
                            break;
                        }
                        case 'f': {
                            ret += print_until_null(dtoa(va_arg(arg, long double), 1, 1, 0, 0, 0));
                            break;
                        }
                        case 'u': {
                            ret += print_until_null(convert_ui(va_arg(arg, unsigned int), 10));
                            break;
                        }
                        case 'l': {
                                traverse++;
                                switch(*traverse) {
                                    case 'i':
                                    case 'd': {
                                        ret += print_until_null(convert_ll(va_arg(arg, long long), 10));
                                        break;
                                    }
                                    case 'u': {
                                        ret += print_until_null(convert_ull(va_arg(arg, unsigned long long), 10));
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
                    if(*traverse == 'f') {
                        ret += print_until_null(dtoa(va_arg(arg, long double), 1, 1, 0, 0, 0));
                    }
                    break;
                }
                default: break;
            }
        } else if (*traverse == '\t') {
            ret += print_until_null("    \0");
        } else {
            putc_vga(*traverse);
            ret++;
        }
        traverse++;
    }
    return ret;
}

// Print a formatted string.
int printf(const char* format, ...) {
	va_list arg;
	int ret;
	va_start(arg, format);
	ret = vprintf(format, arg);
    //ret = temp(format, arg);
	va_end(arg);
	return ret;
}

#undef UINT_BUF_SIZE
#undef INT_BUF_SIZE
#undef LL_BUF_SIZE
#undef ULL_BUF_SIZE
#undef FLOAT_BUF_SIZE