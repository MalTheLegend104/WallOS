/* This file is not officially a part of ACPICA, and therefore does not fall under the same license.
 * This falls under the license of WallOS itself.
 */

/* WallOS Specific Defines */
#ifndef __ACWALLOS_H__
#define __ACWALLOS_H__

// For now, we're going to leave the default data types. 
// Eventually we may need special types for threads, mutexes, etc

// Eventually this will probably be defined
// I dont really want to make sure we support the functions it needs right now.
// #define ACPI_USE_SYSTEM_CLIBRARY
// #define ACPI_USE_STANDARD_HEADERS

// I have no need for a cache in WallOS yet, so I'm not implementing one only for ACPICA
#define ACPI_USE_LOCAL_CACHE

// Make gcc happy
#define ACPI_USE_DO_WHILE_0

// Currently I don't plan on a 32 bit version but might explore the idea in the future.
#ifdef __WALLOS_32__
#define ACPI_MACHINE_WIDTH          32
#else
#define ACPI_MACHINE_WIDTH          64
#endif

#define ACPI_FLUSH_CPU_CACHE() asm volatile("wbinvd");

#endif /* __ACWALLOS_H__ */
