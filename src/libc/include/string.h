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
	void* memcpy(void* __restrict, const void* __restrict, size_t);
	void* memset(void*, int, size_t);
	void* memset32(void*, uint32_t, size_t);
	void memsetw(void* dest, unsigned short val, int count);
	char* strcat_c(char* string, char c, size_t size);
	int strcmp(const char* str1, const char* str2);
#ifdef __cplusplus
}
#endif
#endif