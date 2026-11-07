# Do some general includes and definitions for the rest of my makefiles.
MAKEFLAGS = -s
THIS_FILE := $(lastword $(MAKEFILE_LIST)) # useful for if the user tries to include other makefiles.
.DEFAULT_GOAL := qemu

# Other makefiles for other components
include libs/libs.mk
include src/initrd/initrd.mk
-include config/make/terminal.mk # adds console colors if found

# If we have a config file, include it, same for qemu config
-include config/make/config.mk
-include config/make/qemu_args.mk

# Default args from qemu
# If they weren't already defined in config/make/qemu_args.mk
ARGS ?= -vga std -m 5G -M hpet=on -machine q35 -cpu max -smp 4 -serial stdio

# Distcc is pulled in from config.mk if we have it. If not, it just uses the local system
WALLOS_C_COMPILER 	:= $(DISTCC) x86_64-wallos-gcc
WALLOS_CXX_COMPILER := $(DISTCC) x86_64-wallos-g++
WALLOS_ASSEMBLER 	:= $(DISTCC) nasm
WALLOS_LINKER 		:= x86_64-wallos-ld
WALLOS_OBJCOPY 		:= x86_64-wallos-objcopy
# The final link goes through the gcc driver (not ld directly) so the LTO plugin and backend run.
# No distcc here, linking is local.
WALLOS_LINK_DRIVER	:= x86_64-wallos-gcc

# We export them to make them available to other makefiles that this one will call.
export WALLOS_C_COMPILER
export WALLOS_CXX_COMPILER
export WALLOS_ASSEMBLER
export WALLOS_LINKER
export WALLOS_OBJCOPY

FORCED_INCLUDES = -include $(CURDIR)/config/wallos_debug_macros.h -include $(CURDIR)/config/wallos_config.h

# Default things for all platforms. This includes things like LIBC, the WallOS, and compile flags.
DEBUG_SYMBOLS   :=
C_FLAGS 		:= -ffreestanding -std=gnu99 -Wall -Wextra -Wno-format -nostdlib -lgcc -mno-red-zone -O2 -mcmodel=kernel $(DEBUG_SYMBOLS) $(FORCED_INCLUDES)
CPP_FLAGS 		:= -ffreestanding -std=c++11 -fno-rtti -Wall -Wextra -Wno-format -nostdlib -lgcc -mno-red-zone -O2 -mcmodel=kernel $(DEBUG_SYMBOLS) $(FORCED_INCLUDES)
NASM_FLAGS 		:= $(DEBUG_SYMBOLS)
LINKER_FLAGS 	:=

# ----------------------------------------------------
# LTO
# ----------------------------------------------------
# LTO is opt-in per object
LTO_FLAGS       := -flto -ffat-lto-objects

# Source files (matched by file name, in any directory) that get link time optimized.
# Be selective with this
LTO_SOURCES     := display.cpp

# ----------------------------------------------------
# Flags for libs.mk
# ----------------------------------------------------
WALLOS_C_FLAGS   := $(C_FLAGS)
WALLOS_CXX_FLAGS := $(CPP_FLAGS)
WALLOS_ASM_FLAGS := $(NASM_FLAGS)
WALLOS_LTO_FLAGS := $(LTO_FLAGS)
export WALLOS_C_FLAGS
export WALLOS_CXX_FLAGS
export WALLOS_ASM_FLAGS
export WALLOS_LTO_FLAGS

# Framebuffer settings
# We want to make sure they are all set, and don't want the defines unless they are
ifneq ($(FRAMEBUFFER_WIDTH),)
ifneq ($(FRAMEBUFFER_HEIGHT),)
ifneq ($(FRAMEBUFFER_BPP),)
NASM_FLAGS += \
	-DFB_WIDTH=$(FRAMEBUFFER_WIDTH) \
	-DFB_HEIGHT=$(FRAMEBUFFER_HEIGHT) \
	-DFB_BPP=$(FRAMEBUFFER_BPP)
