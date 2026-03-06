# USB
U盘读写 — 持续接收来自U盘的数据并打印出来

## 功能
- 自动检测挂载的U盘
- 持续监控U盘，发现新文件或文件内容变化时立即读取并打印
- 支持文本文件（UTF-8 输出）和二进制文件（十六进制转储）
- 支持 Linux、macOS 和 Windows

## 使用方法

```bash
# 自动检测U盘并开始监控
python usb_reader.py

# 指定U盘挂载点
python usb_reader.py --mount-point /media/myusb

# 指定轮询间隔（秒，默认 2.0）
python usb_reader.py --interval 1.0
```

按 `Ctrl+C` 停止监控。

## 运行测试

```bash
python -m unittest test_usb_reader -v
```
