CC = cc
CFLAGS = -Wall -Werror -Wextra -g

WRITER_SRC = writer.c
FINDER_SRC = finder.c
AESD_SRC = aesdsocket.c

WRITER_OBJ = $(WRITER_SRC:.c=.o)
FINDER_OBJ = $(FINDER_SRC:.c=.o)
AESD_OBJ = $(AESD_SRC:.c=.o)

WRITER_EXE = writer
FINDER_EXE = finder
AESD_EXE = aesdsocket

all: $(WRITER_EXE) $(FINDER_EXE) $(AESD_EXE)

$(WRITER_EXE): $(WRITER_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

$(FINDER_EXE): $(FINDER_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

$(AESD_EXE): $(AESD_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(WRITER_EXE) $(FINDER_EXE) $(AESD_EXE) $(WRITER_OBJ) $(FINDER_OBJ) $(AESD_OBJ)

.PHONY: all clean

