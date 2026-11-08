#!/usr/bin/env python3
"""Malformed external ARM64 Mach-O relocation and load-command regressions.

Uses the REAL independently Clang-assembled cocoa.o produced in CI.
The loader must reject inconsistent sizeofcmds and overlapping relocation
records before it produces an executable for Bordify.
"""
from pathlib import Path
import struct
import sys
import tempfile

sys.path.insert(0, "tools")
from linkcocoaobj import main as link_cocoa  # noqa: E402


def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def rejected(source, tempdir, title):
    file = Path(tempdir) / (title + ".o")
    file.write_bytes(source)
    try:
        link_cocoa(str(file), str(Path(tempdir) / (title + ".mch")))
    except (ValueError, IndexError, struct.error):
        return
    raise AssertionError(f"Malformed Mach-O accepted: {title}")


def main():
    obj = bytearray(Path("build/apps/cocoa.o").read_bytes())
    assert u32(obj, 0) == 0xFEEDFACF
    with tempfile.TemporaryDirectory(prefix="bordify-macho-test-") as root:
        result = Path(root) / "valid.mch"
        link_cocoa("build/apps/cocoa.o", str(result))
        assert result.read_bytes() == Path("build/apps/cocoa.mch").read_bytes()

        truncated = bytearray(obj)
        struct.pack_into("<I", truncated, 20, 8)
        rejected(truncated, root, "short-command-region")

        oversized = bytearray(obj)
        struct.pack_into("<I", oversized, 16, 12)
        struct.pack_into("<I", oversized, 20, 96)
        rejected(oversized, root, "bad-command-count")

        extra = bytearray(obj)
        struct.pack_into("<I", extra, 20, u32(extra, 20) + 8)
        rejected(extra, root, "unaccounted-command-bytes")

        # Read relocation array from the only validated __TEXT,__text
        # section, not from an untrusted hard-coded file offset.
        section = 32 + 72
        reloc = u32(obj, section + 56)
        count = u32(obj, section + 60)
        assert 3 <= count <= 64
        assert reloc + count * 8 <= len(obj)
        duplicated = bytearray(obj)
        duplicated[reloc + 8:reloc + 12] = obj[reloc:reloc + 4]
        rejected(duplicated, root, "overlapping-branch-relocations")

        outside = bytearray(obj)
        code_length = struct.unpack_from("<Q", obj, section + 40)[0]
        struct.pack_into("<i", outside, reloc, int(code_length))
        rejected(outside, root, "relocation-outside-text")

        print("Apple ARM64 Mach-O load-command and relocation rejection: PASS")


if __name__ == "__main__":
    main()
