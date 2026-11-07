# ------------------------------------------------------------------------------------------------
# ------------------------------------------------------------------------------------------------
# Default config
# Create a copy of this named "config.mk" in the same folder, change defaults as needed
# Everything in here is for the buildsystem (with a few exceptions), all other configs should be in ../wallos_config.h
# ------------------------------------------------------------------------------------------------
# ------------------------------------------------------------------------------------------------

# ------------------------------------------------------------------------------------------------
# ------------------------------------------------------------------------------------------------
# Framebuffer
# ------------------------------------------------------------------------------------------------
# ------------------------------------------------------------------------------------------------

# Define our framebuffer request.
# Default is 1280x720x32bpp, basically everything will support it ant looks good enough.
# Since we do purely software rendering, larger framebuffers will tank performance.
# Some platforms it's more noticeable than others on.
FRAMEBUFFER_WIDTH  := 1280
FRAMEBUFFER_HEIGHT := 720
FRAMEBUFFER_BPP    := 32

# ------------------------------------------------------------------------------------------------
# ------------------------------------------------------------------------------------------------
# Misc
# ------------------------------------------------------------------------------------------------
# ------------------------------------------------------------------------------------------------

# If mkfs needs sudo, set to 1
# Some systems require it, some don't
MKFS_SUDO=0
export MKFS_SUDO

# ------------------------------------------------------------------------------------------------
# ------------------------------------------------------------------------------------------------
# Distcc
# I have a distcc setup on my own network, and wanted to make it automatic
# ------------------------------------------------------------------------------------------------
# ------------------------------------------------------------------------------------------------
# Comment this out if not using distcc
DISTCC = distcc

# We only want to discover hosts when actually building
# These targets don't need distributed compilation
DISTCC_SKIP_TARGETS := clean qemu_only libs_clean initrd_clean

# Potential remote hosts and their maximum job counts
# Should be in the form ip/nproc, for example "192.168.1.50/12" for a server with $(nproc) == 12
# Separate several hosts with a space, the distcc.mk will automatically deal with all of them
DISTCC_REMOTE_HOSTS :=

# Leave alone
# If using distcc, this will attempt to see if each host is available, and use it if it is
ifdef DISTCC
include distcc.mk
endif