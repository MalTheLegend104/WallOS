#include <uacpi/acpi.h>
#include <uacpi/kernel_api.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

#include <panic.h>

#include <klibc/logger.h>
#include <drivers/serial.h>	
#include <memory/virtual_mem.h>
#include <cpu_io.h>

#include <system/timer.h>

// There's a lot of unused params in here
#pragma GCC diagnostic ignored "-Wunused-parameter" 

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// General helper functions
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void uacpi_failure(const char* str) {
	const char* msg[] = { "UACPI called a function stub: ", str };

	printf("UACPI called stub function %s\n", str);

	asm volatile("cli");
	asm volatile("hlt");
	panic_sa(msg, 2);
}

void uacpi_printf(vga_color color, const char* fmt, const char* str) {
	printf_color(color, PRINT_DEFAULT_BG, fmt, str);
}

// Returns the PHYSICAL address of the RSDP structure via *out_rsdp_address.
#include <klibc/multiboot.h>
uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out_rsdp_address) {
	// printf_serial("[UACPI] uacpi_kernel_get_rsdp() called\r\n");
	*out_rsdp_address = (uacpi_phys_addr) getAcpiRoot();
	// printf_serial("[UACPI] uacpi_kernel_get_rsdp() returning 0x%llx\r\n", *out_rsdp_address);
	return UACPI_STATUS_OK;
}

/*
 * Map a physical memory range starting at 'addr' with length 'len', and return
 * a virtual address that can be used to access it.
 *
 * NOTE: 'addr' may be misaligned, in this case the host is expected to round it
 *       down to the nearest page-aligned boundary and map that, while making
 *       sure that at least 'len' bytes are still mapped starting at 'addr'. The
 *       return value preserves the misaligned offset.
 *
 *       Example for uacpi_kernel_map(0x1ABC, 0xF00):
 *           1. Round down the 'addr' we got to the nearest page boundary.
 *              Considering a PAGE_SIZE of 4096 (or 0x1000), 0x1ABC rounded down
 *              is 0x1000, offset within the page is 0x1ABC - 0x1000 => 0xABC
 *           2. Requested 'len' is 0xF00 bytes, but we just rounded the address
 *              down by 0xABC bytes, so add those on top. 0xF00 + 0xABC => 0x19BC
 *           3. Round up the final 'len' to the nearest PAGE_SIZE boundary, in
 *              this case 0x19BC is 0x2000 bytes (2 pages if PAGE_SIZE is 4096)
 *           4. Call the VMM to map the aligned address 0x1000 (from step 1)
 *              with length 0x2000 (from step 3). Let's assume the returned
 *              virtual address for the mapping is 0xF000.
 *           5. Add the original offset within page 0xABC (from step 1) to the
 *              resulting virtual address 0xF000 + 0xABC => 0xFABC. Return it
 *              to uACPI.
 */
// void* uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
// 	// printf_serial("[UACPI] uacpi_kernel_map(0x%llx, 0x%llx) called\r\n", addr, len);

// 	if (addr >= KERNEL_VIRTUAL_BASE) {
// 		printf_serial("[UACPI] uacpi_kernel_map() - already mapped, returning 0x%llx\r\n", addr);
// 		return (void*) addr; // It's already mapped.
// 	}

// 	static uint64_t total_maps = 0;
// 	total_maps++;
// 	if (total_maps % 1000 == 0) serial_printf("Total ACPI Maps: %llu\r\n", total_maps);

// 	// Arbitrary Cutoff, 256 continuous MB.
// 	if (len > 0x200000 * 128) {
// 		printf_serial("[UACPI] uacpi_kernel_map() - length too large, checking signature\r\n");
// 		// Map only the requested location, get the table header, return 0.
// 		char* magic = (char*) mapKernelLocation(addr, 0x24);
// 		printf("ACPICA: Target Signature: \"%c%c%c%c\"\n", magic[0], magic[1], magic[2], magic[3]);
// 		printf_serial("[UACPI] uacpi_kernel_map() - returning NULL due to size\r\n");
// 		return 0;
// 	}

// 	void* ret = (void*) mapKernelLocation(addr, len);
// 	// printf_serial("[UACPI] uacpi_kernel_map() - returning 0x%llx\r\n", (uint64_t) ret);

// 	// printf_serial("\r\nMAP REQUEST:\r\n\tRequest PHYS: 0x%llx\r\n\tRequest LEN:  0x%llx\r\n\tMapped Return: 0x%llx\r\n", PhysicalAddress, Length, ret);
// 	// printf("\nMAP REQUEST:\n\tRequest PHYS: 0x%llx\n\tRequest LEN:  0x%llx\n\tMapped Return: 0x%llx\n", PhysicalAddress, Length, ret);

// 	return ret;
// }

// /*
//  * Unmap a virtual memory range at 'addr' with a length of 'len' bytes.
//  *
//  * NOTE: 'addr' may be misaligned, see the comment above 'uacpi_kernel_map'.
//  *       Similar steps to uacpi_kernel_map can be taken to retrieve the
//  *       virtual address originally returned by the VMM for this mapping
//  *       as well as its true length.
//  */
// void uacpi_kernel_unmap(void* addr, uacpi_size len) {
// 	// printf_serial("[UACPI] uacpi_kernel_unmap(0x%llx, 0x%llx) called (no-op)\r\n", (uint64_t) addr, len);
// 	// I dont really care about unmapping right now. 
// }

// ------------------------------------------------------------------------------------------------
// Physical -> Virtual mapping cache
// Prevents redundant VMM allocations when uACPI re-maps the same physical
// regions (e.g. re-reading table headers, sub-region probes on the SSDT, etc.)
// ------------------------------------------------------------------------------------------------
#define PAGE_SIZE       0x1000
#define PAGE_MASK       (~(uacpi_phys_addr)(PAGE_SIZE - 1))

