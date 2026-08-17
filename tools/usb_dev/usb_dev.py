#!/usr/bin/env python3
"""
usb_gen.py — Generate a freestanding C header from a usb.ids database.

This is a copied and repurposed version of the pci_dev.py file in ../pci_dev/

The output header contains:
  - A flat array of usb_device_id_t structs (vendor, device id, name)
  - A sorted array of usb_vendor_t structs (vendor id, name, offset/count into device array)
  - Binary-search helpers: usb_find_vendor(), get_usb_vendor_name(), get_usb_device_name()

Usage:
    python usb_gen.py usb.ids output.h --mode full
    python usb_gen.py usb.ids output.h --mode condensed --filter filter.txt

Filter format (for condensed mode):
    # Vendor-only line: include all devices for this vendor
    8086

    # Vendor + device line: include only this specific device
    10de:1cb3
"""

import argparse
import re


# ----------------------------------------
# Parsing usb.ids
# ----------------------------------------

# usb.ids is a superset of the pci.ids layout: on top of the vendor/device
# list and the "C" class/subclass/protocol list, it also contains a bunch of
# unrelated top-level sections (Audio Class Terminal Types, HID Descriptor
# Types, Physical Descriptor Bias/Item Types, Languages, HID Country Codes,
# Video Class Terminal Types, and possibly others added over time). We only
# care about the vendor/device and class/subclass sections, so we track an
# explicit "current section" and treat every other top-level line as
# belonging to a section we ignore. This guarantees indented lines that
# belong to those other sections never get mistaken for USB devices or
# class subclasses, no matter what section headers the file adds later.

_CLASS_RE    = re.compile(r"^C\s+([0-9A-Fa-f]{2})\s+(.+)")
_VENDOR_RE   = re.compile(r"^([0-9A-Fa-f]{4})\s+(.+)")
_DEVICE_RE   = re.compile(r"^\t([0-9A-Fa-f]{4})\s+(.+)")
_SUBCLASS_RE = re.compile(r"^\t([0-9A-Fa-f]{2})\s+(.+)")


