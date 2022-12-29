#ifndef STDLIB_H
#define STDLIB_H 

#ifdef __cplusplus
extern "C" {
#endif
	char* itoa(int value, char* buffer, int radix);
	char* ftoa(double d, char* buffer, int precision);
#ifdef __cplusplus
}
#endif

#endif
