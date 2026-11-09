#!/usr/bin/env python3
"""Boot the REAL Linux ISO, inspect VGA pixels, then open GUI apps via virtual keyboard.

The test intentionally fails if it merely builds a valid ISO or reaches a
kernel console without showing Bordify's actual recognizable desktop.
"""
from __future__ import annotations
from PIL import Image, ImageChops
from pathlib import Path
import os
import socket
import subprocess
import sys
import time

ISO = Path(sys.argv[1]).resolve()
OUT = Path("build/linux")
OUT.mkdir(parents=True, exist_ok=True)
SERIAL = OUT / "qemu-serial.log"
STDERR = OUT / "qemu-stderr.log"
MONITOR = OUT / "qemu-monitor.sock"
TIMEOUT = int(os.environ.get("BORDIFY_QEMU_BOOT_TIMEOUT", "300"))

def qemu_command():
    return ["qemu-system-x86_64", "-machine", "accel=kvm:tcg", "-m", "3072",
            "-smp", "2", "-boot", "d", "-cdrom", str(ISO),
            "-nic", "user,model=e1000", "-vga", "std",
            "-display", "none", "-no-reboot", "-serial", f"file:{SERIAL}",
            "-monitor", f"unix:{MONITOR},server=on,wait=off"]

class Monitor:
    def __init__(self, sock):
        self.sock = sock
        self.sock.settimeout(8)
        self.read_prompt()

    def read_prompt(self):
        buf = b""
        until = time.monotonic() + 12
        while b"(qemu)" not in buf and time.monotonic() < until:
            try:
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                buf += chunk
            except socket.timeout:
                break
        return buf.decode(errors="replace")

    def command(self, command):
        self.sock.sendall((command + "\r\n").encode())
        return self.read_prompt()

    def screenshot(self, name):
        path = (OUT / f"qemu-{name}.ppm").resolve()
        if path.exists():
            path.unlink()
        reply = self.command(f"screendump {path}")
        if "Error" in reply or "error" in reply:
            raise RuntimeError("QEMU screendump failed: " + reply)
        for _ in range(30):
            if path.is_file() and path.stat().st_size > 1000:
                with Image.open(path) as img:
                    return img.convert("RGB")
            time.sleep(.1)
        raise RuntimeError(f"QEMU did not write screenshot: {path}")

def is_bordify(im):
    w, h = im.size
    if w < 700 or h < 550:
        return False
    area = im.crop((50, 146, 107, 203))
    reds = sum(1 for r,g,b in area.getdata()
               if r >= 180 and g < 160 and b < 160)
    # Original Bordify web shortcut paints Chrome's distinctive red circular area.
    if reds < 12:
        return False
    r,g,b = im.getpixel((w//2, h//2))
    return min(r,g,b) > 175

def changed_pixels(a, b):
    if a.size != b.size:
        return 999999
    delta = ImageChops.difference(a, b)
    return sum(pixel != (0,0,0) for pixel in delta.getdata())

def run():
    if not ISO.is_file() or ISO.stat().st_size < 2_000_000:
        raise RuntimeError("ISO missing or unexpectedly small")
    MONITOR.unlink(missing_ok=True)
    with STDERR.open("wb") as errs:
        vm = subprocess.Popen(qemu_command(), stdout=subprocess.DEVNULL, stderr=errs)
        try:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline and not MONITOR.exists():
                if vm.poll() is not None:
                    raise RuntimeError(f"QEMU exited early ({vm.returncode})")
                time.sleep(.3)
            if not MONITOR.exists():
                raise RuntimeError("QEMU monitor socket did not start")
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
                sock.connect(str(MONITOR))
                mon = Monitor(sock)
                deadline = time.monotonic() + TIMEOUT
                last = None
                desktop = None
                while time.monotonic() < deadline:
                    if vm.poll() is not None:
                        raise RuntimeError(f"QEMU exited while booting ({vm.returncode})")
                    last = mon.screenshot("boot-last")
                    if is_bordify(last):
                        desktop = last
                        break
                    time.sleep(5)
                if desktop is None:
                    print("Guest serial log tail:", SERIAL.read_text(errors="replace")[-6000:]
                          if SERIAL.exists() else "(no serial text)", file=sys.stderr)
                    raise AssertionError("Linux ISO never displayed the original Bordify desktop")
                print("Bordify Linux ISO actually booted into desktop:", desktop.size, flush=True)
                for key, label in (("f1", "files"), ("f2", "web"), ("f3", "settings")):
                    if key != "f1":
                        mon.command("sendkey esc")
                        time.sleep(2)
                    mon.command("sendkey " + key)
                    time.sleep(3)
                    current = mon.screenshot(label)
                    changed = changed_pixels(desktop, current)
                    if changed < 800:
                        raise AssertionError(f"QEMU keyboard {key} did not display the {label} window: {changed} pixels")
                    print(f"QEMU {key} launched {label}: {changed} different pixels", flush=True)
                print("QEMU real ISO, desktop and Files/web/Settings: PASS", flush=True)
        finally:
            vm.terminate()
            try:
                vm.wait(timeout=10)
            except subprocess.TimeoutExpired:
                vm.kill()
                vm.wait()
            MONITOR.unlink(missing_ok=True)

if __name__ == "__main__":
    run()
