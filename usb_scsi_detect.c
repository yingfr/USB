/*
 * usb_scsi_detect.c
 *
 * 采用SCSI透传方式检测U盘信息（Linux）
 * Detect USB drive information via SCSI passthrough on Linux.
 *
 * Uses the Linux SG_IO ioctl to send raw SCSI commands to a USB mass-storage
 * device exposed as a SCSI generic (/dev/sgN) or block device (/dev/sdX).
 *
 * Supported SCSI commands:
 *   - INQUIRY (standard)                     — vendor / product / revision
 *   - INQUIRY VPD page 0x00                  — supported VPD pages
 *   - INQUIRY VPD page 0x80                  — unit serial number
 *   - INQUIRY VPD page 0x83                  — device identification
 *   - READ CAPACITY (10)                     — disk size / block size
 *   - MODE SENSE (6)                         — device mode parameters
 *
 * Build:
 *   gcc -Wall -Wextra -O2 -o usb_scsi_detect usb_scsi_detect.c
 *
 * Usage:
 *   sudo ./usb_scsi_detect /dev/sdX
 *   sudo ./usb_scsi_detect /dev/sgN
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* The kernel SG_IO header is in <scsi/sg.h> on most distros */
#include <scsi/sg.h>

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define SCSI_TIMEOUT_MS   5000   /* ms */
#define SENSE_BUF_LEN     32
#define MAX_INQUIRY_LEN   96
#define MAX_MODESENSE_LEN 192
#define MAX_VPD_LEN       252

/* SCSI command operation codes */
#define SCSI_OP_INQUIRY        0x12
#define SCSI_OP_READ_CAPACITY  0x25
#define SCSI_OP_MODE_SENSE6    0x1A

/* ------------------------------------------------------------------ */
/* sg_io_hdr helper                                                    */
/* ------------------------------------------------------------------ */

/*
 * Execute a SCSI command via SG_IO.
 *
 * fd       - open file descriptor to /dev/sdX or /dev/sgN
 * cdb      - SCSI Command Descriptor Block
 * cdb_len  - length of CDB in bytes
 * data_buf - data buffer (IN for DATA_IN direction)
 * data_len - size of data buffer
 * sense    - sense data buffer (at least SENSE_BUF_LEN bytes)
 *
 * Returns 0 on success, -1 on ioctl error, or the SCSI status byte on
 * SCSI-level errors.
 */
static int sg_exec(int fd,
                   const uint8_t *cdb, uint8_t cdb_len,
                   uint8_t *data_buf, uint32_t data_len,
                   uint8_t *sense)
{
    sg_io_hdr_t io_hdr;
    uint8_t local_sense[SENSE_BUF_LEN];

    memset(&io_hdr, 0, sizeof(io_hdr));
    io_hdr.interface_id    = 'S';
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.cmdp            = (unsigned char *)cdb;
    io_hdr.cmd_len         = cdb_len;
    io_hdr.dxferp          = data_buf;
    io_hdr.dxfer_len       = data_len;
    io_hdr.sbp             = sense ? sense : local_sense;
    io_hdr.mx_sb_len       = SENSE_BUF_LEN;
    io_hdr.timeout         = SCSI_TIMEOUT_MS;

    if (ioctl(fd, SG_IO, &io_hdr) < 0) {
        perror("SG_IO ioctl");
        return -1;
    }

    /* Check host / driver status */
    if (io_hdr.host_status != 0 || io_hdr.driver_status != 0) {
        return -1;
    }

    return (int)(io_hdr.status & 0x7e); /* SCSI status (0 = GOOD) */
}

/* ------------------------------------------------------------------ */
/* Utility                                                             */
/* ------------------------------------------------------------------ */

/* Print a fixed-width string, stripping trailing spaces */
static void print_trimmed(const char *label, const uint8_t *buf, int len)
{
    int end = len;
    while (end > 0 && (buf[end - 1] == ' ' || buf[end - 1] == '\0'))
        --end;
    printf("  %-18s: %.*s\n", label, end, (const char *)buf);
}

/* Print a hex + ASCII dump of a buffer */
static void hexdump(const uint8_t *buf, int len)
{
    for (int i = 0; i < len; i += 16) {
        printf("    %04x  ", i);
        for (int j = 0; j < 16; j++) {
            if (i + j < len)
                printf("%02x ", buf[i + j]);
            else
                printf("   ");
            if (j == 7)
                printf(" ");
        }
        printf(" |");
        for (int j = 0; j < 16 && i + j < len; j++) {
            uint8_t c = buf[i + j];
            printf("%c", (c >= 0x20 && c < 0x7f) ? c : '.');
        }
        printf("|\n");
    }
}

/* ------------------------------------------------------------------ */
/* INQUIRY (standard)                                                  */
/* ------------------------------------------------------------------ */

