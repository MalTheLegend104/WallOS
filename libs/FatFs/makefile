INCLUDE_DIRS = -Isource -I../../$(KLIBC_INCLUDE)
SOURCE_DIRS = source
OUTPUT_DIR = $(MAKE_OUTPUT_DIR)

# Find all source files
SOURCES = $(shell find $(SOURCE_DIRS) -name '*.c')
OBJECTS = $(patsubst %.c,$(OUTPUT_DIR)/builds/fatfs/%.o,$(SOURCES))
TEMP_LIB = $(OUTPUT_DIR)/libfatfs.a

# Default target
all: $(TEMP_LIB) 

# Compile sources into object files
$(OUTPUT_DIR)/builds/fatfs/%.o: %.c
	@mkdir -p $(dir $@)
	$(WALLOS_C_COMPILER) $(WALLOS_C_FLAGS) $(INCLUDE_DIRS) -c $< -o $@

# Combine all object files into a single temporary archive
$(TEMP_LIB): $(OBJECTS)
	$(AR) $(ARFLAGS) $(TEMP_LIB) $(OBJECTS)

libs_clean:
	rm -rf $(OUTPUT_DIR)/builds/fatfs $(DUMMY_FILE)

.PHONY: all clean
