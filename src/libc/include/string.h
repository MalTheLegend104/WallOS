#ifndef STRING_H
#define STRING_H 

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
	size_t strlen(const char*);
	void strrev(char* arr, int start, int end);
	long strtol(const char* str, char** endptr, int base);
	char* strcat(char* s1, const char* s2);
#ifdef __cplusplus
}
#endif
#endif