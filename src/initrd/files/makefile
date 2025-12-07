MAKEFLAGS=-s
.DEFAULT_GOAL := all

ROOT_DIR = "$(CURDIR)/root"

all: install

# Copy files from ./root to $(OUTPUT_DIR)
install:
	@echo "$(COLOR_CYAN)Copying files from $(ROOT_DIR) to $(MAKE_OUTPUT_DIR)$(END_COLOR)"
	@mkdir -p $(MAKE_OUTPUT_DIR)
	@cp -r $(ROOT_DIR)/* $(MAKE_OUTPUT_DIR)/
	@echo "$(COLOR_GREEN)Files copied successfully!$(END_COLOR)"

# Clean target (optional)
clean:
	@echo "$(COLOR_YELLOW)Nothing to clean up for 'files', skipping...$(END_COLOR)"