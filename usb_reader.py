"""
持续接收来自U盘的数据并打印出来
Continuously receive data from a USB flash drive and print it out.
"""

import os
import sys
import time
import platform


def find_usb_mount_point():
    """
    Attempt to find a mounted USB flash drive.
    Returns the mount point path, or None if not found.
    """
    system = platform.system()

    if system == "Linux":
        # Common Linux USB mount points
        candidates = ["/media", "/mnt"]
        for base in candidates:
            if os.path.isdir(base):
                for entry in os.listdir(base):
                    full = os.path.join(base, entry)
                    if os.path.isdir(full):
                        return full
        # Check /media/<user>/<device> pattern
        media_user = "/media"
        if os.path.isdir(media_user):
            for user in os.listdir(media_user):
                user_path = os.path.join(media_user, user)
                if os.path.isdir(user_path):
                    for device in os.listdir(user_path):
                        device_path = os.path.join(user_path, device)
                        if os.path.isdir(device_path):
                            return device_path

    elif system == "Darwin":
        # macOS USB drives appear under /Volumes.
        # Exclude well-known system volumes.
        system_volumes = {"Macintosh HD", "Macintosh HD - Data", "Recovery", "VM"}
        volumes = "/Volumes"
        if os.path.isdir(volumes):
            for vol in os.listdir(volumes):
                if vol in system_volumes:
                    continue
                full = os.path.join(volumes, vol)
                if os.path.isdir(full):
                    return full

    elif system == "Windows":
        import string
        # Start from D: to skip A: (floppy), B: (floppy) and C: (system drive)
        for letter in string.ascii_uppercase[3:]:
            drive = f"{letter}:\\"
            if os.path.exists(drive):
                return drive

    return None


def list_files(mount_point):
    """Return all file paths under mount_point recursively."""
    file_paths = []
    for root, _dirs, files in os.walk(mount_point):
        for name in sorted(files):
            file_paths.append(os.path.join(root, name))
    return file_paths


def read_and_print_file(filepath):
    """Read a file and print its contents."""
    try:
        with open(filepath, "rb") as f:
            data = f.read()
        print(f"--- {filepath} ({len(data)} bytes) ---")
        try:
            text = data.decode("utf-8")
            print(text)
        except UnicodeDecodeError:
            # Binary file: print hex dump
            hex_lines = [
                " ".join(f"{b:02x}" for b in data[i:i + 16])
                for i in range(0, len(data), 16)
            ]
            print("\n".join(hex_lines))
        print()
    except OSError as e:
        print(f"[Warning] Cannot read {filepath}: {e}", file=sys.stderr)


def usb_receiver(mount_point=None, interval=2.0):
    """
    Continuously monitor a USB flash drive and print any data it contains.

    :param mount_point: Path to the USB mount point. Auto-detected if None.
    :param interval: Polling interval in seconds between scans.
    """
    if mount_point is None:
        print("Searching for USB flash drive...", flush=True)
        while True:
            mount_point = find_usb_mount_point()
            if mount_point:
                print(f"Found USB at: {mount_point}", flush=True)
                break
            print("No USB found. Retrying...", flush=True)
            time.sleep(interval)

    seen_files = {}   # filepath -> last modified time

    print(f"Monitoring USB at: {mount_point}", flush=True)
    print("Press Ctrl+C to stop.\n", flush=True)

    try:
        while True:
            if not os.path.isdir(mount_point):
                print(
                    f"[Warning] Mount point {mount_point} is no longer available.",
                    file=sys.stderr,
                )
                break

            current_files = list_files(mount_point)
            current_set = set(current_files)

            # Remove entries for files that have been deleted
            for gone in list(seen_files.keys()):
                if gone not in current_set:
                    del seen_files[gone]

            for filepath in current_files:
                try:
                    mtime = os.path.getmtime(filepath)
                except OSError:
                    continue

                if filepath not in seen_files or seen_files[filepath] != mtime:
                    seen_files[filepath] = mtime
                    read_and_print_file(filepath)

            time.sleep(interval)

    except KeyboardInterrupt:
        print("\nStopped monitoring USB.", flush=True)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        description="持续接收来自U盘的数据并打印出来 - "
                    "Continuously receive data from a USB flash drive and print it."
    )
    parser.add_argument(
        "--mount-point",
        default=None,
        help="Path to the USB mount point (auto-detected if not specified).",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=2.0,
        help="Polling interval in seconds (default: 2.0).",
    )
    args = parser.parse_args()
    usb_receiver(mount_point=args.mount_point, interval=args.interval)
