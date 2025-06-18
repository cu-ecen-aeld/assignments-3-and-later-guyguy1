CC := $(CROSS_COMPILE)gcc
CFLAGS := -Wall -Wextra
TARGET := aesdsocket

all: $(TARGET)

default: all

$(TARGET): aesdsocket.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)

.PHONY: all default clean
