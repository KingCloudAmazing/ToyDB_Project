
CC = cc
CFLAGS = -std=gnu89 -Wno-implicit-function-declaration

AM_OBJS = am.o amfns.o amsearch.o aminsert.o amstack.o amglobals.o amscan.o amprint.o
TEST_OBJS = $(AM_OBJS) misc.o

all: a.out amlayer.o

a.out: $(AM_OBJS) main.o ../pflayer/pflayer.o
	$(CC) $(CFLAGS) -o $@ $(AM_OBJS) main.o ../pflayer/pflayer.o

amlayer.o: $(AM_OBJS)
	ld -r $(AM_OBJS) -o $@

test1: test1.o $(TEST_OBJS) ../pflayer/pflayer.o
	$(CC) $(CFLAGS) -o $@ test1.o $(TEST_OBJS) ../pflayer/pflayer.o

test2: test2.o $(TEST_OBJS) ../pflayer/pflayer.o
	$(CC) $(CFLAGS) -o $@ test2.o $(TEST_OBJS) ../pflayer/pflayer.o

test3: test3.o $(TEST_OBJS) ../pflayer/pflayer.o
	$(CC) $(CFLAGS) -o $@ test3.o $(TEST_OBJS) ../pflayer/pflayer.o

test_student_am: test_student_am.c $(TEST_OBJS) ../pflayer/pflayer.o
	$(CC) $(CFLAGS) -I../slottedpage -I../pflayer -o $@ \
	test_student_am.c $(TEST_OBJS) ../pflayer/pflayer.o \
	../slottedpage/slotted_page.c ../slottedpage/student_store.c

test_student_am_incremental: test_student_am_incremental.c $(TEST_OBJS) ../pflayer/pflayer.o
	$(CC) $(CFLAGS) -I../slottedpage -I../pflayer -o $@ \
	test_student_am_incremental.c $(TEST_OBJS) ../pflayer/pflayer.o \
	../slottedpage/slotted_page.c ../slottedpage/student_store.c

test_student_am_bulk: test_student_am_bulk.c $(TEST_OBJS) ../pflayer/pflayer.o
	$(CC) $(CFLAGS) -I../slottedpage -I../pflayer -o $@ \
	test_student_am_bulk.c $(TEST_OBJS) ../pflayer/pflayer.o \
	../slottedpage/slotted_page.c ../slottedpage/student_store.c

misc.o: misc.c am.h pf.h testam.h
	$(CC) $(CFLAGS) -c misc.c

test1.o: test1.c am.h testam.h
	$(CC) $(CFLAGS) -c test1.c

test2.o: test2.c am.h testam.h
	$(CC) $(CFLAGS) -c test2.c

test3.o: test3.c am.h testam.h
	$(CC) $(CFLAGS) -c test3.c

clean:
	rm -f *.o a.out test1 test2 test3 \
	test_student_am test_student_am_incremental test_student_am_bulk \
	testrel.0 testrel.1 \
	student_am.0 student_incremental.0 \
	student_records.dat student_incremental_records.dat \
	student_bulk_records.dat student_bulk.0

.PHONY: all clean test1 test2 test3 \
	test_student_am test_student_am_incremental test_student_am_bulk
