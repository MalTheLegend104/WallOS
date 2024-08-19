#include <panic.h>
#include <acpi.h>

#include <actypes.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

#include <klibc/logger.h>

#pragma GCC diagnostic ignored "-Wunused-parameter" 

// All of these are just stubs for ACPICA.
// For the purposes of what we're doing right now, it shouldn't need these.
// We only really use the ACPICA subsystem for table parsing right now.

void acpica_failure(const char* str) {
	const char* msg[] = { "ACPICA called a function stub: ", str };
	panic_sa(msg, 2);
}

ACPI_STATUS AcpiOsWritePciConfiguration(ACPI_PCI_ID* pciId, UINT32 reg, UINT64 value, UINT32 width) {
	acpica_failure(__func__);
	return 0;
}

ACPI_STATUS AcpiOsSignal(UINT32 function, void* info) {
	acpica_failure(__func__);
	return 0;
}

UINT64 AcpiOsGetTimer(void) {
	acpica_failure(__func__);
	return 0;
}

ACPI_STATUS AcpiOsPhysicalTableOverride(ACPI_TABLE_HEADER* existingTable, ACPI_PHYSICAL_ADDRESS* newAddress, UINT32* newTableLength) {
	acpica_failure(__func__);
	return 0;
}

ACPI_STATUS AcpiOsWritePort(ACPI_IO_ADDRESS address, UINT32 value, UINT32 width) {
	acpica_failure(__func__);
	return 0;
}

ACPI_STATUS AcpiOsReadPort(ACPI_IO_ADDRESS address, UINT32* value, UINT32 width) {
	acpica_failure(__func__);
	return 0;
}

ACPI_STATUS AcpiOsWriteMemory(ACPI_PHYSICAL_ADDRESS address, UINT64 value, UINT32 width) {
	acpica_failure(__func__);
	return 0;
}

ACPI_STATUS AcpiOsReadMemory(ACPI_PHYSICAL_ADDRESS address, UINT64* value, UINT32 width) {
	acpica_failure(__func__);
	return 0;
}

ACPI_STATUS AcpiOsReadPciConfiguration(ACPI_PCI_ID* pciId, UINT32 reg, UINT64* value, UINT32 width) {
	acpica_failure(__func__);
	return 0;
}

void AcpiOsWaitEventsComplete(void) {
	acpica_failure(__func__);
}

void AcpiOsVprintf(const char* format, va_list args) {
	vprintf(format, args);
}

void AcpiOsPrintf(const char* format, ...) {
	va_list arg;
	va_start(arg, format);
	vprintf(format, arg);
	//ret = temp(format, arg);
	va_end(arg);
}

ACPI_STATUS AcpiOsInitialize() {
	logger(INFO, "ACPICA called OS init.\n");
	return AE_OK;
}

ACPI_STATUS AcpiOsTerminate() {
	acpica_failure(__func__);
	return AE_OK;
}


#include <klibc/multiboot.h>
ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer() {
	return getAcpiRoot();
}

ACPI_STATUS AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES* PredefinedObject, ACPI_STRING* NewValue) {
	acpica_failure(__func__);
	*NewValue = NULL;
	return AE_OK;
}

ACPI_STATUS AcpiOsTableOverride(ACPI_TABLE_HEADER* ExistingTable, ACPI_TABLE_HEADER** NewTable) {
	acpica_failure(__func__);
	*NewTable = NULL;
	return AE_OK;
}

// Memory
void* AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS PhysicalAddress, ACPI_SIZE Length) {
	acpica_failure(__func__);
	return NULL;
}

void AcpiOsUnmapMemory(void* where, ACPI_SIZE length) {
	acpica_failure(__func__);
}

ACPI_STATUS AcpiOsGetPhysicalAddress(void* LogicalAddress, ACPI_PHYSICAL_ADDRESS* PhysicalAddress) {
	acpica_failure(__func__);
	return AE_OK;
}

#include <memory/kernel_alloc.h>

void* AcpiOsAllocate(ACPI_SIZE Size) {
	void* ptr = kalloc(Size);
	logger(INFO, "ACPICA called OS Allocate for size: 0x%X. Returning pointer: 0x%X\n", Size, ptr);

	return ptr;
}

