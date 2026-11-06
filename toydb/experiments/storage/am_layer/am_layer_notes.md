# AM Layer Experiment Notes

Generated: 2026-11-06T14:37:17+05:30

## Automated workflow

- Legacy AM correctness tests: test1, test2 and test3.
- Student bulk-loader regressions: full-dataset duplicate searches (G1),
  synthetic duplicate boundary (G2), root capacity (G3), and ALL scan (G4).
- B+ tree construction using one-shot insertion.
- B+ tree construction using incremental insertion.
- B+ tree construction using sorted-file bulk loading.
- 30 duplicate-key query measurements per construction method.

## Build times

| Method | End-to-end build time (seconds) |
|---|---:|
| one_shot | 0.070000 |
| incremental | 0.070000 |
| bulk_load | 0.050000 |

## Query summary

| Method | Runs | Median query time (seconds) | Median logical page accesses | Median physical page reads |
|---|---:|---:|---:|---:|
| one_shot | 30 | 0.000022000 | 6.0 | 3.0 |
| incremental | 30 | 0.000028000 | 6.0 | 3.0 |
| bulk_load | 30 | 0.000023000 | 6.0 | 3.0 |

## Methodology and limitations

- Build time is the end-to-end time reported by the existing C test program. It includes Student record-storage preparation and index construction; it is not an isolated B+ tree-only measurement.
- The common query targets duplicate roll number 97305818 and is expected to return two matches.
- Query timing is measured 30 times per method. Medians reduce the effect of individual timing outliers; very small differences should not be over-interpreted.
- Raw query measurements are retained in query_metrics.csv.
- Page counters come from the PF buffer statistics.
- Builds and tests run in a temporary project copy. Only generated result artifacts are copied into this output folder.

See AM_BENCHMARK_README.md in toydb/amlayer for the original benchmark methodology. Test and benchmark output is shown in the terminal; no log files are saved.
