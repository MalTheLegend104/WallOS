# Wallos Disk Manager (WDM)

The Wallos Disk Manager (WDM) is the hardware abstraction layer for block devices. It provides a sector-oriented API to filesystem drivers above it and a registration/lifecycle API to hardware drivers below it.

---

## Public API

All functions return a `WDM_Status`. `WDM_OK = 0`, all error codes are negative.

### Lifecycle

```c
WDM_Status WDM_Init(void);
void       WDM_Shutdown(void);
```

`WDM_Init` must be called once before any other WDM function. `WDM_Shutdown` flushes every registered drive and calls each driver's `on_detach`, then invalidates all handles.

### Registration

```c
WDM_Status WDM_Register(
    const WDM_DriverOps* ops,   // driver vtable. This must remain valid
    void*                ctx,   // opaque driver context
    const WDM_DriveInfo* info,  // geometry / capability
    WDM_DriveHandle*     out    // receives the handle on success
);

WDM_Status WDM_Unregister(WDM_DriveHandle handle);
```

`WDM_Register` copies `*info` into the registry, so the caller's `WDM_DriveInfo` does not need to persist after the call. The `ops` pointer and `ctx` pointer must both remain valid until `WDM_Unregister` returns. `WDM_Register` calls `ops->on_attach(ctx)` synchronously, a non-OK return aborts registration.

### Enumeration and info

```c
WDM_Status WDM_Enumerate(WDM_DriveHandle* handles, uint32_t max, uint32_t* total);
WDM_Status WDM_GetInfo(WDM_DriveHandle handle, WDM_DriveInfo* out);
```

Pass `handles = NULL, max = 0` to query the count only.
All other documentation for these can be found in the doxygen comments for them.

### I/O

```c
WDM_Status WDM_Read (WDM_DriveHandle, WDM_LBA, uint32_t count, void*       buf, WDM_IOFlags);
WDM_Status WDM_Write(WDM_DriveHandle, WDM_LBA, uint32_t count, const void* buf, WDM_IOFlags);
WDM_Status WDM_Trim (WDM_DriveHandle, WDM_LBA, uint32_t count);
WDM_Status WDM_Flush(WDM_DriveHandle);
```

`WDM_Read` / `WDM_Write` enforces these before delegating to the driver:

1. `handle` must be active.
2. `buf` must not be NULL.
3. `lba + count` must not exceed `info.sector_count`.
4. For writes, `info.read_only` must be false.
5. If `info.dma_capable` is false, `WDM_FLAG_DMA` is silently cleared from flags.

`WDM_Trim` is only a hint. If the driver has no `trim` callback the WDM returns `WDM_OK` silently.

`WDM_Flush` with a `NULL` flush callback is treated as a no-op.

---

## Driver Contract (`WDM_DriverOps`)

A hardware driver fills out this struct and passes it to `WDM_Register`. Fields marked **Required** must never be NULL; fields marked **Optional** may be NULL.

```c
typedef struct {
    WDM_Status (*read)     (void* ctx, WDM_LBA, uint32_t count, void*       buf, WDM_IOFlags);  // Required
    WDM_Status (*write)    (void* ctx, WDM_LBA, uint32_t count, const void* buf, WDM_IOFlags);  // Required
    WDM_Status (*flush)    (void* ctx);                                                          // Optional
    WDM_Status (*trim)     (void* ctx, WDM_LBA, uint32_t count);                                // Optional
    WDM_Status (*on_attach)(void* ctx);                                                          // Optional
    void       (*on_detach)(void* ctx);                                                          // Optional
} WDM_DriverOps;
```

### Callback Contracts

**`on_attach(ctx)`**

- ***Optional***
- Called synchronously by `WDM_Register` before the handle is handed back. Use this to validate hardware state (if not already done before the call to register). A non-OK return aborts registration.

**`on_detach(ctx)`**

- ***Optional***
- Called synchronously by `WDM_Unregister` after the final flush. Use this to power down hardware or free driver-private memory. No I/O will be issued after this point.

**`read(ctx, lba, count, buf, flags)`**

