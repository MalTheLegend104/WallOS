INCLUDE_DIRS = -Iinclude
SOURCE_DIRS = src
OUTPUT_DIR = $(MAKE_OUTPUT_DIR)

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

# Compile sources into object files
$(OUTPUT_DIR)/builds/apollo/%.o: %.c
	@mkdir -p $(dir $@)
	$(WALLOS_C_COMPILER) $(WALLOS_C_FLAGS) -O3 $(INCLUDE_DIRS) -c $< -o $@

# Combine all object files into a single temporary archive
$(TEMP_LIB): $(OBJECTS)
	$(AR) $(ARFLAGS) $(TEMP_LIB) $(OBJECTS)

libs_clean:
	rm -rf $(OUTPUT_DIR)/builds/apollo $(DUMMY_FILE)

.PHONY: all clean
