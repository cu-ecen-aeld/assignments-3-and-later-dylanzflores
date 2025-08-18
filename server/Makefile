TARGET = aesdsocket

CC ?= gcc
CFLAGS := -Wall -Werror -Wextra -g

.PHONY: all clean

all: $(TARGET)

default: all

# Link target from object file
$(TARGET): aesdsocket.o
	$(CC) $(CFLAGS) -o $@ $^

# Compile source to object
aesdsocket.o: aesdsocket.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) aesdsocket.o
