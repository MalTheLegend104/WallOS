#ifndef FEATURES_HPP
#define FEATURES_HPP

#include "cpuid_calls.h"
#include <stdbool.h>
class Features {
private:
	static bool AVX;
	static bool FXSR;
	static bool floating_point;
	static const char* highest_supported_float;
	static struct cpu_features* features;

	static bool listFeature(const char* name, bool a);
	static bool listFeatureCheck(const char* name, bool a);
	static void checkFloatingPointSupport();
public:
	static void checkFeatures(struct cpu_features* f);
};

#endif // FEATURES_HPP