// Power-of-two so we can mask instead of modulo. 1024 slots handles a large
// server SSDT comfortably; bump to 2048 if you ever see cache-full warnings.
#define MAP_CACHE_SLOTS 1024

typedef struct {
	uacpi_phys_addr phys_base;   // page-aligned base physical address
	uacpi_size      mapped_len;  // page-aligned total length passed to VMM
	void* virt_base;   // what mapKernelLocation returned
	uint32_t        refcount;    // how many live uacpi_kernel_map calls reference this
} map_cache_entry_t;

static map_cache_entry_t _map_cache[MAP_CACHE_SLOTS];
static uint32_t          _map_cache_used = 0;

// FNV-1a hash, fast and good enough for physical page numbers
static inline uint32_t _map_hash(uacpi_phys_addr phys_base, uacpi_size mapped_len) {
	uint64_t v = (uint64_t) phys_base ^ ((uint64_t) mapped_len << 32);
	v ^= v >> 33;
	v *= 0xff51afd7ed558ccdULL;
	v ^= v >> 33;
	return (uint32_t) (v & (MAP_CACHE_SLOTS - 1));
}

// Returns the cache slot for (phys_base, mapped_len), or -1 if not found.
static int _map_cache_find(uacpi_phys_addr phys_base, uacpi_size mapped_len) {
	uint32_t slot = _map_hash(phys_base, mapped_len);
	// Linear probing
	for (uint32_t i = 0; i < MAP_CACHE_SLOTS; i++) {
		uint32_t idx = (slot + i) & (MAP_CACHE_SLOTS - 1);
		map_cache_entry_t* e = &_map_cache[idx];
		if (!e->virt_base) return -1;  // empty slot => not present
		if (e->phys_base == phys_base && e->mapped_len == mapped_len)
			return (int) idx;
	}
	return -1;
}

// Inserts a new entry. Call only after _map_cache_find returned -1.
static int _map_cache_insert(uacpi_phys_addr phys_base, uacpi_size mapped_len, void* virt_base) {
	if (_map_cache_used >= MAP_CACHE_SLOTS) {
		printf_serial("[UACPI][MAP_CACHE] WARNING: cache full (%u slots), cannot insert phys=0x%llx\r\n",
			MAP_CACHE_SLOTS, (uint64_t) phys_base);
		return -1;
	}
	uint32_t slot = _map_hash(phys_base, mapped_len);
	for (uint32_t i = 0; i < MAP_CACHE_SLOTS; i++) {
		uint32_t idx = (slot + i) & (MAP_CACHE_SLOTS - 1);
		if (!_map_cache[idx].virt_base) {
			_map_cache[idx].phys_base = phys_base;
			_map_cache[idx].mapped_len = mapped_len;
			_map_cache[idx].virt_base = virt_base;
			_map_cache[idx].refcount = 1;
			_map_cache_used++;
			return (int) idx;
		}
	}
	return -1;
}

// ------------------------------------------------------------------------------------------------
// uacpi_kernel_map / uacpi_kernel_unmap
// ------------------------------------------------------------------------------------------------
void* uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
	// Already in virtual address space — identity return.
	if (addr >= KERNEL_VIRTUAL_BASE)
		return (void*) addr;

	// Compute the true page-aligned region we need the VMM to map.
	uacpi_phys_addr page_offset = addr & (PAGE_SIZE - 1);       // offset within first page
	uacpi_phys_addr phys_base = addr & PAGE_MASK;              // round base down
	uacpi_size      aligned_len = (len + page_offset + PAGE_SIZE - 1) & PAGE_MASK; // round len up

	// --- Cache lookup ---
	int idx = _map_cache_find(phys_base, aligned_len);
	if (idx >= 0) {
		_map_cache[idx].refcount++;
		printf_serial("[UACPI][MAP_CACHE] HIT  phys=0x%llx len=0x%llx refcount=%u\r\n",
			(uint64_t) phys_base, (uint64_t) aligned_len, _map_cache[idx].refcount);
// Return the cached virtual base plus the original intra-page offset.
		return (void*) ((uintptr_t) _map_cache[idx].virt_base + page_offset);
	}

	// --- Sanity check: refuse absurdly large requests ---
	if (aligned_len > 0x200000 * 128) {
		printf_serial("[UACPI][MAP_CACHE] WARN: oversized map request 0x%llx bytes at phys 0x%llx\r\n",
			(uint64_t) aligned_len, (uint64_t) phys_base);
// Peek at the table signature to aid debugging, then bail.
		void* peek = (void*) mapKernelLocation(phys_base, 0x24);
		if (peek) {
			char* sig = (char*) peek + page_offset;
			printf_serial("[UACPI][MAP_CACHE] Signature at target: '%c%c%c%c'\r\n",
				sig[0], sig[1], sig[2], sig[3]);
		}
		return NULL;
	}

	// --- Cache miss: call the VMM ---
	void* virt_base = (void*) mapKernelLocation(phys_base, aligned_len);
	if (!virt_base) {
		printf_serial("[UACPI][MAP_CACHE] VMM returned NULL for phys=0x%llx len=0x%llx\r\n",
			(uint64_t) phys_base, (uint64_t) aligned_len);
		return NULL;
	}

	_map_cache_insert(phys_base, aligned_len, virt_base);
	printf_serial("[UACPI][MAP_CACHE] MISS phys=0x%llx len=0x%llx -> virt=0x%llx (cache %u/%u)\r\n",
		(uint64_t) phys_base, (uint64_t) aligned_len, (uint64_t) virt_base,
		_map_cache_used, MAP_CACHE_SLOTS);

	return (void*) ((uintptr_t) virt_base + page_offset);
}

