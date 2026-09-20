#!/bin/bash

# Usage: ./netboot_server.sh 192.168.*.*

SERVER_IP="$1"
SOURCE_DIR="wallos_netboot_server"
OUTPUT_DIR="../dist/netboot_server"

if [ -z "$SERVER_IP" ]; then
    echo "Error: No IP address provided."
    echo "Usage: $0 <server_ip>"
    exit 1
fi

if ! command -v grub-mkimage &> /dev/null; then
    echo "Error: grub-mkimage is not installed."
    exit 1
fi

GRUB_SOURCE="$SOURCE_DIR/boot/grub"
OUTPUT_GRUB="$OUTPUT_DIR/boot/grub"

# Clean/create output directory
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_GRUB"
mkdir -p "$OUTPUT_DIR/ipxe"

# Generate the server-specific GRUB settings
echo "set server_ip=$SERVER_IP" > "$OUTPUT_GRUB/net_settings.cfg"

# Copy UEFI GRUB configuration
cp "$GRUB_SOURCE/grub.cfg" "$OUTPUT_GRUB/grub.cfg"

# Build BIOS PXE GRUB image using the source configuration
echo "Building grub.pxe..."

grub-mkimage \
    -O i386-pc-pxe \
    -o "$OUTPUT_DIR/grub.pxe" \
    -p /boot/grub \
    -c "$GRUB_SOURCE/grub-pxe.cfg" \
    pxe \
    net \
    http \
    tftp \
    multiboot2 \
    normal \
    configfile \
    serial \
    gfxterm \
    all_video

if [ $? -ne 0 ]; then
    echo "Error: grub-mkimage failed."
    exit 1
fi

# Generate server-specific iPXE script
sed "s|@SERVER_IP@|$SERVER_IP|g" \
    "$SOURCE_DIR/ipxe/wallos.ipxe" \
    > "$OUTPUT_DIR/ipxe/wallos.ipxe"

echo "------------------------------------------------"
echo "Success! Netboot files generated."
echo "Server IP: $SERVER_IP"
echo
echo "Output:"
echo "  $OUTPUT_DIR/boot/grub/grub.cfg"
echo "  $OUTPUT_DIR/boot/grub/net_settings.cfg"
echo "  $OUTPUT_DIR/grub.pxe"
echo "  $OUTPUT_DIR/ipxe/wallos.ipxe"