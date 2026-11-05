# AM Layer Benchmark

This benchmark compares the three Student B+ tree index construction methods already implemented in the AM layer:

1. One-shot insertion into the index after loading Student records.
2. Incremental insertion while Student records are loaded.
3. Sorted-file bulk loading.

## Prerequisites

- The unchanged dataset must be at `data/student.txt` in the ToyDB project root.
- Build tools: `cc`, `make`, and Bash.
- Python 3 with `matplotlib` installed.

If matplotlib is missing, install it with:

```bash
python3 -m pip install matplotlib
```

## Run everything

From the ToyDB project root:

```bash
cd toydb/amlayer
chmod +x run_am_benchmarks.sh
./run_am_benchmarks.sh
```

The script builds the PF layer, builds the three existing AM test programs, runs each construction method, measures a common duplicate-key search, writes CSV files, and generates graphs.

## Output

Results are written to `toydb/amlayer/benchmark_results/`:

- `build_time.csv`: end-to-end wall-clock time for each construction program.
- `query_metrics.csv`: 30 independent measurements for each method.
- `execution.log`: construction test output and benchmark run summary.
- `graphs/construction_time.png`: construction-time comparison.
- `graphs/query_time.png`: median query-time comparison.
- `graphs/page_accesses.png`: median logical page accesses and physical page reads.

## Measurement methodology

- Construction time is measured around each complete existing Student-index test program. It includes the Student record-storage preparation and the method-specific index construction. Therefore, it is an end-to-end construction comparison, not just the time spent inside the B+ tree insertion loop.
- Query measurements use the same equality search on roll number `97305818`, which is known to have two matching records in the dataset.
- Each query measurement runs in a fresh process. This avoids carrying scan-descriptor state or buffer contents from one measurement into the next.
- The query timer covers opening the index scan, retrieving matching RIDs, and closing the scan. Process startup and index opening are outside the timed region.
- Page-access counters are read from the PF buffer statistics. `logical_page_accesses` counts logical page reads, while `physical_page_reads` counts page reads from the file into the buffer.
- Query time is recorded 30 times per construction method. The graphs use the median to reduce timing noise. The raw measurements remain in `query_metrics.csv`.

## Results from the validation run

These are measurements from the machine used to validate the scripts; your machine may produce different timings.

| Method | End-to-end build time (s) | Median query time (s) | Logical page accesses | Physical page reads |
|---|---:|---:|---:|---:|
| One-shot | 0.04 | 0.0000855 | 5 | 2 |
| Incremental | 0.05 | 0.0000665 | 5 | 2 |
| Bulk load | 0.04 | 0.0000885 | 5 | 2 |

All three methods returned the expected two matching RIDs for the benchmark search. The identical page counts are plausible for this particular lookup because each index search follows a root-to-leaf path of the same length. The timing differences are very small and should not be over-interpreted.

## Reproducibility note

The bulk-load test now removes a stale `student_bulk.0` index file before creating a fresh index. This makes repeated benchmark runs independent of a previously generated bulk index. The existing B+ tree construction algorithms have otherwise been left unchanged.
