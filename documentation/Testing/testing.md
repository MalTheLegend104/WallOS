# Testing

Most testing is done virtually, using:

- [QEMU](https://www.qemu.org/)
- [VirtualBox](https://www.virtualbox.org/)
- [Bochs](https://bochs.sourceforge.io/)

This page documents the real hardware WallOS has been tested on, along with any quirks or problems observed.

Only relevant hardware is listed. Details like keyboard type or exact motherboard revision are left out unless they matter. That may change as more subsystems (Ethernet, for example) are implemented.

Systems are split into three tiers based on how often they are used:

- **Core:** Primary testing systems. Almost every major commit is tested on these when relevant. They were chosen on the principle that if WallOS works on something weird, it will work on everything.
- **Secondary:** Dedicated testing systems that are used regularly, but less often than the core systems.
- **Occasionally/Rarely Tested:** Systems used when available. They mostly serve other purposes, so testing on them is infrequent.

Which systems get used depends on the subsystem being changed. Not every machine has XHCI, for example, and if SMP works on the dual socket server and the APU1D, it will likely work everywhere else.

## Table of Contents

- [Testing](#testing)
  - [Table of Contents](#table-of-contents)
  - [Core Testing Systems](#core-testing-systems)
    - [Apple Mac Mini (Late 2014)](#apple-mac-mini-late-2014)
    - [Dual Intel Xeon E5-2680 v2, 64GB RAM, Supermicro X9DRD-7LN4F](#dual-intel-xeon-e5-2680-v2-64gb-ram-supermicro-x9drd-7ln4f)
    - [Dell OptiPlex (4th Gen Intel i5, 16GB RAM)](#dell-optiplex-4th-gen-intel-i5-16gb-ram)
    - [PCEngines APU1D](#pcengines-apu1d)
    - [Qmatic Shuttle (Intel Celeron J1900, 4GB RAM)](#qmatic-shuttle-intel-celeron-j1900-4gb-ram)
  - [Secondary Testing Systems](#secondary-testing-systems)
    - [ThinkPad T490 (Intel i5-8365U, iGPU, 8GB RAM)](#thinkpad-t490-intel-i5-8365u-igpu-8gb-ram)
    - [ThinkPad T14 (Intel i5-10310U, iGPU, 24GB RAM)](#thinkpad-t14-intel-i5-10310u-igpu-24gb-ram)
  - [Occasionally/Rarely Tested Systems](#occasionallyrarely-tested-systems)
    - [AMD FX-8370, NVIDIA GT 1030, 64GB RAM, ASUS Motherboard](#amd-fx-8370-nvidia-gt-1030-64gb-ram-asus-motherboard)
    - [Ryzen 1600AF, NVIDIA 1660 Super, 32GB RAM, MSI Motherboard](#ryzen-1600af-nvidia-1660-super-32gb-ram-msi-motherboard)
    - [Intel i7-8700, iGPU, 24GB RAM, Dell OEM Motherboard](#intel-i7-8700-igpu-24gb-ram-dell-oem-motherboard)
    - [Intel Xeon E-2124, iGPU, 8GB RAM, Dell OEM Motherboard](#intel-xeon-e-2124-igpu-8gb-ram-dell-oem-motherboard)
  - [Other](#other)

## Core Testing Systems

These systems make up the primary testing infrastructure for WallOS. Most of them are unusual in some way, described in their dedicated sections.
A lot of these are also picked because they're small and easy to put together in a small test setup.

### Apple Mac Mini (Late 2014)

1.4GHz dual-core i5, iGPU, 4GB RAM.

This is a dedicated test system with some odd hardware choices. It is XHCI-only and uses Apple's own UEFI and ACPI implementations. It is also small and easy to set up, so it is my main test bench at the moment.

| Problem #        | Subsystem | Description | Important (Y/N) |
| ---------------- | --------- | ----------- | --------------- |
| No known issues. | N/A       | N/A         | N               |

### Dual Intel Xeon E5-2680 v2, 64GB RAM, Supermicro X9DRD-7LN4F

This is a dedicated test system, and almost all major commits go through it. It has two sockets, along with an unusual industrial UEFI implementation. The BIOS is highly configurable, SATA PIO works properly, and it has four Ethernet ports and IPMI. Most of the RAM was scavenged from another system and is not ECC.

| Problem #        | Subsystem | Description | Important (Y/N) |
| ---------------- | --------- | ----------- | --------------- |
| No known issues. | N/A       | N/A         | N               |

### Dell OptiPlex (4th Gen Intel i5, 16GB RAM)

I can't recall the exact CPU, only that it's a 4th gen i5.

This is the most conventional PC in the core set, so it's the baseline for a "normal" computer.

| Problem #        | Subsystem | Description | Important (Y/N) |
| ---------------- | --------- | ----------- | --------------- |
| No known issues. | N/A       | N/A         | N               |

### PCEngines APU1D

This system is serial only and BIOS only, and it has an unusual AMD CPU.
Has two Realtek ethernet ports (much to my dismay).
This is mostly here for the CPU and serial restriction.
I eventually want WallOS to be able to be full controlled via serial, and will be a "WallOS Server" test system if I ever get to the point of wanting a dedicated headless server build.

| Problem # | Subsystem  | Description                                                      | Important (Y/N) |
| --------- | ---------- | ---------------------------------------------------------------- | --------------- |
| 1         | Everything | Being serial only, most subsystems can't be meaningfully tested. | N               |

### Qmatic Shuttle (Intel Celeron J1900, 4GB RAM)

This is a dedicated test system built from digital signage hardware, so it has some weird industrial design decisions and a low power CPU.
It has an SD card port, a small 32GB SSD, and VGA, DisplayPort, and HDMI outputs.
It only supports USB 2.0, and the BIOS is very restrictive.

| Problem #        | Subsystem | Description | Important (Y/N) |
| ---------------- | --------- | ----------- | --------------- |
| No known issues. | N/A       | N/A         | N               |

## Secondary Testing Systems

These systems are dedicated to testing WallOS, but are used less often than the core systems.

### ThinkPad T490 (Intel i5-8365U, iGPU, 8GB RAM)

This laptop is usually only used for testing WallOS, but rarely used elsewhere.

| Problem # | Subsystem | Description                                                                                                                                                                                                                                                           | Important (Y/N) |
| --------- | --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------- |
| 1         | ACPI      | It exposes 50+ ACPI tables, and neither uACPI nor ACPICA handles them well. It hasn't caused problems yet, but it could in the future.                                                                                                                                | N               |
| 2         | Graphics? | After running for long enough, the screen freezes. The timing is arbitrary, but it always happens eventually. It seems related to the framebuffer, but could also be an unhandled ACPI event, a system timer issue, or something else. I haven't investigated it yet. | Y               |

### ThinkPad T14 (Intel i5-10310U, iGPU, 24GB RAM)

This laptop is used for other things, so it is rarely tested.

| Problem # | Subsystem | Description                                    | Important (Y/N) |
| --------- | --------- | ---------------------------------------------- | --------------- |
| 1         | Graphics? | Same freeze as the T490 (see problem 2 above). | Y               |

## Occasionally/Rarely Tested Systems

These systems are used for testing when available, but are not part of the primary workflow.

### AMD FX-8370, NVIDIA GT 1030, 64GB RAM, ASUS Motherboard

This was my old main test machine. It was bulky and has since been picked apart for parts. It still exists but is rarely used, and it is now down to a single 8GB DDR3 DIMM.

| Problem #        | Subsystem | Description | Important (Y/N) |
| ---------------- | --------- | ----------- | --------------- |
| No known issues. | N/A       | N/A         | N               |

### Ryzen 1600AF, NVIDIA 1660 Super, 32GB RAM, MSI Motherboard

This system is used for other things, so testing is infrequent. It is the most modern hardware WallOS has been tested on.

| Problem #        | Subsystem | Description | Important (Y/N) |
| ---------------- | --------- | ----------- | --------------- |
| No known issues. | N/A       | N/A         | N               |

### Intel i7-8700, iGPU, 24GB RAM, Dell OEM Motherboard

This system is used for other things, so testing is sparse.

| Problem #        | Subsystem | Description | Important (Y/N) |
| ---------------- | --------- | ----------- | --------------- |
| No known issues. | N/A       | N/A         | N               |

### Intel Xeon E-2124, iGPU, 8GB RAM, Dell OEM Motherboard

This system is used for other things and is only tested during downtime, so testing is very rare.

| Problem #        | Subsystem | Description | Important (Y/N) |
| ---------------- | --------- | ----------- | --------------- |
| No known issues. | N/A       | N/A         | N               |

## Other