void AcpiOsFree(void* Memory) {
	printf("ACPICA called OS Free.\n");
	kfree(Memory);
}

BOOLEAN AcpiOsReadable(void* Memory, ACPI_SIZE Length) {
	acpica_failure(__func__);
	return FALSE;
}

BOOLEAN AcpiOsWritable(void* Memory, ACPI_SIZE Length) {
	acpica_failure(__func__);
	return FALSE;
}

// Multithreading
ACPI_THREAD_ID AcpiOsGetThreadId() {
	logger(WARN, "ACPICA requested thread ID.\n");
	return 1;
}

ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE Type, ACPI_OSD_EXEC_CALLBACK Function, void* Context) {
	acpica_failure(__func__);
	return AE_OK;
}

#include <system/timing.h>
void AcpiOsSleep(UINT64 Milliseconds) {
	sleep(Milliseconds);
}

void AcpiOsStall(UINT32 Microseconds) {
	acpica_failure(__func__);
}

// // Mutexes and Spinlocks
// ACPI_STATUS AcpiOsCreateMutex(ACPI_MUTEX* OutHandle) {
// 	acpica_failure(__func__);
// 	return AE_OK;
// }

// void AcpiOsDeleteMutex(ACPI_MUTEX Handle) {
// 	acpica_failure(__func__);
// }

// ACPI_STATUS AcpiOsAcquireMutex(ACPI_MUTEX Handle, UINT16 Timeout) {
// 	acpica_failure(__func__);
// 	return AE_OK;
// }

// void AcpiOsReleaseMutex(ACPI_MUTEX Handle) {
// 	acpica_failure(__func__);
// }

#include <memory/semaphore.h>

ACPI_STATUS AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits, ACPI_SEMAPHORE* OutHandle) {
	if (OutHandle == NULL) {
		return AE_BAD_PARAMETER;
	}

	semaphore_t* sem = semaphore_create(MaxUnits, InitialUnits);
	if (sem == NULL) {
		return AE_NO_MEMORY;
	}

	*OutHandle = sem;
	return AE_OK;
}

ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle) {
	semaphore_destroy((semaphore_t*) Handle);
	return AE_OK;
}

ACPI_STATUS AcpiOsWaitSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units, UINT16 Timeout) {
	if (Handle == NULL) return AE_BAD_PARAMETER;

	uint64_t time = Timeout;
	if (Timeout == 0xFFFF) time = UINT64_MAX;

	int status = semaphore_wait((semaphore_t*) Handle, Units, time);
	if (status == SEMAPHORE_TIMEOUT) return AE_TIME;

	return AE_OK;
}

ACPI_STATUS AcpiOsSignalSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units) {
	if (Handle == NULL) return AE_BAD_PARAMETER;

	int status = semaphore_signal((semaphore_t*) Handle, Units);
	if (status == SEMAPHORE_OVER_MAX) return AE_LIMIT;
	return AE_OK;
}

#include <memory/spinlock.h>
ACPI_STATUS AcpiOsCreateLock(ACPI_SPINLOCK* OutHandle) {
	if (OutHandle == NULL) return AE_BAD_PARAMETER;

	spinlock_t* spinlock = spinlock_create();
	if (spinlock == NULL) return AE_NO_MEMORY;

	*OutHandle = spinlock;

	return AE_OK;
}

void AcpiOsDeleteLock(ACPI_HANDLE Handle) {
	spinlock_destroy(Handle);
}

ACPI_CPU_FLAGS AcpiOsAcquireLock(ACPI_SPINLOCK Handle) {
	if (Handle == NULL) return AE_OK;

	spinlock_lock(Handle);

	return AE_OK;
}

void AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags) {
	if (Handle == NULL) return;
	spinlock_unlock(Handle);
}

// Interrupt Handling
ACPI_STATUS AcpiOsInstallInterruptHandler(UINT32 InterruptLevel, ACPI_OSD_HANDLER Handler, void* Context) {
	acpica_failure(__func__);
	return AE_OK;
}

ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 InterruptNumber, ACPI_OSD_HANDLER Handler) {
	acpica_failure(__func__);
	return AE_OK;
}
