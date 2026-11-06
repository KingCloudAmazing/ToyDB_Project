#!/usr/bin/env python3

"""
Reproducible ToyDB Storage Comparison Experiment

Place this script at:

    toydb/experiments/storage/generate_storage_comparison.py

Run from the repository root:

    python3 toydb/experiments/storage/generate_storage_comparison.py

The script automatically:

1. Locates the Student dataset.
2. Builds and runs the complete Slotted Page Makefile test suite in a temporary copy.
3. Runs the in-memory slotted-page dataset test.
4. Runs PF-backed slotted-page tests.
5. Runs the static-page tests.
6. Runs all five static PF configurations.
7. Saves complete test output.
8. Extracts actual measurements from the C test output.
9. Validates record counts and storage accounting.
10. Generates a CSV, two graphs and a Markdown summary in slotted_page/.
11. Saves outputs in experiments/storage/slotted_page/.
12. Skips validated existing results unless --force is supplied.

No page counts or storage measurements are hardcoded.
"""

import argparse
import csv
import math
import re
import subprocess
import sys
import shutil
import tempfile
from pathlib import Path

# ------------------------------------------------------------
# Configuration
# ------------------------------------------------------------

PAGE_SIZE = 4096

SLOTTED_HEADER_BYTES = 12
SLOTTED_SLOT_BYTES = 4

PF_PAGE_LINK_BYTES = 4
PF_FILE_HEADER_BYTES = 8

EXPECTED_RECORDS = 17813
EXPECTED_PAYLOAD_BYTES = 1759011

STATIC_CONFIGURATIONS = [108, 112, 128, 160, 256]

SCRIPT_DIR = Path(__file__).resolve().parent
TOYDB_DIR = SCRIPT_DIR.parent.parent
REPO_ROOT = TOYDB_DIR.parent

SLOTTEDPAGE_DIR = TOYDB_DIR / "slottedpage"

DEFAULT_STUDENT_FILE = REPO_ROOT / "data" / "student.txt"

OUTPUT_DIR = SCRIPT_DIR / "slotted_page"
RESULTS_LOG = OUTPUT_DIR / "storage_test_results.txt"

OUTPUT_FILES = [
    "storage_test_results.txt",
    "storage_comparison.csv",
    "storage_pages_comparison.png",
    "storage_payload_utilization.png",
    "storage_comparison_notes.md",
]


# ------------------------------------------------------------
# Utility functions
# ------------------------------------------------------------

def fail(message):
    print(f"\nERROR: {message}", file=sys.stderr)
    sys.exit(1)


def read_metric(block, label):
    """
    Extract an integer measurement from test output.

    Example:
        PF pages allocated : 482
    """

    pattern = (
        rf"^\s*{re.escape(label)}\s*:\s*([\d,]+)"
    )

    match = re.search(pattern, block, re.MULTILINE)

    if not match:
        fail(f"Could not extract '{label}' from test output.")

    return int(match.group(1).replace(",", ""))


# ------------------------------------------------------------
# Read and validate Student dataset
# ------------------------------------------------------------

def read_dataset_stats(student_file):

    if not student_file.exists():
        fail(f"Student dataset not found: {student_file}")

    lines = student_file.read_bytes().splitlines()

    # Remove empty lines.
    lines = [line for line in lines if line.strip()]

    if len(lines) < 2:
        fail("Student dataset is empty or missing its header.")

    # First line is the dataset header.
    records = lines[1:]

    record_count = len(records)

    payload_bytes = sum(len(record) for record in records)

    # Each Student record should contain 16 semicolon-separated fields.
    malformed = [
        i + 1
        for i, record in enumerate(records)
        if record.count(b";") != 15
    ]

    if malformed:
        fail(
            "Malformed Student records detected. "
            f"First invalid rows: {malformed[:5]}"
        )

    if record_count != EXPECTED_RECORDS:
        fail(
            f"Expected {EXPECTED_RECORDS} records, "
            f"found {record_count}."
        )

    if payload_bytes != EXPECTED_PAYLOAD_BYTES:
        fail(
            f"Expected {EXPECTED_PAYLOAD_BYTES} payload bytes, "
            f"found {payload_bytes}."
        )

    return {
        "record_count": record_count,
        "payload_bytes": payload_bytes
    }


