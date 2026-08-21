#ifdef WALLOS_USE_ACPICA
#include <panic.h>
#include <acpi.h>

#include <actypes.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

#include <system/timer.h>

#include <klibc/logger.h>
#include <drivers/serial.h>	
#include <memory/virtual_mem.h>

#pragma GCC diagnostic ignored "-Wunused-parameter" 

// All of these are just stubs for ACPICA.
// For the purposes of what we're doing right now, it shouldn't need these.
// We only really use the ACPICA subsystem for table parsing right now.

void acpi_vlogger(LogType type, const char* fmt, va_list args) {
	switch (type) {
		case LOG: 	printf("[ACPICA][LOG] ");	vprintf_color(PRINT_COLOR_DARK_GREY, PRINT_DEFAULT_BG, fmt, args); 	break;
		case INFO: 	printf("[ACPICA][INFO] ");	vprintf_color(PRINT_COLOR_CYAN, PRINT_DEFAULT_BG, fmt, args); 		break;
		case WARN: 	printf("[ACPICA][WARN] ");	vprintf_color(PRINT_COLOR_YELLOW, PRINT_DEFAULT_BG, fmt, args); 	break;
		case ERROR: printf("[ACPICA][ERROR] ");	vprintf_color(PRINT_COLOR_LIGHT_RED, PRINT_DEFAULT_BG, fmt, args); 	break;
		case FATAL: printf("[ACPICA][FATAL] ");	vprintf_color(PRINT_COLOR_RED, PRINT_DEFAULT_BG, fmt, args); 		break;

		default: vprintf(fmt, args);	break;
	}
}
#include <drivers/serial.h>
void acpi_logger(LogType type, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vlogger(type, fmt, args);
	// vprintf_serial(fmt, args);
	va_end(args);
}

void acpica_failure(const char* str) {
	const char* msg[] = { "ACPICA called a function stub: ", str };

	printf("ACPICA called stub function %s\n", str);

	asm volatile("cli");
	asm volatile("hlt");
	panic_sa(msg, 2);
}

ACPI_STATUS AcpiOsSignal(UINT32 function, void* info) {
	acpica_failure(__func__);
	return 0;
}

UINT64 AcpiOsGetTimer(void) {
	// ACPI wants 100-nanosecond units
	return timer_uptime_no_interrupts() / 100;
}

ACPI_STATUS AcpiOsPhysicalTableOverride(ACPI_TABLE_HEADER* existingTable, ACPI_PHYSICAL_ADDRESS* newAddress, UINT32* newTableLength) {
	*newAddress = 0;
	return 0;
}

#include <cpu_io.h>

ACPI_STATUS AcpiOsWritePort(ACPI_IO_ADDRESS Address, UINT32 Value, UINT32 Width) {
	// Optional: Log for debugging
	printf_serial("[ACPICA] ACPI_IO: Write 0x%X to Port 0x%llx\r\n", Value, (uint64_t) Address);

	switch (Width) {
		case 8:
			outb(Address, (uint8_t) Value);
			break;
		case 16:
			outw(Address, (uint16_t) Value);
			break;
		case 32:
			outl(Address, (uint32_t) Value);
			break;
		default:
			return AE_BAD_PARAMETER;
	}

	return AE_OK;
}


ACPI_STATUS AcpiOsReadPort(ACPI_IO_ADDRESS Address, UINT32* Value, UINT32 Width) {
	switch (Width) {
		case 8:
			*Value = inb(Address);
			break;
		case 16:
			*Value = inw(Address);
			break;
		case 32:
			*Value = inl(Address);
			break;
		default:
			return AE_BAD_PARAMETER;
	}

	// Optional: Log for debugging
	printf_serial("[ACPICA] ACPI_IO: Read 0x%X from Port 0x%llx\r\n", *Value, (uint64_t) Address);

	return AE_OK;
}

ACPI_STATUS AcpiOsWriteMemory(ACPI_PHYSICAL_ADDRESS address, UINT64 value, UINT32 width) {
	acpica_failure(__func__);
	return 0;
}