endif
endif
endif

LIBC_INCLUDE	:= src/libc/include
KLIBC_INCLUDE 	:= src/kernel/klibc/include
KCORE_INCLUDE	:= src/kernel/kcore/include
x86_64_INCLUDE  := src/kernel/x86_64/include
ACPI_INCLUDE	:= src/acpi

# Export the includes for libraries in ./libs
export KLIBC_INCLUDE
export LIBC_INCLUDE
export x86_64_INCLUDE

# ----------------------------------------------------
# LIBC
# ----------------------------------------------------
LIBC_ASM_SRC  	:= $(shell find src/libc -name *.asm)
LIBC_ASM_OBJ  	:= $(patsubst src/libc/%.asm, build/libc/%.o, $(LIBC_ASM_SRC))
LIBC_CPP_SRC  	:= $(shell find src/libc -name *.cpp)
LIBC_CPP_OBJ  	:= $(patsubst src/libc/%.cpp, build/libc/%.o, $(LIBC_CPP_SRC))
LIBC_C_SRC    	:= $(shell find src/libc -name *.c)
LIBC_C_OBJ    	:= $(patsubst src/libc/%.c, build/libc/%.o, $(LIBC_C_SRC))
LIBC_OBJ	  	:= $(LIBC_ASM_OBJ) $(LIBC_CPP_OBJ) $(LIBC_C_OBJ)

$(LIBC_ASM_OBJ): build/libc/%.o : src/libc/%.asm
	echo "Compiling libc asm   -> $(patsubst build/libc/%.o, src/libc/%.asm, $@)"
	mkdir -p $(dir $@) && \
	$(WALLOS_ASSEMBLER) -f elf64 $(patsubst build/libc/%.o, src/libc/%.asm, $@) $(NASM_FLAGS) -o $@

$(LIBC_CPP_OBJ): build/libc/%.o : src/libc/%.cpp
	echo "Compiling libc C++   -> $(patsubst build/libc/%.o, src/libc/%.cpp, $@)"
	mkdir -p $(dir $@) && \
	$(WALLOS_CXX_COMPILER) -c $(patsubst build/libc/%.o, src/libc/%.cpp, $@) -o $@ -I $(LIBC_INCLUDE) $(CPP_FLAGS)

$(LIBC_C_OBJ): build/libc/%.o : src/libc/%.c
	echo "Compiling libc C     -> $(patsubst build/libc/%.o, src/libc/%.c, $@)"
	mkdir -p $(dir $@) && \
	$(WALLOS_C_COMPILER) -c $(patsubst build/libc/%.o, src/libc/%.c, $@) -o $@ -I $(LIBC_INCLUDE) $(C_FLAGS)


# ----------------------------------------------------
# KLIBC
# ----------------------------------------------------
KLIBC_ASM_SRC 	:= $(shell find src/kernel/klibc -name *.asm)
KLIBC_ASM_OBJ 	:= $(patsubst src/kernel/klibc/%.asm, build/klibc/%.o, $(KLIBC_ASM_SRC))
KLIBC_CPP_SRC 	:= $(shell find src/kernel/klibc -name *.cpp)
KLIBC_CPP_OBJ 	:= $(patsubst src/kernel/klibc/%.cpp, build/klibc/%.o, $(KLIBC_CPP_SRC))
KLIBC_C_SRC   	:= $(shell find src/kernel/klibc -name *.c)
KLIBC_C_OBJ   	:= $(patsubst src/kernel/klibc/%.c, build/klibc/%.o, $(KLIBC_C_SRC))
KLIBC_OBJ	  	:= $(KLIBC_ASM_OBJ) $(KLIBC_CPP_OBJ) $(KLIBC_C_OBJ)

