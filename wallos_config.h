/* This file is forcefully included along with every single file.
 *
 * This is meant to serve as VERY BASIC config file to enable/disable modules.
 * This is mostly to get around the mess I've created with makefiles.
 */
#pragma once

// ------------------------------------------------------------------------------------------------
// Versioning
// ------------------------------------------------------------------------------------------------
#include "wallos_version.h"

// TODO: need a better wrapper for architecture flags.
// I don't really want to throw around ifdef __X86_64__ or other flags like that, but do want a common interface
#define WALLOS_ARCH_X86_64

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// ACPI
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

/* ACPI Modules.
 *
 * I intend on supporting both ACPICA (for correctness), and uACPI (for ease of development).
 *
 * They both require a pretty much identical interface.
 * ACPICA tends to be way better at helping diagnose system problems, uACPI doesn't supply great error information.
 * uACPI is significantly less resource intensive and much quicker to run.
 *
 * Only one of these can be built at a time (intentionally). I was considering letting them both exist,
 * and still could "relatively" easily, but think it would just cause more problems than it's worth.
 */
#define WALLOS_USE_ACPICA 1
// #define WALLOS_USE_UACPI 1

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Scheduling/CPU related
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Defines the amount of IOAPIC we allow.
// This is defined statically for simplicity.
// If it becomes a problem, it can be made dynamic.
// My weird dual-socket server only has a 3 of these, 8 should be plenty.
#define WALLOS_IOAPIC_MAX 8

/* Scheduler configuration
 *
 */

// This limits the amount of space (statically allocated) used by the scheduler.
// I see ZERO reason WallOS will run on anything with more than 64 cores.
// If it does, this is easy to change and recompile.
// For initial setup before the scheduler is implemented, we use this in ways it shouldn't be used.
// If this comment is still here, know that scheduler_cpu.c relies on this in a bad way.
#define WALLOS_SYSTEM_MAX_CPU 64

// This should probably be a compiler flag rather than defined here.
// This tells the OS that we have 64 bit write/read rather than needing to split it up into 32bit write/read
#define WALLOS_ARCH_64

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Error Checking
// Checks for critical config items to make sure they are defined to sensible values
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/* 1 and only 1 ACPI subsystem should ever be present. */
#if !defined(WALLOS_USE_ACPICA) && !defined(WALLOS_USE_UACPI)
#error "Must have one ACPI subsystem enabled."
#elif defined(WALLOS_USE_ACPICA) && defined(WALLOS_USE_UACPI)
#error "Must have only one ACPI subsystem enabled, not both."
#endif

