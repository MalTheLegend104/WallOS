#ifndef __ACWALLOSEX_H__
#define __ACWALLOSEX_H__

#include "actypes.h"
#include "actbl.h"
#include "acpiosxf.h"

extern ACPI_STATUS AcpiOsInitialize();

extern ACPI_STATUS AcpiOsTerminate();

extern ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer();

extern ACPI_STATUS AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES* PredefinedObject, ACPI_STRING* NewValue);

extern ACPI_STATUS AcpiOsTableOverride(ACPI_TABLE_HEADER* ExistingTable, ACPI_TABLE_HEADER** NewTable);

extern void* AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS PhysicalAddress, ACPI_SIZE Length);

extern void AcpiOsUnmapMemory(void* where, ACPI_SIZE length);

extern ACPI_STATUS AcpiOsGetPhysicalAddress(void* LogicalAddress, ACPI_PHYSICAL_ADDRESS* PhysicalAddress);

extern void* AcpiOsAllocate(ACPI_SIZE Size);

extern void AcpiOsFree(void* Memory);

extern BOOLEAN AcpiOsReadable(void* Memory, ACPI_SIZE Length);

extern BOOLEAN AcpiOsWritable(void* Memory, ACPI_SIZE Length);

extern ACPI_THREAD_ID AcpiOsGetThreadId();

extern ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE Type, ACPI_OSD_EXEC_CALLBACK Function, void* Context);

extern void AcpiOsSleep(UINT64 Milliseconds);

extern void AcpiOsStall(UINT32 Microseconds);

extern ACPI_STATUS AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits, ACPI_SEMAPHORE* OutHandle);

extern ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle);

extern ACPI_STATUS AcpiOsWaitSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units, UINT16 Timeout);

extern ACPI_STATUS AcpiOsSignalSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units);

extern ACPI_STATUS AcpiOsCreateLock(ACPI_SPINLOCK* OutHandle);

extern void AcpiOsDeleteLock(ACPI_HANDLE Handle);

extern ACPI_CPU_FLAGS AcpiOsAcquireLock(ACPI_SPINLOCK Handle);

extern void AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags);

extern ACPI_STATUS AcpiOsInstallInterruptHandler(UINT32 InterruptLevel, ACPI_OSD_HANDLER Handler, void* Context);

extern ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 InterruptNumber, ACPI_OSD_HANDLER Handler);

// extern ACPI_STATUS AcpiOsCreateMutex(ACPI_MUTEX *OutHandle);
// extern void AcpiOsDeleteMutex(ACPI_MUTEX Handle);
// extern ACPI_STATUS AcpiOsAcquireMutex(ACPI_MUTEX Handle, UINT16 Timeout);
// extern void AcpiOsReleaseMutex(ACPI_MUTEX Handle);

#endif // __ACWALLOSEX_H__