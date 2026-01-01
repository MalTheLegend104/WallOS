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

	int strcmp(const char* str1, const char* str2);
	int strncmp(const char* s1, const char* s2, size_t n);

	char* strcpy(char* dest, const char* src);
	char* strncpy(char* dest, const char* src, size_t n);

	char* strcat(char* s1, const char* s2);
	char* strcat_c(char* string, char c, size_t size);

	char* strchr(const char* str, int ch);
	char* strrchr(const char* s, int c);

	void* memcpy(void*, const void*, size_t);
	int memcmp(const void* s1, const void* s2, size_t n);

	void* memset(void*, int, size_t);
	void* memset32(void*, uint32_t, size_t);
	void memsetw(void* dest, unsigned short val, int count);

	void* memmove(void* dest, const void* src, size_t n);

#ifdef __cplusplus
}
#endif
#endif