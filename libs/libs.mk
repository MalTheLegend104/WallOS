MAKEFLAGS=-s
.DEFAULT_GOAL := all

# This defines all includes for all the libraries
include libs/libs_includes.mk

# Compiler and Linker Settings
# These are fallbacks. The main makefile sets the real values (with distcc, if configured) after including this file.
WALLOS_C_COMPILER 	?= x86_64-wallos-gcc
WALLOS_CXX_COMPILER	?= x86_64-wallos-g++
WALLOS_ASSEMBLER 	?= nasm
WALLOS_LINKER 		?= x86_64-wallos-ld

# Archiver. gcc-ar/gcc-ranlib load the LTO plugin, so archives of LTO objects get a usable symbol index.
# AR is a built-in make variable (value "ar"), so ?= would never replace it. := does. config.mk is included
# after this file, so it can still override these.
AR := x86_64-wallos-gcc-ar
RANLIB := x86_64-wallos-gcc-ranlib
ARFLAGS = rcs


# Flags
# Fallbacks only, for when this file is used without the main makefile.
# The main makefile defines WALLOS_C_FLAGS / WALLOS_CXX_FLAGS / WALLOS_ASM_FLAGS / WALLOS_LTO_FLAGS
# from the same variables the kernel uses, so the libs build with the same codegen flags and forced includes.
WALLOS_C_FLAGS 	 ?= -ffreestanding -std=gnu99 -g -Wall -Wextra -Wno-format -nostdlib -lgcc -mno-red-zone -O0 -mcmodel=kernel
WALLOS_CXX_FLAGS ?= -ffreestanding -fno-rtti -g -Wall -Wextra -Wno-format -nostdlib -lgcc -mno-red-zone -O0 -mcmodel=kernel
WALLOS_ASM_FLAGS ?=
WALLOS_LTO_FLAGS ?=
WALLOS_LD_FLAGS  ?=

OUTPUT_DIR = output

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

export WALLOS_LTO_FLAGS

export AR
export RANLIB
export ARFLAGS

COLOR_GREEN	  ?= \033[0;32m
COLOR_YELLOW  ?= \033[0;93m
COLOR_RED	  ?= \033[0;31m
COLOR_BLUE	  ?= \033[0;34m
COLOR_CYAN	  ?= \033[0;96m
COLOR_MAGENTA ?= \033[0;95m
END_COLOR	  ?= \033[0m

.PHONY: libs

# I sincerely apologize to anyone having to read this and try to figure out what it does.
# It mostly just loops through each directoy, checks if it has a known buildsystem (make, cmake, meson) and then builds it if it does.
# It expects each build to be set up to output the necessary file to OUTPUT_DIR.
# Cleaning just expects each build system to clean up after itself.
libs:
	@echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"
	@echo "$(COLOR_CYAN)<----------------------Building LIBS---------------------->$(END_COLOR)"
	@echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"
	@for dir in libs/*/; do \
		if [ "$$dir" = "libs/$(OUTPUT_DIR)/" ]; then \
			continue; \
		fi; \
		dirname=$${dir%/}; \
		if [ -f "$(CURDIR)/$$dirname/CMakeLists.txt" ]; then \
			echo "$(COLOR_GREEN)Found CMake in $$dir. Building with CMake.$(END_COLOR)"; \
			( cd "$(CURDIR)/$$dirname" && mkdir -p build && cmake -B build . && cmake --build build ) || exit 1; \
			echo "$(COLOR_MAGENTA)Finished with $$dirname.$(END_COLOR)"; \
		elif [ -f "$(CURDIR)/$$dirname/makefile" ]; then \
			echo "$(COLOR_GREEN)Found makefile in $$dir. Building with Make.$(END_COLOR)"; \
			( cd "$(CURDIR)/$$dirname" && $(MAKE) ) || exit 1; \
			echo "$(COLOR_MAGENTA)Finished with $$dirname.$(END_COLOR)"; \
		elif [ -f "$(CURDIR)/$$dirname/meson.build" ]; then \
			echo "$(COLOR_GREEN)Found meson.build in $$dir. Building with Meson.$(END_COLOR)"; \
			( cd "$(CURDIR)/$$dirname" && meson setup build && cd build && ninja ) || exit 1; \
			echo "$(COLOR_MAGENTA)Finished with $$dirname.$(END_COLOR)"; \
		else \
			echo "$(COLOR_YELLOW)No build system found in $$dir$(END_COLOR)"; \
		fi \
	done
	@echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"
	@echo "$(COLOR_CYAN)<-----------------Finished Building LIBS------------------>$(END_COLOR)"
	@echo "$(COLOR_CYAN)<--------------------------------------------------------->$(END_COLOR)"

# Clean Target
libs_clean:
	@for dir in libs/*/; do \
		if [ "$$dir" = "libs/$(OUTPUT_DIR)/" ]; then \
			continue; \
		fi; \
		dirname=$${dir%/}; \
		if [ -f "$(CURDIR)/$$dirname/CMakeLists.txt" ]; then \
			echo "$(COLOR_GREEN)Cleaning $$dir (CMake)$(END_COLOR)"; \
			( cd "$(CURDIR)/$$dirname" && rm -rf build ); \
		elif [ -f "$(CURDIR)/$$dirname/makefile" ]; then \
			echo "$(COLOR_GREEN)Cleaning $$dir (Make)$(END_COLOR)"; \
			( cd "$(CURDIR)/$$dirname" && $(MAKE) clean ); \
		elif [ -f "$(CURDIR)/$$dirname/meson.build" ]; then \
			echo "$(COLOR_GREEN)Cleaning with Meson in $$dir (Meson)$(END_COLOR)"; \
			( cd "$(CURDIR)/$$dirname" && ninja -C build clean ) || true; \
		else \
			echo "$(COLOR_YELLOW)No build system found in $$dir. Nothing to clean up.$(END_COLOR)"; \
		fi \
	done
	rm -rf libs/$(OUTPUT_DIR)
	echo "$(COLOR_GREEN)Cleaned libs output dir.$(END_COLOR)"