#ifndef STDLIB_H
#define STDLIB_H 
#ifdef __cplusplus
extern "C" {
#endif
	char* itoa(long long value, char* buffer, int base);
	char* ftoa(double d, char* buffer, int precision);
	int atoi(const char* str);
#ifdef __cplusplus
}
#endif

#endif