void uacpi_kernel_unmap(void* addr, uacpi_size len) {
	if ((uintptr_t) addr >= KERNEL_VIRTUAL_BASE)
		return;  // identity-mapped, nothing to do

	// Reconstruct the page-aligned key so we can find the cache entry.
	// We don't have the original physical address here, so we match on virt.
	uacpi_size page_offset = (uintptr_t) addr & (PAGE_SIZE - 1);
	void* virt_base = (void*) ((uintptr_t) addr & PAGE_MASK);
	uacpi_size aligned_len = (len + page_offset + PAGE_SIZE - 1) & PAGE_MASK;

	for (uint32_t i = 0; i < MAP_CACHE_SLOTS; i++) {
		map_cache_entry_t* e = &_map_cache[i];
		if (!e->virt_base) continue;
		// Match on virtual base + length (both were derived the same way).
		if (e->virt_base == virt_base && e->mapped_len == aligned_len) {
			if (e->refcount > 0) e->refcount--;
			// Leave the mapping alive even at refcount 0 — the VMM mapping is
			// cheap to keep and uACPI may legitimately re-map the same region.
			// If you ever want to actually free: call your VMM unmap here when
			// refcount hits 0 and zero out the slot + _map_cache_used--.
			printf_serial("[UACPI][MAP_CACHE] UNMAP virt=0x%llx len=0x%llx refcount=%u (kept)\r\n",
				(uint64_t) virt_base, (uint64_t) aligned_len, e->refcount);
			return;
		}
	}

	printf_serial("[UACPI][MAP_CACHE] UNMAP virt=0x%llx not in cache (ignoring)\r\n", (uint64_t) addr);
}

void uacpi_kernel_log(uacpi_log_level lvl, const uacpi_char* str) {
	// printf_serial("[UACPI] uacpi_kernel_log(%d) called\r\n", lvl);
	switch (lvl) {
		case UACPI_LOG_DEBUG: uacpi_printf(VGA_COLOR_GREEN, "[UACPI][DEBUG] %s", str); break;
		case UACPI_LOG_TRACE: uacpi_printf(VGA_COLOR_LIGHT_GREEN, "[UACPI][TRACE] %s", str); break;
		case UACPI_LOG_INFO:  uacpi_printf(VGA_COLOR_CYAN, "[UACPI][INFO] %s", str); break;
		case UACPI_LOG_WARN:  uacpi_printf(VGA_COLOR_YELLOW, "[UACPI][WARN] %s", str); break;
		case UACPI_LOG_ERROR: uacpi_printf(VGA_COLOR_RED, "[UACPI][ERROR] %s", str); break;
		default:
	}
}

/*
 * Only the above ^^^ API may be used by early table access and
 * UACPI_BAREBONES_MODE.
 */
#ifndef UACPI_BAREBONES_MODE

/*
 * Convenience initialization/deinitialization hooks that will be called by
 * uACPI automatically when appropriate if compiled-in.
 */
#ifdef UACPI_KERNEL_INITIALIZATION
/*
 * This API is invoked for each initialization level so that appropriate parts
 * of the host kernel and/or glue code can be initialized at different stages.
 *
 * uACPI API that triggers calls to uacpi_kernel_initialize and the respective
 * 'current_init_lvl' passed to the hook at that stage:
 * 1. uacpi_initialize() -> UACPI_INIT_LEVEL_EARLY
 * 2. uacpi_namespace_load() -> UACPI_INIT_LEVEL_SUBSYSTEM_INITIALIZED
 * 3. (start of) uacpi_namespace_initialize() -> UACPI_INIT_LEVEL_NAMESPACE_LOADED
 * 4. (end of) uacpi_namespace_initialize() -> UACPI_INIT_LEVEL_NAMESPACE_INITIALIZED
 */
uacpi_status uacpi_kernel_initialize(uacpi_init_level current_init_lvl);
void uacpi_kernel_deinitialize(void);
#endif

/*
 * Open a PCI device at 'address' for reading & writing.
 *
 * Note that this must be able to open any arbitrary PCI device, not just those
 * detected during kernel PCI enumeration, since the following pattern is
 * relatively common in AML firmware:
 *    Device (THC0)
 *    {
 *        // Device at 00:10.06
 *        Name (_ADR, 0x00100006)  // _ADR: Address
 *
 *        OperationRegion (THCR, PCI_Config, Zero, 0x0100)
 *        Field (THCR, ByteAcc, NoLock, Preserve)
 *        {
 *            // Vendor ID field in the PCI configuration space
 *            VDID,   32
 *        }
 *
 *        // Check if the device at 00:10.06 actually exists, that is reading
 *        // from its configuration space returns something other than 0xFFs.
 *        If ((VDID != 0xFFFFFFFF))
 *        {
 *            // Actually create the rest of the device's body if it's present
 *            // in the system, otherwise skip it.
 *        }
 *    }
 *
 * The handle returned via 'out_handle' is used to perform IO on the
 * configuration space of the device.
 */
uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle* out_handle) {
	// printf_serial("[UACPI] uacpi_kernel_pci_device_open() called\r\n");
	// uacpi_failure(__func__);
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_pci_device_open() called\r\n");
	return UACPI_STATUS_UNIMPLEMENTED;
}
void uacpi_kernel_pci_device_close(uacpi_handle) {
	// printf_serial("[UACPI] uacpi_kernel_pci_device_close() called\r\n");
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_pci_device_close() called\r\n");

	// uacpi_failure(__func__);
}

