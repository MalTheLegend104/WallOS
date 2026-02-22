# WallOS

WallOS (pronounced Wall-OS) is a 64-bit hobby operating system currently targeting x86_64, with plans for AArch64 and other architectures in the future.

> Disclaimer: Currently, the CI/CD is broken. This build "requires" (not really, it could still be built with x86_64-elf-gcc) a custom cross-compiler.
> More documentation is "on the way" for this, but definitely not a priority.

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
      - I will implement at least the *interface* for a proper scheduler, even if I only care about basic round-robin multithreading for now.
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
     It's a pain to add support for new things. Basically all VMM issues I've had have stemmed from the original PMM design, though, so this is on the back burner until I *really* need 4KB pages.
- Buildsystem
   - The `make` based buildsystem has worked great for basically the entire length of the project. With that said, I hate it. It's a pain to maintain, and I have to basically relearn how and why I did everything the way I did it anytime I need to make changes.
   - I ideally want something smoother to use, likely CMake (which I've tried implementing unsuccessfully several times, mainly due to me giving up halfway through each time). 
- Major Refactor
   - A ton of things are written with only x86_64 in mind, and should ideally be abstracted away into proper interfaces. For example, time depends on the x86 RTC, serial depends on x86 CPU I/O ports, etc. 

### Recently Completed

1. PMM
   - Physical memory manager has finally been rewritten. It now uses a buddy allocator system, and is *way* more robust than the old one.
   - The interface for this is much more intuitive than the old one, and it now supports things that aren't 2MB blocks
      - It still has the old interface, as I haven't rewritten my VMM (and don't plan to for quite a while). 
   - Supports up to 8MB allocations in O(1) time, with 4KB chunks being the minimum allocation size.
3. Framebuffer Graphics
   - This uses my `Apollo Graphics Framework`, designed to work with basically any pixel-based framebuffer.
   - This works very well. It was (and still is) extensively tested independently of this project, and is used in some other private projects of mine.
   - This *is* compiled using `-O3`, which hasn't caused problems yet, but potentially could depending on how GCC is feeling at compile time.
   - This also allowed us to finally have UEFI support. The main `makefile` has to be changed depending on whether you want a UEFI/BIOS or pure BIOS build.

### Kernel

This section is a very basic description of each "module" of the kernel. I really need to just spend a couple of days updating my documentation and putting it in the proper place, but ¯\\\_(ツ)\_/¯.

#### KCore

The core kernel files. This is mostly related to things like the kernel entrypoint, kernel panics, and important drivers (like serial and PS/2).
Pretty much all headers for this are located in [klibc](#Klibc) to make them accessible to the rest of the system. (There's definitely room for buildsystem improvements).

My past self didn't really plan on supporting anything other than x86_64, which has caused present me much pain.
The kernel C entrypoint really should just be part of the x86_64 architecture folder, alongside the regular entrypoint.
The current entrypoint contained here has a lot of dependence on x86_64 system initialization and isn't particularly well abstracted.

#### KLibc

Most of the rest of the kernel subsystems and interfaces, including memory management, syscalls, and the kernel services terminal. This will likely be renamed in the future, after userspace is established.

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

Basically, it's a 2MB (constant size), R/W (but writes aren't saved across reset, obviously), Fat12 filesystem that's appended to the end of the kernel at link time, and distributed as part of the `.bin`.
It's designed to carry the *absolute minimum* required to get the system booted, which is really only a config (that doesn't even do anything) for now.

### ACPI