# ------------------------------------------------------------
# Execute storage tests
# ------------------------------------------------------------

def run_storage_tests(student_file):
    """
    Build and run Slotted Page tests in a temporary mirror of toydb/.

    This avoids deleting or overwriting executables and generated database
    files in the user's actual toydb/slottedpage and toydb/pflayer folders.
    """
    if not SLOTTEDPAGE_DIR.exists():
        fail(f"Cannot locate: {SLOTTEDPAGE_DIR}")

    pf_source_dir = TOYDB_DIR / "pflayer"
    if not pf_source_dir.exists():
        fail(f"Cannot locate PF dependency: {pf_source_dir}")

    print("\n========================================")
    print("TOYDB STORAGE EXPERIMENT")
    print("========================================")
    print(f"Student dataset: {student_file}")
    print("Building and running tests in a temporary copy.")

    complete_output = []

    with tempfile.TemporaryDirectory(prefix="toydb_storage_experiments_") as temp_name:
        temp_toydb = Path(temp_name) / "toydb"
        temp_slotted_dir = temp_toydb / "slottedpage"
        temp_pf_dir = temp_toydb / "pflayer"

        ignore = shutil.ignore_patterns(
            ".DS_Store", "__MACOSX", "__pycache__", "*.o", "*.pyc",
            "a.out", "test_slotted", "test_student_dataset",
            "test_slotted_phase2", "test_pf_integration", "test_pf_phase3",
            "test_student_pf", "test_student_scan",
            "test_student_scan_dataset", "test_static_page", "test_static_pf",
        )
        shutil.copytree(SLOTTEDPAGE_DIR, temp_slotted_dir, ignore=ignore)
        shutil.copytree(pf_source_dir, temp_pf_dir, ignore=ignore)

        commands = [
            ["make", "clean"],
            ["make", f"STUDENT_FILE={student_file}", "test"],
        ]

        for command in commands:
            print("\nExecuting:", " ".join(command))
            result = subprocess.run(
                command,
                cwd=temp_slotted_dir,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                errors="replace",
            )

            complete_output.append("$ " + " ".join(command) + "\n")
            complete_output.append(result.stdout)
            print(result.stdout)

            if result.returncode != 0:
                RESULTS_LOG.write_text(
                    "\n".join(complete_output),
                    encoding="utf-8",
                )
                fail(
                    f"Command failed with exit code {result.returncode}. "
                    f"See {RESULTS_LOG}"
                )

    RESULTS_LOG.write_text("\n".join(complete_output), encoding="utf-8")
    return "\n".join(complete_output)


# ------------------------------------------------------------
# Extract actual slotted-page results
# ------------------------------------------------------------