uacpi_status kernel_pci_read(uacpi_handle device, uacpi_size offset, void* value, size_t bitwidth) {
	// printf_serial("[UACPI] kernel_pci_read() called\r\n");
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] kernel_pci_read() called\r\n");
	return UACPI_STATUS_UNIMPLEMENTED;
}
uacpi_status kernel_pci_write(uacpi_handle device, uacpi_size offset, size_t value, size_t bitwidth) {
	// printf_serial("[UACPI] kernel_pci_write() called\r\n");
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] kernel_pci_read() called\r\n");
	return UACPI_STATUS_UNIMPLEMENTED;
}

/*
 * Read & write the configuration space of a previously open PCI device.
 */
uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset, uacpi_u8* value) {
	// printf_serial("[UACPI] uacpi_kernel_pci_read8() called\r\n");
	return kernel_pci_read(device, offset, value, 8);
}
uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset, uacpi_u16* value) {
	// printf_serial("[UACPI] uacpi_kernel_pci_read16() called\r\n");
	return kernel_pci_read(device, offset, value, 16);
}
uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset, uacpi_u32* value) {
	// printf_serial("[UACPI] uacpi_kernel_pci_read32() called\r\n");
	return kernel_pci_read(device, offset, value, 32);
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset, uacpi_u8 value) {
	// printf_serial("[UACPI] uacpi_kernel_pci_write8() called\r\n");
	return kernel_pci_write(device, offset, value, 8);
}
uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset, uacpi_u16 value) {
	// printf_serial("[UACPI] uacpi_kernel_pci_write16() called\r\n");
	return kernel_pci_write(device, offset, value, 16);
}
uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset, uacpi_u32 value) {
	// printf_serial("[UACPI] uacpi_kernel_pci_write32() called\r\n");
	return kernel_pci_write(device, offset, value, 32);
}

/*
 * Map a SystemIO address at [base, base + len) and return a kernel-implemented
 * handle that can be used for reading and writing the IO range.
 *
 * NOTE: The x86 architecture uses the in/out family of instructions
 *       to access the SystemIO address space.
 */
uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle* out_handle) {
	// printf_serial("[UACPI] uacpi_kernel_io_map(0x%x, 0x%llx) called\r\n", base, len);
	if (!out_handle) return UACPI_STATUS_INVALID_ARGUMENT;

	// For now, just use the base port as the handle
	*out_handle = (uacpi_handle) (uintptr_t) base;
	// printf_serial("[UACPI] uacpi_kernel_io_map() - returning handle 0x%llx\r\n", (uint64_t) *out_handle);
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_io_map() - returning handle 0x%llx\r\n", (uint64_t) *out_handle);
	return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {
	// printf_serial("[UACPI] uacpi_kernel_io_unmap(0x%llx) called (no-op)\r\n", (uint64_t) handle);
	// Nothing to do for now; ports don't require unmapping
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_io_unmap(0x%llx) called (no-op)\r\n", (uint64_t) handle);
	(void) handle;
}

/*
 * Read/Write the IO range mapped via uacpi_kernel_io_map
 * at a 0-based 'offset' within the range.
 *
 * NOTE:
 * The x86 architecture uses the in/out family of instructions
 * to access the SystemIO address space.
 *
 * You are NOT allowed to break e.g. a 4-byte access into four 1-byte accesses.
 * Hardware ALWAYS expects accesses to be of the exact width.
 */
uacpi_status kernel_io_read(uacpi_handle port, uacpi_size offset, void* out_value, size_t width) {
	printf_serial("[UACPI] kernel_io_read(0x%llx, 0x%llx, %d bits) called\r\n", (uint64_t) port, offset, width);
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] kernel_io_read(0x%llx, 0x%llx, %d bits) called\r\n", (uint64_t) port, offset, width);
	switch (width) {
		case 8:
			*((uacpi_u8*) out_value) = inb((uint16_t) (port + offset));
			break;
		case 16:
			*((uacpi_u16*) out_value) = inw((uint16_t) (port + offset));
			break;
		case 32:
			*((uacpi_u32*) out_value) = inl((uint16_t) (port + offset));
			break;
		default:
			printf_serial("[UACPI] kernel_io_read() - invalid width\r\n");
			return UACPI_STATUS_INVALID_ARGUMENT;
	}
	// printf_serial("[UACPI] kernel_io_read() - success\r\n");
	return UACPI_STATUS_OK;
}
uacpi_status kernel_io_write(uacpi_handle port, uacpi_size offset, size_t in_value, size_t width) {
	printf_serial("[UACPI] kernel_io_write(0x%llx, 0x%llx, 0x%llx, %d bits) called\r\n", (uint64_t) port, offset, in_value, width);
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] kernel_io_write(0x%llx, 0x%llx, 0x%llx, %d bits) called\n", (uint64_t) port, offset, in_value, width);
	switch (width) {
		case 8:
			outb((uint16_t) (port + offset), (uint8_t) in_value);
			break;
		case 16:
			outw((uint16_t) (port + offset), (uint16_t) in_value);
			break;
		case 32:
			outl((uint16_t) (port + offset), (uint32_t) in_value);
			break;
		default:
			printf_serial("[UACPI] kernel_io_write() - invalid width\r\n");
			return UACPI_STATUS_INVALID_ARGUMENT;
	}
	// printf_serial("[UACPI] kernel_io_write() - success\r\n");
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read8(uacpi_handle port, uacpi_size offset, uacpi_u8* out_value) {
	return kernel_io_read(port, offset, out_value, 8);
}
uacpi_status uacpi_kernel_io_read16(uacpi_handle port, uacpi_size offset, uacpi_u16* out_value) {
	return kernel_io_read(port, offset, out_value, 16);
}
uacpi_status uacpi_kernel_io_read32(uacpi_handle port, uacpi_size offset, uacpi_u32* out_value) {
	return kernel_io_read(port, offset, out_value, 32);
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle port, uacpi_size offset, uacpi_u8 in_value) {
	return kernel_io_write(port, offset, in_value, 8);
}
uacpi_status uacpi_kernel_io_write16(uacpi_handle port, uacpi_size offset, uacpi_u16 in_value) {
	return kernel_io_write(port, offset, in_value, 16);
}
uacpi_status uacpi_kernel_io_write32(uacpi_handle port, uacpi_size offset, uacpi_u32 in_value) {
	return kernel_io_write(port, offset, in_value, 32);
}

