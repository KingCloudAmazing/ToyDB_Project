# Slotted Page Notes: Slotted Pages vs. Static Records

## Dataset

- Student records: **17,813**
- Actual payload: **1,759,011 bytes**
- Page size: **4,096 bytes**

## Measurement formula

Payload utilization:

`actual payload bytes / allocated data-page bytes * 100`

PF page-link and file-header bytes are excluded from payload utilization but included in estimated PF file size.

## Results

| Configuration | Records/page | Pages | Payload utilization | Padding | Estimated PF file size |
|---|---:|---:|---:|---:|---:|
| Slotted pages | 39.24 average | 454 | 94.59% | 0 bytes | 1,861,408 bytes |
| Static M=108 | 37 | 482 | 89.10% | 164,793 bytes | 1,976,208 bytes |
| Static M=112 | 35 | 509 | 84.37% | 236,045 bytes | 2,086,908 bytes |
| Static M=128 | 31 | 575 | 74.69% | 521,053 bytes | 2,357,508 bytes |
| Static M=160 | 25 | 713 | 60.23% | 1,091,069 bytes | 2,923,308 bytes |
| Static M=256 | 15 | 1,188 | 36.15% | 2,801,117 bytes | 4,870,808 bytes |

## Interpretation

1. Slotted pages allocated **454 pages**, compared with **482 pages** for static M=108.

2. Slotted pages used **5.81% fewer data pages** than static M=108.

3. Payload utilization improved by **5.49 percentage points**.

4. Static storage reserves a fixed maximum length for each record. Increasing M increases unused record-slot space.

5. At M=256, reserved record padding is **2,801,117 bytes**.

6. Slotted pages store variable-length records and use a slot directory, avoiding the large fixed-size reservations required by larger static-record limits.

## Measurement notes

Payload utilization differs from occupied-page utilization.

Page headers, slot descriptors and record-length metadata are storage overhead rather than Student payload.

The slotted-page page count used in the comparison is extracted from `test_student_pf`, which traverses the PF-backed file using `PF_GetFirstPage` and `PF_GetNextPage`. The in-memory `test_student_dataset` count is separately extracted and must match as a cross-check.

The static-record page counts are extracted from the PF-backed static storage tests.

## Generated figures

- storage_pages_comparison.png
- storage_payload_utilization.png

## Reproduction

From this directory:

```bash
python3 generate_storage_comparison.py
# Add --force to rerun and replace existing results.

