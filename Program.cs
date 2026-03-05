using System;
using System.IO;
using System.Linq;

namespace USBTool
{
    class Program
    {
        static void Main(string[] args)
        {
            Console.OutputEncoding = System.Text.Encoding.UTF8;
            Console.WriteLine("============================");
            Console.WriteLine("  USB 读写工具 (USB R/W Tool)");
            Console.WriteLine("============================");
            Console.WriteLine();

            while (true)
            {
                Console.WriteLine("请选择操作 / Please select an operation:");
                Console.WriteLine("  1. 列出所有 USB 驱动器 / List all USB drives");
                Console.WriteLine("  2. 读取文件 / Read file from USB");
                Console.WriteLine("  3. 写入文件 / Write file to USB");
                Console.WriteLine("  4. 列出 USB 目录内容 / List USB directory contents");
                Console.WriteLine("  0. 退出 / Exit");
                Console.Write("> ");

                string? choice = Console.ReadLine()?.Trim();
                Console.WriteLine();

                switch (choice)
                {
                    case "1":
                        ListUsbDrives();
                        break;
                    case "2":
                        ReadFileFromUsb();
                        break;
                    case "3":
                        WriteFileToUsb();
                        break;
                    case "4":
                        ListUsbDirectoryContents();
                        break;
                    case "0":
                        Console.WriteLine("再见 / Goodbye!");
                        return;
                    default:
                        Console.WriteLine("无效选项，请重试。/ Invalid option, please try again.");
                        break;
                }

                Console.WriteLine();
            }
        }

        static DriveInfo[] GetUsbDrives()
        {
            return DriveInfo.GetDrives()
                .Where(d => d.DriveType == DriveType.Removable && d.IsReady)
                .ToArray();
        }

        static void ListUsbDrives()
        {
            var drives = GetUsbDrives();
            if (drives.Length == 0)
            {
                Console.WriteLine("未检测到 USB 驱动器。/ No USB drives detected.");
                return;
            }

            Console.WriteLine("检测到以下 USB 驱动器 / Detected USB drives:");
            foreach (var drive in drives)
            {
                Console.WriteLine($"  驱动器 / Drive: {drive.Name}");
                Console.WriteLine($"    卷标 / Label:       {drive.VolumeLabel}");
                Console.WriteLine($"    格式 / Format:      {drive.DriveFormat}");
                Console.WriteLine($"    总容量 / Total:     {FormatBytes(drive.TotalSize)}");
                Console.WriteLine($"    可用空间 / Free:    {FormatBytes(drive.AvailableFreeSpace)}");
                Console.WriteLine();
            }
        }

        static void ReadFileFromUsb()
        {
            var drives = GetUsbDrives();
            if (drives.Length == 0)
            {
                Console.WriteLine("未检测到 USB 驱动器。/ No USB drives detected.");
                return;
            }

            Console.Write("输入 USB 上的文件完整路径 / Enter the full file path on USB: ");
            string? filePath = Console.ReadLine()?.Trim();
            if (string.IsNullOrEmpty(filePath))
            {
                Console.WriteLine("路径不能为空。/ Path cannot be empty.");
                return;
            }

            try
            {
                if (!File.Exists(filePath))
                {
                    Console.WriteLine($"文件不存在 / File not found: {filePath}");
                    return;
                }

                string content = File.ReadAllText(filePath);
                Console.WriteLine($"--- 文件内容 / File content: {filePath} ---");
                Console.WriteLine(content);
                Console.WriteLine("--- 结束 / End ---");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"读取失败 / Read failed: {ex.Message}");
            }
        }

        static void WriteFileToUsb()
        {
            var drives = GetUsbDrives();
            if (drives.Length == 0)
            {
                Console.WriteLine("未检测到 USB 驱动器。/ No USB drives detected.");
                return;
            }

            Console.Write("输入目标文件路径 / Enter the target file path on USB: ");
            string? filePath = Console.ReadLine()?.Trim();
            if (string.IsNullOrEmpty(filePath))
            {
                Console.WriteLine("路径不能为空。/ Path cannot be empty.");
                return;
            }

            Console.WriteLine("输入要写入的内容（输入空行结束）/ Enter content to write (empty line to finish):");
            var lines = new System.Collections.Generic.List<string>();
            string? line;
            while (!string.IsNullOrEmpty(line = Console.ReadLine()))
            {
                lines.Add(line);
            }
            string content = string.Join(Environment.NewLine, lines);

            try
            {
                string? directory = Path.GetDirectoryName(filePath);
                if (!string.IsNullOrEmpty(directory) && !Directory.Exists(directory))
                {
                    Directory.CreateDirectory(directory);
                }

                File.WriteAllText(filePath, content, System.Text.Encoding.UTF8);
                Console.WriteLine($"写入成功 / Write successful: {filePath}");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"写入失败 / Write failed: {ex.Message}");
            }
        }

        static void ListUsbDirectoryContents()
        {
            var drives = GetUsbDrives();
            if (drives.Length == 0)
            {
                Console.WriteLine("未检测到 USB 驱动器。/ No USB drives detected.");
                return;
            }

            Console.Write("输入 USB 目录路径（留空则列出根目录）/ Enter USB directory path (empty for root): ");
            string? dirPath = Console.ReadLine()?.Trim();

            if (string.IsNullOrEmpty(dirPath))
            {
                if (drives.Length == 1)
                {
                    dirPath = drives[0].RootDirectory.FullName;
                }
                else
                {
                    Console.WriteLine("检测到多个 USB 驱动器，请指定路径。/ Multiple USB drives detected, please specify a path.");
                    return;
                }
            }

            try
            {
                if (!Directory.Exists(dirPath))
                {
                    Console.WriteLine($"目录不存在 / Directory not found: {dirPath}");
                    return;
                }

                Console.WriteLine($"目录内容 / Contents of {dirPath}:");

                var directories = Directory.GetDirectories(dirPath);
                foreach (var dir in directories)
                {
                    Console.WriteLine($"  [目录/DIR] {Path.GetFileName(dir)}");
                }

                var files = Directory.GetFiles(dirPath);
                foreach (var file in files)
                {
                    var fileInfo = new FileInfo(file);
                    Console.WriteLine($"  [文件/FILE] {fileInfo.Name}  ({FormatBytes(fileInfo.Length)})  {fileInfo.LastWriteTime:yyyy-MM-dd HH:mm:ss}");
                }

                Console.WriteLine($"共 / Total: {directories.Length} 个目录/dirs, {files.Length} 个文件/files");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"列出失败 / List failed: {ex.Message}");
            }
        }

        static string FormatBytes(long bytes)
        {
            string[] suffixes = { "B", "KB", "MB", "GB", "TB" };
            int i = 0;
            double size = bytes;
            while (size >= 1024 && i < suffixes.Length - 1)
            {
                size /= 1024;
                i++;
            }
            return $"{size:0.##} {suffixes[i]}";
        }
    }
}
