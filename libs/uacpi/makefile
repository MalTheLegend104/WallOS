INCLUDE_DIRS = -Iinclude -Iinclude/platform
SOURCE_DIRS = source
OUTPUT_DIR = $(MAKE_OUTPUT_DIR)

# Find all source files
SOURCES = $(shell find $(SOURCE_DIRS) -name '*.c')
OBJECTS = $(patsubst %.c,$(OUTPUT_DIR)/builds/uacpi/%.o,$(SOURCES))
TEMP_LIB = $(OUTPUT_DIR)/libuacpi.a

# Default target
all: $(TEMP_LIB)

# Compile sources into object files
$(OUTPUT_DIR)/builds/uacpi/%.o: %.c
	@mkdir -p $(dir $@)
	$(WALLOS_C_COMPILER) $(WALLOS_C_FLAGS) $(INCLUDE_DIRS) -Wno-unused-parameter -c $< -o $@

# Combine all object files into a single temporary archive
$(TEMP_LIB): $(OBJECTS)
	$(AR) $(ARFLAGS) $(TEMP_LIB) $(OBJECTS)

libs_clean:
	rm -rf $(OUTPUT_DIR)/builds/uacpi $(DUMMY_FILE)

.PHONY: all clean
