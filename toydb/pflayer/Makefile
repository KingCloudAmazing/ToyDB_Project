#PUBLICDIR= /usr0/cs564/public/project
# Source files
SRC = buf.c hash.c pf.c

# Object files
OBJ = buf.o hash.o pf.o

# Header files
HDR = pftypes.h pf.h

# Default target
all: pflayer.o

# Build the PF layer
pflayer.o: $(OBJ)
	ld -r -o pflayer.o $(OBJ)

# Build all test programs
tests: testhash testpf testbuf teststats testlru

# Existing PF test
testpf: testpf.o pflayer.o
	cc -o testpf testpf.o pflayer.o

# Existing hash test
testhash: testhash.o pflayer.o
	cc -o testhash testhash.o pflayer.o

# Buffer manager test
testbuf: testbuf.o pflayer.o
	cc -o testbuf testbuf.o pflayer.o

# Buffer statistics test
teststats: teststats.o pflayer.o
	cc -o teststats teststats.o pflayer.o

# Strict LRU test
testlru: testlru.o pflayer.o
	cc -o testlru testlru.o pflayer.o

# Header dependencies
$(OBJ): $(HDR)

testhash.o: $(HDR)

testpf.o: $(HDR)

testbuf.o: $(HDR)

teststats.o: $(HDR)

testlru.o: $(HDR)

# Run lint
lint:
	lint $(SRC)

# Install target
install: pflayer.o