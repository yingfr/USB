# USB

Windows 平台下的 U 盘 SCSI pass-through 轮询程序。

程序会自动找到第一个可移动驱动器，尝试打开卷设备或物理设备，然后执行：

- `SCSI INQUIRY (0x12)`：读取厂商和产品信息
- `READ CAPACITY(10) (0x25)`：读取块大小与最后 LBA
- `VENDOR RECEIVE (0x98)`：按周期拉取数据并打印 HEX/ASCII

按 `Ctrl+C` 可优雅退出。

## 文件说明

| 文件 | 说明 |
|------|------|
| `usb_rw_windows.c` | 主程序（设备发现、SCSI 命令发送、轮询打印） |
| `Makefile` | 使用 MinGW gcc 构建可执行文件 |

## 构建

### 使用 Makefile（推荐）

```bat
make
```

Windows 下默认使用 `gcc`，Linux 下默认使用 `x86_64-w64-mingw32-gcc` 交叉编译。

### 手动编译（MinGW）

```bat
gcc -Wall -Wextra -O2 usb_rw_windows.c -o usb_rw_windows.exe
```

### 手动编译（MSVC）

```bat
cl /W4 /O2 usb_rw_windows.c /Fe:usb_rw_windows.exe
```

## 运行

```bat
usb_rw_windows.exe [poll_ms] [packet_blocks]
```

参数说明：

- `poll_ms`：轮询间隔（毫秒），默认 `200`
- `packet_blocks`：每次请求的块数，默认 `8`，范围 `[1, 65535]`

程序会根据 `READ CAPACITY(10)` 返回的块大小计算：

- `packet_bytes = packet_blocks * block_size`
- 最大不超过 `4 MiB`
- 若 `READ CAPACITY(10)` 失败，块大小回退到 `512`

## 运行流程

1. 扫描逻辑盘符并选取第一个 `DRIVE_REMOVABLE`
2. 通过 `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS` 尝试映射 `PhysicalDrive`
3. 优先打开 `\\.\X:`，失败再尝试 `\\.\PhysicalDriveN`
4. 发送 `INQUIRY` 与 `READ CAPACITY(10)`
5. 进入循环，使用 `0x98` 接收数据并打印

## 输出示例

程序会输出类似日志：

- `SCSI INQUIRY: vendor='...' product='...'`
- `READ CAPACITY(10): last_lba=... block_size=... bytes`
- `[poll N] stream bytes=...`
- 随后打印 HEX 和 ASCII 视图（最多展示前 256 字节）

## 常见问题

1. 找不到 U 盘

- 确认 U 盘已挂载并显示为可移动设备。

2. 打开设备失败或 SCSI 命令失败

- 尝试以管理员权限运行。
- 某些设备/驱动不支持 `0x98`，会出现设备错误或空返回。

3. 热插拔后中断

- 当前默认关闭自动重连（`ENABLE_SCSI_RECONNECT = 0`）。
- 如需实验重连，可在源码中开启该宏后重新编译。

## 说明

这是一个面向调试和验证的低层示例，不是通用 U 盘文件系统读写工具。
