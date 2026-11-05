#!/usr/bin/env python3
"""
AM-layer test and B+ tree benchmark runner.

Place in:
    toydb/experiments/storage/run_am_experiments.py

Run from the repository root:
    python3 toydb/experiments/storage/run_am_experiments.py

Skip a complete, validated result set:
    python3 toydb/experiments/storage/run_am_experiments.py

Force a fresh run and replace previous AM results:
    python3 toydb/experiments/storage/run_am_experiments.py --force

All generated AM outputs are saved under:
    toydb/experiments/storage/am_layer/

The build and tests run in a temporary project copy. The original source tree
and compiled binaries are not used as the build directory.
"""

import argparse
import csv
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path
from statistics import median


LEGACY_TESTS = ["test1", "test2", "test3"]
PROJECT_DIRS = [
    Path("toydb/amlayer"),
    Path("toydb/pflayer"),
    Path("toydb/slottedpage"),
    Path("data"),
]

BENCHMARK_FILES = [
    "build_time.csv",
    "query_metrics.csv",
]

EXPECTED_GRAPHS = [
    "construction_time.png",
    "query_time.png",
    "page_accesses.png",
]


def find_project_root(script_path):
    for parent in script_path.parents:
        if (
            (parent / "toydb" / "amlayer" / "makefile").is_file()
            and (parent / "toydb" / "pflayer" / "Makefile").is_file()
            and (parent / "data" / "student.txt").is_file()
        ):
            return parent
    raise RuntimeError(
        "Cannot locate project root. Expected toydb/amlayer/makefile, "
        "toydb/pflayer/Makefile and data/student.txt."
    )


def run(command, cwd, description):
    """Run a command without creating persistent log files."""
    result = subprocess.run(
        command,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
    )
    print(f"{'PASS' if result.returncode == 0 else 'FAIL'}: {description}")
    if result.returncode != 0:
        print(result.stdout)
        raise RuntimeError(
            f"{description} failed with exit code {result.returncode}"
        )
    return result.stdout


def copy_project_subset(project_root, temp_root):
    for relative in PROJECT_DIRS:
        source = project_root / relative
        if not source.exists():
            raise RuntimeError(f"Required project path is missing: {source}")
        destination = temp_root / relative
        shutil.copytree(
            source,
            destination,
            ignore=shutil.ignore_patterns(
                ".DS_Store", "__MACOSX", "__pycache__", "*.o", "*.pyc",
                "a.out", "testhash", "testpf", "testbuf", "teststats",
                "testlru", "testexperiment", "testworkload",
                "benchmark_am_queries",
            ),
        )


def read_csv(path):
    with path.open(newline="", encoding="utf-8") as file:
        return list(csv.DictReader(file))


def validate_results(output_dir, require_generated=True):
    required = [
        output_dir / "build_time.csv",
        output_dir / "query_metrics.csv",
    ]
    if require_generated:
        required += [
            output_dir / "am_benchmark_results.xlsx",
            output_dir / "am_layer_notes.md",
        ]
    required += [output_dir / name for name in BENCHMARK_FILES]
    required += [output_dir / "graphs" / name for name in EXPECTED_GRAPHS]

    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise RuntimeError("Missing AM result files: " + ", ".join(missing))

    build_rows = read_csv(output_dir / "build_time.csv")
    query_rows = read_csv(output_dir / "query_metrics.csv")

    expected_methods = {"one_shot", "incremental", "bulk_load"}
    if len(build_rows) != 3 or {r["method"] for r in build_rows} != expected_methods:
        raise RuntimeError("build_time.csv does not contain all three methods.")

    if len(query_rows) != 90:
        raise RuntimeError(
            f"Expected 90 query measurements, found {len(query_rows)}."
        )

    for method in sorted(expected_methods):
        rows = [r for r in query_rows if r["method"] == method]
        if len(rows) != 30:
            raise RuntimeError(f"Expected 30 query measurements for {method}.")
        if any(int(r["matches"]) != 2 for r in rows):
            raise RuntimeError(
                f"Expected every duplicate-key query for {method} to return 2 matches."
            )
        if any(int(r["logical_page_accesses"]) <= 0 for r in rows):
            raise RuntimeError(f"Invalid page-access count for {method}.")

    return build_rows, query_rows


