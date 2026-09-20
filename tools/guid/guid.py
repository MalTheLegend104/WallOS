#!/usr/bin/env python3

import argparse
import csv
import uuid

HEADER_TEMPLATE = """/* DISCLAIMER: THIS FILE WAS AUTO-GENERATED USING A SCRIPT.
 * DO NOT UPDATE THIS FILE MANUALLY, IT WILL GET OVERWRITTEN.
 * TO UPDATE THIS FILE: 
 *     1. Navigate to `<project_root>/tools/guid/guid.py/ 
 *     2. Run `./generate.sh`
 *     3. Ensure to run `make clean` in the project root
 */
#ifndef WDM_MOCK_GPT_PARTITION_TYPE_H
#define WDM_MOCK_GPT_PARTITION_TYPE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {{
#endif

/**
 * @brief Identifier for well-known GPT partition types.
 */
typedef enum {{
\tGPT_TYPE_UNKNOWN = 0,
{enum_entries}
}} gpt_partition_type_id_t;


/**
 * @brief Known partition type GUID entry: on-disk bytes, enum id, and display name.
 */
typedef struct {{
\tuint8_t guid[16];
\tgpt_partition_type_id_t id;
\tconst char* name;
}} gpt_known_partition_type_t;

/**
 * @brief Table of well-known GPT partition type GUIDs, in on-disk mixed-endian byte order.
 */
static const gpt_known_partition_type_t gpt_known_partition_types[] = {{
\t/* ENSURE THESE ARE IN MIXED-ENDIAN ORDER. MOST ONLINE REPRESENTATIONS ARE IN CANONICAL GUID/UUID REPRESENTATION. */
{table_entries}
}};

#define GPT_KNOWN_PARTITION_TYPE_COUNT (sizeof(gpt_known_partition_types) / sizeof(gpt_known_partition_types[0]))

#ifdef __cplusplus
}}
#endif

#endif //WDM_MOCK_GPT_PARTITION_TYPE_H
"""

def guid_to_mixed_endian(guid: str) -> list[int]:
    """
    Convert a canonical GUID into GPT on-disk mixed-endian bytes.
    """
    u = uuid.UUID(guid)
    return list(u.bytes_le)


def format_bytes(data: list[int]) -> str:
    return ",".join(f"0x{x:02X}" for x in data)


def format_table_entry(guid: str, enum_name: str, string_name: str) -> str:
    data = guid_to_mixed_endian(guid)

    return (
        f'\t{{ {{ {format_bytes(data)} }}, '
        f'{enum_name}, '
        f'"{string_name}" }},'
    )


def format_enum(enum_name: str) -> str:
    return f"\t{enum_name},"


def process_single(args):
    print("// Enum")
    print(format_enum(args.enum))
    print()

    print("// Table Entry")
    print(format_table_entry(args.guid, args.enum, args.name))


def process_csv(filename, output=None):
    enums = []
    entries = []

    with open(filename, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)

        if reader.fieldnames is None:
            raise ValueError("CSV is empty or missing a header row.")

        required = {"GUID", "ENUM", "STRING"}
        if not required.issubset(reader.fieldnames):
            raise ValueError(
                "CSV must contain the columns: GUID,ENUM,STRING"
            )

        for row in reader:
            guid = row["GUID"].strip()
            enum_name = row["ENUM"].strip()
            string_name = row["STRING"].strip()

            enums.append(f"\t{enum_name},")
            entries.append(
                format_table_entry(guid, enum_name, string_name)
            )

    header = HEADER_TEMPLATE.format(
        enum_entries="\n".join(enums),
        table_entries="\n".join(entries),
    )

    if output:
        with open(output, "w", encoding="utf-8", newline="\n") as f:
            f.write(header)
    else:
        print(header)
    
    print(f"Wrote file to {output}")


def main():
    parser = argparse.ArgumentParser(
        description="Convert canonical GUIDs to GPT mixed-endian table entries."
    )

    parser.add_argument(
        "--csv",
        metavar="FILE",
        help="Read GUID,ENUM,STRING entries from a CSV file.",
    )

    parser.add_argument(
        "-o",
        "--output",
        help="Write generated header to this file.",
    )

    parser.add_argument("guid", nargs="?")
    parser.add_argument("enum", nargs="?")
    parser.add_argument("name", nargs="?")

    args = parser.parse_args()

    if args.csv:
        process_csv(args.csv, args.output)
        return

    if not (args.guid and args.enum and args.name):
        parser.error(
            "Either provide --csv FILE or GUID ENUM STRING."
        )

    process_single(args)


if __name__ == "__main__":
    main()