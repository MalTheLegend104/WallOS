# WallOS

64-Bit hobby OS. Currently only supports x86-64, but hope to expand to Aarch64 and potentially other platforms.

> Disclaimer: Currently the CI/CD is broken. This build "requires" (not really, it could still be built with x86_64-elf-gcc) a custom cross-compiler.
>
> I have a fork of [GCC](https://github.com/MalTheLegend104/gcc), [binutils](https://github.com/MalTheLegend104/binutils-gdb), and [mlibc](https://github.com/MalTheLegend104/mlibc) that contain branches for my patches.
> These are updated as required, generally whenever new major versions come out or new important features are added.
> The process of building them is rather complicated, and will not be discussed here.
> I plan on writing a small python CLI tool to clone/configure/build the toolchain, but it's a very low priority.

## Philosophy and Backstory

### Backstory

Ever since I first learned how to program, I've had a fascination with trying to figure out how things work on a fundamental level.
Naturally, that eventually led me to OS development. You can't get much more fundamental than this, can you? (Before someone says "well, embedded can be lower level...", I also do a lot with embedded systems).

WallOS isn't my first, nor second, attempt at an OS. I've learned a ton from my past failures, and WallOS was inspired heavily (especially in structure, much to my current dismay...) by those past projects.
This is the first OS I've been proud of enough to show off publicly, and it has been developed for long enough that I don't feel like I can just stop and start another OS anew.

#### The name

You're probably wondering about the name... To be honest, there's not a lot there.
When I first made this project, I didn't want to carry over a name from one of my past attempts, and was struggling to come up with something to name it.
I looked up above my monitor, saw my wall, and said, "Whatever, I can change the name later when I think of something better."
The name grew on me, so here it stays.

#### Why C?

I really like C. Seriously, that's the reason. Of course, there are arguments to be made in favor of one language over another, but I seriously just like writing C code.

If you've looked at my code (or the GitHub `Languages` section), you're probably saying "But there's a ton of C++...".
You'd be correct, but virtually none of the language features have been used, much less implemented (exceptions, RTTI, new/delete, etc.).
I mostly treat C++ as "C with namespaces" for the sake of not polluting the global namespace with function names.
Most of what I've been building recently has been pure C, though.

### Philosophy

My entire philosophy with this OS is that it's solely a learning experience.
It has never been written with any intent that it actually become anything more than a learning experience.

I work on it off-and-on as I feel like it, hopping between it and some other personal projects.
I have zero intention of setting deadlines for implementing things. I'll get to it when I get to it, if I ever do.
I have zero remorse about putting things off until I can't anymore (looking at you, PMM).
This entire experience has been with the intention of being fun to me, nothing else.

**The entire OS is written with this in mind:**

WallOS is a learning-focused operating system.  
Code readability and architecture are prioritized over optimizations.  
The goal is to understand systems deeply, not to compete with production kernels.

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
   │   │  ├──acpi
   │   │  │  └─ Internal ACPI abstraction layer.
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
   └──initrd
      ├─ <ramfs program/binary>
      ├─ <ramfs program/binary>
      └─ makefile
```

## TODO

1. CPU Scheduler
   - I really need the ability to spawn tasks in different threads. I don't even really care about this being a fully featured scheduler, I just want different threads.
     - I will implement at least the _interface_ for a proper scheduler, even if I only care about basic round-robin multithreading for now.
   - I have implemented the interface for this, but honestly, I think it'll get a whole rewrite because I don't particularly like how I've done it.
2. System Calls
   - These are already supported, at least in a "the infrastructure exists" kind of way.
     The ability to handle them and register syscall handlers exists, there are just none that are implemented yet.
   - This is really the only "hurdle" before going to userspace.
3. Move terminal to userspace.
   - Scheduling and multitasking are necessary for me to do userspace apps, but I can still move the terminal to userspace.
   - This would also make developing and testing syscalls much easier.

### Future "Wishlist"

- VMM
  - My virtual memory manager has been a long-standing annoyance of mine. Don't get me wrong, it works, it does what it's supposed to, but it's very limited.
    It's a pain to add support for new things. Basically all VMM issues I've had have stemmed from the original PMM design, though, so this is on the back burner until I _really_ need 4KB pages.
- Buildsystem
  - The `make` based buildsystem has worked great for basically the entire length of the project. With that said, I hate it. It's a pain to maintain, and I have to basically relearn how and why I did everything the way I did it anytime I need to make changes.
  - I ideally want something smoother to use, likely CMake (which I've tried implementing unsuccessfully several times, mainly due to me giving up halfway through each time).
- Major Refactor
  - A ton of things are written with only x86_64 in mind, and should ideally be abstracted away into proper interfaces. For example, serial depends on x86 CPU I/O ports exclusively.

### Recently Completed

These are not necessarily in order. It's been a _very_ long since I've merged into main.

1. XHCI
   - I decided to start with XHCI first when implementing USB because it seems "easier" in the sense that the controller did a lot of the work that you'd have to manually do in USB1/2. That said, that's about the about thing that was "easy" about this. XHCI was a nightmare to get working, and some of my testing devices have non-compliant hardware. Got it implemented, and working with HID keyboards, so good enough for me.
   - This will need to be greatly cleaned up. There is a TON of bad code in this (albeit I restarted 3 times in the making of it because of code structure, so it's not too awful). I started out trying to make it readable and followable, and after a few weeks of writing it, I started wanting to "just get it done" and code quality went way down.
2. HID
   - The whole reason I wanted to write a USB driver in the first place is that one of my testing devices doesn't have PS/2 emulation or a serial input/output, so the only way to interact with it was USB keyboards. The HID layer only really supports boot protocol keyboards, but can identify controllers and mice (just doesn't really do anything with them).
3. Input Layer
   - Implementing HID required me to remove the dependence on the PS/2 layer for inputs. I tried to make this layer have as good of an interface as possible, and it is very extendable in the future as needed.
4. Filesystems
   - As a sort of side project while being a bit burned out while implementing XHCI, I wrote iso9660 and FAT (12/26/32) filesystem drivers. Wrote a small filesystem layer (that sits next to the VFS) to help manage mounting. I basically rewrote the entire command layer relating to filesystems and drives, so we have "normal" cd/ls/cat/etc.
5. WallShell
   - Added a ton of new features to this, and actually "ported" the standalone version of WallShell I maintained separately to WallOS. Dedicated way to extract arguments, CWD, proper input handling, some keybinds, etc.
6. Timing
   - Added a proper timer interface that's abstracted from x86_64. Designed to be able to support a ton of different timers in a system with different purposes, and should be very platform agnostic. Also added HPET support, which I should've done a long time ago.
7. Ported [Kilo](https://github.com/antirez/kilo)
   - I wanted a way to actually edit files, so I ported the most minimal text editor I could find. Had no interest in writing my own. I had to do a decent amount of work for this to work with my display and input APIs. It works incredibly well for how small it is.

### Kernel

This section is a very basic description of each "module" of the kernel. I really need to just spend a couple of days updating my documentation and putting it in the proper place, but ¯\\\_(ツ)\_/¯.

#### KCore

The core kernel files. This is mostly related to things like the kernel entrypoint, kernel panics, and important drivers (like serial and PS/2).
There are also the "important" filesystem drivers (FAT and iso9660).

Pretty much all headers for this are located in [klibc](#Klibc) to make them accessible to the rest of the system. (There's definitely room for buildsystem improvements).

My past self didn't really plan on supporting anything other than x86_64, which has caused present me much pain.
The kernel C entrypoint really should just be part of the x86_64 architecture folder, alongside the regular entrypoint.
The current entrypoint contained here has a lot of dependence on x86_64 system initialization and isn't particularly well abstracted.

#### KLibc

Most of the rest of the kernel subsystems and interfaces, including memory management, syscalls, and the kernel services terminal. This will likely be renamed in the future, after userspace is established. It's not really a kernel "libc" (that's the libc folder), and rather just regular kernel API.

Yet again, a good portion of this is x86_64 specific, and it's intermixed with things that are actually properly abstracted.

#### x86_64

This contains all x86_64 platform specific code, such as the post-bootloader booting code that sets up the environment for the kernel, as well as platform specific features such as the IDT and GDT.

Any other platforms that end up supported in the future will end up in similarly titled folders alongside this.

### Libc

This is a minimal implementation of the C standard library (plus variations of stdlib functions that are useful). It has very few functions and is implemented mostly on a "I really need this function" basis.
There are plans to port [mlibc](https://github.com/managarm/mlibc) to WallOS eventually, but this likely won't happen before I get a proper userspace set up, and even when I do, a lot of the kernel won't touch much of it.

Most of this is absolute barebones implementations, and there's definitely plenty of room for optimization.
Pretty much the only things here that are even remotely optimized are `printf` and `fast_memcpy`.

### Ramfs (initrd)

> I hop between calling this "ramfs", "ramdisk", and "initrd" a lot in my code and docs. Any of those refer to the same thing.

The ramfs is documented [here.](documentation/ramfs/ramfs.md)

Basically, it's a 2MB (constant size), Read only (my FAT12 driver only supports reads), Fat12 filesystem that's appended to the end of the kernel at link time, and distributed as part of the `.bin`.
It's designed to carry the _absolute minimum_ required to get the system booted, which is really only a config (that doesn't even do anything) for now. When I get actual binary loading, this will likely be how optional drivers are distributed.

### ACPI

ACPI is essentially handled as a driver by WallOS. It's currently contained in the `x86_64` folder (because I initially couldn't think of where to put it and said "eh whatever, I'll move it later"...).
It will likely get moved to its own dedicated spot later on, if other architectures are ever added.
The driver more so acts like an abstraction layer, along with providing the Operating System Layer (OSL) for [ACPICA](https://www.intel.com/content/www/us/en/developer/topic-technology/open/acpica/overview.html).
WallOS also has a built-in layer for [uACPI](https://github.com/uACPI/uACPI), along with an interface (which still needs a ton of work) that lets the OS not particularly care about which subsystem it was compiled with.

There are advantages to both subsystems:

- `uACPI`
  - Significantly faster
  - OS Layer is better implemented
    - This is on me, ACPICA support is _much_ older, and I've gained a ton of experience by the time I wrote this OSL.
- `ACPICA`
  - "Reference" implementation by Intel
  - Much easier to use for ACPI debugging
  - Requires a lot less of the OSL to actually be implemented to work.

It really doesn't interact with much of the OS by itself and is mostly a standalone module.
In terms of structure, it sits somewhere between `klibc` and `kcore`.
It doesn't go through the normal driver interface though, leaving it in a weird place.
There is an abstraction layer over the ACPI subsystem that the OS in general goes though, and the kernel/OS should never interact directly with the subsystem.

### Sys Calls

System calls will likely be held in a single header, among the likes of <Windows.h> on Windows. If this isn't achievable, we will likely follow the unix-like <sys/header>. This will be determined at a later date, after the userspace is fully designed.

These will take a call convention of `int 0x42` on x86_64 (along with implementing actual `syscall/sysenter` support later on). I also plan on (potentially) supporting `int 0x80` for portability support.

This will get its own proper dedicated documentation whenever I get around to actually implementing the syscalls.

## Documentation

All current documentation can be found [here.](documentation/README.md)

I suck at documentation.
I've been trying to be better about adding comments to interface layers, and the most important systems that drivers touch have been documented with doxygen, but I really need to sit down and actually write regular documentation for a lot of stuff.
There's a lot of small quirks around basically every subsystem.

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

For the record, I'm _**VERY**_ bad at actually remembering to document things as I'm implementing them.
I tend to get into a flow state and just keep writing code without documenting it as I go, and future me hates me for it.

This is okay for some things, as certain interfaces are self-explanatory, but other things need a lot more documentation (_**cough**_ PMM and SATA _**cough**_).

## Contributing

There are many ways to contribute to the project:

- Simply report any bugs or make suggestions.
- Look through `bug` and `feature-requests` tags in issues for something that interests you.
  - I don't really use the `issues` tab much. Most of what I want implemented is marked with `TODO` in the code or earlier in this document in the [todo](#todo) section.
- Review the codebase and changes to see if you find any bugs or potential optimizations.
  - Keep in mind I actively choose to _not_ optimize things at the expense of readability most of the time. There are some exceptions (graphics, for example), but generally, please favor readability above all else.
    This OS is meant to be a learning experience for me (and others), not something that actually ends up being used in the real world.
    - With this in mind, if you see something that is _actively_ causing bad performance or making the OS unusable, don't feel bad about optimizing it. This is what I went through with framebuffer graphics and the PMM.
- Participate in the discussion board.
  - You can ask questions, help others out, talk about potential features, etc.
  - I am _more than happy_ to talk about anything to do with the OS (and can ramble for hours), don't be afraid to reach out.

If you are interested in fixing issues, adding features, or otherwise contributing to the codebase, read the [contribution guide](documentation/General/contributing.md).

## Building

> This section is a bit rambly, sorry about that...

#### Building results in a `*.iso` file, and an associated binary file being put in `/dist/<platform architecture>/`. This iso CAN be deployed to actual systems.

The only way to build this currently is using a gcc cross compiler. This can be done on any system that supports it (tested on FreeBSD, several Linux distros, WSL).

Currently, all build files require the usage of `x86_64-wallos-*` binaries. The process of building these is long and complex, and (as is a trend here) not documented.

The build _can_ be done using `x86_64-elf-*` options from binutils and gcc, but all of the makefiles will need to be changed (or aliased to `x86_64-wallos-*` but that's probably a bad idea).

I hope to distribute a `docker` image to remove the pain of building eventually, but the process of making `x86_64-wallos-*` binaries is nowhere near good enough for a Docker image yet.

I also plan on implementing a small python CLI tool used to help clone/configure/build the required toolchain, but is merely a concept for now and is not on my list of things to do yet.

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

## Testing

This OS gets tested thoroughly in `qemu-system-x86_64`, both in regular BIOS mode and UEFI. This is the main way I do development.

With that said, I take great pride in the fact that this OS runs on real hardware, and I routinely test on several systems.

[There is a dedicated section to real hardware testing in the docs](documentation/Testing/testing.md).
