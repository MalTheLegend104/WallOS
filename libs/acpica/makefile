INCLUDE_DIRS = -Iinclude -Iinclude/platform
SOURCE_DIRS = source
OUTPUT_DIR = $(MAKE_OUTPUT_DIR)

AR ?= x86_64-wallos-ar
ARFLAGS = rcs

# Find all source files
SOURCES = $(shell find $(SOURCE_DIRS) -name '*.c')
OBJECTS = $(patsubst %.c,$(OUTPUT_DIR)/builds/acpica/%.o,$(SOURCES))
TEMP_LIB = $(OUTPUT_DIR)/temp_objects.a
FINAL_OBJ = $(OUTPUT_DIR)/acpica.o
DUMMY_FILE = $(OUTPUT_DIR)/builds/acpica/dummy_combined.c

# Default target
all: $(FINAL_OBJ)

# Create a dummy C file
$(DUMMY_FILE):
	@mkdir -p $(OUTPUT_DIR)
	echo '/* This file is a placeholder for combining object files. */' > $(DUMMY_FILE)
	echo 'int dummy_function() { return 0; }' >> $(DUMMY_FILE)

# Compile sources into object files
$(OUTPUT_DIR)/builds/acpica/%.o: %.c
	@mkdir -p $(dir $@)
	$(WALLOS_C_COMPILER) $(WALLOS_C_FLAGS) $(INCLUDE_DIRS) -Wno-unused-parameter -c $< -o $@

# Combine all object files into a single temporary archive
$(TEMP_LIB): $(OBJECTS)
	$(AR) $(ARFLAGS) $(TEMP_LIB) $(OBJECTS)

# Create a single object file from the combined temporary archive
$(FINAL_OBJ): $(TEMP_LIB) $(DUMMY_FILE)
	$(WALLOS_C_COMPILER) $(WALLOS_C_FLAGS) -c $(DUMMY_FILE) -o $(OUTPUT_DIR)/dummy_combined.o
	$(WALLOS_LINKER) $(WALLOS_LD_FLAGS) -r $(TEMP_LIB) $(OUTPUT_DIR)/dummy_combined.o -o $(FINAL_OBJ)

libs_clean:
	rm -rf $(OUTPUT_DIR)/builds/acpica $(DUMMY_FILE)

.PHONY: all clean
