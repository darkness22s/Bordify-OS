#!/usr/bin/env python3
"""Generate a minimal standalone Mach-O/ARM64 console *test program*.

The binary has a genuine MH_MAGIC_64, CPU_TYPE_ARM64, LC_SEGMENT_64
and LC_MAIN. Its two SVC #0 calls use a test-only Bordify guest ABI,
NOT Darwin/macOS syscalls or Cocoa. Does not depend on Apple SDKs.
"""
from pathlib import Path
import struct
import sys

BASE = 0x100000000
START = 0x100
MESSAGE_AT = 0x180
TEXT = b"BORDIFY ARM64: interpreted Mach-O demo under x86 QEMU\n"


def movz(rd, imm16):
    assert 0 <= imm16 < 65536
    return 0xD2800000 | (imm16 << 5) | rd


def adr(rd, address, current):
    offset = address - current
    assert -0x100000 <= offset < 0x100000
    offset &= 0x1FFFFF
    return 0x10000000 | ((offset & 3) << 29) | ((offset >> 2) << 5) | rd


def main(target):
    buf = bytearray(512)
    struct.pack_into("<IIIIIIII", buf, 0,
                     0xFEEDFACF, 0x0100000C, 0, 2, 2, 96, 0, 0)
    struct.pack_into("<II16sQQQQIIII", buf, 32,
                     0x19, 72, b"__TEXT".ljust(16, b"\0"),
                     BASE, 4096, 0, len(buf), 5, 5, 0, 0)
    struct.pack_into("<IIQQ", buf, 104, 0x80000028, 24, START, 0)

    instructions = (
        adr(0, BASE + MESSAGE_AT, BASE + START),
        movz(1, len(TEXT)),
        movz(8, 1),
        0xD4000001,  # svc 0, guest write
        movz(0, 0),
        movz(8, 2),
        0xD4000001,  # svc 0, guest exit(0)
    )
    struct.pack_into("<" + "I" * len(instructions), buf, START, *instructions)
    buf[MESSAGE_AT:MESSAGE_AT + len(TEXT)] = TEXT
    path = Path(target)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(buf)
    print(f"Generated {path}: ARM64 Mach-O demonstration binary")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: genarm64demo.py output.mch")
    main(sys.argv[1])
