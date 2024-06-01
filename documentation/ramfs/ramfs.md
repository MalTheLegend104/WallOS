# Ramfs

The ramfs is very similar to initrd on linux. It's the temporary files that WallOS needs on boot.
These files are all located in `src/ramfs/`. This folder contains subfolders, each of which can contain it's own build system.

This ramfs supports several types of other buildsystems:

- Makefile
- CMake
- Meson

## Exports

The ramfs buildsystem supports many nice exports (that you should 100% use to ensure compatibility):

- WALLOS_C_COMPILER
- WALLOS_C_FLAGS
- WALLOS_CXX_COMPILER
- WALLOS_CXX_FLAGS
- WALLOS_ASSEMBLER
- WALLOS_ASM_FLAGS
- WALLOS_LINKER
- WALLOS_LD_FLAGS

> It is expected that everything uses these exports.
> If you do not use these exports, make sure you are 100% certain it's configured correctly.
> You ***can not*** just use the regular system GCC or Binutils.
> Building for WallOS requires the use of a cross compiler, which the above exports provide access to.

## Make

All you have to do for make is ensure you use the above exports in place of your c and c++ compilers.
The c and c++ compilers are both custom versions of gcc, meaning they support most of the same flags as regular gcc.
In addition, the `__wallos__` macro is defined, as is `__unix__`. It is advised to use the c and cxx flags, bit it is not required.
This information is the same for the linker, advised but not necessary. Just ensure the output of the executable is `elf64`.
The assembler is nasm by default, meaning you should still use `gas` or any other assembler if required.
The assembler flags are generally empty but are reserved for later usage.

## CMake

The same information as [make](#make) applies. You do have to access it in a certain way though:

```cmake
# accessing any of the fields requires you to access the environment:
set(CMAKE_CXX_COMPILER $ENV{WALLOS_CXX_COMPILER})
```

> You may also have to disable some sanity checks for the compiler (since it's a cross compiler).

## Meson

To be entirely honest, meson was an afterthought, but it still might eventually need to be used.
When adding a meson project, attempt to follow the same advice as [make](#make). The exports are environment variables.
There is also a meson cross-file in the `scripts/src/` folder, titled `wallos-crossfile.txt`.
This sets the compiler, as well as some common binutils tools, to their correct values.