ACPI is essentially handled as a driver by WallOS. It's currently contained in the `x86_64` folder (because I initially couldn't think of where to put it and said "eh whatever, I'll move it later"...).
It will likely get moved to its own dedicated spot later on, if other architectures are ever added.
The driver more so acts like an abstraction layer, along with providing the Operating System Layer (OSL) for [ACPICA](https://www.intel.com/content/www/us/en/developer/topic-technology/open/acpica/overview.html).
WallOS also has a built-in layer for [uACPI](https://github.com/uACPI/uACPI), along with an interface (which still needs a ton of work) that lets the OS not particularly care about which subsystem it was compiled with.

There are advantages to both subsystems:
- `uACPI`
   - Significantly faster
   - OS Layer is better implemented
      - This is on me, ACPICA support is *much* older, and I've gained a ton of experience by the time I wrote this OSL.
- `ACPICA`
   - "Reference" implementation by Intel
   - Much easier to use for ACPI debugging
   - Requires a lot less of the OSL to actually be implemented to work.

It really doesn't interact with much of the OS by itself and is mostly a standalone module.
In terms of structure, it sits somewhere between `klibc` and `kcore`. Whenever a robust interface for drivers is set up, this is likely to change.

### Sys Calls

System calls will likely be held in a single header, among the likes of <Windows.h> on Windows. If this isn't achievable, we will likely follow the unix-like <sys/header>. This will be determined at a later date, after the userspace is fully designed.

These will take a call convention of `int 0x42` on x86_64 (along with implementing actual `syscall/sysenter` support later on). I also plan on (potentially) supporting `int 0x80` for portability support.

This will get its own proper dedicated documentation whenever I get around to actually implementing the syscalls.

## Documentation

All current documentation can be found [here.](documentation/README.md)

### Contributing Documentation

All code that needs to be documented should be done so by following the rules of [doxygen](https://www.doxygen.nl/). It allows for JavaDoc like commenting, along with other common styles.
> I'm a former Java dev, and heavily prefer the JavaDoc style `@tag` as opposed to the `\\tag`. If committing, please use the JavaDoc style tag.

```cpp
/**
* @brief This is example documentation.
*  
* @param a - an integer doing xyz.
* @return int - some integer.
*/
int test(int a);
```

For the record, I'm ***VERY*** bad at actually remembering to document things as I'm implementing them.
I tend to get into a flow state and just keep writing code without documenting it as I go, and future me hates me for it.

This is okay for some things, as certain interfaces are self-explanatory, but other things need a lot more documentation (***cough*** PMM and SATA PIO ***cough***).

## Contributing

There are many ways to contribute to the project:

- Simply report any bugs or make suggestions.
- Look through `bug` and `feature-requests` tags in issues for something that interests you.
   - I don't really use the `issues` tab much. Most of what I want implemented is marked with `TODO` in the code or earlier in this document in the [todo](#todo) section.
- Review the codebase and changes to see if you find any bugs or potential optimizations.
   - Keep in mind I actively choose to *not* optimize things at the expense of readability most of the time. There are some exceptions (graphics, for example), but generally, please favor readability above all else.
     This OS is meant to be a learning experience for me (and others), not something that actually ends up being used in the real world.
      - With this in mind, if you see something that is *actively* causing bad performance or making the OS unusable, don't feel bad about optimizing it. This is what I went through with framebuffer graphics and the PMM.
- Participate in the discussion board.
  - You can ask questions, help others out, talk about potential features, etc.
  - I am *more than happy* to talk about anything to do with the OS (and can ramble for hours), don't be afraid to reach out.

If you are interested in fixing issues, adding features, or otherwise contributing to the codebase, read the [contribution guide](documentation/General/contributing.md).

## Building

> This section is a bit rambly, sorry about that...

#### Building results in a `*.iso` file, and an associated binary file being put in `/dist/<platform architecture>/`. This iso CAN be deployed to actual systems.

The only way to build this currently is using a gcc cross compiler. This can be done on any system that supports it (tested on FreeBSD, several Linux distros, WSL).

Currently, all build files require the usage of `x86_64-wallos-*` binaries. The process of building these is long and complex, and (as is a trend here) not documented.

The build *can* be done using `x86_64-elf-*` options from binutils and gcc, but all of the makefiles will need to be changed (or aliased to `x86_64-wallos-*` but that's probably a bad idea).

I hope to distribute a `docker` image to remove the pain of building eventually, but the process of making `x86_64-wallos-*` binaries is nowhere near good enough for a Docker image yet.

### Packages

#### apt

- `dosfstools`
- `build-essential`
- `xorriso`
- `qemu-system`
  - This package has been completely different in the past, and might not even be the correct package.
  - We need `qemu-system-x86_64`, you can look it up if `qemu-system` doesn't install it.

#### pacman

I have built WallOS on Arch before... I didn't keep track of the packages...

If I end up building it on Arch again, I will put all the packages here (or if someone else does, I'd appreciate a pull request for this list...).

## Testing

This OS gets tested thoroughly in `qemu-system-x86_64`, both in regular BIOS mode and UEFI. This is the main way I do development.

With that said, I take great pride in the fact that this OS runs on real hardware, and I routinely test on several systems. 

[There is a dedicated section to real hardware testing in the docs](documentation/Testing/testing.md).