def extract_slotted_results(output, dataset_stats):
    """Extract both slotted page counts and use the PF-backed count.

    The in-memory test is retained as an independent cross-check. The page
    count placed in the comparison table and graphs comes from traversing the
    actual PF-backed Student file in test_student_pf.
    """
    memory_marker = (
        "PASS: Student dataset in-memory "
        "slotted-page round trip"
    )
    pf_marker = (
        "PASS: PF-backed Student dataset "
        "insert/close/reopen/retrieve"
    )

    def block_after(marker, description):
        marker_pos = output.find(marker)
        if marker_pos < 0:
            fail(f"Could not find successful {description} test output.")

        next_pass = output.find("\nPASS:", marker_pos + len(marker))
        next_static = output.find("\n===== Static storage:", marker_pos + len(marker))
        candidates = [p for p in (next_pass, next_static) if p >= 0]
        block_end = min(candidates) if candidates else len(output)
        return output[marker_pos:block_end]

    # In-memory page count: used only as a cross-check.
    memory_block = block_after(memory_marker, "in-memory slotted-page dataset")
    memory_records = read_metric(memory_block, "Student records verified")
    memory_pages = read_metric(memory_block, "Slotted pages used")

    average_match = re.search(
        r"^\s*Average records per page\s*:\s*([0-9]+(?:\.[0-9]+)?)",
        memory_block,
        flags=re.MULTILINE,
    )
    if not average_match:
        fail("Could not extract average records per page from in-memory test.")
    memory_average = float(average_match.group(1))

    if memory_records != dataset_stats["record_count"]:
        fail("In-memory slotted-page test did not verify every Student record.")
    if memory_pages <= 0:
        fail("In-memory slotted-page test reported an invalid page count.")
    calculated_memory_average = memory_records / memory_pages
    if abs(memory_average - calculated_memory_average) > 0.02:
        fail("In-memory slotted-page average does not match records/pages.")

    # PF-backed page count: this is the value used in the comparison.
    pf_block = block_after(pf_marker, "PF-backed slotted-page Student dataset")
    pf_records = read_metric(pf_block, "Student records verified")
    pf_pages = read_metric(pf_block, "PF pages allocated")

    if pf_records != dataset_stats["record_count"]:
        fail("PF-backed slotted-page test did not verify every Student record.")
    if pf_pages <= 0:
        fail("PF-backed slotted-page test reported an invalid page count.")
    if pf_pages != memory_pages:
        fail(
            "PF-backed and in-memory slotted page counts differ: "
            f"PF={pf_pages}, in-memory={memory_pages}. "
            "Refusing to generate a mixed-basis comparison."
        )

    average_records = pf_records / pf_pages

    print("\nSlotted-page results extracted:")
    print(f"  PF records verified      : {pf_records}")
    print(f"  PF pages allocated       : {pf_pages}")
    print(f"  Average records per page : {average_records:.2f}")
    print(f"  In-memory page cross-check: {memory_pages}")

    return {
        "pages": pf_pages,
        "records": pf_records,
        "average_records": average_records,
        "in_memory_pages": memory_pages,
        "in_memory_average": memory_average,
    }


# ------------------------------------------------------------
# Extract static-record PF results
# ------------------------------------------------------------

