TARGET = aesdsocket

CC ?= gcc
CFLAGS := -Wall -Werror -Wextra -g

.PHONY: all clean

all: $(TARGET)

default: all

$(TARGET): aesdsocket.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)
