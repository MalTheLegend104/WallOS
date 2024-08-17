
# This file is meant to serve as a single location where only the includes for each lib are located.
# $(CURDIR) is the location of where make is invoked (back a folder from this one.)
# This means we have to include the "libs" folder even though we're already in it.

ACPICA_INCLUDE_DIR  = -I"$(CURDIR)/libs/ACPICA/include"
ACPICA_LIB 			= -lacpica

LIBRARY_INCLUDES += $(ACPICA_INCLUDE_DIR)
LIBRARY_FLAGS	 += -Llibs/output $(ACPICA_LIB)
