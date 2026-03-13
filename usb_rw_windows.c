#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>

#define SENSE_BUF_LEN 32
#define INVALID_DISK_NUMBER 0xFFFFFFFFu
#define DEFAULT_POLL_MS 200u
#define DEFAULT_PACKET_BLOCKS 8u
#define DEFAULT_BLOCK_SIZE 512u
#define MAX_PACKET_BYTES (4u * 1024u * 1024u)
#define ENABLE_SCSI_RECONNECT 0
#define SCSI_RECONNECT_MAX_ATTEMPTS 20
#define SCSI_RECONNECT_DELAY_MS 300

#ifndef ERROR_NO_SUCH_DEVICE
#define ERROR_NO_SUCH_DEVICE 433L
#endif

typedef struct _SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER {
    SCSI_PASS_THROUGH_DIRECT sptd;
    ULONG filler;
    UCHAR sense[SENSE_BUF_LEN];
} SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER;

static volatile LONG g_stop = 0;
static DWORD g_last_scsi_winerr = 0;
static UCHAR g_last_scsi_status = 0;
static UCHAR g_last_scsi_sense[SENSE_BUF_LEN];
static int g_reconnect_mask_notice_printed = 0;

static BOOL WINAPI console_ctrl_handler(DWORD ctrlType) {
    switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        InterlockedExchange(&g_stop, 1);
        return TRUE;
    default:
        return FALSE;
    }
}

static void clear_last_scsi_error(void) {
    g_last_scsi_winerr = 0;
    g_last_scsi_status = 0;
    ZeroMemory(g_last_scsi_sense, sizeof(g_last_scsi_sense));
}

static void print_last_scsi_error(const char* where) {
    if (g_last_scsi_winerr != 0) {
        fprintf(stderr, "%s failed. winerr=%lu\n", where, g_last_scsi_winerr);
        return;
    }

    if (g_last_scsi_status != 0) {
        fprintf(stderr, "%s failed. scsi_status=0x%02X sense=", where, g_last_scsi_status);
        for (int i = 0; i < SENSE_BUF_LEN; ++i) {
            fprintf(stderr, "%02X ", g_last_scsi_sense[i]);
        }
        fprintf(stderr, "\n");
    }
}

static int is_recoverable_scsi_error(void) {
    if (g_last_scsi_winerr != 0) {
        if (g_last_scsi_winerr == ERROR_NO_SUCH_DEVICE ||
            g_last_scsi_winerr == ERROR_DEVICE_NOT_CONNECTED ||
            g_last_scsi_winerr == ERROR_NOT_READY ||
            g_last_scsi_winerr == ERROR_IO_DEVICE ||
            g_last_scsi_winerr == ERROR_INVALID_HANDLE) {
            return 1;
        }
        return 0;
    }

    if (g_last_scsi_status != 0) {
        UCHAR senseKey = g_last_scsi_sense[2] & 0x0F;
        if (senseKey == 0x02 || senseKey == 0x06) {
            return 1;
        }
    }

    return 0;
}

