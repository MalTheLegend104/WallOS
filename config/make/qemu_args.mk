ARGS ?= -vga std -m 5G -M hpet=on -machine q35 -cpu max -smp 4 -serial stdio

# This adds one of each type of USB host controllers
# OHCI, UHCI, EHCI, and XHCI
ARGS += \
  -device qemu-xhci,id=xhci \
  -device usb-kbd
#   -device piix3-usb-uhci,id=uhci0
#   -device usb-mouse
#   -device usb-ehci,id=ehci0 \
#   -device pci-ohci,id=ohci \

# ARGS += -device ahci,id=ahci0 \
#   -drive file=test/images/disk.img,if=none,id=d1 \
#   -device ide-hd,drive=d1,bus=ahci0.0

# Uncomment this to add audio devices to the system.
# ARGS +=  -audiodev sdl,id=snd0 \
#   -device sb16,audiodev=snd0 \
#   -device es1370,audiodev=snd0 \
#   -device ac97,audiodev=snd0