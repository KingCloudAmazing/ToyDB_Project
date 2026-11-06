# AM Layer Benchmark

This benchmark compares the three Student B+ tree construction methods already implemented in the AM layer:

1. One-shot insertion into the index after loading Student records.
2. Incremental insertion while Student records are loaded.
3. Sorted-file bulk loading.

For the broader AM page layout, G1-G4 design decisions, and limitations, see [`README.md`](README.md). For the project-wide report, see [`../Technical_Report.md`](../Technical_Report.md).

## Prerequisites

- The supplied dataset at `data/student.txt` in the project root.
- A C compiler, `make`, and Bash.
- Python 3, Matplotlib, and OpenPyXL for the unified result artifacts.
- Install dependencies from the repository root:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r toydb/experiments/storage/requirements.txt
```

## Run the complete AM workflow

From the repository root:

```bash
python toydb/experiments/storage/run_am_experiments.py --force
```

The runner copies the required AM, PF, slotted-page, and dataset directories into a temporary workspace. It builds and runs the three legacy AM tests, the Student bulk-loader full-dataset regression, the synthetic duplicate-boundary regression, the root-capacity regression, and the three construction/query benchmarks. It does not use the repository's build directories as its temporary build area.

The lower-level benchmark can also be run directly:

```bash
cd toydb/amlayer
chmod +x run_am_benchmarks.sh
./run_am_benchmarks.sh
```

The direct script writes intermediate results under `toydb/amlayer/benchmark_results/`. The unified runner writes the consolidated outputs under `toydb/experiments/storage/am_layer/`.

## Output

- `build_time.csv`: one end-to-end wall-clock build measurement per construction method for the validation run.
- `query_metrics.csv`: 30 independent duplicate-key query measurements for each method (90 rows total).
- `am_benchmark_results.xlsx`: workbook containing build and query raw/summary sheets.
- `am_layer_notes.md`: generated result summary and methodology notes.
- `graphs/construction_time.png`: construction-time comparison.
- `graphs/query_time.png`: median query-time comparison.
- `graphs/page_accesses.png`: median logical page accesses and physical page reads.

## Measurement methodology

- Construction time is measured around each complete Student-index construction test program. It includes Student record-storage preparation and the construction method; it is not isolated B+ tree insertion time.
- The common equality query targets roll number `97305818`, which is expected to return two matching RIDs.
- Each query measurement runs in a fresh process. The timer covers opening the index scan, retrieving matching RIDs, and closing the scan. Process startup and index-file open are outside the timed region.
- Query timing is recorded 30 times per method. The graphs use the median; raw measurements are retained in `query_metrics.csv`.
- Page counters come from PF buffer statistics.
- The AM runner also executes G1-G4 regressions. The full-data run checks duplicate roll numbers and the 17,813-entry `ALL` scan; the synthetic boundary test uses 253 distinct keys before a four-RID duplicate group; the capacity test accepts 341 child pages and rejects 342.

## Results from the latest supplied validation run

These measurements were generated on the validation machine on 6 November 2026. Timings may differ on another machine.

### End-to-end construction

| Method | End-to-end build time (s) |
|---|---:|
| One-shot | 0.070 |
| Incremental | 0.070 |
| Sorted bulk load | 0.050 |

### Duplicate-key equality query

| Method | Runs | Median query time (s) | Median query time (μs) | Median logical page accesses | Median physical page reads | Matches |
|---|---:|---:|---:|---:|---:|---:|
| One-shot | 30 | 0.000022 | 22 | 6 | 3 | 2 |
| Incremental | 30 | 0.000028 | 28 | 6 | 3 | 2 |
| Sorted bulk load | 30 | 0.000023 | 23 | 6 | 3 | 2 |

The 90 measurements all returned the expected two RIDs. Median page-access counts are the same for the selected equality lookup. The differences between 22, 28, and 23 microseconds are small; the benchmark should not be used to claim broad superiority of one construction path for every query.

## Reproducibility and scope notes

- The AM bulk loader keeps each equal-key group within one leaf and links leaf pages in sorted order.
- The internal root stores `Child0 | Key0 | Child1 | ...`; each separator is the first key of the child to its right.
- Root capacity for the current eight-character roll key is 340 separators / 341 children. Overflow is rejected safely.
- The current bulk loader implements one internal root above the leaf pages; multi-level internal-tree construction is not implemented.
- G5 additional error cleanup and G6 end-to-end RID-to-Student content validation were deferred.
