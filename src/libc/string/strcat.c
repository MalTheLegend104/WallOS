#include <string.h>
#include <stdarg.h>
// TODO implement a va_args version of strcat
/*const char* strcat(const char* s1, const char* s2) {
	int s1size = strlen(s1);
	int s2size = strlen(s2);
	int size = (s1size + s2size) - 1;
	char buf[size];
	int currentIndex = 0;
	while (currentIndex < s1size - 2) { // we dont want the first \0
		buf[currentIndex] = s1[currentIndex];
		currentIndex++;
	}
	for (int i = currentIndex; i < s2size - 1; i++) {
		buf[i] = s2[i];
	}
	return &buf;
}*/

char* strcat(char* dest, const char* src) {
	char* rdest = dest;
	while (*dest)
		dest++;
	while (*dest++ = *src++)
		;
	return rdest;
}