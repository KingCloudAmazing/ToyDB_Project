#!/usr/bin/env python3
"""
Run the PF-layer correctness tests and performance experiments.

The script builds and runs a temporary copy of the PF sources. It does not
overwrite tracked test binaries, object files, or temporary database files in
the repository.

Place this script in <repo>/toydb/experiments/storage/ and run from any directory:
    python3 toydb/experiments/storage/run_pf_experiments.py

All generated results are saved beside this script.
"""

import argparse
import csv
import re
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path


TESTS = ["testhash", "testpf", "testbuf", "teststats", "testlru"]
CAPACITIES = [2, 4, 8, 16]
POLICIES = [(0, "LRU"), (1, "MRU")]

CAPACITY_PATTERN = re.compile(
    r"^(LRU|MRU)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([\d.]+)%$",
    re.MULTILINE,
)

CAPACITY_FIELDS = [
    "Policy", "Capacity", "LogicalReads", "PhysicalReads",
    "BufferHits", "PhysicalWrites", "HitRatioPercent",
]


def run_command(command, cwd, description=None):
    """Run a command and keep its output in memory; do not create log files."""
    result = subprocess.run(
        command,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
    )
    label = description or " ".join(command)
    print(f"{'PASS' if result.returncode == 0 else 'FAIL'}: {label}")
    if result.returncode != 0:
        print(result.stdout)
    return result


def write_csv(path, fieldnames, rows):
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def validate_capacity_rows(rows):
    if len(rows) != 8:
        raise RuntimeError(f"Expected 8 capacity results, found {len(rows)}")

    for row in rows:
        logical = int(row["LogicalReads"])
        physical = int(row["PhysicalReads"])
        hits = int(row["BufferHits"])
        if logical != 12:
            raise RuntimeError("Capacity experiment should perform 12 logical reads")
        if physical + hits != logical:
            raise RuntimeError(
                f"Inconsistent read counts for {row['Policy']} capacity "
                f"{row['Capacity']}: physical reads + hits != logical reads"
            )


def validate_workload_csv(path):
    with path.open(newline="", encoding="utf-8") as file:
        rows = list(csv.DictReader(file))

    if len(rows) != 10:
        raise RuntimeError(f"Expected 10 workload rows, found {len(rows)}")

    for row in rows:
        read_ops = int(row["read_operations"])
        write_ops = int(row["write_operations"])
        if read_ops + write_ops != 20:
            raise RuntimeError(f"Invalid operation count in workload {row['workload']}")
        if int(row["logical_reads"]) != 20:
            raise RuntimeError(
                "PF_GetThisPage is called for every operation, so logical_reads "
                "should be 20 for each workload"
            )
        if int(row["logical_writes"]) != write_ops:
            raise RuntimeError(f"Logical write count mismatch in {row['workload']}")

    return rows