def extract_static_results(output, dataset_stats):

    header_pattern = re.compile(
        r"^=+\s*Static storage:\s*M=(\d+)\s*=+\s*$",
        re.MULTILINE
    )

    matches = list(header_pattern.finditer(output))

    results = {}

    for index, match in enumerate(matches):

        m = int(match.group(1))

        start = match.end()

        if index + 1 < len(matches):
            end = matches[index + 1].start()
        else:
            end = len(output)

        block = output[start:end]

        values = {
            "M": m,

            "records_inserted":
                read_metric(block, "Student records inserted"),

            "records_scanned":
                read_metric(block, "Student records scanned"),

            "rid_retrievals_verified":
                read_metric(block, "RID retrievals verified"),

            "pages":
                read_metric(block, "PF pages allocated"),

            "actual_payload_bytes":
                read_metric(block, "Actual payload bytes"),

            "reserved_record_bytes":
                read_metric(block, "Reserved record bytes"),

            "length_metadata_bytes":
                read_metric(block, "Length metadata bytes"),

            "page_header_bytes":
                read_metric(block, "Page header bytes"),

            "occupied_page_bytes":
                read_metric(block, "Occupied page bytes"),

            "allocated_page_bytes":
                read_metric(block, "Allocated page bytes"),

            "reserved_padding_bytes":
                read_metric(block, "Reserved padding bytes"),

            "unused_page_bytes":
                read_metric(block, "Unused page bytes"),

            "pf_page_link_bytes":
                read_metric(block, "PF page-link bytes"),

            "pf_file_header_bytes":
                read_metric(block, "PF file-header bytes"),

            "estimated_pf_file_size_bytes":
                read_metric(block, "Estimated PF file size")
        }

        # ----------------------------------------------------
        # Validate static-record results
        # ----------------------------------------------------

        expected_records = dataset_stats["record_count"]
        expected_payload = dataset_stats["payload_bytes"]

        if values["records_inserted"] != expected_records:
            fail(f"Incorrect inserted record count for M={m}.")

        if values["records_scanned"] != expected_records:
            fail(f"Incorrect scanned record count for M={m}.")

        if values["rid_retrievals_verified"] != expected_records:
            fail(f"RID retrieval verification failed for M={m}.")

        if values["actual_payload_bytes"] != expected_payload:
            fail(f"Payload byte mismatch for M={m}.")

        capacity = (PAGE_SIZE - 8) // (m + 2)

        expected_pages = math.ceil(expected_records / capacity)

        if values["pages"] != expected_pages:
            fail(f"Unexpected page count for M={m}.")

        if values["reserved_padding_bytes"] != (
            values["reserved_record_bytes"]
            - values["actual_payload_bytes"]
        ):
            fail(f"Padding calculation mismatch for M={m}.")

        if values["occupied_page_bytes"] != (
            values["reserved_record_bytes"]
            + values["length_metadata_bytes"]
            + values["page_header_bytes"]
        ):
            fail(f"Occupied-page calculation mismatch for M={m}.")

        if values["allocated_page_bytes"] != (
            values["pages"] * PAGE_SIZE
        ):
            fail(f"Allocated-page calculation mismatch for M={m}.")

        if values["unused_page_bytes"] != (
            values["allocated_page_bytes"]
            - values["occupied_page_bytes"]
        ):
            fail(f"Unused-page calculation mismatch for M={m}.")

        if values["estimated_pf_file_size_bytes"] != (
            values["allocated_page_bytes"]
            + values["pf_page_link_bytes"]
            + values["pf_file_header_bytes"]
        ):
            fail(f"PF file-size calculation mismatch for M={m}.")

        values["records_per_page"] = capacity

        values["payload_utilization_percent"] = (
            100.0
            * values["actual_payload_bytes"]
            / values["allocated_page_bytes"]
        )

        values["occupied_page_utilization_percent"] = (
            100.0
            * values["occupied_page_bytes"]
            / values["allocated_page_bytes"]
        )

        results[m] = values

    missing = [
        m for m in STATIC_CONFIGURATIONS
        if m not in results
    ]

    if missing:
        fail(
            "Missing static configurations: "
            + ", ".join(map(str, missing))
        )

    return [results[m] for m in STATIC_CONFIGURATIONS]


# ------------------------------------------------------------
# Calculate slotted-page storage statistics
# ------------------------------------------------------------

def calculate_slotted_statistics(slotted, dataset_stats):

    records = dataset_stats["record_count"]
    payload = dataset_stats["payload_bytes"]
    pages = slotted["pages"]

    allocated = pages * PAGE_SIZE

    header_bytes = pages * SLOTTED_HEADER_BYTES

    slot_directory_bytes = records * SLOTTED_SLOT_BYTES

    occupied = (
        payload
        + header_bytes
        + slot_directory_bytes
    )

    unused = allocated - occupied

    if unused < 0:
        fail("Slotted-page accounting exceeds page capacity.")

    return {
        "configuration": "Slotted pages",
        "method": "Slotted",

        "max_record_length_M_bytes": "",

        "records_per_page":
            f"{slotted['average_records']:.2f} average",

        "pages_allocated": pages,

        "record_count": records,

        "actual_payload_bytes": payload,

        "allocated_data_page_bytes": allocated,

        "payload_utilization_percent":
            100.0 * payload / allocated,

        "occupied_page_bytes": occupied,

        "occupied_page_utilization_percent":
            100.0 * occupied / allocated,

        "reserved_record_bytes": payload,

        "record_padding_bytes": 0,

        "length_metadata_bytes": 0,

        "page_header_bytes": header_bytes,

        "slot_directory_bytes": slot_directory_bytes,

        "unused_page_bytes": unused,

        "pf_page_link_bytes":
            pages * PF_PAGE_LINK_BYTES,

        "pf_file_header_bytes":
            PF_FILE_HEADER_BYTES,

        "estimated_pf_file_size_bytes": (
            allocated
            + pages * PF_PAGE_LINK_BYTES
            + PF_FILE_HEADER_BYTES
        ),

        "page_count_source":
            "test_student_pf output (PF page traversal; in-memory cross-check)"
    }