#include <memory/kernel_alloc.h>

/*
 * Allocate a block of memory of 'size' bytes.
 * The contents of the allocated memory are unspecified.
 */
void* uacpi_kernel_alloc(uacpi_size size) {
	// printf_serial("[UACPI] uacpi_kernel_alloc(0x%llx) called\r\n", size);
	void* ptr = kalloc(size);
	// printf_serial("[UACPI] uacpi_kernel_alloc() - returning 0x%llx\r\n", (uint64_t) ptr);
	return ptr;
}

void uacpi_kernel_free(void* mem) {
	// printf_serial("[UACPI] uacpi_kernel_free(0x%llx) called\r\n", (uint64_t) mem);
	if (!mem) return;
	kfree(mem);
}

/*
 * Returns the number of nanosecond ticks elapsed since boot,
 * strictly monotonic.
 */
uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
	// printf_serial("[UACPI] uacpi_kernel_get_nanoseconds_since_boot() called\r\n");
	// uacpi_failure(__func__);
	// return UACPI_STATUS_UNIMPLEMENTED;
	// ms -> us -> ns
	// We only have ms accuracy
	uacpi_u64 result = timer_uptime_ms() * 1000 * 1000;
	// printf_serial("[UACPI] uacpi_kernel_get_nanoseconds_since_boot() - returning %llu\r\n", result);
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_get_nanoseconds_since_boot() - returning %llu\n", result);

	return result;
}

/*
 * Spin for N microseconds.
 */
void uacpi_kernel_stall(uacpi_u8 usec) {
	// printf_serial("[UACPI] uacpi_kernel_stall(%u) called\r\n", usec);
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_stall(%u) called\n", usec);
	uacpi_failure(__func__);
	// return UACPI_STATUS_UNIMPLEMENTED;
}
/*
 * Sleep for N milliseconds.
 */
void uacpi_kernel_sleep(uacpi_u64 msec) {
	// printf_serial("[UACPI] uacpi_kernel_sleep(%llu) called\r\n", msec);
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_sleep(%llu) called\n", msec);
	busy_wait_ms(msec);
	// printf_serial("[UACPI] uacpi_kernel_sleep() - complete\r\n");
}

#include <memory/semaphore.h>

/*
 * Handle a firmware request.
 *
 * Currently either a Breakpoint or Fatal operators.
 */
uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request* req) {
	printf_serial("[UACPI] uacpi_kernel_handle_firmware_request() called\r\n");
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_handle_firmware_request() called\n");
	if (!req) return UACPI_STATUS_INVALID_ARGUMENT;

	switch (req->type) {
		case UACPI_FIRMWARE_REQUEST_TYPE_BREAKPOINT:
			printf("[UACPI][FIRMWARE] Breakpoint requested by AML code.\n");
			printf_serial("[UACPI][FIRMWARE] Breakpoint requested by AML code.\r\n");
			WALLOS_CLI_HLT();
			panic("Firmware requested breakpoint", 0);
			break;

		case UACPI_FIRMWARE_REQUEST_TYPE_FATAL:
			printf("[UACPI][FIRMWARE] Fatal firmware request: code 0x%X\n", req->fatal.code);
			printf_serial("[UACPI][FIRMWARE] Fatal firmware request: code 0x%X\r\n", req->fatal.code);
			WALLOS_CLI_HLT();
			break;

		default:
			printf("[UACPI][FIRMWARE] Unknown firmware request type %d\n", req->type);
			printf_serial("[UACPI][FIRMWARE] Unknown firmware request type %d\r\n", req->type);
			break;
	}

	return UACPI_STATUS_OK;
}


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Interrupts
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
#include <system/idt.h>

// Storage for uACPI interrupt handlers
#define MAX_UACPI_IRQS 16

// Things that uACPI requires the interrupt handlers to pass to the handler, and allows us to keep track of the interrupts
struct uacpi_irq_info {
	uacpi_interrupt_handler handler;
	uacpi_handle ctx;
	uacpi_u32 irq;
	bool in_use;
};
static struct uacpi_irq_info uacpi_irq_table[MAX_UACPI_IRQS];

// This first one is identical to the rest. This one is the "example" so you can actually see what's happening.
WALLOS_INTERRUPT_HANDLER void uacpi_irq_wrapper_0(struct interrupt_frame* frame) {
	if (uacpi_irq_table[0].in_use && uacpi_irq_table[0].handler) {
		   /* Call the uACPI handler */
		(void) uacpi_irq_table[0].handler(uacpi_irq_table[0].ctx);
		/* uACPI returns HANDLED or UNHANDLED (and we don't really care about the status), but we still need to send EOI */
	}
	/* Send EOI to PIC(s) */
	interrupt_eoi(0);
}

