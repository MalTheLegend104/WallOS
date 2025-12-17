# Device Names

Both Windows and Linux have both simple and complex device names.
WallOS is designed to be as simple, and human-readable, as possible.

## Rules

1. Each device has ONE name and ONE name only.
     - A PCI device may also be a USB device. It only gets one of the tags, not both.
2. Each device gets the most specific name possible.
    - USB is more specific than PCI, so it will get assigned USB.

## Names

This is a comprehensive list of all possible device names in WallOS.
This list is in order from least -> most specific.

| Name   | Description                                              |
|--------|----------------------------------------------------------|
| pci    | Generic PCI device or controller                         |
| usb    | Generic USB device or controller                         |
| bt     | Generic Bluetooth device or controller                   |
| drive  | HDD/SSD storage devices                                  |
| floppy | Floppy drive/controller                                  |
| cdrom  | CD-ROM drive/controller                                  |
| sd     | SD drive/controller                                      |
| rmedia | Removable Media (CD/Floppy/USB Storage)                  |
| audio  | Audio output device                                      |
| mic    | Audio input device                                       |
| mouse  | Mouse/Pointing device                                    |
| kb     | Keyboard device                                          |
| eth    | Ethernet Controller                                      |
| wifi   | Wifi Controller                                          |
| cam    | Camera                                                   |
| cont   | Controller                                               |
| gpu    | Graphics Processing Unit (Dedicated or integrated)       |
| print  | Printer (wireless or wired)                              |
| sensor | Generic sensor device (temperature, accelerometer, etc.) |