# ------------------------------------------------------------
# Convert static results into comparison rows
# ------------------------------------------------------------

def make_static_row(values):

    return {
        "configuration": f"Static M={values['M']}",

        "method": "Static",

        "max_record_length_M_bytes": values["M"],

        "records_per_page": values["records_per_page"],

        "pages_allocated": values["pages"],

        "record_count": values["records_inserted"],

        "actual_payload_bytes": values["actual_payload_bytes"],

        "allocated_data_page_bytes": values["allocated_page_bytes"],

        "payload_utilization_percent":
            values["payload_utilization_percent"],

        "occupied_page_bytes": values["occupied_page_bytes"],

        "occupied_page_utilization_percent":
            values["occupied_page_utilization_percent"],

        "reserved_record_bytes":
            values["reserved_record_bytes"],

        "record_padding_bytes":
            values["reserved_padding_bytes"],

        "length_metadata_bytes":
            values["length_metadata_bytes"],

        "page_header_bytes":
            values["page_header_bytes"],

        "slot_directory_bytes": 0,

        "unused_page_bytes":
            values["unused_page_bytes"],

        "pf_page_link_bytes":
            values["pf_page_link_bytes"],

        "pf_file_header_bytes":
            values["pf_file_header_bytes"],

        "estimated_pf_file_size_bytes":
            values["estimated_pf_file_size_bytes"],

        "page_count_source":
            "test_static_pf output"
    }


# ------------------------------------------------------------
# Generate CSV
# ------------------------------------------------------------

def generate_csv(rows, output_path):

    fields = [
        "configuration",
        "method",
        "max_record_length_M_bytes",
        "records_per_page",
        "pages_allocated",
        "record_count",
        "actual_payload_bytes",
        "allocated_data_page_bytes",
        "payload_utilization_percent",
        "occupied_page_bytes",
        "occupied_page_utilization_percent",
        "reserved_record_bytes",
        "record_padding_bytes",
        "length_metadata_bytes",
        "page_header_bytes",
        "slot_directory_bytes",
        "unused_page_bytes",
        "pf_page_link_bytes",
        "pf_file_header_bytes",
        "estimated_pf_file_size_bytes",
        "page_count_source"
    ]

    with output_path.open(
        "w",
        newline="",
        encoding="utf-8"
    ) as file:

        writer = csv.DictWriter(file, fieldnames=fields)

        writer.writeheader()

        writer.writerows(rows)


# ------------------------------------------------------------
# Generate graphs
# ------------------------------------------------------------

