# Drivers

The driver layers of WallOS are meant to be as streamlined as possible.
There are two main parts of dealing with devices, the `Device Manager` and the `Driver Manager`.

## Device Manager

The device manager is what detects and gathers information about connected devices.
The general steps of the device manager are as follows:

1. Detects device
2. Assigns [name](device_name.md)
3. Requests driver from Driver Manager
   - If driver exists and is loaded, it passes device info off to the driver
   - If driver does not exist or isn't loaded properly, it logs the problem and continues on to next device
4. After all devices loaded, it monitors PnP

Users can enable/disable devices.
There are also several ways to get information about devices.

### Builtin Drivers

The device manager has several built in drivers.
These drivers are required to be able to discover other devices.

- PCI (and by extension PCIe)
- Serial
- ACPI (for on board devices like HPET)

## Driver Manager

The driver managers job is to simply act as a layer of communication between drivers and the device manager.
The general steps of the driver manager are as follows:

1. Notes installed drivers.
2. Waits for device manager to request a driver
   - If driver exists, initialize it
   - If it doesn't, report that to the device manager
3. Waits in standby until device manager needs something

Users can enable/disable drivers.
There are several ways to get information about drivers.
