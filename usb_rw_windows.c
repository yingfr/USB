#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_FILE_SIZE 0x7fffffff

static int is_removable_drive_letter(char letter) {
    char root[] = "X:\\";
    root[0] = letter;
    UINT t = GetDriveTypeA(root);
    return (t == DRIVE_REMOVABLE); // U盘/移动存储通常是这个类型
}

static int find_first_removable_drive(char* outLetter) {
    DWORD mask = GetLogicalDrives(); // bit0=A, bit1=B, ...
    if (mask == 0) return 0;

    for (int i = 0; i < 26; i++) {
        if (mask & (1u << i)) {
            char letter = (char)('A' + i);
            if (is_removable_drive_letter(letter)) {
                *outLetter = letter;
                return 1;
            }
        }
    }
    return 0;
}

static int write_file_win32(const char* path, const void* data, DWORD size) {
    HANDLE h = CreateFileA(
        path,
        GENERIC_WRITE,
        FILE_SHARE_READ,          // 允许别人读
        NULL,
        CREATE_ALWAYS,            // 覆盖写
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "CreateFile(write) failed. path=%s err=%lu\n", path, GetLastError());
        return 0;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(h, data, size, &written, NULL);
    if (!ok || written != size) {
        fprintf(stderr, "WriteFile failed. written=%lu/%lu err=%lu\n", written, size, GetLastError());
        CloseHandle(h);
        return 0;
    }

    // 确保写入落盘（对U盘更建议flush）
    if (!FlushFileBuffers(h)) {
        fprintf(stderr, "FlushFileBuffers failed. err=%lu\n", GetLastError());
        // flush失败不一定代表写入失败，但这里当作失败处理更安全
        CloseHandle(h);
        return 0;
    }

    CloseHandle(h);
    return 1;
}

static int read_file_win32(const char* path, uint8_t** outBuf, DWORD* outSize) {
    HANDLE h = CreateFileA(
        path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "CreateFile(read) failed. path=%s err=%lu\n", path, GetLastError());
        return 0;
    }

    LARGE_INTEGER liSize;
    if (!GetFileSizeEx(h, &liSize) || liSize.QuadPart > MAX_FILE_SIZE) {
        fprintf(stderr, "GetFileSizeEx failed or too large. err=%lu\n", GetLastError());
        CloseHandle(h);
        return 0;
    }

    DWORD size = (DWORD)liSize.QuadPart;
    uint8_t* buf = (uint8_t*)malloc(size + 1);
    if (!buf) {
        fprintf(stderr, "malloc failed.\n");
        CloseHandle(h);
        return 0;
    }

    DWORD read = 0;
    BOOL ok = ReadFile(h, buf, size, &read, NULL);
    if (!ok || read != size) {
        fprintf(stderr, "ReadFile failed. read=%lu/%lu err=%lu\n", read, size, GetLastError());
        free(buf);
        CloseHandle(h);
        return 0;
    }
    buf[size] = 0; // 便于当作字符串打印（不影响二进制）

    CloseHandle(h);
    *outBuf = buf;
    *outSize = size;
    return 1;
}

int main(void) {
    char letter = 0;
    if (!find_first_removable_drive(&letter)) {
        fprintf(stderr, "No removable drive found. Please insert a USB drive.\n");
        return 1;
    }

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%c:\\usb_test.txt", letter);

    const char* msg = "Hello USB!\r\nThis file is written by Win32 ReadFile/WriteFile.\r\n";
    DWORD msgLen = (DWORD)strlen(msg);

    printf("Using removable drive: %c:\\\n", letter);
    printf("Writing: %s\n", path);

    if (!write_file_win32(path, msg, msgLen)) {
        return 2;
    }

    printf("Reading back...\n");
    uint8_t* buf = NULL;
    DWORD size = 0;
    if (!read_file_win32(path, &buf, &size)) {
        return 3;
    }

    printf("Read %lu bytes:\n", (unsigned long)size);
    printf("-----\n%.*s\n-----\n", (int)size, (char*)buf);

    free(buf);
    printf("Done.\n");
    return 0;
}
