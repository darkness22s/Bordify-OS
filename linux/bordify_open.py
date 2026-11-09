#!/usr/bin/env python3
"""Bordify Linux edition: Wine, native apps, and non-executing DMG importer.

This executes untrusted programs with the user's normal privileges, NOT in a
security sandbox. Do not use it as a malware-testing environment.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

MACHO_MAGIC = {
    b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",
    b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
    b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
    b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca",
}
MSI_MAGIC = b"\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1"


class LaunchError(Exception):
    pass


def read_magic(path: Path, length: int = 8) -> bytes:
    try:
        with path.open("rb") as handle:
            return handle.read(length)
    except OSError as exc:
        raise LaunchError(str(exc)) from exc


def plan_launch(path: Path, *, available=shutil.which) -> tuple[list[str], dict[str, str]]:
    """Purely select a command; never invoke a shell with file data unquoted."""
    path = path.expanduser().resolve(strict=True)
    if not (path.is_file() or path.is_dir()):
        raise LaunchError("Not a regular file or directory")
    if path.suffix.lower() == ".pkg":
        raise LaunchError("macOS .pkg installers cannot be imported as Bordify apps. Use a .dmg containing a .app bundle.")
    if path.is_dir() and path.suffix.lower() == ".app":
        raise LaunchError("Direct macOS .app execution is disabled. Import the original DMG as a Bordify .bapp instead.")
    if path.is_dir():
        if not available("xdg-open"):
            raise LaunchError("xdg-open is required to open directories")
        return ["xdg-open", str(path)], {}
    ext = path.suffix.lower()
    magic = read_magic(path)
    if ext in {".exe", ".com"}:
        if not magic.startswith(b"MZ"):
            raise LaunchError("Windows executable has an invalid MZ header")
        if not available("wine"):
            raise LaunchError("Wine is not installed. Install wine + wine64; add wine32 for older 32-bit apps.")
        prefix = str(Path.home() / ".local" / "share" / "bordify" / "wineprefix")
        return ["wine", str(path)], {"WINEPREFIX": prefix}
    if ext == ".msi":
        if magic != MSI_MAGIC:
            raise LaunchError("MSI installer has an invalid compound-file header")
        if not available("wine"):
            raise LaunchError("Wine is not installed")
        prefix = str(Path.home() / ".local" / "share" / "bordify" / "wineprefix")
        return ["wine", "msiexec", "/i", str(path)], {"WINEPREFIX": prefix}
    if ext == ".dmg":
        return ["bordify-package", "import", str(path)], {}
    if ext == ".bapp":
        return ["bordify-package", "inspect", str(path)], {}
    if magic[:4] in MACHO_MAGIC:
        raise LaunchError("macOS Mach-O execution is disabled. A .bapp imports metadata and files, not macOS binary code.")
    if magic[:4] == b"\x7fELF" and os.access(path, os.X_OK):
        return [str(path)], {}
    if not available("xdg-open"):
        raise LaunchError("xdg-open is required to open this file")
    return ["xdg-open", str(path)], {}


def open_file(path: Path) -> int:
    try:
        command, overrides = plan_launch(path)
    except (LaunchError, OSError) as exc:
        error = str(exc)
        print("Bordify: " + error, file=sys.stderr)
        if os.environ.get("BORDIFY_TEST_NO_DIALOG") != "1" and shutil.which("zenity"):
            subprocess.run(["zenity", "--error", "--title=Bordify", "--text=" + error], check=False)
        return 2
    env = os.environ.copy()
    env.update(overrides)
    # Wine does not reliably create the parents of a custom WINEPREFIX.
    # Keep each Bordify Wine installation in its dedicated per-user prefix.
    if "WINEPREFIX" in overrides:
        try:
            Path(overrides["WINEPREFIX"]).parent.mkdir(parents=True, exist_ok=True)
        except OSError as exc:
            print("Bordify: cannot create Wine prefix directory: " + str(exc), file=sys.stderr)
            return 2
    try:
        return subprocess.run(command, env=env, check=False).returncode
    except OSError as exc:
        print("Bordify: unable to launch: " + str(exc), file=sys.stderr)
        return 127


def main() -> int:
    parser = argparse.ArgumentParser(description="Launch a file in Bordify OS Linux edition")
    parser.add_argument("path", type=Path, help="File or .app bundle to open")
    args = parser.parse_args()
    return open_file(args.path)


if __name__ == "__main__":
    raise SystemExit(main())
