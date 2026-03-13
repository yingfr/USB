# Makefile for usb_rw_windows.c
# Linux: cross-compile to Windows with MinGW
# Windows: compile with native MinGW gcc

ifeq ($(OS),Windows_NT)
CC      = gcc
RM_CMD  ?= cmd /C del /Q
else
CC      = x86_64-w64-mingw32-gcc
RM_CMD  ?= rm -f
endif

TARGET  ?= usb_rw_windows.exe
SRC     ?= usb_rw_windows.c
CFLAGS  ?= -Wall -Wextra -O2

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@

clean:
	-$(RM_CMD) $(TARGET)