ACPI_STATUS AcpiOsReadMemory(ACPI_PHYSICAL_ADDRESS address, UINT64* value, UINT32 width) {
	acpica_failure(__func__);
	return 0;
}

#include <drivers/pci.h>

ACPI_STATUS AcpiOsReadPciConfiguration(ACPI_PCI_ID* pciId, UINT32 reg, UINT64* value, UINT32 width) {
	if (!pciId || !value) {
		return AE_BAD_PARAMETER;
	}

	uint8_t bus = (uint8_t) pciId->Bus;
	uint8_t slot = (uint8_t) pciId->Device;
	uint8_t func = (uint8_t) pciId->Function;
	uint8_t offset = (uint8_t) reg;

	switch (width) {
		case 8:
			*value = pci_config_read8(bus, slot, func, offset);
			break;
		case 16:
			*value = pci_config_read16(bus, slot, func, offset);
			break;
		case 32:
			*value = pci_config_read32(bus, slot, func, offset);
			break;
		default:
			return AE_BAD_PARAMETER;
	}

	return AE_OK;
}

ACPI_STATUS AcpiOsWritePciConfiguration(ACPI_PCI_ID* pciId, UINT32 reg, UINT64 value, UINT32 width) {
	if (!pciId) {
		return AE_BAD_PARAMETER;
	}

	uint8_t bus = (uint8_t) pciId->Bus;
	uint8_t slot = (uint8_t) pciId->Device;
	uint8_t func = (uint8_t) pciId->Function;
	uint8_t offset = (uint8_t) reg;

	switch (width) {
		case 8:
			pci_config_write8(bus, slot, func, offset, (uint8_t) value);
			break;
		case 16:
			pci_config_write16(bus, slot, func, offset, (uint16_t) value);
			break;
		case 32:
			pci_config_write32(bus, slot, func, offset, (uint32_t) value);
			break;
		default:
			return AE_BAD_PARAMETER;
	}

	return AE_OK;
}


void AcpiOsWaitEventsComplete(void) {
	acpica_failure(__func__);
}

void AcpiOsVprintf(const char* format, va_list args) {
	// We want to copy the contents from vprintf to the serial console.
	// We have to copy the va_list
	// This caused me many hours of pain only for me to realize using the va_list clobbers it.
	va_list args_copy;
	va_copy(args_copy, args);

	vprintf_color(PRINT_COLOR_GREEN, PRINT_DEFAULT_BG, format, args);

	printf_serial("\r\n");
	vprintf_serial(format, args_copy);

	va_end(args_copy);
}

void AcpiOsPrintf(const char* format, ...) {
	va_list arg;
	va_list arg_copy;

	va_start(arg, format);
	va_copy(arg_copy, arg);

	vprintf_color(PRINT_COLOR_GREEN, PRINT_DEFAULT_BG, format, arg);

	printf_serial("\r\n");
	vprintf_serial(format, arg_copy);

	va_end(arg_copy);
	va_end(arg);
}

ACPI_STATUS AcpiOsInitialize() {
	acpi_logger(INFO, "ACPICA called OS init.\n");
	return AE_OK;
}

ACPI_STATUS AcpiOsTerminate() {
	acpica_failure(__func__);
	return AE_OK;
}


#include <klibc/multiboot.h>
ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer() {
	return (uintptr_t) getAcpiRoot();
}

ACPI_STATUS AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES* PredefinedObject, ACPI_STRING* NewValue) {
	// acpica_failure(__func__);
	*NewValue = NULL;
	return AE_OK;
}

ACPI_STATUS AcpiOsTableOverride(ACPI_TABLE_HEADER* ExistingTable, ACPI_TABLE_HEADER** NewTable) {
	// acpica_failure(__func__);
	*NewTable = NULL;
	return AE_OK;
}

