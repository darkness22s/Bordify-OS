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
import zlib

SECTOR_SIZE = 512


def be32(buffer, offset, value):
    struct.pack_into(">I", buffer, offset, value)


def be64(buffer, offset, value):
    struct.pack_into(">Q", buffer, offset, value)


def make(output, compress=False):
    raw = bytearray(SECTOR_SIZE)
    marker = (b"BORDIFY_DMG_ZLIB_SECTOR_VERIFIED!" if compress
              else b"BORDIFY_DMG_RAW_SECTOR_READ_OK!!")
    raw[:len(marker)] = marker
    payload = zlib.compress(raw, level=9) if compress else bytes(raw)
    block = bytearray(204 + 40 * 2)
    block[:4] = b"mish"
    be32(block, 4, 1)
    be64(block, 8, 0)   # partition starts at logical sector zero
    be64(block, 16, 1)  # 1 disk sector
    be64(block, 24, 0)  # data fork begins at byte zero
    be32(block, 200, 2)
    be32(block, 204, 0x80000005 if compress else 1)  # zlib or raw
    be64(block, 212, 0)  # first sector
    be64(block, 220, 1)  # sector count
    be64(block, 228, 0)  # offset into data fork
    be64(block, 236, len(payload))
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
    be64(footer, 32, len(payload))     # data fork size
    be64(footer, 216, len(payload))  # XML offset
    be64(footer, 224, len(xml))  # XML size
    be64(footer, 492, 1)      # logical sector count

    out = Path(output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(payload + xml + footer)
    print(f"Generated {out} ({'zlib' if compress else 'raw'} UDIF sector, {len(xml)}-byte blkx plist)")


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        raise SystemExit("usage: genudifdemo.py output.dmg [--zlib]")
    if len(sys.argv) == 3 and sys.argv[2] != "--zlib":
        raise SystemExit("unknown option")
    make(sys.argv[1], len(sys.argv) == 3)
