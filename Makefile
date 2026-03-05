# Makefile for usb_rw_windows.c
# Cross-compile on Linux with MinGW, or use native MSVC/MinGW on Windows.

TARGET  = usb_rw_windows.exe
SRC     = usb_rw_windows.c

# Cross-compiler (Linux → Windows x64)
CC      = x86_64-w64-mingw32-gcc
CFLAGS  = -Wall -Wextra -O2

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)
