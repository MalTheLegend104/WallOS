INCLUDE_DIRS = -Iinclude
SOURCE_DIRS = src
OUTPUT_DIR = $(MAKE_OUTPUT_DIR)

# AR and ARFLAGS are built-in make variables, so the "is it still the built-in value" check is needed.
# When the top level makefile exports them, this does nothing.
ifeq ($(origin AR),default)
AR := x86_64-wallos-gcc-ar
endif
ifeq ($(origin ARFLAGS),default)
ARFLAGS := rcs
endif

# Find all source files
SOURCES = $(shell find $(SOURCE_DIRS) -name '*.c')
OBJECTS = $(patsubst %.c,$(OUTPUT_DIR)/builds/apollo/%.o,$(SOURCES))
TEMP_LIB = $(OUTPUT_DIR)/libapollo.a

# Default target
all: $(TEMP_LIB)

# In theory, in combination with the other flags, we should have no problem with setting -O3.
# GCC should know to only output up to SSE2, and leave things like AVX alone.
# To support all x86_64, we have at a minimum of SSE2.
# By the time we start using any apollo functions, SSE is already enabled.
# Apollo is significantly quicker if allowed to be optimized (1/2 the assembly output).

COMPILE_FLAGS = $(WALLOS_C_FLAGS) -O3 $(WALLOS_LTO_FLAGS) $(INCLUDE_DIRS)

# Compile sources into object files
# -MMD -MP also writes a .d file per object, so editing apollo.h rebuilds what includes it.
$(OUTPUT_DIR)/builds/apollo/%.o: %.c
	@mkdir -p $(dir $@)
	$(WALLOS_C_COMPILER) $(COMPILE_FLAGS) -MMD -MP -c $< -o $@

# Combine all object files into a single temporary archive
$(TEMP_LIB): $(OBJECTS)
	@rm -f $(TEMP_LIB)
	$(AR) $(ARFLAGS) $(TEMP_LIB) $(OBJECTS)

libs_clean:
	rm -rf $(OUTPUT_DIR)/builds/apollo
	rm -f $(TEMP_LIB)

# The top level libs_clean runs "make clean" in every lib directory.
clean: libs_clean

.PHONY: all clean libs_clean
