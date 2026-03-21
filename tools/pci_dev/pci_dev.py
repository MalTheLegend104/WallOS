#!/usr/bin/env python3
"""
pci_gen.py — Generate a freestanding C header from a pci.ids database.

The output header contains:
  - A flat array of pci_device_t structs (vendor, device id, name)
  - A sorted array of pci_vendor_t structs (vendor id, name, offset/count into device array)
  - Binary-search helpers: pci_find_vendor(), get_pci_vendor_name(), get_pci_device_name()

Usage:
    python pci_gen.py pci.ids output.h --mode full
    python pci_gen.py pci.ids output.h --mode condensed --filter filter.txt

Filter format (for condensed mode):
    # Vendor-only line: include all devices for this vendor
    8086

    # Vendor + device line: include only this specific device
    10de:1cb3
"""

import argparse
import re


# ----------------------------------------
# Parsing pci.ids
# ----------------------------------------

def parse_pci_ids(path):
    """
    Parse a pci.ids file into a structured dictionary of vendors and classes.

    The pci.ids format uses prefixes and indentation to encode hierarchy:
      - Lines starting with 'C':         PCI Class entries (2-hex-digit id + name)
      - Lines with one tab (under 'C'):  PCI Subclass entries (2-hex-digit id + name)
      - Lines with no leading tab:       Vendor entries (4-hex-digit id + name)
      - Lines with one leading tab:      Device entries (4-hex-digit id + name)
      - Lines with two leading tabs:     Subsystem entries (skipped here)
      - Lines starting with '#' or blank: Comments / whitespace (skipped)

    Returns:
        tuple: (vendors, classes)
            vendors (dict): { vendor_id (int) -> { "name": str, "devices": { device_id (int) -> str } } }
            classes (dict): { class_id (int) -> { "name": str, "subclasses": { sub_id (int) -> str } } }
    """
    vendors = {}
    classes = {}
    current_vendor = None
    current_class = None

    with open(path, "r", encoding="latin1") as f:
        for line in f:
            if not line.strip() or line.startswith("#"):
                continue

            # Handle PCI Class entries (Lines starting with 'C ')
            if line.startswith("C "):
                m = re.match(r"^C\s+([0-9A-Fa-f]{2})\s+(.+)", line)
                if m:
                    cid = int(m.group(1), 16)
                    classes[cid] = {"name": m.group(2).strip(), "subclasses": {}}
                    current_class = cid
                current_vendor = None # Reset vendor context
                continue

            # Handle Subclasses (Indented under 'C')
            if line.startswith("\t") and current_class is not None and not line.startswith("\t\t"):
                # Ensure we aren't accidentally parsing vendor devices as subclasses
                m = re.match(r"^\t([0-9A-Fa-f]{2})\s+(.+)", line)
                if m:
                    sid = int(m.group(1), 16)
                    classes[current_class]["subclasses"][sid] = m.group(2).strip()
                continue

            # Handle Vendor entries (No indent, starts with hex)
            if not line.startswith("\t"):
                m = re.match(r"^([0-9A-Fa-f]{4})\s+(.+)", line)
                if m:
                    vid = int(m.group(1), 16)
                    vendors[vid] = {"name": m.group(2).strip(), "devices": {}}
                    current_vendor = vid
                    current_class = None # Reset class context
                continue

            # Handle Device entries (Indented under Vendor)
            if line.startswith("\t") and not line.startswith("\t\t"):
                m = re.match(r"^\t([0-9A-Fa-f]{4})\s+(.+)", line)
                if m and current_vendor is not None:
                    did = int(m.group(1), 16)
                    vendors[current_vendor]["devices"][did] = m.group(2).strip()

    return vendors, classes

# ----------------------------------------
# Filter handling
# ----------------------------------------

