# The main makefile will propagate make.
# This will include this file several times. Since this has timeouts, we only want it once.
ifndef DISTCC_INITIALIZED

# Gather the local info
DISTCC_LOCAL_JOBS := $(shell nproc)

# Make sure we actually want to use distcc, we don't need it for things like clean
ifneq ($(strip $(MAKECMDGOALS)),)
ifeq ($(strip $(filter-out $(DISTCC_SKIP_TARGETS),$(MAKECMDGOALS))),)
DISTCC_ENABLED := 0
else
DISTCC_ENABLED := 1
endif
else
DISTCC_ENABLED := 1
endif

# If we need it, figure out if any of the potential remotes are online and responsive
ifeq ($(DISTCC_ENABLED),1)

# Extract hostnames, removing their job counts.
distcc_hostname = $(firstword $(subst /, ,$(1)))

DISTCC_CANDIDATES := $(foreach host,$(DISTCC_REMOTE_HOSTS), $(call distcc_hostname,$(host)))

# Probe remote hosts
# Timeout of 300ms because we don't really want to wait forever to see if something connects
DISTCC_DISCOVERED := $(shell lsdistcc -c300 -k500 -px86_64-wallos-gcc $(DISTCC_CANDIDATES) 2>/dev/null)

# Remove hosts that aren't reachable
# Yes this line is unreadable, trust me, that's what it does
DISTCC_ACTIVE_REMOTE := $(foreach host,$(DISTCC_REMOTE_HOSTS), $(if $(filter $(call distcc_hostname,$(host)),$(DISTCC_DISCOVERED)), $(host)))

# Make sure we have localhost in the hosts
DISTCC_HOSTS := localhost/$(DISTCC_LOCAL_JOBS) $(DISTCC_ACTIVE_REMOTE)

export DISTCC_HOSTS

# Calculate total jobs from active hosts
# Kinda cursed way of getting this, but it works well.
DISTCC_JOBS := $(shell \
    echo "$(DISTCC_HOSTS)" | \
    awk '{ \
        for (i = 1; i <= NF; i++) { \
            split($$i, a, "/"); \
            total += a[2]; \
        } \
    } END { print total+0 }')

$(info Using $(DISTCC_JOBS) jobs on $(shell expr $(words $(DISTCC_ACTIVE_REMOTE)) + 1) devices.)

else
# No discovery needed for clean/help/etc, just use the local count
DISTCC_JOBS := $(DISTCC_LOCAL_JOBS)
endif

# Makes sure we don't need to 
MAKEFLAGS += -j$(DISTCC_JOBS)

DISTCC_INITIALIZED := 1
export DISTCC_INITIALIZED
endif