# Virtual File System (VFS)

The VFS is the single entry point for all file and directory operations in WallOS. It resolves paths to mounted filesystems, manages an FD table, enforces access modes, and delegates all filesystem-specific work to a `VFS_FSOps` vtable supplied at mount time.

---

## Public API

All functions return a `VFS_Status`. `VFS_OK = 0`, all error codes are negative.

### Lifecycle

```c
VFS_Status VFS_Init(void);
void       VFS_Shutdown(void);
```

`VFS_Init` zeroes the mount and FD tables. `VFS_Shutdown` force-closes all open FDs and unmounts all filesystems in reverse insertion order (highest `mount_seq` first), ensuring nested mounts are torn down before their parents.

### Mount Management

```c
VFS_Status VFS_Mount(
    const char*      path,    // absolute mount point, e.g. "/" or "/mnt/usb"
    WDM_DriveHandle  drive,   // underlying block device
    const VFS_FSOps* ops,     // filesystem driver vtable (must remain valid)
    void*            fs_ctx   // opaque context passed to all ops callbacks
);

VFS_Status VFS_Unmount(const char* path);
```

`VFS_Mount` validates all `ops` pointers (every field is required), reserves a slot in the mount table, and then calls `ops->on_mount(drive, fs_ctx)`. A non-OK return from `on_mount` aborts the mount and frees the slot. On success `slot->active` is set and `vfs_mount_seq` is incremented.

`VFS_Unmount` fails with `VFS_ERR_BUSY` if any file descriptors are still open on that mount. Callers must close all FDs first.

### File Operations

```c
VFS_Status VFS_Open (const char* path, VFS_OpenFlags flags, VFS_FD* out_fd);
VFS_Status VFS_Close(VFS_FD fd);
VFS_Status VFS_Read (VFS_FD fd, void* buf,       size_t size, size_t* out_read);
VFS_Status VFS_Write(VFS_FD fd, const void* buf, size_t size, size_t* out_written);
```

`VFS_Open` performs a longest-prefix mount resolution, allocates a slot in the global FD table, calls the driver's `open_file`, and on success increments `mount->open_count`. The returned `VFS_FD` is an index into `vfs_fds[]` and is valid until `VFS_Close`.

`VFS_Read` / `VFS_Write` check access mode before dispatching. `VFS_O_RDONLY` is `0x00`, so the check masks the low two bits and compares explicitly:

```c
// Read: reject if opened write-only
uint8_t mode = (uint8_t)(entry->flags & 0x03);
if (mode == VFS_O_WRONLY) return VFS_ERR_BADF;

// Write: reject if opened read-only
if (mode == VFS_O_RDONLY) return VFS_ERR_BADF;
```

A zero-length transfer (`size == 0`) returns `VFS_OK` with `*out_read / *out_written = 0` without calling the driver.

### Directory Operations

```c
VFS_Status VFS_Mkdir  (const char* path);
VFS_Status VFS_Rmdir  (const char* path);
VFS_Status VFS_Opendir(const char* path, VFS_FD* out_fd);
VFS_Status VFS_Readdir(VFS_FD fd, VFS_DirEnt* out_ent);
```

`VFS_Rmdir` pre-checks the mount table before resolving.
If `path` is itself a mount point the call returns `VFS_ERR_BUSY` regardless of which mount would absorb the path.
Directory descriptors share the same FD table as file descriptors and must be closed with `VFS_Close`.
`VFS_Readdir` signals end-of-directory by setting `out_ent->name[0] = '\0'` and returning `VFS_OK`.

---

## Path Resolution

The VFS uses longest-prefix mount matching. For a given `path` every active mount entry whose `path` is a prefix of `path` is a candidate, the one with the longest matching prefix wins.

A prefix match is only valid at a component boundary. The mount path must be followed by `/` or `\0` in the input (or the mount point is `"/"`, which matches everything).

```
Mount table:   "/"        -> fatfs 1
               "/mnt/usb" -> fatfs 2

VFS_Open("/etc/cfg")         -> resolves to fatfs 1, driver path "/etc/cfg"
VFS_Open("/mnt/usb/foo.txt") -> resolves to fatfs 2, driver path "/foo.txt"
VFS_Open("/mnt/usb")         -> resolves to fatfs 2, driver path "/"
VFS_Open("/mnt/usbextra")    -> resolves to fatfs 1
```

After resolution the mount prefix is stripped and the remainder is passed to the driver.
For the root mount (`path_len == 1`) the full path is passed unchanged.

---

## Filesystem Driver Contract (`VFS_FSOps`)

A filesystem driver fills out this struct and passes it to `VFS_Mount`.
All function pointers are **required**.
Paths delivered to every callback are:

- Absolute (begin with `/`).
- Relative to the mount root (mount-point prefix already stripped by the VFS).

