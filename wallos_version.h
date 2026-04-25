/* This file is the only spot the version of WallOS is contained.
 * It's in a dedicated file to make sure it's easy to find and update.
 * This is already force included by every single file, via "wallos_config.h"
 */
#pragma once

/* Version string for the current version of WallOS.
 * This will be displayed in several different places.
 *
 * Anything using this should *not* expect it to remain the same length.
 * The only expectation from this define is that it exist
 */
#define WALLOS_VERSION_STR "WallOS v0.3.1"

/* Integer version of the current version of WallOS
 * This is what's expected to be used for compatibility checking.
 * This is guaranteed to be a 64 bit integer.
 *
 * The main version integer is WALLOS_VERSION_INT, defined in the block below.
 *
 * These three defines are the only ones that should be changed.
 * Everything else is derived from these three.
 */
#define WALLOS_VERSION_MAJOR_INT 0
#define WALLOS_VERSION_MINOR_INT 3
#define WALLOS_VERSION_PATCH_INT 1

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Convience Macros/Defines
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// These are convience macros/constants to make dealing with version info easy.
#define WALLOS_VERSION_MAJOR_BITS 16ULL
#define WALLOS_VERSION_MINOR_BITS 24ULL
#define WALLOS_VERSION_PATCH_BITS 24ULL

#define WALLOS_VERSION_MAJOR_MAX ((1ULL << WALLOS_VERSION_MAJOR_BITS) - 1)
#define WALLOS_VERSION_MINOR_MAX ((1ULL << WALLOS_VERSION_MINOR_BITS) - 1)
#define WALLOS_VERSION_PATCH_MAX ((1ULL << WALLOS_VERSION_PATCH_BITS) - 1)

#define WALLOS_VERSION_PATCH_SHIFT 0ULL
#define WALLOS_VERSION_MINOR_SHIFT (WALLOS_VERSION_PATCH_BITS + WALLOS_VERSION_PATCH_SHIFT)
#define WALLOS_VERSION_MAJOR_SHIFT (WALLOS_VERSION_PATCH_BITS + WALLOS_VERSION_MINOR_BITS + WALLOS_VERSION_PATCH_SHIFT)

// "Getters" to extract the parts of the version number
#define WALLOS_VERSION_MAJOR(v) (((v) >> WALLOS_VERSION_MAJOR_SHIFT) & WALLOS_VERSION_MAJOR_MAX)
#define WALLOS_VERSION_MINOR(v) (((v) >> WALLOS_VERSION_MINOR_SHIFT) & WALLOS_VERSION_MINOR_MAX)
#define WALLOS_VERSION_PATCH(v) (((v) >> WALLOS_VERSION_PATCH_SHIFT) & WALLOS_VERSION_PATCH_MAX)

// This lets you "make" any version integer easily from the major.minor.patch 
#define WALLOS_VERSION_MAKE(maj, min, patch) \
 (((uint64_t)(maj) << WALLOS_VERSION_MAJOR_SHIFT) | \
  ((uint64_t)(min) << WALLOS_VERSION_MINOR_SHIFT) | \
  ((uint64_t)(patch) << WALLOS_VERSION_PATCH_SHIFT))

// This is the main integer containing the version info.
#define WALLOS_VERSION_INT WALLOS_VERSION_MAKE(WALLOS_VERSION_MAJOR_INT, WALLOS_VERSION_MINOR_INT,  WALLOS_VERSION_PATCH_INT)

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Asserts
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// These really shouldn't be possible
// The only way for these to trigger is if something is mistyped (or I somehow have 16 million patches to the same version)
#if WALLOS_VERSION_MAJOR_INT > WALLOS_VERSION_MAJOR_MAX
#error "WALLOS_VERSION_MAJOR_INT exceeds 16-bit limit"
#endif

#if WALLOS_VERSION_MINOR_INT > WALLOS_VERSION_MINOR_MAX
#error "WALLOS_VERSION_MINOR_INT exceeds 24-bit limit"
#endif

#if WALLOS_VERSION_PATCH_INT > WALLOS_VERSION_PATCH_MAX
#error "WALLOS_VERSION_PATCH_INT exceeds 24-bit limit"
#endif
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Historical Version Defines
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/* This is for release versions that had a specific release name or were signifcant in some way
 *
 * These will follow a standard format:
 *     WALLOS_VERSION_<version_name>_<major>_<minor>_<patch>
 * If there is an accompanying string version, _STR is appended at the end
 *
 * These should all use the WALLOS_VERSION_MAKE() macro instead of directly setting them
 */

// The version that this semantic versioning scheme was introduced
#define WALLOS_VERSION_SEMVER_INTRODUCTION_0_3_1 WALLOS_VERSION_MAKE(0,3,1)