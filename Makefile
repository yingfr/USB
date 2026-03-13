# Makefile for usb_scsi_detect
#
# Targets:
#   make              — build usb_scsi_detect (Linux)
#   make clean        — remove build artifacts

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11
TARGET  = usb_scsi_detect
SRC     = usb_scsi_detect.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)