```c
typedef struct {
    VFS_Status (*on_mount)   (WDM_DriveHandle drive, void* fs_ctx);
    void       (*on_unmount) (void* fs_ctx);

    VFS_Status (*open_file)  (void* fs_ctx, const char* path, VFS_OpenFlags flags, VFS_FD* out_fd);
    VFS_Status (*close_file) (void* fs_ctx, VFS_FD fd);
    VFS_Status (*read_file)  (void* fs_ctx, VFS_FD fd, void* buf, size_t size, size_t* out_read);
    VFS_Status (*write_file) (void* fs_ctx, VFS_FD fd, const void* buf, size_t size, size_t* out_written);

    VFS_Status (*make_dir)   (void* fs_ctx, const char* path);
    VFS_Status (*remove_dir) (void* fs_ctx, const char* path);
    VFS_Status (*open_dir)   (void* fs_ctx, const char* path, VFS_FD* out_fd);
    VFS_Status (*read_dir)   (void* fs_ctx, VFS_FD fd, VFS_DirEnt* out_ent);
} VFS_FSOps;
```

### Callback Contracts

**`on_mount(drive, fs_ctx)`**

- Called by `VFS_Mount` after the slot is reserved but before `active` is set. The driver should read and verify on-disk structures and initialize any state inside `fs_ctx`. A non-OK return aborts the mount.

**`on_unmount(fs_ctx)`**

- Called by `VFS_Unmount` (or `VFS_Shutdown`) after open_count reaches zero and the slot is cleared. The driver must flush metadata and free all resources held in `fs_ctx`. No further callbacks will be issued on this context.

**`open_file(fs_ctx, path, flags, out_fd)`**

- Allocate internal file state and write a unique, non-negative integer to `*out_fd`. This driver-level fd is opaque to the VFS. It will be passed back verbatim to `read_file`, `write_file`, and `close_file`. The VFS has already validated the path and allocated a VFS-level FD slot before this call.

**`close_file(fs_ctx, fd)`**

- Release driver state for `fd`. The VFS decrements `open_count` and zeroes its FD table entry after this returns, regardless of the return value.

**`read_file` / `write_file`**

- The VFS has already enforced access mode. The driver receives only calls that are consistent with the flags the file was opened with.

**`open_dir(fs_ctx, path, out_fd)`**

- Same allocation contract as `open_file`. The returned fd will be passed to `read_dir`. Directories are always opened read-only by the VFS.

**`read_dir(fs_ctx, fd, out_ent)`**

- Populate `out_ent` with the next entry. Signal end-of-directory by returning `VFS_OK` with `out_ent->name[0] = '\0'`.

---

## `VFS_OpenFlags`

| Flag | Value | Meaning |
|---|---|---|
| `VFS_O_RDONLY` | `0x00` | Open for reading only |
| `VFS_O_WRONLY` | `0x01` | Open for writing only |
| `VFS_O_RDWR` | `0x02` | Open for reading and writing |
| `VFS_O_CREAT` | `0x04` | Create file if it does not exist |
| `VFS_O_TRUNC` | `0x08` | Truncate to zero length on open |
| `VFS_O_EXCL` | `0x10` | With `VFS_O_CREAT`: fail if file exists |
| `VFS_O_APPEND` | `0x20` | Writes always go to end of file |

The access mode is encoded in the low two bits (mask `0x03`).
`VFS_O_RDONLY = 0` is intentional for POSIX compatibility.
Do not use a plain `& VFS_O_RDONLY` test.

---

## Limits

| Constant | Default | Meaning |
|---|---|---|
| `VFS_PATH_MAX` | 256 | Max absolute path length (including NUL) |
| `VFS_FD_MAX` | 64 | Max simultaneously open file/directory descriptors |
| `VFS_MOUNT_MAX` | 16 | Max simultaneously mounted filesystems |

---

## Reference Implementation

FatFS is the only currently implemented filesystem driver, therefore it stands as the reference implementation.

The filesystem driver wraps FatFs via `fatfs_vfs.c`. It exposes a static `fatfs_vfs_ops` vtable and a per-mount context type `fatfs_vfs_ctx_t` (opaque to callers).

### Allocation

```c
fatfs_vfs_ctx_t* fatfs_vfs_alloc_ctx(WDM_DriveHandle wdm_handle, BYTE pdrv);
void             fatfs_vfs_free_ctx (fatfs_vfs_ctx_t* ctx);
```

`fatfs_vfs_alloc_ctx` registers the WDM handle with FatFs's diskio layer via `ff_register_drive(pdrv, wdm_handle)`, allocates a `FATFS` work area, and prepares an internal open-file table of size `FATFS_VFS_MAX_OPEN_FILES` (defaults to `VFS_FD_MAX`). `fatfs_vfs_free_ctx` is called automatically by `on_unmount`.
Only call it directly if `VFS_Mount` was never reached.

### Typical Mount Sequence

```c
WDM_Init();
WDM_DriveHandle drive = initrd_wdm_init(INITRD_FLAG_NONE);

VFS_Init();
fatfs_vfs_ctx_t* ctx = fatfs_vfs_alloc_ctx(drive, 0);  // pdrv 0
VFS_Mount("/", drive, &fatfs_vfs_ops, ctx);
```

The `pdrv` number passed to `fatfs_vfs_alloc_ctx` must not already be in use by another FatFs mount.