$(KLIBC_ASM_OBJ): build/klibc/%.o : src/kernel/klibc/%.asm
	echo "Compiling klibc asm  -> $(patsubst build/klibc/%.o, src/kernel/klibc/%.asm, $@)"
	mkdir -p $(dir $@) && \
	$(WALLOS_ASSEMBLER) -f elf64 $(patsubst build/klibc/%.o, src/kernel/klibc/%.asm, $@) $(NASM_FLAGS) -o $@

$(KLIBC_CPP_OBJ): build/klibc/%.o : src/kernel/klibc/%.cpp
	echo "Compiling klibc C++  -> $(patsubst build/klibc/%.o, src/kernel/klibc/%.cpp, $@)"
	mkdir -p $(dir $@) && \
	$(WALLOS_CXX_COMPILER) -c $(patsubst build/klibc/%.o, src/kernel/klibc/%.cpp, $@) -o $@ -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(LIBRARY_INCLUDES) $(CPP_FLAGS) -D__is_kernel_

$(KLIBC_C_OBJ): build/klibc/%.o : src/kernel/klibc/%.c
	echo "Compiling klibc C    -> $(patsubst build/klibc/%.o, src/kernel/klibc/%.c, $@)"
	mkdir -p $(dir $@) && \
	$(WALLOS_C_COMPILER) -c $(patsubst build/klibc/%.o, src/kernel/klibc/%.c, $@) -o $@ -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(LIBRARY_INCLUDES) $(C_FLAGS) -D__is_kernel_

# ----------------------------------------------------
# KCORE
# ----------------------------------------------------
KCORE_ASM_SRC 	:= $(shell find src/kernel/kcore -name *.asm)
KCORE_ASM_OBJ 	:= $(patsubst src/kernel/kcore/%.asm, build/kcore/%.o, $(KCORE_ASM_SRC))
KCORE_CPP_SRC 	:= $(shell find src/kernel/kcore -name *.cpp)
KCORE_CPP_OBJ 	:= $(patsubst src/kernel/kcore/%.cpp, build/kcore/%.o, $(KCORE_CPP_SRC))
KCORE_C_SRC   	:= $(shell find src/kernel/kcore -name *.c)
KCORE_C_OBJ   	:= $(patsubst src/kernel/kcore/%.c, build/kcore/%.o, $(KCORE_C_SRC))
KCORE_OBJ	 	:= $(KCORE_ASM_OBJ) $(KCORE_CPP_OBJ) $(KCORE_C_OBJ)
#filter out the 32 bit files
#KCORE_C_SRC 	:= $(filter-out src/kernel/kcore/kernel_early.c, $(KCORE_C_SRC))
# 32_KCORE_C_SRC	:= src/kernel/kcore/kernel_early.c
# 32_KCORE_C_OBJ 	:= build/kcore/kernel_early.o

$(KCORE_ASM_OBJ): build/kcore/%.o : src/kernel/kcore/%.asm
	echo "Compiling kcore asm  -> $(patsubst build/kcore/%.o, src/kernel/kcore/%.asm, $@)"
	mkdir -p $(dir $@) && \
	$(WALLOS_ASSEMBLER) -f elf64 $(patsubst build/kcore/%.o, src/kernel/kcore/%.asm, $@) $(NASM_FLAGS) -o $@

$(KCORE_CPP_OBJ): build/kcore/%.o : src/kernel/kcore/%.cpp
	echo "Compiling kcore C++  -> $(patsubst build/kcore/%.o, src/kernel/kcore/%.cpp, $@)"
	mkdir -p $(dir $@) && \
	$(WALLOS_CXX_COMPILER) -D__is_kernel_ -c $(patsubst build/kcore/%.o, src/kernel/kcore/%.cpp, $@) -o $@ $(LIBRARY_INCLUDES) -I $(KCORE_INCLUDE) -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(CPP_FLAGS) -D__is_kernel_

