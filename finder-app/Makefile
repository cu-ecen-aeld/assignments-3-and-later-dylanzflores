CC = gcc
CROSS_COMPILE ?= #= aarch64-linux-gnu-

CROSS_CC = $(CROSS_COMPILE)gcc
CROSS_CPP = $(CROSS_COMPILE)g++

CFLAGS = -Wall -g -static

# Source files
WRITER_SRC = writer.c
FINDER_SRC = finder.c

# Object files
WRITER_OBJ = $(WRITER_SRC:.c=.o)
FINDER_OBJ = $(FINDER_SRC:.c=.o)

# Executables
WRITER_EXE = writer
FINDER_EXE = finder

# Default target builds both programs
all: $(WRITER_EXE) $(FINDER_EXE)

# Build writer executable
$(WRITER_EXE): $(WRITER_OBJ)
	$(CROSS_CC) $(CFLAGS) $^ -o $@

# Build finder executable
$(FINDER_EXE): $(FINDER_OBJ)
	$(CROSS_CC) $(CFLAGS) $^ -o $@

# Compile writer source to object
$(WRITER_OBJ): $(WRITER_SRC)
	$(CROSS_CC) $(CFLAGS) -c $< -o $@

# Compile finder source to object
$(FINDER_OBJ): $(FINDER_SRC)
	$(CROSS_CC) $(CFLAGS) -c $< -o $@

# Clean
clean:
	rm -f $(WRITER_EXE) $(FINDER_EXE) $(WRITER_OBJ) $(FINDER_OBJ)

# Cross compilation target (build both)
cross: clean all
	@echo "Cross compiled $(WRITER_EXE) and $(FINDER_EXE) with $(CROSS_CC)"

.PHONY: all clean cross