def generate_graphs(rows):

    try:
        import matplotlib

        matplotlib.use("Agg")

        import matplotlib.pyplot as plt

    except ImportError:
        fail(
            "Matplotlib is missing. Install it using:\n"
            "python3 -m pip install matplotlib"
        )

    labels = [
        row["configuration"]
        for row in rows
    ]

    page_counts = [
        row["pages_allocated"]
        for row in rows
    ]

    utilizations = [
        row["payload_utilization_percent"]
        for row in rows
    ]

    # --------------------------------------------------------
    # Graph 1: Pages allocated
    # --------------------------------------------------------

    fig, ax = plt.subplots(figsize=(10, 5.8))

    bars = ax.bar(labels, page_counts)

    ax.set_title(
        "Data Pages Allocated: Slotted vs. Static Records"
    )

    ax.set_xlabel("Storage configuration")

    ax.set_ylabel("Allocated 4,096-byte data pages")

    ax.grid(axis="y", alpha=0.3)

    ax.set_axisbelow(True)

    ax.bar_label(bars, fmt="%.0f", padding=3)

    plt.xticks(rotation=15, ha="right")

    fig.tight_layout()

    fig.savefig(
        OUTPUT_DIR / "storage_pages_comparison.png",
        dpi=180,
        bbox_inches="tight"
    )

    plt.close(fig)

    # --------------------------------------------------------
    # Graph 2: Payload utilization
    # --------------------------------------------------------

    fig, ax = plt.subplots(figsize=(10, 5.8))

    bars = ax.bar(labels, utilizations)

    ax.set_title(
        "Payload Utilization: Slotted vs. Static Records"
    )

    ax.set_xlabel("Storage configuration")

    ax.set_ylabel("Payload utilization (%)")

    ax.set_ylim(0, 105)

    ax.grid(axis="y", alpha=0.3)

    ax.set_axisbelow(True)

    ax.bar_label(bars, fmt="%.2f%%", padding=3)

    plt.xticks(rotation=15, ha="right")

    fig.tight_layout()

    fig.savefig(
        OUTPUT_DIR / "storage_payload_utilization.png",
        dpi=180,
        bbox_inches="tight"
    )

    plt.close(fig)


# ------------------------------------------------------------
# Generate Markdown report
# ------------------------------------------------------------

