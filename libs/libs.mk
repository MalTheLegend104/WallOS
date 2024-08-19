MAKEFLAGS=-s
.DEFAULT_GOAL := all

# This defines all includes for all the libraries
include libs/libs_includes.mk

# Compiler and Linker Settings
WALLOS_C_COMPILER 	?= x86_64-wallos-gcc
WALLOS_CXX_COMPILER	?= x86_64-wallos-g++
WALLOS_ASSEMBLER 	?= nasm
WALLOS_LINKER 		?= x86_64-wallos-ld

# Flags
WALLOS_C_FLAGS 	 ?= -ffreestanding -std=gnu99 -g -Wall -Wextra -Wno-format -nostdlib -lgcc -mno-red-zone -O0 -mcmodel=kernel
WALLOS_CXX_FLAGS ?= -ffreestanding -fno-rtti -g -Wall -Wextra -Wno-format -nostdlib -lgcc -mno-red-zone -O0 -mcmodel=kernel
WALLOS_ASM_FLAGS ?=
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
	@mkdir -p $(OUTPUT_DIR)
	@for dir in libs/*/; do \
		if [ "$$dir" = "$(OUTPUT_DIR)/" ]; then \
        	continue; \
    	fi; \
		dirname=$${dir%/}; \
		if [ -f "$$dir/CMakeLists.txt" ]; then \
			echo "$(COLOR_GREEN)Found CMake in $$dir. Building with CMake.$(END_COLOR)"; \
			cd "$$dir" && mkdir -p build && cmake -B build . && cmake --build build; \
            cd ..; \
			echo "$(COLOR_MAGENTA)Finished with $$dirname.$(END_COLOR)"; \
		elif [ -f "$$dir/makefile" ]; then \
			echo "$(COLOR_GREEN)Found makefile in $$dir. Building with Make.$(END_COLOR)"; \
			cd "$$dir" && $(MAKE); \
			cd ..; \
			echo "$(COLOR_MAGENTA)Finished with $$dirname.$(END_COLOR)"; \
		elif [ -f "$$dir/meson.build" ]; then \
			echo "$(COLOR_GREEN)Found meson.build in $$dir. Building with Meson.$(END_COLOR)"; \
			cd "$$dir" && meson setup build && cd build && ninja; \
			cd ../..; \
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
	rm -rf libs/$(OUTPUT_DIR)
	echo "$(COLOR_GREEN)Cleaned libs output dir.$(END_COLOR)"