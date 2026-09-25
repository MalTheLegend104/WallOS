# Device Manager

The device manager is the central registry for every device the kernel knows about, regardless of what bus or subsystem discovered it. PCI, USB, ACPI platform devices, timers - anything that gets probed ends up as a `wallos_device_t` in here, linked into both a flat registry (for fast lookup) and a tree (for topology).

The goal is the same as the ACPI abstraction: drivers and subsystems shouldn't need to care _how_ a device was found, just that it exists, what it is, and where it lives.

## Device Interface Flags

Every device carries a `device_interface_flags_t` (a `uint64_t` bitmask) describing what it is, at several levels simultaneously:

```
|63|62|61|60      53|52                32|31                12|11         0|
 0  0  0  0000000000 00000000000000000000 00000000000000000000 000000000000
```

- **Bits 0-11 - Physical Transport.** How the device is physically reached (`DEV_INT_MMIO`, `DEV_INT_PCI`, `DEV_INT_USB`, `DEV_INT_PLATFORM`, `DEV_INT_VIRTIO`, `DEV_INT_TIMER`, etc.).
- **Bits 12-31 - Controller Type.** What controller manages it (`DEV_INT_AHCI`, `DEV_INT_NVME`, `DEV_INT_XHCI`, `DEV_INT_EHCI`, `DEV_INT_OHCI`, `DEV_INT_UHCI`, `DEV_INT_HDA`).
- **Bits 32-51 - Protocol/Class.** What protocol it exposes (`DEV_INT_HID`, `DEV_INT_MSC`, `DEV_INT_CDC`, `DEV_INT_AUDIO`, `DEV_INT_NET_MAC`, `DEV_INT_VIDEO`).
- **Bits 52-60 - Low-Speed/Embedded.** For simple embedded buses (`DEV_INT_I2C`, `DEV_INT_SPI`, `DEV_INT_UART`, `DEV_INT_GPIO`, `DEV_INT_SDIO`).
- **Bit 61 - Already Bound.** Set for devices that initialize and manage themselves entirely during early init (timers being the main example) rather than going through `dm_bind()`. This just tells the rest of the driver system "don't bother, this one's handled."
- **Bit 62 - Interface Only.** Marks synthetic nodes that exist purely to give real devices a parent. `pci_discover()`'s `pci` node and USB controller "ports", for example. `DEV_INT_IS_REAL(x)` filters these out during probing/binding.
- **Bit 63 - Unknown.** The device was seen but couldn't be classified. It can still carry other flags (e.g. we might know it's on a PCI bus but not what it is).

A flags value of all ones (`DEV_INT_INVALID`, `0xFFFFFFFFFFFFFFFF`) is reserved to mean "not a valid device" - `create_device()` rejects it outright.

Because these are independent fields packed into one word, a real device usually has flags set across more than one range at once. For example, a SATA drive might be `DEV_INT_PCI | DEV_INT_AHCI`, an HID mouse might be `DEV_INT_USB | DEV_INT_XHCI | DEV_INT_HID`.

### Working with flags

There's a full set of macros for reading and manipulating flags rather than hand-rolling bit math everywhere:

- `DEV_INT_GET_TRANSPORT/CONTROLLER/PROTOCOL/EMBEDDED(x)` mask out just that field.
- `DEV_INT_HAS_TRANSPORT/CONTROLLER/PROTOCOL/EMBEDDED(x)` check whether that field is non-zero.
- `DEV_INT_HAS(x, flag)`, `DEV_INT_HAS_ALL(x, flags)`, `DEV_INT_HAS_ANY(x, flags)` for general flag testing.
- Convenience predicates like `DEV_INT_IS_PCI`, `DEV_INT_IS_USB`, `DEV_INT_IS_MMIO`, `DEV_INT_IS_STORAGE_CTRL` (AHCI or NVMe), `DEV_INT_IS_USB_CTRL` (any of the four host controller types), `DEV_INT_IS_REAL`, `DEV_INT_IS_INTERFACE_ONLY`, `DEV_INT_IS_ALREADY_BOUND`.
- `DEV_INT_MATCH_CONTROLLER(x, flag)` / `DEV_INT_MATCH_TRANSPORT(x, flag)` for exact-match checks against a masked field, rather than just "is this bit set."
- `DEV_INT_SET`, `DEV_INT_CLEAR`, `DEV_INT_TOGGLE` for mutating flags in place, plus `DEV_INT_MARK_UNKNOWN`/`DEV_INT_CLEAR_UNKNOWN` and `DEV_INT_MARK_BOUND`/`DEV_INT_CLEAR_BOUND` shortcuts for the two special bits.

