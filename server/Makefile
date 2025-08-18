CC ?= $(TARGET_CC)
CFLAGS = -Wall -Werror -Wextra -g

TARGET = aesdsocket

all: $(TARGET)

$(TARGET): aesdsocket.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean
