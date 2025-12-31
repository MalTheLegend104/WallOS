
# This file is meant to serve as a single location where only the includes for each lib are located.
# $(CURDIR) is the location of where make is invoked (back a folder from this one.)
# This means we have to include the "libs" folder even though we're already in it.

ACPICA_INCLUDE_DIR  = -I"$(CURDIR)/libs/acpica/include"
ACPICA_LIB 			= -lacpica

UACPI_INCLUDE_DIR = -I"$(CURDIR)/libs/uacpi/include"
UACPI_LIB = -luacpi

FATFS_INCLUDE_DIR  = -I"$(CURDIR)/libs/FatFs/source"
FATFS_LIB 			= -lfatfs

# APOLLO_INCLUDE_DIR  = -I"$(CURDIR)/libs/apollo/include"
# APOLLO_LIB 			= -lapollo

LIBRARY_INCLUDES += $(ACPICA_INCLUDE_DIR) $(APOLLO_INCLUDE_DIR) $(FATFS_INCLUDE_DIR) $(UACPI_INCLUDE_DIR)
LIBRARY_FLAGS	 += -Llibs/output $(ACPICA_LIB) $(APOLLO_LIB) $(FATFS_LIB) $(UACPI_LIB)
