#!/usr/bin/env python3
"""Generate an intentionally tiny Win32 console PE32 program.

This uses a real PE32 import directory and Win32 stdcall ABI, importing
GetStdHandle, WriteFile and ExitProcess from kernel32.dll. Its only
dependency is Bordify's small Win32 compatibility shim. NOT x64.
"""
from pathlib import Path
import struct
import sys

BASE = 0x40000000
FILE_ALIGNMENT = 0x200
SECTION_RVA = 0x1000
SECTION_RAW = 0x200


def set16(b, where, value):
    struct.pack_into("<H", b, where, value)


def set32(b, where, value):
    struct.pack_into("<I", b, where, value)


def main(output):
    out = bytearray(0x600)
    out[:2] = b"MZ"
    set32(out, 0x3C, 0x80)
    pe = 0x80
    out[pe:pe + 4] = b"PE\0\0"
    set16(out, pe + 4, 0x014C)  # IMAGE_FILE_MACHINE_I386
    set16(out, pe + 6, 1)       # one section
    set16(out, pe + 20, 224)    # sizeof PE32 optional header
    set16(out, pe + 22, 0x0102)  # executable, 32-bit
    opt = pe + 24
    set16(out, opt, 0x10B)
    set32(out, opt + 16, 0x1000)   # AddressOfEntryPoint
    set32(out, opt + 20, 0x1000)   # BaseOfCode
    set32(out, opt + 24, 0x1000)   # BaseOfData
    set32(out, opt + 28, BASE)
    set32(out, opt + 32, 0x1000)   # SectionAlignment
    set32(out, opt + 36, FILE_ALIGNMENT)
    set16(out, opt + 40, 4)        # MajorOperatingSystemVersion
    set16(out, opt + 48, 4)        # MajorSubsystemVersion
    set32(out, opt + 56, 0x2000)   # SizeOfImage
    set32(out, opt + 60, 0x200)    # SizeOfHeaders
    set16(out, opt + 68, 3)        # Windows CUI console
    set32(out, opt + 72, 0x4000)   # StackReserve
    set32(out, opt + 76, 0x1000)   # StackCommit
    set32(out, opt + 80, 0x4000)   # HeapReserve
    set32(out, opt + 84, 0x1000)   # HeapCommit
    set32(out, opt + 92, 16)       # NumberOfRvaAndSizes
    set32(out, opt + 104, 0x1200)  # IMAGE_DIRECTORY_ENTRY_IMPORT RVA
    set32(out, opt + 108, 40)
    sec = opt + 224
    out[sec:sec + 8] = b".text\0\0\0"
    set32(out, sec + 8, 0x400)     # VirtualSize
    set32(out, sec + 12, SECTION_RVA)
    set32(out, sec + 16, 0x400)    # SizeOfRawData
    set32(out, sec + 20, SECTION_RAW)
    set32(out, sec + 36, 0xE0000020)  # executable + readable + writable code

    def raw(rva):
        assert SECTION_RVA <= rva < SECTION_RVA + 0x400
        return SECTION_RAW + rva - SECTION_RVA

    # Import descriptor plus 20-byte zero terminator.
    set32(out, raw(0x1200), 0x1260)       # OriginalFirstThunk
    set32(out, raw(0x1200) + 12, 0x1240)  # Name
    set32(out, raw(0x1200) + 16, 0x1280)  # FirstThunk
    out[raw(0x1240):raw(0x1240) + 13] = b"kernel32.dll\0"
    for index, rva in enumerate((0x12A0, 0x12B0, 0x12C0)):
        set32(out, raw(0x1260) + index * 4, rva)
        set32(out, raw(0x1280) + index * 4, rva)
    for rva, name in ((0x12A0, b"WriteFile\0"),
                      (0x12B0, b"GetStdHandle\0"),
                      (0x12C0, b"ExitProcess\0")):
        out[raw(rva):raw(rva) + 2] = b"\x00\x00"  # hint
        out[raw(rva) + 2:raw(rva) + 2 + len(name)] = name

    message = b"BORDIFY WIN32: WriteFile through kernel32 compatibility layer\r\n"
    out[raw(0x12D0):raw(0x12D0) + len(message)] = message

    # Win32 stdcall: STD_OUTPUT_HANDLE -> GetStdHandle; WriteFile; ExitProcess.
    code = bytearray()
    code += b"\x68" + struct.pack("<I", 0xFFFFFFF5)       # push -11
    code += b"\xff\x15" + struct.pack("<I", BASE + 0x1284)
    code += b"\x89\xc6"                                  # mov esi,eax
    code += b"\x6a\x00"                                  # overlapped=NULL
    code += b"\x6a\x00"                                  # written=NULL
    code += b"\x68" + struct.pack("<I", len(message))
    code += b"\x68" + struct.pack("<I", BASE + 0x12D0)
    code += b"\x56"                                      # push esi
    code += b"\xff\x15" + struct.pack("<I", BASE + 0x1280)
    code += b"\x6a\x00"                                  # exit status=0
    code += b"\xff\x15" + struct.pack("<I", BASE + 0x1288)
    code += b"\x0f\x0b"                                  # ud2 if ExitProcess returns
    out[raw(0x1000):raw(0x1000) + len(code)] = code
    path = Path(output)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(out)
    print(f"Generated {path}: PE32 console, 3 kernel32 imports, {len(out)} bytes")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: genwin32demo.py output.exe")
    main(sys.argv[1])
