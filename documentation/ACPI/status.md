# ACPI Status

The OSL (operating system layer) for both [uACPI](https://github.com/uACPI/uACPI) and [ACPICA](https://www.intel.com/content/www/us/en/developer/topic-technology/open/acpica/overview.html).

Both of these are relatively out of date, at least as to the versions I have included in this repository. The ACPICA version is a release from 2024, and the uACPI is a full major release version out of date (at the time of writing).
I do not plan on updating these unless necessary, both are plenty of performant and work well on all systems I have. I have tried to update to the latest version of uACPI, but had problems and abandoned the idea, as I didn't want to put more effort into it.

## ACPI API

There is an abstracted ACPI layer in the OS so that all other subsystems can interact with a common layer and not rely on the OS being built with a particular ACPI subsystem.
The API provides common formats for important ACPI tables, as well as some common required functions.
It also provides a small interface for dealing with power states (although this is mostly shutdown and restart only).

### Adding to the API

#### ACPI Table

All new ACPI generic ACPI tables should be added to `src\kernel\klibc\include\acpi\acpi_generic_tables.h` (and the corresponding `.c` file).
The getter functions should be in `src\kernel\klibc\include\acpi\acpi_api.h`.

The structures should be generic, and ideally should not require `__attribute__((packed))`.
They should include all fields that are in the ACPI spec (for spec defined tables), including unused or reserved fields.

When adding to the `.c` file, you should add a public getter function to follow the rest, and a private (static) function to retrieve the table from the ACPI subsystem if it's not already been populated.

There should only be one instance of a table for most tables.
Very few ACPI tables are allowed to have multiple copies, and those that do typically aren't mean to be interacted with directly.
Things like the `SSDT` would require special handling if the kernel ever needs to interact with then, but it shouldn't.

### Using the API

Simply include `<acpi\acpi_api.h>`. This will pull in `acpi_generic_tables.h` as well.

#### Tables

If getting a table, a `NULL` should imply that the system does not have the table, and never will.
Callers should not modify the fields of the tables.
If, for some reason, a field needs to be changed, it should create a copy of the table provided by the API.

#### Functions

There are functions to get general information about the ACPI subsystem (determine which one it is, if it's been initialized yet, etc.).

- `acpi_get_subsystem()` returns which subsystem the kernel was built with (`ACPICA`, `uACPI`, or `NONE`). Most callers shouldn't need this directly, it's mainly here for logging/debugging and the rare case where subsystem-specific behavior actually matters.
- `acpi_is_present()` reports whether an ACPI root table was found by the bootloader. Everything else in the API assumes this is `true`. Callers should check it before relying on ACPI at all.
- `acpi_setup_complete()` / `acpi_set_setup_completed()` track whether ACPI init has finished. Subsystems that depend on ACPI being fully brought up (tables parsed, namespace loaded, etc.) should check the former before touching anything else in this API, and the ACPI init path is responsible for calling the latter exactly once, when it's actually done.

##### Power State Functions

These wrap the underlying subsystem's sleep/reset calls so the rest of the kernel doesn't need to know which one is in use.

- `acpi_shutdown()` transitions the system into S5 (soft off). It preps the sleep state, disables interrupts, then enters it. This is a `noreturn` function. It will never return, on the system being started again after this, it will go through the normal startup path.
- `acpi_reboot()` restarts the system, also `noreturn`. It tries, in order: the ACPI reset register, an 8042 keyboard controller reset, and finally a deliberate triple fault as a last resort.
- `acpi_sleep(uint8_t state)` is intended for entering the other, non-terminal sleep states (S1–S4). (Declared but currently unimplemented.)

Both `acpi_shutdown()` and `acpi_reboot()` currently share a `shutdown_failed`/hang path and are marked with a TODO to revisit once SMP is finished. On a multi-core system, every CPU needs to be halted, not just the one that called these functions. Don't rely on the current single-CPU hang behavior sticking around.

On failure to properly shutdown/restart, `acpi_shutdown` and `acpi_reboot` will print a message telling the user it's safe to force shutdown the computer.
This is very similar to the WIN95/WIN98 shutdown screen. Neither of these functions will ever return.

##### Device Functions

- `acpi_find_devices(wallos_acpi_dev_t type, acpi_handle_t* out_handles, size_t* count)` walks the ACPI namespace for devices matching `type`, filling `out_handles` and reporting how many were found in `count`. Callers should be prepared for zero matches, not every device type exists on every system.
- `acpi_identify_handle(const acpi_handle_t handle)` does the reverse: given a handle, it identifies which `wallos_acpi_dev_t` it corresponds to. Useful when walking the namespace generically and dispatching based on device type.

##### Resource Functions

- `acpi_get_resources(const acpi_handle_t handle, acpi_mem_resource_t* mem, size_t* mem_count, acpi_irq_resource_t* irq, size_t* irq_count)` retrieves the memory and IRQ resources associated with a device handle (from its `_CRS`, effectively). As with tables, callers should not assume every device has both memory and IRQ resources, check the counts.
- `acpi_get_pci_routing(const acpi_handle_t pci_root, acpi_pci_route_t* routes, size_t* route_count)` retrieves the PCI interrupt routing table (`_PRT`) for a given PCI root bridge handle. This is how the kernel figures out which legacy IRQ (or GSI) each PCI device's interrupt pin is routed to.

##### Debug Functions

- `acpi_dump_namespace()` prints the full ACPI namespace, mainly useful when bringing up ACPI support on new hardware or diagnosing why a device isn't being found.
- `acpi_dump_tables()` prints the set of ACPI tables the subsystem has found. Handy alongside `acpi_dump_namespace()` when a table seems to be missing or malformed.

Both dump functions are debugging aids and are not expected to be called as part of normal kernel operation.
