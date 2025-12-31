/* This file is forcefully included along with every single file.
 *
 * This is meant to serve as VERY BASIC config file to enable/disable modules.
 * This is mostly to get around the mess I've created with makefiles.
 */
#pragma once

/* ACPI Modules.
 *
 * I intend on supporting both ACPICA (for correctness), and uACPI (for ease of development).
 *
 * They both require a pretty much identical interface.
 * ACPICA tends to be way better at helping diagnose system problems, uACPI doesn't supply great error information.
 * uACPI is significantly less resource intensive and much quicker to run.
 */
#define WALLOS_USE_ACPICA 1
// #define WALLOS_USE_UACPI 1
