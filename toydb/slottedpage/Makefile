CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -O2
SANFLAGS ?=

PF_DIR = ../pflayer
PF_SRC = $(PF_DIR)/pf.c $(PF_DIR)/buf.c $(PF_DIR)/hash.c
PF_CFLAGS = -std=gnu89 -D_DEFAULT_SOURCE -fno-builtin -w
SP_PF_SRC = slotted_page.c student_store.c
STUDENT_FILE = ../../data/student.txt

.PHONY: all test-basic test-student test-phase2 test clean clean-phase2 test-pf test-pf-phase3 test-student-pf test-scan test-scan-dataset

all: test_slotted test_student_dataset test_slotted_phase2 test_pf_integration test_pf_phase3 test_student_pf test_student_scan test_student_scan_dataset

test_slotted: slotted_page.c slotted_page.h test_slotted.c
	$(CC) $(CFLAGS) $(SANFLAGS) slotted_page.c test_slotted.c -o $@

test_student_dataset: slotted_page.c slotted_page.h test_student_dataset.c
	$(CC) $(CFLAGS) $(SANFLAGS) slotted_page.c test_student_dataset.c -o $@

test_slotted_phase2: slotted_page.c slotted_page.h test_slotted_phase2.c
	$(CC) $(CFLAGS) $(SANFLAGS) slotted_page.c test_slotted_phase2.c -o $@

test_pf_integration: $(SP_PF_SRC) slotted_page.h student_store.h pf_api.h rid.h $(PF_SRC) test_pf_integration.c
	$(CC) $(CFLAGS) $(PF_CFLAGS) -I$(PF_DIR) $(SP_PF_SRC) $(PF_SRC) test_pf_integration.c -o $@

test_pf_phase3: $(SP_PF_SRC) slotted_page.h student_store.h pf_api.h rid.h $(PF_SRC) test_pf_phase3.c
	$(CC) $(CFLAGS) $(PF_CFLAGS) -I$(PF_DIR) $(SP_PF_SRC) $(PF_SRC) test_pf_phase3.c -o $@

test_student_pf: $(SP_PF_SRC) slotted_page.h student_store.h pf_api.h rid.h $(PF_SRC) test_student_pf.c
	$(CC) $(CFLAGS) $(PF_CFLAGS) -I$(PF_DIR) $(SP_PF_SRC) $(PF_SRC) test_student_pf.c -o $@

test_student_scan: $(SP_PF_SRC) test_student_scan.c slotted_page.h student_store.h pf_api.h rid.h
	$(CC) $(CFLAGS) $(PF_CFLAGS) -I$(PF_DIR) $(SP_PF_SRC) $(PF_SRC) test_student_scan.c -o $@

test_student_scan_dataset: $(SP_PF_SRC) test_student_scan_dataset.c slotted_page.h student_store.h pf_api.h rid.h
	$(CC) $(CFLAGS) $(PF_CFLAGS) -I$(PF_DIR) $(SP_PF_SRC) $(PF_SRC) test_student_scan_dataset.c -o $@

test-basic: test_slotted
		./test_slotted

test-student: test_student_dataset
	@test -n "$(STUDENT_FILE)" || (echo "Set STUDENT_FILE=/path/to/student.txt"; exit 2)
	./test_student_dataset "$(STUDENT_FILE)"

test-phase2: test_slotted_phase2
	./test_slotted_phase2

test-pf: test_pf_integration
	./test_pf_integration

test-pf-phase3: test_pf_phase3
	./test_pf_phase3

test-student-pf: test_student_pf
	@test -n "$(STUDENT_FILE)" || (echo "Set STUDENT_FILE=/path/to/student.txt"; exit 2)
	./test_student_pf "$(STUDENT_FILE)"

test-scan: test_student_scan
	./test_student_scan

test-scan-dataset: test_student_scan_dataset
	@test -n "$(STUDENT_FILE)" || (echo "Set STUDENT_FILE=/path/to/student.txt"; exit 2)
	./test_student_scan_dataset "$(STUDENT_FILE)"

test: test-basic test-student test-phase2 test-pf test-pf-phase3 \
	  test-student-pf test-scan test-scan-dataset

clean:
	rm -f test_slotted test_student_dataset test_slotted_phase2 test_pf_integration test_pf_phase3 test_student_pf test_student_scan test_student_scan_dataset

clean-phase2:
	rm -f test_slotted_phase2