WALLOS_INTERRUPT_HANDLER void uacpi_irq_wrapper_1(struct interrupt_frame* frame) { if (uacpi_irq_table[1].in_use && uacpi_irq_table[1].handler) { (void) uacpi_irq_table[1].handler(uacpi_irq_table[1].ctx); } interrupt_eoi(1); }
WALLOS_INTERRUPT_HANDLER void uacpi_irq_wrapper_2(struct interrupt_frame* frame) { if (uacpi_irq_table[2].in_use && uacpi_irq_table[2].handler) { (void) uacpi_irq_table[2].handler(uacpi_irq_table[2].ctx); } interrupt_eoi(2); }
WALLOS_INTERRUPT_HANDLER void uacpi_irq_wrapper_3(struct interrupt_frame* frame) { if (uacpi_irq_table[3].in_use && uacpi_irq_table[3].handler) { (void) uacpi_irq_table[3].handler(uacpi_irq_table[3].ctx); } interrupt_eoi(3); }
WALLOS_INTERRUPT_HANDLER void uacpi_irq_wrapper_4(struct interrupt_frame* frame) { if (uacpi_irq_table[4].in_use && uacpi_irq_table[4].handler) { (void) uacpi_irq_table[4].handler(uacpi_irq_table[4].ctx); } interrupt_eoi(4); }
WALLOS_INTERRUPT_HANDLER void uacpi_irq_wrapper_5(struct interrupt_frame* frame) { if (uacpi_irq_table[5].in_use && uacpi_irq_table[5].handler) { (void) uacpi_irq_table[5].handler(uacpi_irq_table[5].ctx); } interrupt_eoi(5); }
WALLOS_INTERRUPT_HANDLER void uacpi_irq_wrapper_6(struct interrupt_frame* frame) { if (uacpi_irq_table[6].in_use && uacpi_irq_table[6].handler) { (void) uacpi_irq_table[6].handler(uacpi_irq_table[6].ctx); } interrupt_eoi(6); }
WALLOS_INTERRUPT_HANDLER void uacpi_irq_wrapper_7(struct interrupt_frame* frame) { if (uacpi_irq_table[7].in_use && uacpi_irq_table[7].handler) { (void) uacpi_irq_table[7].handler(uacpi_irq_table[7].ctx); } interrupt_eoi(7); }
WALLOS_INTERRUPT_HANDLER void uacpi_irq_wrapper_8(struct interrupt_frame* frame) { if (uacpi_irq_table[8].in_use && uacpi_irq_table[8].handler) { (void) uacpi_irq_table[8].handler(uacpi_irq_table[8].ctx); } interrupt_eoi(8); }
WALLOS_INTERRUPT_HANDLER void uacpi_irq_wrapper_9(struct interrupt_frame* frame) { if (uacpi_irq_table[9].in_use && uacpi_irq_table[9].handler) { (void) uacpi_irq_table[9].handler(uacpi_irq_table[9].ctx); } interrupt_eoi(9); }
WALLOS_INTERRUPT_HANDLER void uacpi_irq_wrapper_10(struct interrupt_frame* frame) { if (uacpi_irq_table[10].in_use && uacpi_irq_table[10].handler) { (void) uacpi_irq_table[10].handler(uacpi_irq_table[10].ctx); } interrupt_eoi(10); }
WALLOS_INTERRUPT_HANDLER void uacpi_irq_wrapper_11(struct interrupt_frame* frame) { if (uacpi_irq_table[11].in_use && uacpi_irq_table[11].handler) { (void) uacpi_irq_table[11].handler(uacpi_irq_table[11].ctx); } interrupt_eoi(11); }
WALLOS_INTERRUPT_HANDLER void uacpi_irq_wrapper_12(struct interrupt_frame* frame) { if (uacpi_irq_table[12].in_use && uacpi_irq_table[12].handler) { (void) uacpi_irq_table[12].handler(uacpi_irq_table[12].ctx); } interrupt_eoi(12); }
WALLOS_INTERRUPT_HANDLER void uacpi_irq_wrapper_13(struct interrupt_frame* frame) { if (uacpi_irq_table[13].in_use && uacpi_irq_table[13].handler) { (void) uacpi_irq_table[13].handler(uacpi_irq_table[13].ctx); } interrupt_eoi(13); }
WALLOS_INTERRUPT_HANDLER void uacpi_irq_wrapper_14(struct interrupt_frame* frame) { if (uacpi_irq_table[14].in_use && uacpi_irq_table[14].handler) { (void) uacpi_irq_table[14].handler(uacpi_irq_table[14].ctx); } interrupt_eoi(14); }
WALLOS_INTERRUPT_HANDLER void uacpi_irq_wrapper_15(struct interrupt_frame* frame) { if (uacpi_irq_table[15].in_use && uacpi_irq_table[15].handler) { (void) uacpi_irq_table[15].handler(uacpi_irq_table[15].ctx); } interrupt_eoi(15); }


// Array of wrapper function pointers
static void (*uacpi_irq_wrappers[])(struct interrupt_frame*) = {
	uacpi_irq_wrapper_0,
	uacpi_irq_wrapper_1,
	uacpi_irq_wrapper_2,
	uacpi_irq_wrapper_3,
	uacpi_irq_wrapper_4,
	uacpi_irq_wrapper_5,
	uacpi_irq_wrapper_6,
	uacpi_irq_wrapper_7,
	uacpi_irq_wrapper_8,
	uacpi_irq_wrapper_9,
	uacpi_irq_wrapper_10,
	uacpi_irq_wrapper_11,
	uacpi_irq_wrapper_12,
	uacpi_irq_wrapper_13,
	uacpi_irq_wrapper_14,
	uacpi_irq_wrapper_15,
};

/*
 * Install an interrupt handler at 'irq', 'ctx' is passed to the provided
 * handler for every invocation.
 *
 * 'out_irq_handle' is set to a kernel-implemented value that can be used to
 * refer to this handler from other API.
 */
uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx, uacpi_handle* out_irq_handle) {
	// Validate IRQ number
	if (irq > 15) {
		logger(ERROR, "uACPI: IRQ %u out of range (0-15)\n", irq);
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	// Find a free slot in our table
	int slot = -1;
	for (int i = 0; i < MAX_UACPI_IRQS; i++) {
		if (!uacpi_irq_table[i].in_use) {
			slot = i;
			break;
		}
	}

	if (slot == -1) {
		logger(ERROR, "uACPI: No free IRQ handler slots\n");
		return UACPI_STATUS_OUT_OF_MEMORY;
	}

	// Store the uACPI handler info
	uacpi_irq_table[slot].handler = handler;
	uacpi_irq_table[slot].ctx = ctx;
	uacpi_irq_table[slot].irq = irq;
	uacpi_irq_table[slot].in_use = true;

	// Calculate IDT vector: IRQ 0-15 map to vectors 32-47 (0x20-0x2F)
	uint8_t vector = 0x20 + irq;

	logger(INFO, "uACPI: Installing handler for IRQ %u (vector 0x%x)\n", irq, vector);

	// Install our wrapper in the IDT
	add_interrupt_handler(vector, uacpi_irq_wrappers[slot], 0, 0x8E);

	// Enable the IRQ in the PIC
	irq_enable(irq);

	// Return the slot index as the handle
	*out_irq_handle = (uacpi_handle) (uintptr_t) slot;

	return UACPI_STATUS_OK;
}


/*
 * Uninstall an interrupt handler. 'irq_handle' is the value returned via
 * 'out_irq_handle' during installation.
 */
uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler, uacpi_handle irq_handle) {
	int slot = (int) (uintptr_t) irq_handle;

	if (slot < 0 || slot >= MAX_UACPI_IRQS || !uacpi_irq_table[slot].in_use) {
		logger(ERROR, "uACPI: Invalid IRQ handle for uninstall\n");
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	uacpi_u32 irq = uacpi_irq_table[slot].irq;

	logger(INFO, "uACPI: Uninstalling handler for IRQ %u\n", irq);

	// Disable the IRQ in the PIC
	irq_disable(irq);

	// Restore the generic handler
	uint8_t vector = 0x20 + irq;
//	set_idt_entry(&idt[vector], isr_stub_table[vector], 0, 0x8E);
	remove_interrupt_handler(vector);

	// Clear the slot
	uacpi_irq_table[slot].in_use = false;
	uacpi_irq_table[slot].handler = NULL;
	uacpi_irq_table[slot].ctx = NULL;

	return UACPI_STATUS_OK;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// Spinlocks, Mutex's, "Events", "Work"
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

#include <memory/spinlock.h>

/*
 * Create/free an opaque non-recursive kernel mutex object.
 */
uacpi_handle uacpi_kernel_create_mutex(void) {
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_create_mutex() called\n");
	// printf_serial("[UACPI] uacpi_kernel_create_mutex() called\r\n");
	semaphore_t* sem = semaphore_create(1, 1);
	// printf_serial("[UACPI] uacpi_kernel_create_mutex() - returning 0x%llx\r\n", (uint64_t) sem);
	return sem;
}

void uacpi_kernel_free_mutex(uacpi_handle handle) {
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_free_mutex() called\n");
	// printf_serial("[UACPI] uacpi_kernel_free_mutex(0x%llx) called\r\n", (uint64_t) handle);
	if (!handle) return;
	semaphore_destroy((semaphore_t*) handle);
}

/*
 * Create/free an opaque kernel (semaphore-like) event object.
 */
uacpi_handle uacpi_kernel_create_event(void) {

	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_create_event() called\n");
	// printf_serial("[UACPI] uacpi_kernel_create_event() called\r\n");
	uacpi_handle evt = semaphore_create(UINT32_MAX, 0); // Start at 0
	// printf_serial("[UACPI] uacpi_kernel_create_event() - returning 0x%llx\r\n", (uint64_t) evt);
	return evt;
}

void uacpi_kernel_free_event(uacpi_handle handle) {
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_free_event() called\n");

	// printf_serial("[UACPI] uacpi_kernel_free_event(0x%llx) called\r\n", (uint64_t) handle);
	if (!handle) return;
	semaphore_destroy((semaphore_t*) handle);
}

/*
 * Returns a unique identifier of the currently executing thread.
 *
 * The returned thread id cannot be UACPI_THREAD_ID_NONE.
 */
uacpi_thread_id uacpi_kernel_get_thread_id(void) {
	// printf_serial("[UACPI] uacpi_kernel_get_thread_id() called\r\n");
	// This ensures we keep a consistent value across threads. 
	// GCC wasn't happy with just returning a single value.
	// This has the bonus of being a "random" number (that's really always the same between runs)
	static int dummy_thread;
	return (uacpi_thread_id) &dummy_thread; // unique pointer for this thread
}
/*
 * Try to acquire the mutex with a millisecond timeout.
 *
 * The timeout value has the following meanings:
 * 0x0000 - Attempt to acquire the mutex once, in a non-blocking manner
 * 0x0001...0xFFFE - Attempt to acquire the mutex for at least 'timeout'
 *                   milliseconds
 * 0xFFFF - Infinite wait, block until the mutex is acquired
 *
 * The following are possible return values:
 * 1. UACPI_STATUS_OK - successful acquire operation
 * 2. UACPI_STATUS_TIMEOUT - timeout reached while attempting to acquire (or the
 *                           single attempt to acquire was not successful for
 *                           calls with timeout=0)
 * 3. Any other value - signifies a host internal error and is treated as such
 */
uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle, uacpi_u16 timeout_ms) {
	// printf_serial("[UACPI] uacpi_kernel_acquire_mutex(0x%llx, %u) called\r\n", (uint64_t) handle, timeout_ms);
	if (!handle) return UACPI_STATUS_INTERNAL_ERROR;

	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_acquire_mutex() called\n");


	uint64_t timeout = (timeout_ms == 0xFFFF) ? UINT64_MAX : timeout_ms;
	int status = semaphore_wait((semaphore_t*) handle, 1, timeout);

	int ret = UACPI_STATUS_OK;

	if (status == SEMAPHORE_TIMEOUT) {
		// printf_serial("[UACPI] uacpi_kernel_acquire_mutex() - timeout\r\n");
		ret = UACPI_STATUS_TIMEOUT;
	}
	// printf_serial("[UACPI] uacpi_kernel_acquire_mutex() - %s\r\n", ret == UACPI_STATUS_OK ? "STATUS_OK" : "STATUS_TIMEOUT");
	return ret;
}

void uacpi_kernel_release_mutex(uacpi_handle handle) {
	// printf_serial("[UACPI] uacpi_kernel_release_mutex(0x%llx) called\r\n", (uint64_t) handle);
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_release_mutex() called\n");

	if (!handle) return;
	semaphore_signal((semaphore_t*) handle, 1);
}
/*
 * Try to wait for an event (counter > 0) with a millisecond timeout.
 * A timeout value of 0xFFFF implies infinite wait.
 *
 * The internal counter is decremented by 1 if wait was successful.
 *
 * A successful wait is indicated by returning UACPI_TRUE.
 */
uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout_ms) {
	printf_serial("[UACPI] uacpi_kernel_wait_for_event(0x%llx, %u) called\r\n", (uint64_t) handle, timeout_ms);
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_wait_for_event(0x%llx, %u) called\n", (uint64_t) handle, timeout_ms);

	if (!handle) return 0;
	uint64_t timeout = (timeout_ms == 0xFFFF) ? UINT64_MAX : timeout_ms;
	int status = semaphore_wait((semaphore_t*) handle, 1, timeout);
	uacpi_bool result = status == SEMAPHORE_SUCCESS ? 1 : 0;
	// printf_serial("[UACPI] uacpi_kernel_wait_for_event() - returning %d\r\n", result);
	return result;
}

