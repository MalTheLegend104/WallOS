#include <klibc/cpuid_calls.h>
#include <stdlib.h>
#include <stdint.h>
#include <cpuid.h>
#include <string.h>
#include <stddef.h>
#define exx unsigned int

#include <klibc/kprint.h>

// shifts every element in an array to the right by 1
void shiftRight(char* array) {
	int size = 33;
	for (int i = size - 1; i > 0; i--) {
		array[i] = array[i - 1];
	}
	array[0] = '0';
}

// Deciphers the ebx, ecx, and edx registers
char* convertbinarytoascii(exx reg, char* buf) {
	char regValue[33]; // 32 bits + \0
	itoa(reg, regValue, 2);

	int length = strlen(regValue);
	char test[10];

	// If the 32nd bit isn't set, itoa wont put it in the string
	if (length == 31) {
		shiftRight(regValue);
		length++;
	}

	// Work our way backwards
	char temp[9]; // 8 bits + \0
	int currentIndex = 24;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 8; j++) {
			temp[j] = regValue[currentIndex];
			currentIndex++;
		}
		currentIndex -= 16; // move the index back 8 from last time
		buf[i] = strtol(temp, 0, 2);
	}

	buf[4] = '\0';
	return buf;
}

char* vendorID() {
	// Get the values in the registers
	exx ax, bx, cx, dx;
	__cpuid(0, ax, bx, cx, dx);

	char ebx[5]; // 4 bits + \0
	convertbinarytoascii(bx, ebx);

	char edx[5]; // 4 bits + \0
	convertbinarytoascii(dx, edx);

	char ecx[5]; // 4 bits + \0
	convertbinarytoascii(cx, ecx);
	return strcat(strcat(ebx, edx), ecx);
}

cpu_features* cpuFeatures() {

}
#undef exx