static void do_inquiry(int fd)
{
    uint8_t cdb[6]  = { SCSI_OP_INQUIRY, 0, 0, 0, MAX_INQUIRY_LEN, 0 };
    uint8_t buf[MAX_INQUIRY_LEN];
    uint8_t sense[SENSE_BUF_LEN];

    memset(buf, 0, sizeof(buf));

    printf("\n=== STANDARD INQUIRY ===\n");

    int rc = sg_exec(fd, cdb, sizeof(cdb), buf, sizeof(buf), sense);
    if (rc != 0) {
        printf("  INQUIRY command failed (rc=%d)\n", rc);
        return;
    }

    uint8_t peripheral_type = buf[0] & 0x1f;
    uint8_t removable       = (buf[1] >> 7) & 0x01;
    uint8_t version         = buf[2];

    const char *type_str;
    switch (peripheral_type) {
    case 0x00: type_str = "Direct-access block device (disk)"; break;
    case 0x05: type_str = "CD/DVD";                            break;
    case 0x07: type_str = "Optical memory device";             break;
    case 0x0e: type_str = "Simplified direct-access (floppy)"; break;
    default:   type_str = "Other";                             break;
    }

    printf("  %-18s: 0x%02x (%s)\n", "Peripheral type",
           peripheral_type, type_str);
    printf("  %-18s: %s\n", "Removable",
           removable ? "Yes" : "No");
    printf("  %-18s: 0x%02x\n", "SCSI version", version);

    print_trimmed("Vendor",          buf + 8,  8);
    print_trimmed("Product",         buf + 16, 16);
    print_trimmed("Revision",        buf + 32, 4);

    /* Additional vendor-specific data (bytes 36-55) */
    int extra = buf[4] + 4;   /* total returned length */
    if (extra > (int)sizeof(buf))
        extra = (int)sizeof(buf);
    if (extra > 36) {
        printf("  %-18s:\n", "Vendor-specific");
        hexdump(buf + 36, extra - 36 < 20 ? extra - 36 : 20);
    }
}

/* ------------------------------------------------------------------ */
/* INQUIRY — VPD pages                                                 */
/* ------------------------------------------------------------------ */

/* Page 0x00: list of supported VPD pages */
static void do_vpd_page00(int fd)
{
    uint8_t cdb[6]  = { SCSI_OP_INQUIRY, 0x01, 0x00, 0, MAX_VPD_LEN, 0 };
    uint8_t buf[MAX_VPD_LEN];
    uint8_t sense[SENSE_BUF_LEN];

    memset(buf, 0, sizeof(buf));

    int rc = sg_exec(fd, cdb, sizeof(cdb), buf, sizeof(buf), sense);
    if (rc != 0) return;

    int n = buf[3];
    printf("\n=== VPD SUPPORTED PAGES (0x00) ===\n");
    printf("  Supported pages:");
    for (int i = 0; i < n && i + 4 < MAX_VPD_LEN; i++)
        printf(" 0x%02x", buf[4 + i]);
    printf("\n");
}

/* Page 0x80: Unit Serial Number */
static void do_vpd_page80(int fd)
{
    uint8_t cdb[6]  = { SCSI_OP_INQUIRY, 0x01, 0x80, 0, MAX_VPD_LEN, 0 };
    uint8_t buf[MAX_VPD_LEN];
    uint8_t sense[SENSE_BUF_LEN];

    memset(buf, 0, sizeof(buf));

    printf("\n=== VPD UNIT SERIAL NUMBER (0x80) ===\n");

    int rc = sg_exec(fd, cdb, sizeof(cdb), buf, sizeof(buf), sense);
    if (rc != 0) {
        printf("  Not supported or failed (rc=%d)\n", rc);
        return;
    }

    int sn_len = buf[3];
    if (sn_len <= 0 || sn_len + 4 > MAX_VPD_LEN) {
        printf("  (empty)\n");
        return;
    }

    printf("  %-18s: %.*s\n", "Serial number", sn_len, (char *)(buf + 4));
}

/* Page 0x83: Device Identification */
static void do_vpd_page83(int fd)
{
    uint8_t cdb[6]  = { SCSI_OP_INQUIRY, 0x01, 0x83, 0, MAX_VPD_LEN, 0 };
    uint8_t buf[MAX_VPD_LEN];
    uint8_t sense[SENSE_BUF_LEN];

    memset(buf, 0, sizeof(buf));

    printf("\n=== VPD DEVICE IDENTIFICATION (0x83) ===\n");

    int rc = sg_exec(fd, cdb, sizeof(cdb), buf, sizeof(buf), sense);
    if (rc != 0) {
        printf("  Not supported or failed (rc=%d)\n", rc);
        return;
    }

    int page_len = (buf[2] << 8) | buf[3];
    int off = 4;
    int idx = 0;

    while (off + 4 <= 4 + page_len && off + 4 < MAX_VPD_LEN) {
        uint8_t code_set    = buf[off] & 0x0f;
        uint8_t id_type     = buf[off + 1] & 0x0f;
        int     id_len      = buf[off + 3];

        (void)id_type; /* suppress unused warning; used for context only */

        printf("  Descriptor %d (code_set=0x%x):\n", ++idx, code_set);

        if (code_set == 1) {        /* Binary */
            hexdump(buf + off + 4, id_len);
        } else {                    /* ASCII / UTF-8 */
            printf("    %.*s\n", id_len, (char *)(buf + off + 4));
        }

        off += 4 + id_len;
    }

    if (idx == 0)
        printf("  (no identification descriptors)\n");
}

