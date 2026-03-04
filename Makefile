# Makefile for usb_rw_windows
# Usage:
#   make           -> build with gcc (MinGW)
#   make msvc      -> build with MSVC cl.exe
#   make clean     -> remove build artifacts

TARGET = usb_rw_windows.exe
SRC    = usb_rw_windows.c

.PHONY: all msvc clean

all: $(TARGET)

$(TARGET): $(SRC)
	gcc $(SRC) -o $(TARGET)

msvc: $(SRC)
	cl $(SRC) /Fe:$(TARGET)

clean:
	del /Q $(TARGET) 2>nul || rm -f $(TARGET)
