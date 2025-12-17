# ACPICA

All the code contained in this folder is *mostly* unmodified source code from the [ACPICA repository.](https://github.com/acpica/acpica)
Not all of ACPICA is used, WallOS only needs the kernel level components, and not the userspace tools (like the compiler/simulator/help utility/etc.).

## Version

The current version of ACPICA being used is: [G20240322](https://github.com/acpica/acpica/releases/tag/G20240322)

> Every update of ACPICA must be manually edited to work in WallOS.
> A branch for updating ACPICA will be created upon new releases.

## Building

A custom `makefile` is contained in this directory. It is NOT supposed to be used on it's own. It is to be invoked by `libs/libs.mk`.

## License

ACPICA is provided under several different licenses. WallOS chooses to use the second, which is copied below.
***This applies only to files in `./source/*` and `./include/*`. `./include/platform/acwallos.h` is EXCLUDED, and follows the same license as [WallOS itself.](../../LICENSE.md)***

```plaintext
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions, and the following disclaimer,
   without modification.
2. Redistributions in binary form must reproduce at minimum a disclaimer
   substantially similar to the "NO WARRANTY" disclaimer below
   ("Disclaimer") and any redistribution must be conditioned upon
   including a substantially similar Disclaimer requirement for further
   binary redistribution.
3. Neither the names of the above-listed copyright holders nor the names
   of any contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Modifications

A list of modifications are below:

2024-17-08:

- Added `acwallos.h` and `acwallosex.h`
- Modified `acenv.h` to include `acwallos.h`3
- Modified `acenvex.h` to include `acwallosex.h`

2024-19-08:

- Added pragma to `acpixf.h` to silence unused parameter warnings.
- Removed `acwallosex.h` and reverted changes to `acenvex.h` as it wasn't needed.
