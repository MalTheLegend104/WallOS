# WallOS

64-Bit hobby OS. Currently only supports x86-64, but hope to expand to Aarch64 and potentially other platforms.

> Disclaimer: Currently the CI/CD is broken. This build "requires" (not really, it could still be built with x86_64-elf-gcc) a custom cross-compiler.
> More documentation is "on the way" for this, but definitely not a priority.

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

## TODO

1. PCI device discovery
2. CPU Scheduler
   - I really need the ability to spawn tasks in different threads. I don't even really care about this being a fully featured scheduler, I just want different threads.
3. System Calls
   - These are already supported, at least in a "the infrastructure exists" kind of way.
     The ability to handle and registers handlers exists, there's just none that are implemented yet.
4. Move terminal to userspace.
   - Scheduling and multitasking are necessary for me to do userspace apps, but I can still move the terminal to userspace.
   - This would also make developing and testing syscalls much easier.

### Other

> This is an unordered list of things that need to eventually be implemented, but aren't really in the way of anything.

- Optimization of framebuffer on real hardware
  - I have a feeling the problems I had were due to the interaction between the BIOS provided framebuffer and the GPU in my test system.

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

These will take a call convention of `int 0x42` on x86_64 (along with implementing actual `syscall/sysenter` support later). I also plan on (potentially) supporting `int 0x80` for portability support.

## Documentation

All current documentation can be found [here.](documentation/README.md)

### Contributing Documentation

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

The only way to build this currently is using a gcc cross compiler. This can be done on any system that supports it (tested on FreeBSD, several linux distros, WSL).

Currently all build files require the usage of `x86_64-wallos-*` binaries. The process of building these is long and complex, and (as is a trend here) not documented.

The build *can* be done using `x86_64-elf-*` options from binutils and gcc, but all of the makefiles will need to be changed (or aliased to `x86_64-wallos-*` but that's probably a bad idea).

I hope to distribute a `docker` image to remove the pain of building eventually, but the process of making `x86_64-wallos-*` binaries is nowhere near good enough for a docker image yet.

### Packages

#### apt

- `dosfstools`
- `build-essential`
- `xorriso`
- `qemu-system`
  - This package has been completely different in the past, and might not even be the correct package.
  - We need `qemu-system-x86_64`, you can look it up if `qemu-system` doesn't install it.

There is a multitude of GRUB packages needed.

- `grub-efi-amd64-bin`
  - This is only required for UEFI builds.

#### pacman

I have built WallOS on Arch before... I didn't keep track of the packages...

If I end up building it on Arch again I will put all the packages here (or if someone else does I'd appreciate a pull request for this list...).
