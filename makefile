MAKEFLAGS = -s
#default things for all platforms. This includes things like LIBC, the WallOS, and compile flags.
DEBUG_SYMBOLS   := 
C_FLAGS 		:= -ffreestanding -std=gnu99 -g -Wall -Wextra -Wno-format -nostdlib -lgcc -mno-red-zone -O0 -mcmodel=kernel $(DEBUG_SYMBOLS)
CPP_FLAGS 		:= -ffreestanding -fno-rtti -g -Wall -Wextra -Wno-format -nostdlib -lgcc -mno-red-zone -O0 -mcmodel=kernel $(DEBUG_SYMBOLS)
NASM_FLAGS 		:= $(DEBUG_SYMBOLS)
LINKER_FLAGS 	:=

LIBC_INCLUDE	:= src/libc/include
KLIBC_INCLUDE 	:= src/kernel/klibc/include
KCORE_INCLUDE	:= src/kernel/kcore/include

CRTBEGIN_OBJ:=$(shell $(CC) $(CFLAGS) -print-file-name=crtbegin.o)
CRTEND_OBJ:=$(shell $(CC) $(CFLAGS) -print-file-name=crtend.o)

# ----------------------------------------------------
# CRTI/CRTN (stuff required for full C++ support)
# ----------------------------------------------------
CRTI_SRC	:= $(shell find src/kernel -name crti.s)
CRTI_OBJ	:= $(patsubst src/kernel/%.s, build/crt/%.o, $(CRTI_SRC))
$(CRTI_OBJ): build/crt/%.o : src/kernel/%.s
	echo "Compiling CRTI       -> $(patsubst build/crt/%.o, src/kernel/%.s, $@)"
	mkdir -p $(dir $@) && \
	as $(patsubst build/crt/%.o, src/kernel/%.s, $@) $(NASM_FLAGS) -o $@

CRTN_SRC	:= $(shell find src/kernel -name crtn.s)
CRTN_OBJ	:= $(patsubst src/kernel/%.s, build/crt/%.o, $(CRTN_SRC))
$(CRTN_OBJ): build/crt/%.o : src/kernel/%.s
	echo "Compiling CRTN       -> $(patsubst build/crt/%.o, src/kernel/%.s, $@)"
	mkdir -p $(dir $@) && \
	as $(patsubst build/crt/%.o, src/kernel/%.s, $@) $(NASM_FLAGS) -o $@

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
	nasm -f elf64 $(patsubst build/libc/%.o, src/libc/%.asm, $@) $(NASM_FLAGS) -o $@

$(LIBC_CPP_OBJ): build/libc/%.o : src/libc/%.cpp
	echo "Compiling libc C++   -> $(patsubst build/libc/%.o, src/libc/%.cpp, $@)"
	mkdir -p $(dir $@) && \
	x86_64-elf-g++ -c $(patsubst build/libc/%.o, src/libc/%.cpp, $@) -o $@ -I $(LIBC_INCLUDE) $(CPP_FLAGS)

$(LIBC_C_OBJ): build/libc/%.o : src/libc/%.c
	echo "Compiling libc C     -> $(patsubst build/libc/%.o, src/libc/%.c, $@)"
	mkdir -p $(dir $@) && \
	x86_64-elf-gcc -c $(patsubst build/libc/%.o, src/libc/%.c, $@) -o $@ -I $(LIBC_INCLUDE) $(C_FLAGS)

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
	nasm -f elf64 $(patsubst build/klibc/%.o, src/kernel/klibc/%.asm, $@) $(NASM_FLAGS) -o $@

$(KLIBC_CPP_OBJ): build/klibc/%.o : src/kernel/klibc/%.cpp
	echo "Compiling klibc C++  -> $(patsubst build/klibc/%.o, src/kernel/klibc/%.cpp, $@)"
	mkdir -p $(dir $@) && \
	x86_64-elf-g++ -c $(patsubst build/klibc/%.o, src/kernel/klibc/%.cpp, $@) -o $@ -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(CPP_FLAGS) -D__is_kernel_

$(KLIBC_C_OBJ): build/klibc/%.o : src/kernel/klibc/%.c
	echo "Compiling klibc C    -> $(patsubst build/klibc/%.o, src/kernel/klibc/%.c, $@)"
	mkdir -p $(dir $@) && \
	x86_64-elf-gcc -c $(patsubst build/klibc/%.o, src/kernel/klibc/%.c, $@) -o $@ -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(C_FLAGS) -D__is_kernel_

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
	nasm -f elf64 $(patsubst build/kcore/%.o, src/kernel/kcore/%.asm, $@) $(NASM_FLAGS) -o $@

$(KCORE_CPP_OBJ): build/kcore/%.o : src/kernel/kcore/%.cpp
	echo "Compiling kcore C++  -> $(patsubst build/kcore/%.o, src/kernel/kcore/%.cpp, $@)"
	mkdir -p $(dir $@) && \
	x86_64-elf-g++ -D__is_kernel_ -c $(patsubst build/kcore/%.o, src/kernel/kcore/%.cpp, $@) -o $@ -I $(KCORE_INCLUDE) -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(CPP_FLAGS) -D__is_kernel_