def load_filter(path):
    """
    Parse a filter file into two sets used by apply_filter().

    Filter file format (one entry per line, '#' starts a comment):
        8086          -> include vendor 8086 with ALL of its devices
        10de:1cb3     -> include only device 1cb3 from vendor 10de

    Returns:
        vendor_filter  (set of int): vendor IDs whose full device list should be kept
        device_filter  (set of (int, int)): (vendor_id, device_id) pairs to keep
    """
    vendor_filter = set()   # Vendors whose full device list is included
    device_filter = set()   # Specific (vendor, device) pairs to include

    if not path:
        return vendor_filter, device_filter

    with open(path, "r") as f:
        for line in f:
            # Strip inline comments, then whitespace
            line = line.split('#')[0].strip()
            if not line:
                continue

            try:
                if ":" in line:
                    # Specific device: "VVVV:DDDD"
                    v, d = line.split(":")
                    device_filter.add((int(v, 16), int(d, 16)))
                else:
                    # Whole vendor: "VVVV"
                    vendor_filter.add(int(line, 16))
            except ValueError:
                print(f"Warning: Could not parse filter line: {line!r}")

    return vendor_filter, device_filter


def apply_filter(data, vendor_filter, device_filter):
    """
    Return a subset of `data` based on the filter sets.

    Inclusion rules (in priority order):
      1. If vendor_id is in vendor_filter -> include the vendor with ALL its devices.
      2. If any (vendor_id, device_id) pairs are in device_filter for this vendor
         -> include the vendor with ONLY those specific devices.
      3. Otherwise -> exclude the vendor entirely.

    Args:
        data          (dict): full parsed pci.ids dict from parse_pci_ids()
        vendor_filter (set):  vendor IDs to include wholesale
        device_filter (set):  (vendor_id, device_id) pairs to include selectively

    Returns:
        dict: filtered subset of `data` in the same structure
    """
    # If both filter sets are empty, nothing was requested — return everything.
    if not vendor_filter and not device_filter:
        return data

    out = {}
    for vid, vinfo in data.items():

        # Vendor is listed on its own -> keep all its devices.
        if vid in vendor_filter:
            out[vid] = vinfo
            continue

        # Check if any specific devices were requested for this vendor.
        filtered_devices = {
            did: dname
            for did, dname in vinfo["devices"].items()
            if (vid, did) in device_filter
        }

        if filtered_devices:
            out[vid] = {
                "name": vinfo["name"],
                "devices": filtered_devices
            }

        # vendor not matched by either rule -> excluded.

    return out


# ----------------------------------------
# Code generation helpers
# ----------------------------------------

def c_escape(s):
    """
    Escape backslashes and double-quotes in `s` for safe embedding in a C string literal.
    """
    return s.replace("\\", "\\\\").replace('"', '\\"')


# ----------------------------------------
# Header generation
# ----------------------------------------

