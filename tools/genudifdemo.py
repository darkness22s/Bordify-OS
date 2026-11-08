#!/usr/bin/env python3
"""Build a deterministic, *uncompressed*, read-only test UDIF DMG.

The 512-byte disk contains a marker. A 'mish' block map references that
raw sector. This exercises actual FAT16 offset reads and UDIF block-map
decoding in QEMU; it is NOT an HFS+/APFS installer disk.
"""
from pathlib import Path
import base64
import struct
import sys

SECTOR_SIZE = 512


def be32(buffer, offset, value):
    struct.pack_into(">I", buffer, offset, value)


def be64(buffer, offset, value):
    struct.pack_into(">Q", buffer, offset, value)


def make(output):
    raw = bytearray(SECTOR_SIZE)
    raw[:32] = b"BORDIFY_DMG_RAW_SECTOR_READ_OK!!"
    block = bytearray(204 + 40 * 2)
    block[:4] = b"mish"
    be32(block, 4, 1)
    be64(block, 8, 0)   # partition starts at logical sector zero
    be64(block, 16, 1)  # 1 disk sector
    be64(block, 24, 0)  # data fork begins at byte zero
    be32(block, 200, 2)
    be32(block, 204, 1)   # raw
    be64(block, 212, 0)  # first sector
    be64(block, 220, 1)  # sector count
    be64(block, 228, 0)  # offset into data fork
    be64(block, 236, 512)
    be32(block, 244, 0xFFFFFFFF)  # end
    be64(block, 252, 1)           # next logical sector

    encoded = base64.b64encode(block).decode("ascii")
    xml = ("<?xml version='1.0' encoding='UTF-8'?>\n"
           "<plist><dict><key>resource-fork</key><dict>"
           "<key>blkx</key><array><dict><key>Data</key><data>\n"
           + encoded +
           "\n</data></dict></array></dict></dict></plist>").encode("ascii")
    footer = bytearray(512)
    footer[:4] = b"koly"
    be32(footer, 4, 4)        # version
    be32(footer, 8, 512)      # koly header size
    be64(footer, 24, 0)       # data fork offset
    be64(footer, 32, 512)     # data fork size
    be64(footer, 216, len(raw))  # XML offset
    be64(footer, 224, len(xml))  # XML size
    be64(footer, 492, 1)      # logical sector count

    out = Path(output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(raw + xml + footer)
    print(f"Generated {out} (uncompressed UDIF raw sector, {len(xml)}-byte blkx plist)")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: genudifdemo.py output.dmg")
    make(sys.argv[1])