def generate_markdown(rows, dataset_stats):

    slotted = rows[0]

    static108 = next(
        row for row in rows
        if row["max_record_length_M_bytes"] == 108
    )

    static256 = next(
        row for row in rows
        if row["max_record_length_M_bytes"] == 256
    )

    page_reduction = 100.0 * (
        1.0
        - slotted["pages_allocated"]
        / static108["pages_allocated"]
    )

    utilization_gain = (
        slotted["payload_utilization_percent"]
        - static108["payload_utilization_percent"]
    )

    table = [
        "| Configuration | Records/page | Pages | Payload utilization | Padding | Estimated PF file size |",
        "|---|---:|---:|---:|---:|---:|"
    ]

    for row in rows:

        table.append(
            f"| {row['configuration']} "
            f"| {row['records_per_page']} "
            f"| {row['pages_allocated']:,} "
            f"| {row['payload_utilization_percent']:.2f}% "
            f"| {row['record_padding_bytes']:,} bytes "
            f"| {row['estimated_pf_file_size_bytes']:,} bytes |"
        )

    content = f"""# Slotted Page Notes: Slotted Pages vs. Static Records

## Dataset

- Student records: **{dataset_stats['record_count']:,}**
- Actual payload: **{dataset_stats['payload_bytes']:,} bytes**
- Page size: **{PAGE_SIZE:,} bytes**

## Measurement formula

Payload utilization:

`actual payload bytes / allocated data-page bytes * 100`

PF page-link and file-header bytes are excluded from payload utilization but included in estimated PF file size.

## Results

{chr(10).join(table)}

## Interpretation

1. Slotted pages allocated **{slotted['pages_allocated']:,} pages**, compared with **{static108['pages_allocated']:,} pages** for static M=108.

2. Slotted pages used **{page_reduction:.2f}% fewer data pages** than static M=108.

3. Payload utilization improved by **{utilization_gain:.2f} percentage points**.

4. Static storage reserves a fixed maximum length for each record. Increasing M increases unused record-slot space.

5. At M=256, reserved record padding is **{static256['record_padding_bytes']:,} bytes**.

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
"""

    notes_path = OUTPUT_DIR / "storage_comparison_notes.md"
    notes_path.write_text(content + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(
    description="Run and summarize ToyDB Slotted Page storage experiments."
    )

    parser.add_argument(
        "--student-file",
        type=Path,
        default=DEFAULT_STUDENT_FILE,
        help="Path to student.txt"
    )

    parser.add_argument(
        "--force",
        action="store_true",
        help="Rerun all tests and regenerate the Slotted Page results."
    )

    args = parser.parse_args()

    student_file = args.student_file.expanduser().resolve()

    OUTPUT_DIR.mkdir(
        parents=True,
        exist_ok=True
    )

    existing_results = all(
        (OUTPUT_DIR / filename).is_file()
        and (OUTPUT_DIR / filename).stat().st_size > 0
        for filename in OUTPUT_FILES
    )

    if existing_results:
        try:
            with (OUTPUT_DIR / "storage_comparison.csv").open(
                newline="", encoding="utf-8"
            ) as csv_file:
                saved_rows = list(csv.DictReader(csv_file))
            existing_results = (
                len(saved_rows) == 6
                and all(
                    row.get("configuration")
                    for row in saved_rows
                )
                and "Slotted pages" in {
                    row["configuration"] for row in saved_rows
                }
                and "Static M=256" in {
                    row["configuration"] for row in saved_rows
                }
            )
            notes = (OUTPUT_DIR / "storage_comparison_notes.md").read_text(
                encoding="utf-8"
            )
            existing_results = existing_results and (
                "## Results" in notes and "## Measurement notes" in notes
            )
        except (OSError, csv.Error, KeyError):
            existing_results = False

    if existing_results and not args.force:
        print("Existing Slotted Page results are present.")
        print("Skipping tests and result generation.")
        print(f"Results: {OUTPUT_DIR}")
        print("Use --force to regenerate all results.")
        return

    # 1. Read actual dataset statistics.
    dataset_stats = read_dataset_stats(student_file)

    print("\nDataset validation passed.")

    print(
        f"Records      : {dataset_stats['record_count']:,}"
    )

    print(
        f"Payload bytes: {dataset_stats['payload_bytes']:,}"
    )

    # 2. Execute all required storage tests.
    output = run_storage_tests(student_file)

    # 3. Extract slotted-page results from C test output.
    slotted = extract_slotted_results(
        output,
        dataset_stats
    )

    # 4. Extract static PF results from C test output.
    static_results = extract_static_results(
        output,
        dataset_stats
    )

    # 5. Build comparison rows.
    rows = [
        calculate_slotted_statistics(
            slotted,
            dataset_stats
        )
    ]

    rows.extend(
        make_static_row(values)
        for values in static_results
    )

    # 6. Generate CSV.
    generate_csv(
        rows,
        OUTPUT_DIR / "storage_comparison.csv"
    )

    # 7. Generate graphs.
    generate_graphs(rows)

    # 8. Generate report notes.
    generate_markdown(
        rows,
        dataset_stats
    )

    # 9. Print final summary.
    print("\n========================================")
    print("STORAGE EXPERIMENT SUMMARY")
    print("========================================")

    for row in rows:

        print(
            f"{row['configuration']:<18} "
            f"Pages: {row['pages_allocated']:>5} "
            f"Payload utilization: "
            f"{row['payload_utilization_percent']:.2f}% "
            f"Source: {row['page_count_source']}"
        )

    print("\nGenerated files:")

    generated_files = [
        RESULTS_LOG,
        OUTPUT_DIR / "storage_comparison.csv",
        OUTPUT_DIR / "storage_pages_comparison.png",
        OUTPUT_DIR / "storage_payload_utilization.png",
        OUTPUT_DIR / "storage_comparison_notes.md"
    ]

    for file in generated_files:

        print(f"- {file}")

    print("\nALL STORAGE EXPERIMENTS COMPLETED SUCCESSFULLY.")

if __name__ == "__main__":
    main()