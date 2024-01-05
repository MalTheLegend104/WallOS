#ifndef FEATURES_HPP
#define FEATURES_HPP

#include <stdbool.h>
#include <klibc/cpuid_calls.h>

class Features {
private:
	static bool AVX;
	static bool FXSR;
	static bool APIC;
	static const char* highest_supported_float;
	static struct cpu_features* features;

	static bool listFeature(const char* name, bool a);
	static bool listFeatureCheck(const char* name, bool a);
	static void checkFloatingPointSupport();
	static void enableSSE();
	static bool setupAPIC();
public:
	static void checkFeatures(struct cpu_features* f);
	static const char* highestFloat();
	static bool getAPIC();

	static void enableFeatures();
};

#endif // FEATURES_HPP