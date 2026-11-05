#!/usr/bin/env python3
import csv
import os
import sys
import matplotlib.pyplot as plt

if len(sys.argv) != 4:
    raise SystemExit("Usage: generate_am_graphs.py BUILD_CSV QUERY_CSV OUTPUT_DIR")

build_csv, query_csv, output_dir = sys.argv[1:]
os.makedirs(output_dir, exist_ok=True)
with open(build_csv, newline="") as f:
    build = list(csv.DictReader(f))
with open(query_csv, newline="") as f:
    query = list(csv.DictReader(f))
if not build or not query:
    raise SystemExit("Benchmark CSV files are empty.")

methods = [r["method"] for r in build]
plt.figure()
plt.bar(methods, [float(r["build_seconds"]) for r in build])
plt.ylabel("Wall-clock time (seconds)")
plt.xlabel("Construction method")
plt.title("B+ Tree Index Construction Time")
plt.tight_layout()
plt.savefig(os.path.join(output_dir, "construction_time.png"), dpi=180)
plt.close()

from statistics import median

methods = [r["method"] for r in build]
summary = {}
for method in methods:
    rows = [r for r in query if r["method"] == method]
    summary[method] = {
        "query_seconds": median(float(r["query_seconds"]) for r in rows),
        "logical_page_accesses": median(int(r["logical_page_accesses"]) for r in rows),
        "physical_page_reads": median(int(r["physical_page_reads"]) for r in rows),
    }

plt.figure()
plt.bar(methods, [summary[m]["query_seconds"] for m in methods])
plt.ylabel("CPU time (seconds)")
plt.xlabel("Construction method")
plt.title("Single Duplicate-Key Search Time")
plt.tight_layout()
plt.savefig(os.path.join(output_dir, "query_time.png"), dpi=180)
plt.close()

x = list(range(len(methods)))
width = 0.36
plt.figure()
plt.bar([i-width/2 for i in x],
        [summary[m]["logical_page_accesses"] for m in methods],
        width, label="Logical page accesses")
plt.bar([i+width/2 for i in x],
        [summary[m]["physical_page_reads"] for m in methods],
        width, label="Physical page reads")
plt.xticks(x, methods)
plt.ylabel("Page operations")
plt.xlabel("Construction method")
plt.title("Page Accesses for One Duplicate-Key Search")
plt.legend()
plt.tight_layout()
plt.savefig(os.path.join(output_dir, "page_accesses.png"), dpi=180)
plt.close()
print("Generated construction_time.png, query_time.png, page_accesses.png")
