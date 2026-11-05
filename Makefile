
.PHONY: all test build test-pf test-storage test-am clean

# Default command runs the complete project test suite.
all: test

test: test-pf test-storage test-am
	@echo ""
	@echo "========================================"
	@echo " ALL TESTS COMPLETED"
	@echo "========================================"

# Build all three components.
build:
	$(MAKE) -C toydb/pflayer all
	$(MAKE) -C toydb/slottedpage all
	$(MAKE) -C toydb/amlayer all

# PF correctness tests and performance experiments.
test-pf:
	@echo "\n===== PF LAYER TESTS ====="
	$(MAKE) -C toydb/pflayer test

# Slotted-page and static-record tests.
test-storage:
	@echo "\n===== SLOTTED PAGE TESTS ====="
	$(MAKE) -C toydb/slottedpage test

# AM basic tests and Student B+ tree benchmarks.
test-am:
	@echo "\n===== AM LAYER TESTS ====="
	$(MAKE) -C toydb/pflayer all
	$(MAKE) -C toydb/amlayer test1 test2 test3
	cd toydb/amlayer && ./test1
	cd toydb/amlayer && ./test2
	cd toydb/amlayer && ./test3
	cd toydb/amlayer && ./run_am_benchmarks.sh
	cd toydb/amlayer && ./test_student_am_bulk --boundary-test

# Clean generated build files using component Makefiles.
clean:
	$(MAKE) -C toydb/pflayer clean
	$(MAKE) -C toydb/slottedpage clean
	$(MAKE) -C toydb/amlayer clean