$(KCORE_C_OBJ): build/kcore/%.o : src/kernel/kcore/%.c
	echo "Compiling kcore C    -> $(patsubst build/kcore/%.o, src/kernel/kcore/%.c, $@)"
	mkdir -p $(dir $@) && \
	$(WALLOS_C_COMPILER) -D__is_kernel_ -c $(patsubst build/kcore/%.o, src/kernel/kcore/%.c, $@) -o $@ $(LIBRARY_INCLUDES) -I $(KCORE_INCLUDE) -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(C_FLAGS) -D__is_kernel_

# Compile kernel_early in 32 bit mode. kernel_early does NOT have access to any includes.
# $(32_KCORE_C_OBJ): build/kcore/%.o : src/kernel/kcore/%.c
# 	echo "Compiling kcore C    -> $(patsubst build/kcore/%.o, src/kernel/kcore/%.c, $@)"
# 	mkdir -p $(dir $@) && \
# 	i686-elf-gcc -D__is_kernel_ -c $(patsubst build/kcore/%.o, src/kernel/kcore/%.c, $@) -o $@ -m32 $(C_FLAGS)


# ----------------------------------------------------
# x86-64
# ----------------------------------------------------
x86_64_ASM_SRCS := $(shell find src/kernel/x86_64 -name *.asm)
x86_64_ASM_OBJ  := $(patsubst src/kernel/x86_64/%.asm, build/x86_64/%.o, $(x86_64_ASM_SRCS))
x86_64_C_SRC    := $(shell find src/kernel/x86_64 -name *.c)
IDT_C_SRC       := $(filter src/kernel/x86_64/idt/%.c, $(x86_64_C_SRC)) # filter only idt/*.c files
x86_64_C_SRC    := $(filter-out src/kernel/x86_64/idt/%.c, $(x86_64_C_SRC)) # remove idt/*.c from x86_64_C_SRC

x86_64_C_OBJ    := $(patsubst src/kernel/x86_64/%.c, build/x86_64/%.o, $(x86_64_C_SRC))

IDT_C_OBJ       := $(patsubst src/kernel/x86_64/%.c, build/x86_64/%.o, $(IDT_C_SRC))

x86_64_OBJ := $(x86_64_ASM_OBJ) $(x86_64_C_OBJ)

$(x86_64_ASM_OBJ): build/x86_64/%.o : src/kernel/x86_64/%.asm
	echo "Compiling x86_64 asm -> $<"
	mkdir -p $(dir $@) && \
	$(WALLOS_ASSEMBLER) -f elf64 $< $(NASM_FLAGS) -o $@

$(x86_64_C_OBJ): build/x86_64/%.o : src/kernel/x86_64/%.c
	echo "Compiling x86_64 C   -> $<"
	mkdir -p $(dir $@) && \
	$(WALLOS_C_COMPILER) -c $< -o $@ -I $(x86_64_INCLUDE) $(LIBRARY_INCLUDES) -I $(KCORE_INCLUDE) -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(C_FLAGS) -D__is_kernel_

$(IDT_C_OBJ): build/x86_64/%.o : src/kernel/x86_64/%.c
	echo "Compiling IDT C      -> $<"
	mkdir -p $(dir $@) && \
	$(WALLOS_C_COMPILER) -c $< -o $@ -mgeneral-regs-only -I $(x86_64_INCLUDE) $(LIBRARY_INCLUDES) -I $(x86_64_INCLUDE) -I $(KCORE_INCLUDE) -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(C_FLAGS) -D__is_kernel_

# ----------------------------------------------------
# LTO OBJECT SELECTION
# ----------------------------------------------------
# Turn LTO_SOURCES into object paths
LTO_CANDIDATES := $(LIBC_OBJ) $(KLIBC_OBJ) $(KCORE_OBJ) $(x86_64_OBJ)
LTO_OBJS       := $(foreach src,$(LTO_SOURCES),$(filter %/$(basename $(src)).o,$(LTO_CANDIDATES)))

# These only apply while building the listed objects
$(LTO_OBJS): C_FLAGS   += $(LTO_FLAGS)
$(LTO_OBJS): CPP_FLAGS += $(LTO_FLAGS)