// Memory
void* AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS PhysicalAddress, ACPI_SIZE Length) {
	if (PhysicalAddress >= KERNEL_VIRTUAL_BASE) return (void*) PhysicalAddress; // It's already mapped.

	// Arbitrary Cutoff, 256 continuous MB.
	if (Length > 0x200000 * 128) {
		// Map only the requested location, get the table header, return 0.
		char* magic = (char*) mapKernelLocation(PhysicalAddress, 0x24);
		acpi_logger(WARN, "Very long memory map request. \n\t\tTarget Signature: \"%c%c%c%c\"\n", magic[0], magic[1], magic[2], magic[3]);
		return 0;
	}

	void* ret = (void*) mapKernelLocation(PhysicalAddress, Length);

	// printf_serial("\r\nMAP REQUEST:\r\n\tRequest PHYS: 0x%llx\r\n\tRequest LEN:  0x%llx\r\n\tMapped Return: 0x%llx\r\n", PhysicalAddress, Length, ret);
	// printf("\nMAP REQUEST:\n\tRequest PHYS: 0x%llx\n\tRequest LEN:  0x%llx\n\tMapped Return: 0x%llx\n", PhysicalAddress, Length, ret);

	return ret;
}

void AcpiOsUnmapMemory(void* where, ACPI_SIZE length) {
	// I dont really care about unmapping right now. 
	// printf_serial("UNMAP:\r\n\tMem Addr: 0x%llx\r\n\tLen: 0x%llx\r\n", where, length);
}

ACPI_STATUS AcpiOsGetPhysicalAddress(void* LogicalAddress, ACPI_PHYSICAL_ADDRESS* PhysicalAddress) {
	acpica_failure(__func__);
	return AE_OK;
}

#include <memory/kernel_alloc.h>

void* AcpiOsAllocate(ACPI_SIZE Size) {
	void* ptr = kalloc(Size);
	//acpi_logger(INFO, "ACPICA called OS Allocate for size: 0x%llx. Returning pointer: 0x%llx\n", Size, ptr);

	return ptr;
}

void AcpiOsFree(void* Memory) {
	//printf("ACPICA called OS Free.\n");
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
	// This just made me mad so it's commented
	//printf_serial("[WARN] ACPICA requested ThreadID.\r\n");
	return 1;
}

typedef struct {
	ACPI_OSD_EXEC_CALLBACK function;
	void* context;
} acpi_deferred_work_t;

#define ACPI_DEFERRED_QUEUE_SIZE 16
static acpi_deferred_work_t deferred_queue[ACPI_DEFERRED_QUEUE_SIZE];
static volatile uint32_t deferred_head = 0;
static volatile uint32_t deferred_tail = 0;

ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE Type, ACPI_OSD_EXEC_CALLBACK Function, void* Context) {
	uint32_t next = (deferred_tail + 1) % ACPI_DEFERRED_QUEUE_SIZE;
	if (next == deferred_head) {
		return AE_NO_MEMORY; // queue full
	}
	deferred_queue[deferred_tail].function = Function;
	deferred_queue[deferred_tail].context = Context;
	deferred_tail = next;
	return AE_OK;
}

void acpi_process_deferred_work(void) {
	while (deferred_head != deferred_tail) {
		acpi_deferred_work_t work = deferred_queue[deferred_head];
		deferred_head = (deferred_head + 1) % ACPI_DEFERRED_QUEUE_SIZE;
		if (work.function) work.function(work.context);
	}
}

void AcpiOsSleep(UINT64 Milliseconds) {
	busy_wait_ms(Milliseconds);
}

void AcpiOsStall(UINT32 Microseconds) {
	uint64_t start_ns = timer_uptime_no_interrupts();
	uint64_t wait_ns = Microseconds * 1000ull;

	while ((timer_uptime_no_interrupts() - start_ns) < wait_ns) {
		__asm__ volatile ("pause");
	}
}

