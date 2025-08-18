TARGET = aesdsocket

CC ?= gcc
CFLAGS := -Wall -Werror -Wextra -g

.PHONY: all clean

all: $(TARGET)

aesdsocket: aesdsocket.o
	$(CC) $(CFLAGS) -o $@ $^

aesdsocket.o: aesdsocket.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(TARGET) aesdsocket.o

