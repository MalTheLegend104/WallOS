# Do some general includes and definitions for the rest of my makefiles.
MAKEFLAGS = -s
THIS_FILE := $(lastword $(MAKEFILE_LIST)) # useful for if the user tries to include other makefiles.
.DEFAULT_GOAL := qemu

# This is for qemu:
ARGS ?= -m 5G

# These make it much easier to change things whenever we are finally self hosted.
WALLOS_C_COMPILER 	:= x86_64-wallos-gcc
WALLOS_CXX_COMPILER := x86_64-wallos-g++
WALLOS_ASSEMBLER 	:= nasm
WALLOS_LINKER 		:= x86_64-wallos-ld

# We export them to make them available to other makefiles that this one will call.
# Currently this only applies to the ramfs, but eventually I plan on having a few built in programs.
export WALLOS_C_COMPILER
export WALLOS_CXX_COMPILER
export WALLOS_ASSEMBLER
export WALLOS_LINKER

# To make builds look nicer and easier to follow, we define and export the following:
COLOR_GREEN	  ?= \033[0;32m
COLOR_YELLOW  ?= \033[0;93m
COLOR_RED	  ?= \033[0;31m
COLOR_BLUE	  ?= \033[0;34m
COLOR_CYAN	  ?= \033[0;96m
COLOR_MAGENTA ?= \033[0;95m
END_COLOR	  ?= \033[0m

export COLOR_GREEN
export COLOR_YELLOW
export COLOR_RED
export COLOR_BLUE
export COLOR_CYAN
export COLOR_MAGENTA
export END_COLOR

# Default things for all platforms. This includes things like LIBC, the WallOS, and compile flags.
DEBUG_SYMBOLS   := 
C_FLAGS 		:= -ffreestanding -std=gnu99 -g -Wall -Wextra -Wno-format -nostdlib -lgcc -mno-red-zone -O0 -mcmodel=kernel $(DEBUG_SYMBOLS)
CPP_FLAGS 		:= -ffreestanding -std=c++11 -fno-rtti -g -Wall -Wextra -Wno-format -nostdlib -lgcc -mno-red-zone -O0 -mcmodel=kernel $(DEBUG_SYMBOLS)
NASM_FLAGS 		:= $(DEBUG_SYMBOLS)
LINKER_FLAGS 	:=

LIBC_INCLUDE	:= src/libc/include
KLIBC_INCLUDE 	:= src/kernel/klibc/include
KCORE_INCLUDE	:= src/kernel/kcore/include
ACPI_INCLUDE	:= src/acpi

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
# ACPI
# ----------------------------------------------------
ACPI_ASM_SRC 	:= $(shell find src/acpi -name *.asm)
ACPI_ASM_OBJ 	:= $(patsubst src/acpi/%.asm, build/acpi/%.o, $(ACPI_ASM_SRC))
ACPI_CPP_SRC 	:= $(shell find src/acpi -name *.cpp)
ACPI_CPP_OBJ 	:= $(patsubst src/acpi/%.cpp, build/acpi/%.o, $(ACPI_CPP_SRC))
ACPI_C_SRC   	:= $(shell find src/acpi -name *.c)
ACPI_C_OBJ   	:= $(patsubst src/acpi/%.c, build/acpi/%.o, $(ACPI_C_SRC))
ACPI_OBJ	 	:= $(ACPI_ASM_OBJ) $(ACPI_CPP_OBJ) $(ACPI_C_OBJ)
#filter out the 32 bit files
#ACPI_C_SRC 	:= $(filter-out src/acpi/kernel_early.c, $(ACPI_C_SRC))
# 32_ACPI_C_SRC	:= src/acpi/kernel_early.c
# 32_ACPI_C_OBJ 	:= build/kcore/kernel_early.o

$(ACPI_ASM_OBJ): build/acpi/%.o : src/acpi/%.asm
	echo "Compiling apci asm   -> $(patsubst build/acpi/%.o, src/acpi/%.asm, $@)"
	mkdir -p $(dir $@) && \
	$(WALLOS_ASSEMBLER) -f elf64 $(patsubst build/acpi/%.o, src/acpi/%.asm, $@) $(NASM_FLAGS) -o $@

$(ACPI_CPP_OBJ): build/acpi/%.o : src/acpi/%.cpp
	echo "Compiling acpi C++   -> $(patsubst build/acpi/%.o, src/acpi/%.cpp, $@)"
	mkdir -p $(dir $@) && \
	$(WALLOS_CXX_COMPILER) -D__is_kernel_ -c $(patsubst build/acpi/%.o, src/acpi/%.cpp, $@) -o $@ -I $(ACPI_INCLUDE) -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(CPP_FLAGS)

$(ACPI_C_OBJ): build/acpi/%.o : src/acpi/%.c
	echo "Compiling acpi C     -> $(patsubst build/acpi/%.o, src/acpi/%.c, $@)"
	mkdir -p $(dir $@) && \
	$(WALLOS_C_COMPILER) -D__is_kernel_ -c $(patsubst build/acpi/%.o, src/acpi/%.c, $@) -o $@ -I $(ACPI_INCLUDE) -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(C_FLAGS)


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
	$(WALLOS_CXX_COMPILER) -c $(patsubst build/klibc/%.o, src/kernel/klibc/%.cpp, $@) -o $@ -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(CPP_FLAGS) -D__is_kernel_

$(KLIBC_C_OBJ): build/klibc/%.o : src/kernel/klibc/%.c
	echo "Compiling klibc C    -> $(patsubst build/klibc/%.o, src/kernel/klibc/%.c, $@)"
	mkdir -p $(dir $@) && \
	$(WALLOS_C_COMPILER) -c $(patsubst build/klibc/%.o, src/kernel/klibc/%.c, $@) -o $@ -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(C_FLAGS) -D__is_kernel_

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
	$(WALLOS_CXX_COMPILER) -D__is_kernel_ -c $(patsubst build/kcore/%.o, src/kernel/kcore/%.cpp, $@) -o $@ -I $(KCORE_INCLUDE) -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(CPP_FLAGS) -D__is_kernel_

$(KCORE_C_OBJ): build/kcore/%.o : src/kernel/kcore/%.c
	echo "Compiling kcore C    -> $(patsubst build/kcore/%.o, src/kernel/kcore/%.c, $@)"
	mkdir -p $(dir $@) && \
	$(WALLOS_C_COMPILER) -D__is_kernel_ -c $(patsubst build/kcore/%.o, src/kernel/kcore/%.c, $@) -o $@ -I $(KCORE_INCLUDE) -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(C_FLAGS) -D__is_kernel_

# Compile kernel_early in 32 bit mode. kernel_early does NOT have access to any includes.
# $(32_KCORE_C_OBJ): build/kcore/%.o : src/kernel/kcore/%.c
# 	echo "Compiling kcore C    -> $(patsubst build/kcore/%.o, src/kernel/kcore/%.c, $@)"
# 	mkdir -p $(dir $@) && \
# 	i686-elf-gcc -D__is_kernel_ -c $(patsubst build/kcore/%.o, src/kernel/kcore/%.c, $@) -o $@ -m32 $(C_FLAGS)


# ----------------------------------------------------
# x86-64
# ----------------------------------------------------
x86_64_INCLUDE  := src/kernel/x86_64/include
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
	$(WALLOS_C_COMPILER) -c $< -o $@ -I $(x86_64_INCLUDE) -I $(KCORE_INCLUDE) -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(C_FLAGS) -D__is_kernel_

$(IDT_C_OBJ): build/x86_64/%.o : src/kernel/x86_64/%.c
	echo "Compiling IDT C      -> $<"
	mkdir -p $(dir $@) && \
	x86_64-elf-gcc -c $< -o $@ -mgeneral-regs-only -I $(x86_64_INCLUDE) -I $(KCORE_INCLUDE) -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(C_FLAGS) -D__is_kernel_

# ----------------------------------------------------
# COMMANDS
# ---------------------------------------------------------
# All the commands for building, cleaning, running, etc.
# all 	-> builds for all supported architectures
# build 		-> builds for x86-64 (default)
# build_arm64 	-> builds for Aarch64 <-------------------- This doesnt exist yet. It's just an example.
# clean 		-> deletes all build files, iso's, etc. 
# qemu			-> builds x86-64 iso, then runs in qemu

.PHONY: all build clean ramfs qemu

all: 
	$(MAKE) -f $(THIS_FILE) ramfs 

	echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"
	echo "$(COLOR_CYAN)<-----------------Building x86_64 Kernel------------------>$(END_COLOR)"
	echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"
	$(MAKE) -f $(THIS_FILE) build 
	echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"
	echo "$(COLOR_CYAN)<-----------------Finished x86_64 Kernel------------------>$(END_COLOR)"
	echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"

build: $(LIBC_OBJ) $(KLIBC_OBJ) $(KCORE_OBJ) $(x86_64_OBJ) $(IDT_C_OBJ) $(ACPI_OBJ)
	mkdir -p dist/x86_64
	echo "<---------------Linking--------------->"
	$(WALLOS_LINKER) -n -o dist/x86_64/WallOS.bin -T targets/x86_64/linker.ld font.o $(LIBC_OBJ) $(KLIBC_OBJ) $(x86_64_OBJ) $(IDT_C_OBJ) $(ACPI_OBJ) $(KCORE_OBJ)
	echo "<------------Compiling ISO------------>"
	cp dist/x86_64/WallOS.bin targets/x86_64/iso/boot/WallOS.bin && \
	grub-mkrescue /usr/lib/grub/i386-pc -o dist/x86_64/WallOS.iso targets/x86_64/iso
	echo "<---------Finished  Compiling--------->"
	
ramfs:
	cd src/ramfs && make && cd ../../

qemu: all
	qemu-system-x86_64 -cdrom dist/x86_64/WallOS.iso -cpu max $(ARGS)

clean:
	rm -rf build && echo "$(COLOR_GREEN)Cleaned build folder$(END_COLOR)"
	rm -rf dist && echo "$(COLOR_GREEN)Cleaned dist folder$(END_COLOR)"
	rm -rf src/output/ && echo "$(COLOR_GREEN)Cleaned ramfs output folder$(END_COLOR)"
	find ./targets/ -name "*.bin" -exec rm {} + && echo "$(COLOR_GREEN)Cleaned targets folder.$(END_COLOR)" || true
	echo "$(COLOR_CYAN)Finished Cleaning!$(END_COLOR)"