INCLUDE_DIRS = -Iinclude -Iinclude/platform
SOURCE_DIRS = source
OUTPUT_DIR = $(MAKE_OUTPUT_DIR)

AR ?= x86_64-wallos-ar
ARFLAGS = rcs

# Find all source files
SOURCES = $(shell find $(SOURCE_DIRS) -name '*.c')
OBJECTS = $(patsubst %.c,$(OUTPUT_DIR)/builds/acpica/%.o,$(SOURCES))
TEMP_LIB = $(OUTPUT_DIR)/libacpica.a

# Default target
all: $(TEMP_LIB)

# Compile sources into object files
$(OUTPUT_DIR)/builds/acpica/%.o: %.c
	@mkdir -p $(dir $@)
	$(WALLOS_C_COMPILER) $(WALLOS_C_FLAGS) $(INCLUDE_DIRS) -Wno-unused-parameter -c $< -o $@

# Combine all object files into a single temporary archive
$(TEMP_LIB): $(OBJECTS)
	$(AR) $(ARFLAGS) $(TEMP_LIB) $(OBJECTS)

libs_clean:
	rm -rf $(OUTPUT_DIR)/builds/acpica $(DUMMY_FILE)

.PHONY: all clean
