# We need the includes from apollo (for display) and general klibc includes
INCLUDE_DIRS = -I../../$(KLIBC_INCLUDE) -I../apollo/include
SOURCE_DIRS = src
OUTPUT_DIR = $(MAKE_OUTPUT_DIR)

# Find all source files
SOURCES = $(shell find $(SOURCE_DIRS) -name '*.c')
OBJECTS = $(patsubst %.c,$(OUTPUT_DIR)/builds/kilo/%.o,$(SOURCES))
TEMP_LIB = $(OUTPUT_DIR)/libkilo.a

# Default target
all: $(TEMP_LIB)

# Kilo isn't perf-critical, but -O3 is kinda free here so no reason not to keep it

# Compile sources into object files
$(OUTPUT_DIR)/builds/kilo/%.o: %.c
	@mkdir -p $(dir $@)
	$(WALLOS_C_COMPILER) $(WALLOS_C_FLAGS) -O3 $(INCLUDE_DIRS) -c $< -o $@

# Combine all object files into a single temporary archive
$(TEMP_LIB): $(OBJECTS)
	$(AR) $(ARFLAGS) $(TEMP_LIB) $(OBJECTS)

libs_clean:
	rm -rf $(OUTPUT_DIR)/builds/kilo $(DUMMY_FILE)

.PHONY: all clean