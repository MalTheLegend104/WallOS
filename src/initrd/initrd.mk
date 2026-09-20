MAKEFLAGS=-s
.DEFAULT_GOAL := all

# Some systems require sudo to use MKFS
# I'd recommend exporting MKFS_SUDO=1 in .bashrc (or equivalent) if your system needs it.
MKFS_SUDO ?= 0
MKFS_CMD := $(if $(filter 1,$(MKFS_SUDO)),sudo mkfs.fat,mkfs.fat)

# Compiler and Linker Settings
WALLOS_C_COMPILER 	?= x86_64-wallos-gcc
WALLOS_CXX_COMPILER	?= x86_64-wallos-g++
WALLOS_ASSEMBLER 	?= nasm
WALLOS_LINKER 		?= x86_64-wallos-ld

# Flags
WALLOS_C_FLAGS 	 ?= -ffreestanding -std=gnu99 -g -Wall -Wextra -Wno-format -nostdlib -lgcc -mno-red-zone -O0
WALLOS_CXX_FLAGS ?= -ffreestanding -fno-rtti -g -Wall -Wextra -Wno-format -nostdlib -lgcc -mno-red-zone -O0
WALLOS_ASM_FLAGS ?=
WALLOS_LD_FLAGS  ?=

OUTPUT_DIR = output

# I hate this with a passion, but it's the least painful way to do it
# I swear I'll convert this god forsaken build system to cmake one day...
CURRENT_DIR = src/initrd/

# Set the environment vars for all the build systems.
# CMake (and potentially meson) are expected to be in the "build" directory, not the base source directory.
export MAKE_OUTPUT_DIR = ../$(OUTPUT_DIR)
export CMAKE_OUTPUT_DIR = ../../$(OUTPUT_DIR)

export WALLOS_C_COMPILER
export WALLOS_C_FLAGS

export WALLOS_CXX_COMPILER
export WALLOS_CXX_FLAGS

export WALLOS_ASSEMBLER
export WALLOS_ASM_FLAGS 

export WALLOS_LINKER
export WALLOS_LD_FLAGS

COLOR_GREEN	  ?= \033[0;32m
COLOR_YELLOW  ?= \033[0;93m
COLOR_RED	  ?= \033[0;31m
COLOR_BLUE	  ?= \033[0;34m
COLOR_CYAN	  ?= \033[0;96m
COLOR_MAGENTA ?= \033[0;95m
END_COLOR	  ?= \033[0m

# FAT image settings
IMAGE_NAME = initrd.img
IMAGE_SIZE_MB = 2
IMAGE_FILE = $(CURDIR)/dist/$(IMAGE_NAME)

initrd: build_projects image

# Target: all (default target)
build_projects:
	@echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"
	@echo "$(COLOR_CYAN)<--------------------Building INITRD---------------------->$(END_COLOR)"
	@echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"
	@mkdir -p $(CURRENT_DIR)$(OUTPUT_DIR)
	@for dir in src/initrd/*/; do \
		if [ "$$dir" = "src/initrd/$(OUTPUT_DIR)/" ]; then \
        	continue; \
    	fi; \
		dirname=$${dir%/}; \
		if [ -f "$$dir/CMakeLists.txt" ]; then \
			echo "$(COLOR_GREEN)Found CMake in $$dir. Building with CMake.$(END_COLOR)"; \
			cd "$(CURDIR)/$$dir" && mkdir -p build && cmake -B build . && cmake --build build; \
            cd ..; \
			echo "$(COLOR_MAGENTA)Finished with $$dirname.$(END_COLOR)"; \
		elif [ -f "$(CURDIR)/$$dirname/makefile" ]; then \
			echo "$(COLOR_GREEN)Found makefile in $$dir. Building with Make.$(END_COLOR)"; \
			cd "$(CURDIR)/$$dir" && $(MAKE); \
			cd ..; \
			echo "$(COLOR_MAGENTA)Finished with $$dirname.$(END_COLOR)"; \
		elif [ -f "$$dir/meson.build" ]; then \
			echo "$(COLOR_GREEN)Found meson.build in $$dir. Building with Meson.$(END_COLOR)"; \
			cd "$(CURDIR)/$$dir" && meson setup build && cd build && ninja; \
			cd ../..; \
			echo "$(COLOR_MAGENTA)Finished with $$dirname.$(END_COLOR)"; \
		else \
			echo "$(COLOR_YELLOW)No build system found in $$dir$(END_COLOR)"; \
		fi \
	done
	@echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"
	@echo "$(COLOR_CYAN)<----------------Finished Building INITRD----------------->$(END_COLOR)"
	@echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"

# -----------------------------
# Create FAT12 image using mtools
# -----------------------------
image: build_projects
	@echo "IMAGE FILE: $(IMAGE_FILE)"
	@echo "OUTPUT_DIR: $(CURRENT_DIR)$(OUTPUT_DIR)"
	@echo "$(COLOR_CYAN)Creating FAT12 image $(IMAGE_NAME) from $(OUTPUT_DIR)$(END_COLOR)"
	@mkdir -p "$(CURDIR)/dist"
	@dd if=/dev/zero of="$(IMAGE_FILE)" bs=1M count=$(IMAGE_SIZE_MB)
	@$(MKFS_CMD) -F 12 "$(IMAGE_FILE)"
	@mcopy -i "$(IMAGE_FILE)" -s $(CURRENT_DIR)$(OUTPUT_DIR)/* ::
	@cp "$(IMAGE_FILE)" "$(CURDIR)/targets/x86_64/iso/boot/initrd.img"
	@echo "$(COLOR_GREEN)FAT12 image created at $(IMAGE_FILE)$(END_COLOR)"

# Clean Target
initrd_clean:
	@for dir in src/initrd/*/; do \
		if [ "$$dir" = "src/initrd/$(OUTPUT_DIR)/" ]; then \
        	continue; \
    	fi; \
		if [ -f "$$dir/CMakeLists.txt" ]; then \
			echo "$(COLOR_GREEN)Cleaning $$dir (CMake)$(END_COLOR)"; \
			cd "$$dir" && rm -rf build; \
			cd ..; \
		elif [ -f "$$dir/makefile" ]; then \
			echo "$(COLOR_GREEN)Cleaning $$dir (Make)$(END_COLOR)"; \
			cd "$$dir" && make clean; \
			cd ..; \
		elif [ -f "$$dir/meson.build" ]; then \
			echo "$(COLOR_GREEN)Cleaning with Meson in $$dir (Meson)$(END_COLOR)"; \
			cd "$$dir" && meson build && cd build && ninja clean; \
			cd ../..; \
		else \
			echo "$(COLOR_YELLOW)No build system found in $$dir. Nothing to clean up.$(END_COLOR)"; \
		fi \
	done
	rm -rf src/initrd/$(OUTPUT_DIR)
	echo "$(COLOR_GREEN)Cleaned libs output dir.$(END_COLOR)"
