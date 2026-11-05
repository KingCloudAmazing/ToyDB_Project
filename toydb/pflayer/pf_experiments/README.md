# PF Buffer Manager Experiments

This folder contains the recorded results and analysis for the PF buffer-manager
LRU/MRU experiments.

## Experiment setup

- Page-access sequence: `0, 1, 2, 3, 0, 1, 4, 0, 1, 2, 3, 4`
- Logical reads per configuration: 12
- Replacement policies: LRU and MRU
- Buffer capacities: 2, 4, 8, and 16 pages

## Files

- `pf_experiment_results.csv`: measurements for all eight configurations.
- `capacity_vs_physical_reads.png`: physical reads against buffer capacity.
- `capacity_vs_hit_ratio.png`: buffer hit ratio against buffer capacity.
- `lru_vs_mru_comparison.png`: LRU/MRU physical-read comparison.
- `pf_experiment_analysis.txt`: observations and conclusions.

## Key observations

- At capacity 2, MRU records 2 hits while LRU records 0.
- At capacity 4, MRU records 6 hits while LRU records 4.
- At capacities 8 and 16, both policies record 7 hits and 5 physical reads.
- Physical writes are zero because the measured workload only reads pages.

These conclusions apply to this specific access sequence and should not be
interpreted as a general claim that MRU is better than LRU.