$(KCORE_C_OBJ): build/kcore/%.o : src/kernel/kcore/%.c
	echo "Compiling kcore C    -> $(patsubst build/kcore/%.o, src/kernel/kcore/%.c, $@)"
	mkdir -p $(dir $@) && \
	x86_64-elf-gcc -D__is_kernel_ -c $(patsubst build/kcore/%.o, src/kernel/kcore/%.c, $@) -o $@ -I $(KCORE_INCLUDE) -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(C_FLAGS) -D__is_kernel_

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
x86_64_C_SRC    := $(filter-out src/kernel/x86_64/idt/idt_main.c, $(x86_64_C_SRC)) # filter out idt
IDT_C_SRC       := $(filter src/kernel/x86_64/idt/%.c, $(x86_64_C_SRC)) # filter only idt/*.c files
x86_64_C_SRC    := $(filter-out src/kernel/x86_64/idt/%.c, $(x86_64_C_SRC)) # remove idt/*.c from x86_64_C_SRC

x86_64_C_OBJ    := $(patsubst src/kernel/x86_64/%.c, build/x86_64/%.o, $(x86_64_C_SRC))
IDT_C_OBJ       := $(patsubst src/kernel/x86_64/%.c, build/x86_64/%.o, $(IDT_C_SRC))
IDT_MAIN_OBJ    := build/x86_64/idt/idt_main.o

x86_64_OBJ := $(x86_64_ASM_OBJ) $(x86_64_C_OBJ) $(IDT_C_OBJ)

$(x86_64_ASM_OBJ): build/x86_64/%.o : src/kernel/x86_64/%.asm
	echo "Compiling x86_64 asm -> $<"
	mkdir -p $(dir $@) && \
	nasm -f elf64 $< $(NASM_FLAGS) -o $@

$(x86_64_C_OBJ): build/x86_64/%.o : src/kernel/x86_64/%.c
	echo "Compiling x86_64 C   -> $<"
	mkdir -p $(dir $@) && \
	x86_64-elf-gcc -c $< -o $@ -I $(x86_64_INCLUDE) -I $(KCORE_INCLUDE) -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(C_FLAGS) -D__is_kernel_

$(IDT_C_OBJ): build/x86_64/%.o : src/kernel/x86_64/%.c
	echo "Compiling IDT C      -> $<"
	mkdir -p $(dir $@) && \
	x86_64-elf-gcc -c $< -o $@ -mgeneral-regs-only -I $(x86_64_INCLUDE) -I $(KCORE_INCLUDE) -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(C_FLAGS) -D__is_kernel_

$(IDT_MAIN_OBJ): src/kernel/x86_64/idt/idt_main.c
	echo "Compiling IDT_MAIN   -> $<"
	mkdir -p $(dir $@) && \
	x86_64-elf-gcc -c $< -o $@ -mgeneral-regs-only -I $(x86_64_INCLUDE) -I $(KCORE_INCLUDE) -I $(KLIBC_INCLUDE) -I $(LIBC_INCLUDE) $(C_FLAGS) -D__is_kernel_

# ----------------------------------------------------
# COMMANDS
# ----------------------------------------------------
# All the commands for building, cleaning, running, etc.
# build-all 	-> builds for all supported architectures
# build 		-> builds for x86-64 (default)
# build_arm64 	-> builds for Aarch64
# clean 		-> deletes all build files, iso's, etc. 
# run			-> builds x86-64 iso, then runs in qemu
# run_arm64		-> builds Aarch64 iso, then runs in qemu
.PHONY: build-all build clean
build-all: 
	echo "<-----------Compiling x86_64---------->"
	$(call build)

build: $(LIBC_OBJ) $(KLIBC_OBJ) $(KCORE_OBJ) $(x86_64_OBJ) $(CRTI_OBJ) $(CRTN_OBJ) $(CRTBEGIN_OBJ) $(CRTEND_OBJ) $(IDT_MAIN_OBJ)
	mkdir -p dist/x86_64
	echo "<---------------Linking--------------->"
	x86_64-elf-ld -n -o dist/x86_64/WallOS.bin -T targets/x86_64/linker.ld font.o $(LIBC_OBJ) $(KLIBC_OBJ) $(x86_64_OBJ) $(IDT_MAIN_OBJ) $(KCORE_OBJ)
	echo "<------------Compiling ISO------------>"
	cp dist/x86_64/WallOS.bin targets/x86_64/iso/boot/WallOS.bin && \
	grub-mkrescue /usr/lib/grub/i386-pc -o dist/x86_64/WallOS.iso targets/x86_64/iso
	echo "<---------Finished  Compiling--------->"
	
qemu: build
	qemu-system-x86_64 -cdrom dist/x86_64/WallOS.iso -cpu max $(ARGS)

clean:
	rm -rf ./build/ && echo "Cleaned build folder"
	rm -rf ./dist/*
	find ./targets/ -name "*.bin" | xargs rm || echo "Cleaned targets folder."
	echo "Finished Cleaning!"