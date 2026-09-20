OUTPUT_H="../../src/kernel/klibc/include/filesystem/partitions/gpt_partition_type.h"
GUID_FILE="partitions.csv"
PY_SCRIPT=guid.py

python3 "$PY_SCRIPT" --csv partitions.csv --output "$OUTPUT_H"