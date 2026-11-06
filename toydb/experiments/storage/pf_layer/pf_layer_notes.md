# PF Layer Automated Tests and Experiments

Run the complete PF workflow from the PF layer directory:

```bash
python3 run_pf_experiments.py
```

The runner compiles a temporary copy of the PF C sources. It does not overwrite
tracked test executables or create build files in the repository.

## Correctness tests

- `testhash`
- `testpf`
- `testbuf`
- `teststats`
- `testlru`

## Performance experiments

1. LRU/MRU replacement comparison for buffer capacities 2, 4, 8 and 16.
2. Read/write workload comparison from 100% reads to 100% writes, using a
   three-page buffer and 20 operations per workload.

## Generated outputs

- `pf_test_summary.csv`: correctness-test status and exit codes.
- `pf_experiment_results.csv`: capacity and replacement-policy measurements.
- `pf_workload_results.csv`: read/write workload measurements.
- `pf_experiment_results.xlsx`: Excel workbook with overview and all result tables.
- `capacity_vs_physical_reads.png`
- `capacity_vs_hit_ratio.png`
- `lru_vs_mru_comparison.png`
- `workload_physical_reads.png`
- `workload_physical_writes.png`
- `pf_experiment_analysis.txt`

The workload's `logical_reads` column counts `PF_GetThisPage` calls. Since the
test calls it for every operation, that value is 20 for every workload. The
separate `read_operations` and `write_operations` columns describe the mix of
read-only and modifying operations.
