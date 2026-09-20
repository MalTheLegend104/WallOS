# Testing

As mentioned elsewhere, most testing is performed virtually using:
- [Qemu](https://www.qemu.org/)
- [VirtualBox](https://www.virtualbox.org/)
- [Bochs](https://bochs.sourceforge.io/)

This section documents the real hardware WallOS has been tested on, along with any quirks or problems observed.

For brevity, only relevant hardware components are listed. Details like keyboard type or exact motherboard revision are omitted unless they become relevant. This may change as additional subsystems (e.g., Ethernet support) are implemented.

Systems are listed in order of importance: desktops first, laptops second, servers last.

## Table of Contents

- [Testing](#testing)
   * [Table of Contents](#table-of-contents)
   * [Desktops](#desktops)
      + [AMD FX-8370, NVidia 1030Ti, 64GB RAM, ASUS Motherboard](#amd-fx-8370-nvidia-1030ti-64gb-ram-asus-motherboard)
      + [Ryzen 1600AF, NVidia 1660 Super, 32GB RAM, MSI Motherboard](#ryzen-1600af-nvidia-1660-super-32gb-ram-msi-motherboard)
      + [Intel 8700, iGPU, 24GB RAM, Dell OEM Motherboard](#intel-8700-igpu-24gb-ram-dell-oem-motherboard)
   * [Laptops](#laptops)
      + [Thinkpad T490 (Intel i5-8365U, iGPU, 8GB RAM)](#thinkpad-t490-intel-i5-8365u-igpu-8gb-ram)
      + [Thinkpad T14 (Intel i5-10310U, iGPU, 24GB RAM)](#thinkpad-t14-intel-i5-10310u-igpu-24gb-ram)
   * [Servers](#servers)
      + [Intel Xeon E-2124, iGPU, 8GB RAM, Dell OEM Motherboard](#intel-xeon-e-2124-igpu-8gb-ram-dell-oem-motherboard)
   * [Other](#other)

## Desktops

### AMD FX-8370, NVidia 1030Ti, 64GB RAM, ASUS Motherboard

This system is dedicated to testing WallOS, and basically every commit gets tested on this before getting pushed.

| Problem #          | Subsystem | Description | Important (Y/N) |
|--------------------|-----------|-------------|-----------------|
| No known issues.   | N/A       | N/A         | N               |

### Ryzen 1600AF, NVidia 1660 Super, 32GB RAM, MSI Motherboard

This system is used elsewhere for other things, so testing doesn't get done super often. This is the most "modern" system I've tested it on, though.

| Problem #          | Subsystem | Description | Important (Y/N) |
|--------------------|-----------|-------------|-----------------|
| No known issues.   | N/A       | N/A         | N               |

### Intel 8700, iGPU, 24GB RAM, Dell OEM Motherboard

Same as the previous desktop, this is used for other things, so testing is done rather sparsely on it.

| Problem #          | Subsystem | Description | Important (Y/N) |
|--------------------|-----------|-------------|-----------------|
| No known issues.   | N/A       | N/A         | N               |

## Laptops

### Thinkpad T490 (Intel i5-8365U, iGPU, 8GB RAM)

This laptop is purely dedicated to testing WallOS.

| Problem # | Subsystem | Description | Important (Y/N) |
|---|---|---|---|
| 1 | ACPI | Tons of ACPI tables (I mean like 50+ tables), and neither uACPI nor ACPICA are particularly happy about them. It doesn't cause problems per se, but definitely may cause them in the future. | N |
| 2 | Graphics? | After enough time, the screen freezes. It happens rather arbitrarily, but will always happen eventually. Best I can tell it's the framebuffer, but may also be due to an unhandled ACPI event, system timer being messed up, etc. I've not cared enough to worry about this yet, though. | Y |

### Thinkpad T14 (Intel i5-10310U, iGPU, 24GB RAM)

This laptop is used elsewhere, so rarely tested.

| Problem # | Subsystem | Description | Important (Y/N) |
|---|---|---|---|
| 1 | Graphics? | Exact same problem as described for the other Thinkpad. | Y |

## Servers

### Intel Xeon E-2124, iGPU, 8GB RAM, Dell OEM Motherboard

This is used elsewhere, and really only tested on during downtime (so VERY rarely).

| Problem #          | Subsystem | Description | Important (Y/N) |
|--------------------|-----------|-------------|-----------------|
| No known issues.   | N/A       | N/A         | N               |

## Other

Nothing yet, but I do plan to eventually get a dual-socket server with two CPUs, which will definitely be a learning experience.