/* ------------------------------------------------------------------ */
/* READ CAPACITY (10)                                                  */
/* ------------------------------------------------------------------ */

static void do_read_capacity(int fd)
{
    uint8_t cdb[10] = { SCSI_OP_READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t buf[8];
    uint8_t sense[SENSE_BUF_LEN];

    memset(buf, 0, sizeof(buf));

    printf("\n=== READ CAPACITY (10) ===\n");

    int rc = sg_exec(fd, cdb, sizeof(cdb), buf, sizeof(buf), sense);
    if (rc != 0) {
        printf("  READ CAPACITY failed (rc=%d)\n", rc);
        return;
    }

    uint32_t last_lba  = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16)
                       | ((uint32_t)buf[2] <<  8) |  (uint32_t)buf[3];
    uint32_t block_size = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16)
                        | ((uint32_t)buf[6] <<  8) |  (uint32_t)buf[7];

    uint64_t total_blocks = (uint64_t)last_lba + 1;
    uint64_t total_bytes  = total_blocks * block_size;

    printf("  %-22s: %u\n",   "Last LBA",         last_lba);
    printf("  %-22s: %u bytes\n", "Block size",    block_size);
    printf("  %-22s: %llu blocks\n", "Total blocks",
           (unsigned long long)total_blocks);
    printf("  %-22s: %.2f GB  (%.2f GiB)\n", "Capacity",
           (double)total_bytes / 1e9,
           (double)total_bytes / (1024.0 * 1024.0 * 1024.0));
}

/* ------------------------------------------------------------------ */
/* MODE SENSE (6)                                                      */
/* ------------------------------------------------------------------ */

static void do_mode_sense(int fd)
{
    /* Page code 0x3f = return all mode pages */
    uint8_t cdb[6] = { SCSI_OP_MODE_SENSE6, 0, 0x3f, 0,
                        MAX_MODESENSE_LEN, 0 };
    uint8_t buf[MAX_MODESENSE_LEN];
    uint8_t sense[SENSE_BUF_LEN];

    memset(buf, 0, sizeof(buf));

    printf("\n=== MODE SENSE (6) ===\n");

    int rc = sg_exec(fd, cdb, sizeof(cdb), buf, sizeof(buf), sense);
    if (rc != 0) {
        printf("  MODE SENSE failed (rc=%d)\n", rc);
        return;
    }

    int data_len   = buf[0] + 1;   /* mode data length field + 1 */
    int medium_type = buf[1];
    int wp          = (buf[2] >> 7) & 1;
    int blk_desc_len = buf[3];

    printf("  %-22s: 0x%02x\n", "Medium type",       medium_type);
    printf("  %-22s: %s\n",     "Write-protected",   wp ? "Yes" : "No");
    printf("  %-22s: %d bytes\n","Block desc. length", blk_desc_len);

    /* Parse block descriptor (if present) */
    if (blk_desc_len >= 8) {
        uint32_t num_blocks  = ((uint32_t)buf[4] << 24) |
                               ((uint32_t)buf[5] << 16) |
                               ((uint32_t)buf[6] <<  8) | buf[7];
        uint32_t block_size  = ((uint32_t)buf[9] << 16) |
                               ((uint32_t)buf[10] << 8) | buf[11];
        printf("  %-22s: %u\n",       "Block count",  num_blocks);
        printf("  %-22s: %u bytes\n", "Block size",   block_size);
    }

    /* Raw dump of mode pages */
    if (data_len > 4 + blk_desc_len && data_len <= (int)sizeof(buf)) {
        printf("  Mode pages raw data:\n");
        hexdump(buf + 4 + blk_desc_len,
                data_len - 4 - blk_desc_len);
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr,
                "Usage: %s <device>\n"
                "  e.g. %s /dev/sdb\n"
                "       %s /dev/sg1\n",
                argv[0], argv[0], argv[0]);
        return EXIT_FAILURE;
    }

    const char *dev = argv[1];
    int fd = open(dev, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "Cannot open %s: %s\n", dev, strerror(errno));
        return EXIT_FAILURE;
    }

    /* Verify the file descriptor supports SG_IO */
    int ver = 0;
    if (ioctl(fd, SG_GET_VERSION_NUM, &ver) < 0) {
        /* Not a native sg device — try to use block SG passthrough */
        /* On recent kernels /dev/sdX also supports SG_IO, so just proceed */
    }

    printf("SCSI passthrough detection for: %s\n", dev);
    if (ver > 0)
        printf("sg driver version: %d\n", ver);

    do_inquiry(fd);
    do_vpd_page00(fd);
    do_vpd_page80(fd);
    do_vpd_page83(fd);
    do_read_capacity(fd);
    do_mode_sense(fd);

    printf("\nDone.\n");
    close(fd);
    return EXIT_SUCCESS;
}
