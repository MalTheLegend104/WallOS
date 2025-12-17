# Libs

This folder contains all external libraries used in WallOS. Each subfolder contains it's own isolated buildsystem and source files, as well as a README detailing how the library is used.

## Licenses

A lot of the licenses of the included libraries are not the same as WallOS. Each folder contains information about the particular license used.

## Building

This folder contains a `makefile` that calls each independent build system for each library.
All compiled libraries will be output in a `output` folder contained in this directory.