def create_excel(output_dir, build_rows, query_rows):
    try:
        from openpyxl import Workbook
        from openpyxl.styles import Font, PatternFill, Alignment
        from openpyxl.utils import get_column_letter
    except ImportError as exc:
        raise RuntimeError(
            "Excel output requires openpyxl. Install with: "
            "python3 -m pip install openpyxl"
        ) from exc

    workbook = Workbook()
    sheet = workbook.active
    sheet.title = "Build Times"
    sheet.append(["Method", "Build time (seconds)"])
    for row in build_rows:
        sheet.append([row["method"], float(row["build_seconds"])])

    raw = workbook.create_sheet("Query Raw")
    raw.append([
        "Method", "Query time (seconds)", "Matches",
        "Logical page accesses", "Physical page reads",
        "Logical writes", "Physical writes", "Buffer hits",
    ])
    for row in query_rows:
        raw.append([
            row["method"], float(row["query_seconds"]), int(row["matches"]),
            int(row["logical_page_accesses"]), int(row["physical_page_reads"]),
            int(row["logical_writes"]), int(row["physical_writes"]),
            int(row["buffer_hits"]),
        ])

    summary = workbook.create_sheet("Query Summary")
    summary.append([
        "Method", "Runs", "Median query time (seconds)",
        "Median logical page accesses", "Median physical page reads",
        "Matches per query",
    ])
    for method in ["one_shot", "incremental", "bulk_load"]:
        rows = [r for r in query_rows if r["method"] == method]
        summary.append([
            method,
            len(rows),
            median(float(r["query_seconds"]) for r in rows),
            median(int(r["logical_page_accesses"]) for r in rows),
            median(int(r["physical_page_reads"]) for r in rows),
            int(rows[0]["matches"]),
        ])

    for ws in workbook.worksheets:
        ws.freeze_panes = "A2"
        ws.auto_filter.ref = ws.dimensions
        for cell in ws[1]:
            cell.font = Font(bold=True, color="FFFFFF")
            cell.fill = PatternFill("solid", fgColor="365F91")
            cell.alignment = Alignment(horizontal="center")
        for column in ws.columns:
            width = max(len(str(cell.value or "")) for cell in column) + 2
            ws.column_dimensions[get_column_letter(column[0].column)].width = min(
                max(width, 14), 38
            )

    workbook.save(output_dir / "am_benchmark_results.xlsx")


def create_notes(output_dir, build_rows, query_rows):
    lines = [
        "# AM Layer Experiment Notes",
        "",
        f"Generated: {datetime.now().astimezone().isoformat(timespec='seconds')}",
        "",
        "## Automated workflow",
        "",
        "- Legacy AM correctness tests: test1, test2 and test3.",
        "- B+ tree construction using one-shot insertion.",
        "- B+ tree construction using incremental insertion.",
        "- B+ tree construction using sorted-file bulk loading.",
        "- 30 duplicate-key query measurements per construction method.",
        "",
        "## Build times",
        "",
        "| Method | End-to-end build time (seconds) |",
        "|---|---:|",
    ]
    for row in build_rows:
        lines.append(
            f"| {row['method']} | {float(row['build_seconds']):.6f} |"
        )

    lines += [
        "",
        "## Query summary",
        "",
        "| Method | Runs | Median query time (seconds) | "
        "Median logical page accesses | Median physical page reads |",
        "|---|---:|---:|---:|---:|",
    ]
    for method in ["one_shot", "incremental", "bulk_load"]:
        rows = [r for r in query_rows if r["method"] == method]
        lines.append(
            f"| {method} | {len(rows)} | "
            f"{median(float(r['query_seconds']) for r in rows):.9f} | "
            f"{median(int(r['logical_page_accesses']) for r in rows)} | "
            f"{median(int(r['physical_page_reads']) for r in rows)} |"
        )

    lines += [
        "",
        "## Methodology and limitations",
        "",
        "- Build time is the end-to-end time reported by the existing C test "
        "program. It includes Student record-storage preparation and index "
        "construction; it is not an isolated B+ tree-only measurement.",
        "- The common query targets duplicate roll number 97305818 and is "
        "expected to return two matches.",
        "- Query timing is measured 30 times per method. Medians reduce the "
        "effect of individual timing outliers; very small differences should "
        "not be over-interpreted.",
        "- Raw query measurements are retained in query_metrics.csv.",
        "- Page counters come from the PF buffer statistics.",
        "- Builds and tests run in a temporary project copy. Only generated "
        "result artifacts are copied into this output folder.",
        "",
        "See AM_BENCHMARK_README.md in toydb/amlayer for the original benchmark "
        "methodology. Test and benchmark output is shown in the terminal; no log files are saved.",
    ]
    (output_dir / "am_layer_notes.md").write_text(
        "\n".join(lines) + "\n", encoding="utf-8"
    )