def generate_graphs(output_dir, capacity_rows):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise RuntimeError(
            "Matplotlib is required to generate graphs. Install it with: "
            "python3 -m pip install matplotlib"
        ) from exc

    policies = ["LRU", "MRU"]
    by_policy = {
        policy: sorted(
            [row for row in capacity_rows if row["Policy"] == policy],
            key=lambda row: int(row["Capacity"]),
        )
        for policy in policies
    }

    capacities = [int(row["Capacity"]) for row in by_policy["LRU"]]

    # Graph 1: capacity vs physical reads.
    fig, ax = plt.subplots()
    for policy in policies:
        ax.plot(
            capacities,
            [int(row["PhysicalReads"]) for row in by_policy[policy]],
            marker="o",
            label=policy,
        )
    ax.set_title("Buffer Capacity vs Physical Reads")
    ax.set_xlabel("Buffer capacity (pages)")
    ax.set_ylabel("Physical reads")
    ax.set_xticks(capacities)
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(output_dir / "capacity_vs_physical_reads.png", dpi=160)
    plt.close(fig)

    # Graph 2: capacity vs hit ratio.
    fig, ax = plt.subplots()
    for policy in policies:
        ax.plot(
            capacities,
            [float(row["HitRatioPercent"]) for row in by_policy[policy]],
            marker="o",
            label=policy,
        )
    ax.set_title("Buffer Capacity vs Hit Ratio")
    ax.set_xlabel("Buffer capacity (pages)")
    ax.set_ylabel("Buffer hit ratio (%)")
    ax.set_xticks(capacities)
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(output_dir / "capacity_vs_hit_ratio.png", dpi=160)
    plt.close(fig)

    # Graph 3: direct LRU/MRU physical-read comparison.
    fig, ax = plt.subplots()
    width = 0.35
    positions = list(range(len(capacities)))
    ax.bar(
        [position - width / 2 for position in positions],
        [int(row["PhysicalReads"]) for row in by_policy["LRU"]],
        width,
        label="LRU",
    )
    ax.bar(
        [position + width / 2 for position in positions],
        [int(row["PhysicalReads"]) for row in by_policy["MRU"]],
        width,
        label="MRU",
    )
    ax.set_title("LRU vs MRU: Physical Reads")
    ax.set_xlabel("Buffer capacity (pages)")
    ax.set_ylabel("Physical reads")
    ax.set_xticks(positions)
    ax.set_xticklabels([str(value) for value in capacities])
    ax.grid(True, axis="y", alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(output_dir / "lru_vs_mru_comparison.png", dpi=160)
    plt.close(fig)


def generate_excel(output_dir, test_summary, capacity_rows, workload_rows):
    """Create a workbook containing correctness and experiment results."""
    try:
        from openpyxl import Workbook
        from openpyxl.styles import Font, PatternFill, Alignment
        from openpyxl.utils import get_column_letter
    except ImportError as exc:
        raise RuntimeError(
            "openpyxl is required for Excel output. Install it with: "
            "python3 -m pip install openpyxl"
        ) from exc

    workbook = Workbook()
    overview = workbook.active
    overview.title = "Overview"
    overview.append(["PF Layer Experiment Results"])
    overview.append(["Correctness tests passed", sum(r["status"] == "PASS" for r in test_summary)])
    overview.append(["Correctness tests total", len(test_summary)])
    overview.append(["Capacity configurations", len(capacity_rows)])
    overview.append(["Read/write workloads", len(workload_rows)])
    overview.append([])
    overview.append(["Generated outputs", ""])
    overview.append(["Correctness summary", "pf_test_summary.csv"])
    overview.append(["Capacity results", "pf_experiment_results.csv"])
    overview.append(["Workload results", "pf_workload_results.csv"])
    overview.append(["Graphs", "PNG files in this folder"])

    sheets = [
        ("Correctness Tests", test_summary, ["test", "status", "exit_code"]),
        ("Capacity Results", capacity_rows, CAPACITY_FIELDS),
        ("Workload Results", workload_rows, list(workload_rows[0].keys()) if workload_rows else []),
    ]

    for title, rows, fields in sheets:
        ws = workbook.create_sheet(title)
        if fields:
            ws.append(fields)
            for row in rows:
                ws.append([row.get(field, "") for field in fields])

    for ws in workbook.worksheets:
        ws.freeze_panes = "A2"
        ws.auto_filter.ref = ws.dimensions if ws.max_row > 1 else "A1"
        for cell in ws[1]:
            cell.font = Font(bold=True, color="FFFFFF")
            cell.fill = PatternFill("solid", fgColor="244062")
            cell.alignment = Alignment(horizontal="center")
        for column in ws.columns:
            max_length = max(
                (len(str(cell.value)) if cell.value is not None else 0)
                for cell in column
            )
            ws.column_dimensions[get_column_letter(column[0].column)].width = min(max(max_length + 2, 12), 42)

    workbook.save(output_dir / "pf_experiment_results.xlsx")


def generate_analysis(output_dir, capacity_rows, workload_rows):
    lines = [
        "PF BUFFER MANAGER EXPERIMENT ANALYSIS",
        f"Generated: {datetime.now().astimezone().isoformat(timespec='seconds')}",
        "",
        "1. CAPACITY AND REPLACEMENT-POLICY EXPERIMENT",
        "Page-access sequence: 0, 1, 2, 3, 0, 1, 4, 0, 1, 2, 3, 4",
        "Policies: LRU and MRU",
        "Capacities: 2, 4, 8 and 16 pages",
        "",
        "Results:",
    ]
    for row in capacity_rows:
        lines.append(
            f"- {row['Policy']}, capacity {row['Capacity']}: "
            f"{row['PhysicalReads']} physical reads, "
            f"{row['BufferHits']} hits, "
            f"{row['HitRatioPercent']}% hit ratio."
        )

    lines.extend([
        "",
        "Observations:",
        "- Increasing capacity reduces physical reads for this access sequence "
        "until the five distinct pages can be retained.",
        "- LRU and MRU results depend on the access sequence; these measurements "
        "do not establish that one policy is generally better.",
        "- Physical writes are zero in this experiment because it only reads pages.",
        "",
        "2. READ/WRITE WORKLOAD EXPERIMENT",
        "Each workload performs 20 page operations over five pages with a "
        "three-page buffer.",
        "",
        "Results:",
    ])
    for row in workload_rows:
        lines.append(
            f"- {row['workload']} / {row['policy']}: "
            f"{row['read_operations']} read operations, "
            f"{row['write_operations']} write operations; "
            f"{row['physical_reads']} physical reads, "
            f"{row['physical_writes']} physical writes, "
            f"{row['buffer_hits']} hits, "
            f"{row['hit_ratio']}% hit ratio."
        )

    lines.extend([
        "",
        "Measurement note:",
        "- logical_reads counts calls to PF_GetThisPage. The workload calls "
        "PF_GetThisPage once for every operation, including operations that "
        "subsequently modify the page.",
        "- read_operations and write_operations describe the intended workload "
        "mix; they are not the same as logical_reads and logical_writes.",
        "- logical_writes counts dirty unfix operations.",
        "- Workload physical-write statistics are collected after closing the "
        "file, so dirty-page flushes on close are included.",
        "",
        "These results describe only the tested workloads and configuration.",
    ])
    (output_dir / "pf_experiment_analysis.txt").write_text(
        "\n".join(lines) + "\n", encoding="utf-8"
    )


def generate_readme(output_dir):
    content = """# PF Layer Automated Tests and Experiments

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
- `pf_experiment_analysis.txt`

The workload's `logical_reads` column counts `PF_GetThisPage` calls. Since the
test calls it for every operation, that value is 20 for every workload. The
separate `read_operations` and `write_operations` columns describe the mix of
read-only and modifying operations.
"""
    (output_dir / "pf_layer_notes.md").write_text(content, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description="Run PF correctness tests and experiments.")
    parser.add_argument(
        "--output-dir",
        default=None,
        help="Output directory (default: beside this script).",
    )
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent

    # Expected location: <repository>/toydb/experiments/storage/
    # PF C sources and Makefile live in <repository>/toydb/pflayer/.
    if (
        script_dir.name == "storage"
        and script_dir.parent.name == "experiments"
        and script_dir.parent.parent.name == "toydb"
    ):
        repository_root = script_dir.parent.parent.parent
        source_dir = repository_root / "toydb" / "pflayer"
        default_output_dir = script_dir / "pf_layer"
    else:
        # Also allow running the script directly from the PF layer directory.
        source_dir = script_dir
        default_output_dir = script_dir / "experiments" / "storage"

    makefile = source_dir / "Makefile"
    if not makefile.exists():
        raise SystemExit(f"ERROR: Makefile not found in {source_dir}")

    output_dir = (
        Path(args.output_dir).expanduser().resolve()
        if args.output_dir
        else default_output_dir
    )
    output_dir.mkdir(parents=True, exist_ok=True)
    # Remove logs left by earlier versions of this runner.
    old_logs = output_dir / "logs"
    if old_logs.exists():
        shutil.rmtree(old_logs)

    source_files = (
        list(source_dir.glob("*.c"))
        + list(source_dir.glob("*.h"))
        + [makefile]
    )
    if not source_files:
        raise SystemExit("ERROR: No PF source files found.")

    print(f"PF source directory: {source_dir}")
    print(f"Results directory:   {output_dir}")
    print("Building in a temporary directory; repository binaries will not be changed.\n")

    test_summary = []
    capacity_rows = []

    with tempfile.TemporaryDirectory(prefix="pf_layer_experiments_") as temp_name:
        temp_dir = Path(temp_name)
        for source in source_files:
            shutil.copy2(source, temp_dir / source.name)
        (temp_dir / "pf_experiments").mkdir(exist_ok=True)

        build = run_command(
            ["make", "tests"],
            temp_dir,
            "Build PF tests",
        )
        if build.returncode != 0:
            raise SystemExit("PF test build failed; see the error output above.")

        print("\n===== PF CORRECTNESS TESTS =====")
        for test_name in TESTS:
            result = run_command(
                [f"./{test_name}"],
                temp_dir,
                f"PF correctness test {test_name}",
            )
            test_summary.append({
                "test": test_name,
                "status": "PASS" if result.returncode == 0 else "FAIL",
                "exit_code": result.returncode,
            })

        write_csv(
            output_dir / "pf_test_summary.csv",
            ["test", "status", "exit_code"],
            test_summary,
        )

        failed_tests = [row["test"] for row in test_summary if row["status"] != "PASS"]
        if failed_tests:
            raise SystemExit(
                "Correctness test(s) failed: " + ", ".join(failed_tests)
                + ". Performance experiments were not run."
            )

        print("\n===== LRU/MRU CAPACITY EXPERIMENTS =====")
        for policy_number, policy_name in POLICIES:
            for capacity in CAPACITIES:
                result = run_command(
                    ["./testexperiment", str(capacity), str(policy_number)],
                    temp_dir,
                    f"{policy_name} capacity {capacity}",
                )
                if result.returncode != 0:
                    raise SystemExit("Capacity experiment failed; see error output above.")

                match = CAPACITY_PATTERN.search(result.stdout)
                if not match:
                    raise SystemExit("Could not parse capacity result; inspect the terminal output above.")

                values = match.groups()
                capacity_rows.append({
                    "Policy": values[0],
                    "Capacity": int(values[1]),
                    "LogicalReads": int(values[2]),
                    "PhysicalReads": int(values[3]),
                    "BufferHits": int(values[4]),
                    "PhysicalWrites": int(values[5]),
                    "HitRatioPercent": float(values[6]),
                })

        validate_capacity_rows(capacity_rows)
        write_csv(
            output_dir / "pf_experiment_results.csv",
            CAPACITY_FIELDS,
            capacity_rows,
        )

        print("\n===== READ/WRITE WORKLOAD EXPERIMENTS =====")
        workload_result = run_command(
            ["./testworkload"],
            temp_dir,
            "Read/write workload experiment",
        )
        if workload_result.returncode != 0:
            raise SystemExit("Workload experiment failed; see error output above.")

        generated_workload_csv = temp_dir / "pf_experiments" / "pf_workload_results.csv"
        if not generated_workload_csv.exists():
            raise SystemExit("testworkload did not generate its expected CSV file.")

        workload_rows = validate_workload_csv(generated_workload_csv)
        shutil.copy2(
            generated_workload_csv,
            output_dir / "pf_workload_results.csv",
        )

    generate_graphs(output_dir, capacity_rows)
    generate_analysis(output_dir, capacity_rows, workload_rows)
    generate_excel(output_dir, test_summary, capacity_rows, workload_rows)
    generate_readme(output_dir)

    print("\n========================================")
    print(" ALL PF TESTS AND EXPERIMENTS PASSED")
    print("========================================")
    print(f"Correctness tests: {len(test_summary)}/{len(TESTS)} passed")
    print(f"Capacity results:  {len(capacity_rows)} configurations")
    print(f"Workload results:  {len(workload_rows)} configurations")
    print(f"Generated files:   {output_dir}")
    print("Excel workbook:    pf_experiment_results.xlsx")
    


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nInterrupted by user.", file=sys.stderr)
        sys.exit(130)
    except Exception as error:
        print(f"\nERROR: {error}", file=sys.stderr)
        sys.exit(1)
