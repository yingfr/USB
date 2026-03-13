# USB
U盘读写

Windows C program that locates the first removable drive and performs a write + readback test using Win32 `CreateFile`/`WriteFile`/`ReadFile` APIs.

## Files

- **`usb_rw_windows.c`** — Windows-only source file:
  - Scans `GetLogicalDrives()` bitmask for the first `DRIVE_REMOVABLE` volume
  - Writes `usb_test.txt` to the USB root via `WriteFile` + `FlushFileBuffers`
  - Reads it back and prints contents to stdout

## Build

### MSVC

```bat
cl usb_rw_windows.c /Fe:usb_rw_windows.exe
```

### MinGW

```bat
gcc usb_rw_windows.c -o usb_rw_windows.exe
```

## Usage

Insert a USB drive, then run:

```bat
usb_rw_windows.exe
```

The program auto-detects the first removable drive letter and writes `<drive>:\usb_test.txt`.

## Notes

- **Windows only** — uses Win32 APIs (`windows.h`)
- A USB (removable) drive must be inserted before running
- May require administrator privileges depending on drive permissions
