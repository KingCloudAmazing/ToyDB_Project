#!/bin/bash
set -euo pipefail

# Run from any directory; all paths are resolved relative to this script.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RESULTS_DIR="$SCRIPT_DIR/benchmark_results"
mkdir -p "$RESULTS_DIR"
rm -f "$RESULTS_DIR"/*.log "$RESULTS_DIR"/*.csv "$RESULTS_DIR"/graphs/*.png
mkdir -p "$RESULTS_DIR/graphs"

if [ ! -f "../../data/student.txt" ]; then
    echo "ERROR: ../../data/student.txt not found."
    echo "Place the unchanged dataset at data/student.txt in the project root."
    exit 1
fi

echo "Building PF layer..."
make -C ../pflayer -f Makefile all

echo "Building AM tests and query benchmark..."
make -f makefile test_student_am test_student_am_incremental test_student_am_bulk

cc -std=gnu89 -Wno-implicit-function-declaration \
   -I. -I../pflayer -I../slottedpage \
   -o benchmark_am_queries benchmark_am_queries.c \
   am.o amfns.o amsearch.o aminsert.o amstack.o amglobals.o \
   amscan.o amprint.o misc.o ../pflayer/pflayer.o \
   ../slottedpage/slotted_page.c ../slottedpage/student_store.c

BUILD_CSV="$RESULTS_DIR/build_time.csv"
QUERY_CSV="$RESULTS_DIR/query_metrics.csv"
LOG="$RESULTS_DIR/execution.log"

echo "method,build_seconds" > "$BUILD_CSV"
echo "method,query_seconds,matches,logical_page_accesses,physical_page_reads,logical_writes,physical_writes,buffer_hits" > "$QUERY_CSV"
: > "$LOG"

run_build() {
    method="$1"
    executable="$2"
    index_file="$3"
    echo "==================================================" | tee -a "$LOG"
    echo "Running $method construction..." | tee -a "$LOG"

    /usr/bin/time -p -o "$RESULTS_DIR/time.tmp" "./$executable" \
        > "$RESULTS_DIR/$method.log" 2>&1
    cat "$RESULTS_DIR/$method.log" >> "$LOG"

    seconds="$(awk '$1=="real" {print $2}' "$RESULTS_DIR/time.tmp")"
    if [ -z "$seconds" ]; then
        echo "Could not read wall-clock time for $method" >&2
        exit 1
    fi
    echo "$method,$seconds" >> "$BUILD_CSV"

    if [ ! -f "$index_file" ]; then
        echo "Expected index file $index_file was not created" >&2
        exit 1
    fi

    echo "Running common query workload for $method..." | tee -a "$LOG"
    for run in $(seq 1 30); do
        query_line="$(./benchmark_am_queries "$index_file" "$method")"
        echo "$query_line" >> "$QUERY_CSV"
    done
    echo "Completed 30 independent query measurements for $method" | tee -a "$LOG"
}

run_build "one_shot" "test_student_am" "student_am.0"
run_build "incremental" "test_student_am_incremental" "student_incremental.0"
run_build "bulk_load" "test_student_am_bulk" "student_bulk.0"

python3 "$SCRIPT_DIR/generate_am_graphs.py" \
    "$BUILD_CSV" "$QUERY_CSV" "$RESULTS_DIR/graphs"

rm -f "$RESULTS_DIR/time.tmp"
echo
echo "AM benchmarks completed."
echo "Results: $RESULTS_DIR"
echo "Graphs : $RESULTS_DIR/graphs"
