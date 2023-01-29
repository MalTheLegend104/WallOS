#include "klibc/features.hpp"
#include "klibc/kprint.h"
#include <stdio.h>
#include <stdbool.h>
#include "klibc/logger.h"

bool listFeature(const char* name, bool a) {
	if (a) {
		Logger::Checklist::checkEntry("%s is supported.", name);
	} else {
		Logger::Checklist::noCheckEntry("%s is not supported.", name);
	}
}

void check_features(const cpu_features features) {
	// Start out with most important features
	listFeature("FPU", features.FPU == FEATURE_SUPPORTED);
	listFeature("AVX", features.AVX == FEATURE_SUPPORTED);
	// All the SSE's
	listFeature("SSE", features.SSE == FEATURE_SUPPORTED);
	listFeature("SSE2", features.SSE2 == FEATURE_SUPPORTED);
	listFeature("SSE3", features.SSE3 == FEATURE_SUPPORTED);
	listFeature("SSSE3", features.SSSE3 == FEATURE_SUPPORTED);
	listFeature("SSE4.1", features.SSE4_1 == FEATURE_SUPPORTED);
	listFeature("SSE4.2", features.SSE4_2 == FEATURE_SUPPORTED);
}