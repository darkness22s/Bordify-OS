#!/usr/bin/env python3
"""Wrap a REAL clang-assembled ARM64 Mach-O object in a simple MH_EXECUTE.

The tiny Bordify interpreter accepts one __TEXT segment and LC_MAIN,
no dyld and no relocations. Extract a reloc-free __TEXT,__text section
from a cross-assembled Mach-O object, preserving its real instruction
bytes. Fail on unresolved linker relocation or unexpected sections.
"""
from pathlib import Path
import struct
import sys

BASE = 0x100000000
ENTRY_OFFSET = 0x100
MAX_FILE = 4096


def u32(data, at):
    return struct.unpack_from("<I", data, at)[0]


def wrap(obj_path, out_path):
    obj = Path(obj_path).read_bytes()
    if len(obj) < 32 or u32(obj, 0) != 0xFEEDFACF or \
       u32(obj, 4) != 0x0100000C or u32(obj, 12) != 1:
        raise ValueError("input must be a Mach-O ARM64 MH_OBJECT")
    commands = u32(obj, 16)
    command_bytes = u32(obj, 20)
    if not 1 <= commands <= 12 or 32 + command_bytes > len(obj):
        raise ValueError("malformed Mach-O load command range")
    at = 32
    code = None
    for _ in range(commands):
        if at + 8 > len(obj):
            raise ValueError("truncated command header")
        cmd, length = struct.unpack_from("<II", obj, at)
        if length < 8 or at + length > 32 + command_bytes:
            raise ValueError("invalid command size")
        if cmd == 0x19:
            if length < 72:
                raise ValueError("short LC_SEGMENT_64")
            nsects = u32(obj, at + 64)
            if length < 72 + nsects * 80:
                raise ValueError("short section table")
            for section in range(nsects):
                off = at + 72 + section * 80
                name, segment = struct.unpack_from("<16s16s", obj, off)
                length_bytes = struct.unpack_from("<Q", obj, off + 40)[0]
                file_offset = u32(obj, off + 48)
                reloc_count = u32(obj, off + 60)
                if name.rstrip(b"\0") == b"__text" and \
                   segment.rstrip(b"\0") == b"__TEXT":
                    if code is not None or reloc_count or \
                       file_offset + length_bytes > len(obj):
                        raise ValueError("multiple/relocating/invalid __text sections")
                    code = obj[file_offset:file_offset + length_bytes]
        at += length
    if code is None or len(code) < 32 or \
       len(code) > MAX_FILE - ENTRY_OFFSET:
        raise ValueError("expected a self-contained <= 3840-byte text section")
    length = ((ENTRY_OFFSET + len(code) + 511) // 512) * 512
    result = bytearray(length)
    struct.pack_into("<IIIIIIII", result, 0,
                     0xFEEDFACF, 0x0100000C, 0, 2, 2, 96, 0, 0)
    struct.pack_into("<II16sQQQQIIII", result, 32,
                     0x19, 72, b"__TEXT".ljust(16, b"\0"),
                     BASE, 4096, 0, len(result), 5, 5, 0, 0)
    struct.pack_into("<IIQQ", result, 104,
                     0x80000028, 24, ENTRY_OFFSET, 0)
    result[ENTRY_OFFSET:ENTRY_OFFSET + len(code)] = code
    path = Path(out_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(result)
    print(f"Wrapped {obj_path} -> {path}: {len(code)} bytes of "
          "clang-assembled ARM64 machine code (no relocations)")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: wraparm64obj.py input.o output.mch")
    wrap(sys.argv[1], sys.argv[2])
