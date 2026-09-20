#!/bin/bash

# Usage: ./netboot_iso.sh 192.168.*.*
SERVER_IP=$1
SOURCE_DIR="wallos_netboot"
OUTPUT_DIR="../dist/netboot"
OUTPUT_ISO="$OUTPUT_DIR/wallos_netboot.iso"

# 1. Validation
if [ -z "$SERVER_IP" ]; then
    echo "Error: No IP address provided."
    echo "Usage: $0 <server_ip>"
    exit 1
fi

if ! command -v grub-mkrescue &> /dev/null; then
    echo "Error: grub-mkrescue is not installed. Please install 'grub-efi-amd64-bin'."
    exit 1
fi

# 2. Setup Directory Structure
TARGET_GRUB_DIR="$SOURCE_DIR/boot/grub"
mkdir -p "$TARGET_GRUB_DIR"
mkdir -p "$OUTPUT_DIR"

# 3. Generate the config file
echo "Creating net_settings.cfg..."
echo "set server_ip=$SERVER_IP" > "$TARGET_GRUB_DIR/net_settings.cfg"

# 4. Run grub-mkrescue
echo "Building ISO to $OUTPUT_ISO..."
grub-mkrescue -o "$OUTPUT_ISO" "$SOURCE_DIR"

if [ $? -eq 0 ]; then
    echo "------------------------------------------------"
    echo "Success! Build complete."
    echo "Server IP set to: $SERVER_IP"
    echo "ISO Location: $OUTPUT_ISO"
else
    echo "Error: grub-mkrescue failed to build the ISO."
    exit 1
fi