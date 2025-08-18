CC = $(TARGET_CC)
CFLAGS = -Wall -Werror -Wextra -g

WRITER_SRC = writer.c
FINDER_SRC = finder.c

WRITER_OBJ = $(WRITER_SRC:.c=.o)
FINDER_OBJ = $(FINDER_SRC:.c=.o)

WRITER_EXE = writer
FINDER_EXE = finder

all: $(WRITER_EXE) $(FINDER_EXE)

$(WRITER_EXE): $(WRITER_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

$(FINDER_EXE): $(FINDER_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(WRITER_EXE) $(FINDER_EXE) $(WRITER_OBJ) $(FINDER_OBJ)

.PHONY: all clean

