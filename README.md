# WallOS

It's an x86-64 os. It does OS thing 

## Documentation

All code that needs to be documented should be done so by following the rules of [doxygen](https://www.doxygen.nl/). It allows for JavaDoc like commenting, along with other common styles.

```cpp
/**
* This is example documentation.
*  
* @param a - an integer doing xyz.
* @return int - some integer.
*/
int test(int a);
```

## TODO

#### Unordered/Long Term

- GUI
- Command Handler
- Userspace Applications
- Multitasking

#### Ordered

- IDT
- GDT
- Virtual Memory (paging/malloc/free/new/delete)
- Basic I/O

## Building

In `/scripts/`, there exist commands for setting up the dev environment for the project. Windows commands are the `*.bat` files, while the `*.sh` and files without extentions are for linux.

> You must have docker installed already on Linux and Windows. On windows, I highly recommend installing qemu.

To build, simply run the script to build for the desired architecture. `build64` and `build.bat` both build for `x86_64`. Running `qemu` or `qemu.bat` both result in `x86_64` being built and ran in a vm. 

###### Building results in the `*.iso` file, and associated binary file being put in `/dist/`. This iso CAN be deployed to actual systems.