- ***Required***
- By the time this is called, the WDM has already verified that `lba + count <= sector_count` and that `buf` is non-NULL. The driver does not need to repeat those checks, though it may add its own hardware-level checks.

**`write(ctx, lba, count, buf, flags)`**

- ***Required***
- Same pre-validated guarantees as `read`. If `WDM_FLAG_SYNC` is set, the driver must block until the device confirms data is on stable storage (ATA FLUSH CACHE or equivalent). Without `WDM_FLAG_SYNC`, the driver may return once data reaches the device's internal buffer.
- If a drive is read-only, have this be a stub that returns `WDM_ERR_WRITE_PROT`.

**`flush(ctx)`**

- ***Optional***
- Issue a cache-flush command. Return `WDM_ERR_UNSUPPORTED` if the device has no write-back cache, WDM treats this as `WDM_OK`.

**`trim(ctx, lba, count)`**

- ***Optional***
- Issue a TRIM/Deallocate hint. This is always optional and informational. Returning `WDM_OK` without doing anything is valid.

---

## `WDM_DriveInfo` Fields

Filled in by the driver at registration time:

| Field | Type | Description |
|---|---|---|
| `sector_count` | `uint64_t` | Total logical sectors on the device |
| `sector_size` | `uint32_t` | Bytes per logical sector (typically 512) |
| `physical_sector` | `uint32_t` | Physical sector size (for 4Kn drives) |
| `optimal_xfer` | `uint32_t` | Preferred transfer size in sectors |
| `removable` | `bool` | True if media may be swapped at runtime |
| `read_only` | `bool` | True to have WDM reject all writes |
| `dma_capable` | `bool` | True if the driver accepts `WDM_FLAG_DMA` |
| `model[64]` | `char[]` | Human-readable model string |
| `serial[32]` | `char[]` | Serial number string |

---

## Reference Implementations

### initrd (RAM disk)

`initrd_wdm_init(flags)` in `initrd.c` is the simplest possible WDM driver. Its context (`initrd_ctx_t`) holds a pointer to the bootloader-supplied RAM buffer (`_initrd_data`) and its size (`_initrd_size`).

```
on_attach : checks data != NULL and size % 512 == 0
read      : memcpy(buf, data + lba * 512, count * 512)
write     : memcpy(data + lba * 512, buf, count * 512)
flush     : no-op (RAM is always coherent)
trim      : NULL (not applicable)
on_detach : no-op (buffer owned by bootloader)
```

If `INITRD_FLAG_READ_ONLY` is passed, the `write` pointer in the ops table is set to `NULL`. The WDM then rejects writes with `WDM_ERR_WRITE_PROT` via the `info.read_only` flag path.

`WDM_DriveInfo` is populated as:

```c
info.sector_count = _initrd_size / 512;
info.sector_size  = 512;
info.removable    = false;
info.dma_capable  = false;
strncpy(info.model,  "initrd RAM disk", ...);
strncpy(info.serial, "INITRD-0",        ...);
```

### AHCI (SATA disk)

The AHCI driver (`ahci.c`) is registered once per detected SATA port, with a separate `WDM_DriveHandle` per port. Its context (`ahci_port_t`) carries the port's MMIO base address and a pointer to its DMA-accessible `ahci_port_mem_t` structure.

```
on_attach : reads PxSSTS, issues ATA IDENTIFY, extracts sector_count / model / serial
read      : builds H2D FIS (ATA READ DMA EXT), populates PRD table, issues command slot,
            polls PxCI until the slot clears, returns WDM_ERR_IO on TFD error bits
write     : same as read but uses ATA WRITE DMA EXT and sets W bit in command header
flush     : issues ATA FLUSH CACHE EXT, polls completion
trim      : NULL (not currently implemented)
on_detach : stops the port DMA engine (clears PxCMD.ST / FRE)
```

The AHCI driver declares `dma_capable = false` in the current build (DMA support is stubbed out at the kernel level), so `WDM_FLAG_DMA` is always stripped by the WDM before reaching the driver.