static void* alloc_io_buffer(SIZE_T size) {
    if (size == 0) {
        return NULL;
    }
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

static void free_io_buffer(void* p) {
    if (p) {
        VirtualFree(p, 0, MEM_RELEASE);
    }
}

static int parse_u32(const char* s, uint32_t* outValue) {
    char* end = NULL;
    unsigned long v = strtoul(s, &end, 0);

    if (s[0] == '\0' || (end && *end != '\0')) {
        return 0;
    }
    if (v > 0xFFFFFFFFul) {
        return 0;
    }

    *outValue = (uint32_t)v;
    return 1;
}

static int is_removable_drive_letter(char letter) {
    char root[] = "X:\\";
    root[0] = letter;
    return (GetDriveTypeA(root) == DRIVE_REMOVABLE);
}

static int find_first_removable_drive(char* outLetter) {
    DWORD mask = GetLogicalDrives();
    if (mask == 0) {
        return 0;
    }

    for (int i = 0; i < 26; ++i) {
        if ((mask & (1u << i)) == 0) {
            continue;
        }

        {
            char letter = (char)('A' + i);
            if (is_removable_drive_letter(letter)) {
                *outLetter = letter;
                return 1;
            }
        }
    }

    return 0;
}

static int get_disk_number_from_drive_letter(char letter, DWORD* outDiskNumber) {
    BYTE extentsBuf[sizeof(VOLUME_DISK_EXTENTS) + sizeof(DISK_EXTENT) * 8];
    DWORD bytesReturned = 0;
    char volumePath[] = "\\\\.\\X:";
    HANDLE hVol;
    BOOL ok;

    volumePath[4] = letter;

    hVol = CreateFileA(
        volumePath,
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    if (hVol == INVALID_HANDLE_VALUE) {
        return 0;
    }

    ok = DeviceIoControl(
        hVol,
        IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
        NULL,
        0,
        extentsBuf,
        (DWORD)sizeof(extentsBuf),
        &bytesReturned,
        NULL
    );
    CloseHandle(hVol);

    if (!ok) {
        return 0;
    }

    {
        VOLUME_DISK_EXTENTS* extents = (VOLUME_DISK_EXTENTS*)extentsBuf;
        if (extents->NumberOfDiskExtents < 1) {
            return 0;
        }
        *outDiskNumber = extents->Extents[0].DiskNumber;
    }

    return 1;
}

static HANDLE try_open_device_path(const char* path, DWORD desiredAccess, const char* label) {
    HANDLE h = CreateFileA(
        path,
        desiredAccess,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (h != INVALID_HANDLE_VALUE) {
        printf("Using %s: %s (desired_access=0x%08lX)\n", label, path, (unsigned long)desiredAccess);
    }

    return h;
}

static HANDLE open_scsi_target(char letter, DWORD diskNumber) {
    char volumePath[] = "\\\\.\\X:";
    HANDLE h;

    volumePath[4] = letter;

    h = try_open_device_path(volumePath, GENERIC_READ | GENERIC_WRITE, "volume device");
    if (h != INVALID_HANDLE_VALUE) {
        return h;
    }

    h = try_open_device_path(volumePath, GENERIC_READ, "volume device");
    if (h != INVALID_HANDLE_VALUE) {
        return h;
    }

    h = try_open_device_path(volumePath, 0, "volume device");
    if (h != INVALID_HANDLE_VALUE) {
        return h;
    }

    if (diskNumber != INVALID_DISK_NUMBER) {
        char physicalPath[64];
        snprintf(physicalPath, sizeof(physicalPath), "\\\\.\\PhysicalDrive%lu", (unsigned long)diskNumber);

        h = try_open_device_path(physicalPath, GENERIC_READ | GENERIC_WRITE, "physical device");
        if (h != INVALID_HANDLE_VALUE) {
            return h;
        }

        h = try_open_device_path(physicalPath, GENERIC_READ, "physical device");
        if (h != INVALID_HANDLE_VALUE) {
            return h;
        }

        h = try_open_device_path(physicalPath, 0, "physical device");
        if (h != INVALID_HANDLE_VALUE) {
            return h;
        }
    }

    return INVALID_HANDLE_VALUE;
}

static int query_scsi_address(HANDLE hDevice, SCSI_ADDRESS* outAddr) {
    DWORD bytesReturned = 0;

    ZeroMemory(outAddr, sizeof(*outAddr));
    outAddr->Length = sizeof(*outAddr);

    return DeviceIoControl(
        hDevice,
        IOCTL_SCSI_GET_ADDRESS,
        NULL,
        0,
        outAddr,
        (DWORD)sizeof(*outAddr),
        &bytesReturned,
        NULL
    );
}

static int scsi_pt_read(
    HANDLE hDisk,
    const uint8_t* cdb,
    UCHAR cdbLen,
    UCHAR senseLen,
    void* data,
    DWORD dataLen,
    DWORD timeoutSec,
    DWORD* outDataTransferred
) {
    SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER swb;
    SCSI_ADDRESS addr;
    DWORD bytesReturned = 0;
    BOOL ok;

    ZeroMemory(&swb, sizeof(swb));
    swb.sptd.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
    swb.sptd.CdbLength = cdbLen;
    swb.sptd.SenseInfoLength = senseLen;
    swb.sptd.DataIn = SCSI_IOCTL_DATA_IN;
    swb.sptd.DataTransferLength = dataLen;
    swb.sptd.TimeOutValue = timeoutSec;
    swb.sptd.DataBuffer = data;
    swb.sptd.SenseInfoOffset = (ULONG)offsetof(SCSI_PASS_THROUGH_DIRECT_WITH_BUFFER, sense);
    memcpy(swb.sptd.Cdb, cdb, cdbLen);

    if (query_scsi_address(hDisk, &addr)) {
        swb.sptd.PathId = addr.PathId;
        swb.sptd.TargetId = addr.TargetId;
        swb.sptd.Lun = addr.Lun;
    }

    ok = DeviceIoControl(
        hDisk,
        IOCTL_SCSI_PASS_THROUGH_DIRECT,
        &swb,
        (DWORD)sizeof(swb),
        &swb,
        (DWORD)sizeof(swb),
        &bytesReturned,
        NULL
    );

    if (!ok) {
        clear_last_scsi_error();
        g_last_scsi_winerr = GetLastError();
        return 0;
    }

    if (swb.sptd.ScsiStatus != 0) {
        clear_last_scsi_error();
        g_last_scsi_status = swb.sptd.ScsiStatus;
        memcpy(g_last_scsi_sense, swb.sense, SENSE_BUF_LEN);
        return 0;
    }

    clear_last_scsi_error();
    if (outDataTransferred) {
        *outDataTransferred = swb.sptd.DataTransferLength;
    }
    return 1;
}

static int scsi_inquiry(HANDLE hDisk) {
    uint8_t cdb[6] = {0};
    uint8_t* data = (uint8_t*)alloc_io_buffer(96);
    char vendor[9] = {0};
    char product[17] = {0};

    if (!data) {
        return 0;
    }

    ZeroMemory(data, 96);
    cdb[0] = 0x12;
    cdb[4] = 96;

    if (!scsi_pt_read(hDisk, cdb, 6, SENSE_BUF_LEN, data, 96, 5, NULL)) {
        free_io_buffer(data);
        return 0;
    }

    memcpy(vendor, &data[8], 8);
    memcpy(product, &data[16], 16);
    free_io_buffer(data);

    printf("SCSI INQUIRY: vendor='%s' product='%s'\n", vendor, product);
    return 1;
}

static uint32_t be32_to_u32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           ((uint32_t)p[3]);
}

static int scsi_read_capacity10(HANDLE hDisk, uint32_t* outLastLba, uint32_t* outBlockSize) {
    uint8_t cdb[10] = {0};
    uint8_t* data = (uint8_t*)alloc_io_buffer(8);

    if (!data) {
        return 0;
    }

    ZeroMemory(data, 8);
    cdb[0] = 0x25;

    if (!scsi_pt_read(hDisk, cdb, 10, SENSE_BUF_LEN, data, 8, 10, NULL)) {
        free_io_buffer(data);
        return 0;
    }

    *outLastLba = be32_to_u32(&data[0]);
    *outBlockSize = be32_to_u32(&data[4]);
    free_io_buffer(data);
    return 1;
}

static int scsi_receive_vendor98(HANDLE hDisk, uint8_t* pData, DWORD length, DWORD* outBytesRead) {
    uint8_t cdb[12] = {0};

    cdb[0] = 0x98;

    return scsi_pt_read(
        hDisk,
        cdb,
        12,
        24,
        pData,
        length,
        2,
        outBytesRead
    );
}

static int reconnect_scsi_target(
    char letter,
    DWORD diskNumber,
    HANDLE* inoutDisk,
    uint32_t* outBlockSize
) {
    if (*inoutDisk != INVALID_HANDLE_VALUE) {
        CloseHandle(*inoutDisk);
        *inoutDisk = INVALID_HANDLE_VALUE;
    }

    fprintf(stderr, "SCSI link lost, reconnecting...\n");

    for (int attempt = 1; attempt <= SCSI_RECONNECT_MAX_ATTEMPTS; ++attempt) {
        uint32_t lastLba = 0;
        uint32_t blockSize = 0;
        HANDLE h = open_scsi_target(letter, diskNumber);

        if (h == INVALID_HANDLE_VALUE) {
            Sleep(SCSI_RECONNECT_DELAY_MS);
            continue;
        }

        if (!scsi_inquiry(h)) {
            CloseHandle(h);
            Sleep(SCSI_RECONNECT_DELAY_MS);
            continue;
        }

        if (!scsi_read_capacity10(h, &lastLba, &blockSize)) {
            blockSize = DEFAULT_BLOCK_SIZE;
        }

        *inoutDisk = h;
        *outBlockSize = blockSize;
        fprintf(stderr,
                "SCSI reconnected on attempt %d. block_size=%lu\n",
                attempt,
                (unsigned long)blockSize);
        return 1;
    }

    fprintf(stderr, "SCSI reconnect failed after %d attempts.\n", SCSI_RECONNECT_MAX_ATTEMPTS);
    return 0;
}

static void print_hex_ascii(const uint8_t* buf, DWORD size, DWORD maxPrint) {
    DWORD n = (size < maxPrint) ? size : maxPrint;

    printf("HEX (%lu bytes shown):\n", (unsigned long)n);
    for (DWORD i = 0; i < n; ++i) {
        printf("%02X ", buf[i]);
        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }
    if (n % 16 != 0) {
        printf("\n");
    }

    printf("ASCII:\n");
    for (DWORD i = 0; i < n; ++i) {
        putchar(isprint(buf[i]) ? buf[i] : '.');
    }
    printf("\n");
}

static int monitor_scsi_stream_output(
    HANDLE* inoutDisk,
    char letter,
    DWORD diskNumber,
    DWORD packetBytes,
    DWORD pollMs,
    uint32_t* inoutBlockSize
) {
    uint8_t* buf = (uint8_t*)alloc_io_buffer(packetBytes);
    unsigned long pollCount = 0;

    if (!buf) {
        fprintf(stderr, "alloc_io_buffer failed for stream monitor.\n");
        return 0;
    }

    printf("Stream mode only. packet_bytes=%lu poll_ms=%lu\n",
           (unsigned long)packetBytes,
           (unsigned long)pollMs);
    printf("Monitoring stream output... press Ctrl+C to stop.\n");

    while (InterlockedCompareExchange(&g_stop, 0, 0) == 0) {
        DWORD nBytesRead = 0;
        ++pollCount;

        if (!scsi_receive_vendor98(*inoutDisk, buf, packetBytes, &nBytesRead)) {
            print_last_scsi_error("receive(0x98)");

            if (ENABLE_SCSI_RECONNECT && is_recoverable_scsi_error()) {
                if (reconnect_scsi_target(letter, diskNumber, inoutDisk, inoutBlockSize)) {
                    if (!scsi_receive_vendor98(*inoutDisk, buf, packetBytes, &nBytesRead)) {
                        print_last_scsi_error("receive(0x98) retry");
                    } else if (nBytesRead > 0) {
                        printf("[poll %lu] stream bytes=%lu\n", pollCount, (unsigned long)nBytesRead);
                        print_hex_ascii(buf, nBytesRead, 256);
                    }
                }
            } else if (!ENABLE_SCSI_RECONNECT && is_recoverable_scsi_error() && !g_reconnect_mask_notice_printed) {
                fprintf(stderr, "SCSI reconnect is masked (ENABLE_SCSI_RECONNECT=0).\n");
                g_reconnect_mask_notice_printed = 1;
            }

            Sleep(pollMs);
            continue;
        }

        if (nBytesRead > 0) {
            printf("[poll %lu] stream bytes=%lu\n", pollCount, (unsigned long)nBytesRead);
            print_hex_ascii(buf, nBytesRead, 256);
        }

        Sleep(pollMs);
    }

    free_io_buffer(buf);
    return 1;
}

int main(int argc, char** argv) {
    uint32_t pollMs = DEFAULT_POLL_MS;
    uint32_t packetBlocks = DEFAULT_PACKET_BLOCKS;
    uint32_t blockSize = DEFAULT_BLOCK_SIZE;
    uint64_t packetBytes64;
    DWORD packetBytes;
    char letter = 0;
    DWORD diskNumber = INVALID_DISK_NUMBER;
    HANDLE hDisk = INVALID_HANDLE_VALUE;
    uint32_t lastLba = 0;

    if (argc > 1 && !parse_u32(argv[1], &pollMs)) {
        fprintf(stderr, "Invalid poll_ms: %s\n", argv[1]);
        return 1;
    }
    if (argc > 2 && !parse_u32(argv[2], &packetBlocks)) {
        fprintf(stderr, "Invalid packet_blocks: %s\n", argv[2]);
        return 1;
    }
    if (packetBlocks == 0 || packetBlocks > 65535) {
        fprintf(stderr, "packet_blocks must be in range [1, 65535].\n");
        return 1;
    }

    printf("Usage: %s [poll_ms] [packet_blocks]\n", argv[0]);

    if (!find_first_removable_drive(&letter)) {
        fprintf(stderr, "No removable drive found. Please insert a USB drive.\n");
        return 1;
    }

    printf("Using removable drive letter: %c:\\\n", letter);

    if (!get_disk_number_from_drive_letter(letter, &diskNumber)) {
        fprintf(stderr, "Warning: failed to map drive letter to disk number, volume-only open will be used.\n");
        diskNumber = INVALID_DISK_NUMBER;
    }

    hDisk = open_scsi_target(letter, diskNumber);
    if (hDisk == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed to open SCSI target.\n");
        return 2;
    }

    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    if (!scsi_inquiry(hDisk)) {
        print_last_scsi_error("SCSI INQUIRY");
        CloseHandle(hDisk);
        return 3;
    }

    if (!scsi_read_capacity10(hDisk, &lastLba, &blockSize)) {
        blockSize = DEFAULT_BLOCK_SIZE;
        fprintf(stderr, "READ CAPACITY(10) failed, fallback block_size=%lu.\n", (unsigned long)blockSize);
    } else {
        printf("READ CAPACITY(10): last_lba=%lu block_size=%lu bytes\n",
               (unsigned long)lastLba,
               (unsigned long)blockSize);
    }

    packetBytes64 = (uint64_t)packetBlocks * (uint64_t)blockSize;
    if (packetBytes64 == 0ull || packetBytes64 > (uint64_t)MAX_PACKET_BYTES) {
        fprintf(stderr, "packet_bytes out of range: %llu (max=%u)\n",
                (unsigned long long)packetBytes64,
                MAX_PACKET_BYTES);
        CloseHandle(hDisk);
        return 4;
    }
    packetBytes = (DWORD)packetBytes64;

    if (!monitor_scsi_stream_output(&hDisk, letter, diskNumber, packetBytes, pollMs, &blockSize)) {
        CloseHandle(hDisk);
        return 5;
    }

    CloseHandle(hDisk);
    printf("Stopped.\n");
    return 0;
}