def generate_header(data, classes, out_path):
    """
    Write a self-contained, freestanding C header to `out_path`.

    Layout of the generated header:
      - pci_devices[]: Flat array of pci_device_t, sorted by (vendor_id, device_id).
      - pci_vendors[]: Sorted array of pci_vendor_t with offsets/counts into pci_devices[].
      - pci_classes[]: Flat lookup table containing both Base Classes and Subclasses.

    Helper APIs (static inline):
      - pci_find_vendor():      Binary search over pci_vendors[].
      - get_pci_vendor_name():  Returns vendor name string or "Unknown Vendor".
      - get_pci_device_name():  Two-stage binary search (Vendor -> Device slice).
      - get_pci_class_name():   Linear lookup for Class/Subclass names with automatic fallback to Base Class name if subclass is unknown.

    Args:
        data (dict):    Parsed (and optionally filtered) vendor/device dict.
        classes (dict): Parsed PCI class/subclass dict.
        out_path (str): Path to write the .h file.
    """
    vendor_ids = sorted(data.keys())

    # Build the flat device array and per-vendor metadata
    devices_flat = []           # list of (vendor_id, device_id, name)
    vendor_meta  = []           # list of (vendor_id, name, offset, count)

    for vid in vendor_ids:
        vinfo = data[vid]
        devs  = sorted(vinfo["devices"].items(), key=lambda x: x[0])

        offset = len(devices_flat)          # Start index of this vendor's slice
        for did, name in devs:
            devices_flat.append((vid, did, name))
        count = len(devs)                   # Number of devices for this vendor

        vendor_meta.append((vid, vinfo["name"], offset, count))

    # Write the header
    with open(out_path, "w") as f:
        f.write("/* DISCLAIMER: THIS FILE WAS AUTO-GENERATED USING A SCRIPT.\n")
        f.write(" * DO NOT UPDATE THIS FILE MANUALLY, IT WILL GET OVERWRITTEN.\n *\n")
        f.write(" * TO UPDATE THIS FILE: \n")
        f.write(" *     1. Navigate to `<project_root>/tools/pci_dev/ \n")
        f.write(" *     2. Run `./generate.sh filtered`\n")
        f.write(" *     3. Ensure to run `make clean` in the project root\n")
        f.write(" */\n")
        f.write("#ifndef WALLOS_PCI_IDS_H\n")
        f.write("#define WALLOS_PCI_IDS_H\n\n")
        f.write("#include <stdint.h>\n\n")

        # pci_device_t + pci_devices[]
        f.write("/* Identifies a single PCI device (vendor + device id pair). */\n")
        f.write("typedef struct {\n")
        f.write("    uint16_t    vendor;\n")
        f.write("    uint16_t    device;\n")
        f.write("    const char* name;\n")
        f.write("} pci_device_t;\n\n")

        f.write("/*\n")
        f.write(" * Flat table of all known PCI devices, sorted by (vendor, device).\n")
        f.write(" * Do not index into this directly. Use get_pci_device_name().\n")
        f.write(" */\n")
        f.write("static const pci_device_t pci_devices[] = {\n")
        for vid, did, name in devices_flat:
            f.write(f'    {{0x{vid:04x}, 0x{did:04x}, "{c_escape(name)}"}},\n')
        f.write("};\n\n")

        # pci_vendor_t + pci_vendors[]
        f.write("/*\n")
        f.write(" * Describes a PCI vendor and locates its devices in pci_devices[].\n")
        f.write(" * device_offset + device_count define the contiguous slice for this vendor.\n")
        f.write(" */\n")
        f.write("typedef struct {\n")
        f.write("    uint16_t    vendor;\n")
        f.write("    uint32_t    device_offset;  /* Index of first device in pci_devices[] */\n")
        f.write("    uint32_t    device_count;   /* Number of consecutive devices          */\n")
        f.write("    const char* name;\n")
        f.write("} pci_vendor_t;\n\n")

        f.write("/* Sorted by vendor id. Required for the binary search below. */\n")
        f.write("static const pci_vendor_t pci_vendors[] = {\n")
        for vid, name, offset, count in vendor_meta:
            f.write(f'    {{0x{vid:04x}, {offset}, {count}, "{c_escape(name)}"}},\n')
        f.write("};\n\n")

        f.write("/* PCI Class and Subclass lookup table */\n")
        f.write("typedef struct {\n")
        f.write("    uint8_t base_class;\n")
        f.write("    uint8_t sub_class; /* 0xFF indicates the base class name itself */\n")
        f.write("    const char* name;\n")
        f.write("} pci_class_t;\n\n")

        f.write("static const pci_class_t pci_classes[] = {\n")
        for cid in sorted(classes.keys()):
            cinfo = classes[cid]
            # Base Class entry
            f.write(f'    {{0x{cid:02x}, 0xff, "{c_escape(cinfo["name"])}"}},\n')
            # Subclass entries
            for sid in sorted(cinfo["subclasses"].keys()):
                sname = cinfo["subclasses"][sid]
                f.write(f'    {{0x{cid:02x}, 0x{sid:02x}, "{c_escape(sname)}"}},\n')
        f.write("};\n\n")

        f.write("static const unsigned int pci_vendor_count = sizeof(pci_vendors) / sizeof(pci_vendors[0]);\n")
        f.write("static const unsigned int pci_class_count = sizeof(pci_classes) / sizeof(pci_classes[0]);\n\n")

        f.write("/*\n")
        f.write(" * Returns the name of the PCI class/subclass.\n")
        f.write(" * If the specific subclass is unknown, it returns the base class name.\n")
        f.write(" */\n")
        f.write("static inline const char* get_pci_class_name(uint8_t base_id, uint8_t sub_id) {\n")
        f.write("    const char* fallback = \"Unknown Class\";\n")
        f.write("    for (unsigned int i = 0; i < pci_class_count; i++) {\n")
        f.write("        if (pci_classes[i].base_class == base_id) {\n")
        f.write("            if (pci_classes[i].sub_class == sub_id) return pci_classes[i].name;\n")
        f.write("            if (pci_classes[i].sub_class == 0xff) fallback = pci_classes[i].name;\n")
        f.write("        }\n")
        f.write("    }\n")
        f.write("    return fallback;\n")
        f.write("}\n\n")

        # pci_find_vendor()
        f.write("/*\n")
        f.write(" * Binary search for a vendor entry by id.\n")
        f.write(" * Returns a pointer into pci_vendors[], or NULL if not found.\n")
        f.write(" */\n")
        f.write("static inline const pci_vendor_t* pci_find_vendor(uint32_t vendor_id) {\n")
        f.write("    int left  = 0;\n")
        f.write("    int right = (int)pci_vendor_count - 1;\n")
        f.write("    while (left <= right) {\n")
        f.write("        int mid      = left + (right - left) / 2; /* avoids overflow vs (l+r)/2 */\n")
        f.write("        uint16_t v   = pci_vendors[mid].vendor;\n")
        f.write("        if      (v == vendor_id) return &pci_vendors[mid];\n")
        f.write("        else if (v <  vendor_id) left  = mid + 1;\n")
        f.write("        else                     right = mid - 1;\n")
        f.write("    }\n")
        f.write("    return 0;\n")
        f.write("}\n\n")

        # get_pci_vendor_name()
        f.write('/* Returns the vendor name string, or "Unknown Vendor" if not found. */\n')
        f.write("static inline const char* get_pci_vendor_name(uint32_t vendor_id) {\n")
        f.write("    const pci_vendor_t* v = pci_find_vendor(vendor_id);\n")
        f.write('    return v ? v->name : "Unknown Vendor";\n')
        f.write("}\n\n")

        # get_pci_device_name()
        f.write("/*\n")
        f.write(" * Two-stage lookup:\n")
        f.write(" *   1. Binary search pci_vendors[] for the vendor.\n")
        f.write(" *   2. Binary search that vendor's slice of pci_devices[] for the device.\n")
        f.write(" * Returns the device name string, or \"Unknown Device\" if either lookup fails.\n")
        f.write(" */\n")
        f.write("static inline const char*get_pci_device_name(uint32_t vendor_id, uint32_t device_id) {\n")
        f.write("    const pci_vendor_t* v = pci_find_vendor(vendor_id);\n")
        f.write('    if (!v) return "Unknown Device";\n\n')
        f.write("    int left  = 0;\n")
        f.write("    int right = (int)v->device_count - 1;\n")
        f.write("    while (left <= right) {\n")
        f.write("        int mid              = left + (right - left) / 2;\n")
        f.write("        const pci_device_t* d = &pci_devices[v->device_offset + mid];\n")
        f.write("        if      (d->device == device_id) return d->name;\n")
        f.write("        else if (d->device <  device_id) left  = mid + 1;\n")
        f.write("        else                             right = mid - 1;\n")
        f.write("    }\n")
        f.write('    return "Unknown Device";\n')
        f.write("}\n\n")

        f.write("#endif /* PCI_IDS_H */\n")


# ----------------------------------------
# Entry point
# ----------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Generate a freestanding C header from a pci.ids database."
    )
    parser.add_argument("input",    help="Path to pci.ids file")
    parser.add_argument("output",   help="Path to write the output .h file")
    parser.add_argument(
        "--mode",
        choices=["full", "condensed"],
        default="full",
        help="'full' includes all entries; 'condensed' requires --filter",
    )
    parser.add_argument(
        "--filter",
        metavar="FILE",
        help="Filter file for condensed mode (vendor or vendor:device lines)",
    )
    args = parser.parse_args()

    data, classes = parse_pci_ids(args.input)

    if args.mode == "condensed":
        if not args.filter:
            parser.error("--mode condensed requires --filter <file>")
        vendor_filter, device_filter = load_filter(args.filter)
        data = apply_filter(data, vendor_filter, device_filter)

    generate_header(data, classes, args.output)
    print(f"Wrote {args.output} ({len(data)} vendors)")


if __name__ == "__main__":
    main()