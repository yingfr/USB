# USB
U盘读写

## usb_scsi_detect — SCSI透传方式检测U盘（Linux）

`usb_scsi_detect.c` 使用 Linux `SG_IO` ioctl 向 USB 大容量存储设备发送原始 SCSI 命令，
无需依赖任何第三方库，直接输出设备的详细信息。

### 检测内容

| 命令 | 说明 |
|---|---|
| INQUIRY（标准） | 设备类型、厂商、产品名、固件版本、是否可移除 |
| INQUIRY VPD 0x00 | 设备支持的 VPD 页列表 |
| INQUIRY VPD 0x80 | 序列号 |
| INQUIRY VPD 0x83 | 设备标识符 |
| READ CAPACITY (10) | 容量（块数、块大小、总 GB） |
| MODE SENSE (6) | 写保护状态、介质类型、模式页原始数据 |

### 编译

```bash
make
# 或手动编译
gcc -Wall -Wextra -O2 -std=c11 -o usb_scsi_detect usb_scsi_detect.c
```

### 运行

```bash
# 使用设备文件（需要 root 或 disk 组权限）
sudo ./usb_scsi_detect /dev/sdb
sudo ./usb_scsi_detect /dev/sg1
```

### 示例输出

```
SCSI passthrough detection for: /dev/sdb

=== STANDARD INQUIRY ===
  Peripheral type   : 0x00 (Direct-access block device (disk))
  Removable         : Yes
  SCSI version      : 0x06
  Vendor            : SanDisk
  Product           : Ultra
  Revision          : 1.00

=== VPD SUPPORTED PAGES (0x00) ===
  Supported pages: 0x00 0x80 0x83

=== VPD UNIT SERIAL NUMBER (0x80) ===
  Serial number     : AA01234567890

=== VPD DEVICE IDENTIFICATION (0x83) ===
  Descriptor 1 (code_set=0x2):
    SanDisk Ultra

=== READ CAPACITY (10) ===
  Last LBA              : 60063743
  Block size            : 512 bytes
  Total blocks          : 60063744 blocks
  Capacity              : 30.75 GB  (28.64 GiB)

=== MODE SENSE (6) ===
  Medium type           : 0x00
  Write-protected       : No
  Block desc. length    : 8 bytes
  Block count           : 0
  Block size            : 512 bytes

Done.
```

### 平台要求

- Linux（需要内核支持 `scsi/sg.h`，通常已内置）
- 设备文件 `/dev/sdX` 或 `/dev/sgN`（需要相应读权限）
- 适用于所有通过 USB Mass Storage 协议工作的 U 盘
