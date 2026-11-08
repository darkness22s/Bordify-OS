#!/usr/bin/env python3
"""Build deterministic tiny UDIF fixtures for FAT16/QEMU integration.

--hfsplus contains a synthetic HFS+ volume *header* in the third logical
sector. Its catalog B-tree is intentionally absent: this is a filesystem
header recognition test, not a mountable macOS volume or an installer.
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


def make(output, compress=False, hfsplus=False):
    raw = bytearray(SECTOR_SIZE * (64 if hfsplus else 1))
    if hfsplus:
        # HFSPlusVolumeHeader is at byte 1024, in big-endian order.
        header = 2 * SECTOR_SIZE
        struct.pack_into(">HH", raw, header, 0x482B, 4)
        be32(raw, header + 4, 1 << 13)  # journaled
        be32(raw, header + 32, 7)        # file count
        be32(raw, header + 36, 2)        # folder count
        be32(raw, header + 40, 4096)     # allocation block bytes
        be32(raw, header + 44, 8)        # 8 blocks, 64 sectors
        be32(raw, header + 48, 3)        # free blocks
        be32(raw, header + 276, 8192)    # catalog logical bytes
        be32(raw, header + 284, 2)       # catalog fork total blocks
        be32(raw, header + 288, 2)       # catalog first extent start
        be32(raw, header + 292, 2)       # catalog first extent length
        # Synthetic B-tree header node at first catalog extent, block 2.
        node = 2 * 4096
        raw[node + 8] = 1               # header node kind
        struct.pack_into(">H", raw, node + 10, 3)  # three records
        struct.pack_into(">H", raw, node + 14, 1)  # depth
        be32(raw, node + 16, 1)         # root node
        be32(raw, node + 20, 1)         # leaf record count
        be32(raw, node + 24, 1)         # first leaf
        be32(raw, node + 28, 1)         # last leaf
        struct.pack_into(">H", raw, node + 32, 512)  # B-tree node size
        be32(raw, node + 36, 16)        # total nodes
        be32(raw, node + 40, 12)        # free nodes
        for at, value in ((510, 14), (508, 120), (506, 248), (504, 256)):
            struct.pack_into(">H", raw, node + at, value)
    else:
        marker = (b"BORDIFY_DMG_ZLIB_SECTOR_VERIFIED!" if compress
                  else b"BORDIFY_DMG_RAW_SECTOR_READ_OK!!")
        raw[:len(marker)] = marker
    payload = zlib.compress(raw, level=9) if compress else bytes(raw)
    block = bytearray(204 + 40 * 2)
    block[:4] = b"mish"
    be32(block, 4, 1)
    be64(block, 8, 0)   # partition starts at logical sector zero
    be64(block, 16, len(raw) // SECTOR_SIZE)  # logical disk sectors
    be64(block, 24, 0)  # data fork begins at byte zero
    be32(block, 200, 2)
    be32(block, 204, 0x80000005 if compress else 1)  # zlib or raw
    be64(block, 212, 0)  # first sector
    be64(block, 220, len(raw) // SECTOR_SIZE)  # run sector count
    be64(block, 228, 0)  # offset into data fork
    be64(block, 236, len(payload))
    be32(block, 244, 0xFFFFFFFF)  # end
    be64(block, 252, len(raw) // SECTOR_SIZE)  # next logical sector

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
    be64(footer, 492, len(raw) // SECTOR_SIZE)  # logical sectors

    out = Path(output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(payload + xml + footer)
    kind = "HFS+ header (not mountable)" if hfsplus else "sector marker"
    print(f"Generated {out} ({'zlib' if compress else 'raw'} UDIF, {kind}, {len(xml)}-byte blkx plist)")


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        raise SystemExit("usage: genudifdemo.py output.dmg [--zlib|--hfsplus]")
    option = sys.argv[2] if len(sys.argv) == 3 else ""
    if option not in ("", "--zlib", "--hfsplus"):
        raise SystemExit("unknown option")
    make(sys.argv[1], option in ("--zlib", "--hfsplus"),
         option == "--hfsplus")
