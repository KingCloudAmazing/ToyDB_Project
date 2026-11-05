# Storage Comparison: Slotted Pages vs. Static Records

## Dataset and measurement basis

- Student records: **17,813**
- Actual record payload: **1,759,011 bytes**
- Page size: **4,096 bytes**
- Payload utilization = actual Student payload bytes / allocated data-page bytes × 100.
- Static record padding = reserved record-slot bytes − actual Student payload bytes.
- PF page-link and file-header bytes are excluded from payload utilization and included in estimated PF file size.

## Results

| Configuration | Records/page | Pages | Payload utilization | Record padding | Estimated PF file size |
|---|---:|---:|---:|---:|---:|
| Slotted pages | 39.24 average | 454 | 94.59% | 0 bytes | 1,861,408 bytes |
| Static M=108 | 37 | 482 | 89.10% | 164,793 bytes | 1,976,208 bytes |
| Static M=112 | 35 | 509 | 84.37% | 236,045 bytes | 2,086,908 bytes |
| Static M=128 | 31 | 575 | 74.69% | 521,053 bytes | 2,357,508 bytes |
| Static M=160 | 25 | 713 | 60.23% | 1,091,069 bytes | 2,923,308 bytes |
| Static M=256 | 15 | 1,188 | 36.15% | 2,801,117 bytes | 4,870,808 bytes |

## Interpretation

1. Slotted pages allocated **454 pages**, compared with **482 pages** for static M=108.
2. Relative to static M=108, slotted pages used **5.81% fewer data pages** and improved payload utilization by **5.49 percentage points**.
3. Static storage reserves a fixed maximum slot length. As M increases, padding grows; at M=256 it is **2,801,117 bytes**.
4. Slotted storage packs variable-length records and maintains a slot directory, avoiding the fixed-size reservation imposed by larger M values.
5. For this Student dataset, slotted pages are the most space-efficient configuration tested.

## Measurement notes

Payload utilization differs from occupied-page utilization. Page headers, slot descriptors and static record-length metadata are overhead, not Student payload. The estimated PF file size includes the PF page wrapper/link and file header. The slotted PF size is derived from the project's PF layout; static PF figures are parsed from the C test output.

## Reproduction

From this directory, run:

```bash
python3 generate_storage_comparison.py
```

The script cleans and runs the storage tests, captures the complete output in `storage_test_results.txt`, validates the reported byte accounting, and regenerates the CSV, graphs and this note.

## Figures

- `storage_pages_comparison.png`
- `storage_payload_utilization.png`