def parse_usb_ids(path):
    """
    Parse a usb.ids file into a structured dictionary of vendors and classes.

    The usb.ids format uses prefixes and indentation to encode hierarchy:
      - Lines starting with 'C':          USB Class entries (2-hex-digit id + name)
      - Lines with one tab (under 'C'):   USB Subclass entries
      - Lines with two tabs (under 'C'):  USB Protocol entries (skipped here)
      - Lines with no leading tab:        Vendor entries (4-hex-digit id + name)
      - Lines with one leading tab:       Device entries (4-hex-digit id + name)
      - Lines with two leading tabs:      Interface entries (skipped here)
      - Lines starting with '#' or blank: Comments / whitespace (skipped)
      - Any other top-level line:         Start of an unrelated section
                                           (Audio Terminal Types, HID
                                           Descriptor Types, Languages,
                                           etc.) whose contents are skipped
                                           entirely.

    Returns:
        tuple: (vendors, classes)
            vendors (dict): { vendor_id (int) -> { "name": str, "devices": { device_id (int) -> str } } }
            classes (dict): { class_id (int) -> { "name": str, "subclasses": { sub_id (int) -> str } } }
    """
    vendors = {}
    classes = {}

    current_vendor = None
    current_class = None
    section = "other"  # one of: "vendor", "class", "other"

    with open(path, "r", encoding="latin1") as f:
        for raw_line in f:
            line = raw_line.rstrip("\n")

            if not line.strip() or line.lstrip().startswith("#"):
                continue

            # Top-level (non-indented) line: decide which section we're
            # entering and reset per-section state accordingly.
            if not line.startswith("\t"):
                m = _CLASS_RE.match(line)
                if m:
                    cid = int(m.group(1), 16)
                    classes[cid] = {"name": m.group(2).strip(), "subclasses": {}}
                    current_class = cid
                    current_vendor = None
                    section = "class"
                    continue

                m = _VENDOR_RE.match(line)
                if m:
                    vid = int(m.group(1), 16)
                    vendors[vid] = {"name": m.group(2).strip(), "devices": {}}
                    current_vendor = vid
                    current_class = None
                    section = "vendor"
                    continue

                # Any other top-level line (e.g. "AT ...", "HID ...",
                # "L ...", "PHY ...", "HCC ...", "VT ...") is the start of
                # a section we don't parse. Clear state so its indented
                # children can't be attributed to a stale vendor/class.
                current_vendor = None
                current_class = None
                section = "other"
                continue

            # Indented line: only meaningful directly under a vendor or
            # class we're currently tracking, and only at one tab of
            # depth (two-tab lines are interfaces/protocols, which we
            # don't need). Everything else - including any indentation
            # under an "other" section - is skipped.
            if line.startswith("\t\t"):
                continue

            if section == "vendor" and current_vendor is not None:
                m = _DEVICE_RE.match(line)
                if m:
                    did = int(m.group(1), 16)
                    vendors[current_vendor]["devices"][did] = m.group(2).strip()
                continue

            if section == "class" and current_class is not None:
                m = _SUBCLASS_RE.match(line)
                if m:
                    sid = int(m.group(1), 16)
                    classes[current_class]["subclasses"][sid] = m.group(2).strip()
                continue

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
        data          (dict): full parsed usb.ids dict from parse_usb_ids()
        vendor_filter (set):  vendor IDs to include wholesale
        device_filter (set):  (vendor_id, device_id) pairs to include selectively

    Returns:
        dict: filtered subset of `data` in the same structure
    """
    
    # --- Check for missing filters and warn ---
    for vid in vendor_filter:
        if vid not in data:
            print(f"Warning: Filtered vendor {vid:04x} not found in usb.ids database.")
            
    for vid, did in device_filter:
        if vid not in data:
            print(f"Warning: Filtered device {vid:04x}:{did:04x} not found (vendor {vid:04x} is missing).")
        elif did not in data[vid]["devices"]:
            print(f"Warning: Filtered device {vid:04x}:{did:04x} not found in vendor {vid:04x}'s device list.")

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
      - usb_devices[]: Flat array of usb_device_id_t, sorted by (vendor_id, device_id).
      - usb_vendors[]: Sorted array of usb_vendor_t with offsets/counts into usb_devices[].
      - usb_classes[]: Flat lookup table containing both Base Classes and Subclasses.

    Helper APIs (static inline):
      - usb_find_vendor():      Binary search over usb_vendors[].
      - get_usb_vendor_name():  Returns vendor name string or "Unknown Vendor".
      - get_usb_device_name():  Two-stage binary search (Vendor -> Device slice).
      - get_usb_class_name():   Linear lookup for Class/Subclass names with automatic fallback to Base Class name if subclass is unknown.

    Args:
        data (dict):    Parsed (and optionally filtered) vendor/device dict.
        classes (dict): Parsed USB class/subclass dict.
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
        f.write(" *     1. Navigate to `<project_root>/tools/usb_dev/ \n")
        f.write(" *     2. Run `./generate.sh filtered`\n")
        f.write(" *     3. Ensure to run `make clean` in the project root\n")
        f.write(" */\n")
        f.write("#ifndef WALLOS_USB_IDS_H\n")
        f.write("#define WALLOS_USB_IDS_H\n\n")
        f.write("#include <stdint.h>\n\n")

        # usb_device_id_t + usb_devices[]
        f.write("/* Identifies a single USB device (vendor + device id pair). */\n")
        f.write("typedef struct {\n")
        f.write("    uint16_t    vendor;\n")
        f.write("    uint16_t    device;\n")
        f.write("    const char* name;\n")
        f.write("} usb_device_id_t;\n\n")

        f.write("/*\n")
        f.write(" * Flat table of all known USB devices, sorted by (vendor, device).\n")
        f.write(" * Do not index into this directly. Use get_usb_device_name().\n")
        f.write(" */\n")
        f.write("static const usb_device_id_t usb_devices[] = {\n")
        for vid, did, name in devices_flat:
            f.write(f'    {{0x{vid:04x}, 0x{did:04x}, "{c_escape(name)}"}},\n')
        f.write("};\n\n")

        # usb_vendor_t + usb_vendors[]
        f.write("/*\n")
        f.write(" * Describes a USB vendor and locates its devices in usb_devices[].\n")
        f.write(" * device_offset + device_count define the contiguous slice for this vendor.\n")
        f.write(" */\n")
        f.write("typedef struct {\n")
        f.write("    uint16_t    vendor;\n")
        f.write("    uint32_t    device_offset;  /* Index of first device in usb_devices[] */\n")
        f.write("    uint32_t    device_count;   /* Number of consecutive devices          */\n")
        f.write("    const char* name;\n")
        f.write("} usb_vendor_t;\n\n")

        f.write("/* Sorted by vendor id. Required for the binary search below. */\n")
        f.write("static const usb_vendor_t usb_vendors[] = {\n")
        for vid, name, offset, count in vendor_meta:
            f.write(f'    {{0x{vid:04x}, {offset}, {count}, "{c_escape(name)}"}},\n')
        f.write("};\n\n")

        f.write("/* USB Class and Subclass lookup table */\n")
        f.write("typedef struct {\n")
        f.write("    uint8_t base_class;\n")
        f.write("    uint8_t sub_class; /* 0xFF indicates the base class name itself */\n")
        f.write("    const char* name;\n")
        f.write("} usb_class_t;\n\n")

        f.write("static const usb_class_t usb_classes[] = {\n")
        for cid in sorted(classes.keys()):
            cinfo = classes[cid]
            # Base Class entry
            f.write(f'    {{0x{cid:02x}, 0xff, "{c_escape(cinfo["name"])}"}},\n')
            # Subclass entries
            for sid in sorted(cinfo["subclasses"].keys()):
                sname = cinfo["subclasses"][sid]
                f.write(f'    {{0x{cid:02x}, 0x{sid:02x}, "{c_escape(sname)}"}},\n')
        f.write("};\n\n")

        f.write("static const unsigned int usb_vendor_count = sizeof(usb_vendors) / sizeof(usb_vendors[0]);\n")
        f.write("static const unsigned int usb_class_count = sizeof(usb_classes) / sizeof(usb_classes[0]);\n\n")

        f.write("/*\n")
        f.write(" * Returns the name of the USB class/subclass.\n")
        f.write(" * If the specific subclass is unknown, it returns the base class name.\n")
        f.write(" */\n")
        f.write("static inline const char* get_usb_class_name(uint8_t base_id, uint8_t sub_id) {\n")
        f.write("    const char* fallback = \"Unknown Class\";\n")
        f.write("    for (unsigned int i = 0; i < usb_class_count; i++) {\n")
        f.write("        if (usb_classes[i].base_class == base_id) {\n")
        f.write("            if (usb_classes[i].sub_class == sub_id) return usb_classes[i].name;\n")
        f.write("            if (usb_classes[i].sub_class == 0xff) fallback = usb_classes[i].name;\n")
        f.write("        }\n")
        f.write("    }\n")
        f.write("    return fallback;\n")
        f.write("}\n\n")

        # usb_find_vendor()
        f.write("/*\n")
        f.write(" * Binary search for a vendor entry by id.\n")
        f.write(" * Returns a pointer into usb_vendors[], or NULL if not found.\n")
        f.write(" */\n")
        f.write("static inline const usb_vendor_t* usb_find_vendor(uint32_t vendor_id) {\n")
        f.write("    int left  = 0;\n")
        f.write("    int right = (int)usb_vendor_count - 1;\n")
        f.write("    while (left <= right) {\n")
        f.write("        int mid      = left + (right - left) / 2; /* avoids overflow vs (l+r)/2 */\n")
        f.write("        uint16_t v   = usb_vendors[mid].vendor;\n")
        f.write("        if      (v == vendor_id) return &usb_vendors[mid];\n")
        f.write("        else if (v <  vendor_id) left  = mid + 1;\n")
        f.write("        else                     right = mid - 1;\n")
        f.write("    }\n")
        f.write("    return 0;\n")
        f.write("}\n\n")

        # get_usb_vendor_name()
        f.write('/* Returns the vendor name string, or "Unknown Vendor" if not found. */\n')
        f.write("static inline const char* get_usb_vendor_name(uint32_t vendor_id) {\n")
        f.write("    const usb_vendor_t* v = usb_find_vendor(vendor_id);\n")
        f.write('    return v ? v->name : "Unknown Vendor";\n')
        f.write("}\n\n")

        # get_usb_device_name()
        f.write("/*\n")
        f.write(" * Two-stage lookup:\n")
        f.write(" *   1. Binary search usb_vendors[] for the vendor.\n")
        f.write(" *   2. Binary search that vendor's slice of usb_devices[] for the device.\n")
        f.write(" * Returns the device name string, or \"Unknown Device\" if either lookup fails.\n")
        f.write(" */\n")
        f.write("static inline const char* get_usb_device_name(uint32_t vendor_id, uint32_t device_id) {\n")
        f.write("    const usb_vendor_t* v = usb_find_vendor(vendor_id);\n")
        f.write('    if (!v) return "Unknown Device";\n\n')
        f.write("    int left  = 0;\n")
        f.write("    int right = (int)v->device_count - 1;\n")
        f.write("    while (left <= right) {\n")
        f.write("        int mid              = left + (right - left) / 2;\n")
        f.write("        const usb_device_id_t* d = &usb_devices[v->device_offset + mid];\n")
        f.write("        if      (d->device == device_id) return d->name;\n")
        f.write("        else if (d->device <  device_id) left  = mid + 1;\n")
        f.write("        else                             right = mid - 1;\n")
        f.write("    }\n")
        f.write('    return "Unknown Device";\n')
        f.write("}\n\n")

        f.write("#endif /* WALLOS_USB_IDS_H */\n")


# ----------------------------------------
# Entry point
# ----------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Generate a freestanding C header from a usb.ids database."
    )
    parser.add_argument("input",    help="Path to usb.ids file")
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

    data, classes = parse_usb_ids(args.input)

    if args.mode == "condensed":
        if not args.filter:
            parser.error("--mode condensed requires --filter <file>")
        vendor_filter, device_filter = load_filter(args.filter)
        data = apply_filter(data, vendor_filter, device_filter)

    generate_header(data, classes, args.output)
    print(f"Wrote {args.output} ({len(data)} vendors)")


if __name__ == "__main__":
    main()