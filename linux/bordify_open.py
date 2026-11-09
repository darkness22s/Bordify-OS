#!/usr/bin/env python3
"""Bordify Linux edition: explicit Wine/Darling/native application dispatcher.

This executes untrusted programs with the user's normal privileges, NOT in a
security sandbox. Do not use it as a malware-testing environment.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import plistlib
import shlex
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


def resolve_macos_bundle(bundle: Path) -> Path:
    """Do not trust CFBundleExecutable as an arbitrary path."""
    info_path = bundle / "Contents" / "Info.plist"
    try:
        with info_path.open("rb") as handle:
            info = plistlib.load(handle)
    except (OSError, ValueError, TypeError, plistlib.InvalidFileException) as exc:
        raise LaunchError("Invalid macOS .app bundle Info.plist") from exc
    name = info.get("CFBundleExecutable")
    if not isinstance(name, str) or not name or name in {".", ".."}:
        raise LaunchError("macOS bundle has no safe CFBundleExecutable")
    if "/" in name or "\\" in name or "\x00" in name:
        raise LaunchError("Invalid macOS bundle executable path")
    contents = (bundle / "Contents" / "MacOS").resolve()
    binary = (contents / name).resolve()
    if binary.parent != contents or not binary.is_file():
        raise LaunchError("macOS bundle executable missing or points outside Contents/MacOS")
    if read_magic(binary, 4) not in MACHO_MAGIC:
        raise LaunchError("Bundle executable is not a Mach-O program")
    return binary


def plan_launch(path: Path, *, available=shutil.which) -> tuple[list[str], dict[str, str]]:
    """Purely select a command; never invoke a shell with file data unquoted."""
    path = path.expanduser().resolve(strict=True)
    if not (path.is_file() or path.is_dir()):
        raise LaunchError("Not a regular file or directory")
    if path.is_dir() and path.suffix.lower() == ".app":
        path = resolve_macos_bundle(path)
    elif path.is_dir():
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
        if not available("darling"):
            raise LaunchError("A DMG is an installer image, not an app. Install Darling to try mounting it.")
        inside = "/Volumes/SystemRoot" + str(path)
        return ["darling", "shell", "hdiutil attach " + shlex.quote(inside)], {}
    if magic[:4] in MACHO_MAGIC:
        if not available("darling"):
            raise LaunchError("Darling is not installed. macOS application support is experimental.")
        # Darling translates host paths under /Volumes/SystemRoot. The darling
        # shell accepts a sh -c string; quote ALL user-controlled path content.
        inside = "/Volumes/SystemRoot" + str(path)
        return ["darling", "shell", "exec " + shlex.quote(inside)], {}
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
