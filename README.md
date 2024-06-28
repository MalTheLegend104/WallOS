# WallOS

64-Bit hobby OS. Currently only supports x86-64, but hope to expand to Aarch64 and potentially other platforms.

## Project Structure

### **The general project structure is this:**

```plaintext
src┐
   ├──kernel
   │   ├──kcore
   │   │ 
   │   ├──klibc
   │   │  ├──include
   │   │  └──<klibc implementations>
   │   │ 
   │   ├──x86_64
   │   │  ├──boot
   │   │  └──<other platform specific files>
   │   │ 
   │   └──<other architectures here>
   │      ├──boot
   │      └──<other platform specific files>
   │   
   ├──libc
   │  ├──include
   │  ├──string
   │  ├──stdlib
   │  └──<source code for other libc here>
   │
   ├──acpi
   │  └─ Internal ACPI abstraction layer. The actual work is handled by ACPICA.
   │
   └──ramfs
      ├─ <ramfs program/binary>
      ├─ <ramfs program/binary>
      └─ makefile
```

### Kernel

#### KCore

The core kernel files. This is mostly related to things like the kernel entrypoint, kernel panics, and important drivers (like serial and PS/2).

#### KLibc

Most of the rest of the kernel subsystems and interfaces, including memory management, syscalls, and the kernel services terminal. This will likely be renamed in the future, after userspace is established.

#### x86_64

This contains all x86_64 platform specific code, such as the post-bootloader booting code that sets up the environment for the kernel, as well as platform specific features such as the IDT and GDT.

### Libc

This is a minimal implementation of the C standard library. It has very few functions, and is implemented mostly on a "I really need this function" basis.
There are plans to port [mlibc](https://github.com/managarm/mlibc) to WallOS eventually, but this likely wont happen before I get a proper userspace set up.

### Ramfs

The ramfs is documented [here.](documentation/ramfs/ramfs.md)

### ACPI

ACPI is essentially handled as a driver by WallOS. Instead of having it's own dedicated folder, it will likely get moved in a future release.
The driver more so acts like an abstraction layer, along with providing the Operating System Layer for [ACPICA](https://www.intel.com/content/www/us/en/developer/topic-technology/open/acpica/overview.html).

It really doesn't interact with much of the OS by itself, and is mostly a standalone module.
In terms of structure, it sits somewhere between `klibc` and `kcore`. Whenever a robust interface for drivers is setup, this is likely to change.

### Sys Calls

System calls will likely be held in a single header, among the likes of <Windows.h> on windows. If this isn't achieveable, we will likely follow the unix-like <sys/header>. This will be determined at a later date, after the userspace is fully designed.

## Documentation

All code that needs to be documented should be done so by following the rules of [doxygen](https://www.doxygen.nl/). It allows for JavaDoc like commenting, along with other common styles.
> I'm a former Java dev, and heavily perfer the JavaDoc style `@tag` as opposed to the `\\tag`. If commiting, please use the JavaDoc style tag.

```cpp
/**
* @brief This is example documentation.
*  
* @param a - an integer doing xyz.
* @return int - some integer.
*/
int test(int a);
```

## TODO

### Ordered

1. Filesystem
   - At the very least, want to be able to load from disk and load programs.
   - This could also just be a simple ramfs
2. System Calls
   - These are already supported, at least in a "the infrastructure exists" kind of way.
     The ability to handle and registers handlers exists, there's just none that are implemented yet.
3. Move terminal to userspace.
   - Scheduling and multitasking are necessary for me to do userspace apps, but I can still move the terminal to userspace.
   - This would also make developing and testing syscalls much easier.

### Unordered/Long Term

- GUI
- Multitasking
- Userspace Applications

## Contributing

There are many ways to contribute to the project:

- Simply report any bugs or make suggestions.
- Look through `bug` and `feature-reqests` tags in issues for something that interests you.
- Review the codebase and changes to see if you find any bugs or potential optimizations.
- Participate in the discussion board.
  - You can ask questions, help others out, talk about potential features, etc.

If you are interested in fixing issues, adding features, or otherwise contributing to the codebase, read the [contribution guide](documentation/General/contributing.md).

## Building

#### Building results in a `*.iso` file, and an associated binary file being put in `/dist/<platform architecture>/`. This iso CAN be deployed to actual systems.

### Docker

- In `/docker/`, there exist commands for setting up the dev environment for the project. Windows commands are the `*.bat` files, while the `*.sh` and files without extensions are for linux (potentially MacOS as well, this is untested).
    > You must have docker already installed. On windows, I highly recommend installing qemu.
- To build, simply run the script to build for the desired architecture. `build64` and `build.bat` both build for `x86_64`. Running `qemu` or `qemu.bat` both result in `x86_64` being built and ran in a vm.
    > Other platforms have names according to their architecture. For example `build64` builds for x86_64, `buildarm64` builds for Aarch64, etc.

### Native

- In `/scripts/`, there exist commands for setting up the dev environment for the project. Simply run `dockerless-setup.sh`, and follow the prompts.
    > In order to fully setup the environment you **WILL** have to build GCC and Binutils. (Grab a coffee and check your emails, it's going to take a while.)

- All the other commands are identical to the docker versions, `clean` cleans the build dirs, `build` builds for all architectures, etc.

### WSL

- Windows Subsystem for Linux is a fully fledged linux enviroment, meaning everything under [native](#native) applies here.
    > The only exception is that a lot of the time Windows and the WSL version of QEMU don't get along. Because of this, on WSL the `qemu` script defaults to using the Window's executable QEMU. If for some reason QEMU is not installed, or you simply want to run the linux version, run the `qemu` script with the `--native` flag.
