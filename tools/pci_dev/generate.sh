#!/bin/bash

# Configuration
PCI_IDS_URL="https://pci-ids.ucw.cz/v2.2/pci.ids"
PCI_IDS_FILE="pci.ids"
PY_SCRIPT="pci_dev.py"
OUTPUT_H="../../src/kernel/klibc/include/drivers/pci_dev.h"
FILTER_FILE="filter.txt"

# Download/Update pci.ids if it doesn't exist
if [ ! -f "$PCI_IDS_FILE" ]; then
    echo "Downloading pci.ids..."
    wget -q --show-progress $PCI_IDS_URL -O $PCI_IDS_FILE
else
    echo "pci.ids already exists, skipping download"
fi

# Check for the Python script
if [ ! -f "$PY_SCRIPT" ]; then
    echo "Error: $PY_SCRIPT not found in current directory."
    exit 1
fi

# Handle Arguments
case "$1" in
    full)
        echo "Generating FULL header..."
        python3 "$PY_SCRIPT" "$PCI_IDS_FILE" "$OUTPUT_H" --mode full
        ;;
    filtered)
        if [ ! -f "$FILTER_FILE" ]; then
            echo "Error: $FILTER_FILE not found. Please create it first."
            exit 1
        fi
        echo "Generating filtered header..."
        python3 "$PY_SCRIPT" "$PCI_IDS_FILE" "$OUTPUT_H" --mode condensed --filter "$FILTER_FILE"
        ;;
    *)
        echo "Usage: $0 {full|filtered}"
        exit 1
        ;;
esac

if [ $? -eq 0 ]; then
    echo "Created $OUTPUT_H"
else
    echo "Error: Code generation failed."
fi