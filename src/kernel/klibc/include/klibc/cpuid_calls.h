#ifndef CPUID_CALLS_H
#define CPUID_CALLS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
	// We only really care about ecx and edx
	// ebx contains info related to the system, not cpu
	// See https://www.amd.com/system/files/TechDocs/25481.pdf
	typedef struct cpu_features {
		// ecx
		uint8_t F16C; // last unreserved bit, #29
		uint8_t AVX;
		uint8_t OSXSAVE;
		uint8_t XSAVE;
		uint8_t AES;
		uint8_t POPCNT;
		uint8_t SSE4_2;
		uint8_t SSE4_1;
		uint8_t CMPXCHG16B; // idek what this is
		uint8_t FMA;
		uint8_t SSSE3;
		uint8_t MONITOR;
		uint8_t SSE3; // starts here with bit 0
		// edx
		uint8_t HTT; // last unreserved bit, #28
		uint8_t SSE2;
		uint8_t SSE;
		uint8_t FXSR;
		uint8_t MMX;
		uint8_t CLFSH;
		uint8_t PSE36;
		uint8_t PAT;
		uint8_t CMOV;
		uint8_t MCA;
		uint8_t PGE;
		uint8_t MTRR;
		uint8_t SYSENTER_SYSEXIT;
		uint8_t APIC;
		uint8_t CMPXCHG8B; // yet again i do not know what this is
		uint8_t MCE;
		uint8_t PAE;
		uint8_t MSR;
		uint8_t TSC;
		uint8_t PSE;
		uint8_t DE;
		uint8_t VME;
		uint8_t FPU;
	} cpu_features;

	char* vendorID();
	cpu_features* cpuFeatures();

#ifdef __cplusplus
}
#endif
#endif // CPUID_CALLS_H