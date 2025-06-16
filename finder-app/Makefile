CC := $(CROSS_COMPILE)gcc
CFLAGS := -Wall

all: writer

writer: writer.o
	$(CC) $(CFLAGS) -o $@ writer.o

writer.o: writer.c
	$(CC) $(CFLAGS) -c writer.c -o $@

clean:
	rm -f writer writer.o

.PHONY: all clean