Prefer these macros over raw bitwise ops when touching `interfaces`. They keep intent obvious and make it a lot harder to accidentally test the wrong bit range. If the bit ranges change in the future (which they may), using these macros ensures that code wont be broken.

`get_flag_name()` maps a single flag back to its string name (`"pci"`, `"xhci"`, `"hid"`, etc.), used by things like `print_device_flags()` for debug output. It expects exactly one bit at a time, not a combined mask.

## The Device Descriptor

`wallos_device_t` is the struct representing a single device. A few things worth knowing about its fields:

- **`name`** is owned by whoever created the device, _not_ the device manager. It must outlive the device struct, and the caller is responsible for freeing it after `remove_device()`. The device manager never touches it.
- **`path`** is the opposite: owned entirely by the device manager. Nothing outside should modify it directly. Call `recalculate_device_path()` if it needs to be rebuilt (e.g. after moving a device in the tree).
- **Identity fields** (`vendor_id`, `device_id`, `subsystem_id`) are generic across bus types - not every bus populates all three, but they're common enough to have their own spot, rather than bury in the `location` union.
- **Topology** is a classic first-child/next-sibling tree: `parent`, `first_child`, `next_sibling`. You walk a device's children with `first_child->next_sibling->next_sibling->...->NULL`. There's no `prev_sibling`. If you need to walk backwards, you don't. Restructure the traversal instead.
- **`location`** is a union keyed by transport type - `pci` gets bus/slot/function, `usb` gets port/address, `mmio` gets a base address and IRQ. Only read the member that matches the device's transport flag.
- **`driver_data`** is an opaque pointer for the bound driver's private state. The device manager never allocates or frees it. The driver that sets it owns its lifetime, and must clean it up _before_ calling `remove_device()`.

### Walking children

`DEV_FOR_EACH_CHILD(parent, child)` iterates the direct children of a device (non-recursive). `DEV_HAS_CHILDREN(dev)` is a quick non-NULL check on `first_child`. For a full recursive walk, see `print_device_tree_recursive()` in the implementation. There's currently no public recursive iterator, just the print helper.

## Device Paths

Right now, a device's path mirrors its position in the tree: `get_device_path()` walks from the device up through every `parent` to the root, joins each `name` with `/`, and prefixes the whole thing with `/dev`. So a USB keyboard sitting under a PCI-attached XHCI controller ends up as something like:

```
/dev/pci/pci0/xhci0/port0/keyboard0
```

This is rebuilt whenever the tree shape changes: `register_device()` computes it once at registration, and `recalculate_device_path()` / `update_device_path_recursive()` rebuild it, and every descendant's path, since their paths depend on this one, after a move. Paths are heap-allocated (`kalloc`'d in `get_device_path()`), and `remove_device()` frees a device's own path when it's torn down.

**Planned addition:** a flatter `/dev/keyboard0`-style namespace, closer to how Linux's `devfs`/`udev` present devices, is going to sit _alongside_ this full-topology.
The bus path is still incredibly valuable (it tells you exactly how a device is reached, which matters for debugging and for disambiguating devices with the same generic name), so it isn't going anywhere.
The flat name will just be a second, stable alias for the common case where callers don't care about topology and want something short and predictable.
The topology itself (`parent`/`first_child`/`next_sibling`) already drives things like `dev tree` and driver binding order regardless of which path scheme is used.
Since a device may end up addressable by more than one string once this lands, prefer `resolve_device()` / `find_device_by_name()` over manually building or parsing paths, that way lookups keep working no matter which naming scheme the caller used.

## Registry Functions

- `create_device(flags, name)` allocates and zero-initializes a `wallos_device_t`, refusing `DEV_INT_INVALID`. It does **not** register the device. It's just constructed, not yet visible to lookups.
- `register_device(dev)` computes the device's path and links it into the internal registry (a simple singly-linked list, `device_registry`). This is what makes a device visible to `find_device_by_name()`/`find_device_by_path()`/`resolve_device()`.
- `remove_device(dev)` unlinks the device from both its parent's child list and the flat registry, frees its `path`, and frees the `wallos_device_t` itself. It deliberately does **not** free `driver_data` (driver's job, must happen first), does **not** free `name` (caller's job), and does **not** recursively remove children. If a device has children, the caller must tear down the subtree leaves-first before removing the parent.
- `recalculate_device_path(dev)` rebuilds a device's path (and recursively, all descendants' paths) after something in its ancestry changes.

### Lookup

- `find_device_by_name(name)` and `find_device_by_path(path)` do a linear scan of the registry.
- `resolve_device(input)` is the convenience entry point most callers should use: if `input` starts with `/`, it's treated as a path, otherwise as a name.

### The registry itself

`device_registry` is a private, module-static singly-linked list of `device_node_t` (`{ dev, next }`). The device manager owns it, subsystems only ever get pointers into it via the lookup functions above. `internal_get_dev_registry()` exposes the raw root node for the rare case where something genuinely needs to iterate _every_ device (e.g. the `dev list`/`dev tree` commands with no filter).
Reach for this only when there's no better way to get at the devices you need, it bypasses all the filtering/lookup logic.

## Bound vs. Unbound

A device is considered **bound** if it has a driver attached, either `bound_driver` is set, or it carries `DEV_INT_ALREADY_BOUND` (self-managed devices like timers that never go through the normal binding path). Interface-only nodes (`DEV_INT_INTERFACE_ONLY`) are excluded from both categories; they're structural, not drivable. `device_is_bound()` / `device_is_unbound()` encode this, and the `dev list --bound`/`--unbound` filters use them directly.

## The `dev` Command

`device_cmd()` implements a small terminal command (aliased `dev`) for inspecting the registry at runtime:

- **`dev list [name] [--bound|-b] [--unbound|-u]`** - lists registered devices, optionally rooted at a specific device (by name or path) and filtered by bind state. `--bound` and `--unbound` are mutually exclusive.
- **`dev tree [name] [--depth|-d N]`** - prints the device tree as a `├──`/`└──` box-drawing tree (via `print_device_tree_recursive()`), rooted at a device or, if none given, every root-level device in the registry. `--depth` caps how many levels deep to print; omit it for unlimited depth.
- **`dev path <name>`** - prints a device's cached path.
- **`dev info <name>`** - prints name, vendor:device ID, decoded interface flags (via `print_device_flags()`/`get_flag_name()`), parent, and child count.
- **`dev refresh <name>`** - forces a path recalculation via `recalculate_device_path()`.

All of these resolve their target through `resolve_device()`, so either a name or a full path works interchangeably anywhere a device is expected. Extra information is output over serial.

## Adding to the Device Manager

When adding support for a new bus or controller type:

- Add a new flag bit in the correct range in `device_interface_t` (transport/controller/protocol/embedded). Don't reuse or overload an existing bit even if it seems close enough.
- Add a matching case to `get_flag_name()` so it shows up correctly in `dev info`/debug output instead of falling through as `unknown_bit_N`.
- If the device needs bus-specific location data that doesn't fit `pci`/`usb`/`mmio`, extend the `location` union rather than bolting fields onto the base struct.
- Devices are still expected to be created via `create_device()` and made visible via `register_device()`. Don't hand-construct a `wallos_device_t` and link it into the tree/registry manually, since `register_device()` is what computes the initial path.
