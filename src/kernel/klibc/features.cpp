#include "klibc/features.hpp"
#include "klibc/kprint.h"
#include <stdio.h>
#include "klibc/logger.h"
#include "panic.h"

bool Features::AVX;
bool Features::FXSR;
bool Features::APIC;
bool Features::floating_point;
const char* Features::highest_supported_float;
struct cpu_features* Features::features;

bool Features::listFeature(const char* name, bool a) {
	puts_vga("    ");
	if (a) {
		Logger::Checklist::checkEntry("%s is supported.", name);
	} else {
		Logger::Checklist::blankEntry("%s is not supported.", name);
	}
	return a;
}

bool Features::listFeatureCheck(const char* name, bool a) {
	puts_vga("    ");
	if (a) {
		Logger::Checklist::checkEntry("%s is supported.", name);
	} else {
		Logger::Checklist::noCheckEntry("%s is not supported.", name);
	}
	return a;
}

void Features::checkFloatingPointSupport() {
	/*
	 * Testing for cpu features is simple:
	 * if (features->feature == FEATURE_SUPPORTED) {
	 *     // feature is supported
	 * } else {
	 *     // feature isnt supported
	 * }
	 */
	if (listFeature("AVX", features->AVX == FEATURE_SUPPORTED)) {
		AVX = true;
		putc_vga('\t');
		FXSR = features->FXSR == FEATURE_SUPPORTED;
		listFeature("FXSR", FXSR);
		if (FXSR) {
			highest_supported_float = "FXSR";
		} else {
			highest_supported_float = "AVX";
		}
		return;
	}
	if (listFeature("SSE4.2", features->SSE4_2 == FEATURE_SUPPORTED)) {
		highest_supported_float = "SSE4.2";
		return;
	} else if (listFeature("SSE4.1", features->SSE4_1 == FEATURE_SUPPORTED)) {
		highest_supported_float = "SSE4.2";
		return;
	} else if (listFeature("SSSE3", features->SSSE3 == FEATURE_SUPPORTED)) {
		highest_supported_float = "SSSE3";
		return;
	} else if (listFeature("SSE3", features->SSE3 == FEATURE_SUPPORTED)) {
		highest_supported_float = "SSE3";
		return;
	} else if (listFeature("SSE2", features->SSE2 == FEATURE_SUPPORTED)) {
		highest_supported_float = "SSE2";
		return;
	} else if (listFeature("SSE", features->SSE == FEATURE_SUPPORTED)) {
		highest_supported_float = "SSE";
		return;
	} else if (listFeatureCheck("FPU", features->FPU == FEATURE_SUPPORTED)) {
		highest_supported_float = "FPU";
		return;
	}
	highest_supported_float = nullptr;
}

/**
 * @brief Check all important features.
 * It will call abort() if needed features do not exist.
 * @param f struct of features that will be checked.
 */
void Features::checkFeatures(struct cpu_features* f) {
	features = f;
	// Ideally we are going to be avoiding floats for as long as possible.
	// Still good to know if we have support for it though.
	Logger::Checklist::blankEntry("Checking Floating Point Support");
	checkFloatingPointSupport();
	if (highest_supported_float == nullptr) {
		Logger::Checklist::noCheckEntry("No Floating Point support.");
		floating_point = false;
	} else {
		Logger::Checklist::checkEntry("%s is highest supported Float Point instruction set.", Features::highest_supported_float);
		floating_point = true;
	}

	if (features->APIC == FEATURE_SUPPORTED) {
		APIC = true;
		Logger::Checklist::checkEntry("APIC exists.");
	} else {
		APIC = false;
		Logger::Checklist::noCheckEntry("APIC does not exists.");
	}
}

/**
 * @brief Returns the string equalivent of
 * the highest supported floating point instruction set
 *
 * @return const char* String of the highest supported floating point instruction set
 */
const char* Features::highestFloat() {
	return highest_supported_float;
}

/**
 * @brief Returns whether or not the APIC exists.
 *
 * @return true Does exist.
 * @return false Does not exist.
 */
bool Features::getAPIC() {
	return APIC;
}