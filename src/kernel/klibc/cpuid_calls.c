#include <klibc/cpuid_calls.h>
#include <stdlib.h>
#include <stdint.h>
#include <cpuid.h>
#include <string.h>
#include <stddef.h>

#include <klibc/kprint.h>

#define exx uint32_t
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

/**
 * @brief Gets the vendor ID of the cpu.
 *
 * @return char* string containing the vendorID
 */
char* vendorID() {
	// Call cpuid, get the return values
	exx ax, bx, cx, dx;
	__cpuid(0, ax, bx, cx, dx);

	// 4 bits + \0
	char ebx[5];
	convertbinarytoascii(bx, ebx);

	char edx[5];
	convertbinarytoascii(dx, edx);

	char ecx[5];
	convertbinarytoascii(cx, ecx);

	// This line of code is beautiful
	return strcat(strcat(ebx, edx), ecx);
}

/**
 * @brief Shift the array so that all 32 bits are represented.
 * Calling itoa with base 2 will result in any leading 0's being disgarded.
 * Normally this doesnt really matter, but in case of OS dev, those disgarded
 * bits can be very important.
 *
 * @param array Array containing the 32 bit binary number.
 * This MUST be sized as 33 to account for the null terminator.
 */
void padding_32(char* array) {
	int size = strlen(array);
	int shift = 32 - size;
	for (int i = 32; i > 0; i--) {
		array[i] = array[i - shift];
	}
	for (int i = 0; i < shift; i++) {
		array[i] = '0';
	}
}


cpu_features cpuFeatures() {
	exx ax, bx, cx, dx;
	__cpuid(1, ax, bx, cx, dx);
	cpu_features features = {};

	// ---------------------------------------------
	// ECX FEATURES
	// ---------------------------------------------
	char ecx[33];
	itoa(cx, ecx, 2);
	padding_32(ecx);
	/* When reading the manual, the indexs starts at 31 and works backwords.
	 *
	 * Ours is layed out like this:
	 * edx[0] -> manual bit 31
	 * edx[1] -> manual bit 30
	 * etc
	 * We reverse this to fix the mappings.
	 */
	strrev(ecx, 0, 31);
	// this is gonna be painful 🥲
	// 31-> "Reserved for use by hypervisor to indicate guest status."
	// 30 is reserved
	features.F16C = ecx[29];
	features.AVX = ecx[28];
	features.OSXSAVE = ecx[27];
	features.XSAVE = ecx[26];
	features.AES = ecx[25];
	// 24 is reserved
	features.POPCNT = ecx[23];
	// 22:21 are reserved
	features.SSE4_2 = ecx[20];
	features.SSE4_1 = ecx[19];
	// 18:14 are reserved
	features.CMPXCHG16B = ecx[13];
	features.FMA = ecx[12];
	// 11:10 are reserved
	features.SSE3 = ecx[9];
	// 8:4 are reserved
	features.MONITOR = ecx[3];
	// 2 is reserved
	features.PCLMULQDQ = ecx[1];
	features.SSE3 = ecx[0];

	// ---------------------------------------------
	// EDX FEATURES
	// ---------------------------------------------
	char edx[33]; // 32 bits + \0
	itoa(dx, edx, 2);
	padding_32(edx);
	strrev(edx, 0, 31);
	puts_vga("\n");
	puts_vga(edx);
	puts_vga("\n");
	// 31:29 are reserved
	features.HTT = edx[28];
	// 27 is reserved
	features.SSE2 = edx[26];
	features.SSE = edx[25];
	features.FXSR = edx[24];
	features.MMX = edx[23];
	// 22:20 are reserved
	features.CLFSH = edx[19];
	// 18 is reserved
	features.PSE36 = edx[17];
	features.PAT = edx[16];
	features.CMOV = edx[15];
	features.MCA = edx[14];
	features.PGE = edx[13];
	features.MTRR = edx[12];
	features.SYSENTER_SYSEXIT = edx[11];
	// 10 is reserved
	features.APIC = edx[9];
	features.CMPXCHG8B = edx[8];
	features.MCE = edx[7];
	features.PAE = edx[6];
	features.MSR = edx[5];
	features.TSC = edx[4];
	features.PSE = edx[3];
	features.DE = edx[2];
	features.VME = edx[1];
	features.FPU = edx[0];

	return features;
}
#undef exx