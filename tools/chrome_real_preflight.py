#!/usr/bin/env python3
"""Record provenance and actual disk format for official Google Chrome builds.

A genuine PE32/PE32+ executable or UDIF DMG is required. This script
never executes downloaded code, decrypts content, or claims Bordify runs
Chrome. Generated JSON and Markdown are safe to upload; Google packages
themselves must not be redistributed as Actions artifacts.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

WIN_URL = "https://dl.google.com/chrome/install/ChromeStandaloneSetup64.exe"
MAC_URL = "https://dl.google.com/chrome/mac/universal/stable/GGRO/googlechrome.dmg"


def digest(path):
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def inspect_windows(path):
    size = path.stat().st_size
    with path.open("rb") as f:
        head = f.read(65536)
    if size < 512 or head[:2] != b"MZ":
        raise ValueError("Chrome Windows download is not an MZ executable")
    peoff = struct.unpack_from("<I", head, 0x3C)[0]
    if peoff + 26 > len(head) or head[peoff:peoff + 4] != b"PE\0\0":
        raise ValueError("Chrome Windows download lacks a valid PE header in first 64KiB")
    machine, sections = struct.unpack_from("<HH", head, peoff + 4)
    optional_size = struct.unpack_from("<H", head, peoff + 20)[0]
    magic = struct.unpack_from("<H", head, peoff + 24)[0]
    if magic not in (0x10B, 0x20B) or optional_size < 2:
        raise ValueError("Chrome Windows download has unexpected PE optional header")
    return {
        "filename": path.name,
        "source": WIN_URL,
        "size": size,
        "sha256": digest(path),
        "format": "PE32+" if magic == 0x20B else "PE32",
        "machine": f"0x{machine:04x}",
        "sections": sections,
        "bordify_pe32_ring3_limit": 64 * 1024,
        "bordify_amd64_interpreter_limit": 64 * 1024,
        "execution_expected": False,
        "reason": "Bordify supports only small, audited PE32/PE32+ console workloads and a handful of Win32 imports.",
    }


def inspect_macos(path):
    size = path.stat().st_size
    if size < 512:
        raise ValueError("Chrome macOS download is too small for a UDIF trailer")
    with path.open("rb") as f:
        f.seek(-512, 2)
        footer = f.read(512)
    if footer[:4] != b"koly":
        raise ValueError("Chrome macOS download is not a UDIF DMG (koly trailer absent)")
    version, trailer_size = struct.unpack_from(">II", footer, 4)
    meta_offset, meta_size = struct.unpack_from(">QQ", footer, 216)
    virtual_sectors = struct.unpack_from(">Q", footer, 492)[0]
    data_offset, data_size = struct.unpack_from(">QQ", footer, 24)
    proper_range = (
        version == 4 and trailer_size == 512
        and meta_size > 0 and meta_offset <= size - 512
        and meta_size <= size - 512 - meta_offset
        and data_offset <= size - 512
        and data_size <= size - 512 - data_offset
    )
    if not proper_range:
        raise ValueError("Chrome macOS DMG has unexpected or out-of-bounds UDIF metadata")
    metadata_prefix = b""
    with path.open("rb") as f:
        f.seek(meta_offset)
        metadata_prefix = f.read(min(meta_size, 128))
    return {
        "filename": path.name,
        "source": MAC_URL,
        "size": size,
        "sha256": digest(path),
        "format": "UDIF DMG",
        "koly_version": version,
        "metadata_length": meta_size,
        "metadata_encoding": "XML plist" if b"<?xml" in metadata_prefix else
                             "binary plist" if metadata_prefix.startswith(b"bplist") else
                             "other",
        "virtual_disk_bytes": virtual_sectors * 512,
        "bordify_xml_map_limit": 64 * 1024,
        "execution_expected": False,
        "reason": "Bordify cannot install or launch general APFS/HFS+ Chrome.app bundles or provide macOS dyld/Cocoa services.",
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--win", type=Path, required=True)
    parser.add_argument("--mac", type=Path, required=True)
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--markdown", type=Path, required=True)
    opts = parser.parse_args()

    windows = inspect_windows(opts.win)
    macos = inspect_macos(opts.mac)
    result = {"windows": windows, "macos": macos}
    opts.json.parent.mkdir(parents=True, exist_ok=True)
    opts.json.write_text(json.dumps(result, indent=2) + "\n")

    lines = [
        "## Real Google Chrome installers against Bordify OS",
        "",
        "This is an **experimental compatibility attempt**, not proof that Chrome runs.",
        "",
        "| Real downloaded package | Format | File size | Bordify result |",
        "|---|---|---:|---|",
        f"| Windows offline EXE | {windows['format']} / {windows['machine']} | "
        f"{windows['size']:,} bytes | Unsupported runtime (guest loader attempted) |",
        f"| macOS universal DMG | UDIF ({macos['metadata_encoding']}) | "
        f"{macos['size']:,} bytes | Unsupported app bundle / filesystem runtime |",
        "",
        f"**Windows SHA-256:** \`{windows['sha256']}\`",
        "",
        f"**macOS SHA-256:** \`{macos['sha256']}\`",
        "",
        "Only these checks are expected to pass: both downloads are genuine file formats, "
        "Bordify attempts its restricted loaders in QEMU, records why Chrome is rejected, "
        "and the OS still reaches its desktop.",
    ]
    opts.markdown.write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