def results_are_complete(output_dir):
    try:
        validate_results(output_dir)
        return True
    except Exception:
        return False


def main():
    parser = argparse.ArgumentParser(
        description="Run AM correctness tests and B+ tree benchmarks."
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Rerun all tests/benchmarks and replace existing AM results.",
    )
    args = parser.parse_args()

    script_path = Path(__file__).resolve()
    project_root = find_project_root(script_path)
    storage_dir = script_path.parent
    output_dir = storage_dir / "am_layer"
    output_dir.mkdir(parents=True, exist_ok=True)

    if not args.force and results_are_complete(output_dir):
        # Remove log files left by earlier runner versions.
        for old_log in output_dir.rglob("*.log"):
            old_log.unlink()
        print("Existing AM results are complete and passed validation.")
        print("Skipping tests and benchmarks; no log files are retained.")
        print(f"Results: {output_dir}")
        print("Use --force to regenerate every result.")
        return

    if not args.force and any(output_dir.iterdir()):
        print("Existing AM results are incomplete or invalid.")
        print("A fresh run will regenerate the AM result set.")

    print(f"Project: {project_root}")
    print(f"AM output: {output_dir}")
    print("Building and testing in a temporary project copy.\n")

    with tempfile.TemporaryDirectory(prefix="toydb_am_experiments_") as temp_name:
        temp_root = Path(temp_name)
        copy_project_subset(project_root, temp_root)

        temp_am = temp_root / "toydb" / "amlayer"
        temp_pf = temp_root / "toydb" / "pflayer"
        run(
            ["make", "-C", str(temp_pf), "-f", "Makefile", "all"],
            temp_root,
            "Build PF dependency",
        )
        run(
            ["make", "-f", "makefile", "test1", "test2", "test3"],
            temp_am,
            "Build legacy AM tests",
        )

        for test in LEGACY_TESTS:
            run(
                [f"./{test}"],
                temp_am,
                f"AM correctness test {test}",
            )

        run(
            ["bash", "run_am_benchmarks.sh"],
            temp_am,
            "B+ tree construction and query benchmarks",
        )

        temp_results = temp_am / "benchmark_results"
        for filename in BENCHMARK_FILES:
            source = temp_results / filename
            if not source.is_file():
                raise RuntimeError(f"Benchmark did not produce {filename}.")

        # Replace only the AM output folder, never PF or Slotted Page outputs.
        if output_dir.exists():
            shutil.rmtree(output_dir)
        output_dir.mkdir(parents=True)

        # The benchmark shell script may create logs in its temporary directory.
        # Copy only CSVs and graphs; no log files are retained in the results.
        for filename in BENCHMARK_FILES:
            shutil.copy2(temp_results / filename, output_dir / filename)

        shutil.copytree(
            temp_results / "graphs",
            output_dir / "graphs",
        )

    # Validate benchmark inputs before generating the derived workbook/notes.
    build_rows, query_rows = validate_results(output_dir, require_generated=False)
    create_excel(output_dir, build_rows, query_rows)
    create_notes(output_dir, build_rows, query_rows)
    # Confirm the complete output set after generating derived files.
    validate_results(output_dir, require_generated=True)

    print("\n========================================")
    print(" ALL AM TESTS AND BENCHMARKS PASSED")
    print("========================================")
    print("Correctness tests: 3/3 passed")
    print("Construction methods: 3 completed")
    print("Query measurements: 90 (30 per method)")
    print("Generated CSV, Excel, graphs and notes. No log files were generated.")
    print(f"Results: {output_dir}")
    print("Run again to skip; use --force to regenerate.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nInterrupted by user.", file=sys.stderr)
        sys.exit(130)
    except Exception as error:
        print(f"\nERROR: {error}", file=sys.stderr)
        sys.exit(1)
