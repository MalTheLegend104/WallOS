#ifndef STRING_H
#define STRING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif
	size_t strlen(const char*);
	size_t strnlen(const char* str, size_t max);
	void strrev(char* arr, int start, int end);
	long strtol(const char* str, char** endptr, int base);
	long long strtoll(const char* __restrict__ nptr, char** __restrict__ endptr, int base);
	unsigned long strtoul(const char* __restrict__ nptr, char** __restrict__ endptr, int base);
	unsigned long long strtoull(const char* __restrict__ nptr, char** __restrict__ endptr, int base);

	float strtof(const char* __restrict__ nptr, char** __restrict__ endptr);
	double strtod(const char* __restrict__ nptr, char** __restrict__ endptr);

	int strcmp(const char* str1, const char* str2);
	int strncmp(const char* s1, const char* s2, size_t n);
	int strcasecmp(const char* s1, const char* s2);

	char* strcpy(char* dest, const char* src);
	char* strncpy(char* dest, const char* src, size_t n);

	char* strcat(char* s1, const char* s2);
	char* strcat_c(char* string, char c, size_t size);

	char* strchr(const char* str, int ch);
	char* strrchr(const char* s, int c);

	char* strstr(const char* str, const char* sub);

	void* memcpy(void*, const void*, size_t);
	int memcmp(const void* s1, const void* s2, size_t n);

	void* memset(void*, int, size_t);
	void* memset32(void*, uint32_t, size_t);
	void memsetw(void* dest, unsigned short val, int count);

	void* memmove(void* dest, const void* src, size_t n);

	/**
	 * @brief Check if a string contains a substring
	 *
	 * @param str String to search
	 * @param substring Substring to search for
	 * @return true If the string contains the substring
	 * @return false String or Substring are invalid or empty, or substring wasn't found.
	 */
	static inline bool str_contains(const char* str, const char* substring) {
		if (str == NULL || substring == NULL) return false;
		if (*substring == '\0') return true;

		for (; *str != '\0'; str++) {
			const char* h = str;
			const char* n = substring;
			while (*h != '\0' && *n != '\0' && *h == *n) {
				h++;
				n++;
			}
			if (*n == '\0') {
				return true;
			}
		}
		return false;
	}

#ifdef __cplusplus
}
#endif
#endif