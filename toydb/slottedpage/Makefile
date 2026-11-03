CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -O2
SANFLAGS ?=

.PHONY: all test clean

all: test_slotted test_student_dataset

test_slotted: slotted_page.c slotted_page.h test_slotted.c
	$(CC) $(CFLAGS) $(SANFLAGS) slotted_page.c test_slotted.c -o $@

test_student_dataset: slotted_page.c slotted_page.h test_student_dataset.c
	$(CC) $(CFLAGS) $(SANFLAGS) slotted_page.c test_student_dataset.c -o $@

test: all
	./test_slotted
	@test -n "$(STUDENT_FILE)" || (echo "Set STUDENT_FILE=/path/to/student.txt"; exit 2)
	./test_student_dataset "$(STUDENT_FILE)"

clean:
	rm -f test_slotted test_student_dataset
