CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -O2
SANFLAGS ?=

.PHONY: all test-basic test-student test-phase2 test clean clean-phase2

all: test_slotted test_student_dataset test_slotted_phase2

test_slotted: slotted_page.c slotted_page.h test_slotted.c
	$(CC) $(CFLAGS) $(SANFLAGS) slotted_page.c test_slotted.c -o $@

test_student_dataset: slotted_page.c slotted_page.h test_student_dataset.c
	$(CC) $(CFLAGS) $(SANFLAGS) slotted_page.c test_student_dataset.c -o $@

test_slotted_phase2: slotted_page.c slotted_page.h test_slotted_phase2.c
	$(CC) $(CFLAGS) $(SANFLAGS) slotted_page.c test_slotted_phase2.c -o $@

test-basic: test_slotted
		./test_slotted

test-student: test_student_dataset
	@test -n "$(STUDENT_FILE)" || (echo "Set STUDENT_FILE=/path/to/student.txt"; exit 2)
	./test_student_dataset "$(STUDENT_FILE)"

test-phase2: test_slotted_phase2
	./test_slotted_phase2

test: test-basic test-student test-phase2

clean:
	rm -f test_slotted test_student_dataset test_slotted_phase2

clean-phase2:
	rm -f test_slotted_phase2
