CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Wextra
LDFLAGS ?= -lpthread
TARGET ?= aesdsocket

USE_AESD_CHAR_DEVICE := 1

ifeq ($(USE_AESD_CHAR_DEVICE),1)
  $(info ----Compiling for aesd char device----)
  CFLAGS += -DUSE_AESD_CHAR_DEVICE
else
  $(info ----Compiling for file----)
endif


all: $(TARGET)

default: all

$(TARGET): aesdsocket.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)

.PHONY: all default clean
