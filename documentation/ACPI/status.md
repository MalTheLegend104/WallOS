# ACPICA OS Layer Implementation Status

Most of this page is courtesy of [this osedev.wiki page.](https://osdev.wiki/wiki/ACPICA)

> ACPICA provides a ACPI parser that allows you to get information from the tables without doing much else. it *shouldn't* rely on anything below. this is really what we care about the most right now. A lot of these are just going to be stubs, and we'll print a message and hlt when they called.

## Table of Contents

- [Env](#env)
- [Memory](#memory)
- [Multithreading](#multithreading)
- [Mutexes and Spinlocks](#mutual-exclusion-and-synchronization)
- [Interrupt Handling](#interrupt-handling)

## Env

- [ ] `ACPI_STATUS AcpiOsInitialize()`
  - This is called during ACPICA Initialization. It gives the possibility to the OSL to initialize itself. Generally it should do nothing.
- [ ] `ACPI_STATUS AcpiOsTerminate()`
  - This is called during ACPICA Shutdown (which is not the computer shutdown, just the ACPI). Here you can free any memory which was allocated in AcpiOsInitialize.
  - This will do absolutely nothing on WallOS.
- [ ] `ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer()`
  - ACPICA leaves to you the job of finding the RSDP for platform compatibility.
  - This can be done using AcpiFindRootPointer(), but we also get the RSDP from GRUB.
- [ ] `ACPI_STATUS AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES *PredefinedObject, ACPI_STRING *NewValue)`
  - This function allows the host to override the predefined objects in the ACPI namespace. It is called when a new object is found in the ACPI namespace. However you can just put NULL in *NewValue and return.
- [ ] `ACPI_STATUS AcpiOsTableOverride(ACPI_TABLE_HEADER *ExistingTable, ACPI_TABLE_HEADER **NewTable)`
  - The same of AcpiOsPredefinedOverride but for entire ACPI tables. You can replace them. Just put NULL in *NewTable and return.

## Memory

- [ ] `void *AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS PhysicalAddress, ACPI_SIZE Length)`
  - This is not really easy. ACPICA is asking you to map a physical address in the virtual address space. If you don't use paging, just return PhysicalAddress. You need:
      1. To round Length up to the size of a page (Length can be 2, 1024 for example)
      2. Find a range of virtual addresses where map the physical frames.
      3. Map the physical frames to the virtual addresses chosen.
      4. Return the virtual address plus the page offset of the physical address. (Eg. If you where asked to map 0x40E you have to return 0xF000040E and not just 0xF0000000)
- [ ] `void AcpiOsUnmapMemory(void *where, ACPI_SIZE length)`
  - Unmap pages mapped using AcpiOsMapMemory. Where is the Virtual address returned in AcpiOsMapMemory and length is equal to the length of the same function. Just remove the virtual address form the page directory and set that virtual address as reusable. Note: for the last two functions you might need a separated heap.
- [ ] `ACPI_STATUS AcpiOsGetPhysicalAddress(void *LogicalAddress, ACPI_PHYSICAL_ADDRESS *PhysicalAddress)`
  - Get the physical address pointed by LogicalAddress and put it in `*PhysicalAddress`. If you do not use paging just put LogicalAddress in `*PhysicalAddress`
- [ ] `void *AcpiOsAllocate(ACPI_SIZE Size);`
  - literally just `return malloc(size)` (although it'll be kalloc)
- [ ] `void AcpiOsFree(void *Memory);`
  - literally just call kfree
- [ ] `BOOLEAN AcpiOsReadable(void *Memory, ACPI_SIZE Length)`
  - in theory never used. mostly asking that the memory + length doesn't overrun a page boundary and is readable
- [ ] `BOOLEAN AcpiOsWritable(void *Memory, ACPI_SIZE Length)`
  - Same as the last one, but with writable. ACPICA is running in the kernel, it is always writable if readable.
- [ ] caches
  - We have the option of using our own caches. we wont.

  ```c
  #define ACPI_CACHE_T                ACPI_MEMORY_LIST
  #define ACPI_USE_LOCAL_CACHE        1
  ```

## Multithreading

We wont get here for a long time. We'll probably still need stubs.

- [ ] `ACPI_THREAD_ID AcpiOsGetThreadId()`
  - Does what it says. behaves like `pthread_self()`;
- [ ] `ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE Type, ACPI_OSD_EXEC_CALLBACK Function, void *Context)`
  - Create a new thread (or process) with entry point at Function using parameter Context. Type is not really useful. When the scheduler chooses this thread it has to pass in Context to the first argument (RDI for x86-64, stack for x86-32 (using System V ABI) to have something like: `Function(Context);`
- [ ] `void AcpiOsSleep(UINT64 Milliseconds)`
  - typical sleep function
- [ ] `void AcpiOsStall(UINT32 Microseconds)`
  - like sleep but doesn't put the thread in the queue. should just loop the thread.

## Mutual Exclusion and Synchronization

same as above. probably need stubs, don't know about usage.

- [ ] `ACPI_STATUS AcpiOsCreateMutex(ACPI_MUTEX *OutHandle)`
  - Create space for a new Mutex using malloc (or eventually new) and put the address of the Mutex in *OutHandle, return AE_NO_MEMORY if malloc or new return NULL. Else return AE_OK like in most other functions.
- [ ] `void AcpiOsDeleteMutex(ACPI_MUTEX Handle)`
- [ ] `ACPI_STATUS AcpiOsAcquireMutex(ACPI_MUTEX Handle, UINT16 Timeout)`
  - This would be silly too if not for the Timeout parameter. Timeout can be one of:  
    0: acquire the Mutex if it is free, but do not wait if it is not  
    1 - +inf: acquire the Mutex if it is free, but wait for Timeout milliseconds if it is not  
    -1 (0xFFFF): acquire the Mutex if it is free, or wait until it became free, then return
- [ ] `void AcpiOsReleaseMutex(ACPI_MUTEX Handle)`
- [ ] `ACPI_STATUS AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits, ACPI_SEMAPHORE *OutHandle)`
- Create a new Semaphore with the counter initialized to InitialUnits and put its address in *OutHandle. I don't know how tu use MaxUnits. The spec says: The maximum number of units this Semaphore will be required to accept.
However you should be ok if you ignore this.
- [ ] `ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle)`
- [ ] `ACPI_STATUS AcpiOsWaitSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units, UINT16 Timeout)`
  - Just like AcpiOsAcquireMutex, same logic for Timeout. Units isn't used in the linux implementation. However it should be the number of times you have to call sem_wait. I'm not sure about this.
- [ ] `ACPI_STATUS AcpiOsSignalSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units)`
  - Opposite of Wait. Units: number of times you should call sem_post.
- [ ] `ACPI_STATUS AcpiOsCreateLock(ACPI_SPINLOCK *OutHandle)`
  - Create a new spinlock and put its address in *OutHandle. Spinlock should disable interrupts on the current CPU to avoid scheduling and make sure that no other CPU will access the reserved area.
- [ ] `void AcpiOsDeleteLock(ACPI_HANDLE Handle)`
- [ ] `ACPI_CPU_FLAGS AcpiOsAcquireLock(ACPI_SPINLOCK Handle)`
  - Lock the spinlock and return a value that will be used as parameter for ReleaseLock. It is mainly used for the status of interrupts before the lock was acquired.
- [ ] `void AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags)`
  - Release the lock. Flags is the return value of AcquireLock. If you used this to store the interrupt state, now is the moment to use it.

## Interrupt Handling

- [ ] `ACPI_STATUS AcpiOsInstallInterruptHandler(UINT32 InterruptLevel, ACPI_OSD_HANDLER Handler, void *Context)`
- ACPI sometimes fires interrupt. ACPICA will take care of them. InterruptLevel is the IRQ number that ACPI will use. Handler is an internal function of ACPICA which handles interrupts. Context is the parameter to be past to the Handler. If you're lucky, your IRQ manager uses handlers of this form: `uint32_t handler(void *);` (WallOS doesn't. We'll cross this road when we get there.)
- [ ] `ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 InterruptNumber, ACPI_OSD_HANDLER Handler)`
  - Just UnregisterIrq (InterruptNumber). Handler is provided in case you have an IRQ manager which can have many handlers for one IRQ. This would let you know which handler on that IRQ you have to remove. (basically just unregister the interrupt. WallOS doesn't allow multiple IRQs per number.)