/*
 * Signal the event object by incrementing its internal counter by 1.
 *
 * This function may be used in interrupt contexts.
 */
void uacpi_kernel_signal_event(uacpi_handle handle) {
	// printf_serial("[UACPI] uacpi_kernel_signal_event(0x%llx) called\r\n", (uint64_t) handle);

	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_signal_event(0x%llx) called\n", (uint64_t) handle);
	if (!handle) return;
	semaphore_signal((semaphore_t*) handle, 1);
}


/*
 * Reset the event counter to 0.
 */
void uacpi_kernel_reset_event(uacpi_handle handle) {
	// printf_serial("[UACPI] uacpi_kernel_reset_event(0x%llx) called\r\n", (uint64_t) handle);
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_reset_event(0x%llx) called\r\n", (uint64_t) handle);
	if (!handle) return;
	// Consume all available units in semaphore
	while (semaphore_wait((semaphore_t*) handle, 1, 0) == SEMAPHORE_SUCCESS);
	// printf_serial("[UACPI] uacpi_kernel_reset_event() - complete\r\n");
}


/*
 * Create/free a kernel spinlock object.
 *
 * Unlike other types of locks, spinlocks may be used in interrupt contexts.
 */
uacpi_handle uacpi_kernel_create_spinlock(void) {
	// printf_serial("[UACPI] uacpi_kernel_create_spinlock() called\r\n");
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_create_spinlock() called\n");
	return spinlock_create();
}
void uacpi_kernel_free_spinlock(uacpi_handle handle) {
	// printf_serial("[UACPI] uacpi_kernel_unlock_spinlock(0x%llx) called\r\n", (uint64_t) handle);
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_unlock_spinlock() called\n");

	spinlock_destroy(handle);
}

/*
 * Lock/unlock helpers for spinlocks.
 *
 * These are expected to disable interrupts, returning the previous state of cpu
 * flags, that can be used to possibly re-enable interrupts if they were enabled
 * before.
 *
 * Note that lock is infalliable.
 */
uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle) {
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_lock_spinlock() called\n");
	if (!handle) return 0;
	spinlock_lock((spinlock_t*) handle);
	return 0; // Flags are ignored in single-core for now
}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags flags) {
	// printf_color(PRINT_COLOR_LIGHT_GREY, PRINT_DEFAULT_BG, "[UACPI] uacpi_kernel_unlock_spinlock() called\n");

	if (!handle) return;
	spinlock_unlock((spinlock_t*) handle);
}

/*
 * Schedules deferred work for execution.
 * Might be invoked from an interrupt context.
 */
uacpi_status uacpi_kernel_schedule_work(uacpi_work_type type, uacpi_work_handler handler, uacpi_handle ctx) {
	printf_serial("[UACPI] uacpi_kernel_schedule_work(type=%d) called\r\n", type);
	// printf("[UACPI] Warning: schedule_work called but not implemented\n");
	return UACPI_STATUS_UNIMPLEMENTED;
}


/*
 * Waits for two types of work to finish:
 * 1. All in-flight interrupts installed via uacpi_kernel_install_interrupt_handler
 * 2. All work scheduled via uacpi_kernel_schedule_work
 *
 * Note that the waits must be done in this order specifically.
 */
uacpi_status uacpi_kernel_wait_for_work_completion(void) {
	// printf("[UACPI] Warning: wait_for_work_completion called but not implemented\n");
	printf_serial("[UACPI] uacpi_kernel_wait_for_work_completion() called\r\n");

	return UACPI_STATUS_UNIMPLEMENTED;
}

#endif // WALLOS_USE_UACPI