# Handy to check that the right files were picked up: make lto_list
.PHONY: lto_list
lto_list:
	echo "LTO objects: $(LTO_OBJS)"

# ----------------------------------------------------
# COMMANDS
# ---------------------------------------------------------
# All the commands for building, cleaning, running, etc.
# all 	-> builds for all supported architectures
# build 		-> builds for x86-64 (default)
# build_arm64 	-> builds for Aarch64 <-------------------- This doesnt exist yet. It's just an example.
# clean 		-> deletes all build files, iso's, etc.
# qemu			-> builds x86-64 iso, then runs in qemu

.PHONY: all build clean qemu

all:
	+$(MAKE) -f $(THIS_FILE) initrd
	+$(MAKE) -f $(THIS_FILE) libs
	+$(MAKE) -f $(THIS_FILE) build

build: $(LIBC_OBJ) $(KLIBC_OBJ) $(KCORE_OBJ) $(x86_64_OBJ) $(IDT_C_OBJ) initrd_temp
	echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"
	echo "$(COLOR_CYAN)<-----------------Building x86_64 Kernel------------------>$(END_COLOR)"
	echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"
	mkdir -p dist/x86_64
	echo "<---------------Linking--------------->"
	$(WALLOS_LINK_DRIVER) -flto=auto -O2 -mcmodel=kernel -mno-red-zone \
	    -ffreestanding -nostdlib -static -Wl,-n \
	    -T targets/x86_64/linker.ld -o dist/x86_64/WallOS.bin \
	    dist/initrd.o $(LIBC_OBJ) $(KLIBC_OBJ) $(x86_64_OBJ) $(IDT_C_OBJ) $(KCORE_OBJ) $(LIBRARY_FLAGS) $(LINKER_FLAGS) -lgcc
	echo "<------------Compiling ISO------------>"
# 	cp dist/x86_64/WallOS.bin targets/x86_64/iso/boot/WallOS.bin && \
#	grub-mkrescue /usr/lib/grub/i386-pc -o dist/x86_64/WallOS.iso targets/x86_64/iso
# This in theory makes it work for both regular BIOS and UEFI.
# I think this works in practice but I need to actually finish apollo before I can fully test it.
	cp dist/x86_64/WallOS.bin targets/x86_64/iso/boot/WallOS.bin && \
	grub-mkrescue -o dist/x86_64/WallOS.iso targets/x86_64/iso --modules="multiboot2"
	echo "<---------Finished  Compiling--------->"

	echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"
	echo "$(COLOR_CYAN)<-----------------Finished x86_64 Kernel------------------>$(END_COLOR)"
	echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"

initrd_temp:
	echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"
	echo "$(COLOR_CYAN)<------------------INITRD_TEMP_OBJCOPY-------------------->$(END_COLOR)"
	echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"
	echo "$(WALLOS_OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 dist/initrd.img dist/initrd.o --rename-section .data=.initrd"
	$(WALLOS_OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 dist/initrd.img dist/initrd.o --rename-section .data=.initrd


qemu: all
# 	qemu-system-x86_64 -cdrom dist/x86_64/WallOS.iso  -cpu max $(ARGS)
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -cdrom dist/x86_64/WallOS.iso $(ARGS)

qemu_only:
	qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd -cdrom dist/x86_64/WallOS.iso $(ARGS)

qemu_bios:
	qemu-system-x86_64 -cdrom dist/x86_64/WallOS.iso  -cpu max $(ARGS)

clean: libs_clean initrd_clean
	rm -rf build && echo "$(COLOR_GREEN)Cleaned build folder$(END_COLOR)"
	rm -rf dist && echo "$(COLOR_GREEN)Cleaned dist folder$(END_COLOR)"
	find ./targets/ -name "*.bin" -exec rm {} + && echo "$(COLOR_GREEN)Cleaned targets folder.$(END_COLOR)" || true
	echo "$(COLOR_CYAN)Finished Cleaning!$(END_COLOR)"