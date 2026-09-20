#include <string.h>

void strrev(char* str, int start, int end) {
	if (str == NULL) return;
	char temp;
	while (start < end) {
		temp = str[start];
		str[start] = str[end];
		str[end] = temp;
		start++;
		end--;
	}
}