ACPI_STATUS AcpiOsEnterSleep(UINT8 SleepState, UINT32 RegaValue, UINT32 RegbValue) {

	// Log what's happening 
	printf_serial("Entering sleep state S%u (PM1a=0x%X, PM1b=0x%X)\n", SleepState, RegaValue, RegbValue);

	// Sleep state should be one of these
	// We only use state 5 (full shutdown) right now.
	switch (SleepState) {
		case 1: // S1 - CPU sleep
		case 2: // S2 - CPU sleep + some devices off
		case 3: // S3 - Suspend to RAM
			// You might want to flush caches, save state, etc.
			break;

		case 4: // S4 - Suspend to disk (hibernation)
			// Save all memory to disk if you support hibernation
			break;

		case 5: // S5 - Soft off (shutdown)
			// Final cleanup before power off
			// Disable interrupts (should already be done)
			asm volatile("cli");
			break;
	}

	return AE_OK;
}

// Mutexes and Spinlocks
// Mutexes (using the Semaphore primitives)
ACPI_STATUS AcpiOsCreateMutex(ACPI_MUTEX* OutHandle) {
	// A Mutex is a semaphore with Max 1, Initial 1
	return AcpiOsCreateSemaphore(1, 1, (ACPI_SEMAPHORE*) OutHandle);
}

void AcpiOsDeleteMutex(ACPI_MUTEX Handle) {
	// Directly use the semaphore delete logic
	AcpiOsDeleteSemaphore((ACPI_SEMAPHORE) Handle);
}

ACPI_STATUS AcpiOsAcquireMutex(ACPI_MUTEX Handle, UINT16 Timeout) {
	// Mutexes always wait for 1 unit
	return AcpiOsWaitSemaphore((ACPI_SEMAPHORE) Handle, 1, Timeout);
}

void AcpiOsReleaseMutex(ACPI_MUTEX Handle) {
	// Signal 1 unit back to the semaphore
	AcpiOsSignalSemaphore((ACPI_SEMAPHORE) Handle, 1);
}

#include <memory/semaphore.h>

