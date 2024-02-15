# Testing

This outlines the several ways that WallOS is tested.

## Stages
The WallOS testing cycle goes through a few stages. See the next section to learn more about each stage.
- Stage 1 (Primary)
  - QEMU
    - If you don't like QEMU, don't feel forced to use it. You can skip to stage two for primary development if you wish.
    - Realize that your Pull Requests *will* be tested with Stage 1 and Stage 2.
- Stage 2 (Secondary)
  - Bochs
  - Virtualbox
- Stage 3 (Final)
  - Real hardware.
  -  It's impossible to test all hardware configurations, especially as a small hobby OS.
    - Feel free to test on your own hardware. If you run into bugs, create an issue on the GitHub and follow the correct template.

### How are stages used?
For almost all "general" development, QEMU is used. It's integrated well into the scripts, including the makefile. 
You don't have to use it for your own independent development, but it will still be tested using QEMU before being accepted into main.
Every once in a while, usually after a major feature has been added, stage 2 is tested. 
All changed/added functionalities of the kernel are tested in this stage. 
Stage 3 is only used before full kernel releases, mostly because it's a pain to set up. 
This *will* change if WallOS ever gets to the point of being self-hosted.

## Virtual Machines
### [QEMU](https://www.qemu.org/)
QEMU is a very versatile VM, which works very well and consistently, across all platforms that it supports (including macOS, Windows, and Linux).
- It's very quick to startup.
- It allows for virtually full control of the system through command line configuration, along with very helpful debug tools.
- Interfaces very well with GDB.
- For these reasons, QEMU is the main testing platform for WallOS.
  - QEMU is the basis of all development. It's used to test everything changed in the kernel.

### [Bochs](https://github.com/bochs-emu/Bochs)
Bochs is an open source x86_64 emulator. Like QEMU, it runs fairly well and fairly consistently across platforms.
- It has a slower startup time than QEMU.
- The config is more cumbersome, but allows for almost as much control as QEMU.
- Certain things are slow, especially relating to the framebuffer being updated.
- Interfaces well with GDB. 
- For these reasons, Bochs is a secondary testing platform for WallOS. 
  - This mostly means that it isn't used along every step of the way.

### [Virtualbox](https://www.virtualbox.org/wiki/VirtualBox)
Virtualbox is a very well-developed and tested virtual machine written by Oracle. 
- Supports a wide amount of customization
- More user-friendly interface than QEMU and Bochs.
- It is harder to get the proper setup to emulate a "normal" x86_64 system, since you have to go through and enable/disable certain features.
- While it *can* be used from the command line, it's more cumbersome to set up this way than either bochs or QEMU.
- It is slower on startup than either of the above
- It's also slower on kernel boot, and can lag a little behind with printing to the framebuffer.
- For these reasons, Virtualbox is a secondary testing platform for WallOS.
  - This mostly means that it isn't used along every step of the way.

> There are dozens more VMs that you can test on.
> Testing on other VMs is encouraged, but issues on other VMs will likely be ignored as long as it works properly on the above three.

## Real Hardware
The end-all-be-all for 