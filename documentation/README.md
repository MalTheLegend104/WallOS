# Documentation
This folder contains a series of documentation, ranging across anything to do with this repo.
Most of the things in this folder are referenced elsewhere in the project.
> This folder in general is for documentation relating to the project itself, not code documentation. 
> See the [doxygen section](#doxygen) for code documentation.


## Doxygen
As shown in the [documentation guide](General/documentation.md), most of WallOS should be commented with doxygen comments.
If you want general code documentation, you can use [doxygen](https://www.doxygen.nl/index.html) to generate them.

## Categories

### General Information
- [Documentation](General/documentation.md)
- [Code Style](General/code-style.md)
- [Contribution Guide](General/contributing.md)

### Terminal Information
- Run `help` in the terminal to get a list of commands.
- You can run `help -s <string>` to get a list of commands that start with `<string>`
- [Command Creation](Terminal/command_creation.md)

### Memory Information
- [Memory System Structure](Memory/memory_structure.md)
- [Ramblings on memory addresses](../src/kernel/klibc/memory/virtual_mem.cpp)