ACPI_STATUS AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits, ACPI_SEMAPHORE* OutHandle) {
	//printf_serial("ACPICA requested semaphore.\r\n");
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



// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Interrupts
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
#include <acpi.h>
#include <system/idt.h>
#include <klibc/logger.h>

#define MAX_ACPI_IRQS        16
#define MAX_HANDLERS_PER_IRQ 8

struct acpi_irq_handler {
	ACPI_OSD_HANDLER handler;
	void* ctx;
};

struct acpi_irq_info {
	bool installed;
	size_t count;
	struct acpi_irq_handler handlers[MAX_HANDLERS_PER_IRQ];
};

static struct acpi_irq_info acpi_irq_table[MAX_ACPI_IRQS];

// This one is left here, not as a macro, just so it's obvious as to what it's doing.
WALLOS_INTERRUPT_HANDLER void acpi_irq_wrapper_0(struct interrupt_frame* frame) {
	(void) frame;
	bool handled = false;

	struct acpi_irq_info* irq = &acpi_irq_table[0];

	for (size_t i = 0; i < irq->count; i++) {
		if (irq->handlers[i].handler(irq->handlers[i].ctx) == ACPI_INTERRUPT_HANDLED) handled = true;
	}

	(void) handled; // why did we ever have this?

	/* EOI once, after all handlers. This one doesn't need to send anything to the slave PIC. */
	interrupt_eoi(0);
}

#define DEFINE_ACPI_IRQ_WRAPPER(n) WALLOS_INTERRUPT_HANDLER void acpi_irq_wrapper_##n(struct interrupt_frame *frame) { (void) frame; bool handled = false; struct acpi_irq_info *irq = &acpi_irq_table[n]; for (size_t i = 0; i < irq->count; i++) { if (irq->handlers[i].handler(irq->handlers[i].ctx) == ACPI_INTERRUPT_HANDLED) { handled = true; }} (void) handled;interrupt_eoi(n); }
DEFINE_ACPI_IRQ_WRAPPER(1)
DEFINE_ACPI_IRQ_WRAPPER(2)
DEFINE_ACPI_IRQ_WRAPPER(3)
DEFINE_ACPI_IRQ_WRAPPER(4)
DEFINE_ACPI_IRQ_WRAPPER(5)
DEFINE_ACPI_IRQ_WRAPPER(6)
DEFINE_ACPI_IRQ_WRAPPER(7)
DEFINE_ACPI_IRQ_WRAPPER(8)
DEFINE_ACPI_IRQ_WRAPPER(9)
DEFINE_ACPI_IRQ_WRAPPER(10)
DEFINE_ACPI_IRQ_WRAPPER(11)
DEFINE_ACPI_IRQ_WRAPPER(12)
DEFINE_ACPI_IRQ_WRAPPER(13)
DEFINE_ACPI_IRQ_WRAPPER(14)
DEFINE_ACPI_IRQ_WRAPPER(15)

static void (*acpi_irq_wrappers[])(struct interrupt_frame*) = {
	acpi_irq_wrapper_0,
	acpi_irq_wrapper_1,
	acpi_irq_wrapper_2,
	acpi_irq_wrapper_3,
	acpi_irq_wrapper_4,
	acpi_irq_wrapper_5,
	acpi_irq_wrapper_6,
	acpi_irq_wrapper_7,
	acpi_irq_wrapper_8,
	acpi_irq_wrapper_9,
	acpi_irq_wrapper_10,
	acpi_irq_wrapper_11,
	acpi_irq_wrapper_12,
	acpi_irq_wrapper_13,
	acpi_irq_wrapper_14,
	acpi_irq_wrapper_15,
};

ACPI_STATUS AcpiOsInstallInterruptHandler(UINT32 irq, ACPI_OSD_HANDLER handler, void* ctx) {
	if (irq >= MAX_ACPI_IRQS || !handler) {
		acpi_logger(ERROR, "ACPICA: Invalid IRQ %u\n", irq);
		return AE_BAD_PARAMETER;
	}

	struct acpi_irq_info* info = &acpi_irq_table[irq];

	if (info->count >= MAX_HANDLERS_PER_IRQ) {
		acpi_logger(ERROR, "ACPICA: Too many handlers for IRQ %u\n", irq);
		return AE_LIMIT;
	}

	/* Install IDT handler once */
	if (!info->installed) {
		uint8_t vector = 0x20 + irq;


		acpi_logger(INFO, "ACPICA: Installing IRQ %u (vector 0x%x)\n", irq, vector);
		// printf_serial("ACPICA: Installing IRQ %u (vector 0x%x)\r\n", irq, vector);

		add_interrupt_handler(vector, acpi_irq_wrappers[irq], 0, 0x8E);
		irq_enable(irq);
		if (irq == 9) {
			irq_set_level_triggered(9);
		}
		info->installed = true;
	}

	info->handlers[info->count++] = (struct acpi_irq_handler){
		.handler = handler,
		.ctx = ctx
	};

	return AE_OK;
}

ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 irq, ACPI_OSD_HANDLER handler) {
	if (irq >= MAX_ACPI_IRQS || !handler) {
		acpi_logger(ERROR, "ACPICA: Invalid IRQ remove\n");
		return AE_BAD_PARAMETER;
	}

	struct acpi_irq_info* info = &acpi_irq_table[irq];

	for (size_t i = 0; i < info->count; i++) {
		if (info->handlers[i].handler == handler) {

			memmove(&info->handlers[i],
				&info->handlers[i + 1],
				(info->count - i - 1) *
				sizeof(struct acpi_irq_handler));

			info->count--;
			break;
		}
	}

	/* Disable IRQ if no handlers remain */
	if (info->count == 0 && info->installed) {
		uint8_t vector = 0x20 + irq;

		acpi_logger(INFO, "ACPICA: Removing IRQ %u\n", irq);

		irq_disable(irq);
		remove_interrupt_handler(vector);
		info->installed = false;
	}

	return AE_OK;
}


// // Interrupt Handling
// ACPI_STATUS AcpiOsInstallInterruptHandler(UINT32 InterruptLevel, ACPI_OSD_HANDLER Handler, void* Context) {
// 	acpica_failure(__func__);
// 	return AE_OK;
// }

// ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 InterruptNumber, ACPI_OSD_HANDLER Handler) {
// 	acpica_failure(__func__);
// 	return AE_OK;
// }

#endif //WALLOS_USE_ACPICA