#ifndef STRING_H
#define STRING_H 

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
	size_t strlen(const char*);
	void strrev(char* arr, int start, int end);
#ifdef __cplusplus
}
#endif
#endif