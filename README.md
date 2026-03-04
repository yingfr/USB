# USB

Windows 平台 U 盘读写示例程序，使用 Win32 API（`CreateFile` / `ReadFile` / `WriteFile`）对可移动存储设备进行文件读写。

## 文件说明

| 文件 | 说明 |
|------|------|
| `usb_rw_windows.c` | 主程序：自动找到第一个可移动驱动器，写入测试文件并回读验证 |

## 编译方式

### MSVC (cl)

```bat
cl usb_rw_windows.c /Fe:usb_rw_windows.exe
```

### MinGW (gcc)

```bat
gcc usb_rw_windows.c -o usb_rw_windows.exe
```

## 运行注意事项

1. **需要插入 U 盘**：程序会自动扫描驱动器列表，找到第一个类型为 `DRIVE_REMOVABLE` 的驱动器。
2. **写入文件**：程序会在 U 盘根目录写入 `usb_test.txt` 并回读验证内容。
3. **管理员权限**：通常不需要，但如果 U 盘有写保护或访问受限，请以管理员身份运行。
4. **仅支持 Windows**：代码依赖 Win32 API，不支持 Linux/macOS。
