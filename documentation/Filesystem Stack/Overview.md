# WallOS Storage Stack

This document describes the layered storage architecture used in WallOS: how the Virtual File System (VFS), filesystem drivers, the Wallos Disk Manager (WDM), and hardware drivers fit together and communicate.

---

## Layer Map

```
┌─────────────────────────────────────────────────────┐
│                   Kernel / User Code                │
│         VFS_Open / VFS_Read / VFS_Write ...         │
├─────────────────────────────────────────────────────┤
│                       VFS                           │
│  Mount table - FD table - path resolution           │
├─────────────────────────────────────────────────────┤
│            Filesystem Driver (VFS_FSOps)            │
│  Translates VFS calls into sector-level I/O         │
├─────────────────────────────────────────────────────┤
│                       WDM                           │
│  Drive registry · geometry · I/O dispatch           │
├─────────────────────────────────────────────────────┤
│              Hardware Driver (WDM_DriverOps)        │
│         initrd         │            AHCI            │
└─────────────────────────────────────────────────────┘

```

Each layer depends only on the layer directly below it through a well-defined vtable interface. The VFS never touches hardware, hardware drivers never know about files or paths.

---

## Layer Responsibilities

### VFS (`vfs.c` / `vfs.h`)

The VFS is the sole entry point for all file and directory operations. It owns:

- A **mount table** (`vfs_mounts`, up to `VFS_MOUNT_MAX = 16` entries) mapping absolute path prefixes to `(WDM_DriveHandle, VFS_FSOps*, fs_ctx)`.
- A **file descriptor table** (`vfs_fds`, up to `VFS_FD_MAX = 64` entries) mapping `VFS_FD` integers to per-mount driver fds.
- **Path resolution**: longest-prefix matching against the mount table to find the right filesystem driver, then stripping the mount-point prefix before handing the path to that driver.
- **Access mode enforcement**: checks at `VFS_Read` / `VFS_Write` time that the descriptor was opened with the correct flags.

The VFS does not interpret on-disk structures. It delegates everything filesystem-specific to the `VFS_FSOps` vtable it received at mount time.

### Filesystem Driver (`VFS_FSOps`)

A filesystem driver is a struct of function pointers (defined in `vfs.h`) that the VFS calls to perform all actual filesystem work. The current shipped driver is the FatFs binding (`fatfs_vfs.c`).

Responsibilities:

- Validate on-disk structures during `on_mount`.
- Maintain its own internal open-file table keyed by driver-level `VFS_FD` values.
- Translate file read/write calls into `WDM_Read` / `WDM_Write` calls on the `WDM_DriveHandle` it was given at mount time.
- Release all resources in `on_unmount`.

Paths arrive with the mount-point prefix already stripped by the VFS, so they always begin with `/` relative to that filesystem's root.

### WDM (`wdm.c` / `wdm.h`)

The WDM is a drive registry and I/O dispatcher. It acts as a thin layer between the filesystems and drives.
It provides some minimal flag/state checking before passing along to the relevant hardware driver.

The WDM does not know what a file, directory, or filesystem is. It speaks only in sectors (LBAs).

### Hardware Driver (`WDM_DriverOps`)

A hardware driver is a struct of function pointers that the WDM calls to perform raw sector I/O.
Drivers register themselves via `WDM_Register` and receive an opaque `ctx` pointer that they use to carry per-device state.

---

## Data Ownership

| Object | Owner | Lifetime |
|---|---|---|
| `WDM_DriveInfo` | WDM (copied on `WDM_Register`) | Until `WDM_Unregister` |
| `WDM_DriverOps*` (vtable) | Caller / driver | Must outlive `WDM_Unregister` |
| `VFS_FSOps*` (vtable) | Caller / FS driver | Must outlive `VFS_Unmount` |
| `fs_ctx` | FS driver | Freed by `on_unmount` |
| `VFS_FD` (kernel-visible) | VFS | Valid until `VFS_Close` |
| Driver-level `VFS_FD` | FS driver | Valid until driver's `close_file` |
