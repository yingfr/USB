# Makefile for usb_rw_windows.c
# Supports cross-compilation on Linux using MinGW-w64
# and native compilation on Windows using MinGW

CC_CROSS  = x86_64-w64-mingw32-gcc
CC_WIN    = gcc
TARGET    = usb_rw_windows.exe
SRC       = usb_rw_windows.c
CFLAGS    = -Wall -Wextra -O2

.PHONY: all cross clean

# Default: cross-compile on Linux → Windows (requires mingw-w64)
all: cross

cross:
	$(CC_CROSS) $(CFLAGS) $(SRC) -o $(TARGET)

# Native MinGW on Windows
win:
	$(CC_WIN) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
