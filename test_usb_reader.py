"""
Tests for usb_reader.py
"""

import os
import sys
import time
import tempfile
import threading
import unittest

# Ensure the project root is on the path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from usb_reader import list_files, read_and_print_file, usb_receiver


class TestListFiles(unittest.TestCase):
    def test_empty_directory(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            self.assertEqual(list_files(tmpdir), [])

    def test_single_file(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            fpath = os.path.join(tmpdir, "data.txt")
            with open(fpath, "w") as f:
                f.write("hello")
            result = list_files(tmpdir)
            self.assertEqual(result, [fpath])

    def test_multiple_files_sorted(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            names = ["c.txt", "a.txt", "b.txt"]
            for name in names:
                open(os.path.join(tmpdir, name), "w").close()
            result = list_files(tmpdir)
            self.assertEqual(result, sorted(os.path.join(tmpdir, n) for n in names))

    def test_nested_files(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            subdir = os.path.join(tmpdir, "sub")
            os.makedirs(subdir)
            outer = os.path.join(tmpdir, "outer.txt")
            inner = os.path.join(subdir, "inner.txt")
            for p in (outer, inner):
                open(p, "w").close()
            result = list_files(tmpdir)
            self.assertIn(outer, result)
            self.assertIn(inner, result)


class TestReadAndPrintFile(unittest.TestCase):
    def test_text_file_printed(self):
        with tempfile.NamedTemporaryFile(
            mode="w", suffix=".txt", delete=False
        ) as f:
            f.write("Hello, USB!\n")
            fpath = f.name
        try:
            import io
            from contextlib import redirect_stdout
            buf = io.StringIO()
            with redirect_stdout(buf):
                read_and_print_file(fpath)
            output = buf.getvalue()
            self.assertIn("Hello, USB!", output)
            self.assertIn(fpath, output)
        finally:
            os.unlink(fpath)

    def test_binary_file_hex_dump(self):
        with tempfile.NamedTemporaryFile(
            mode="wb", suffix=".bin", delete=False
        ) as f:
            # Use bytes that are invalid UTF-8 to force the hex-dump path
            f.write(bytes([0x80, 0x81, 0x82, 0x83, 0xFF, 0xFE]))
            fpath = f.name
        try:
            import io
            from contextlib import redirect_stdout
            buf = io.StringIO()
            with redirect_stdout(buf):
                read_and_print_file(fpath)
            output = buf.getvalue()
            # Hex dump should contain '00 01 02 ...'
            self.assertIn("80 81 82", output)
        finally:
            os.unlink(fpath)

    def test_missing_file_warns(self):
        import io
        from contextlib import redirect_stderr
        buf = io.StringIO()
        with redirect_stderr(buf):
            read_and_print_file("/nonexistent/path/file.txt")
        self.assertIn("Warning", buf.getvalue())


class TestUsbReceiver(unittest.TestCase):
    def test_monitors_new_files(self):
        """usb_receiver should print content of files appearing on the drive."""
        with tempfile.TemporaryDirectory() as tmpdir:
            output_lines = []

            def fake_print(*args, **kwargs):
                output_lines.append(" ".join(str(a) for a in args))

            import builtins
            original_print = builtins.print

            results = {"printed": False}

            def run_receiver():
                builtins.print = fake_print
                try:
                    # Run for a short time, then stop via mount removal
                    usb_receiver(mount_point=tmpdir, interval=0.1)
                finally:
                    builtins.print = original_print

            # Write a file before starting
            fpath = os.path.join(tmpdir, "test.txt")
            with open(fpath, "w") as f:
                f.write("USB data line 1\n")

            t = threading.Thread(target=run_receiver, daemon=True)
            t.start()

            # Give the receiver time to pick up the file
            time.sleep(0.5)

            # Verify the content was printed
            combined = "\n".join(output_lines)
            self.assertIn("USB data line 1", combined)

            # Stop by removing the mount point contents so the loop exits
            # (The daemon thread will be killed when the test process ends)

    def test_detects_updated_file(self):
        """usb_receiver should re-print a file when it is modified."""
        with tempfile.TemporaryDirectory() as tmpdir:
            output_lines = []

            def fake_print(*args, **kwargs):
                output_lines.append(" ".join(str(a) for a in args))

            import builtins
            original_print = builtins.print

            fpath = os.path.join(tmpdir, "data.txt")
            with open(fpath, "w") as f:
                f.write("version 1\n")

            def run_receiver():
                builtins.print = fake_print
                try:
                    usb_receiver(mount_point=tmpdir, interval=0.1)
                finally:
                    builtins.print = original_print

            t = threading.Thread(target=run_receiver, daemon=True)
            t.start()

            time.sleep(0.3)

            # Update the file
            time.sleep(0.05)
            with open(fpath, "w") as f:
                f.write("version 2\n")
            # Force mtime change
            os.utime(fpath, (time.time() + 1, time.time() + 1))

            time.sleep(0.4)

            combined = "\n".join(output_lines)
            self.assertIn("version 1", combined)
            self.assertIn("version 2", combined)


if __name__ == "__main__":
    unittest.main()
